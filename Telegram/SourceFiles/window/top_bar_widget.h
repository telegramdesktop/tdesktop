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
class IconedButton;

namespace Window {

class TopBarWidget : public TWidget {
	Q_OBJECT

public:
	TopBarWidget(MainWidget *w);

	void enterEvent(QEvent *e) override;
	void enterFromChildEvent(QEvent *e, QWidget *child) override;
	void leaveEvent(QEvent *e) override;
	void leaveToChildEvent(QEvent *e, QWidget *child) override;
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void step_appearance(float64 ms, bool timer);

	void startAnim();
	void stopAnim();
	void showAll();
	void showSelected(uint32 selCount, bool canDelete = false);

	void updateAdaptiveLayout();

	void updateMembersShowArea();

	Ui::RoundButton *mediaTypeButton();

protected:
	bool eventFilter(QObject *obj, QEvent *e) override;

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
	anim::fvalue a_over = { 0. };
	Animation _a_appearance;

	PeerData *_selPeer = nullptr;
	int _selCount = 0;
	bool _canDelete = false;

	bool _animating = false;

	ChildWidget<Ui::RoundButton> _clearSelection;
	ChildWidget<Ui::RoundButton> _forward, _delete;

	ChildWidget<Ui::PeerAvatarButton> _info;
	ChildWidget<Ui::RoundButton> _mediaType;

	ChildWidget<IconedButton> _search;
	ChildWidget<TWidget> _membersShowArea = { nullptr };

};

} // namespace Window
