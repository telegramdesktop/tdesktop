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

	bool positionInited() const {
		return _positionInited;
	}
	void positionUpdated();

	bool titleVisible() const;
	void setTitleVisible(bool visible);
	QString titleText() const {
		return _titleText;
	}

	QWidget *filedialogParent();

	virtual ~MainWindow();

	TWidget *bodyWidget() {
		return _body.data();
	}

public slots:
	bool minimizeToTray();

protected:
	void resizeEvent(QResizeEvent *e) override;

	void savePosition(Qt::WindowState state = Qt::WindowActive);

	virtual void initHook() {
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

	// This one is overriden in Windows for historical reasons.
	virtual int32 screenNameChecksum(const QString &name) const;

	void setPositionInited();

	void createMediaView();

private slots:
	void savePositionByTimer() {
		savePosition();
	}
	void onReActivate();

private:
	void updatePalette();
	void updateControlsGeometry();
	void updateUnreadCounter();
	void initSize();

	object_ptr<QTimer> _positionUpdatedTimer;
	bool _positionInited = false;

	object_ptr<TitleWidget> _title = { nullptr };
	object_ptr<TWidget> _body;

	QString _titleText;

	object_ptr<MediaView> _mediaView = { nullptr };

};

} // namespace Window
