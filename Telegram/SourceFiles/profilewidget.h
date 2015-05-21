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

class ProfileWidget;
class ProfileInner : public TWidget, public RPCSender, public Animated {
	Q_OBJECT

public:

	ProfileInner(ProfileWidget *profile, ScrollArea *scroll, const PeerData *peer);

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

	bool animStep(float64 ms);

	PeerData *peer() const;
	bool allMediaShown() const;
	
	void updateOnlineDisplay();
	void updateOnlineDisplayTimer();
	void reorderParticipants();

	void saveError(const QString &str = QString());

	void loadProfilePhotos(int32 yFrom);

	void updateNotifySettings();
	void mediaOverviewUpdated(PeerData *peer);

	~ProfileInner();
	
public slots:

	void peerUpdated(PeerData *data);
	void updateSelected();

	void openContextImage();
	void deleteContextImage();

	void onShareContact();
	void onSendMessage();
	void onEnableNotifications();

	void onClearHistory();
	void onClearHistorySure();
	void onAddParticipant();

	void onUpdatePhoto();
	void onUpdatePhotoCancel();

	void onPhotoUpdateDone(PeerId peer);
	void onPhotoUpdateFail(PeerId peer);
	void onPhotoUpdateStart();

	void onKickConfirm();

	void onMediaShowAll();
	void onMediaPhotos();
	void onMediaVideos();
	void onMediaDocuments();
	void onMediaAudios();

	void onMenuDestroy(QObject *obj);
	void onCopyPhone();
	void onCopyUsername();

	void onInvitationLink();
	void onCreateInvitationLink();
	void onCreateInvitationLinkSure();

	void onFullPeerLoaded(PeerData *peer);

private:

	void showAll();
	void updateInvitationLink();

	void chatInviteDone(const MTPExportedChatInvite &result);

	ProfileWidget *_profile;
	ScrollArea *_scroll;

	PeerData *_peer;
	UserData *_peerUser;
	ChatData *_peerChat;
	History *_hist;
	bool _chatAdmin;

	int32 _width, _left;

	// profile
	Text _nameText;
	QString _nameCache;
	QString _phoneText;
	TextLinkPtr _photoLink;
	FlatButton _uploadPhoto, _addParticipant;
	FlatButton _sendMessage, _shareContact;
	LinkButton _cancelPhoto, _createInvitationLink, _invitationLink;
	QString _invitationText;

	anim::fvalue a_photo;
	bool _photoOver;

	QString _errorText;

	// settings
	FlatCheckbox _enableNotifications;
	LinkButton _clearHistory;

	// shared media
	bool _allMediaTypes;
	LinkButton _mediaShowAll, _mediaPhotos, _mediaVideos, _mediaDocuments, _mediaAudios;
	LinkButton *_mediaLinks[OverviewCount];
	QString overviewLinkText(int32 type, int32 count);

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
	ContextMenu *_menu;

};

class ProfileWidget : public QWidget, public RPCSender, public Animated {
	Q_OBJECT

public:

	ProfileWidget(QWidget *parent, const PeerData *peer);

	void resizeEvent(QResizeEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void paintEvent(QPaintEvent *e);
    void dragEnterEvent(QDragEnterEvent *e);
    void dropEvent(QDropEvent *e);

	void paintTopBar(QPainter &p, float64 over, int32 decreaseWidth);
	void topBarClick();

	PeerData *peer() const;
	int32 lastScrollTop() const;
	bool allMediaShown() const;

	void animShow(const QPixmap &oldAnimCache, const QPixmap &bgAnimTopBarCache, bool back = false, int32 lastScrollTop = -1, bool allMediaShown = false);
	bool animStep(float64 ms);

	void updateOnlineDisplay();
	void updateOnlineDisplayTimer();

	void updateNotifySettings();
	void mediaOverviewUpdated(PeerData *peer);

	~ProfileWidget();

public slots:

	void activate();
	void onScroll();

private:

	ScrollArea _scroll;
	ProfileInner _inner;

	bool _showing;
	QPixmap _animCache, _bgAnimCache, _animTopBarCache, _bgAnimTopBarCache;
	anim::ivalue a_coord, a_bgCoord;
	anim::fvalue a_alpha, a_bgAlpha;

};

