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

#include "ui/rp_widget.h"

namespace style {
struct FlatLabel;
} // namespace style

namespace Ui {

class NumbersAnimation {
public:
	NumbersAnimation(
		const style::font &font,
		base::lambda<void()> animationCallback);

	void setWidthChangedCallback(base::lambda<void()> callback) {
		_widthChangedCallback = std::move(callback);
	}
	void setText(const QString &text, int value);
	void stepAnimation(TimeMs ms);
	void finishAnimating();

	void paint(Painter &p, int x, int y, int outerWidth);
	int countWidth() const;
	int maxWidth() const;

private:
	struct Digit {
		QChar from = 0;
		QChar to = 0;
		int fromWidth = 0;
		int toWidth = 0;
	};

	void animationCallback();
	void realSetText(QString text, int value);

	const style::font &_font;

	QList<Digit> _digits;
	int _digitWidth = 0;

	int _fromWidth = 0;
	int _toWidth = 0;

	Animation _a_ready;
	QString _delayedText;
	int _delayedValue = 0;

	int _value = 0;
	bool _growing = false;

	base::lambda<void()> _animationCallback;
	base::lambda<void()> _widthChangedCallback;

};

struct StringWithNumbers {
	QString text;
	int offset = -1;
	int length = 0;
};

class LabelWithNumbers : public Ui::RpWidget {
public:
	LabelWithNumbers(
		QWidget *parent,
		const style::FlatLabel &st,
		int textTop,
		const StringWithNumbers &value);

	void setValue(const StringWithNumbers &value);
	void finishAnimating();

	int naturalWidth() const override {
		return _beforeWidth + _numbers.maxWidth() + _afterWidth;
	}

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	static QString GetBefore(const StringWithNumbers &value);
	static QString GetAfter(const StringWithNumbers &value);
	static QString GetNumbers(const StringWithNumbers &value);

	const style::FlatLabel &_st;
	int _textTop;
	QString _before;
	QString _after;
	NumbersAnimation _numbers;
	int _beforeWidth = 0;
	int _afterWidth = 0;
	Animation _beforeWidthAnimation;

};

} // namespace Ui

namespace Lang {

template <typename ResultString>
struct StartReplacements;

template <>
struct StartReplacements<Ui::StringWithNumbers> {
	static inline Ui::StringWithNumbers Call(QString &&langString) {
		return { std::move(langString) };
	}
};

template <typename ResultString>
struct ReplaceTag;

template <>
struct ReplaceTag<Ui::StringWithNumbers> {
	static Ui::StringWithNumbers Call(
		Ui::StringWithNumbers &&original,
		ushort tag,
		const Ui::StringWithNumbers &replacement);
};

} // namespace Lang
