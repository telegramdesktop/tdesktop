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
#pragma once

#include "styles/style_widgets.h"

namespace Ui {

class LabelSimple : public TWidget {
public:
	LabelSimple(QWidget *parent, const style::LabelSimple &st = st::defaultLabelSimple, const QString &value = QString());

	// This method also resizes the label.
	void setText(const QString &newText, bool *outTextChanged = nullptr);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	QString _fullText;
	int _fullTextWidth;

	QString _text;
	int _textWidth;

	const style::LabelSimple &_st;

};

} // namespace Ui
