/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_message.h"

#include "history/history_message.h"

namespace HistoryView {

Message::Message(not_null<HistoryMessage*> data, Context context)
: Element(data, context) {
}

not_null<HistoryMessage*> Message::message() const {
	return static_cast<HistoryMessage*>(data().get());
}

} // namespace HistoryView
