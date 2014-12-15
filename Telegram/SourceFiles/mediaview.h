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

class MediaView : public TWidget, public RPCSender, public Animated {
	Q_OBJECT

public:

	MediaView();

	void paintEvent(QPaintEvent *e);
	
	void keyPressEvent(QKeyEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mouseReleaseEvent(QMouseEvent *e);
	void contextMenuEvent(QContextMenuEvent *e);
	void touchEvent(QTouchEvent *e);

	bool event(QEvent *e);

	void hide();

	void updateOver(const QPoint &mpos);

	void showPhoto(PhotoData *photo, HistoryItem *context);
	void showPhoto(PhotoData *photo, PeerData *context);
	void showDocument(DocumentData *doc, QPixmap pix, HistoryItem *context);
	void moveToScreen();
	void moveToPhoto(int32 delta);
	void preloadPhotos(int32 delta);

	void mediaOverviewUpdated(PeerData *peer);
	void changingMsgId(HistoryItem *row, MsgId newId);
	void updateControls();

	bool animStep(float64 dt);

	void showSaveMsgFile();

	~MediaView();

public slots:

	void onClose();
	void onSave();
	void onDownload();
	void onShowInFolder();
	void onForward();
	void onDelete();
	void onOverview();
	void onCopy();
	void onMenuDestroy(QObject *obj);
	void receiveMouse();

	void onCheckActive();
	void onTouchTimer();

	void updateImage();

private:

	void showPhoto(PhotoData *photo);
	void loadPhotosBack();

	void photosLoaded(History *h, const MTPmessages_Messages &msgs, mtpRequestId req);
	void userPhotosLoaded(UserData *u, const MTPphotos_Photos &photos, mtpRequestId req);

	void updateHeader();
	void updatePolaroid();
	void snapXY();

	QBrush _transparentBrush;

	QTimer _timer;
	PhotoData *_photo;
	DocumentData *_doc;
	QRect _avail, _leftNav, _rightNav, _bottomBar, _nameNav, _dateNav, _polaroidOut, _polaroidIn;
	int32 _availBottom;
	bool _leftNavVisible, _rightNavVisible;
	QString _dateText;

	uint64 _animStarted;

	int32 _maxWidth, _maxHeight, _width, _x, _y, _w, _h, _xStart, _yStart;
	int32 _zoom; // < 0 - out, 0 - none, > 0 - in
	float64 _zoomToScreen; // for documents
	QPoint _mStart;
	bool _pressed;
	int32 _dragging;
	QPixmap _current;
	int32 _full; // -1 - thumb, 0 - medium, 1 - full

	History *_history; // if conversation photos overview
	PeerData *_peer;
	UserData *_user, *_from; // if user profile photos overview
	Text _fromName;
	int32 _index; // index in photos array, -1 if just photo
	MsgId _msgid; // msgId of current photo

	QString _header;

	mtpRequestId _loadRequest;

	enum OverState {
		OverNone,
		OverLeftNav,
		OverRightNav,
		OverName,
		OverDate
	};
	OverState _over, _down;
	QPoint _lastAction;

	IconedButton _close, _save, _forward, _delete, _overview;
	ContextMenu *_menu;
	bool _receiveMouse;

	bool _touchPress, _touchMove, _touchRightButton;
	QTimer _touchTimer;
	QPoint _touchStart;

	QString _saveMsgFilename;
	uint64 _saveMsgStarted;
	anim::fvalue _saveMsgOpacity;
	QRect _saveMsg;
	QTimer _saveMsgUpdater;
	Text _saveMsgText;

	typedef QMap<OverState, uint64> Showing;
	Showing _animations;
	typedef QMap<OverState, anim::fvalue> ShowingOpacities;
	ShowingOpacities _animOpacities;

	bool updateOverState(OverState newState);
	float64 overLevel(OverState control);
	QColor overColor(const QColor &a, float64 ca, const QColor &b, float64 cb);

};
