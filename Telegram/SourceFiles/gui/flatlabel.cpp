/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#include "stdafx.h"

#include "gui/flatlabel.h"

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
	textstyleRestore();
	int32 w = _st.width ? _st.width : _text.maxWidth(), h = _text.countHeight(w);
	resize(w, h);
}

void FlatLabel::setRichText(const QString &text) {
	textstyleSet(&_tst);
    const char *t = text.toUtf8().constData();
	_text.setRichText(_st.font, text, _labelOptions);
	textstyleRestore();
	int32 w = _st.width ? _st.width : _text.maxWidth(), h = _text.countHeight(w);
	resize(w, h);
	setMouseTracking(_text.hasLinks());
}

void FlatLabel::setLink(uint16 lnkIndex, const TextLinkPtr &lnk) {
	_text.setLink(lnkIndex, lnk);
}

void FlatLabel::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateHover();
}

void FlatLabel::mousePressEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateHover();
	if (textlnkOver()) {
		textlnkDown(textlnkOver());
		update();
	}
}

void FlatLabel::mouseReleaseEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateHover();
	if (textlnkOver() && textlnkOver() == textlnkDown()) {
		textlnkOver()->onClick(e->button());
	}
	textlnkDown(TextLinkPtr());
}

void FlatLabel::leaveEvent(QEvent *e) {
	if (_myLink) {
		if (textlnkOver() == _myLink) {
			textlnkOver(TextLinkPtr());
			update();
		}
		_myLink = TextLinkPtr();
		setCursor(style::cur_default);
	}
}

void FlatLabel::updateLink() {
	_lastMousePos = QCursor::pos();
	updateHover();
}

void FlatLabel::updateHover() {
	QPoint m(mapFromGlobal(_lastMousePos));
	bool wasMy = (_myLink == textlnkOver());
	textstyleSet(&_tst);
	_myLink = _text.link(m.x(), m.y(), width(), _st.align);
	textstyleRestore();
	if (_myLink != textlnkOver()) {
		if (wasMy || _myLink || rect().contains(m)) {
			textlnkOver(_myLink);
		}
		setCursor(_myLink ? style::cur_pointer : style::cur_default);
		update();
	}
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
