/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "payments/ui/payments_reaction_box.h"

#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/continuous_sliders.h"
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

void PaidReactionSlider(
		not_null<VerticalLayout*> container,
		int min,
		int current,
		int max,
		Fn<void(int)> changed) {
	const auto top = st::boxTitleClose.height + st::creditsHistoryRightSkip;
	const auto slider = container->add(
		object_ptr<MediaSlider>(container, st::settingsScale),
		st::boxRowPadding + QMargins(0, top, 0, 0));
	slider->resize(slider->width(), st::settingsScale.seekSize.height());
	slider->setPseudoDiscrete(
		max + 1 - min,
		[=](int index) { return min + index; },
		current - min,
		changed,
		changed);
}

} // namespace

void PaidReactionsBox(
		not_null<GenericBox*> box,
		PaidReactionBoxArgs &&args) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::boostBox);
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
		st::boxRowPadding + QMargins(0, st::boostTitleSkip, 0, 0));
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_paid_react_about(
				lt_channel,
				rpl::single(Text::Bold(args.channel)),
				Text::RichLangValue),
			st::boostText),
		(st::boxRowPadding
			+ QMargins(0, st::boostTextSkip, 0, st::boostBottomSkip)));

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
		const auto &padding = st::boostBox.buttonPadding;
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
