/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_group_settings.h"

#include "ui/widgets/checkbox.h"
#include "lang/lang_keys.h"
#include "styles/style_layers.h"

namespace Calls {

void GroupCallSettingsBox(
		not_null<Ui::GenericBox*> box,
		Fn<void()> copyShareLink,
		Fn<void()> discard) {
	box->setTitle(tr::lng_group_call_settings_title());
	//box->addRow(object_ptr<Ui::Checkbox>(
	//	box.get(),
	//	tr::lng_group_call_new_muted(),
	//	newMuted
	//	))
	box->addButton(tr::lng_settings_save(), [=] {
		box->closeBox();
	});
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

} // namespace Calls
