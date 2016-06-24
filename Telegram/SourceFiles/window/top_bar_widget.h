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

#include "ui/twidget.h"

namespace Ui {
class PeerAvatarButton;
class RoundButton;
} // namespace Ui
class FlatButton;
class IconedButton;

namespace Window {

class TopBarWidget : public TWidget {
	Q_OBJECT

public:
	TopBarWidget(MainWidget *w);

	void enterEvent(QEvent *e) override;
	void enterFromChildEvent(QEvent *e) override;
	void leaveEvent(QEvent *e) override;
	void leaveToChildEvent(QEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void step_appearance(float64 ms, bool timer);

	void startAnim();
	void stopAnim();
	void showAll();
	void showSelected(uint32 selCount, bool canDelete = false);

	void updateAdaptiveLayout();

	Ui::RoundButton *mediaTypeButton();

public slots:
	void onForwardSelection();
	void onDeleteSelection();
	void onClearSelection();
	void onInfoClicked();
	void onSearch();

signals:
	void clicked();

private:
	MainWidget *main();
	anim::fvalue a_over;
	Animation _a_appearance;

	PeerData *_selPeer = nullptr;
	uint32 _selCount;
	bool _canDelete;
	QString _selStr;
	int32 _selStrLeft, _selStrWidth;

	bool _animating;

	ChildWidget<Ui::RoundButton> _clearSelection;
	ChildWidget<FlatButton> _forward, _delete;
	int _selectionButtonsWidth, _forwardDeleteWidth;

	ChildWidget<Ui::PeerAvatarButton> _info;
	ChildWidget<Ui::RoundButton> _mediaType;

	ChildWidget<IconedButton> _search;

};

} // namespace Window
