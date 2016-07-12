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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "ui/widgets/label_simple.h"

namespace Ui {

LabelSimple::LabelSimple(QWidget *parent, const style::LabelSimple &st, const QString &value) : TWidget(parent)
, _st(st) {
	setText(value);
}

void LabelSimple::setText(const QString &value, bool *outTextChanged) {
	if (_fullText == value) {
		if (outTextChanged) *outTextChanged = false;
		return;
	}

	_fullText = value;
	_fullTextWidth = _st.font->width(_fullText);
	if (!_st.maxWidth || _fullTextWidth <= _st.maxWidth) {
		_text = _fullText;
		_textWidth = _fullTextWidth;
	} else {
		auto newText = _st.font->elided(_fullText, _st.maxWidth);
		if (newText == _text) {
			if (outTextChanged) *outTextChanged = false;
			return;
		}
		_text = newText;
		_textWidth = _st.font->width(_text);
	}
	resize(_textWidth, _st.font->height);
	update();
	if (outTextChanged) *outTextChanged = true;
}

void LabelSimple::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.setFont(_st.font);
	p.setPen(_st.textFg);
	p.drawTextLeft(0, 0, width(), _text, _textWidth);
}

} // namespace Ui
