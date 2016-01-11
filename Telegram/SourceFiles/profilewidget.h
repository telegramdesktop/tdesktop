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

class ProfileWidget;
class ProfileInner : public TWidget, public RPCSender {
	Q_OBJECT

public:

	ProfileInner(ProfileWidget *profile, ScrollArea *scroll, PeerData *peer);

	void start();

	void peerUsernameChanged();

	bool event(QEvent *e);
	void paintEvent(QPaintEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void mouseReleaseEvent(QMouseEvent *e);
	void keyPressEvent(QKeyEvent *e);
	void enterEvent(QEvent *e);
	void leaveEvent(QEvent *e);
	void leaveToChildEvent(QEvent *e);
	void resizeEvent(QResizeEvent *e);
	void contextMenuEvent(QContextMenuEvent *e);

	void step_photo(float64 ms, bool timer);

	PeerData *peer() const;
	bool allMediaShown() const;
	
	void updateOnlineDisplay();
	void updateOnlineDisplayTimer();
	void reorderParticipants();

	void saveError(const QString &str = QString());

	void loadProfilePhotos(int32 yFrom);

	void updateNotifySettings();
	int32 mediaOverviewUpdated(PeerData *peer, MediaOverviewType type); // returns scroll shift

	void requestHeight(int32 newHeight);
	int32 countMinHeight();
	void allowDecreaseHeight(int32 decreaseBy);

	~ProfileInner();
	
public slots:

	void peerUpdated(PeerData *data);
	void updateSelected();

	void openContextImage();
	void deleteContextImage();

	void onShareContact();
	void onInviteToGroup();
	void onSendMessage();
	void onSearchInPeer();
	void onEnableNotifications();

	void onClearHistory();
	void onClearHistorySure();
	void onDeleteConversation();
	void onDeleteConversationSure();
	void onDeleteChannel();
	void onDeleteChannelSure();
	void onBlockUser();
	void onAddParticipant();
	void onMigrate();
	void onMigrateSure();

	void onUpdatePhoto();
	void onUpdatePhotoCancel();

	void onPhotoUpdateDone(PeerId peer);
	void onPhotoUpdateFail(PeerId peer);
	void onPhotoUpdateStart();

	void onKickConfirm();

	void onMediaPhotos();
	void onMediaVideos();
	void onMediaSongs();
	void onMediaDocuments();
	void onMediaAudios();
	void onMediaLinks();

	void onMenuDestroy(QObject *obj);
	void onCopyPhone();
	void onCopyUsername();

	void onInvitationLink();
	void onCreateInvitationLink();
	void onCreateInvitationLinkSure();
	void onPublicLink();

	void onMembers();
	void onAdmins();

	void onFullPeerUpdated(PeerData *peer);

	void onBotSettings();
	void onBotHelp();

private:

	void showAll();
	void updateInvitationLink();
	void updateBotLinksVisibility();

	void chatInviteDone(const MTPExportedChatInvite &result);
	bool updateMediaLinks(int32 *addToScroll = 0); // returns if anything changed

	void migrateDone(const MTPUpdates &updates);
	bool migrateFail(const RPCError &error);

	ProfileWidget *_profile;
	ScrollArea *_scroll;

	PeerData *_peer;
	UserData *_peerUser;
	ChatData *_peerChat;
	ChannelData *_peerChannel;
	History *_migrated, *_history;
	bool _amCreator;

	int32 _width, _left, _addToHeight;

	// profile
	Text _nameText;
	QString _nameCache;
	QString _phoneText;
	TextLinkPtr _photoLink;
	FlatButton _uploadPhoto, _addParticipant;
	FlatButton _sendMessage, _shareContact, _inviteToGroup;
	LinkButton _cancelPhoto, _createInvitationLink, _invitationLink;
	QString _invitationText;
	LinkButton _botSettings, _botHelp, _username, _members, _admins;

	Text _about;
	int32 _aboutTop, _aboutHeight;

	anim::fvalue a_photoOver;
	Animation _a_photo;
	bool _photoOver;

	QString _errorText;

	// migrate to megagroup
	bool _showMigrate, _forceShowMigrate;
	Text _aboutMigrate;
	FlatButton _migrate;

	// settings
	FlatCheckbox _enableNotifications;

	// shared media
	bool _notAllMediaLoaded;
	LinkButton *_mediaButtons[OverviewCount];
	QString overviewLinkText(int32 type, int32 count);

	// actions
	LinkButton _searchInPeer, _clearHistory, _deleteConversation;
	UserBlockedStatus _wasBlocked;
	mtpRequestId _blockRequest;
	LinkButton _blockUser, _deleteChannel;

	// participants
	int32 _pHeight;
	int32 _kickWidth, _selectedRow, _lastPreload;
	uint64 _contactId;
	UserData *_kickOver, *_kickDown, *_kickConfirm;

	typedef struct {
		Text name;
		QString online;
		bool cankick;
	} ParticipantData;
	typedef QVector<UserData*> Participants;
	Participants _participants;
	typedef QVector<ParticipantData*> ParticipantsData;
	ParticipantsData _participantsData;

	QPoint _lastPos;

	QString _onlineText;
	PopupMenu *_menu;

	QString _secretText;

	void blockDone(bool blocked, const MTPBool &result);
	bool blockFail(const RPCError &error);

};

class ProfileWidget : public TWidget, public RPCSender {
	Q_OBJECT

public:

	ProfileWidget(QWidget *parent, PeerData *peer);

	void resizeEvent(QResizeEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void paintEvent(QPaintEvent *e);
    void dragEnterEvent(QDragEnterEvent *e);
    void dropEvent(QDropEvent *e);
	void keyPressEvent(QKeyEvent *e);

	void paintTopBar(QPainter &p, float64 over, int32 decreaseWidth);
	void topBarClick();

	PeerData *peer() const;
	int32 lastScrollTop() const;

	void animShow(const QPixmap &oldAnimCache, const QPixmap &bgAnimTopBarCache, bool back = false, int32 lastScrollTop = -1);
	void step_show(float64 ms, bool timer);

	void updateOnlineDisplay();
	void updateOnlineDisplayTimer();

	void peerUsernameChanged();

	void updateNotifySettings();
	void mediaOverviewUpdated(PeerData *peer, MediaOverviewType type);
	void updateWideMode();

	void grabStart() {
		_sideShadow.hide();
		_inGrab = true;
		resizeEvent(0);
	}
	void grabFinish() {
		_sideShadow.setVisible(cWideMode());
		_inGrab = false;
		resizeEvent(0);
	}

	void clear();
	~ProfileWidget();

public slots:

	void activate();
	void onScroll();

private:

	ScrollArea _scroll;
	ProfileInner _inner;

	Animation _a_show;
	QPixmap _cacheUnder, _cacheOver, _cacheTopBarUnder, _cacheTopBarOver;
	anim::ivalue a_coordUnder, a_coordOver;
	anim::fvalue a_shadow;

	PlainShadow _sideShadow, _topShadow;
	bool _inGrab;

};

