/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
		Fn<void()> animationCallback);

	void setWidthChangedCallback(Fn<void()> callback) {
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

	Fn<void()> _animationCallback;
	Fn<void()> _widthChangedCallback;

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
