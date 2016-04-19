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

#include "ui/flatlabel.h"

namespace {
	TextParseOptions _labelOptions = {
		TextParseMultiline, // flags
		0, // maxw
		0, // maxh
		Qt::LayoutDirectionAuto, // dir
	};
}

FlatLabel::FlatLabel(QWidget *parent, const QString &text, const style::flatLabel &st, const style::textStyle &tst) : TWidget(parent),
_text(st.width ? st.width : QFIXED_MAX), _st(st), _tst(tst), _opacity(1) {
	setRichText(text);
}

void FlatLabel::setText(const QString &text) {
	textstyleSet(&_tst);
	_text.setText(_st.font, text, _labelOptions);
	int32 w = _st.width ? _st.width : _text.maxWidth(), h = _text.countHeight(w);
	textstyleRestore();
	resize(w, h);
}

void FlatLabel::setRichText(const QString &text) {
	textstyleSet(&_tst);
	_text.setRichText(_st.font, text, _labelOptions);
	int32 w = _st.width ? _st.width : _text.maxWidth(), h = _text.countHeight(w);
	textstyleRestore();
	resize(w, h);
	setMouseTracking(_text.hasLinks());
}

void FlatLabel::resizeToWidth(int32 width) {
	textstyleSet(&_tst);
	int32 w = width, h = _text.countHeight(w);
	textstyleRestore();
	resize(w, h);
}

void FlatLabel::setLink(uint16 lnkIndex, const ClickHandlerPtr &lnk) {
	_text.setLink(lnkIndex, lnk);
}

void FlatLabel::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateHover();
}

void FlatLabel::mousePressEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateHover();
	ClickHandler::pressed();
}

void FlatLabel::mouseReleaseEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateHover();
	if (ClickHandlerPtr activated = ClickHandler::unpressed()) {
		App::activateClickHandler(activated, e->button());
	}
}

void FlatLabel::enterEvent(QEvent *e) {
	_lastMousePos = QCursor::pos();
	updateHover();
}

void FlatLabel::leaveEvent(QEvent *e) {
	ClickHandler::clearActive(this);
}

void FlatLabel::clickHandlerActiveChanged(const ClickHandlerPtr &action, bool active) {
	setCursor(active ? style::cur_pointer : style::cur_default);
	update();
}

void FlatLabel::clickHandlerPressedChanged(const ClickHandlerPtr &action, bool active) {
	update();
}

void FlatLabel::updateLink() {
	_lastMousePos = QCursor::pos();
	updateHover();
}

void FlatLabel::updateHover() {
	QPoint m(mapFromGlobal(_lastMousePos));

	textstyleSet(&_tst);
	Text::StateRequest request;
	request.align = _st.align;
	auto state = _text.getState(m.x(), m.y(), width(), request);
	textstyleRestore();

	ClickHandler::setActive(state.link, this);
}

void FlatLabel::setOpacity(float64 o) {
	_opacity = o;
	update();
}

void FlatLabel::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	p.setOpacity(_opacity);
	textstyleSet(&_tst);
	_text.draw(p, 0, 0, width(), _st.align, e->rect().y(), e->rect().bottom());
	textstyleRestore();
}
