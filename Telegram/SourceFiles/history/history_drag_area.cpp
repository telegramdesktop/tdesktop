/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_drag_area.h"

#include "styles/style_chat_helpers.h"
#include "styles/style_boxes.h"
#include "boxes/confirm_box.h"
#include "boxes/sticker_set_box.h"
#include "inline_bots/inline_bot_result.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "dialogs/dialogs_layout.h"
#include "history/history_widget.h"
#include "storage/localstorage.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "apiwrap.h"
#include "mainwidget.h"
#include "ui/widgets/shadow.h"

DragArea::DragArea(QWidget *parent) : TWidget(parent) {
	setMouseTracking(true);
	setAcceptDrops(true);
}

bool DragArea::overlaps(const QRect &globalRect) {
	if (isHidden() || _a_opacity.animating()) {
		return false;
	}

	auto inner = innerRect();
	auto testRect = QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size());
	return inner.marginsRemoved(QMargins(st::boxRadius, 0, st::boxRadius, 0)).contains(testRect)
		|| inner.marginsRemoved(QMargins(0, st::boxRadius, 0, st::boxRadius)).contains(testRect);
}


void DragArea::mouseMoveEvent(QMouseEvent *e) {
	if (_hiding) return;

	auto in = QRect(st::dragPadding.left(), st::dragPadding.top(), width() - st::dragPadding.left() - st::dragPadding.right(), height() - st::dragPadding.top() - st::dragPadding.bottom()).contains(e->pos());
	setIn(in);
}

void DragArea::dragMoveEvent(QDragMoveEvent *e) {
	QRect r(st::dragPadding.left(), st::dragPadding.top(), width() - st::dragPadding.left() - st::dragPadding.right(), height() - st::dragPadding.top() - st::dragPadding.bottom());
	setIn(r.contains(e->pos()));
	e->setDropAction(_in ? Qt::CopyAction : Qt::IgnoreAction);
	e->accept();
}

void DragArea::setIn(bool in) {
	if (_in != in) {
		_in = in;
		_a_in.start([this] { update(); }, _in ? 0. : 1., _in ? 1. : 0., st::boxDuration);
	}
}

void DragArea::setText(const QString &text, const QString &subtext) {
	_text = text;
	_subtext = subtext;
	update();
}

void DragArea::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto ms = getms();
	auto opacity = _a_opacity.current(ms, _hiding ? 0. : 1.);
	if (!_a_opacity.animating() && _hiding) {
		return;
	}
	p.setOpacity(opacity);
	auto inner = innerRect();

	if (!_cache.isNull()) {
		p.drawPixmapLeft(inner.x() - st::boxRoundShadow.extend.left(), inner.y() - st::boxRoundShadow.extend.top(), width(), _cache);
		return;
	}

	Ui::Shadow::paint(p, inner, width(), st::boxRoundShadow);
	App::roundRect(p, inner, st::boxBg, BoxCorners);

	p.setPen(anim::pen(st::dragColor, st::dragDropColor, _a_in.current(ms, _in ? 1. : 0.)));

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
	setIn(false);
}

void DragArea::dropEvent(QDropEvent *e) {
	static_cast<HistoryWidget*>(parentWidget())->dropEvent(e);
	if (e->isAccepted() && _droppedCallback) {
		_droppedCallback(e->mimeData());
	}
}

void DragArea::otherEnter() {
	showStart();
}

void DragArea::otherLeave() {
	hideStart();
}

void DragArea::hideFast() {
	_a_opacity.finish();
	hide();
}

void DragArea::hideStart() {
	if (_hiding || isHidden()) {
		return;
	}
	if (_cache.isNull()) {
		_cache = Ui::GrabWidget(
			this,
			innerRect().marginsAdded(st::boxRoundShadow.extend));
	}
	_hiding = true;
	setIn(false);
	_a_opacity.start([this] { opacityAnimationCallback(); }, 1., 0., st::boxDuration);
}

void DragArea::hideFinish() {
	hide();
	_in = false;
	_a_in.finish();
}

void DragArea::showStart() {
	if (!_hiding && !isHidden()) {
		return;
	}
	_hiding = false;
	if (_cache.isNull()) {
		_cache = Ui::GrabWidget(
			this,
			innerRect().marginsAdded(st::boxRoundShadow.extend));
	}
	show();
	_a_opacity.start([this] { opacityAnimationCallback(); }, 0., 1., st::boxDuration);
}

void DragArea::opacityAnimationCallback() {
	update();
	if (!_a_opacity.animating()) {
		_cache = QPixmap();
		if (_hiding) {
			hideFinish();
		}
	}
}
