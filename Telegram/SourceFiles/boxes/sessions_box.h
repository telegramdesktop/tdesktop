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

#include "boxes/abstract_box.h"
#include "core/single_timer.h"

class ConfirmBox;

namespace Ui {
class IconButton;
class LinkButton;
} // namespace Ui

class SessionsBox : public BoxContent, public RPCSender {
	Q_OBJECT

public:
	SessionsBox(QWidget*);

protected:
	void prepare() override;

	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private slots:
	void onOneTerminated();
	void onAllTerminated();
	void onTerminateAll();
	void onShortPollAuthorizations();
	void onCheckNewAuthorization();

private:
	void setLoading(bool loading);
	struct Data {
		uint64 hash;

		int32 activeTime;
		int32 nameWidth, activeWidth, infoWidth, ipWidth;
		QString name, active, info, ip;
	};
	using List = QList<Data>;

	void gotAuthorizations(const MTPaccount_Authorizations &result);

	bool _loading = false;

	Data _current;
	List _list;

	class Inner;
	QPointer<Inner> _inner;

	object_ptr<SingleTimer> _shortPollTimer;
	mtpRequestId _shortPollRequest = 0;

};

// This class is hold in header because it requires Qt preprocessing.
class SessionsBox::Inner : public TWidget, public RPCSender {
	Q_OBJECT

public:
	Inner(QWidget *parent, SessionsBox::List *list, SessionsBox::Data *current);

	void listUpdated();

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

signals:
	void oneTerminated();
	void allTerminated();
	void terminateAll();

public slots:
	void onTerminate();
	void onTerminateAll();

private:
	void terminateDone(uint64 hash, const MTPBool &result);
	bool terminateFail(uint64 hash, const RPCError &error);

	void terminateAllDone(const MTPBool &res);
	bool terminateAllFail(const RPCError &error);

	SessionsBox::List *_list;
	SessionsBox::Data *_current;

	typedef QMap<uint64, Ui::IconButton*> TerminateButtons;
	TerminateButtons _terminateButtons;

	object_ptr<Ui::LinkButton> _terminateAll;
	QPointer<ConfirmBox> _terminateBox;

};
