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

class MainWidget;

class DialogsListWidget : public QWidget {
	Q_OBJECT

public:

	DialogsListWidget(QWidget *parent, MainWidget *main);

	void dialogsReceived(const QVector<MTPDialog> &dialogs);
	void showMore(int32 pixels);

	void activate();

	void contactsReceived(const QVector<MTPContact> &contacts);
	int32 addNewContact(int32 uid, bool sel = false); // return y of row or -1 if failed

	void paintEvent(QPaintEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void enterEvent(QEvent *e);
	void leaveEvent(QEvent *e);

	void selectSkip(int32 direction);
	void selectSkipPage(int32 pixels, int32 direction);

	void createDialogAtTop(History *history, int32 unreadCount);
	void dlgUpdated(DialogRow *row);
	void dlgUpdated(History *row);
	void removePeer(PeerData *peer);
	void removeContact(UserData *user);

	void loadPeerPhotos(int32 yFrom);
	void clearFilter();
	void refresh(bool toTop = false);

	void choosePeer();

	void destroyData();

	PeerData *peerBefore(const PeerData *peer) const;
	PeerData *peerAfter(const PeerData *peer) const;
	void scrollToPeer(const PeerId &peer);
	
	DialogsIndexed &contactsList();
	DialogsIndexed &dialogsList();

	void setMouseSel(bool msel, bool toTop = false);

public slots:

	void onUpdateSelected(bool force = false);
	void onParentGeometryChanged();
	void onDialogToTop(const History::DialogLinks &links);
	void onPeerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars);
	void onPeerPhotoChanged(PeerData *peer);
	void onDialogRowReplaced(DialogRow *oldRow, DialogRow *newRow);
	void onFilterUpdate(QString newFilter);

signals:

	void peerChosen(const PeerId &);
	void mustScrollTo(int scrollToTop, int scrollToBottom);
	void dialogToTopFrom(int movedFrom);

private:

	void addDialog(const MTPDdialog &dialog);

	DialogsIndexed dialogs;
	DialogsIndexed contactsNoDialogs;
	DialogsIndexed contacts;
	DialogRow *sel;
	bool contactSel;
	bool selByMouse;

	QString filter;
	typedef QVector<DialogRow*> FilteredDialogs;
	FilteredDialogs filtered;
	int32 filteredSel;

	QPoint lastMousePos;

	void paintDialog(QPainter &p, DialogRow *dialog);

};

class DialogsWidget : public QWidget, public Animated, public RPCSender {
	Q_OBJECT

public:
	DialogsWidget(MainWidget *parent);

	void dialogsReceived(const MTPmessages_Dialogs &dialogs);
	void contactsReceived(const MTPcontacts_Contacts &contacts);
	bool addNewContact(int32 uid, bool show = true);
	
	void resizeEvent(QResizeEvent *e);
	void keyPressEvent(QKeyEvent *e);
	void paintEvent(QPaintEvent *e);

	void loadDialogs();
	void createDialogAtTop(History *history, int32 unreadCount);
	void dlgUpdated(DialogRow *row);
	void dlgUpdated(History *row);

	void dialogsToUp();

	void regTyping(History *history, UserData *user);

	bool animStep(float64 ms);

	void setInnerFocus();

	void destroyData();

	PeerData *peerBefore(const PeerData *peer) const;
	PeerData *peerAfter(const PeerData *peer) const;
	void scrollToPeer(const PeerId &peer);

	void removePeer(PeerData *peer);
	void removeContact(UserData *user);

	DialogsIndexed &contactsList();

	void enableShadow(bool enable = true);

signals:

	void peerChosen(const PeerId &);
	void cancelled();

public slots:

	void onCancel();
	void onListScroll();
	void activate();
	void onFilterUpdate();
	void onAddContact();
	void onNewGroup();

	void onDialogToTopFrom(int movedFrom);

private:

	void loadConfig();
	bool _configLoaded;

	bool _drawShadow;

	void unreadCountsReceived(const QVector<MTPDialog> &dialogs);
	bool dialogsFailed(const RPCError &e);

	bool contactsFailed();

	int32 dlgOffset, dlgCount;
	mtpRequestId dlgPreloading;
	mtpRequestId contactsRequest;

	FlatInput _filter;
	IconedButton _newGroup, _addContact;
	ScrollArea scroll;
	DialogsListWidget list;
};
