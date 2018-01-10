/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;

namespace HistoryView {

enum class Context : char {
	History,
	Feed,
	AdminLog
};

class Message
	: public RuntimeComposer
	, public ClickHandlerHost {
public:
	Message(not_null<HistoryItem*> data, Context context);

	MsgId id() const;
	not_null<HistoryItem*> data() const;

	int y() const {
		return _y;
	}
	void setY(int y) {
		_y = y;
	}

private:
	const not_null<HistoryItem*> _data;
	int _y = 0;
	Context _context;

};

} // namespace HistoryView
