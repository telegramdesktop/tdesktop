/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/button_labels.h"

#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"

namespace Ui {

void SetButtonTwoLabels(
		not_null<Ui::RoundButton*> button,
		rpl::producer<TextWithEntities> title,
		rpl::producer<TextWithEntities> subtitle,
		const style::FlatLabel &st,
		const style::FlatLabel &subst,
		const style::color *textFg) {
	const auto buttonTitle = Ui::CreateChild<Ui::FlatLabel>(
		button,
		std::move(title),
		st);
	buttonTitle->show();
	const auto buttonSubtitle = Ui::CreateChild<Ui::FlatLabel>(
		button,
		rpl::duplicate(
			subtitle
		) | rpl::filter([](const TextWithEntities &text) {
			return !text.empty();
		}),
		subst);
	buttonSubtitle->setOpacity(0.6);
	if (textFg) {
		buttonTitle->setTextColorOverride((*textFg)->c);
		buttonSubtitle->setTextColorOverride((*textFg)->c);
		style::PaletteChanged() | rpl::on_next([=] {
			buttonTitle->setTextColorOverride((*textFg)->c);
			buttonSubtitle->setTextColorOverride((*textFg)->c);
		}, buttonTitle->lifetime());
	}
	rpl::combine(
		button->sizeValue(),
		buttonTitle->sizeValue(),
		buttonSubtitle->sizeValue(),
		std::move(subtitle)
	) | rpl::on_next([=](
			QSize outer,
			QSize title,
			QSize subtitle,
			const TextWithEntities &subtitleText) {
		const auto withSubtitle = !subtitleText.empty();
		buttonSubtitle->setVisible(withSubtitle);

		const auto two = title.height() + subtitle.height();
		const auto titleTop = withSubtitle
			? (outer.height() - two) / 2
			: button->st().textTop;
		const auto subtitleTop = titleTop + title.height();
		buttonTitle->moveToLeft(
			(outer.width() - title.width()) / 2,
			titleTop);
		buttonSubtitle->moveToLeft(
			(outer.width() - subtitle.width()) / 2,
			subtitleTop);
	}, buttonTitle->lifetime());
	buttonTitle->setAttribute(Qt::WA_TransparentForMouseEvents);
	buttonSubtitle->setAttribute(Qt::WA_TransparentForMouseEvents);
}

} // namespace Ui
