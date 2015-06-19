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
#include "style.h"

#include "flatinput.h"
#include "window.h"
#include "countryinput.h"

namespace {
	template <typename InputClass>
	class InputStyle : public QCommonStyle {
	public:
		InputStyle() {
		}

		void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget = 0) const {
		}
		QRect subElementRect(SubElement r, const QStyleOption *opt, const QWidget *widget = 0) const {
			switch (r) {
				case SE_LineEditContents:
					const InputClass *w = widget ? qobject_cast<const InputClass*>(widget) : 0;
					return w ? w->getTextRect() : QCommonStyle::subElementRect(r, opt, widget);
				break;
			}
			return QCommonStyle::subElementRect(r, opt, widget);
		}
	};
	InputStyle<FlatInput> _flatInputStyle;
	InputStyle<InputField> _inputFieldStyle;
}

FlatInput::FlatInput(QWidget *parent, const style::flatInput &st, const QString &pholder, const QString &v) : QLineEdit(v, parent), _fullph(pholder), _oldtext(v), _kev(0), _customUpDown(false), _phVisible(!v.length()),
	a_phLeft(_phVisible ? 0 : st.phShift), a_phAlpha(_phVisible ? 1 : 0), a_phColor(st.phColor->c),
    a_borderColor(st.borderColor->c), a_bgColor(st.bgColor->c), _notingBene(0), _st(st) {
	resize(_st.width, _st.height);
	
	setFont(_st.font->f);
	setAlignment(_st.align);

	QPalette p(palette());
	p.setColor(QPalette::Text, _st.textColor->c);
	setPalette(p);

	connect(this, SIGNAL(textChanged(const QString &)), this, SLOT(onTextChange(const QString &)));
	connect(this, SIGNAL(textEdited(const QString &)), this, SLOT(onTextEdited()));
	if (App::wnd()) connect(this, SIGNAL(selectionChanged()), App::wnd(), SLOT(updateGlobalMenu()));

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
		QBrush b(a_borderColor.current());
		p.fillRect(0, 0, width() - _st.borderWidth, _st.borderWidth, b);
		p.fillRect(width() - _st.borderWidth, 0, _st.borderWidth, height() - _st.borderWidth, b);
		p.fillRect(_st.borderWidth, height() - _st.borderWidth, width() - _st.borderWidth, _st.borderWidth, b);
		p.fillRect(0, _st.borderWidth, _st.borderWidth, height() - _st.borderWidth, b);
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

void FlatInput::resizeEvent(QResizeEvent *e) {
	int32 availw = width() - _st.textMrg.left() - _st.textMrg.right() - _st.phPos.x() - 1;
	if (_st.font->m.width(_fullph) > availw) {
		_ph = _st.font->m.elidedText(_fullph, Qt::ElideRight, availw);
	} else {
		_ph = _fullph;
	}
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
	if (App::wnd()) App::wnd()->updateGlobalMenu();
}

void FlatInput::onTextChange(const QString &text) {
	_oldtext = text;
	if (App::wnd()) App::wnd()->updateGlobalMenu();
}

void FlatInput::notaBene() {
	_notingBene = 1;
	setFocus();
	a_borderColor.start(_st.borderError->c);
	anim::start(this);
}

CountryCodeInput::CountryCodeInput(QWidget *parent, const style::flatInput &st) : FlatInput(parent, st), _nosignal(false) {

}

void CountryCodeInput::startErasing(QKeyEvent *e) {
	setFocus();
	keyPressEvent(e);
}

void CountryCodeInput::codeSelected(const QString &code) {
	QString old(text());
	setText('+' + code);
	_nosignal = true;
	correctValue(0, old);
	_nosignal = false;
	emit changed();
}

void CountryCodeInput::correctValue(QKeyEvent *e, const QString &was) {
	QString oldText(text()), newText, addToNumber;
	int oldPos(cursorPosition()), newPos(-1), oldLen(oldText.length()), start = 0, digits = 5;
	newText.reserve(oldLen + 1);
	newText += '+';
	if (oldLen && oldText[0] == '+') {
		++start;
	}
	for (int i = start; i < oldLen; ++i) {
		QChar ch(oldText[i]);
		if (ch.isDigit()) {
			if (!digits || !--digits) {
				addToNumber += ch;
			} else {
				newText += ch;
			}
		}
		if (i == oldPos) {
			newPos = newText.length();
		}
	}
	if (!addToNumber.isEmpty()) {
		QString validCode = findValidCode(newText.mid(1));
		addToNumber = newText.mid(1 + validCode.length()) + addToNumber;
		newText = '+' + validCode;
	}
	if (newPos < 0 || newPos > newText.length()) {
		newPos = newText.length();
	}
	if (newText != oldText) {
		setText(newText);
		if (newPos != oldPos) {
			setCursorPosition(newPos);
		}
	}
	if (!_nosignal && was != newText) {
		emit codeChanged(newText.mid(1));
	}
	if (!addToNumber.isEmpty()) {
		emit addedToNumber(addToNumber);
	}
}

InputField::InputField(QWidget *parent, const style::InputField &st, const QString &pholder, const QString &v) : QLineEdit(v, parent),
_lastText(v),
_keyEvent(0),
_customUpDown(false),

_placeholderFull(pholder),
_placeholderVisible(!v.length()),
a_placeholderLeft(_placeholderVisible ? 0 : st.placeholderShift),
a_placeholderOpacity(_placeholderVisible ? 1 : 0),
a_placeholderFg(st.placeholderFg->c),
_placeholderFgAnim(animFunc(this, &InputField::placeholderFgStep)),
_placeholderShiftAnim(animFunc(this, &InputField::placeholderShiftStep)),

a_borderFg(st.borderFg->c),
a_borderOpacityActive(0),
_borderAnim(animFunc(this, &InputField::borderStep)),

_focused(false), _error(false), _st(&st) {
	resize(_st->width, _st->height);

	setFont(_st->font->f);
	setAlignment(_st->textAlign);
	setLayoutDirection(cLangDir());

	QPalette p(palette());
	p.setColor(QPalette::Text, _st->textFg->c);
	setPalette(p);

	connect(this, SIGNAL(textChanged(const QString &)), this, SLOT(onTextChange(const QString &)));
	connect(this, SIGNAL(textEdited(const QString &)), this, SLOT(onTextEdited()));
	if (App::wnd()) connect(this, SIGNAL(selectionChanged()), App::wnd(), SLOT(updateGlobalMenu()));

	setStyle(&_inputFieldStyle);
	setTextMargins(0, 0, 0, 0);
	setContentsMargins(0, 0, 0, 0);

	setAttribute(Qt::WA_AcceptTouchEvents);
	_touchTimer.setSingleShot(true);
	connect(&_touchTimer, SIGNAL(timeout()), this, SLOT(onTouchTimer()));
}

void InputField::setCustomUpDown(bool customUpDown) {
	_customUpDown = customUpDown;
}

void InputField::onTouchTimer() {
	_touchRightButton = true;
}

bool InputField::event(QEvent *e) {
	if (e->type() == QEvent::TouchBegin || e->type() == QEvent::TouchUpdate || e->type() == QEvent::TouchEnd || e->type() == QEvent::TouchCancel) {
		QTouchEvent *ev = static_cast<QTouchEvent*>(e);
		if (ev->device()->type() == QTouchDevice::TouchScreen) {
			touchEvent(ev);
			return QLineEdit::event(e);
		}
	}
	return QLineEdit::event(e);
}

void InputField::touchEvent(QTouchEvent *e) {
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

QRect InputField::getTextRect() const {
	QMargins m = _st->textMargins + QMargins(-2, -1, -2, -1);
	if (rtl()) {
		int l = m.left();
		m.setLeft(m.right());
		m.setRight(l);
	}
	return rect().marginsRemoved(m);
}

void InputField::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (_st->border) {
		p.fillRect(0, height() - _st->border, width(), _st->border, _st->borderFg->b);
	}
	if (_st->borderActive && a_borderOpacityActive.current() > 0) {
		p.setOpacity(a_borderOpacityActive.current());
		p.fillRect(0, height() - _st->borderActive, width(), _st->borderActive, a_borderFg.current());
		p.setOpacity(1);
	}
	if (_st->iconSprite.pxWidth()) {
		p.drawSpriteLeft(_st->iconPosition, width(), _st->iconSprite);
	}

	bool drawPlaceholder = _placeholderVisible;
	if (_placeholderShiftAnim.animating()) {
		p.setOpacity(a_placeholderOpacity.current());
		drawPlaceholder = true;
	}
	if (drawPlaceholder) {
		p.save();
		p.setClipRect(rect());

		QRect r(rect().marginsRemoved(_st->textMargins + _st->placeholderMargins));
		r.moveLeft(r.left() + a_placeholderLeft.current());
		if (rtl()) r.moveLeft(width() - r.left() - r.width());

		p.setFont(_st->font->f);
		p.setPen(a_placeholderFg.current());
		p.drawText(r, _placeholder, _st->placeholderAlign);

		p.restore();
	}
	QLineEdit::paintEvent(e);
}

void InputField::focusInEvent(QFocusEvent *e) {
	if (!_focused) {
		_focused = true;

		a_placeholderFg.start(_st->placeholderFgActive->c);
		_placeholderFgAnim.start();

		a_borderFg.start((_error ? _st->borderFgError : _st->borderFgActive)->c);
		a_borderOpacityActive.start(1);
		_borderAnim.start();
	}
	QLineEdit::focusInEvent(e);
	emit focused();
}

void InputField::focusOutEvent(QFocusEvent *e) {
	if (_focused) {
		_focused = false;

		a_placeholderFg.start(_st->placeholderFg->c);
		_placeholderFgAnim.start();

		a_borderFg.start((_error ? _st->borderFgError : _st->borderFg)->c);
		a_borderOpacityActive.start(_error ? 1 : 0);
		_borderAnim.start();
	}
	QLineEdit::focusOutEvent(e);
	emit blurred();
}

void InputField::resizeEvent(QResizeEvent *e) {
	int32 availw = width() - _st->textMargins.left() - _st->textMargins.right() - _st->placeholderMargins.left() - _st->placeholderMargins.right() - 2;
	_placeholder = (_st->font->m.width(_placeholderFull) > availw) ? _st->font->m.elidedText(_placeholderFull, Qt::ElideRight, availw) : _placeholderFull;
	update();
}

QSize InputField::sizeHint() const {
	return geometry().size();
}

QSize InputField::minimumSizeHint() const {
	return geometry().size();
}

bool InputField::placeholderFgStep(float64 ms) {
	float dt = ms / _st->duration;
	bool res = true;
	if (dt >= 1) {
		res = false;
		a_placeholderFg.finish();
	} else {
		a_placeholderFg.update(dt, anim::linear);
	}
	update();
	return res;
}

bool InputField::placeholderShiftStep(float64 ms) {
	float dt = ms / _st->duration;
	bool res = true;
	if (dt >= 1) {
		res = false;
		a_placeholderLeft.finish();
		a_placeholderOpacity.finish();
	} else {
		a_placeholderLeft.update(dt, anim::linear);
		a_placeholderOpacity.update(dt, anim::linear);
	}
	update();
	return res;
}

bool InputField::borderStep(float64 ms) {
	float dt = ms / _st->duration;
	bool res = true;
	if (dt >= 1) {
		res = false;
		a_borderFg.finish();
		a_borderOpacityActive.finish();
	} else {
		a_borderFg.update(dt, anim::linear);
		a_borderOpacityActive.update(dt, anim::linear);
	}
	update();
	return res;
}

void InputField::updatePlaceholder() {
	bool placeholderVisible = !_lastText.isEmpty();
	if (placeholderVisible != _placeholderVisible) {
		_placeholderVisible = placeholderVisible;

		a_placeholderLeft.start(_placeholderVisible ? 0 : _st->placeholderShift);
		a_placeholderOpacity.start(_placeholderVisible ? 1 : 0);
		_placeholderShiftAnim.start();
	}
}

void InputField::correctValue(QKeyEvent *e, const QString &was) {
}

void InputField::keyPressEvent(QKeyEvent *e) {
	QString was(_lastText);

	_keyEvent = e;
	if (_customUpDown && (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down)) {
		e->ignore();
	} else {
		QLineEdit::keyPressEvent(e);
	}

	if (was == _lastText) { // call correct manually
		correctValue(_keyEvent, was);
		_lastText = text();
		if (was != _lastText) emit changed();
		updatePlaceholder();
	}
	if (e->key() == Qt::Key_Escape) {
		emit cancelled();
	} else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		emit accepted();
	}
	_keyEvent = 0;
}

void InputField::onTextEdited() {
	QString was(_lastText);
	correctValue(_keyEvent, was);
	_lastText = text();
	if (was != _lastText) emit changed();
	updatePlaceholder();
	if (App::wnd()) App::wnd()->updateGlobalMenu();
}

void InputField::onTextChange(const QString &text) {
	_lastText = text;
	if (App::wnd()) App::wnd()->updateGlobalMenu();
}

void InputField::setError(bool error) {
	if (error != _error) {
		_error = error;

		a_borderFg.start((_error ? _st->borderFgError : (_focused ? _st->borderFgActive : _st->borderFg))->c);
		a_borderOpacityActive.start((_error || _focused) ? 1 : 0);
		_borderAnim.start();
	}
}
