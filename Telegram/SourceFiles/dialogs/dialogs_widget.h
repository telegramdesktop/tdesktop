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

#include "window/section_widget.h"
#include "ui/widgets/scroll_area.h"

class DialogsInner;

namespace Dialogs {
class Row;
class FakeRow;
class IndexedList;
} // namespace Dialogs

namespace Ui {
class IconButton;
class PopupMenu;
class DropdownMenu;
class FlatButton;
class FlatInput;
class CrossButton;
template <typename Widget>
class WidgetScaledFadeWrap;
} // namespace Ui

namespace Window {
class Controller;
} // namespace Window

enum DialogsSearchRequestType {
	DialogsSearchFromStart,
	DialogsSearchFromOffset,
	DialogsSearchPeerFromStart,
	DialogsSearchPeerFromOffset,
	DialogsSearchMigratedFromStart,
	DialogsSearchMigratedFromOffset,
};

class DialogsWidget : public Window::AbstractSectionWidget, public RPCSender {
	Q_OBJECT

public:
	DialogsWidget(QWidget *parent, not_null<Window::Controller*> controller);

	void updateDragInScroll(bool inScroll);

	void searchInPeer(PeerData *peer);

	void loadDialogs();
	void loadPinnedDialogs();
	void createDialog(History *history);
	void dlgUpdated(Dialogs::Mode list, Dialogs::Row *row);
	void dlgUpdated(PeerData *peer, MsgId msgId);

	void dialogsToUp();

	void startWidthAnimation();
	void stopWidthAnimation();

	bool hasTopBarShadow() const {
		return true;
	}
	void showAnimated(Window::SlideDirection direction, const Window::SectionSlideParams &params);
	void showFast();

	void destroyData();

	void peerBefore(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg) const;
	void peerAfter(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg) const;
	void scrollToPeer(const PeerId &peer, MsgId msgId);

	void removeDialog(History *history);

	Dialogs::IndexedList *contactsList();
	Dialogs::IndexedList *dialogsList();
	Dialogs::IndexedList *contactsNoDialogsList();

	void searchMessages(const QString &query, PeerData *inPeer = 0);
	void onSearchMore();

	// Float player interface.
	bool wheelEventFromFloatPlayer(QEvent *e, Window::Column myColumn, Window::Column playerColumn) override;
	QRect rectForFloatPlayer(Window::Column myColumn, Window::Column playerColumn) override;

	void notify_userIsContactChanged(UserData *user, bool fromThisApp);
	void notify_historyMuteUpdated(History *history);

signals:
	void cancelled();

public slots:
	void onDraggingScrollDelta(int delta);

	void onCancel();
	void onListScroll();
	void activate();
	void onFilterUpdate(bool force = false);
	bool onCancelSearch();
	void onCancelSearchInPeer();

	void onFilterCursorMoved(int from = -1, int to = -1);
	void onCompleteHashtag(QString tag);

	void onDialogMoved(int movedFrom, int movedTo);
	bool onSearchMessages(bool searchCache = false);
	void onNeedSearchMessages();

	void onChooseByDrag();

private slots:
	void onDraggingScrollTimer();

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	void onCheckUpdateStatus();
#endif // TDESKTOP_DISABLE_AUTOUPDATE

protected:
	void dragEnterEvent(QDragEnterEvent *e) override;
	void dragMoveEvent(QDragMoveEvent *e) override;
	void dragLeaveEvent(QDragLeaveEvent *e) override;
	void dropEvent(QDropEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void animationCallback();
	void dialogsReceived(const MTPmessages_Dialogs &dialogs, mtpRequestId requestId);
	void pinnedDialogsReceived(const MTPmessages_PeerDialogs &dialogs, mtpRequestId requestId);
	void contactsReceived(const MTPcontacts_Contacts &result);
	void searchReceived(DialogsSearchRequestType type, const MTPmessages_Messages &result, mtpRequestId requestId);
	void peerSearchReceived(const MTPcontacts_Found &result, mtpRequestId requestId);

	void setSearchInPeer(PeerData *peer, UserData *from = nullptr);
	void showSearchFrom();
	void showMainMenu();
	void clearSearchCache();
	void updateLockUnlockVisibility();
	void updateJumpToDateVisibility(bool fast = false);
	void updateSearchFromVisibility(bool fast = false);
	void updateControlsGeometry();
	void updateForwardBar();

	void unreadCountsReceived(const QVector<MTPDialog> &dialogs);
	bool dialogsFailed(const RPCError &error, mtpRequestId req);
	bool contactsFailed(const RPCError &error);
	bool searchFailed(DialogsSearchRequestType type, const RPCError &error, mtpRequestId req);
	bool peopleFailed(const RPCError &error, mtpRequestId req);

	bool _dragInScroll = false;
	bool _dragForward = false;
	QTimer _chooseByDragTimer;

	bool _dialogsFull = false;
	int32 _dialogsOffsetDate = 0;
	MsgId _dialogsOffsetId = 0;
	PeerData *_dialogsOffsetPeer = nullptr;
	mtpRequestId _dialogsRequestId = 0;
	mtpRequestId _pinnedDialogsRequestId = 0;
	mtpRequestId _contactsRequestId = 0;
	bool _pinnedDialogsReceived = false;

	object_ptr<Ui::IconButton> _forwardCancel = { nullptr };
	object_ptr<Ui::IconButton> _mainMenuToggle;
	object_ptr<Ui::FlatInput> _filter;
	object_ptr<Ui::WidgetScaledFadeWrap<Ui::IconButton>> _chooseFromUser;
	object_ptr<Ui::WidgetScaledFadeWrap<Ui::IconButton>> _jumpToDate;
	object_ptr<Ui::CrossButton> _cancelSearch;
	object_ptr<Ui::IconButton> _lockUnlock;
	object_ptr<Ui::ScrollArea> _scroll;
	QPointer<DialogsInner> _inner;
	class UpdateButton;
	object_ptr<UpdateButton> _updateTelegram = { nullptr };

	Animation _a_show;
	Window::SlideDirection _showDirection;
	QPixmap _cacheUnder, _cacheOver;

	PeerData *_searchInPeer = nullptr;
	PeerData *_searchInMigrated = nullptr;
	UserData *_searchFromUser = nullptr;
	QString _lastFilterText;

	QTimer _searchTimer;

	QString _peerSearchQuery;
	bool _peerSearchFull = false;
	mtpRequestId _peerSearchRequest = 0;

	QString _searchQuery;
	UserData *_searchQueryFrom = nullptr;
	bool _searchFull = false;
	bool _searchFullMigrated = false;
	mtpRequestId _searchRequest = 0;

	using SearchCache = QMap<QString, MTPmessages_Messages>;
	SearchCache _searchCache;

	using SearchQueries = QMap<mtpRequestId, QString>;
	SearchQueries _searchQueries;

	using PeerSearchCache = QMap<QString, MTPcontacts_Found>;
	PeerSearchCache _peerSearchCache;

	using PeerSearchQueries = QMap<mtpRequestId, QString>;
	PeerSearchQueries _peerSearchQueries;

	QPixmap _widthAnimationCache;

	object_ptr<QTimer> _draggingScrollTimer = { nullptr };
	int _draggingScrollDelta = 0;

};
