/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "menu/add_action_callback_factory.h"

#include "menu/add_action_callback.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_menu_icons.h"

namespace Menu {

MenuCallback CreateAddActionCallback(
		const base::unique_qptr<Ui::PopupMenu> &menu) {
	return MenuCallback([&](MenuCallback::Args a) {
		if (a.fillSubmenu) {
			const auto action = menu->addAction(
				a.text,
				std::move(a.handler),
				a.icon);
			// Dummy menu.
			action->setMenu(Ui::CreateChild<QMenu>(menu->menu().get()));
			a.fillSubmenu(menu->ensureSubmenu(action));
			return action;
		} else if (a.isSeparator) {
			return menu->addSeparator();
		} else if (a.isAttention) {
			return menu->addAction(base::make_unique_q<Ui::Menu::Action>(
				menu,
				st::menuWithIconsAttention,
				Ui::Menu::CreateAction(
					menu->menu().get(),
					a.text,
					std::move(a.handler)),
				a.icon,
				a.icon));
		}
		return menu->addAction(a.text, std::move(a.handler), a.icon);
	});
}

} // namespace Menu
