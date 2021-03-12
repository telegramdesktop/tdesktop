/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

namespace Ui {
class DropdownMenu;
class GenericBox;
class BoxContent;
} // namespace Ui

namespace Calls {
class GroupCall;
} // namespace Calls

namespace Calls::Group {

enum class BoxContext {
	GroupCallPanel,
	MainWindow,
};

void LeaveBox(
	not_null<Ui::GenericBox*> box,
	not_null<GroupCall*> call,
	bool discardChecked,
	BoxContext context);

void ConfirmBox(
	not_null<Ui::GenericBox*> box,
	const TextWithEntities &text,
	rpl::producer<QString> button,
	Fn<void()> callback);

void FillMenu(
	not_null<Ui::DropdownMenu*> menu,
	not_null<PeerData*> peer,
	not_null<GroupCall*> call,
	Fn<void()> chooseJoinAs,
	Fn<void(object_ptr<Ui::BoxContent>)> showBox);

} // namespace Calls::Group
