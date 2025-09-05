/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "payments/ui/payments_reaction_box.h"

#include "base/qt/qt_compare.h"
#include "lang/lang_keys.h"
#include "ui/boxes/boost_box.h" // MakeBoostFeaturesBadge.
#include "ui/controls/who_reacted_context_action.h"
#include "ui/effects/premium_bubble.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/dynamic_image.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_credits.h"
#include "styles/style_layers.h"
#include "styles/style_media_player.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"

namespace Settings {
[[nodiscard]] not_null<Ui::RpWidget*> AddBalanceWidget(
	not_null<Ui::RpWidget*> parent,
	not_null<Main::Session*> session,
	rpl::producer<CreditsAmount> balanceValue,
	bool rightAlign,
	rpl::producer<float64> opacityValue = nullptr);
} // namespace Settings

namespace Ui {
namespace {

constexpr auto kMaxTopPaidShown = 3;

struct TopReactorKey {
	std::shared_ptr<DynamicImage> photo;
	int count = 0;
	QString name;

	friend inline auto operator<=>(
		const TopReactorKey &,
		const TopReactorKey &) = default;
	friend inline bool operator==(
		const TopReactorKey &,
		const TopReactorKey &) = default;
};

struct Discreter {
	Fn<int(float64)> ratioToValue;
	Fn<float64(int)> valueToRatio;
};

[[nodiscard]] Discreter DiscreterForMax(int max) {
	Expects(max >= 2);

	// 1/8 of width is 1..10
	// 1/3 of width is 1..100
	// 2/3 of width is 1..1000

	auto thresholds = base::flat_map<float64, int>();
	thresholds.emplace(0., 1);
	if (max <= 40) {
		thresholds.emplace(1., max);
	} else if (max <= 300) {
		thresholds.emplace(1. / 4, 10);
		thresholds.emplace(1., max);
	} else if (max <= 600) {
		thresholds.emplace(1. / 8, 10);
		thresholds.emplace(1. / 2, 100);
		thresholds.emplace(1., max);
	} else if (max <= 1900) {
		thresholds.emplace(1. / 8, 10);
		thresholds.emplace(1. / 3, 100);
		thresholds.emplace(1., max);
	} else {
		thresholds.emplace(1. / 8, 10);
		thresholds.emplace(1. / 3, 100);
		thresholds.emplace(2. / 3, 1000);
		thresholds.emplace(1., max);
	}

	const auto ratioToValue = [=](float64 ratio) {
		ratio = std::clamp(ratio, 0., 1.);
		const auto j = thresholds.lower_bound(ratio);
		if (j == begin(thresholds)) {
			return 1;
		}
		const auto i = j - 1;
		const auto progress = (ratio - i->first) / (j->first - i->first);
		const auto value = i->second + (j->second - i->second) * progress;
		return int(base::SafeRound(value));
	};
	const auto valueToRatio = [=](int value) {
		value = std::clamp(value, 1, max);
		auto i = begin(thresholds);
		auto j = i + 1;
		while (j->second < value) {
			i = j++;
		}
		const auto progress = (value - i->second)
			/ float64(j->second - i->second);
		return i->first + (j->first - i->first) * progress;
	};
	return {
		.ratioToValue = ratioToValue,
		.valueToRatio = valueToRatio,
	};
}

void PaidReactionSlider(
		not_null<VerticalLayout*> container,
		int current,
		int max,
		Fn<void(int)> changed) {
	Expects(current >= 1 && current <= max);

	const auto slider = container->add(
		object_ptr<MediaSlider>(container, st::paidReactSlider),
		st::boxRowPadding + QMargins(0, st::paidReactSliderTop, 0, 0));
	slider->resize(slider->width(), st::paidReactSlider.seekSize.height());

	const auto discreter = DiscreterForMax(max);
	slider->setAlwaysDisplayMarker(true);
	slider->setDirection(ContinuousSlider::Direction::Horizontal);
	slider->setValue(discreter.valueToRatio(current));
	slider->setAdjustCallback([=](float64 ratio) {
		return discreter.valueToRatio(discreter.ratioToValue(ratio));
	});
	const auto ratioToValue = discreter.ratioToValue;
	slider->setChangeProgressCallback([=](float64 value) {
		changed(ratioToValue(value));
	});
	slider->setChangeFinishedCallback([=](float64 value) {
		changed(ratioToValue(value));
	});
}

[[nodiscard]] QImage GenerateBadgeImage(int count) {
	return GenerateSmallBadgeImage(
		Lang::FormatCountDecimal(count),
		st::paidReactTopStarIcon,
		st::creditsBg3->c,
		st::premiumButtonFg->c);
}

void AddArrowDown(not_null<RpWidget*> widget) {
	const auto arrow = CreateChild<RpWidget>(widget);
	const auto icon = &st::paidReactChannelArrow;
	const auto skip = st::lineWidth * 4;
	const auto size = icon->width() + skip * 2;
	arrow->resize(size, size);
	widget->widthValue() | rpl::start_with_next([=](int width) {
		const auto left = (width - st::paidReactTopUserpic) / 2;
		arrow->moveToRight(left - skip, -st::lineWidth, width);
	}, widget->lifetime());
	arrow->paintRequest() | rpl::start_with_next([=] {
		Painter p(arrow);
		auto hq = PainterHighQualityEnabler(p);
		p.setBrush(st::activeButtonBg);
		p.setPen(st::activeButtonFg);
		const auto rect = arrow->rect();
		const auto line = st::lineWidth;
		p.drawEllipse(rect.marginsRemoved({ line, line, line, line }));
		icon->paint(p, skip, (size - icon->height()) / 2 + line, size);
	}, widget->lifetime());
	arrow->setAttribute(Qt::WA_TransparentForMouseEvents);
	arrow->show();
}

[[nodiscard]] not_null<RpWidget*> MakeTopReactor(
		not_null<QWidget*> parent,
		const PaidReactionTop &data,
		Fn<void()> selectShownPeer) {
	const auto result = CreateChild<AbstractButton>(parent);
	result->show();
	if (data.click && !data.my) {
		result->setClickedCallback(data.click);
	} else if (data.click && selectShownPeer) {
		result->setClickedCallback(selectShownPeer);
		AddArrowDown(result);
	} else {
		result->setAttribute(Qt::WA_TransparentForMouseEvents);
	}

	struct State {
		QImage badge;
		Text::String name;
	};
	const auto state = result->lifetime().make_state<State>();
	state->name.setText(st::defaultTextStyle, data.name);

	const auto count = data.count;
	const auto photo = data.photo->clone();
	photo->subscribeToUpdates([=] {
		result->update();
	});
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		state->badge = QImage();
	}, result->lifetime());
	result->paintRequest() | rpl::start_with_next([=] {
		auto p = Painter(result);
		const auto left = (result->width() - st::paidReactTopUserpic) / 2;
		p.drawImage(left, 0, photo->image(st::paidReactTopUserpic));

		if (state->badge.isNull()) {
			state->badge = GenerateBadgeImage(count);
		}
		const auto bwidth = state->badge.width()
			/ state->badge.devicePixelRatio();
		p.drawImage(
			(result->width() - bwidth) / 2,
			st::paidReactTopBadgeSkip,
			state->badge);

		p.setPen(st::windowFg);
		const auto skip = st::normalFont->spacew;
		const auto nameTop = st::paidReactTopNameSkip;
		const auto available = result->width() - skip * 2;
		state->name.draw(p, skip, nameTop, available, style::al_top);
	}, result->lifetime());

	return result;
}

void SelectShownPeer(
		std::shared_ptr<base::weak_qptr<PopupMenu>> menu,
		not_null<QWidget*> parent,
		const std::vector<PaidReactionTop> &mine,
		uint64 selected,
		Fn<void(uint64)> callback) {
	if (*menu) {
		(*menu)->hideMenu();
	}
	(*menu) = CreateChild<PopupMenu>(
		parent,
		st::paidReactChannelMenu);

	struct Entry {
		not_null<Ui::WhoReactedEntryAction*> action;
		std::shared_ptr<Ui::DynamicImage> userpic;
	};
	auto actions = std::make_shared<std::vector<Entry>>();
	actions->reserve(mine.size());
	for (const auto &entry : mine) {
		auto action = base::make_unique_q<WhoReactedEntryAction>(
			(*menu)->menu(),
			nullptr,
			(*menu)->menu()->st(),
			Ui::WhoReactedEntryData());
		const auto index = int(actions->size());
		actions->push_back({ action.get(), entry.photo->clone() });
		const auto id = entry.barePeerId;
		const auto updateUserpic = [=] {
			const auto size = st::defaultWhoRead.photoSize;
			actions->at(index).action->setData({
				.text = entry.name,
				.type = ((id == selected)
					? Ui::WhoReactedType::RefRecipientNow
					: Ui::WhoReactedType::RefRecipient),
				.userpic = actions->at(index).userpic->image(size),
				.callback = [=] { callback(id); },
			});
		};
		actions->back().userpic->subscribeToUpdates(updateUserpic);

		(*menu)->addAction(std::move(action));
		updateUserpic();
	}
	(*menu)->popup(QCursor::pos());
}

void FillTopReactors(
		not_null<VerticalLayout*> container,
		std::vector<PaidReactionTop> top,
		rpl::producer<int> chosen,
		rpl::producer<uint64> shownPeer,
		Fn<void(uint64)> changeShownPeer) {
	container->add(
		MakeBoostFeaturesBadge(
			container,
			tr::lng_paid_react_top_title(),
			[](QRect) { return st::creditsBg3->b; }),
		st::boxRowPadding + st::paidReactTopTitleMargin);

	const auto height = st::paidReactTopNameSkip + st::normalFont->height;
	const auto wrap = container->add(
		object_ptr<SlideWrap<FixedHeightWidget>>(
			container,
			object_ptr<FixedHeightWidget>(container, height),
			st::paidReactTopMargin));
	const auto parent = wrap->entity();
	using Key = TopReactorKey;
	struct State {
		base::flat_map<Key, not_null<RpWidget*>> cache;
		std::vector<not_null<RpWidget*>> widgets;
		rpl::event_stream<> updated;
		std::optional<int> initialChosen;
		bool chosenChanged = false;
	};
	const auto state = wrap->lifetime().make_state<State>();
	const auto menu = std::make_shared<base::weak_qptr<Ui::PopupMenu>>();

	rpl::combine(
		std::move(chosen),
		std::move(shownPeer)
	) | rpl::start_with_next([=](int chosen, uint64 barePeerId) {
		if (!state->initialChosen) {
			state->initialChosen = chosen;
		} else if (*state->initialChosen != chosen) {
			state->chosenChanged = true;
		}
		auto mine = std::vector<PaidReactionTop>();
		auto list = std::vector<PaidReactionTop>();
		list.reserve(kMaxTopPaidShown + 1);
		for (const auto &entry : top) {
			if (!entry.my) {
				list.push_back(entry);
			} else if (entry.barePeerId == barePeerId) {
				auto copy = entry;
				if (state->chosenChanged) {
					copy.count += chosen;
				}
				list.push_back(copy);
			}
			if (entry.my && entry.barePeerId) {
				mine.push_back(entry);
			}
		}
		ranges::stable_sort(
			list,
			ranges::greater(),
			&PaidReactionTop::count);
		while (list.size() > kMaxTopPaidShown
			|| (!list.empty() && !list.back().count)) {
			list.pop_back();
		}
		auto selectShownPeer = (mine.size() < 2)
			? Fn<void()>()
			: [=] { SelectShownPeer(
				menu,
				parent,
				mine,
				barePeerId,
				changeShownPeer); };
		if (list.empty()) {
			wrap->hide(anim::type::normal);
		} else {
			for (const auto &widget : state->widgets) {
				widget->hide();
			}
			state->widgets.clear();
			for (const auto &entry : list) {
				const auto key = Key{
					.photo = entry.photo,
					.count = entry.count,
					.name = entry.name,
				};
				const auto i = state->cache.find(key);
				const auto widget = (i != end(state->cache))
					? i->second
					: MakeTopReactor(parent, entry, selectShownPeer);
				state->widgets.push_back(widget);
				widget->show();
			}
			for (const auto &[k, widget] : state->cache) {
				if (widget->isHidden()) {
					delete widget;
				}
			}
			wrap->show(anim::type::normal);
		}

		state->updated.fire({});
	}, wrap->lifetime());
	wrap->finishAnimating();

	rpl::combine(
		state->updated.events_starting_with({}),
		wrap->widthValue()
	) | rpl::start_with_next([=](auto, int width) {
		const auto single = width / 4;
		if (single <= st::paidReactTopUserpic) {
			return;
		}
		const auto count = int(state->widgets.size());
		auto left = (width - single * count) / 2;
		for (const auto &widget : state->widgets) {
			widget->setGeometry(left, 0, single, height);
			left += single;
		}
	}, wrap->lifetime());
}

} // namespace

void PaidReactionsBox(
		not_null<GenericBox*> box,
		PaidReactionBoxArgs &&args) {
	Expects(!args.top.empty());

	args.max = std::max(args.max, 2);
	args.chosen = std::clamp(args.chosen, 1, args.max);

	box->setWidth(st::boxWideWidth);
	box->setStyle(st::paidReactBox);
	box->setNoContentMargin(true);

	struct State {
		rpl::variable<int> chosen;
		rpl::variable<uint64> shownPeer;
		uint64 savedShownPeer = 0;
	};
	const auto state = box->lifetime().make_state<State>();

	state->chosen = args.chosen;
	const auto changed = [=](int count) {
		state->chosen = count;
	};

	const auto initialShownPeer = ranges::find(
		args.top,
		true,
		&PaidReactionTop::my
	)->barePeerId;
	state->shownPeer = initialShownPeer;
	state->savedShownPeer = ranges::find_if(args.top, [](
			const PaidReactionTop &entry) {
		return entry.my && entry.barePeerId != 0;
	})->barePeerId;

	const auto content = box->verticalLayout();
	AddSkip(content, st::boxTitleClose.height + st::paidReactBubbleTop);

	const auto valueToRatio = DiscreterForMax(args.max).valueToRatio;
	auto bubbleRowState = state->chosen.value() | rpl::map([=](int value) {
		const auto full = st::boxWideWidth
			- st::boxRowPadding.left()
			- st::boxRowPadding.right();
		const auto marker = st::paidReactSlider.seekSize.width();
		const auto start = marker / 2;
		const auto inner = full - marker;
		const auto correct = start + inner * valueToRatio(value);
		return Premium::BubbleRowState{
			.counter = value,
			.ratio = correct / full,
		};
	});
	Premium::AddBubbleRow(
		content,
		st::boostBubble,
		BoxShowFinishes(box),
		std::move(bubbleRowState),
		Premium::BubbleType::Credits,
		nullptr,
		&st::paidReactBubbleIcon,
		st::boxRowPadding);

	const auto already = ranges::find(
		args.top,
		true,
		&PaidReactionTop::my)->count;
	PaidReactionSlider(content, args.chosen, args.max, changed);

	box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

	box->addRow(
		object_ptr<FlatLabel>(
			box,
			tr::lng_paid_react_title(),
			st::boostCenteredTitle),
		st::boxRowPadding + QMargins(0, st::paidReactTitleSkip, 0, 0),
		style::al_top);
	const auto labelWrap = box->addRow(
		object_ptr<RpWidget>(box),
		(st::boxRowPadding
			+ QMargins(0, st::lineWidth, 0, st::boostBottomSkip)));
	const auto label = CreateChild<FlatLabel>(
		labelWrap,
		(already
			? tr::lng_paid_react_already(
				lt_count,
				rpl::single(already) | tr::to_count(),
				Text::RichLangValue)
			: tr::lng_paid_react_about(
				lt_channel,
				rpl::single(Text::Bold(args.channel)),
				Text::RichLangValue)),
		st::boostText);
	labelWrap->widthValue() | rpl::start_with_next([=](int width) {
		label->resizeToWidth(width);
	}, label->lifetime());
	label->heightValue() | rpl::start_with_next([=](int height) {
		const auto min = 2 * st::normalFont->height;
		const auto skip = std::max((min - height) / 2, 0);
		labelWrap->resize(labelWrap->width(), 2 * skip + height);
		label->moveToLeft(0, skip);
	}, label->lifetime());

	FillTopReactors(
		content,
		std::move(args.top),
		state->chosen.value(),
		state->shownPeer.value(),
		[=](uint64 barePeerId) {
			state->shownPeer = state->savedShownPeer = barePeerId;
		});

	const auto named = box->addRow(
		object_ptr<Checkbox>(
			box,
			tr::lng_paid_react_show_in_top(tr::now),
			state->shownPeer.current() != 0),
		style::al_top);
	named->checkedValue(
	) | rpl::start_with_next([=](bool show) {
		state->shownPeer = show ? state->savedShownPeer : 0;
	}, named->lifetime());

	const auto button = box->addButton(rpl::single(QString()), [=] {
		args.send(state->chosen.current(), state->shownPeer.current());
	});

	box->boxClosing() | rpl::filter([=] {
		return state->shownPeer.current() != initialShownPeer;
	}) | rpl::start_with_next([=] {
		args.send(0, state->shownPeer.current());
	}, box->lifetime());

	{
		const auto buttonLabel = CreateChild<FlatLabel>(
			button,
			rpl::single(QString()),
			st::creditsBoxButtonLabel);
		args.submit(
			state->chosen.value()
		) | rpl::start_with_next([=](const TextWithEntities &text) {
			buttonLabel->setMarkedText(text);
		}, buttonLabel->lifetime());
		buttonLabel->setTextColorOverride(
			box->getDelegate()->style().button.textFg->c);
		button->sizeValue(
		) | rpl::start_with_next([=](const QSize &size) {
			buttonLabel->moveToLeft(
				(size.width() - buttonLabel->width()) / 2,
				(size.height() - buttonLabel->height()) / 2);
		}, buttonLabel->lifetime());
		buttonLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
	}

	box->widthValue(
	) | rpl::start_with_next([=](int width) {
		const auto &padding = st::paidReactBox.buttonPadding;
		button->resizeToWidth(width
			- padding.left()
			- padding.right());
		button->moveToLeft(padding.left(), button->y());
	}, button->lifetime());

	{
		const auto balance = Settings::AddBalanceWidget(
			content,
			args.session,
			std::move(args.balanceValue),
			false);
		rpl::combine(
			balance->sizeValue(),
			box->widthValue()
		) | rpl::start_with_next([=] {
			balance->moveToLeft(
				st::creditsHistoryRightSkip * 2,
				st::creditsHistoryRightSkip);
			balance->update();
		}, balance->lifetime());
	}
}

object_ptr<BoxContent> MakePaidReactionBox(PaidReactionBoxArgs &&args) {
	return Box(PaidReactionsBox, std::move(args));
}

QImage GenerateSmallBadgeImage(
		QString text,
		const style::icon &icon,
		QColor bg,
		QColor fg,
		const style::RoundCheckbox *borderSt) {
	const auto length = st::chatSimilarBadgeFont->width(text);
	const auto contents = st::chatSimilarLockedIconPosition.x()
		+ icon.width()
		+ st::paidReactTopStarSkip
		+ length;
	const auto badge = QRect(
		st::chatSimilarBadgePadding.left(),
		st::chatSimilarBadgePadding.top(),
		contents,
		st::chatSimilarBadgeFont->height);
	const auto rect = badge.marginsAdded(st::chatSimilarBadgePadding);
	const auto add = borderSt ? borderSt->width : 0;
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		(rect + QMargins(add, add, add, add)).size() * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	auto q = QPainter(&result);

	const auto &font = st::chatSimilarBadgeFont;
	const auto textTop = badge.y() + font->ascent;
	const auto position = st::chatSimilarLockedIconPosition;

	auto hq = PainterHighQualityEnabler(q);
	q.translate(add, add);
	q.setBrush(bg);
	if (borderSt) {
		q.setPen(QPen(borderSt->border->c, borderSt->width));
	} else {
		q.setPen(Qt::NoPen);
	}
	const auto radius = rect.height() / 2.;
	const auto shift = add / 2.;
	q.drawRoundedRect(
		QRectF(rect) + QMarginsF(shift, shift, shift, shift),
		radius,
		radius);

	auto textLeft = 0;
	icon.paint(
		q,
		badge.x() + position.x(),
		badge.y() + position.y(),
		rect.width());
	textLeft += position.x() + icon.width() + st::paidReactTopStarSkip;

	q.setFont(font);
	q.setPen(fg);
	q.drawText(textLeft, textTop, text);
	q.end();

	return result;
}

} // namespace Ui
