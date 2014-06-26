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
#include "style.h"

#include "flatinput.h"

namespace {
	class FlatInputStyle : public QCommonStyle {
	public:
		FlatInputStyle() {
		}

		void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget = 0) const {
		}
		QRect subElementRect(SubElement r, const QStyleOption *opt, const QWidget *widget = 0) const {
			switch (r) {
				case SE_LineEditContents:
					const FlatInput *w = widget ? qobject_cast<const FlatInput*>(widget) : 0;
					return w ? w->getTextRect() : QCommonStyle::subElementRect(r, opt, widget);
				break;
			}
			return QCommonStyle::subElementRect(r, opt, widget);
		}
	};
	FlatInputStyle _flatInputStyle;
}

FlatInput::FlatInput(QWidget *parent, const style::flatInput &st, const QString &pholder, const QString &v) : QLineEdit(v, parent), _oldtext(v), _kev(0), _customUpDown(false), _phVisible(!v.length()),
	a_phLeft(_phVisible ? 0 : st.phShift), a_phAlpha(_phVisible ? 1 : 0), a_phColor(st.phColor->c),
    a_borderColor(st.borderColor->c), a_bgColor(st.bgColor->c), _notingBene(0), _st(st) {
	resize(_st.width, _st.height);
	
	setFont(_st.font->f);
	setAlignment(_st.align);
	
	_ph = _st.font->m.elidedText(pholder, Qt::ElideRight, width() - _st.textMrg.left() - _st.textMrg.right() - _st.phPos.x() - 1);

	QPalette p(palette());
	p.setColor(QPalette::Text, _st.textColor->c);
	setPalette(p);

	connect(this, SIGNAL(textChanged(const QString &)), this, SLOT(onTextChange(const QString &)));
	connect(this, SIGNAL(textEdited(const QString &)), this, SLOT(onTextEdited()));

	setStyle(&_flatInputStyle);
	setTextMargins(0, 0, 0, 0);
	setContentsMargins(0, 0, 0, 0);

	setAttribute(Qt::WA_AcceptTouchEvents);
	_touchTimer.setSingleShot(true);
	connect(&_touchTimer, SIGNAL(timeout()), this, SLOT(onTouchTimer()));
}

void FlatInput::customUpDown(bool custom) {
	_customUpDown = custom;
}

void FlatInput::onTouchTimer() {
	_touchRightButton = true;
}

bool FlatInput::event(QEvent *e) {
	if (e->type() == QEvent::TouchBegin || e->type() == QEvent::TouchUpdate || e->type() == QEvent::TouchEnd || e->type() == QEvent::TouchCancel) {
		QTouchEvent *ev = static_cast<QTouchEvent*>(e);
		if (ev->device()->type() == QTouchDevice::TouchScreen) {
			touchEvent(ev);
			return QLineEdit::event(e);
		}
	}
	return QLineEdit::event(e);
}

void FlatInput::touchEvent(QTouchEvent *e) {
	switch (e->type()) {
	case QEvent::TouchBegin:
		if (_touchPress || e->touchPoints().isEmpty()) return;
		_touchTimer.start(QApplication::startDragTime());
		_touchPress = true;
		_touchMove = _touchRightButton = false;
		_touchStart = e->touchPoints().cbegin()->screenPos().toPoint();
		break;

	case QEvent::TouchUpdate:
		if (!_touchPress || e->touchPoints().isEmpty()) return;
		if (!_touchMove && (e->touchPoints().cbegin()->screenPos().toPoint() - _touchStart).manhattanLength() >= QApplication::startDragDistance()) {
			_touchMove = true;
		}
		break;

	case QEvent::TouchEnd:
		if (!_touchPress) return;
		if (!_touchMove && window()) {
			Qt::MouseButton btn(_touchRightButton ? Qt::RightButton : Qt::LeftButton);
			QPoint mapped(mapFromGlobal(_touchStart)), winMapped(window()->mapFromGlobal(_touchStart));

			if (_touchRightButton) {
				QContextMenuEvent contextEvent(QContextMenuEvent::Mouse, mapped, _touchStart);
				contextMenuEvent(&contextEvent);
			}
		}
		_touchTimer.stop();
		_touchPress = _touchMove = _touchRightButton = false;
		break;

	case QEvent::TouchCancel:
		_touchPress = false;
		_touchTimer.stop();
		break;
	}
}

QRect FlatInput::getTextRect() const {
	return rect().marginsRemoved(_st.textMrg + QMargins(-2, -1, -2, -1));
}

void FlatInput::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	p.fillRect(rect(), a_bgColor.current());
	if (_st.borderWidth) {
		p.setPen(a_borderColor.current());
		for (uint32 i = 0; i < _st.borderWidth; ++i) {
			p.drawRect(i, i, width() - 2 * i - 1, height() - 2 * i - 1);
		}
	}
	if (_st.imgRect.pxWidth()) {
		p.drawPixmap(_st.imgPos, App::sprite(), _st.imgRect);
	}

	bool phDraw = _phVisible;
	if (animating()) {
		p.setOpacity(a_phAlpha.current());
		phDraw = true;
	}
	if (phDraw) {
		p.save();
		p.setClipRect(rect());
		QRect phRect(_st.textMrg.left() + _st.phPos.x() + a_phLeft.current(), _st.textMrg.top() + _st.phPos.y(), width() - _st.textMrg.left() - _st.textMrg.right(), height() - _st.textMrg.top() - _st.textMrg.bottom());
		p.setFont(_st.font->f);
		p.setPen(a_phColor.current());
		p.drawText(phRect, _ph, QTextOption(_st.phAlign));
		p.restore();
	}
	QLineEdit::paintEvent(e);
}

void FlatInput::focusInEvent(QFocusEvent *e) {
	a_phColor.start(_st.phFocusColor->c);
	if (_notingBene <= 0) {
		a_borderColor.start(_st.borderActive->c);
	}
	a_bgColor.start(_st.bgActive->c);
	anim::start(this);
	QLineEdit::focusInEvent(e);
	emit focused();
}

void FlatInput::focusOutEvent(QFocusEvent *e) {
	a_phColor.start(_st.phColor->c);
	if (_notingBene <= 0) {
		a_borderColor.start(_st.borderColor->c);
	}
	a_bgColor.start(_st.bgColor->c);
	anim::start(this);
	QLineEdit::focusOutEvent(e);
	emit blurred();
}

QSize FlatInput::sizeHint() const {
	return geometry().size();
}

QSize FlatInput::minimumSizeHint() const {
	return geometry().size();
}

bool FlatInput::animStep(float64 ms) {
	float dt = ms / _st.phDuration;
	bool res = true;
	if (dt >= 1) {
		res = false;
		a_phLeft.finish();
		a_phAlpha.finish();
		a_phColor.finish();
		a_bgColor.finish();
		if (_notingBene > 0) {
			_notingBene = -1;
			a_borderColor.start((hasFocus() ? _st.borderActive : _st.borderColor)->c);
			anim::start(this);
			return true;
		} else if (_notingBene) {
			_notingBene = 0;
		}
		a_borderColor.finish();
	} else {
		a_phLeft.update(dt, _st.phLeftFunc);
		a_phAlpha.update(dt, _st.phAlphaFunc);
		a_phColor.update(dt, _st.phColorFunc);
		a_bgColor.update(dt, _st.phColorFunc);
		a_borderColor.update(dt, _st.phColorFunc);
	}
	update();
	return res;
}

void FlatInput::updatePlaceholder() {
	bool vis = !text().length();
	if (vis == _phVisible) return;

	a_phLeft.start(vis ? 0 : _st.phShift);
	a_phAlpha.start(vis ? 1 : 0);
	anim::start(this);

	_phVisible = vis;
}

void FlatInput::correctValue(QKeyEvent *e, const QString &was) {
}

void FlatInput::keyPressEvent(QKeyEvent *e) {
	QString was(text());
	_kev = e;
	if (_customUpDown && (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down)) {
		e->ignore();
	} else {
		QLineEdit::keyPressEvent(e);
	}

	if (was == text()) { // call correct manually
		correctValue(_kev, was);
		_oldtext = text();
		if (was != _oldtext) emit changed();
		updatePlaceholder();
	}
	if (e->key() == Qt::Key_Escape) {
		emit cancelled();
	} else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		emit accepted();
	}
	_kev = 0;
}

void FlatInput::onTextEdited() {
	QString was(_oldtext);
	correctValue(_kev, was);
	_oldtext = text();
	if (was != _oldtext) emit changed();
	updatePlaceholder();
}

void FlatInput::onTextChange(const QString &text) {
	_oldtext = text;
}

void FlatInput::notaBene() {
	_notingBene = 1;
	setFocus();
	a_borderColor.start(_st.borderError->c);
	anim::start(this);
}
