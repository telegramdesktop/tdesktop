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
#include "lang.h"

#include "confirmbox.h"
#include "mainwidget.h"
#include "window.h"

ConfirmBox::ConfirmBox(QString text, QString doneText, QString cancelText) :
	_confirm(this, doneText.isEmpty() ? lang(lng_continue) : doneText, st::btnSelectDone),
	_cancel(this, cancelText.isEmpty() ? lang(lng_cancel) : cancelText, st::btnSelectCancel),
	_text(100), _hiding(false), a_opacity(0, 1), af_opacity(anim::linear) {

	_text.setText(st::boxFont, text, _textPlainOptions);

	_width = st::confirmWidth;
	_textWidth = _width - st::boxPadding.left() - st::boxPadding.right();
	_textHeight = _text.countHeight(_textWidth);
	_height = st::boxPadding.top() + _textHeight + st::boxPadding.bottom() + _confirm.height();

	_confirm.move(_width - _confirm.width(), st::boxPadding.top() + _textHeight + st::boxPadding.bottom());
	_cancel.move(0, st::boxPadding.top() + _textHeight + st::boxPadding.bottom());

	connect(&_confirm, SIGNAL(clicked()), this, SIGNAL(confirmed()));
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(onCancel()));

	resize(_width, _height);

	showAll();
	_cache = myGrab(this, rect());
	hideAll();
}

void ConfirmBox::hideAll() {
	_confirm.hide();
	_cancel.hide();
}

void ConfirmBox::showAll() {
	_confirm.show();
	_cancel.show();
}

void ConfirmBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		emit confirmed();
	} else if (e->key() == Qt::Key_Escape) {
		onCancel();
	}
}

void ConfirmBox::parentResized() {
	QSize s = parentWidget()->size();
	setGeometry((s.width() - _width) / 2, (s.height() - _height) / 2, _width, _height);
	update();
}

void ConfirmBox::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	if (_cache.isNull()) {
		if (!_hiding || a_opacity.current() > 0.01) {
			// fill bg
			p.fillRect(0, 0, _width, _height, st::boxBG->b);

			// paint shadows
			p.fillRect(0, _height - st::btnSelectCancel.height - st::scrollDef.bottomsh, _width, st::scrollDef.bottomsh, st::scrollDef.shColor->b);

			// paint button sep
			p.fillRect(st::btnSelectCancel.width, _height - st::btnSelectCancel.height, st::lineWidth, st::btnSelectCancel.height, st::btnSelectSep->b);

			// draw box title / text
			p.setFont(st::boxFont->f);
			p.setPen(st::black->p);
			_text.draw(p, st::boxPadding.left(), st::boxPadding.top(), _textWidth, (_text.maxWidth() < _width) ? style::al_center : style::al_left);
		}
	} else {
		p.setOpacity(a_opacity.current());
		p.drawPixmap(0, 0, _cache);
	}
}

void ConfirmBox::animStep(float64 ms) {
	if (ms >= 1) {
		a_opacity.finish();
		_cache = QPixmap();
		if (!_hiding) {
			showAll();
			setFocus();
		}
	} else {
		a_opacity.update(ms, af_opacity);
	}
	update();
}

void ConfirmBox::onCancel() {
	emit cancelled();
	emit closed();
}

void ConfirmBox::startHide() {
	_hiding = true;
	if (_cache.isNull()) {
		_cache = myGrab(this, rect());
		hideAll();
	}
	a_opacity.start(0);
}

ConfirmBox::~ConfirmBox() {
}
