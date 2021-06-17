/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "base/unique_qptr.h"

namespace Ui {

class RpWidget;
class PopupMenu;

class InviteLinkLabel final {
public:
	InviteLinkLabel(
		not_null<QWidget*> parent,
		rpl::producer<QString> text,
		Fn<base::unique_qptr<PopupMenu>()> createMenu);

	[[nodiscard]] object_ptr<RpWidget> take();

	[[nodiscard]] rpl::producer<> clicks();

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	const base::unique_qptr<RpWidget> _outer;
	base::unique_qptr<Ui::PopupMenu> _menu;

};

} // namespace Ui
