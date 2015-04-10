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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "abstractbox.h"

class ContactsInner : public QWidget, public RPCSender {
	Q_OBJECT

private:

	struct ContactData;

public:

	ContactsInner(bool creatingChat);
	ContactsInner(ChatData *chat);
	void init();

	void paintEvent(QPaintEvent *e);
	void enterEvent(QEvent *e);
	void leaveEvent(QEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void resizeEvent(QResizeEvent *e);
	
	void paintDialog(QPainter &p, UserData *user, ContactData *data, bool sel);
	void updateFilter(QString filter = QString());

	void selectSkip(int32 dir);
	void selectSkipPage(int32 h, int32 dir);

	QVector<UserData*> selected();
	QVector<MTPInputUser> selectedInputs();
	PeerData *selectedUser();

	void loadProfilePhotos(int32 yFrom);
	void chooseParticipant();
	void changeCheckState(DialogRow *row);
	void changeCheckState(ContactData *data);

	void peopleReceived(const QString &query, const QVector<MTPContactFound> &people);

	void refresh();

	ChatData *chat() const;
	bool creatingChat() const;

	~ContactsInner();

signals:

	void mustScrollTo(int ymin, int ymax);
	void selectAllQuery();
	void searchByUsername();

public slots:

	void onDialogRowReplaced(DialogRow *oldRow, DialogRow *newRow);

	void updateSel();
	void peerUpdated(PeerData *peer);

private:

	ChatData *_chat;
	bool _creatingChat;
	
	int32 _time;

	DialogsIndexed *_contacts;
	DialogRow *_sel;
	QString _filter;
	typedef QVector<DialogRow*> FilteredDialogs;
	FilteredDialogs _filtered;
	int32 _filteredSel;
	bool _mouseSel;

	int32 _selCount;

	struct ContactData {
		Text name;
		QString online;
		bool inchat;
		bool check;
	};
	typedef QMap<UserData*, ContactData*> ContactsData;
	ContactsData _contactsData;

	ContactData *contactData(DialogRow *row);

	bool _searching;
	QString _lastQuery;
	typedef QVector<UserData*> ByUsernameRows;
	typedef QVector<ContactData*> ByUsernameDatas;
	ByUsernameRows _byUsername, _byUsernameFiltered;
	ByUsernameDatas d_byUsername, d_byUsernameFiltered; // filtered is partly subset of d_byUsername, partly subset of _byUsernameDatas
	ByUsernameDatas _byUsernameDatas;
	int32 _byUsernameSel;

	QPoint _lastMousePos;
	LinkButton _addContactLnk;

};

class ContactsBox : public ItemListBox, public RPCSender {
	Q_OBJECT

public:

	ContactsBox(bool creatingChat = false);
	ContactsBox(ChatData *chat);
	void keyPressEvent(QKeyEvent *e);
	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);

public slots:

	void onFilterUpdate();
	void onScroll();

	void onAdd();
	void onInvite();
	void onNext();

	bool onSearchByUsername(bool searchCache = false);
	void onNeedSearchByUsername();

protected:

	void hideAll();
	void showAll();
	void showDone();

private:

	void init();

	ContactsInner _inner;
	FlatButton _addContact;
	FlatInput _filter;

	FlatButton _next, _cancel;

	void peopleReceived(const MTPcontacts_Found &result, mtpRequestId req);
	bool peopleFailed(const RPCError &error, mtpRequestId req);

	QTimer _searchTimer;
	QString _peopleQuery;
	bool _peopleFull;
	mtpRequestId _peopleRequest;

	typedef QMap<QString, MTPcontacts_Found> PeopleCache;
	PeopleCache _peopleCache;

	typedef QMap<mtpRequestId, QString> PeopleQueries;
	PeopleQueries _peopleQueries;
};

class CreateGroupBox : public AbstractBox, public RPCSender {
	Q_OBJECT

public:

	CreateGroupBox(const MTPVector<MTPInputUser> &users);
	void keyPressEvent(QKeyEvent *e);
	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);

public slots:

	void onCreate();

protected:

	void hideAll();
	void showAll();
	void showDone();

private:

	void created(const MTPUpdates &updates);
	bool failed(const RPCError &e);

	MTPVector<MTPInputUser> _users;

	int32 _createRequestId;

	FlatInput _name;
	FlatButton _create, _cancel;
};
