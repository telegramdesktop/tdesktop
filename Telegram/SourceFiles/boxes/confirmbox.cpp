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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "lang.h"

#include "confirmbox.h"
#include "mainwidget.h"
#include "window.h"

TextParseOptions _confirmBoxTextOptions = {
	TextParseLinks | TextParseMultiline | TextParseRichText, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

ConfirmBox::ConfirmBox(const QString &text, const QString &doneText, const QString &cancelText, const style::flatButton &doneStyle, const style::flatButton &cancelStyle) : _infoMsg(false),
_confirm(this, doneText.isEmpty() ? lang(lng_continue) : doneText, doneStyle),
_cancel(this, cancelText.isEmpty() ? lang(lng_cancel) : cancelText, cancelStyle),
_close(this, QString(), st::btnInfoClose),
_text(100) {
	init(text);
}

ConfirmBox::ConfirmBox(const QString &text, bool noDone, const QString &cancelText) : _infoMsg(true),
_confirm(this, QString(), st::btnSelectDone),
_cancel(this, QString(), st::btnSelectCancel),
_close(this, cancelText.isEmpty() ? lang(lng_close) : cancelText, st::btnInfoClose),
_text(100) {
	init(text);
}

void ConfirmBox::init(const QString &text) {
	_text.setText(st::boxFont, text, (_infoMsg ? _confirmBoxTextOptions : _textPlainOptions));

	_textWidth = st::boxWidth - st::boxPadding.left() - st::boxPadding.right();
	_textHeight = _text.countHeight(_textWidth);
	setMaxHeight(st::boxPadding.top() + _textHeight + st::boxPadding.bottom() + (_infoMsg ? _close.height() : _confirm.height()));

	if (_infoMsg) {
		_confirm.hide();
		_cancel.hide();

		connect(&_close, SIGNAL(clicked()), this, SLOT(onClose()));

		setMouseTracking(_text.hasLinks());
	} else {
		_close.hide();

		connect(&_confirm, SIGNAL(clicked()), this, SIGNAL(confirmed()));
		connect(&_cancel, SIGNAL(clicked()), this, SLOT(onClose()));
	}

	prepare();
}

void ConfirmBox::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateHover();
}

void ConfirmBox::mousePressEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateHover();
	if (textlnkOver()) {
		textlnkDown(textlnkOver());
		update();
	}
	return LayeredWidget::mousePressEvent(e);
}

void ConfirmBox::mouseReleaseEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateHover();
	if (textlnkOver() && textlnkOver() == textlnkDown()) {
		App::wnd()->hideLayer();
		textlnkOver()->onClick(e->button());
	}
	textlnkDown(TextLinkPtr());
}

void ConfirmBox::leaveEvent(QEvent *e) {
	if (_myLink) {
		if (textlnkOver() == _myLink) {
			textlnkOver(TextLinkPtr());
			update();
		}
		_myLink = TextLinkPtr();
		setCursor(style::cur_default);
		update();
	}
}

void ConfirmBox::updateLink() {
	_lastMousePos = QCursor::pos();
	updateHover();
}

void ConfirmBox::updateHover() {
	QPoint m(mapFromGlobal(_lastMousePos));
	bool wasMy = (_myLink == textlnkOver());
	_myLink = _text.link(m.x() - st::boxPadding.left(), m.y() - st::boxPadding.top(), _textWidth, (_text.maxWidth() < width()) ? style::al_center : style::al_left);
	if (_myLink != textlnkOver()) {
		if (wasMy || _myLink || rect().contains(m)) {
			textlnkOver(_myLink);
		}
		setCursor(_myLink ? style::cur_pointer : style::cur_default);
		update();
	}
}

void ConfirmBox::closePressed() {
	emit cancelled();
}

void ConfirmBox::hideAll() {
	_confirm.hide();
	_cancel.hide();
	_close.hide();
}

void ConfirmBox::showAll() {
	if (_infoMsg) {
		_close.show();
	} else {
		_confirm.show();
		_cancel.show();
	}
}

void ConfirmBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		emit confirmed();
	} else {
		AbstractBox::keyPressEvent(e);
	}
}

void ConfirmBox::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	if (paint(p)) return;

	if (!_infoMsg) {
		// paint shadows
		p.fillRect(0, height() - st::btnSelectCancel.height - st::scrollDef.bottomsh, width(), st::scrollDef.bottomsh, st::scrollDef.shColor->b);

		// paint button sep
		p.fillRect(st::btnSelectCancel.width, height() - st::btnSelectCancel.height, st::lineWidth, st::btnSelectCancel.height, st::btnSelectSep->b);
	}

	// draw box title / text
	p.setFont(st::boxFont->f);
	p.setPen(st::black->p);
	_text.draw(p, st::boxPadding.left(), st::boxPadding.top(), _textWidth, (_text.maxWidth() < width()) ? style::al_center : style::al_left);
}

void ConfirmBox::resizeEvent(QResizeEvent *e) {
	if (_infoMsg) {
		_close.move(0, st::boxPadding.top() + _textHeight + st::boxPadding.bottom());
	} else {
		_confirm.move(width() - _confirm.width(), st::boxPadding.top() + _textHeight + st::boxPadding.bottom());
		_cancel.move(0, st::boxPadding.top() + _textHeight + st::boxPadding.bottom());
	}
}
