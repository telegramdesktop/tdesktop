/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "base/unique_qptr.h"
#include "ui/layers/generic_box.h"

namespace style {
struct FlatLabel;
} // namespace style

namespace Ui {
class DropdownMenu;
class GenericBox;
class BoxContent;
} // namespace Ui

namespace Ui::Menu {
class ItemBase;
class Menu;
} // namespace Ui::Menu

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

struct ConfirmBoxArgs {
	TextWithEntities text;
	rpl::producer<QString> button;
	Fn<void()> callback;
	const style::FlatLabel *st = nullptr;
	Fn<bool(const ClickHandlerPtr&, Qt::MouseButton)> filter;
};

void ConfirmBoxBuilder(not_null<Ui::GenericBox*> box, ConfirmBoxArgs &&args);

inline auto ConfirmBox(ConfirmBoxArgs &&args) {
	return Box(ConfirmBoxBuilder, std::move(args));
}

void FillMenu(
	not_null<Ui::DropdownMenu*> menu,
	not_null<PeerData*> peer,
	not_null<GroupCall*> call,
	bool wide,
	Fn<void()> chooseJoinAs,
	Fn<void()> chooseShareScreenSource,
	Fn<void(object_ptr<Ui::BoxContent>)> showBox);

[[nodiscard]] base::unique_qptr<Ui::Menu::ItemBase> MakeAttentionAction(
	not_null<Ui::Menu::Menu*> menu,
	const QString &text,
	Fn<void()> callback);

} // namespace Calls::Group
