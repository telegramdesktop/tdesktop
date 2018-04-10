/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
