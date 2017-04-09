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

#include "window/window_title.h"

class MediaView;

namespace Window {

class TitleWidget;

class MainWindow : public QWidget, protected base::Subscriber {
	Q_OBJECT

public:
	MainWindow();

	bool hideNoQuit();
	void hideMediaview();

	void init();
	HitTestResult hitTest(const QPoint &p) const;
	void updateIsActive(int timeout);
	bool isActive() const {
		return _isActive;
	}

	bool positionInited() const {
		return _positionInited;
	}
	void positionUpdated();

	bool titleVisible() const;
	void setTitleVisible(bool visible);
	QString titleText() const {
		return _titleText;
	}

	void reActivateWindow() {
		onReActivate();
		QTimer::singleShot(200, this, SLOT(onReActivate()));
	}

	void showPhoto(const PhotoOpenClickHandler *lnk, HistoryItem *item = 0);
	void showPhoto(PhotoData *photo, HistoryItem *item);
	void showPhoto(PhotoData *photo, PeerData *item);
	void showDocument(DocumentData *doc, HistoryItem *item);
	bool ui_isMediaViewShown();

	QWidget *filedialogParent();

	void showRightColumn(object_ptr<TWidget> widget);
	bool canExtendWidthBy(int addToWidth);
	void tryToExtendWidthBy(int addToWidth);

	virtual void updateTrayMenu(bool force = false) {
	}

	// TODO: rewrite using base::Observable
	void documentUpdated(DocumentData *doc);
	virtual void changingMsgId(HistoryItem *row, MsgId newId);

	virtual ~MainWindow();

	TWidget *bodyWidget() {
		return _body.data();
	}
	virtual PeerData *ui_getPeerForMouseAction();

public slots:
	bool minimizeToTray();
	void updateGlobalMenu() {
		updateGlobalMenuHook();
	}

protected:
	void resizeEvent(QResizeEvent *e) override;

	void savePosition(Qt::WindowState state = Qt::WindowActive);

	virtual void initHook() {
	}

	virtual void updateIsActiveHook() {
	}

	void clearWidgets();
	virtual void clearWidgetsHook() {
	}

	virtual void stateChangedHook(Qt::WindowState state) {
	}

	virtual void titleVisibilityChangedHook() {
	}

	virtual void unreadCounterChangedHook() {
	}

	virtual void closeWithoutDestroy() {
		hide();
	}

	virtual void updateGlobalMenuHook() {
	}

	virtual bool hasTrayIcon() const {
		return false;
	}
	virtual void showTrayTooltip() {
	}

	virtual void workmodeUpdated(DBIWorkMode mode) {
	}

	virtual void updateControlsGeometry();

	// This one is overriden in Windows for historical reasons.
	virtual int32 screenNameChecksum(const QString &name) const;

	void setPositionInited();

	void createMediaView();

private slots:
	void savePositionByTimer() {
		savePosition();
	}
	void onReActivate();
	void updateIsActiveByTimer() {
		updateIsActive(0);
	}

private:
	void updatePalette();
	void updateUnreadCounter();
	void initSize();

	bool computeIsActive() const;

	object_ptr<QTimer> _positionUpdatedTimer;
	bool _positionInited = false;

	object_ptr<TitleWidget> _title = { nullptr };
	object_ptr<TWidget> _body;
	object_ptr<TWidget> _rightColumn = { nullptr };

	QString _titleText;

	object_ptr<QTimer> _isActiveTimer;
	bool _isActive = false;

	object_ptr<MediaView> _mediaView = { nullptr };

};

} // namespace Window
