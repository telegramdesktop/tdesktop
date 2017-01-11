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

#include "core/basic_types.h"

class SingleTimer : public QTimer { // single shot timer with check
	Q_OBJECT

public:
	SingleTimer(QObject *parent = nullptr);

	void setSingleShot(bool); // is not available
	void start(); // is not available

	void setTimeoutHandler(base::lambda<void()> &&handler);

public slots:
	void start(int msec);
	void startIfNotActive(int msec);

private slots:
	void adjust();
	void onTimeout();

private:
	TimeMs _finishing = 0;
	bool _inited = false;
	base::lambda<void()> _handler;

};
