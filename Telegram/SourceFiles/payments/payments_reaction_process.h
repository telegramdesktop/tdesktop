/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

enum class HistoryReactionSource : char;

class HistoryItem;

namespace HistoryView {
class Element;
} // namespace HistoryView

namespace Ui {
class Show;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Payments {

[[nodiscard]] bool LookupMyPaidAnonymous(not_null<HistoryItem*> item);

void TryAddingPaidReaction(
	not_null<HistoryItem*> item,
	HistoryView::Element *view,
	int count,
	std::optional<PeerId> shownPeer,
	std::shared_ptr<Ui::Show> show,
	Fn<void(bool)> finished = nullptr);

void ShowPaidReactionDetails(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item,
	HistoryView::Element *view,
	HistoryReactionSource source);

} // namespace Payments
