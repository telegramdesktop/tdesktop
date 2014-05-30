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

#include "gui/flatbutton.h"
#include "gui/flatcheckbox.h"
#include "sysbuttons.h"

#include <QtWidgets/QWidget>

class Window;
class Settings;

class Slider : public QWidget {
	Q_OBJECT

public:

	Slider(QWidget *parent, const style::slider &st, int32 count, int32 sel = 0);

	void mousePressEvent(QMouseEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mouseReleaseEvent(QMouseEvent *e);

	int32 selected() const;
	void setSelected(int32 sel);

	void paintEvent(QPaintEvent *e);

signals:

	void changed(int32 oldSelected);

private:

	int32 _count, _sel, _wasSel;
	style::slider _st;
	bool _pressed;

};

class SettingsInner : public QWidget, public RPCSender, public Animated {
	Q_OBJECT

public:

	SettingsInner(Settings *parent);

	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);
	void keyPressEvent(QKeyEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void contextMenuEvent(QContextMenuEvent *e);

	bool animStep(float64 ms);

	void updateSize(int32 newWidth);
	bool getPhotoCoords(PhotoData *photo, int32 &x, int32 &y, int32 &w) const;

	void updateOnlineDisplay();

	void gotFullSelf(const MTPUserFull &self);

	void showAll();

public slots:

	void updateConnectionType();

	void peerUpdated(PeerData *data);

	void onUpdatePhoto();
	void onUpdatePhotoCancel();

	void onAutoUpdate();
	void onCheckNow();
	void onRestartNow();

	void onConnectionType();

	void onWorkmodeTray();
	void onWorkmodeWindow();

	void onAutoStart();
	void onStartMinimized();

	void onScaleAuto();
	void onScaleChange();

	void onSoundNotify();
	void onDesktopNotify();

	void onReplaceEmojis();
	void onViewEmojis();

	void onEnterSend();
	void onCtrlEnterSend();

	void onDontAskDownloadPath();
	void onDownloadPathEdit();
	void onDownloadPathEdited();
	void onDownloadPathClear();
	void onDownloadPathClearSure();
	void onTempDirCleared();
	void onTempDirClearFailed();

	void onCatsAndDogs();

	void onUpdateChecking();
	void onUpdateLatest();
	void onUpdateDownloading(qint64 ready, qint64 total);
	void onUpdateReady();
	void onUpdateFailed();

	void onLogout();
	void onResetSessions();

	void onPhotoUpdateDone(PeerId peer);
	void onPhotoUpdateFail(PeerId peer);
	void onPhotoUpdateStart();

private:

	void doneResetSessions(const MTPBool &res);
	void saveError(const QString &str = QString());

	void setScale(DBIScale newScale);

	UserData *_self;
	int32 _left;

	// profile
	Text _nameText;
	QString _nameCache;
	QString _phoneText;
	TextLinkPtr _photoLink;
	FlatButton _uploadPhoto;
	LinkButton _cancelPhoto;
	bool _nameOver, _photoOver;
	anim::fvalue a_photo;

	QString _errorText;

	// notifications
	FlatCheckbox _desktopNotify, _soundNotify;

	// general
	FlatCheckbox _autoUpdate;
	LinkButton _checkNow, _restartNow;
	FlatCheckbox _workmodeTray, _workmodeWindow;
	FlatCheckbox _autoStart, _startMinimized;
	FlatCheckbox _dpiAutoScale;
	Slider _dpiSlider;
	int32 _dpiWidth1, _dpiWidth2, _dpiWidth3, _dpiWidth4;

	QString _curVersionText, _newVersionText;
	int32 _curVersionWidth, _newVersionWidth;

	enum UpdatingState {
		UpdatingNone,
		UpdatingCheck,
		UpdatingLatest,
		UpdatingDownload,
		UpdatingFail,
		UpdatingReady
	};
	UpdatingState _updatingState;
	QString _newVersionDownload;

	// chat options
	FlatCheckbox _replaceEmojis;
	LinkButton _viewEmojis;
	FlatRadiobutton _enterSend, _ctrlEnterSend;
	FlatCheckbox _dontAskDownloadPath;
	int32 _downloadPathWidth;
	LinkButton _downloadPathEdit, _downloadPathClear;
	int32 _tempDirClearingWidth, _tempDirClearedWidth, _tempDirClearFailedWidth;
	enum TempDirClearState {
		TempDirClearFailed = 0,
		TempDirEmpty       = 1,
		TempDirExists      = 2,
		TempDirClearing    = 3,
		TempDirCleared     = 4,
	};
	TempDirClearState _tempDirClearState;
	FlatCheckbox _catsAndDogs;

	// advanced
	LinkButton _connectionType, _resetSessions;
	FlatButton _logOut;

	QString _connectionTypeText;
	int32 _connectionTypeWidth;

	bool _resetDone;

	void setUpdatingState(UpdatingState state, bool force = false);
	void setDownloadProgress(qint64 ready, qint64 total);

};

class Settings : public QWidget, public Animated {
	Q_OBJECT

public:

	Settings(Window *parent);

	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);
	void dragEnterEvent(QDragEnterEvent *e);
	void dropEvent(QDropEvent *e);

	bool getPhotoCoords(PhotoData *photo, int32 &x, int32 &y, int32 &w) const;

	void animShow(const QPixmap &bgAnimCache, bool back = false);
	bool animStep(float64 ms);

	void updateOnlineDisplay();
	void updateConnectionType();

	~Settings();

public slots:

	void onParentResize(const QSize &newSize);
	
private:

	void showAll();
	void hideAll();

	QPixmap _animCache, _bgAnimCache;
	anim::ivalue a_coord, a_bgCoord;
	anim::fvalue a_alpha, a_bgAlpha;

	ScrollArea _scroll;
	SettingsInner _inner;
	IconedButton _close;
};
