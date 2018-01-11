/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/history_view_element.h"

class HistoryMessage;

namespace HistoryView {

class Message : public Element {
public:
	Message(not_null<HistoryMessage*> data, Context context);

private:
	not_null<HistoryMessage*> message() const;

};

} // namespace HistoryView
