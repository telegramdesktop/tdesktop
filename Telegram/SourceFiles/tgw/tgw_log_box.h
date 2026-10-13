/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace HistoryView {
class Element;
} // namespace HistoryView

namespace Ui {
class PopupMenu;
class Show;
} // namespace Ui

namespace Tgw {

void AddLogAction(
	not_null<Ui::PopupMenu*> menu,
	not_null<const HistoryView::Element*> view,
	std::shared_ptr<Ui::Show> show);

} // namespace Tgw
