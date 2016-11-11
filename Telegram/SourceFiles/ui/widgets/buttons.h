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

#include "ui/abstract_button.h"
#include "styles/style_widgets.h"

namespace Ui {

class FlatButton : public AbstractButton {
public:
	FlatButton(QWidget *parent, const QString &text, const style::FlatButton &st);

	void step_appearance(float64 ms, bool timer);
	void setOpacity(float64 o);
	float64 opacity() const;

	void setText(const QString &text);
	void setWidth(int32 w);

	int32 textWidth() const;

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(int oldState, StateChangeSource source) override;

private:
	QString _text, _textForAutoSize;
	int _width;

	const style::FlatButton &_st;

	anim::fvalue a_over;
	Animation _a_appearance;

	float64 _opacity = 1.;

};

class LinkButton : public AbstractButton {
public:
	LinkButton(QWidget *parent, const QString &text, const style::LinkButton &st = st::defaultLinkButton);

	int naturalWidth() const override;

	void setText(const QString &text);

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(int oldState, StateChangeSource source) override;

private:
	QString _text;
	int _textWidth = 0;
	const style::LinkButton &_st;

};

class RoundButton : public AbstractButton {
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

	void onStateChanged(int oldState, StateChangeSource source) override;

private:
	void updateText();
	void resizeToText();

	QString _text, _fullText;
	int _textWidth;

	QString _secondaryText, _fullSecondaryText;
	int _secondaryTextWidth = 0;

	int _fullWidthOverride = 0;

	const style::RoundButton &_st;

	TextTransform _transform = TextTransform::ToUpper;

};

class IconButton : public AbstractButton {
public:
	IconButton(QWidget *parent, const style::IconButton &st);

	// Pass nullptr to restore the default icon.
	void setIcon(const style::icon *icon, const style::icon *iconOver = nullptr);

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(int oldState, StateChangeSource source) override;

private:
	const style::IconButton &_st;
	const style::icon *_iconOverride = nullptr;
	const style::icon *_iconOverrideOver = nullptr;

	FloatAnimation _a_over;

};

} // namespace Ui
