/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#pragma once

#include "layerwidget.h"

class ContactsInner : public QWidget, public RPCSender {
	Q_OBJECT

public:

	ContactsInner();

	void paintEvent(QPaintEvent *e);
	void enterEvent(QEvent *e);
	void leaveEvent(QEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mousePressEvent(QMouseEvent *e);
	
	void paintDialog(QPainter &p, DialogRow *row, bool sel);
	void updateFilter(QString filter = QString());

	void selectSkip(int32 dir);
	void selectSkipPage(int32 h, int32 dir);

	void loadProfilePhotos(int32 yFrom);

	~ContactsInner();

signals:

	void mustScrollTo(int ymin, int ymax);

public slots:

	void onDialogRowReplaced(DialogRow *oldRow, DialogRow *newRow);

	void updateSel();
	void peerUpdated(PeerData *peer);

	void chooseParticipant();

private:

	int32 _time;

	DialogsIndexed *_contacts;
	DialogRow *_sel;
	QString _filter;
	typedef QVector<DialogRow*> FilteredDialogs;
	FilteredDialogs _filtered;
	int32 _filteredSel;
	bool _mouseSel;

	typedef struct {
		Text name;
		QString online;
	} ContactData;
	typedef QMap<UserData*, ContactData*> ContactsData;
	ContactsData _contactsData;

	ContactData *contactData(DialogRow *row);

	QPoint _lastMousePos;

};

class ContactsBox : public LayeredWidget, public RPCSender {
	Q_OBJECT

public:

	ContactsBox();
	void parentResized();
	void animStep(float64 dt);
	void keyPressEvent(QKeyEvent *e);
	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);
	void startHide();
	~ContactsBox();

public slots:

	void onFilterUpdate();
	void onClose();
	void onScroll();
	void onAdd();

private:

	void hideAll();
	void showAll();

	void created(const MTPmessages_StatedMessage &result);
	bool failed(const RPCError &e);

	ScrollArea _scroll;
	ContactsInner _inner;
	FlatButton _addContact;
	int32 _width, _height;
	FlatInput _filter;
	BottomButton _close;
	bool _hiding;

	QPixmap _cache;

	anim::fvalue a_opacity;
};
