/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/algorithm.h"

#include <QtCore/QObject>

namespace base {

class qt_connection final {
public:
	qt_connection(QMetaObject::Connection data = {}) : _data(data) {
	}
	qt_connection(qt_connection &&other) : _data(base::take(other._data)) {
	}
	qt_connection &operator=(qt_connection &&other) {
		reset(base::take(other._data));
		return *this;
	}
	~qt_connection() {
		disconnect();
	}

	void release() {
		_data = QMetaObject::Connection();
	}
	void reset(QMetaObject::Connection data = {}) {
		disconnect();
		_data = data;
	}

private:
	void disconnect() {
		if (_data) {
			QObject::disconnect(base::take(_data));
		}
	}

	QMetaObject::Connection _data;

};

} // namespace base
