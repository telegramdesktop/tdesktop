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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "abstractbox.h"

class ConfirmBox;

struct SessionData {
	uint64 hash;

	int32 activeTime;
	int32 nameWidth, activeWidth, infoWidth, ipWidth;
	QString name, active, info, ip;
};
typedef QList<SessionData> SessionsList;

class SessionsInner : public TWidget, public RPCSender {
	Q_OBJECT

public:

	SessionsInner(SessionsList *list, SessionData *current);

	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);

	void listUpdated();

	~SessionsInner();

signals:

	void oneTerminated();
	void allTerminated();
	void terminateAll();

public slots:

	void onTerminate();
	void onTerminateSure();
	void onTerminateAll();
	void onTerminateAllSure();
	void onNoTerminateBox(QObject *obj);

private:
	
	void terminateDone(uint64 hash, const MTPBool &result);
	bool terminateFail(uint64 hash, const RPCError &error);

	void terminateAllDone(const MTPBool &res);
	bool terminateAllFail(const RPCError &error);

	SessionsList *_list;
	SessionData *_current;

	typedef QMap<uint64, IconedButton*> TerminateButtons;
	TerminateButtons _terminateButtons;

	uint64 _terminating;
	LinkButton _terminateAll;
	ConfirmBox *_terminateBox;

};

class SessionsBox : public ScrollableBox, public RPCSender {
	Q_OBJECT

public:

	SessionsBox();
	void resizeEvent(QResizeEvent *e);
	void paintEvent(QPaintEvent *e);

public slots:

	void onOneTerminated();
	void onAllTerminated();
	void onTerminateAll();
	void onShortPollAuthorizations();
	void onNewAuthorization();

protected:

	void hideAll();
	void showAll();

private:

	void gotAuthorizations(const MTPaccount_Authorizations &result);

	bool _loading;

	SessionData _current;
	SessionsList _list;

	SessionsInner _inner;
	ScrollableBoxShadow _shadow;
	BoxButton _done;

	SingleTimer _shortPollTimer;
	mtpRequestId _shortPollRequest;

};
