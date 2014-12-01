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

PhoneInput::PhoneInput(QWidget *parent, const style::flatInput &st, const QString &ph) : FlatInput(parent, st, ph) {
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
	bool strict = (digitCount == MaxPhoneTailLength);

	newText.reserve(oldLen);
	for (int i = 0; i < oldLen; ++i) {
		QChar ch(oldText[i]);
		if (ch.isDigit()) {
			if (!digitCount--) {
				break;
			}
			newText += ch;
			if (strict && !digitCount) {
				break;
			}
		} else if (ch == ' ' || ch == '-' || ch == '(' || ch == ')') {
			newText += ch;
		}
		if (i == oldPos) {
			newPos = newText.length();
		}
	}
	if (newPos < 0) {
		newPos = newText.length();
	}
	if (newText != oldText) {
		setText(newText);
		if (newPos != oldPos) {
			setCursorPosition(newPos);
		}
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
