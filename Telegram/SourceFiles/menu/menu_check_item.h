/*
This file is part of exteraGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/xmdnx/exteraGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "ui/widgets/menu/menu_action.h"

namespace Ui {
class CheckView;
} // namespace Ui

namespace Menu {

class ItemWithCheck final : public Ui::Menu::Action {
public:
	using Ui::Menu::Action::Action;

	void init(bool checked);

	not_null<Ui::CheckView*> checkView() const;

private:
	std::unique_ptr<Ui::CheckView> _checkView;
};

} // namespace Menu
