/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text.h"

class Painter;

namespace Ui {

class RippleAnimation;
struct ChatPaintContext;

class UnsupportedNoticeCard final {
public:
	void setTexts(
		const QString &title,
		const QString &text,
		const QString &button);

	int resizeGetHeight(int availableWidth);

	void paint(
		Painter &p,
		const ChatPaintContext &context,
		QRect cardRect,
		RippleAnimation *ripple) const;

	[[nodiscard]] int width() const;
	[[nodiscard]] int height() const;
	[[nodiscard]] QRect buttonRect() const;
	[[nodiscard]] QSize buttonSize() const;

private:
	Text::String _title;
	Text::String _text;
	Text::String _button;
	QSize _buttonSize;
	int _width = 0;
	int _height = 0;
	int _columnWidth = 0;
	int _textLines = 1;

};

} // namespace Ui
