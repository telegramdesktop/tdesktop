/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "ui/widgets/menu/menu_action.h"

namespace Ui {
class CheckView;
} // namespace Ui

namespace Menu {

class ItemWithCheck final : public Ui::Menu::Action {
public:
	using Ui::Menu::Action::Action;

	void init(bool checked);
	void setChangeAllowed(Fn<bool(bool checked)> allowed);

	not_null<Ui::CheckView*> checkView() const;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void shake();

	std::unique_ptr<Ui::CheckView> _checkView;
	Fn<bool(bool)> _changeAllowed;
	Ui::Animations::Simple _shakeAnimation;
	int _shakeShift = 0;
};

} // namespace Menu
