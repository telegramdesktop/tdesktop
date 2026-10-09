/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/button_busy.h"

#include "ui/widgets/buttons.h"

namespace Ui {

void SetButtonDimmed(Ui::RoundButton *button, bool dimmed) {
	if (!button) {
		return;
	}
	button->setAttribute(Qt::WA_TransparentForMouseEvents, dimmed);
	button->setTextFgOverride(dimmed
		? std::make_optional(anim::with_alpha(button->st().textFg->c, 0.5))
		: std::nullopt);
}

void SetButtonBusy(Ui::RoundButton *button, bool busy) {
	if (!button) {
		return;
	}
	button->setDisabled(busy);
	SetButtonDimmed(button, busy);
}

} // namespace Ui
