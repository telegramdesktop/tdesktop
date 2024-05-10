/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace style {
struct ComposeIcons;
} // namespace style

namespace Api {
struct SendOptions;
} // namespace Api

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

enum class Type {
	Disabled,
	SilentOnly,
	Scheduled,
	ScheduledToUser, // For "Send when online".
	Reminder,
};

struct Details {
	Type type = Type::Disabled;
	bool effectAllowed = false;
};

enum class FillMenuResult {
	Prepared,
	Skipped,
	Failed,
};

enum class ActionType {
	Send,
	Schedule,
};
using Action = std::variant<Api::SendOptions, ActionType>;
[[nodiscard]] Fn<void(Action, Details)> DefaultCallback(
	std::shared_ptr<ChatHelpers::Show> show,
	Fn<void(Api::SendOptions)> send);

FillMenuResult FillSendMenu(
	not_null<Ui::PopupMenu*> menu,
	std::shared_ptr<ChatHelpers::Show> showForEffect,
	Details details,
	Fn<void(Action, Details)> action,
	const style::ComposeIcons *iconsOverride = nullptr,
	std::optional<QPoint> desiredPositionOverride = std::nullopt);

void SetupMenuAndShortcuts(
	not_null<Ui::RpWidget*> button,
	std::shared_ptr<ChatHelpers::Show> show,
	Fn<Details()> details,
	Fn<void(Action, Details)> action);

void SetupUnreadMentionsMenu(
	not_null<Ui::RpWidget*> button,
	Fn<Data::Thread*()> currentThread);

void SetupUnreadReactionsMenu(
	not_null<Ui::RpWidget*> button,
	Fn<Data::Thread*()> currentThread);

} // namespace SendMenu
