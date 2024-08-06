/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "payments/ui/payments_reaction_box.h"

#include "lang/lang_keys.h"
#include "ui/boxes/boost_box.h" // MakeBoostFeaturesBadge.
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/dynamic_image.h"
#include "ui/painter.h"
#include "styles/style_chat.h"
#include "styles/style_credits.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"

namespace Settings {
[[nodiscard]] not_null<Ui::RpWidget*> AddBalanceWidget(
	not_null<Ui::RpWidget*> parent,
	rpl::producer<uint64> balanceValue,
	bool rightAlign);
} // namespace Settings

namespace Ui {
namespace {

constexpr auto kMaxTopPaidShown = 3;

void PaidReactionSlider(
		not_null<VerticalLayout*> container,
		int min,
		int current,
		int max,
		Fn<void(int)> changed) {
	const auto top = st::boxTitleClose.height + st::creditsHistoryRightSkip;
	const auto slider = container->add(
		object_ptr<MediaSlider>(container, st::paidReactSlider),
		st::boxRowPadding + QMargins(0, top, 0, 0));
	slider->resize(slider->width(), st::paidReactSlider.seekSize.height());
	slider->setPseudoDiscrete(
		max + 1 - min,
		[=](int index) { return min + index; },
		current - min,
		changed,
		changed);
}

[[nodiscard]] QImage GenerateBadgeImage(int count) {
	const auto text = Lang::FormatCountDecimal(count);
	const auto length = st::chatSimilarBadgeFont->width(text);
	const auto contents = length
		+ st::chatSimilarLockedIcon.width();
	const auto badge = QRect(
		st::chatSimilarBadgePadding.left(),
		st::chatSimilarBadgePadding.top(),
		contents,
		st::chatSimilarBadgeFont->height);
	const auto rect = badge.marginsAdded(st::chatSimilarBadgePadding);

	auto result = QImage(
		rect.size() * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(style::DevicePixelRatio());
	result.fill(Qt::transparent);
	auto q = QPainter(&result);

	const auto &font = st::chatSimilarBadgeFont;
	const auto textTop = badge.y() + font->ascent;
	const auto icon = &st::chatSimilarLockedIcon;
	const auto position = st::chatSimilarLockedIconPosition;

	auto hq = PainterHighQualityEnabler(q);
	q.setBrush(st::creditsBg3);
	q.setPen(Qt::NoPen);
	const auto radius = rect.height() / 2.;
	q.drawRoundedRect(rect, radius, radius);

	auto textLeft = 0;
	if (icon) {
		icon->paint(
			q,
			badge.x() + position.x(),
			badge.y() + position.y(),
			rect.width());
		textLeft += position.x() + icon->width();
	}

	q.setFont(font);
	q.setPen(st::premiumButtonFg);
	q.drawText(textLeft, textTop, text);
	q.end();

	return result;
}

[[nodiscard]] not_null<Ui::RpWidget*> MakeTopReactor(
		not_null<QWidget*> parent,
		const PaidReactionTop &data) {
	const auto result = Ui::CreateChild<Ui::RpWidget>(parent);
	result->show();

	struct State {
		QImage badge;
		Ui::Text::String name;
	};
	const auto state = result->lifetime().make_state<State>();
	state->name.setText(st::defaultTextStyle, data.name);

	const auto count = data.count;
	const auto photo = data.photo;
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

void FillTopReactors(
		not_null<VerticalLayout*> container,
		std::vector<PaidReactionTop> top) {
	container->add(
		MakeBoostFeaturesBadge(
			container,
			tr::lng_paid_react_top_title(),
			[](QRect) { return st::creditsBg3->b; }),
		st::boxRowPadding + st::paidReactTopTitleMargin);

	const auto height = st::paidReactTopNameSkip + st::normalFont->height;
	const auto wrap = container->add(
		object_ptr<Ui::FixedHeightWidget>(container, height),
		st::paidReactTopMargin);
	struct State {
		std::vector<not_null<Ui::RpWidget*>> widgets;
	};
	const auto state = wrap->lifetime().make_state<State>();

	const auto topCount = std::min(int(top.size()), kMaxTopPaidShown);
	for (auto i = 0; i != topCount; ++i) {
		state->widgets.push_back(MakeTopReactor(wrap, top[i]));
	}

	wrap->widthValue() | rpl::start_with_next([=](int width) {
		const auto single = width / 4;
		if (single <= st::paidReactTopUserpic) {
			return;
		}
		auto left = (width - single * topCount) / 2;
		for (const auto widget : state->widgets) {
			widget->setGeometry(left, 0, single, height);
			left += single;
		}
	}, wrap->lifetime());
}

} // namespace

void PaidReactionsBox(
		not_null<GenericBox*> box,
		PaidReactionBoxArgs &&args) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::paidReactBox);
	box->setNoContentMargin(true);

	struct State {
		rpl::variable<int> chosen;
	};
	const auto state = box->lifetime().make_state<State>();
	state->chosen = args.chosen;
	const auto changed = [=](int count) {
		state->chosen = count;
	};
	PaidReactionSlider(
		box->verticalLayout(),
		args.min,
		args.chosen,
		args.max,
		changed);

	box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_paid_react_title(),
			st::boostCenteredTitle),
		st::boxRowPadding + QMargins(0, st::paidReactTitleSkip, 0, 0));
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_paid_react_about(
				lt_channel,
				rpl::single(Text::Bold(args.channel)),
				Text::RichLangValue),
			st::boostText),
		(st::boxRowPadding
			+ QMargins(0, st::lineWidth, 0, st::boostBottomSkip)));

	if (!args.top.empty()) {
		FillTopReactors(box->verticalLayout(), std::move(args.top));
	}

	const auto button = box->addButton(rpl::single(QString()), [=] {
		args.send(state->chosen.current());
	});
	{
		const auto buttonLabel = Ui::CreateChild<Ui::FlatLabel>(
			button,
			rpl::single(QString()),
			st::creditsBoxButtonLabel);
		args.submit(
			state->chosen.value()
		) | rpl::start_with_next([=](const TextWithContext &text) {
			buttonLabel->setMarkedText(
				text.text,
				text.context);
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
			box->verticalLayout(),
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

} // namespace Ui
