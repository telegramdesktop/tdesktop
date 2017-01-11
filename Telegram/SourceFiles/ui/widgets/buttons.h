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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "ui/abstract_button.h"
#include "styles/style_widgets.h"

#include <memory>

namespace Ui {

class RippleAnimation;

class LinkButton : public AbstractButton {
public:
	LinkButton(QWidget *parent, const QString &text, const style::LinkButton &st = st::defaultLinkButton);

	int naturalWidth() const override;

	void setText(const QString &text);

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(State was, StateChangeSource source) override;

private:
	QString _text;
	int _textWidth = 0;
	const style::LinkButton &_st;

};

class RippleButton : public AbstractButton {
public:
	RippleButton(QWidget *parent, const style::RippleAnimation &st);

	// Displays full ripple circle constantly.
	enum class SetForceRippledWay {
		Default,
		SkipAnimation,
	};
	void setForceRippled(bool rippled, SetForceRippledWay way = SetForceRippledWay::Default);
	bool forceRippled() const {
		return _forceRippled;
	}

	~RippleButton();

protected:
	void paintRipple(QPainter &p, int x, int y, TimeMs ms, const QColor *colorOverride = nullptr);

	void onStateChanged(State was, StateChangeSource source) override;

	virtual QImage prepareRippleMask() const;
	virtual QPoint prepareRippleStartPosition() const;
	QPoint disabledRippleStartPosition() const {
		return QPoint(-0x3FFFFFFF, -0x3FFFFFFF);
	}
	void resetRipples();

private:
	void ensureRipple();
	void handleRipples(bool wasDown, bool wasPress);

	const style::RippleAnimation &_st;
	std_::unique_ptr<RippleAnimation> _ripple;
	bool _forceRippled = false;

};

class FlatButton : public RippleButton {
public:
	FlatButton(QWidget *parent, const QString &text, const style::FlatButton &st);

	void setText(const QString &text);
	void setWidth(int32 w);

	int32 textWidth() const;

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(State was, StateChangeSource source) override;

private:
	QString _text, _textForAutoSize;
	int _width;

	const style::FlatButton &_st;

};

class RoundButton : public RippleButton {
public:
	RoundButton(QWidget *parent, const QString &text, const style::RoundButton &st);

	void setText(const QString &text);

	void setNumbersText(const QString &numbersText) {
		setNumbersText(numbersText, numbersText.toInt());
	}
	void setNumbersText(int numbers) {
		setNumbersText(QString::number(numbers), numbers);
	}
	void setWidthChangedCallback(base::lambda<void()> &&callback);
	void stepNumbersAnimation(TimeMs ms);
	void finishNumbersAnimation();

	int contentWidth() const;

	void setFullWidth(int newFullWidth);

	enum class TextTransform {
		NoTransform,
		ToUpper,
	};
	void setTextTransform(TextTransform transform);

	~RoundButton();

protected:
	void paintEvent(QPaintEvent *e) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	void setNumbersText(const QString &numbersText, int numbers);
	void numbersAnimationCallback();
	void updateText();
	void resizeToText();

	QString _text, _fullText;
	int _textWidth;

	class Numbers;
	std_::unique_ptr<Numbers> _numbers;

	int _fullWidthOverride = 0;

	const style::RoundButton &_st;

	TextTransform _transform = TextTransform::ToUpper;

};

class IconButton : public RippleButton {
public:
	IconButton(QWidget *parent, const style::IconButton &st);

	// Pass nullptr to restore the default icon.
	void setIconOverride(const style::icon *iconOverride, const style::icon *iconOverOverride = nullptr);
	void setRippleColorOverride(const style::color *colorOverride);

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(State was, StateChangeSource source) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	const style::IconButton &_st;
	const style::icon *_iconOverride = nullptr;
	const style::icon *_iconOverrideOver = nullptr;
	const style::color *_rippleColorOverride = nullptr;

	Animation _a_over;

};

class LeftOutlineButton : public RippleButton {
public:
	LeftOutlineButton(QWidget *parent, const QString &text, const style::OutlineButton &st = st::defaultLeftOutlineButton);

	void setText(const QString &text);

protected:
	void paintEvent(QPaintEvent *e) override;

	int resizeGetHeight(int newWidth) override;

private:
	QString _text, _fullText;
	int _textWidth, _fullTextWidth;

	const style::OutlineButton &_st;

};

class CrossButton : public RippleButton {
public:
	CrossButton(QWidget *parent, const style::CrossButton &st);

	void showAnimated();
	void hideAnimated();
	void hideFast();

	bool isShown() const {
		return _shown;
	}

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(State was, StateChangeSource source) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	void startAnimation(bool shown);
	void animationCallback();

	const style::CrossButton &_st;

	bool _shown = false;
	Animation _a_show;

};

} // namespace Ui
