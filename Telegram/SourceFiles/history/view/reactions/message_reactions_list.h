/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

class HistoryItem;

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class BoxContent;
} // namespace Ui

namespace HistoryView {

object_ptr<Ui::BoxContent> ReactionsListBox(
	not_null<Window::SessionController*> window,
	not_null<HistoryItem*> item,
	QString selected);

} // namespace HistoryView
