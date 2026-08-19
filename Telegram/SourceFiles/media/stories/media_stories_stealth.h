/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace style {
struct StealthBoxStyle;
} // namespace style

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Ui::Menu {
struct MenuCallback;
} // namespace Ui::Menu

namespace Window {
class SessionController;
} // namespace Window

namespace Media::Stories {

struct StealthModeDescriptor {
	Fn<void()> onActivated = nullptr;
	const style::StealthBoxStyle *st = nullptr;
};

void SetupStealthMode(
	std::shared_ptr<ChatHelpers::Show> show,
	StealthModeDescriptor descriptor = {});

void AddStealthModeMenu(
	const Ui::Menu::MenuCallback &add,
	not_null<PeerData*> peer,
	not_null<Window::SessionController*> controller);

[[nodiscard]] QString TimeLeftText(int left);

} // namespace Media::Stories
