/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"

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
	void finishAnimating();

	void paint(QPainter &p, int x, int y, int outerWidth);
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

	Ui::Animations::Simple _a_ready;
	QString _delayedText;
	int _delayedValue = 0;

	int _value = 0;
	bool _growing = false;

	Fn<void()> _animationCallback;
	Fn<void()> _widthChangedCallback;

};

struct StringWithNumbers {
	static StringWithNumbers FromString(const QString &text) {
		return { text };
	}

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
	Ui::Animations::Simple _beforeWidthAnimation;

};

} // namespace Ui
