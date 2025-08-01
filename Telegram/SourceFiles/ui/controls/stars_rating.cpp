/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/stars_rating.h"

#include "info/profile/info_profile_icon.h"
#include "lang/lang_keys.h"
#include "ui/effects/premium_bubble.h"
#include "ui/effects/premium_graphics.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/text/custom_emoji_helper.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/tooltip.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "styles/style_info.h"
#include "styles/style_info_levels.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"
#include "styles/style_media_view.h"
#include "styles/style_menu_icons.h"

namespace Ui {
namespace {

constexpr auto kAutoCollapseTimeout = 4 * crl::time(1000);

using Counters = Data::StarsRating;

struct Feature {
	const style::icon &icon;
	QString title;
	TextWithEntities about;
};

[[nodiscard]] object_ptr<Ui::RpWidget> MakeFeature(
		QWidget *parent,
		Feature feature,
		const Text::MarkedContext &context) {
	auto result = object_ptr<Ui::PaddingWrap<>>(
		parent,
		object_ptr<Ui::RpWidget>(parent),
		st::infoStarsFeatureMargin);
	const auto widget = result->entity();
	const auto icon = Ui::CreateChild<Info::Profile::FloatingIcon>(
		widget,
		feature.icon,
		st::infoStarsFeatureIconPosition);
	const auto title = Ui::CreateChild<Ui::FlatLabel>(
		widget,
		feature.title,
		st::infoStarsFeatureTitle);
	const auto about = Ui::CreateChild<Ui::FlatLabel>(
		widget,
		rpl::single(feature.about),
		st::infoStarsFeatureAbout,
		st::defaultPopupMenu,
		context);
	icon->show();
	title->show();
	about->show();
	widget->widthValue(
	) | rpl::start_with_next([=](int width) {
		const auto left = st::infoStarsFeatureLabelLeft;
		const auto available = width - left;
		title->resizeToWidth(available);
		about->resizeToWidth(available);
		auto top = 0;
		title->move(left, top);
		top += title->height() + st::infoStarsFeatureSkip;
		about->move(left, top);
		top += about->height();
		widget->resize(width, top);
	}, widget->lifetime());
	return result;
}

[[nodiscard]] Fn<QImage()> CustomEmojiBadgeFactory(
		const QString &text,
		const style::color &bg,
		const style::color &fg) {
	return [=] {
		auto string = Ui::Text::String(
			st::settingsPremiumNewBadge.style,
			text.toUpper());
		const auto size = QSize(string.maxWidth(), string.minHeight());
		const auto padding = st::settingsPremiumNewBadgePadding;
		const auto full = size.grownBy(padding);
		const auto ratio = style::DevicePixelRatio();

		auto result = QImage(
			full * ratio,
			QImage::Format_ARGB32_Premultiplied);
		result.setDevicePixelRatio(ratio);
		result.fill(Qt::transparent);

		auto p = QPainter(&result);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(bg);

		const auto r = padding.left();
		p.drawRoundedRect(0, 0, full.width(), full.height(), r, r);

		p.setPen(fg);
		string.draw(p, { .position = { padding.left(), padding.top() } });

		p.end();
		return result;
	};
}

[[nodiscard]] Counters AdjustByReached(Counters data) {
	if (data.stars < 0) {
		return data;
	}
	const auto reached = !data.nextLevelStars;
	if (reached) {
		--data.level;
		data.stars = data.nextLevelStars = std::max({
			data.stars,
			data.thisLevelStars,
			1
		});
		data.thisLevelStars = 0;
	} else {
		data.stars = std::max(data.thisLevelStars, data.stars);
		data.nextLevelStars = std::max(
			data.nextLevelStars,
			data.stars + 1);
	}
	return data;
}

[[nodiscard]] Fn<QString(int)> BubbleTextFactory(int countForScale) {
	return [=](int count) {
		return (countForScale < 10'000)
			? QString::number(count)
			: (countForScale < 10'000'000)
			? (QString::number((count / 100) / 10.) + 'K')
			: (QString::number((count / 100'000) / 10.) + 'M');
	};
}

void FillRatingLimit(
		rpl::producer<> showFinished,
		not_null<VerticalLayout*> container,
		rpl::producer<Counters> data,
		style::margins limitLinePadding,
		int starsForScale) {
	const auto addSkip = [&](int skip) {
		container->add(object_ptr<Ui::FixedHeightWidget>(container, skip));
	};

	const auto ratio = [=](Counters rating) {
		const auto min = rating.thisLevelStars;
		const auto max = rating.nextLevelStars;

		Assert(rating.stars >= min && rating.stars <= max);
		const auto count = (max - min);
		const auto index = (rating.stars - min);
		if (!index) {
			return 0.;
		} else if (index == count) {
			return 1.;
		} else if (count == 2) {
			return 0.5;
		}
		const auto available = st::boxWideWidth
			- st::boxPadding.left()
			- st::boxPadding.right();
		const auto average = available / float64(count);
		const auto levelWidth = [&](int add) {
			return st::normalFont->width(
				tr::lng_boost_level(
					tr::now,
					lt_count,
					rating.level + add));
		};
		const auto paddings = 2 * st::premiumLineTextSkip;
		const auto labelLeftWidth = paddings + levelWidth(0);
		const auto labelRightWidth = paddings + levelWidth(1);
		const auto first = std::max(average, labelLeftWidth * 1.);
		const auto last = std::max(average, labelRightWidth * 1.);
		const auto other = (available - first - last) / (count - 2);
		return (first + (index - 1) * other) / available;
	};

	auto adjustedData = rpl::duplicate(data) | rpl::map(AdjustByReached);

	auto bubbleRowState = rpl::duplicate(
		adjustedData
	) | rpl::combine_previous(
		Counters()
	) | rpl::map([=](Counters previous, Counters counters) {
		return Premium::BubbleRowState{
			.counter = counters.stars,
			.ratio = ratio(counters),
			.animateFromZero = (counters.level != previous.level),
			.dynamic = true,
		};
	});
	Premium::AddBubbleRow(
		container,
		st::boostBubble,
		std::move(showFinished),
		rpl::duplicate(bubbleRowState),
		Premium::BubbleType::StarRating,
		BubbleTextFactory(starsForScale),
		&st::infoStarsCrown,
		limitLinePadding);
	addSkip(st::premiumLineTextSkip);

	const auto level = [](int level) {
		return tr::lng_boost_level(tr::now, lt_count, level);
	};
	auto limitState = std::move(
		bubbleRowState
	) | rpl::map([](const Premium::BubbleRowState &state) {
		return Premium::LimitRowState{
			.ratio = state.ratio,
			.animateFromZero = state.animateFromZero,
			.dynamic = state.dynamic
		};
	});
	auto left = rpl::duplicate(
		adjustedData
	) | rpl::map([=](Counters counters) {
		return level(counters.level);
	});
	auto right = rpl::duplicate(
		adjustedData
	) | rpl::map([=](Counters counters) {
		return level(counters.level + 1);
	});
	Premium::AddLimitRow(
		container,
		st::boostLimits,
		Premium::LimitRowLabels{
			.leftLabel = std::move(left),
			.rightLabel = std::move(right),
			.activeLineBg = [=] { return st::windowBgActive->b; },
		},
		std::move(limitState),
		limitLinePadding);
}

void AboutRatingBox(
		not_null<GenericBox*> box,
		const QString &name,
		Counters data) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::boostBox);

	struct State {
		rpl::variable<Counters> data;
		rpl::variable<bool> full;
	};
	const auto state = box->lifetime().make_state<State>();
	state->data = std::move(data);

	FillRatingLimit(
		BoxShowFinishes(box),
		box->verticalLayout(),
		state->data.value(),
		st::boxRowPadding,
		data.stars);

	box->setMaxHeight(st::boostBoxMaxHeight);

	auto title = tr::lng_stars_rating_title();;

	auto text = !name.isEmpty()
		? tr::lng_stars_rating_about(
			lt_name,
			rpl::single(TextWithEntities{ name }),
			Ui::Text::RichLangValue) | rpl::type_erased()
		: tr::lng_stars_rating_about_your(
			Ui::Text::RichLangValue) | rpl::type_erased();

	box->addRow(
		object_ptr<Ui::FlatLabel>(box, std::move(title), st::infoStarsTitle),
		st::boxRowPadding + QMargins(0, st::boostTitleSkip / 2, 0, 0));

	const auto aboutLabel = box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			std::move(text),
			st::boostText),
		(st::boxRowPadding
			+ QMargins(0, st::boostTextSkip, 0, st::boostBottomSkip)));
	aboutLabel->setTryMakeSimilarLines(true);

	auto helper = Ui::Text::CustomEmojiHelper();
	const auto makeBadge = [&](
			const QString &text,
			const style::color &bg,
			const style::color &fg) {
		return helper.paletteDependent(
			CustomEmojiBadgeFactory(text, bg, fg),
			st::badgeEmojiMargin);
	};
	const auto makeActive = [&](const QString &text) {
		return makeBadge(text, st::windowBgActive, st::windowFgActive);
	};
	const auto makeInactive = [&](const QString &text) {
		return makeBadge(text, st::windowSubTextFg, st::windowFgActive);
	};
	const auto features = std::vector<Feature>{
		{
			st::menuIconRatingGifts,
			tr::lng_stars_title_gifts_telegram(tr::now),
			tr::lng_stars_about_gifts_telegram(
				tr::now,
				lt_emoji,
				makeActive(tr::lng_stars_rating_added(tr::now)),
				Ui::Text::RichLangValue),
		},
		{
			st::menuIconRatingUsers,
			tr::lng_stars_title_gifts_users(tr::now),
			tr::lng_stars_about_gifts_users(
				tr::now,
				lt_emoji,
				makeActive(tr::lng_stars_rating_added(tr::now)),
				Ui::Text::RichLangValue),
		},
		{
			st::menuIconRatingRefund,
			tr::lng_stars_title_refunds(tr::now),
			tr::lng_stars_about_refunds(
				tr::now,
				lt_emoji,
				makeInactive(tr::lng_stars_rating_deducted(tr::now)),
				Ui::Text::RichLangValue),
		},
	};
	const auto context = helper.context();
	for (const auto &feature : features) {
		box->addRow(MakeFeature(box, feature, context));
	}
	box->addButton(rpl::single(QString()), [=] {
		box->closeBox();
	})->setText(rpl::single(Ui::Text::IconEmoji(
		&st::infoStarsUnderstood
	).append(' ').append(tr::lng_stars_rating_understood(tr::now))));
}

[[nodiscard]] not_null<const style::LevelShape*> SelectShape(int level) {
	struct Shape {
		int level = 0;
		not_null<const style::LevelShape*> shape;
	};
	const auto list = std::vector<Shape>{
		{ 1, &st::level1 },
		{ 2, &st::level2 },
		{ 3, &st::level3 },
		{ 4, &st::level4 },
		{ 5, &st::level5 },
		{ 6, &st::level6 },
		{ 7, &st::level7 },
		{ 8, &st::level8 },
		{ 9, &st::level9 },
		{ 10, &st::level10 },
		{ 20, &st::level20 },
		{ 30, &st::level30 },
		{ 40, &st::level40 },
		{ 50, &st::level50 },
		{ 60, &st::level60 },
		{ 70, &st::level70 },
		{ 80, &st::level80 },
		{ 90, &st::level90 },
	};
	const auto i = ranges::lower_bound(
		list,
		level + 1,
		ranges::less(),
		&Shape::level);
	return (i != begin(list)) ? (i - 1)->shape : list.front().shape;
}

} // namespace

StarsRating::StarsRating(
	QWidget *parent,
	std::shared_ptr<Ui::Show> show,
	const QString &name,
	rpl::producer<Counters> value)
: _widget(std::make_unique<Ui::AbstractButton>(parent))
, _show(std::move(show))
, _name(name)
, _value(std::move(value)) {
	init();
}

StarsRating::~StarsRating() = default;

void StarsRating::init() {
	_widget->setPointerCursor(true);

	_widget->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(_widget.get());
		paint(p);
	}, lifetime());

	_widget->setClickedCallback([=] {
		if (!_value.current()) {
			return;
		}
		_show->show(Box(AboutRatingBox, _name, _value.current()));
	});

	_widget->resize(_widget->width(), st::level1.icon.height());

	_value.value() | rpl::start_with_next([=](Counters rating) {
		updateData(rating);
	}, lifetime());
}

void StarsRating::updateData(Data::StarsRating rating) {
	if (!rating) {
		_shape = nullptr;
		_widthValue = 0;
	} else {
		_shape = SelectShape(rating.level);
		_collapsedText.setText(
			st::levelStyle,
			Lang::FormatCountDecimal(rating.level));
		_widthValue = _shape->icon.width() - st::levelMargin.left();
	}
	updateWidth();
}

void StarsRating::updateWidth() {
	if (const auto widthToRight = _widthValue.current()) {
		const auto &margin = st::levelMargin;
		_widget->resize(margin.left() + widthToRight, _widget->height());
		_widget->update();
	} else {
		_widget->resize(0, _widget->height());
	}
}

void StarsRating::raise() {
	_widget->raise();
}

void StarsRating::moveTo(int x, int y) {
	_widget->move(x - st::levelMargin.left(), y - st::levelMargin.top());
}

void StarsRating::paint(QPainter &p) {
	if (!_shape) {
		return;
	}
	_shape->icon.paint(p, 0, 0, _widget->width());

	const auto x = (_widget->width() - _collapsedText.maxWidth()) / 2;
	p.setPen(st::levelTextFg);
	_collapsedText.draw(p, {
		.position = QPoint(x, 0) + _shape->position,
		.availableWidth = _collapsedText.maxWidth(),
	});
}

rpl::producer<int> StarsRating::widthValue() const {
	return _widthValue.value();
}

rpl::lifetime &StarsRating::lifetime() {
	return _widget->lifetime();
}

} // namespace Ui
