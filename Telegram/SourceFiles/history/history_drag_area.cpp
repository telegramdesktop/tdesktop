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
#include "stdafx.h"
#include "history/history_drag_area.h"

#include "styles/style_stickers.h"
#include "styles/style_boxes.h"
#include "boxes/confirmbox.h"
#include "boxes/stickersetbox.h"
#include "inline_bots/inline_bot_result.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "dialogs/dialogs_layout.h"
#include "historywidget.h"
#include "localstorage.h"
#include "lang.h"
#include "mainwindow.h"
#include "apiwrap.h"
#include "mainwidget.h"

DragArea::DragArea(QWidget *parent) : TWidget(parent)
, _hiding(false)
, _in(false)
, a_opacity(0)
, a_colorDrop(0)
, _a_appearance(animation(this, &DragArea::step_appearance))
, _shadow(st::boxShadow) {
	setMouseTracking(true);
	setAcceptDrops(true);
}

void DragArea::mouseMoveEvent(QMouseEvent *e) {
	if (_hiding) return;

	bool newIn = QRect(st::dragPadding.left(), st::dragPadding.top(), width() - st::dragPadding.left() - st::dragPadding.right(), height() - st::dragPadding.top() - st::dragPadding.bottom()).contains(e->pos());
	if (newIn != _in) {
		_in = newIn;
		a_opacity.start(1);
		a_colorDrop.start(_in ? 1. : 0.);
		_a_appearance.start();
	}
}

void DragArea::dragMoveEvent(QDragMoveEvent *e) {
	QRect r(st::dragPadding.left(), st::dragPadding.top(), width() - st::dragPadding.left() - st::dragPadding.right(), height() - st::dragPadding.top() - st::dragPadding.bottom());
	bool newIn = r.contains(e->pos());
	if (newIn != _in) {
		_in = newIn;
		a_opacity.start(1);
		a_colorDrop.start(_in ? 1. : 0.);
		_a_appearance.start();
	}
	e->setDropAction(_in ? Qt::CopyAction : Qt::IgnoreAction);
	e->accept();
}

void DragArea::setText(const QString &text, const QString &subtext) {
	_text = text;
	_subtext = subtext;
	update();
}

void DragArea::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (_a_appearance.animating()) {
		p.setOpacity(a_opacity.current());
	}

	QRect r(st::dragPadding.left(), st::dragPadding.top(), width() - st::dragPadding.left() - st::dragPadding.right(), height() - st::dragPadding.top() - st::dragPadding.bottom());

	// draw shadow
	_shadow.paint(p, r, st::boxShadowShift);

	p.fillRect(r, st::dragBg);

	p.setPen(anim::pen(st::dragColor, st::dragDropColor, a_colorDrop.current()));

	p.setFont(st::dragFont);
	p.drawText(QRect(0, (height() - st::dragHeight) / 2, width(), st::dragFont->height), _text, QTextOption(style::al_top));

	p.setFont(st::dragSubfont);
	p.drawText(QRect(0, (height() + st::dragHeight) / 2 - st::dragSubfont->height, width(), st::dragSubfont->height * 2), _subtext, QTextOption(style::al_top));
}

void DragArea::dragEnterEvent(QDragEnterEvent *e) {
	static_cast<HistoryWidget*>(parentWidget())->dragEnterEvent(e);
	e->setDropAction(Qt::IgnoreAction);
	e->accept();
}

void DragArea::dragLeaveEvent(QDragLeaveEvent *e) {
	static_cast<HistoryWidget*>(parentWidget())->dragLeaveEvent(e);
	_in = false;
	a_opacity.start(_hiding ? 0 : 1);
	a_colorDrop.start(_in ? 1. : 0.);
	_a_appearance.start();
}

void DragArea::dropEvent(QDropEvent *e) {
	static_cast<HistoryWidget*>(parentWidget())->dropEvent(e);
	if (e->isAccepted()) {
		emit dropped(e->mimeData());
	}
}

void DragArea::otherEnter() {
	showStart();
}

void DragArea::otherLeave() {
	hideStart();
}

void DragArea::hideFast() {
	if (_a_appearance.animating()) {
		_a_appearance.stop();
	}
	a_opacity = anim::fvalue(0, 0);
	hide();
}

void DragArea::hideStart() {
	_hiding = true;
	_in = false;
	a_opacity.start(0);
	a_colorDrop.start(_in ? 1. : 0.);
	_a_appearance.start();
}

void DragArea::hideFinish() {
	hide();
	_in = false;
	a_colorDrop = anim::fvalue(0.);
}

void DragArea::showStart() {
	_hiding = false;
	show();
	a_opacity.start(1);
	a_colorDrop.start(_in ? 1. : 0.);
	_a_appearance.start();
}

void DragArea::step_appearance(float64 ms, bool timer) {
	float64 dt = ms / st::defaultDropdownDuration;
	if (dt >= 1) {
		a_opacity.finish();
		a_colorDrop.finish();
		if (_hiding) {
			hideFinish();
		}
		_a_appearance.stop();
	} else {
		a_opacity.update(dt, anim::linear);
		a_colorDrop.update(dt, anim::linear);
	}
	if (timer) update();
}
