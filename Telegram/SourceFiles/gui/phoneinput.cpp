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

#include "gui/phoneinput.h"
#include "numbers.h"
#include "lang.h"

PhoneInput::PhoneInput(QWidget *parent, const style::flatInput &st) : FlatInput(parent, st, lang(lng_phone_ph)) {
}

void PhoneInput::paintEvent(QPaintEvent *e) {
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

void PhoneInput::correctValue(QKeyEvent *e, const QString &was) {
	if (e && e->key() == Qt::Key_Backspace && !was.length()) {
		emit voidBackspace(e);
		return;
	}
	QString oldText(text()), newText;
	int oldPos(cursorPosition()), newPos(-1), oldLen(oldText.length()), digitCount = 0;
	for (int i = 0; i < oldLen; ++i) {
		if (oldText[i].isDigit()) {
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

		QChar ch(oldText[i]);
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
	if (newText != oldText) {
		setText(newText);
		setCursorPosition(newPos);
	}
}

void PhoneInput::addedToNumber(const QString &added) {
	setFocus();
	QString was(text());
	setText(added + text());
	setCursorPosition(added.length());
	correctValue(0, was);
	updatePlaceholder();
}

void PhoneInput::onChooseCode(const QString &code) {
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
			ph.append(QString(QChar(0x2212)).repeated(pattern.at(i)));
		}
		setPlaceholder(ph);
	}
	correctValue(0, text());
	setPlaceholderFast(!pattern.isEmpty());
	updatePlaceholder();
}

PortInput::PortInput(QWidget *parent, const style::flatInput &st, const QString &ph, const QString &val) : FlatInput(parent, st, ph, val) {
	correctValue(0, QString());
}

void PortInput::correctValue(QKeyEvent *e, const QString &was) {
	QString oldText(text()), newText(oldText);

	newText.replace(QRegularExpression(qsl("[^\\d]")), QString());
	if (!newText.toInt()) {
		newText = QString();
	} else if (newText.toInt() > 65535) {
		newText = was;
	}
	if (newText != oldText) {
		setText(newText);
		updatePlaceholder();
	}
}
