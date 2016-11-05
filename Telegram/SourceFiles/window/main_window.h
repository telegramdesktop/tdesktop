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

namespace Window {

class TitleWidget;

class MainWindow : public QMainWindow, protected base::Subscriber {
	Q_OBJECT

public:
	MainWindow();

	void init();
	HitTestResult hitTest(const QPoint &p) const;

	bool positionInited() const {
		return _positionInited;
	}
	void positionUpdated();

	virtual void closeWithoutDestroy();

	virtual ~MainWindow();

	QWidget *bodyWidget() {
		return _body;
	}

protected:
	void resizeEvent(QResizeEvent *e) override;

	void savePosition(Qt::WindowState state = Qt::WindowActive);

	virtual void initHook() {
	}

	virtual void stateChangedHook(Qt::WindowState state) {
	}

	// This one is overriden in Windows for historical reasons.
	virtual int32 screenNameChecksum(const QString &name) const;

	void setPositionInited();

private slots:
	void savePositionByTimer() {
		savePosition();
	}

private:
	void initSize();

	ChildObject<QTimer> _positionUpdatedTimer;
	bool _positionInited = false;

	ChildWidget<TitleWidget> _title = { nullptr };
	ChildWidget<QWidget> _body;

};

} // namespace Window
