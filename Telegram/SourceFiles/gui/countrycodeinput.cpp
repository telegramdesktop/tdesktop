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

#include "gui/countrycodeinput.h"
#include "gui/countryinput.h"

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
