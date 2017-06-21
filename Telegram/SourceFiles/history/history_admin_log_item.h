/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "history/history_admin_log_inner.h"

namespace AdminLog {

class Item {
public:
	struct TextState {
		HistoryTextState data;
		ClickHandlerHost *handler = nullptr;
	};

	Item(gsl::not_null<History*> history, LocalIdManager &idManager, const MTPDchannelAdminLogEvent &event);

	uint64 id() const {
		return _id;
	}
	QDateTime date() const {
		return _date;
	}
	int top() const {
		return _top;
	}
	void setTop(int top) {
		_top = top;
	}
	int height() const {
		return _height;
	}

	int resizeGetHeight(int newWidth);
	void draw(Painter &p, QRect clip, TextSelection selection, TimeMs ms);
	bool hasPoint(QPoint point) const;
	TextState getState(QPoint point, HistoryStateRequest request) const;
	TextSelection adjustSelection(TextSelection selection, TextSelectType type) const;
	void updatePressed(QPoint point);

	QString getForwardedInfoText() const;

	~Item();

private:
	gsl::not_null<ChannelData*> channel() {
		return _history->peer->asChannel();
	}
	void addPart(HistoryItem *item);

	uint64 _id = 0;
	QDateTime _date;
	gsl::not_null<History*> _history;
	gsl::not_null<UserData*> _from;
	std::vector<HistoryItem*> _parts;
	int _top = 0;
	int _height = 0;

};

} // namespace AdminLog
