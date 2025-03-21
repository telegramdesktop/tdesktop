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
enum class QuickDialogAction;
enum class QuickDialogActionLabel;
} // namespace Dialogs::Ui

namespace Lottie {
class Icon;
} // namespace Lottie

namespace Window {
class SessionController;
} // namespace Window

namespace Dialogs {

void PerformQuickDialogAction(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	Ui::QuickDialogAction action,
	FilterId filterId);

[[nodiscard]] QString ResolveQuickDialogLottieIconName(
	Ui::QuickDialogActionLabel action);

[[nodiscard]] Ui::QuickDialogActionLabel ResolveQuickDialogLabel(
	not_null<History*> history,
	Ui::QuickDialogAction action,
	FilterId filterId);

[[nodiscard]] QString ResolveQuickDialogLabel(Ui::QuickDialogActionLabel);

[[nodiscard]] const style::color &ResolveQuickActionBg(
	Ui::QuickDialogActionLabel);
[[nodiscard]] const style::color &ResolveQuickActionBgActive(
	Ui::QuickDialogActionLabel);

void DrawQuickAction(
	QPainter &p,
	const QRect &rect,
	not_null<Lottie::Icon*> icon,
	Ui::QuickDialogActionLabel label,
	float64 iconRatio = 1.,
	bool twoLines = false);

} // namespace Dialogs
