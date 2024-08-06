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

namespace Payments {

void TryAddingPaidReaction(
	not_null<HistoryItem*> item,
	HistoryView::Element *view,
	int count,
	std::shared_ptr<Ui::Show> show,
	Fn<void(bool)> finished = nullptr);

void ShowPaidReactionDetails(
	std::shared_ptr<Ui::Show> show,
	not_null<HistoryItem*> item,
	HistoryView::Element *view,
	HistoryReactionSource source);

} // namespace Payments
