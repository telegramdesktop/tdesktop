/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_common.h"

namespace style {
struct ComposeIcons;
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

enum class Type : uchar {
	Disabled,
	SilentOnly,
	Scheduled,
	ScheduledToUser, // For "Send when online".
	Reminder,
};

enum class SpoilerState : uchar {
	None,
	Enabled,
	Possible,
};

enum class CaptionState : uchar {
	None,
	Below,
	Above,
};

struct Details {
	Type type = Type::Disabled;
	SpoilerState spoiler = SpoilerState::None;
	CaptionState caption = CaptionState::None;
	bool effectAllowed = false;
};

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
