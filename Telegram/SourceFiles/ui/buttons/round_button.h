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

#include "ui/button.h"

namespace Ui {

class RoundButton : public Button {
public:
	RoundButton(QWidget *parent, const QString &text, const style::RoundButton &st);

	void setText(const QString &text);
	void setSecondaryText(const QString &secondaryText);

	int contentWidth() const;

	void setFullWidth(int newFullWidth);

	enum class TextTransform {
		NoTransform,
		ToUpper,
	};
	void setTextTransform(TextTransform transform);

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(int oldState, ButtonStateChangeSource source) override;

private:
	void step_over(float64 ms, bool timer);

	void updateText();
	void resizeToText();

	QString _text, _fullText;
	int _textWidth;

	QString _secondaryText, _fullSecondaryText;
	int _secondaryTextWidth = 0;

	int _fullWidthOverride = 0;

	const style::RoundButton &_st;

	anim::fvalue a_textBgOverOpacity;
	anim::cvalue a_textFg, a_secondaryTextFg;
	Animation _a_over;

	TextTransform _transform = TextTransform::NoTransform;

};

} // namespace Ui
