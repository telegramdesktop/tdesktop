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

#include <QtWidgets/QWidget>
#include "sysbuttons.h"

class MainWindow;
namespace Media {
namespace Player {
class TitleButton;
class PanelEvent;
} // namespace Player
} // namespace Media
class AudioMsgId;

class TitleWidget : public TWidget, private base::Subscriber {
	Q_OBJECT

public:
	TitleWidget(QWidget *parent);

	void updateCounter();

	void mousePressEvent(QMouseEvent *e);
	void mouseDoubleClickEvent(QMouseEvent *e);

	void maximizedChanged(bool maximized, bool force = false);

	HitTestType hitTest(const QPoint &p);

	void setHideLevel(float64 level);

	void step_update(float64 ms, bool timer);

public slots:
	void onWindowStateChanged(Qt::WindowState state = Qt::WindowNoState);
	void updateControlsVisibility();
	void onContacts();
	void onAbout();

signals:
	void hiderClicked();

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void updateAdaptiveLayout();
	void updateRestartButtonVisibility();
	void updateMenuButtonsVisibility();
	void updateSystemButtonsVisibility();
	void updateControlsPosition();

	style::color statusColor;

	class Hider;
	float64 hideLevel = 0;
	ChildWidget<Hider> _hider = { nullptr };

	float64 _lastUpdateMs;

	FlatButton _cancel, _settings, _contacts, _about;

	ChildWidget<Media::Player::TitleButton> _player = { nullptr };
	LockBtn _lock;
	UpdateBtn _update;
	MinimizeBtn _minimize;
	MaximizeBtn _maximize;
	RestoreBtn _restore;
	CloseBtn _close;

	Animation _a_update;

	bool lastMaximized;

	QPixmap _counter;

};
