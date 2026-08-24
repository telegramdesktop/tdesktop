/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace style {
struct Box;
struct FlatLabel;
struct IconButton;
struct InputField;
} // namespace style

namespace Ui {
class Show;
} // namespace Ui

namespace Window {

struct UnlockPasscodeBoxStyle {
	not_null<const style::Box*> box;
	not_null<const style::IconButton*> close;
	not_null<const style::FlatLabel*> description;
	not_null<const style::InputField*> field;
	not_null<const style::FlatLabel*> error;
};

// Shows the unlock box through `show` and returns true when the app is
// locked by a local passcode, running `unlocked` after a successful
// unlock. Returns false when the app is not locked, so the caller can
// proceed with the action right away.
[[nodiscard]] bool ShowUnlockPasscodeBox(
	std::shared_ptr<Ui::Show> show,
	UnlockPasscodeBoxStyle st,
	Fn<void()> unlocked);

} // namespace Window
