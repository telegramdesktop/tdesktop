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

class MediaView : public QWidget, public RPCSender, public Animated {
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

	void updateOver(const QPoint &mpos);

	void showPhoto(PhotoData *photo, HistoryItem *context);
	void showPhoto(PhotoData *photo, PeerData *context);
	void moveToScreen();
	void moveToPhoto(int32 delta);
	void preloadPhotos(int32 delta);

	void mediaOverviewUpdated(PeerData *peer);
	void changingMsgId(HistoryItem *row, MsgId newId);
	void updateControls();

	bool animStep(float64 dt);

	~MediaView();

public slots:

	void onClose();
	void onSave();
	void onForward();
	void onDelete();
	void onCopy();
	void onMenuDestroy(QObject *obj);
	void receiveMouse();

	void onCheckActive();
	void onTouchTimer();

private:

	void showPhoto(PhotoData *photo);
	void loadPhotosBack();

	void photosLoaded(History *h, const MTPmessages_Messages &msgs, mtpRequestId req);
	void userPhotosLoaded(UserData *u, const MTPphotos_Photos &photos, mtpRequestId req);

	void updateHeader();

	QTimer _timer;
	PhotoData *_photo;
	QRect _avail, _leftNav, _rightNav, _nameNav, _dateNav;
	bool _leftNavVisible, _rightNavVisible;
	QString _dateText;

	int32 _maxWidth, _maxHeight, _x, _y, _w;
	QPixmap _current;
	bool _full;

	History *_history; // if conversation photos overview
	PeerData *_peer;
	UserData *_user, *_from; // if user profile photos overview
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

	FlatButton _close, _save, _forward, _delete;
	QMenu *_menu;
	bool _receiveMouse;

	bool _touchPress, _touchMove, _touchRightButton;
	QTimer _touchTimer;
	QPoint _touchStart;

	typedef QMap<OverState, uint64> Showing;
	Showing _animations;
	typedef QMap<OverState, anim::fvalue> ShowingOpacities;
	ShowingOpacities _animOpacities;

	bool updateOverState(OverState newState);
	float64 overLevel(OverState control);
	QColor nameDateColor(float64 over);
};
