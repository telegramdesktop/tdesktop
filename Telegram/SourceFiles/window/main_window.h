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

#include <rpl/event_stream.h>
#include "window/window_title.h"
#include "base/timer.h"

class MediaView;

namespace Window {

class Controller;
class TitleWidget;

QImage LoadLogo();
QImage LoadLogoNoMargin();
QIcon CreateIcon();

class MainWindow : public QWidget, protected base::Subscriber {
	Q_OBJECT

public:
	MainWindow();

	Window::Controller *controller() const {
		return _controller.get();
	}
	void setInactivePress(bool inactive);
	bool wasInactivePress() const {
		return _wasInactivePress;
	}

	bool hideNoQuit();

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
#if defined Q_OS_LINUX32 || defined Q_OS_LINUX64
		onReActivate();
		QTimer::singleShot(200, this, SLOT(onReActivate()));
#endif // Q_OS_LINUX32 || Q_OS_LINUX64
	}

	void showRightColumn(object_ptr<TWidget> widget);
	int maximalExtendBy() const;
	bool canExtendNoMove(int extendBy) const;

	// Returns how much could the window get extended.
	int tryToExtendWidthBy(int addToWidth);

	virtual void updateTrayMenu(bool force = false) {
	}

	virtual ~MainWindow();

	TWidget *bodyWidget() {
		return _body.data();
	}

	void launchDrag(std::unique_ptr<QMimeData> data);
	base::Observable<void> &dragFinished() {
		return _dragFinished;
	}

	rpl::producer<> leaveEvents() const;

public slots:
	bool minimizeToTray();
	void updateGlobalMenu() {
		updateGlobalMenuHook();
	}

protected:
	void resizeEvent(QResizeEvent *e) override;
	void leaveEvent(QEvent *e) override;

	void savePosition(Qt::WindowState state = Qt::WindowActive);
	void handleStateChanged(Qt::WindowState state);
	void handleActiveChanged();

	virtual void initHook() {
	}

	virtual void updateIsActiveHook() {
	}

	virtual void handleActiveChangedHook() {
	}

	void clearWidgets();
	virtual void clearWidgetsHook() {
	}

	virtual void updateWindowIcon();

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

private slots:
	void savePositionByTimer() {
		savePosition();
	}
	void onReActivate();

private:
	void checkAuthSession();
	void updatePalette();
	void updateUnreadCounter();
	void initSize();

	bool computeIsActive() const;

	object_ptr<QTimer> _positionUpdatedTimer;
	bool _positionInited = false;

	std::unique_ptr<Window::Controller> _controller;
	object_ptr<TitleWidget> _title = { nullptr };
	object_ptr<TWidget> _body;
	object_ptr<TWidget> _rightColumn = { nullptr };

	QIcon _icon;
	QString _titleText;

	bool _isActive = false;
	base::Timer _isActiveTimer;
	bool _wasInactivePress = false;
	base::Timer _inactivePressTimer;

	base::Observable<void> _dragFinished;
	rpl::event_stream<> _leaveEvents;

};

} // namespace Window
