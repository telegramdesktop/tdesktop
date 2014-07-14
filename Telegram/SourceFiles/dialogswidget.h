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
	void searchReceived(const QVector<MTPMessage> &messages, bool fromStart, int32 fullCount);
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

	bool choosePeer();

	void destroyData();

	PeerData *peerBefore(const PeerData *peer) const;
	PeerData *peerAfter(const PeerData *peer) const;
	void scrollToPeer(const PeerId &peer);
	
	typedef QVector<FakeDialogRow*> SearchResults;

	DialogsIndexed &contactsList();
	DialogsIndexed &dialogsList();
	SearchResults &searchList();

	void setMouseSel(bool msel, bool toTop = false);

	enum State {
		DefaultState = 0,
		FilteredState = 1,
		SearchedState = 2,
	};
	void setState(State newState);
	State state() const;

	void onFilterUpdate(QString newFilter, bool force = false);

	~DialogsListWidget();

public slots:

	void onUpdateSelected(bool force = false);
	void onParentGeometryChanged();
	void onDialogToTop(const History::DialogLinks &links);
	void onPeerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars);
	void onPeerPhotoChanged(PeerData *peer);
	void onDialogRowReplaced(DialogRow *oldRow, DialogRow *newRow);

	void onItemRemoved(HistoryItem *item);
	void onItemReplaced(HistoryItem *oldItem, HistoryItem *newItem);

signals:

	void peerChosen(const PeerId &, MsgId);
	void mustScrollTo(int scrollToTop, int scrollToBottom);
	void dialogToTopFrom(int movedFrom);
	void searchMessages();

private:

	void addDialog(const MTPDdialog &dialog);
	void clearSearchResults();

	DialogsIndexed dialogs;
	DialogsIndexed contactsNoDialogs;
	DialogsIndexed contacts;
	DialogRow *sel;
	bool contactSel;
	bool selByMouse;

	QString filter;
	typedef QVector<DialogRow*> FilteredDialogs;
	FilteredDialogs filterResults;
	int32 filteredSel;

	SearchResults searchResults;
	int32 searchedCount, searchedSel;

	State _state;

	QPoint lastMousePos;

	void paintDialog(QPainter &p, DialogRow *dialog);

};

class DialogsWidget : public QWidget, public Animated, public RPCSender {
	Q_OBJECT

public:
	DialogsWidget(MainWidget *parent);

	void dialogsReceived(const MTPmessages_Dialogs &dialogs);
	void contactsReceived(const MTPcontacts_Contacts &contacts);
	void searchReceived(bool fromStart, const MTPmessages_Messages &result, mtpRequestId req);
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
	
	void onSearchMore(MsgId minMsgId);
	void clearFiltered();

signals:

	void peerChosen(const PeerId &, MsgId);
	void cancelled();

public slots:

	void onCancel();
	void onListScroll();
	void activate();
	void onFilterUpdate();
	void onAddContact();
	void onNewGroup();
	void onCancelSearch();

	void onDialogToTopFrom(int movedFrom);
	bool onSearchMessages(bool searchCache = false);
	void onNeedSearchMessages();

private:

	void loadConfig();
	bool _configLoaded;

	bool _drawShadow;

	void unreadCountsReceived(const QVector<MTPDialog> &dialogs);
	bool dialogsFailed(const RPCError &e);
	bool contactsFailed();
	bool searchFailed(const RPCError &error, mtpRequestId req);

	int32 dlgOffset, dlgCount;
	mtpRequestId dlgPreloading;
	mtpRequestId contactsRequest;

	FlatInput _filter;
	IconedButton _newGroup, _addContact, _cancelSearch;
	ScrollArea scroll;
	DialogsListWidget list;

	QTimer _searchTimer;
	QString _searchQuery;
	bool _searchFull;
	mtpRequestId _searchRequest;

	typedef QMap<QString, MTPmessages_Messages> SearchCache;
	SearchCache _searchCache;

	typedef QMap<mtpRequestId, QString> SearchQueries;
	SearchQueries _searchQueries;

};
