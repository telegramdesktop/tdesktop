/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class History;
class PeerData;

namespace Dialogs::Ui {
enum class SwipeDialogAction;
enum class SwipeDialogActionLabel;
} // namespace Dialogs::Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Dialogs {

void PerformSwipeDialogAction(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	Ui::SwipeDialogAction action,
	FilterId filterId);

[[nodiscard]] QString ResolveSwipeDialogLottieIconName(
	not_null<PeerData*> peer,
	Ui::SwipeDialogAction action,
	FilterId filterId);

[[nodiscard]] Ui::SwipeDialogActionLabel ResolveSwipeDialogLabel(
	not_null<History*> history,
	Ui::SwipeDialogAction action,
	FilterId filterId);

[[nodiscard]] QString ResolveSwipeDialogLabel(Ui::SwipeDialogActionLabel);

} // namespace Dialogs
