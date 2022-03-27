/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "menu/menu_check_item.h"

#include "ui/widgets/checkbox.h"
#include "styles/style_media_player.h"

namespace Menu {

void ItemWithCheck::init(bool checked) {
	enableMouseSelecting();

	AbstractButton::setDisabled(true);

	_checkView = std::make_unique<Ui::CheckView>(st::defaultCheck, false);
	_checkView->checkedChanges(
	) | rpl::start_with_next([=](bool checked) {
		setIcon(checked ? &st::mediaPlayerMenuCheck : nullptr);
	}, lifetime());

	_checkView->setChecked(checked, anim::type::normal);
	AbstractButton::clicks(
	) | rpl::start_with_next([=] {
		_checkView->setChecked(
			!_checkView->checked(),
			anim::type::normal);
	}, lifetime());
}

not_null<Ui::CheckView*> ItemWithCheck::checkView() const {
	return _checkView.get();
}

} // namespace Menu
