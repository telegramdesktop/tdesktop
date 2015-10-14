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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "style.h"

#include "flatinput.h"
#include "window.h"
#include "countryinput.h"

#include "lang.h"
#include "numbers.h"

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
	InputStyle<MaskedInputField> _inputFieldStyle;
}

FlatInput::FlatInput(QWidget *parent, const style::flatInput &st, const QString &pholder, const QString &v) : QLineEdit(v, parent),
_oldtext(v), _fullph(pholder), _fastph(false), _customUpDown(false), _phVisible(!v.length()),
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
	QLineEdit::setTextMargins(0, 0, 0, 0);
	setContentsMargins(0, 0, 0, 0);

	setAttribute(Qt::WA_AcceptTouchEvents);
	_touchTimer.setSingleShot(true);
	connect(&_touchTimer, SIGNAL(timeout()), this, SLOT(onTouchTimer()));
}

void FlatInput::customUpDown(bool custom) {
	_customUpDown = custom;
}

void FlatInput::setTextMargins(const QMargins &mrg) {
	_st.textMrg = mrg;
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
	Painter p(this);
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
		QRect phRect(placeholderRect());
		phRect.moveLeft(phRect.left() + a_phLeft.current());
		phPrepare(p);
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
	if (_st.font->width(_fullph) > availw) {
		_ph = _st.font->elided(_fullph, availw);
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

void FlatInput::setPlaceholder(const QString &ph) {
	_fullph = ph;
	resizeEvent(0);
	update();
}

void FlatInput::setPlaceholderFast(bool fast) {
	_fastph = fast;
	if (_fastph) {
		a_phLeft = anim::ivalue(_phVisible ? 0 : _st.phShift, _phVisible ? 0 : _st.phShift);
		a_phAlpha = anim::fvalue(_phVisible ? 1 : 0, _phVisible ? 1 : 0);
		update();
	}
}

void FlatInput::updatePlaceholder() {
	bool vis = !text().length();
	if (vis == _phVisible) return;

	if (_fastph) {
		a_phLeft = anim::ivalue(vis ? 0 : _st.phShift, vis ? 0 : _st.phShift);
		a_phAlpha = anim::fvalue(vis ? 1 : 0, vis ? 1 : 0);
		update();
	} else {
		a_phLeft.start(vis ? 0 : _st.phShift);
		a_phAlpha.start(vis ? 1 : 0);
		anim::start(this);
	}
	_phVisible = vis;
}

const QString &FlatInput::placeholder() const {
	return _fullph;
}

QRect FlatInput::placeholderRect() const {
	return QRect(_st.textMrg.left() + _st.phPos.x(), _st.textMrg.top() + _st.phPos.y(), width() - _st.textMrg.left() - _st.textMrg.right(), height() - _st.textMrg.top() - _st.textMrg.bottom());
}

void FlatInput::correctValue(const QString &was, QString &now) {
}

void FlatInput::phPrepare(Painter &p) {
	p.setFont(_st.font->f);
	p.setPen(a_phColor.current());
}

void FlatInput::keyPressEvent(QKeyEvent *e) {
	QString wasText(_oldtext);

	bool shift = e->modifiers().testFlag(Qt::ShiftModifier), alt = e->modifiers().testFlag(Qt::AltModifier);
	bool ctrl = e->modifiers().testFlag(Qt::ControlModifier) || e->modifiers().testFlag(Qt::MetaModifier), ctrlGood = true;
	if (_customUpDown && (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down)) {
		e->ignore();
	} else {
		QLineEdit::keyPressEvent(e);
	}

	QString newText(text());
	if (wasText == newText) { // call correct manually
		correctValue(wasText, newText);
		_oldtext = newText;
		if (wasText != _oldtext) emit changed();
		updatePlaceholder();
	}
	if (e->key() == Qt::Key_Escape) {
		emit cancelled();
	} else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		emit submitted(ctrl && shift);
	}
}

void FlatInput::onTextEdited() {
	QString wasText(_oldtext), newText(text());

	correctValue(wasText, newText);
	_oldtext = newText;
	if (wasText != _oldtext) emit changed();
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
	QString wasText(getLastText()), newText = '+' + code;
	setText(newText);
	_nosignal = true;
	correctValue(wasText, newText);
	_nosignal = false;
	emit changed();
}

void CountryCodeInput::correctValue(const QString &was, QString &now) {
	QString newText, addToNumber;
	int oldPos(cursorPosition()), newPos(-1), oldLen(now.length()), start = 0, digits = 5;
	newText.reserve(oldLen + 1);
	newText += '+';
	if (oldLen && now[0] == '+') {
		++start;
	}
	for (int i = start; i < oldLen; ++i) {
		QChar ch(now[i]);
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
	if (newText != now) {
		now = newText;
		setText(newText);
		updatePlaceholder();
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

PhonePartInput::PhonePartInput(QWidget *parent, const style::flatInput &st) : FlatInput(parent, st, lang(lng_phone_ph)) {
}

void PhonePartInput::paintEvent(QPaintEvent *e) {
	FlatInput::paintEvent(e);

	Painter p(this);
	QString t(text());
	if (!pattern.isEmpty() && !t.isEmpty()) {
		QString ph = placeholder().mid(t.size());
		if (!ph.isEmpty()) {
			p.setClipRect(rect());
			QRect phRect(placeholderRect());
			int tw = phFont()->width(t);
			if (tw < phRect.width()) {
				phRect.setLeft(phRect.left() + tw);
				phPrepare(p);
				p.drawText(phRect, ph, style::al_left);
			}
		}
	}
}

void PhonePartInput::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Backspace && text().isEmpty()) {
		emit voidBackspace(e);
	} else {
		FlatInput::keyPressEvent(e);
	}
}

void PhonePartInput::correctValue(const QString &was, QString &now) {
	QString newText;
	int oldPos(cursorPosition()), newPos(-1), oldLen(now.length()), digitCount = 0;
	for (int i = 0; i < oldLen; ++i) {
		if (now[i].isDigit()) {
			++digitCount;
		}
	}
	if (digitCount > MaxPhoneTailLength) digitCount = MaxPhoneTailLength;

	bool inPart = !pattern.isEmpty();
	int curPart = -1, leftInPart = 0;
	newText.reserve(oldLen);
	for (int i = 0; i < oldLen; ++i) {
		if (i == oldPos && newPos < 0) {
			newPos = newText.length();
		}

		QChar ch(now[i]);
		if (ch.isDigit()) {
			if (!digitCount--) {
				break;
			}
			if (inPart) {
				if (leftInPart) {
					--leftInPart;
				} else {
					newText += ' ';
					++curPart;
					inPart = curPart < pattern.size();
					leftInPart = inPart ? (pattern.at(curPart) - 1) : 0;

					++oldPos;
				}
			}
			newText += ch;
		} else if (ch == ' ' || ch == '-' || ch == '(' || ch == ')') {
			if (inPart) {
				if (leftInPart) {
				} else {
					newText += ch;
					++curPart;
					inPart = curPart < pattern.size();
					leftInPart = inPart ? pattern.at(curPart) : 0;
				}
			} else {
				newText += ch;
			}
		}
	}
	int32 newlen = newText.size();
	while (newlen > 0 && newText.at(newlen - 1).isSpace()) {
		--newlen;
	}
	if (newlen < newText.size()) newText = newText.mid(0, newlen);
	if (newPos < 0) {
		newPos = newText.length();
	}
	if (newText != now) {
		now = newText;
		setText(now);
		updatePlaceholder();
		setCursorPosition(newPos);
	}
}

void PhonePartInput::addedToNumber(const QString &added) {
	setFocus();
	QString wasText(getLastText()), newText = added + wasText;
	setText(newText);
	setCursorPosition(added.length());
	correctValue(wasText, newText);
	updatePlaceholder();
}

void PhonePartInput::onChooseCode(const QString &code) {
	pattern = phoneNumberParse(code);
	if (!pattern.isEmpty() && pattern.at(0) == code.size()) {
		pattern.pop_front();
	} else {
		pattern.clear();
	}
	if (pattern.isEmpty()) {
		setPlaceholder(lang(lng_phone_ph));
	} else {
		QString ph;
		ph.reserve(20);
		for (int i = 0, l = pattern.size(); i < l; ++i) {
			ph.append(' ');
			ph.append(QString(pattern.at(i), QChar(0x2212)));
		}
		setPlaceholder(ph);
	}
	QString newText(getLastText());
	correctValue(newText, newText);
	setPlaceholderFast(!pattern.isEmpty());
	updatePlaceholder();
}

InputArea::InputArea(QWidget *parent, const style::InputArea &st, const QString &ph, const QString &val) : TWidget(parent),
_maxLength(-1),
_inner(this),
_oldtext(val),

_undoAvailable(false),
_redoAvailable(false),
_inHeightCheck(false),
_ctrlEnterSubmit(true),

_customUpDown(false),

_placeholderFull(ph),
_placeholderVisible(val.isEmpty()),
a_placeholderLeft(_placeholderVisible ? 0 : st.placeholderShift),
a_placeholderOpacity(_placeholderVisible ? 1 : 0),
a_placeholderFg(st.placeholderFg->c),
_a_placeholderFg(animFunc(this, &InputArea::animStep_placeholderFg)),
_a_placeholderShift(animFunc(this, &InputArea::animStep_placeholderShift)),

a_borderOpacityActive(0),
a_borderFg(st.borderFg->c),
_a_border(animFunc(this, &InputArea::animStep_border)),

_focused(false), _error(false),

_st(st),

_touchPress(false),
_touchRightButton(false),
_touchMove(false),
_correcting(false) {
	_inner.setAcceptRichText(false);
	resize(_st.width, _st.heightMin);

	setAttribute(Qt::WA_OpaquePaintEvent);

	_inner.setFont(_st.font->f);

	_placeholder = _st.font->elided(_placeholderFull, width() - _st.textMargins.left() - _st.textMargins.right() - _st.placeholderMargins.left() - _st.placeholderMargins.right() - 1);

	QPalette p(palette());
	p.setColor(QPalette::Text, _st.textFg->c);
	setPalette(p);

	_inner.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	_inner.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	_inner.setFrameStyle(QFrame::NoFrame | QFrame::Plain);
	_inner.viewport()->setAutoFillBackground(false);

	_inner.setContentsMargins(0, 0, 0, 0);
	_inner.document()->setDocumentMargin(0);

	setAttribute(Qt::WA_AcceptTouchEvents);
	_inner.viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
	_touchTimer.setSingleShot(true);
	connect(&_touchTimer, SIGNAL(timeout()), this, SLOT(onTouchTimer()));

	connect(_inner.document(), SIGNAL(contentsChange(int,int,int)), this, SLOT(onDocumentContentsChange(int,int,int)));
	connect(_inner.document(), SIGNAL(contentsChanged()), this, SLOT(onDocumentContentsChanged()));
	connect(&_inner, SIGNAL(undoAvailable(bool)), this, SLOT(onUndoAvailable(bool)));
	connect(&_inner, SIGNAL(redoAvailable(bool)), this, SLOT(onRedoAvailable(bool)));
	if (App::wnd()) connect(&_inner, SIGNAL(selectionChanged()), App::wnd(), SLOT(updateGlobalMenu()));

	setCursor(style::cur_text);
	heightAutoupdated();

	if (!val.isEmpty()) {
		_inner.setPlainText(val);
	}
	_inner.document()->clearUndoRedoStacks();
}

void InputArea::onTouchTimer() {
	_touchRightButton = true;
}

bool InputArea::heightAutoupdated() {
	if (_st.heightMin < 0 || _st.heightMax < 0 || _inHeightCheck) return false;
	_inHeightCheck = true;

	myEnsureResized(this);

	int newh = qCeil(_inner.document()->size().height()) + _st.textMargins.top() + _st.textMargins.bottom();
	if (newh > _st.heightMax) {
		newh = _st.heightMax;
	} else if (newh < _st.heightMin) {
		newh = _st.heightMin;
	}
	if (height() != newh) {
		resize(width(), newh);
		_inHeightCheck = false;
		return true;
	}
	_inHeightCheck = false;
	return false;
}

void InputArea::checkContentHeight() {
	if (heightAutoupdated()) {
		emit resized();
	}
}

InputArea::InputAreaInner::InputAreaInner(InputArea *parent) : QTextEdit(parent) {
}

bool InputArea::InputAreaInner::viewportEvent(QEvent *e) {
	if (e->type() == QEvent::TouchBegin || e->type() == QEvent::TouchUpdate || e->type() == QEvent::TouchEnd || e->type() == QEvent::TouchCancel) {
		QTouchEvent *ev = static_cast<QTouchEvent*>(e);
		if (ev->device()->type() == QTouchDevice::TouchScreen) {
			qobject_cast<InputArea*>(parentWidget())->touchEvent(ev);
			return QTextEdit::viewportEvent(e);
		}
	}
	return QTextEdit::viewportEvent(e);
}

void InputArea::touchEvent(QTouchEvent *e) {
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

void InputArea::paintEvent(QPaintEvent *e) {
	Painter p(this);

	QRect r(rect().intersected(e->rect()));
	p.fillRect(r, st::white->b);
	if (_st.border) {
		p.fillRect(0, height() - _st.border, width(), _st.border, _st.borderFg->b);
	}
	if (_st.borderActive && a_borderOpacityActive.current() > 0) {
		p.setOpacity(a_borderOpacityActive.current());
		p.fillRect(0, height() - _st.borderActive, width(), _st.borderActive, a_borderFg.current());
		p.setOpacity(1);
	}

	bool drawPlaceholder = _placeholderVisible;
	if (_a_placeholderShift.animating()) {
		p.setOpacity(a_placeholderOpacity.current());
		drawPlaceholder = true;
	}
	if (drawPlaceholder) {
		p.save();
		p.setClipRect(r);

		QRect r(rect().marginsRemoved(_st.textMargins + _st.placeholderMargins));
		r.moveLeft(r.left() + a_placeholderLeft.current());
		if (rtl()) r.moveLeft(width() - r.left() - r.width());

		p.setFont(_st.font);
		p.setPen(a_placeholderFg.current());
		p.drawText(r, _placeholder, _st.placeholderAlign);

		p.restore();
	}
	TWidget::paintEvent(e);
}

void InputArea::startBorderAnimation() {
	a_borderFg.start((_error ? _st.borderFgError : (_focused ? _st.borderFgActive : _st.borderFg))->c);
	a_borderOpacityActive.start((_error || _focused) ? 1 : 0);
	_a_border.start();
}

void InputArea::focusInEvent(QFocusEvent *e) {
	_inner.setFocus();
}

void InputArea::mousePressEvent(QMouseEvent *e) {
	_inner.setFocus();
}

void InputArea::contextMenuEvent(QContextMenuEvent *e) {
	_inner.contextMenuEvent(e);
}

void InputArea::InputAreaInner::focusInEvent(QFocusEvent *e) {
	f()->focusInInner();
	QTextEdit::focusInEvent(e);
	emit f()->focused();
}

void InputArea::focusInInner() {
	if (!_focused) {
		_focused = true;

		a_placeholderFg.start(_st.placeholderFgActive->c);
		_a_placeholderFg.start();

		startBorderAnimation();
	}
}

void InputArea::InputAreaInner::focusOutEvent(QFocusEvent *e) {
	f()->focusOutInner();
	QTextEdit::focusOutEvent(e);
	emit f()->blurred();
}

void InputArea::focusOutInner() {
	if (_focused) {
		_focused = false;

		a_placeholderFg.start(_st.placeholderFg->c);
		_a_placeholderFg.start();

		startBorderAnimation();
	}
}

QSize InputArea::sizeHint() const {
	return geometry().size();
}

QSize InputArea::minimumSizeHint() const {
	return geometry().size();
}

QString InputArea::getText(int32 start, int32 end) const {
	if (end >= 0 && end <= start) return QString();

	if (start < 0) start = 0;
	bool full = (start == 0) && (end < 0);

	QTextDocument *doc(_inner.document());
	QTextBlock from = full ? doc->begin() : doc->findBlock(start), till = (end < 0) ? doc->end() : doc->findBlock(end);
	if (till.isValid()) till = till.next();

	int32 possibleLen = 0;
	for (QTextBlock b = from; b != till; b = b.next()) {
		possibleLen += b.length();
	}
	QString result;
	result.reserve(possibleLen + 1);
	if (!full && end < 0) {
		end = possibleLen;
	}

	for (QTextBlock b = from; b != till; b = b.next()) {
		for (QTextBlock::Iterator iter = b.begin(); !iter.atEnd(); ++iter) {
			QTextFragment fragment(iter.fragment());
			if (!fragment.isValid()) continue;

			int32 p = full ? 0 : fragment.position(), e = full ? 0 : (p + fragment.length());
			if (!full) {
				if (p >= end || e <= start) {
					continue;
				}
			}

			QTextCharFormat f = fragment.charFormat();
			QString emojiText;
			QString t(fragment.text());
			if (!full) {
				if (p < start) {
					t = t.mid(start - p, end - start);
				} else if (e > end) {
					t = t.mid(0, end - p);
				}
			}
			QChar *ub = t.data(), *uc = ub, *ue = uc + t.size();
			for (; uc != ue; ++uc) {
				switch (uc->unicode()) {
				case 0xfdd0: // QTextBeginningOfFrame
				case 0xfdd1: // QTextEndOfFrame
				case QChar::ParagraphSeparator:
				case QChar::LineSeparator:
					*uc = QLatin1Char('\n');
					break;
				case QChar::Nbsp:
					*uc = QLatin1Char(' ');
					break;
				case QChar::ObjectReplacementCharacter:
					if (emojiText.isEmpty() && f.isImageFormat()) {
						QString imageName = static_cast<QTextImageFormat*>(&f)->name();
						if (imageName.startsWith(qstr("emoji://e."))) {
							if (EmojiPtr emoji = emojiFromUrl(imageName)) {
								emojiText = emojiString(emoji);
							}
						}
					}
					if (uc > ub) result.append(ub, uc - ub);
					if (!emojiText.isEmpty()) result.append(emojiText);
					ub = uc + 1;
					break;
				}
			}
			if (uc > ub) result.append(ub, uc - ub);
		}
		result.append('\n');
	}
	result.chop(1);
	return result;
}

bool InputArea::hasText() const {
	QTextDocument *doc(_inner.document());
	QTextBlock from = doc->begin(), till = doc->end();

	if (from == till) return false;

	for (QTextBlock::Iterator iter = from.begin(); !iter.atEnd(); ++iter) {
		QTextFragment fragment(iter.fragment());
		if (!fragment.isValid()) continue;
		if (!fragment.text().isEmpty()) return true;
	}
	return (from.next() != till);
}

bool InputArea::isUndoAvailable() const {
	return _undoAvailable;
}

bool InputArea::isRedoAvailable() const {
	return _redoAvailable;
}

void InputArea::insertEmoji(EmojiPtr emoji, QTextCursor c) {
	QTextImageFormat imageFormat;
	int32 ew = ESize + st::emojiPadding * cIntRetinaFactor() * 2, eh = _st.font->height * cIntRetinaFactor();
	imageFormat.setWidth(ew / cIntRetinaFactor());
	imageFormat.setHeight(eh / cIntRetinaFactor());
	imageFormat.setName(qsl("emoji://e.") + QString::number(emojiKey(emoji), 16));
	imageFormat.setVerticalAlignment(QTextCharFormat::AlignBaseline);

	static QString objectReplacement(QChar::ObjectReplacementCharacter);
	c.insertText(objectReplacement, imageFormat);
}

QVariant InputArea::InputAreaInner::loadResource(int type, const QUrl &name) {
	QString imageName = name.toDisplayString();
	if (imageName.startsWith(qstr("emoji://e."))) {
		if (EmojiPtr emoji = emojiFromUrl(imageName)) {
			return QVariant(App::emojiSingle(emoji, f()->_st.font->height));
		}
	}
	return QVariant();
}

void InputArea::processDocumentContentsChange(int position, int charsAdded) {
	int32 emojiPosition = 0, emojiLen = 0;
	const EmojiData *emoji = 0;

	QTextDocument *doc(_inner.document());
	QTextCursor c(_inner.textCursor());
	c.joinPreviousEditBlock();
	while (true) {
		int32 start = position, end = position + charsAdded;
		QTextBlock from = doc->findBlock(start), till = doc->findBlock(end);
		if (till.isValid()) till = till.next();

		for (QTextBlock b = from; b != till; b = b.next()) {
			for (QTextBlock::Iterator iter = b.begin(); !iter.atEnd(); ++iter) {
				QTextFragment fragment(iter.fragment());
				if (!fragment.isValid()) continue;

				int32 fp = fragment.position(), fe = fp + fragment.length();
				if (fp >= end || fe <= start) {
					continue;
				}

				QString t(fragment.text());
				const QChar *ch = t.constData(), *e = ch + t.size();
				for (; ch != e; ++ch) {
					emoji = emojiFromText(ch, e, emojiLen);
					if (emoji) {
						emojiPosition = fp + (ch - t.constData());
						break;
					}
					if (ch + 1 < e && ch->isHighSurrogate() && (ch + 1)->isLowSurrogate()) ++ch;
				}
				if (emoji) break;
			}
			if (emoji) break;
		}
		if (emoji) {
			if (!_inner.document()->pageSize().isNull()) {
				_inner.document()->setPageSize(QSizeF(0, 0));
			}

			QTextCursor c(doc->docHandle(), emojiPosition);
			c.setPosition(emojiPosition + emojiLen, QTextCursor::KeepAnchor);
			int32 removedUpto = c.position();

			insertEmoji(emoji, c);

			charsAdded -= removedUpto - position;
			position = emojiPosition + 1;

			emoji = 0;
			emojiPosition = 0;
		} else {
			break;
		}
	}
	c.endEditBlock();
}

void InputArea::onDocumentContentsChange(int position, int charsRemoved, int charsAdded) {
	if (_correcting) return;

	QString oldtext(_oldtext);
	QTextCursor(_inner.document()->docHandle(), 0).joinPreviousEditBlock();

	QTextCursor c(_inner.document()->docHandle(), 0);
	c.movePosition(QTextCursor::End);
	int pos = c.position();

	_correcting = true;
	if (_maxLength >= 0) {
		QTextCursor c(_inner.document()->docHandle(), 0);
		c.movePosition(QTextCursor::End);
		int32 fullSize = c.position(), toRemove = fullSize - _maxLength;
		if (toRemove > 0) {
			if (toRemove > charsAdded) {
				if (charsAdded) {
					c.setPosition(position);
					c.setPosition((position + charsAdded), QTextCursor::KeepAnchor);
					c.removeSelectedText();
				}
				c.setPosition(fullSize - (toRemove - charsAdded));
				c.setPosition(fullSize, QTextCursor::KeepAnchor);
				c.removeSelectedText();
				position = _maxLength;
				charsAdded = 0;
				charsRemoved += toRemove;
			} else {
				c.setPosition(position + (charsAdded - toRemove));
				c.setPosition(position + charsAdded, QTextCursor::KeepAnchor);
				c.removeSelectedText();
				charsAdded -= toRemove;
			}
		}
	}
	_correcting = false;

	QTextCursor(_inner.document()->docHandle(), 0).endEditBlock();

	if (_inner.document()->availableRedoSteps() > 0) return;

	const int takeBack = 3;

	position -= takeBack;
	charsAdded += takeBack;
	if (position < 0) {
		charsAdded += position;
		position = 0;
	}
	if (charsAdded <= 0) return;

	_correcting = true;
	QSizeF s = _inner.document()->pageSize();
	processDocumentContentsChange(position, charsAdded);
	if (_inner.document()->pageSize() != s) {
		_inner.document()->setPageSize(s);
	}
	_correcting = false;
}

void InputArea::onDocumentContentsChanged() {
	if (_correcting) return;

	if (_error) {
		_error = false;
		startBorderAnimation();
	}

	QString curText(getText());
	if (_oldtext != curText) {
		_oldtext = curText;
		emit changed();
		checkContentHeight();
	}
	updatePlaceholder();
	if (App::wnd()) App::wnd()->updateGlobalMenu();
}

void InputArea::onUndoAvailable(bool avail) {
	_undoAvailable = avail;
	if (App::wnd()) App::wnd()->updateGlobalMenu();
}

void InputArea::onRedoAvailable(bool avail) {
	_redoAvailable = avail;
	if (App::wnd()) App::wnd()->updateGlobalMenu();
}

bool InputArea::animStep_placeholderFg(float64 ms) {
	float dt = ms / _st.duration;
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

bool InputArea::animStep_placeholderShift(float64 ms) {
	float dt = ms / _st.duration;
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

bool InputArea::animStep_border(float64 ms) {
	float dt = ms / _st.duration;
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

void InputArea::updatePlaceholder() {
	bool placeholderVisible = _oldtext.isEmpty();
	if (placeholderVisible != _placeholderVisible) {
		_placeholderVisible = placeholderVisible;

		a_placeholderLeft.start(_placeholderVisible ? 0 : _st.placeholderShift);
		a_placeholderOpacity.start(_placeholderVisible ? 1 : 0);
		_a_placeholderShift.start();
	}
}

QMimeData *InputArea::InputAreaInner::createMimeDataFromSelection() const {
	QMimeData *result = new QMimeData();
	QTextCursor c(textCursor());
	int32 start = c.selectionStart(), end = c.selectionEnd();
	if (end > start) {
		result->setText(f()->getText(start, end));
	}
	return result;
}

void InputArea::customUpDown(bool custom) {
	_customUpDown = custom;
}

void InputArea::setCtrlEnterSubmit(bool ctrlEnterSubmit) {
	_ctrlEnterSubmit = ctrlEnterSubmit;
}

void InputArea::InputAreaInner::keyPressEvent(QKeyEvent *e) {
	bool shift = e->modifiers().testFlag(Qt::ShiftModifier), alt = e->modifiers().testFlag(Qt::AltModifier);
	bool macmeta = (cPlatform() == dbipMac) && e->modifiers().testFlag(Qt::ControlModifier) && !e->modifiers().testFlag(Qt::MetaModifier) && !e->modifiers().testFlag(Qt::AltModifier);
	bool ctrl = e->modifiers().testFlag(Qt::ControlModifier) || e->modifiers().testFlag(Qt::MetaModifier), ctrlGood = (ctrl && f()->_ctrlEnterSubmit) || (!ctrl && !shift && !f()->_ctrlEnterSubmit) || (ctrl && shift);
	bool enter = (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return);

	if (macmeta && e->key() == Qt::Key_Backspace) {
		QTextCursor tc(textCursor()), start(tc);
		start.movePosition(QTextCursor::StartOfLine);
		tc.setPosition(start.position(), QTextCursor::KeepAnchor);
		tc.removeSelectedText();
	} else if (enter && ctrlGood) {
		emit f()->submitted(ctrl && shift);
	} else if (e->key() == Qt::Key_Escape) {
		e->ignore();
		emit f()->cancelled();
	} else if (e->key() == Qt::Key_Tab || e->key() == Qt::Key_Backtab) {
		if (alt || ctrl) {
			e->ignore();
		} else {
			if (!focusNextPrevChild(e->key() == Qt::Key_Tab && !shift)) {
				e->ignore();
			}
		}
	} else if (e->key() == Qt::Key_Search || e == QKeySequence::Find) {
		e->ignore();
	} else if (f()->_customUpDown && (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down)) {
		e->ignore();
	} else {
		QTextCursor tc(textCursor());
		if (enter && ctrl) {
			e->setModifiers(e->modifiers() & ~Qt::ControlModifier);
		}
		QTextEdit::keyPressEvent(e);
		if (tc == textCursor()) {
			bool check = false;
			if (e->key() == Qt::Key_PageUp || e->key() == Qt::Key_Up) {
				tc.movePosition(QTextCursor::Start, e->modifiers().testFlag(Qt::ShiftModifier) ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
				check = true;
			} else if (e->key() == Qt::Key_PageDown || e->key() == Qt::Key_Down) {
				tc.movePosition(QTextCursor::End, e->modifiers().testFlag(Qt::ShiftModifier) ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
				check = true;
			}
			if (check) {
				if (tc == textCursor()) {
					e->ignore();
				} else {
					setTextCursor(tc);
				}
			}
		}
	}
}

void InputArea::InputAreaInner::paintEvent(QPaintEvent *e) {
	return QTextEdit::paintEvent(e);
}

void InputArea::resizeEvent(QResizeEvent *e) {
	_placeholder = _st.font->elided(_placeholderFull, width() - _st.textMargins.left() - _st.textMargins.right() - _st.placeholderMargins.left() - _st.placeholderMargins.right() - 1);
	_inner.setGeometry(rect().marginsRemoved(_st.textMargins));
	TWidget::resizeEvent(e);
	checkContentHeight();
}

void InputArea::showError() {
	_error = true;
	if (hasFocus()) {
		startBorderAnimation();
	} else {
		_inner.setFocus();
	}
}

InputField::InputField(QWidget *parent, const style::InputField &st, const QString &ph, const QString &val) : TWidget(parent),
_maxLength(-1),
_inner(this),
_oldtext(val),

_undoAvailable(false),
_redoAvailable(false),

_customUpDown(true),

_placeholderFull(ph),
_placeholderVisible(val.isEmpty()),
a_placeholderLeft(_placeholderVisible ? 0 : st.placeholderShift),
a_placeholderOpacity(_placeholderVisible ? 1 : 0),
a_placeholderFg(st.placeholderFg->c),
_a_placeholderFg(animFunc(this, &InputField::animStep_placeholderFg)),
_a_placeholderShift(animFunc(this, &InputField::animStep_placeholderShift)),

a_borderOpacityActive(0),
a_borderFg(st.borderFg->c),
_a_border(animFunc(this, &InputField::animStep_border)),

_focused(false), _error(false),

_st(st),

_touchPress(false),
_touchRightButton(false),
_touchMove(false),
_correcting(false) {
	_inner.setAcceptRichText(false);
	resize(_st.width, _st.height);

	_inner.setWordWrapMode(QTextOption::NoWrap);
	_inner.setLineWrapMode(QTextEdit::NoWrap);

	setAttribute(Qt::WA_OpaquePaintEvent);

	_inner.setFont(_st.font->f);
	_inner.setAlignment(_st.textAlign);

	_placeholder = _st.font->elided(_placeholderFull, width() - _st.textMargins.left() - _st.textMargins.right() - _st.placeholderMargins.left() - _st.placeholderMargins.right() - 1);

	QPalette p(palette());
	p.setColor(QPalette::Text, _st.textFg->c);
	setPalette(p);

	_inner.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	_inner.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	_inner.setFrameStyle(QFrame::NoFrame | QFrame::Plain);
	_inner.viewport()->setAutoFillBackground(false);

	_inner.setContentsMargins(0, 0, 0, 0);
	_inner.document()->setDocumentMargin(0);

	setAttribute(Qt::WA_AcceptTouchEvents);
	_inner.viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
	_touchTimer.setSingleShot(true);
	connect(&_touchTimer, SIGNAL(timeout()), this, SLOT(onTouchTimer()));

	connect(_inner.document(), SIGNAL(contentsChange(int,int,int)), this, SLOT(onDocumentContentsChange(int,int,int)));
	connect(_inner.document(), SIGNAL(contentsChanged()), this, SLOT(onDocumentContentsChanged()));
	connect(&_inner, SIGNAL(undoAvailable(bool)), this, SLOT(onUndoAvailable(bool)));
	connect(&_inner, SIGNAL(redoAvailable(bool)), this, SLOT(onRedoAvailable(bool)));
	if (App::wnd()) connect(&_inner, SIGNAL(selectionChanged()), App::wnd(), SLOT(updateGlobalMenu()));
	
	setCursor(style::cur_text);
	if (!val.isEmpty()) {
		_inner.setPlainText(val);
	}
	_inner.document()->clearUndoRedoStacks();
}

void InputField::onTouchTimer() {
	_touchRightButton = true;
}

InputField::InputFieldInner::InputFieldInner(InputField *parent) : QTextEdit(parent) {
}

bool InputField::InputFieldInner::viewportEvent(QEvent *e) {
	if (e->type() == QEvent::TouchBegin || e->type() == QEvent::TouchUpdate || e->type() == QEvent::TouchEnd || e->type() == QEvent::TouchCancel) {
		QTouchEvent *ev = static_cast<QTouchEvent*>(e);
		if (ev->device()->type() == QTouchDevice::TouchScreen) {
			qobject_cast<InputField*>(parentWidget())->touchEvent(ev);
			return QTextEdit::viewportEvent(e);
		}
	}
	return QTextEdit::viewportEvent(e);
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

void InputField::paintEvent(QPaintEvent *e) {
	Painter p(this);

	QRect r(rect().intersected(e->rect()));
	p.fillRect(r, st::white->b);
	if (_st.border) {
		p.fillRect(0, height() - _st.border, width(), _st.border, _st.borderFg->b);
	}
	if (_st.borderActive && a_borderOpacityActive.current() > 0) {
		p.setOpacity(a_borderOpacityActive.current());
		p.fillRect(0, height() - _st.borderActive, width(), _st.borderActive, a_borderFg.current());
		p.setOpacity(1);
	}
	if (_st.iconSprite.pxWidth()) {
		p.drawSpriteLeft(_st.iconPosition, width(), _st.iconSprite);
	}
	
	bool drawPlaceholder = _placeholderVisible;
	if (_a_placeholderShift.animating()) {
		p.setOpacity(a_placeholderOpacity.current());
		drawPlaceholder = true;
	}
	if (drawPlaceholder) {
		p.save();
		p.setClipRect(r);

		QRect r(rect().marginsRemoved(_st.textMargins + _st.placeholderMargins));
		r.moveLeft(r.left() + a_placeholderLeft.current());
		if (rtl()) r.moveLeft(width() - r.left() - r.width());
	
		p.setFont(_st.font);
		p.setPen(a_placeholderFg.current());
		p.drawText(r, _placeholder, _st.placeholderAlign);
	
		p.restore();
	}
	TWidget::paintEvent(e);
}

void InputField::startBorderAnimation() {
	a_borderFg.start((_error ? _st.borderFgError : (_focused ? _st.borderFgActive : _st.borderFg))->c);
	a_borderOpacityActive.start((_error || _focused) ? 1 : 0);
	_a_border.start();
}

void InputField::focusInEvent(QFocusEvent *e) {
	_inner.setFocus();
}

void InputField::mousePressEvent(QMouseEvent *e) {
	_inner.setFocus();
}

void InputField::contextMenuEvent(QContextMenuEvent *e) {
	_inner.contextMenuEvent(e);
}

void InputField::InputFieldInner::focusInEvent(QFocusEvent *e) {
	f()->focusInInner();
	QTextEdit::focusInEvent(e);
	emit f()->focused();
}

void InputField::focusInInner() {
	if (!_focused) {
		_focused = true;

		a_placeholderFg.start(_st.placeholderFgActive->c);
		_a_placeholderFg.start();

		startBorderAnimation();
	}
}

void InputField::InputFieldInner::focusOutEvent(QFocusEvent *e) {
	f()->focusOutInner();
	QTextEdit::focusOutEvent(e);
	emit f()->blurred();
}

void InputField::focusOutInner() {
	if (_focused) {
		_focused = false;

		a_placeholderFg.start(_st.placeholderFg->c);
		_a_placeholderFg.start();

		startBorderAnimation();
	}
}

QSize InputField::sizeHint() const {
	return geometry().size();
}

QSize InputField::minimumSizeHint() const {
	return geometry().size();
}

QString InputField::getText(int32 start, int32 end) const {
	if (end >= 0 && end <= start) return QString();

	if (start < 0) start = 0;
	bool full = (start == 0) && (end < 0);

	QTextDocument *doc(_inner.document());
	QTextBlock from = full ? doc->begin() : doc->findBlock(start), till = (end < 0) ? doc->end() : doc->findBlock(end);
	if (till.isValid()) till = till.next();

	int32 possibleLen = 0;
	for (QTextBlock b = from; b != till; b = b.next()) {
		possibleLen += b.length();
	}
	QString result;
	result.reserve(possibleLen + 1);
	if (!full && end < 0) {
		end = possibleLen;
	}

	for (QTextBlock b = from; b != till; b = b.next()) {
		for (QTextBlock::Iterator iter = b.begin(); !iter.atEnd(); ++iter) {
			QTextFragment fragment(iter.fragment());
			if (!fragment.isValid()) continue;

			int32 p = full ? 0 : fragment.position(), e = full ? 0 : (p + fragment.length());
			if (!full) {
				if (p >= end || e <= start) {
					continue;
				}
			}

			QTextCharFormat f = fragment.charFormat();
			QString emojiText;
			QString t(fragment.text());
			if (!full) {
				if (p < start) {
					t = t.mid(start - p, end - start);
				} else if (e > end) {
					t = t.mid(0, end - p);
				}
			}
			QChar *ub = t.data(), *uc = ub, *ue = uc + t.size();
			for (; uc != ue; ++uc) {
				switch (uc->unicode()) {
				case 0xfdd0: // QTextBeginningOfFrame
				case 0xfdd1: // QTextEndOfFrame
				case QChar::ParagraphSeparator:
				case QChar::LineSeparator:
					*uc = QLatin1Char('\n');
					break;
				case QChar::Nbsp:
					*uc = QLatin1Char(' ');
					break;
				case QChar::ObjectReplacementCharacter:
					if (emojiText.isEmpty() && f.isImageFormat()) {
						QString imageName = static_cast<QTextImageFormat*>(&f)->name();
						if (imageName.startsWith(qstr("emoji://e."))) {
							if (EmojiPtr emoji = emojiFromUrl(imageName)) {
								emojiText = emojiString(emoji);
							}
						}
					}
					if (uc > ub) result.append(ub, uc - ub);
					if (!emojiText.isEmpty()) result.append(emojiText);
					ub = uc + 1;
					break;
				}
			}
			if (uc > ub) result.append(ub, uc - ub);
		}
		result.append('\n');
	}
	result.chop(1);
	return result;
}

bool InputField::hasText() const {
	QTextDocument *doc(_inner.document());
	QTextBlock from = doc->begin(), till = doc->end();

	if (from == till) return false;

	for (QTextBlock::Iterator iter = from.begin(); !iter.atEnd(); ++iter) {
		QTextFragment fragment(iter.fragment());
		if (!fragment.isValid()) continue;
		if (!fragment.text().isEmpty()) return true;
	}
	return (from.next() != till);
}

bool InputField::isUndoAvailable() const {
	return _undoAvailable;
}

bool InputField::isRedoAvailable() const {
	return _redoAvailable;
}

void InputField::insertEmoji(EmojiPtr emoji, QTextCursor c) {
	QTextImageFormat imageFormat;
	int32 ew = ESize + st::emojiPadding * cIntRetinaFactor() * 2, eh = _st.font->height * cIntRetinaFactor();
	imageFormat.setWidth(ew / cIntRetinaFactor());
	imageFormat.setHeight(eh / cIntRetinaFactor());
	imageFormat.setName(qsl("emoji://e.") + QString::number(emojiKey(emoji), 16));
	imageFormat.setVerticalAlignment(QTextCharFormat::AlignBaseline);

	static QString objectReplacement(QChar::ObjectReplacementCharacter);
	c.insertText(objectReplacement, imageFormat);
}

QVariant InputField::InputFieldInner::loadResource(int type, const QUrl &name) {
	QString imageName = name.toDisplayString();
	if (imageName.startsWith(qstr("emoji://e."))) {
		if (EmojiPtr emoji = emojiFromUrl(imageName)) {
			return QVariant(App::emojiSingle(emoji, f()->_st.font->height));
		}
	}
	return QVariant();
}

void InputField::processDocumentContentsChange(int position, int charsAdded) {
	int32 emojiPosition = 0, emojiLen = 0;
	const EmojiData *emoji = 0;

	static QString space(' ');

	QTextDocument *doc(_inner.document());
	QTextCursor c(_inner.textCursor());
	c.joinPreviousEditBlock();
	while (true) {
		int32 start = position, end = position + charsAdded;
		QTextBlock from = doc->findBlock(start), till = doc->findBlock(end);
		if (till.isValid()) till = till.next();

		for (QTextBlock b = from; b != till; b = b.next()) {
			for (QTextBlock::Iterator iter = b.begin(); !iter.atEnd(); ++iter) {
				QTextFragment fragment(iter.fragment());
				if (!fragment.isValid()) continue;

				int32 fp = fragment.position(), fe = fp + fragment.length();
				if (fp >= end || fe <= start) {
					continue;
				}

				QString t(fragment.text());
				const QChar *ch = t.constData(), *e = ch + t.size();
				for (; ch != e; ++ch) {
					// QTextBeginningOfFrame // QTextEndOfFrame
					if (ch->unicode() == 0xfdd0 || ch->unicode() == 0xfdd1 || ch->unicode() == QChar::ParagraphSeparator || ch->unicode() == QChar::LineSeparator || ch->unicode() == '\n' || ch->unicode() == '\r') {
						if (!_inner.document()->pageSize().isNull()) {
							_inner.document()->setPageSize(QSizeF(0, 0));
						}
						int32 nlPosition = fp + (ch - t.constData());
						QTextCursor c(doc->docHandle(), nlPosition);
						c.setPosition(nlPosition + 1, QTextCursor::KeepAnchor);
						c.insertText(space);
						position = nlPosition + 1;
						emoji = TwoSymbolEmoji; // just a flag
						break;
					}
					emoji = emojiFromText(ch, e, emojiLen);
					if (emoji) {
						emojiPosition = fp + (ch - t.constData());
						break;
					}
					if (ch + 1 < e && ch->isHighSurrogate() && (ch + 1)->isLowSurrogate()) ++ch;
				}
				if (emoji) break;
			}
			if (emoji) break;
			if (b.next() != doc->end()) {
				int32 nlPosition = b.next().position() - 1;
				QTextCursor c(doc->docHandle(), nlPosition);
				c.setPosition(nlPosition + 1, QTextCursor::KeepAnchor);
				c.insertText(space);
				position = nlPosition + 1;
				emoji = TwoSymbolEmoji; // just a flag
				break;
			}
		}
		if (emoji == TwoSymbolEmoji) { // just skip
			emoji = 0;
			emojiPosition = 0;
		} else if (emoji) {
			if (!_inner.document()->pageSize().isNull()) {
				_inner.document()->setPageSize(QSizeF(0, 0));
			}

			QTextCursor c(doc->docHandle(), emojiPosition);
			c.setPosition(emojiPosition + emojiLen, QTextCursor::KeepAnchor);
			int32 removedUpto = c.position();

			insertEmoji(emoji, c);

			charsAdded -= removedUpto - position;
			position = emojiPosition + 1;

			emoji = 0;
			emojiPosition = 0;
		} else {
			break;
		}
	}
	c.endEditBlock();
}

void InputField::onDocumentContentsChange(int position, int charsRemoved, int charsAdded) {
	if (_correcting) return;

	QString oldtext(_oldtext);
	QTextCursor(_inner.document()->docHandle(), 0).joinPreviousEditBlock();

	QTextCursor c(_inner.document()->docHandle(), 0);
	c.movePosition(QTextCursor::End);
	int pos = c.position();

	_correcting = true;
	if (_maxLength >= 0) {
		QTextCursor c(_inner.document()->docHandle(), 0);
		c.movePosition(QTextCursor::End);
		int32 fullSize = c.position(), toRemove = fullSize - _maxLength;
		if (toRemove > 0) {
			if (toRemove > charsAdded) {
				if (charsAdded) {
					c.setPosition(position);
					c.setPosition((position + charsAdded), QTextCursor::KeepAnchor);
					c.removeSelectedText();
				}
				c.setPosition(fullSize - (toRemove - charsAdded));
				c.setPosition(fullSize, QTextCursor::KeepAnchor);
				c.removeSelectedText();
				position = _maxLength;
				charsAdded = 0;
				charsRemoved += toRemove;
			} else {
				c.setPosition(position + (charsAdded - toRemove));
				c.setPosition(position + charsAdded, QTextCursor::KeepAnchor);
				c.removeSelectedText();
				charsAdded -= toRemove;
			}
		}
	}
	_correcting = false;

	QTextCursor(_inner.document()->docHandle(), 0).endEditBlock();

	if (_inner.document()->availableRedoSteps() > 0) return;

	const int takeBack = 3;

	position -= takeBack;
	charsAdded += takeBack;
	if (position < 0) {
		charsAdded += position;
		position = 0;
	}
	if (charsAdded <= 0) return;

	_correcting = true;
	QSizeF s = _inner.document()->pageSize();
	processDocumentContentsChange(position, charsAdded);
	if (_inner.document()->pageSize() != s) {
		_inner.document()->setPageSize(s);
	}
	_correcting = false;
}

void InputField::onDocumentContentsChanged() {
	if (_correcting) return;

	if (_error) {
		_error = false;
		startBorderAnimation();
	}

	QString curText(getText());
	if (_oldtext != curText) {
		_oldtext = curText;
		emit changed();
	}
	updatePlaceholder();
	if (App::wnd()) App::wnd()->updateGlobalMenu();
}

void InputField::onUndoAvailable(bool avail) {
	_undoAvailable = avail;
	if (App::wnd()) App::wnd()->updateGlobalMenu();
}

void InputField::onRedoAvailable(bool avail) {
	_redoAvailable = avail;
	if (App::wnd()) App::wnd()->updateGlobalMenu();
}

void InputField::selectAll() {
	QTextCursor c(_inner.textCursor());
	c.setPosition(0);
	c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
	_inner.setTextCursor(c);
}

bool InputField::animStep_placeholderFg(float64 ms) {
	float dt = ms / _st.duration;
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

bool InputField::animStep_placeholderShift(float64 ms) {
	float dt = ms / _st.duration;
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

bool InputField::animStep_border(float64 ms) {
	float dt = ms / _st.duration;
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
	bool placeholderVisible = _oldtext.isEmpty();
	if (placeholderVisible != _placeholderVisible) {
		_placeholderVisible = placeholderVisible;

		a_placeholderLeft.start(_placeholderVisible ? 0 : _st.placeholderShift);
		a_placeholderOpacity.start(_placeholderVisible ? 1 : 0);
		_a_placeholderShift.start();
	}
}

QMimeData *InputField::InputFieldInner::createMimeDataFromSelection() const {
	QMimeData *result = new QMimeData();
	QTextCursor c(textCursor());
	int32 start = c.selectionStart(), end = c.selectionEnd();
	if (end > start) {
		result->setText(f()->getText(start, end));
	}
	return result;
}

void InputField::customUpDown(bool custom) {
	_customUpDown = custom;
}

void InputField::InputFieldInner::keyPressEvent(QKeyEvent *e) {
	bool shift = e->modifiers().testFlag(Qt::ShiftModifier), alt = e->modifiers().testFlag(Qt::AltModifier);
	bool macmeta = (cPlatform() == dbipMac) && e->modifiers().testFlag(Qt::ControlModifier) && !e->modifiers().testFlag(Qt::MetaModifier) && !e->modifiers().testFlag(Qt::AltModifier);
	bool ctrl = e->modifiers().testFlag(Qt::ControlModifier) || e->modifiers().testFlag(Qt::MetaModifier), ctrlGood = true;
	bool enter = (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return);

	if (macmeta && e->key() == Qt::Key_Backspace) {
		QTextCursor tc(textCursor()), start(tc);
		start.movePosition(QTextCursor::StartOfLine);
		tc.setPosition(start.position(), QTextCursor::KeepAnchor);
		tc.removeSelectedText();
	} else if (enter && ctrlGood) {
		emit f()->submitted(ctrl && shift);
	} else if (e->key() == Qt::Key_Escape) {
		e->ignore();
		emit f()->cancelled();
	} else if (e->key() == Qt::Key_Tab || e->key() == Qt::Key_Backtab) {
		if (alt || ctrl) {
			e->ignore();
		} else {
			if (!focusNextPrevChild(e->key() == Qt::Key_Tab && !shift)) {
				e->ignore();
			}
		}
	} else if (e->key() == Qt::Key_Search || e == QKeySequence::Find) {
		e->ignore();
	} else if (f()->_customUpDown && (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down)) {
		e->ignore();
	} else {
		QTextCursor tc(textCursor());
		if (enter && ctrl) {
			e->setModifiers(e->modifiers() & ~Qt::ControlModifier);
		}
		QTextEdit::keyPressEvent(e);
		if (tc == textCursor()) {
			bool check = false;
			if (e->key() == Qt::Key_PageUp || e->key() == Qt::Key_Up) {
				tc.movePosition(QTextCursor::Start, e->modifiers().testFlag(Qt::ShiftModifier) ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
				check = true;
			} else if (e->key() == Qt::Key_PageDown || e->key() == Qt::Key_Down) {
				tc.movePosition(QTextCursor::End, e->modifiers().testFlag(Qt::ShiftModifier) ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
				check = true;
			}
			if (check) {
				if (tc == textCursor()) {
					e->ignore();
				} else {
					setTextCursor(tc);
				}
			}
		}
	}
}

void InputField::InputFieldInner::paintEvent(QPaintEvent *e) {
	return QTextEdit::paintEvent(e);
}

void InputField::resizeEvent(QResizeEvent *e) {
	_placeholder = _st.font->elided(_placeholderFull, width() - _st.textMargins.left() - _st.textMargins.right() - _st.placeholderMargins.left() - _st.placeholderMargins.right() - 1);
	_inner.setGeometry(rect().marginsRemoved(_st.textMargins));
	TWidget::resizeEvent(e);
}

void InputField::showError() {
	_error = true;
	if (hasFocus()) {
		startBorderAnimation();
	} else {
		_inner.setFocus();
	}
}

MaskedInputField::MaskedInputField(QWidget *parent, const style::InputField &st, const QString &placeholder, const QString &val) : QLineEdit(val, parent),
_st(st),
_maxLength(-1),
_oldtext(val),

_undoAvailable(false),
_redoAvailable(false),

_customUpDown(false),

_placeholderFull(placeholder),
_placeholderVisible(val.isEmpty()),
_placeholderFast(false),
a_placeholderLeft(_placeholderVisible ? 0 : st.placeholderShift),
a_placeholderOpacity(_placeholderVisible ? 1 : 0),
a_placeholderFg(st.placeholderFg->c),
_a_placeholderFg(animFunc(this, &MaskedInputField::animStep_placeholderFg)),
_a_placeholderShift(animFunc(this, &MaskedInputField::animStep_placeholderShift)),

a_borderOpacityActive(0),
a_borderFg(st.borderFg->c),
_a_border(animFunc(this, &MaskedInputField::animStep_border)),

_focused(false), _error(false),

_touchPress(false),
_touchRightButton(false),
_touchMove(false) {
	resize(_st.width, _st.height);

	setFont(_st.font->f);
	setAlignment(_st.textAlign);

	QPalette p(palette());
	p.setColor(QPalette::Text, _st.textFg->c);
	setPalette(p);

	setAttribute(Qt::WA_OpaquePaintEvent);

	connect(this, SIGNAL(textChanged(const QString&)), this, SLOT(onTextChange(const QString&)));
	connect(this, SIGNAL(cursorPositionChanged(int,int)), this, SLOT(onCursorPositionChanged(int,int)));

	connect(this, SIGNAL(textEdited(const QString&)), this, SLOT(onTextEdited()));
	if (App::wnd()) connect(this, SIGNAL(selectionChanged()), App::wnd(), SLOT(updateGlobalMenu()));

	setStyle(&_inputFieldStyle);
	QLineEdit::setTextMargins(0, 0, 0, 0);
	setContentsMargins(0, 0, 0, 0);
	
	setAttribute(Qt::WA_AcceptTouchEvents);
	_touchTimer.setSingleShot(true);
	connect(&_touchTimer, SIGNAL(timeout()), this, SLOT(onTouchTimer()));

	setTextMargins(_st.textMargins);
	updatePlaceholder();
}

void MaskedInputField::customUpDown(bool custom) {
	_customUpDown = custom;
}

void MaskedInputField::setTextMargins(const QMargins &mrg) {
	_textMargins = mrg;
	_placeholder = _st.font->elided(_placeholderFull, width() - _textMargins.left() - _textMargins.right() - _st.placeholderMargins.left() - _st.placeholderMargins.right() - 1);
}

void MaskedInputField::onTouchTimer() {
	_touchRightButton = true;
}

bool MaskedInputField::event(QEvent *e) {
	if (e->type() == QEvent::TouchBegin || e->type() == QEvent::TouchUpdate || e->type() == QEvent::TouchEnd || e->type() == QEvent::TouchCancel) {
		QTouchEvent *ev = static_cast<QTouchEvent*>(e);
		if (ev->device()->type() == QTouchDevice::TouchScreen) {
			touchEvent(ev);
			return QLineEdit::event(e);
		}
	}
	return QLineEdit::event(e);
}

void MaskedInputField::touchEvent(QTouchEvent *e) {
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

QRect MaskedInputField::getTextRect() const {
	return rect().marginsRemoved(_textMargins + QMargins(-2, -1, -2, -1));
}

void MaskedInputField::paintEvent(QPaintEvent *e) {
	Painter p(this);

	QRect r(rect().intersected(e->rect()));
	p.fillRect(r, st::white->b);
	if (_st.border) {
		p.fillRect(0, height() - _st.border, width(), _st.border, _st.borderFg->b);
	}
	if (_st.borderActive && a_borderOpacityActive.current() > 0) {
		p.setOpacity(a_borderOpacityActive.current());
		p.fillRect(0, height() - _st.borderActive, width(), _st.borderActive, a_borderFg.current());
		p.setOpacity(1);
	}
	if (_st.iconSprite.pxWidth()) {
		p.drawSpriteLeft(_st.iconPosition, width(), _st.iconSprite);
	}

	p.setClipRect(r);
	paintPlaceholder(p);

	QLineEdit::paintEvent(e);
}

void MaskedInputField::startBorderAnimation() {
	a_borderFg.start((_error ? _st.borderFgError : (_focused ? _st.borderFgActive : _st.borderFg))->c);
	a_borderOpacityActive.start((_error || _focused) ? 1 : 0);
	_a_border.start();
}

void MaskedInputField::focusInEvent(QFocusEvent *e) {
	if (!_focused) {
		_focused = true;

		a_placeholderFg.start(_st.placeholderFgActive->c);
		_a_placeholderFg.start();

		startBorderAnimation();
	}
	QLineEdit::focusInEvent(e);
	emit focused();
}

void MaskedInputField::focusOutEvent(QFocusEvent *e) {
	if (_focused) {
		_focused = false;

		a_placeholderFg.start(_st.placeholderFg->c);
		_a_placeholderFg.start();

		startBorderAnimation();
	}
	QLineEdit::focusOutEvent(e);
	emit blurred();
}

void MaskedInputField::resizeEvent(QResizeEvent *e) {
	_placeholder = _st.font->elided(_placeholderFull, width() - _textMargins.left() - _textMargins.right() - _st.placeholderMargins.left() - _st.placeholderMargins.right() - 1);
	QLineEdit::resizeEvent(e);
}

void MaskedInputField::showError() {
	_error = true;
	if (hasFocus()) {
		startBorderAnimation();
	} else {
		setFocus();
	}
}

QSize MaskedInputField::sizeHint() const {
	return geometry().size();
}

QSize MaskedInputField::minimumSizeHint() const {
	return geometry().size();
}

bool MaskedInputField::animStep_placeholderFg(float64 ms) {
	float dt = ms / _st.duration;
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

bool MaskedInputField::animStep_placeholderShift(float64 ms) {
	float dt = ms / _st.duration;
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

bool MaskedInputField::animStep_border(float64 ms) {
	float dt = ms / _st.duration;
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

bool MaskedInputField::setPlaceholder(const QString &placeholder) {
	if (_placeholderFull != placeholder) {
		_placeholderFull = placeholder;
		resizeEvent(0);
		update();
		return true;
	}
	return false;
}

void MaskedInputField::setPlaceholderFast(bool fast) {
	_placeholderFast = fast;
	if (_placeholderFast) {
		a_placeholderLeft = anim::ivalue(_placeholderVisible ? 0 : _st.placeholderShift, _placeholderVisible ? 0 : _st.placeholderShift);
		a_placeholderOpacity = anim::fvalue(_placeholderVisible ? 1 : 0, _placeholderVisible ? 1 : 0);
		update();
	}
}

void MaskedInputField::updatePlaceholder() {
	bool placeholderVisible = _oldtext.isEmpty();
	if (placeholderVisible != _placeholderVisible) {
		_placeholderVisible = placeholderVisible;

		if (_placeholderFast) {
			a_placeholderLeft = anim::ivalue(_placeholderVisible ? 0 : _st.placeholderShift, _placeholderVisible ? 0 : _st.placeholderShift);
			a_placeholderOpacity = anim::fvalue(_placeholderVisible ? 1 : 0, _placeholderVisible ? 1 : 0);
			update();
		} else {
			a_placeholderLeft.start(_placeholderVisible ? 0 : _st.placeholderShift);
			a_placeholderOpacity.start(_placeholderVisible ? 1 : 0);
			_a_placeholderShift.start();
		}
	}
}

const QString &MaskedInputField::placeholder() const {
	return _placeholderFull;
}

QRect MaskedInputField::placeholderRect() const {
	return rect().marginsRemoved(_st.textMargins + _st.placeholderMargins);
}

void MaskedInputField::correctValue(const QString &was, int32 wasCursor, QString &now, int32 &nowCursor) {
}

void MaskedInputField::paintPlaceholder(Painter &p) {
	bool drawPlaceholder = _placeholderVisible;
	if (_a_placeholderShift.animating()) {
		p.setOpacity(a_placeholderOpacity.current());
		drawPlaceholder = true;
	}
	if (drawPlaceholder) {
		p.save();

		QRect phRect(placeholderRect());
		phRect.moveLeft(phRect.left() + a_placeholderLeft.current());
		if (rtl()) phRect.moveLeft(width() - phRect.left() - phRect.width());

		placeholderPreparePaint(p);
		p.drawText(phRect, _placeholder, _st.placeholderAlign);

		p.restore();
	}
}

void MaskedInputField::placeholderPreparePaint(Painter &p) {
	p.setFont(_st.font);
	p.setPen(a_placeholderFg.current());
}

void MaskedInputField::keyPressEvent(QKeyEvent *e) {
	QString wasText(_oldtext);
	int32 wasCursor(_oldcursor);

	bool shift = e->modifiers().testFlag(Qt::ShiftModifier), alt = e->modifiers().testFlag(Qt::AltModifier);
	bool ctrl = e->modifiers().testFlag(Qt::ControlModifier) || e->modifiers().testFlag(Qt::MetaModifier), ctrlGood = true;
	if (_customUpDown && (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down)) {
		e->ignore();
	} else {
		QLineEdit::keyPressEvent(e);
	}

	QString newText(text());
	int32 newCursor(cursorPosition());
	if (wasText == newText && wasCursor == newCursor) { // call correct manually
		correctValue(wasText, wasCursor, newText, newCursor);
		_oldtext = newText;
		_oldcursor = newCursor;
		if (wasText != _oldtext) emit changed();
		updatePlaceholder();
	}
	if (e->key() == Qt::Key_Escape) {
		e->ignore();
		emit cancelled();
	} else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		emit submitted(ctrl && shift);
	}
}

void MaskedInputField::onTextEdited() {
	QString wasText(_oldtext), newText(text());
	int32 wasCursor(_oldcursor), newCursor(cursorPosition());

	correctValue(wasText, wasCursor, newText, newCursor);
	_oldtext = newText;
	_oldcursor = newCursor;
	if (wasText != _oldtext) emit changed();
	updatePlaceholder();

	if (App::wnd()) App::wnd()->updateGlobalMenu();
}

void MaskedInputField::onTextChange(const QString &text) {
	_oldtext = QLineEdit::text();
	if (_error) {
		_error = false;
		startBorderAnimation();
	}
	if (App::wnd()) App::wnd()->updateGlobalMenu();
}

void MaskedInputField::onCursorPositionChanged(int oldPosition, int position) {
	_oldcursor = position;
}

PasswordField::PasswordField(QWidget *parent, const style::InputField &st, const QString &ph, const QString &val) : MaskedInputField(parent, st, ph, val) {
	setEchoMode(QLineEdit::Password);
}

PortInput::PortInput(QWidget *parent, const style::InputField &st, const QString &ph, const QString &val) : MaskedInputField(parent, st, ph, val) {
	if (!val.toInt() || val.toInt() > 65535) {
		setText(QString());
	}
}

void PortInput::correctValue(const QString &was, int32 wasCursor, QString &now, int32 &nowCursor) {
	QString newText;
	newText.reserve(now.size());
	int32 newCursor = nowCursor;
	for (int32 i = 0, l = now.size(); i < l; ++i) {
		if (now.at(i).isDigit()) {
			newText.append(now.at(i));
		} else if (i < nowCursor) {
			--newCursor;
		}
	}
	if (!newText.toInt()) {
		newText = QString();
		newCursor = 0;
	} else if (newText.toInt() > 65535) {
		newText = was;
		newCursor = wasCursor;
	}
	if (newText != now) {
		now = newText;
		setText(newText);
	}
	if (newCursor != nowCursor) {
		nowCursor = newCursor;
		setCursorPosition(newCursor);
	}
}

UsernameInput::UsernameInput(QWidget *parent, const style::InputField &st, const QString &ph, const QString &val, bool isLink) : MaskedInputField(parent, st, ph, val),
_linkPlaceholder(isLink ? qsl("telegram.me/") : QString()) {
	if (!_linkPlaceholder.isEmpty()) {
		setTextMargins(style::margins(_st.textMargins.left() + _st.font->width(_linkPlaceholder), _st.textMargins.top(), _st.textMargins.right(), _st.textMargins.bottom()));
	}
}

void UsernameInput::paintPlaceholder(Painter &p) {
	if (_linkPlaceholder.isEmpty()) {
		MaskedInputField::paintPlaceholder(p);
	} else {
		p.setFont(_st.font);
		p.setPen(_st.placeholderFg);
		p.drawText(QRect(_st.textMargins.left(), _st.textMargins.top(), width(), height() - _st.textMargins.top() - _st.textMargins.bottom()), _linkPlaceholder, style::al_topleft);
	}
}

void UsernameInput::correctValue(const QString &was, int32 wasCursor, QString &now, int32 &nowCursor) {
	QString newText;
	int32 newCursor = nowCursor, from, len = now.size();
	for (from = 0; from < len; ++from) {
		if (!now.at(from).isSpace()) {
			break;
		}
		if (newCursor > 0) --newCursor;
	}
	len -= from;
	if (len > MaxUsernameLength) len = MaxUsernameLength + (now.at(from) == '@' ? 1 : 0);
	for (int32 to = from + len; to > from;) {
		--to;
		if (!now.at(to).isSpace()) {
			break;
		}
		--len;
	}
	newText = now.mid(from, len);
	if (newCursor > len) {
		newCursor = len;
	}
	if (newText != now) {
		now = newText;
		setText(newText);
	}
	if (newCursor != nowCursor) {
		nowCursor = newCursor;
		setCursorPosition(newCursor);
	}
}

PhoneInput::PhoneInput(QWidget *parent, const style::InputField &st, const QString &ph, const QString &val) : MaskedInputField(parent, st, ph, val)
, _defaultPlaceholder(ph) {
	QString phone(val);
	if (phone.isEmpty()) {
		clearText();
	} else {
		int32 pos = phone.size();
		correctValue(QString(), 0, phone, pos);
	}
}

void PhoneInput::focusInEvent(QFocusEvent *e) {
	MaskedInputField::focusInEvent(e);
	setSelection(cursorPosition(), cursorPosition());
}

void PhoneInput::clearText() {
	QString phone;
	if (App::self()) {
		QVector<int> newPattern = phoneNumberParse(App::self()->phone);
		if (!newPattern.isEmpty()) {
			phone = App::self()->phone.mid(0, newPattern.at(0));
		}
	}
	setText(phone);
	int32 pos = phone.size();
	correctValue(QString(), 0, phone, pos);
}

void PhoneInput::paintPlaceholder(Painter &p) {
	QString t(getLastText());
	if (!pattern.isEmpty() && !t.isEmpty()) {
		QString ph = placeholder().mid(t.size());
		if (!ph.isEmpty()) {
			p.setClipRect(rect());
			QRect phRect(placeholderRect());
			int tw = phFont()->width(t);
			if (tw < phRect.width()) {
				phRect.setLeft(phRect.left() + tw);
				placeholderPreparePaint(p);
				p.drawText(phRect, ph, style::al_topleft);
			}
		}
	} else {
		MaskedInputField::paintPlaceholder(p);
	}
}

void PhoneInput::correctValue(const QString &was, int32 wasCursor, QString &now, int32 &nowCursor) {
	QString digits(now);
	digits.replace(QRegularExpression(qsl("[^\\d]")), QString());
	pattern = phoneNumberParse(digits);

	QString newPlaceholder;
	if (pattern.isEmpty()) {
		newPlaceholder = lang(lng_contact_phone);
	} else if (pattern.size() == 1 && pattern.at(0) == digits.size()) {
		newPlaceholder = QString(pattern.at(0) + 2, ' ') + lang(lng_contact_phone);
	} else {
		newPlaceholder.reserve(20);
		for (int i = 0, l = pattern.size(); i < l; ++i) {
			if (i) {
				newPlaceholder.append(' ');
			} else {
				newPlaceholder.append('+');
			}
			newPlaceholder.append(i ? QString(pattern.at(i), QChar(0x2212)) : digits.mid(0, pattern.at(i)));
		}
	}
	if (setPlaceholder(newPlaceholder)) {
		setPlaceholderFast(!pattern.isEmpty());
		updatePlaceholder();
	}

	QString newText;
	int oldPos(nowCursor), newPos(-1), oldLen(now.length()), digitCount = qMin(digits.size(), MaxPhoneCodeLength + MaxPhoneTailLength);

	bool inPart = !pattern.isEmpty(), plusFound = false;
	int curPart = 0, leftInPart = inPart ? pattern.at(curPart) : 0;
	newText.reserve(oldLen + 1);
	newText.append('+');
	for (int i = 0; i < oldLen; ++i) {
		if (i == oldPos && newPos < 0) {
			newPos = newText.length();
		}

		QChar ch(now[i]);
		if (ch.isDigit()) {
			if (!digitCount--) {
				break;
			}
			if (inPart) {
				if (leftInPart) {
					--leftInPart;
				} else {
					newText += ' ';
					++curPart;
					inPart = curPart < pattern.size();
					leftInPart = inPart ? (pattern.at(curPart) - 1) : 0;

					++oldPos;
				}
			}
			newText += ch;
		} else if (ch == ' ' || ch == '-' || ch == '(' || ch == ')') {
			if (inPart) {
				if (leftInPart) {
				} else {
					newText += ch;
					++curPart;
					inPart = curPart < pattern.size();
					leftInPart = inPart ? pattern.at(curPart) : 0;
				}
			} else {
				newText += ch;
			}
		} else if (ch == '+') {
			plusFound = true;
		}
	}
	if (!plusFound && newText == qstr("+")) {
		newText = QString();
		newPos = 0;
	}
	int32 newlen = newText.size();
	while (newlen > 0 && newText.at(newlen - 1).isSpace()) {
		--newlen;
	}
	if (newlen < newText.size()) newText = newText.mid(0, newlen);
	if (newPos < 0) {
		newPos = newText.length();
	}
	if (newText != now) {
		now = newText;
		setText(newText);
		updatePlaceholder();
		setCursorPosition(newPos);
	}
}
