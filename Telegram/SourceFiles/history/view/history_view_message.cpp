/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_message.h"

namespace HistoryView {

Message::Message(not_null<HistoryItem*> data, Context context)
: _data(data)
, _context(context) {
}

MsgId Message::id() const {
	return _data->id;
}

not_null<HistoryItem*> Message::data() const {
	return _data;
}

} // namespace HistoryView
