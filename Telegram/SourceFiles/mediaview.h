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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "dropdown.h"

class MediaView : public TWidget, public RPCSender, public ClickHandlerHost {
	Q_OBJECT

public:

	MediaView();

	void paintEvent(QPaintEvent *e) override;

	void keyPressEvent(QKeyEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void touchEvent(QTouchEvent *e);

	bool event(QEvent *e) override;

	void hide();

	void updateOver(QPoint mpos);

	void showPhoto(PhotoData *photo, HistoryItem *context);
	void showPhoto(PhotoData *photo, PeerData *context);
	void showDocument(DocumentData *doc, HistoryItem *context);
	void moveToScreen();
	bool moveToNext(int32 delta);
	void preloadData(int32 delta);

	void leaveToChildEvent(QEvent *e, QWidget *child) override { // e -- from enterEvent() of child TWidget
		updateOverState(OverNone);
	}
	void enterFromChildEvent(QEvent *e, QWidget *child) override { // e -- from leaveEvent() of child TWidget
		updateOver(mapFromGlobal(QCursor::pos()));
	}

	void mediaOverviewUpdated(PeerData *peer, MediaOverviewType type);
	void documentUpdated(DocumentData *doc);
	void changingMsgId(HistoryItem *row, MsgId newId);
	void updateDocSize();
	void updateControls();
	void updateDropdown();

	void showSaveMsgFile();
	void close();

	void activateControls();
	void onDocClick();

	void clipCallback(ClipReaderNotification notification);
	PeerData *ui_getPeerForMouseAction();

	void clearData();

	~MediaView();

	// ClickHandlerHost interface
	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) override;

public slots:

	void onHideControls(bool force = false);
	void onDropdownHiding();

	void onScreenResized(int screen);

	void onToMessage();
	void onSaveAs();
	void onDownload();
	void onSaveCancel();
	void onShowInFolder();
	void onForward();
	void onDelete();
	void onOverview();
	void onCopy();
	void onMenuDestroy(QObject *obj);
	void receiveMouse();

	void onDropdown();

	void onCheckActive();
	void onTouchTimer();

	void updateImage();

private:

	void displayPhoto(PhotoData *photo, HistoryItem *item);
	void displayDocument(DocumentData *doc, HistoryItem *item);
	void findCurrent();
	void loadBack();

	// Radial animation interface.
	float64 radialProgress() const;
	bool radialLoading() const;
	QRect radialRect() const;
	void radialStart();
	uint64 radialTimeShift() const;

	// Computes the last OverviewChatPhotos PhotoData* from _history or _migrated.
	struct LastChatPhoto {
		HistoryItem *item;
		PhotoData *photo;
	};
	LastChatPhoto computeLastOverviewChatPhoto();
	void computeAdditionalChatPhoto(PeerData *peer, PhotoData *lastOverviewPhoto);

	void userPhotosLoaded(UserData *u, const MTPphotos_Photos &photos, mtpRequestId req);

	void deletePhotosDone(const MTPVector<MTPlong> &result);
	bool deletePhotosFail(const RPCError &error);

	void updateHeader();
	void snapXY();

	void step_state(uint64 ms, bool timer);
	void step_radial(uint64 ms, bool timer);

	QBrush _transparentBrush;

	PhotoData *_photo = nullptr;
	DocumentData *_doc = nullptr;
	MediaOverviewType _overview = OverviewCount;
	QRect _closeNav, _closeNavIcon;
	QRect _leftNav, _leftNavIcon, _rightNav, _rightNavIcon;
	QRect _headerNav, _nameNav, _dateNav;
	QRect _saveNav, _saveNavIcon, _moreNav, _moreNavIcon;
	bool _leftNavVisible = false;
	bool _rightNavVisible = false;
	bool _saveVisible = false;
	bool _headerHasLink = false;
	QString _dateText;
	QString _headerText;

	Text _caption;
	QRect _captionRect;

	uint64 _animStarted;

	int _width = 0;
	int _x = 0, _y = 0, _w = 0, _h = 0;
	int _xStart = 0, _yStart = 0;
	int _zoom = 0; // < 0 - out, 0 - none, > 0 - in
	float64 _zoomToScreen = 0.; // for documents
	QPoint _mStart;
	bool _pressed = false;
	int32 _dragging = 0;
	QPixmap _current;
	ClipReader *_gif = nullptr;
	int32 _full = -1; // -1 - thumb, 0 - medium, 1 - full

	bool fileShown() const;
	bool gifShown() const;
	void stopGif();

	style::sprite _docIcon;
	style::color _docIconColor;
	QString _docName, _docSize, _docExt;
	int _docNameWidth = 0, _docSizeWidth = 0, _docExtWidth = 0;
	QRect _docRect, _docIconRect;
	int _docThumbx = 0, _docThumby = 0, _docThumbw = 0;
	LinkButton _docDownload, _docSaveAs, _docCancel;

	QRect _photoRadialRect;
	RadialAnimation _radial;

	History *_migrated = nullptr;
	History *_history = nullptr; // if conversation photos or files overview
	PeerData *_peer = nullptr;
	UserData *_user = nullptr; // if user profile photos overview

	// There can be additional first photo in chat photos overview, that is not
	// in the _history->overview[OverviewChatPhotos] (if the item was deleted).
	PhotoData *_additionalChatPhoto = nullptr;

	// We save the information about the reason of the current mediaview show:
	// did we open a peer profile photo or a photo from some message.
	// We use it when trying to delete a photo: if we've opened a peer photo,
	// then we'll delete group photo instead of the corresponding message.
	bool _firstOpenedPeerPhoto = false;

	PeerData *_from = nullptr;
	Text _fromName;

	int _index = -1; // index in photos or files array, -1 if just photo
	MsgId _msgid = 0; // msgId of current photo or file
	bool _msgmigrated = false; // msgId is from _migrated history
	ChannelId _channel = NoChannel;
	bool _canForward = false;
	bool _canDelete = false;

	mtpRequestId _loadRequest = 0;

	enum OverState {
		OverNone,
		OverLeftNav,
		OverRightNav,
		OverClose,
		OverHeader,
		OverName,
		OverDate,
		OverSave,
		OverMore,
		OverIcon,
	};
	OverState _over = OverNone;
	OverState _down = OverNone;
	QPoint _lastAction, _lastMouseMovePos;
	bool _ignoringDropdown = false;

	Animation _a_state;

	enum ControlsState {
		ControlsShowing,
		ControlsShown,
		ControlsHiding,
		ControlsHidden,
	};
	ControlsState _controlsState = ControlsShown;
	uint64 _controlsAnimStarted = 0;
	QTimer _controlsHideTimer;
	anim::fvalue a_cOpacity;

	PopupMenu *_menu = nullptr;
	Dropdown _dropdown;
	IconedButton *_btnSaveCancel, *_btnToMessage, *_btnShowInFolder, *_btnSaveAs, *_btnCopy, *_btnForward, *_btnDelete, *_btnViewAll;
	QList<IconedButton*> _btns;

	bool _receiveMouse = true;

	bool _touchPress = false, _touchMove = false, _touchRightButton = false;
	QTimer _touchTimer;
	QPoint _touchStart;
	QPoint _accumScroll;

	QString _saveMsgFilename;
	uint64 _saveMsgStarted = 0;
	anim::fvalue _saveMsgOpacity = { 0 };
	QRect _saveMsg;
	QTimer _saveMsgUpdater;
	Text _saveMsgText;

	typedef QMap<OverState, uint64> Showing;
	Showing _animations;
	typedef QMap<OverState, anim::fvalue> ShowingOpacities;
	ShowingOpacities _animOpacities;

	void updateOverRect(OverState state);
	bool updateOverState(OverState newState);
	float64 overLevel(OverState control);
	QColor overColor(const QColor &a, float64 ca, const QColor &b, float64 cb);

};
