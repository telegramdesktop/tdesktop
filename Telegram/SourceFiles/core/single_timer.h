/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/basic_types.h"

class SingleTimer : public QTimer { // single shot timer with check
	Q_OBJECT

public:
	SingleTimer(QObject *parent = nullptr);

	void setSingleShot(bool); // is not available
	void start(); // is not available

	void setTimeoutHandler(Fn<void()> handler);

public slots:
	void start(int msec);
	void startIfNotActive(int msec);

private slots:
	void adjust();
	void onTimeout();

private:
	TimeMs _finishing = 0;
	Fn<void()> _handler;

};
