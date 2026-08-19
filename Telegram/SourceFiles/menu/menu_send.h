/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_common.h"
#include "menu/menu_send_details.h"

namespace style {
struct ComposeIcons;
struct PopupMenu;
} // namespace style

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Ui {
class PopupMenu;
class RpWidget;
class Show;
} // namespace Ui

namespace Data {
class Thread;
} // namespace Data

namespace SendMenu {

enum class FillMenuResult : uchar {
	Prepared,
	Skipped,
	Failed,
};

enum class ActionType : uchar {
	Send,
	Schedule,
	SpoilerOn,
	SpoilerOff,
	CaptionUp,
	CaptionDown,
	PhotoQualityOn,
	PhotoQualityOff,
	ChangePrice,
};
struct Action {
	using Type = ActionType;

	Api::SendOptions options;
	Type type = Type::Send;
};
[[nodiscard]] Fn<void(Action, Details)> DefaultCallback(
	std::shared_ptr<ChatHelpers::Show> show,
	Fn<void(Api::SendOptions)> send);

FillMenuResult FillSendMenu(
	not_null<Ui::PopupMenu*> menu,
	std::shared_ptr<ChatHelpers::Show> maybeShow,
	Details details,
	Fn<void(Action, Details)> action,
	const style::ComposeIcons *iconsOverride = nullptr,
	std::optional<QPoint> desiredPositionOverride = std::nullopt);

FillMenuResult AttachSendMenuEffect(
	not_null<Ui::PopupMenu*> menu,
	std::shared_ptr<ChatHelpers::Show> show,
	Details details,
	Fn<void(Action, Details)> action,
	std::optional<QPoint> desiredPositionOverride = std::nullopt);

void SetupMenuAndShortcuts(
	not_null<Ui::RpWidget*> button,
	std::shared_ptr<ChatHelpers::Show> maybeShow,
	Fn<Details()> details,
	Fn<void(Action, Details)> action,
	const style::PopupMenu *stOverride = nullptr,
	const style::ComposeIcons *iconsOverride = nullptr);

void SetupUnreadMentionsMenu(
	not_null<Ui::RpWidget*> button,
	Fn<Data::Thread*()> currentThread);

void SetupUnreadReactionsMenu(
	not_null<Ui::RpWidget*> button,
	Fn<Data::Thread*()> currentThread);

void SetupUnreadPollVotesMenu(
	not_null<Ui::RpWidget*> button,
	Fn<Data::Thread*()> currentThread);

} // namespace SendMenu
