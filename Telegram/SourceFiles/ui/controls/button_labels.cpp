/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/button_labels.h"

#include "ui/widgets/labels.h"

namespace Ui {

void SetButtonTwoLabels(
		not_null<Ui::RpWidget*> button,
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
		std::move(subtitle),
		subst);
	buttonSubtitle->show();
	buttonSubtitle->setOpacity(0.6);
	if (textFg) {
		buttonTitle->setTextColorOverride((*textFg)->c);
		buttonSubtitle->setTextColorOverride((*textFg)->c);
		style::PaletteChanged() | rpl::start_with_next([=] {
			buttonTitle->setTextColorOverride((*textFg)->c);
			buttonSubtitle->setTextColorOverride((*textFg)->c);
		}, buttonTitle->lifetime());
	}
	rpl::combine(
		button->sizeValue(),
		buttonTitle->sizeValue(),
		buttonSubtitle->sizeValue()
	) | rpl::start_with_next([=](QSize outer, QSize title, QSize subtitle) {
		const auto two = title.height() + subtitle.height();
		const auto titleTop = (outer.height() - two) / 2;
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
