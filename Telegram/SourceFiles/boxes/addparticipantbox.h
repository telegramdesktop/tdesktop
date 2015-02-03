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

#include "layerwidget.h"

class AddParticipantInner : public QWidget, public RPCSender {
	Q_OBJECT

public:

	AddParticipantInner(ChatData *chat);

	void paintEvent(QPaintEvent *e);
	void enterEvent(QEvent *e);
	void leaveEvent(QEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void resizeEvent(QResizeEvent *e);

	void paintDialog(QPainter &p, DialogRow *row, bool sel);
	void updateFilter(QString filter = QString());

	void selectSkip(int32 dir);
	void selectSkipPage(int32 h, int32 dir);

	void loadProfilePhotos(int32 yFrom);
	void chooseParticipant();
	void changeCheckState(DialogRow *row);

	ChatData *chat();
	QVector<UserData*> selected();

	~AddParticipantInner();

signals:

	void mustScrollTo(int ymin, int ymax);
	void selectAllQuery();

public slots:

	void onDialogRowReplaced(DialogRow *oldRow, DialogRow *newRow);

	void updateSel();
	void peerUpdated(PeerData *peer);

private:

	ChatData *_chat;

	int32 _time;

	DialogsIndexed *_contacts;
	DialogRow *_sel;
	QString _filter;
	typedef QVector<DialogRow*> FilteredDialogs;
	FilteredDialogs _filtered;
	int32 _filteredSel;
	bool _mouseSel;

	int32 _selCount;

	typedef struct {
		Text name;
		QString online;
		bool inchat;
		bool check;
	} ContactData;
	typedef QMap<UserData*, ContactData*> ContactsData;
	ContactsData _contactsData;

	ContactData *contactData(DialogRow *row);

	QPoint _lastMousePos;
	LinkButton _addContactLnk;

};

class AddParticipantBox : public LayeredWidget {
	Q_OBJECT

public:

	AddParticipantBox(ChatData *chat);
	void parentResized();
	void animStep(float64 dt);
	void keyPressEvent(QKeyEvent *e);
	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);
	void startHide();
	~AddParticipantBox();

public slots:

	void onFilterUpdate();
	void onClose();
	void onScroll();
	void onInvite();

private:

	void hideAll();
	void showAll();

	ScrollArea _scroll;
	AddParticipantInner _inner;
	int32 _width, _height;
	FlatInput _filter;
	FlatButton _invite, _cancel;

	bool _hiding;

	QPixmap _cache;

	anim::fvalue a_opacity;
	anim::transition af_opacity;
};
