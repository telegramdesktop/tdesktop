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

class MainWidget;

class DialogsInner : public SplittedWidget {
	Q_OBJECT

public:

	DialogsInner(QWidget *parent, MainWidget *main);

	void dialogsReceived(const QVector<MTPDialog> &dialogs);
	void addSavedPeersAfter(const QDateTime &date);
	void addAllSavedPeers();
	void searchReceived(const QVector<MTPMessage> &messages, bool fromStart, int32 fullCount);
	void peopleReceived(const QString &query, const QVector<MTPPeer> &people);
	void showMore(int32 pixels);

	void activate();

	void contactsReceived(const QVector<MTPContact> &contacts);
	int32 addNewContact(int32 uid, bool sel = false); // -2 - err, -1 - don't scroll, >= 0 - scroll

	int32 filteredOffset() const;
	int32 peopleOffset() const;
	int32 searchedOffset() const;

	void mouseMoveEvent(QMouseEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void resizeEvent(QResizeEvent *e);
	void enterEvent(QEvent *e);
	void leaveEvent(QEvent *e);

	void peopleResultPaint(PeerData *peer, Painter &p, int32 w, bool act, bool sel, bool onlyBackground) const;
	void searchInPeerPaint(Painter &p, int32 w, bool onlyBackground) const;

	void selectSkip(int32 direction);
	void selectSkipPage(int32 pixels, int32 direction);

	void createDialog(History *history);
	void moveDialogToTop(const History::DialogLinks &links);
	void dlgUpdated(DialogRow *row);
	void dlgUpdated(History *row, MsgId msgId);
	void removePeer(PeerData *peer);
	void removeContact(UserData *user);

	void loadPeerPhotos(int32 yFrom);
	void clearFilter();
	void refresh(bool toTop = false);

	bool choosePeer();
	void saveRecentHashtags(const QString &text);

	void destroyData();

	void peerBefore(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg) const;
	void peerAfter(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg) const;
	void scrollToPeer(const PeerId &peer, MsgId msgId);

	typedef QVector<DialogRow*> FilteredDialogs;
	typedef QVector<PeerData*> PeopleResults;
	typedef QVector<FakeDialogRow*> SearchResults;

	DialogsIndexed &contactsList();
	DialogsIndexed &dialogsList();
	FilteredDialogs &filteredList();
	PeopleResults &peopleList();
	SearchResults &searchList();
	MsgId lastSearchId() const;

	void setMouseSel(bool msel, bool toTop = false);

	enum State {
		DefaultState = 0,
		FilteredState = 1,
		SearchedState = 2,
	};
	void setState(State newState);
	State state() const;
	bool hasFilteredResults() const;

	void searchInPeer(PeerData *peer);

	void onFilterUpdate(QString newFilter, bool force = false);
	void onHashtagFilterUpdate(QStringRef newFilter);
	void itemRemoved(HistoryItem *item);
	void itemReplaced(HistoryItem *oldItem, HistoryItem *newItem);

	PeerData *updateFromParentDrag(QPoint globalPos);

	~DialogsInner();

public slots:

	void onUpdateSelected(bool force = false);
	void onParentGeometryChanged();
	void onPeerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars);
	void onPeerPhotoChanged(PeerData *peer);
	void onDialogRowReplaced(DialogRow *oldRow, DialogRow *newRow);

signals:

	void mustScrollTo(int scrollToTop, int scrollToBottom);
	void dialogMoved(int movedFrom, int movedTo);
	void searchMessages();
	void searchResultChosen();
	void cancelSearchInPeer();
	void completeHashtag(QString tag);
	void refreshHashtags();

protected:

	void paintRegion(Painter &p, const QRegion &region, bool paintingOther);

private:

	void clearSearchResults(bool clearPeople = true);
	void updateSelectedRow();

	DialogsIndexed dialogs;
	DialogsIndexed contactsNoDialogs;
	DialogsIndexed contacts;
	DialogRow *sel;
	bool contactSel;
	bool selByMouse;

	QString filter, _hashtagFilter;

	QStringList hashtagResults;
	int32 hashtagSel;

	FilteredDialogs filterResults;
	int32 filteredSel;

	SearchResults searchResults;
	int32 searchedCount, searchedSel;

	QString peopleQuery;
	PeopleResults peopleResults;
	int32 peopleSel;

	MsgId _lastSearchId;

	State _state;

	QPoint lastMousePos;

	void paintDialog(QPainter &p, DialogRow *dialog);

	LinkButton _addContactLnk;
	IconedButton _cancelSearchInPeer;

	bool _overDelete;

	PeerData *_searchInPeer;

};

class DialogsWidget : public TWidget, public Animated, public RPCSender {
	Q_OBJECT

public:
	DialogsWidget(MainWidget *parent);

	void dialogsReceived(const MTPmessages_Dialogs &dialogs, mtpRequestId req);
	void contactsReceived(const MTPcontacts_Contacts &contacts);
	void searchReceived(bool fromStart, const MTPmessages_Messages &result, mtpRequestId req);
	void peopleReceived(const MTPcontacts_Found &result, mtpRequestId req);
	bool addNewContact(int32 uid, bool show = true);
	
	void dragEnterEvent(QDragEnterEvent *e);
	void dragMoveEvent(QDragMoveEvent *e);
	void dragLeaveEvent(QDragLeaveEvent *e);
	void dropEvent(QDropEvent *e);
	void updateDragInScroll(bool inScroll);

	void resizeEvent(QResizeEvent *e);
	void keyPressEvent(QKeyEvent *e);
	void paintEvent(QPaintEvent *e);

	void searchInPeer(PeerData *peer);

	void loadDialogs();
	void createDialog(History *history);
	void dlgUpdated(DialogRow *row);
	void dlgUpdated(History *row, MsgId msgId);

	void dialogsToUp();

	void animShow(const QPixmap &bgAnimCache);
	bool animStep(float64 ms);

	void destroyData();

	void peerBefore(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg) const;
	void peerAfter(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg) const;
	void scrollToPeer(const PeerId &peer, MsgId msgId);

	void removePeer(PeerData *peer);
	void removeContact(UserData *user);

	DialogsIndexed &contactsList();
	DialogsIndexed &dialogsList();

	void searchMessages(const QString &query, PeerData *inPeer = 0);
	void onSearchMore(MsgId minMsgId);

	void itemRemoved(HistoryItem *item);
	void itemReplaced(HistoryItem *oldItem, HistoryItem *newItem);

signals:

	void cancelled();

public slots:

	void onCancel();
	void onListScroll();
	void activate();
	void onFilterUpdate(bool force = false);
	void onAddContact();
	void onNewGroup();
	bool onCancelSearch();
	void onCancelSearchInPeer();

	void onFilterCursorMoved(int from = -1, int to = -1);
	void onCompleteHashtag(QString tag);

	void onDialogMoved(int movedFrom, int movedTo);
	bool onSearchMessages(bool searchCache = false);
	void onNeedSearchMessages();

	void onChooseByDrag();

private:

	bool _dragInScroll, _dragForward;
	QTimer _chooseByDragTimer;

	void unreadCountsReceived(const QVector<MTPDialog> &dialogs);
	bool dialogsFailed(const RPCError &error, mtpRequestId req);
	bool contactsFailed(const RPCError &error);
	bool searchFailed(const RPCError &error, mtpRequestId req);
	bool peopleFailed(const RPCError &error, mtpRequestId req);

	int32 _dialogsOffset, _dialogsCount;
	mtpRequestId _dialogsRequest, _channelDialogsRequest, _contactsRequest;

	FlatInput _filter;
	IconedButton _newGroup, _addContact, _cancelSearch;
	ScrollArea _scroll;
	DialogsInner _inner;

	QPixmap _animCache, _bgAnimCache;
	anim::ivalue a_coord, a_bgCoord;
	anim::fvalue a_alpha, a_bgAlpha;

	PeerData *_searchInPeer;

	QTimer _searchTimer;
	QString _searchQuery, _peopleQuery;
	bool _searchFull, _peopleFull;
	mtpRequestId _searchRequest, _peopleRequest;

	typedef QMap<QString, MTPmessages_Messages> SearchCache;
	SearchCache _searchCache;

	typedef QMap<mtpRequestId, QString> SearchQueries;
	SearchQueries _searchQueries;

	typedef QMap<QString, MTPcontacts_Found> PeopleCache;
	PeopleCache _peopleCache;

	typedef QMap<mtpRequestId, QString> PeopleQueries;
	PeopleQueries _peopleQueries;

};
