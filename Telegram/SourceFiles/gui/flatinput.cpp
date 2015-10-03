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
}

FlatInput::FlatInput(QWidget *parent, const style::flatInput &st, const QString &pholder, const QString &v) : QLineEdit(v, parent),
_fullph(pholder), _oldtext(v), _fastph(false), _kev(0), _customUpDown(false), _phVisible(!v.length()),
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

void FlatInput::setTextMargin(const QMargins &mrg) {
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

void FlatInput::correctValue(QKeyEvent *e, const QString &was) {
}

void FlatInput::phPrepare(Painter &p) {
	p.setFont(_st.font->f);
	p.setPen(a_phColor.current());
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

UsernameInput::UsernameInput(QWidget *parent, const style::flatInput &st, const QString &ph, const QString &val) : FlatInput(parent, st, ph, val) {
}

void UsernameInput::correctValue(QKeyEvent *e, const QString &was) {
	QString oldText(text()), newText;
	int32 newPos = cursorPosition(), from, len = oldText.size();
	for (from = 0; from < len; ++from) {
		if (!oldText.at(from).isSpace()) {
			break;
		}
		if (newPos > 0) --newPos;
	}
	len -= from;
	if (len > MaxUsernameLength) len = MaxUsernameLength + (oldText.at(from) == '@' ? 1 : 0);
	for (int32 to = from + len; to > from;) {
		--to;
		if (!oldText.at(to).isSpace()) {
			break;
		}
		--len;
	}
	newText = oldText.mid(from, len);
	if (newText != oldText) {
		setText(newText);
		setCursorPosition(newPos);
	}
}

InputField::InputField(QWidget *parent, const style::InputField &st, const QString &ph, const QString &val) : TWidget(parent),
_inner(this, val),
_oldtext(val),
_keyEvent(0),

_undoAvailable(false),
_redoAvailable(false),

_fakeMargin(0),
_customUpDown(false),

_placeholderFull(ph),
_placeholderVisible(val.isEmpty()),
a_placeholderLeft(_placeholderVisible ? 0 : st.placeholderShift),
a_placeholderOpacity(_placeholderVisible ? 1 : 0),
a_placeholderFg(st.placeholderFg->c),
_placeholderFgAnim(animFunc(this, &InputField::placeholderFgStep)),
_placeholderShiftAnim(animFunc(this, &InputField::placeholderShiftStep)),

a_borderOpacityActive(0),
a_borderFg(st.borderFg->c),
_borderAnim(animFunc(this, &InputField::borderStep)),

_focused(false), _error(false),

_st(&st),

_touchPress(false),
_touchRightButton(false),
_touchMove(false),
_replacingEmojis(false) {
	_inner.setAcceptRichText(false);
	resize(_st->width, _st->height);

	_inner.setWordWrapMode(QTextOption::NoWrap);
	_inner.setLineWrapMode(QTextEdit::NoWrap);

	_inner.setFont(_st->font->f);
	_inner.setAlignment(cRtl() ? Qt::AlignRight : Qt::AlignLeft);

	_placeholder = _st->font->elided(_placeholderFull, width() - _st->textMargins.left() - _st->textMargins.right() - _st->placeholderMargins.left() - _st->placeholderMargins.right() - 1);

	QPalette p(palette());
	p.setColor(QPalette::Text, _st->textFg->c);
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

	connect(_inner.document(), SIGNAL(contentsChange(int, int, int)), this, SLOT(onDocumentContentsChange(int, int, int)));
	connect(_inner.document(), SIGNAL(contentsChanged()), this, SLOT(onDocumentContentsChanged()));
	connect(&_inner, SIGNAL(undoAvailable(bool)), this, SLOT(onUndoAvailable(bool)));
	connect(&_inner, SIGNAL(redoAvailable(bool)), this, SLOT(onRedoAvailable(bool)));
	if (App::wnd()) connect(&_inner, SIGNAL(selectionChanged()), App::wnd(), SLOT(updateGlobalMenu()));
}

void InputField::onTouchTimer() {
	_touchRightButton = true;
}

InputField::InputFieldInner::InputFieldInner(InputField *parent, const QString &val) : QTextEdit(val, parent) {
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

int32 InputField::fakeMargin() const {
	return _fakeMargin;
}

void InputField::paintEvent(QPaintEvent *e) {
	Painter p(this);

	QRect r(rect().intersected(e->rect()));
	p.fillRect(r, st::white->b);
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
		p.setClipRect(r);

		QRect r(rect().marginsRemoved(_st->textMargins + _st->placeholderMargins));
		r.moveLeft(r.left() + a_placeholderLeft.current());
		if (rtl()) r.moveLeft(width() - r.left() - r.width());
	
		p.setFont(_st->font->f);
		p.setPen(a_placeholderFg.current());
		p.drawText(r, _placeholder, _st->placeholderAlign);
	
		p.restore();
	}
	TWidget::paintEvent(e);
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

		a_placeholderFg.start(_st->placeholderFgActive->c);
		_placeholderFgAnim.start();

		a_borderFg.start((_error ? _st->borderFgError : _st->borderFgActive)->c);
		a_borderOpacityActive.start(1);
		_borderAnim.start();
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

		a_placeholderFg.start(_st->placeholderFg->c);
		_placeholderFgAnim.start();

		a_borderFg.start((_error ? _st->borderFgError : _st->borderFg)->c);
		a_borderOpacityActive.start(_error ? 1 : 0);
		_borderAnim.start();
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
	int32 ew = ESize + st::emojiPadding * cIntRetinaFactor() * 2, eh = _st->font->height * cIntRetinaFactor();
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
			return QVariant(App::emojiSingle(emoji, f()->_st->font->height));
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

			for (Insertions::iterator i = _insertions.begin(), e = _insertions.end(); i != e; ++i) {
				if (i->first >= removedUpto) {
					i->first -= removedUpto - emojiPosition - 1;
				} else if (i->first >= emojiPosition) {
					i->second -= removedUpto - emojiPosition;
					i->first = emojiPosition + 1;
				} else if (i->first + i->second > emojiPosition + 1) {
					i->second -= qMin(removedUpto, i->first + i->second) - emojiPosition;
				}
			}

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
	if (_replacingEmojis) return;

	if (_inner.document()->availableRedoSteps() > 0) return;

	const int takeBack = 3;

	position -= takeBack;
	charsAdded += takeBack;
	if (position < 0) {
		charsAdded += position;
		position = 0;
	}
	if (charsAdded <= 0) return;

	//	_insertions.push_back(Insertion(position, charsAdded));
	_replacingEmojis = true;
	QSizeF s = _inner.document()->pageSize();
	processDocumentContentsChange(position, charsAdded);
	if (_inner.document()->pageSize() != s) {
		_inner.document()->setPageSize(s);
	}
	_replacingEmojis = false;
}

void InputField::onDocumentContentsChanged() {
	if (_replacingEmojis) return;

	if (!_insertions.isEmpty()) {
		if (_inner.document()->availableRedoSteps() > 0) {
			_insertions.clear();
		} else {
			_replacingEmojis = true;
			QSizeF s = _inner.document()->pageSize();

			do {
				Insertion i = _insertions.front();
				_insertions.pop_front();
				if (i.second > 0) {
					processDocumentContentsChange(i.first, i.second);
				}
			} while (!_insertions.isEmpty());

			if (_inner.document()->pageSize() != s) {
				_inner.document()->setPageSize(s);
			}
			_replacingEmojis = false;
		}
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
const QString &InputField::getLastText() const {
	return _oldtext;
}

void InputField::updatePlaceholder() {
	bool placeholderVisible = _oldtext.isEmpty();
	if (placeholderVisible != _placeholderVisible) {
		_placeholderVisible = placeholderVisible;

		a_placeholderLeft.start(_placeholderVisible ? 0 : _st->placeholderShift);
		a_placeholderOpacity.start(_placeholderVisible ? 1 : 0);
		_placeholderShiftAnim.start();
	}
}

void InputField::correctValue(QKeyEvent *e, const QString &was) {
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
	bool shift = e->modifiers().testFlag(Qt::ShiftModifier);
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
		emit f()->cancelled();
	} else if (e->key() == Qt::Key_Tab || (ctrl && e->key() == Qt::Key_Backtab)) {
		if (ctrl) {
			e->ignore();
		} else {
			emit f()->tabbed();
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
	_placeholder = _st->font->elided(_placeholderFull, width() - _st->textMargins.left() - _st->textMargins.right() - _st->placeholderMargins.left() - _st->placeholderMargins.right() - 1);
	_inner.setGeometry(rect().marginsRemoved(_st->textMargins));
	TWidget::resizeEvent(e);
}
