/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "base/runtime_composer.h"
#include "ui/click_handler.h"

class PaintContextBase {
public:
	PaintContextBase(crl::time ms, bool selecting)
	: ms(ms)
	, selecting(selecting) {
	}
	crl::time ms = 0;
	bool selecting = false;

};

class AbstractLayoutItem
	: public RuntimeComposer<AbstractLayoutItem>
	, public ClickHandlerHost {
public:
	AbstractLayoutItem();

	AbstractLayoutItem(const AbstractLayoutItem &other) = delete;
	AbstractLayoutItem &operator=(
		const AbstractLayoutItem &other) = delete;

	[[nodiscard]] int maxWidth() const;
	[[nodiscard]] int minHeight() const;
	virtual int resizeGetHeight(int width);

	[[nodiscard]] int width() const;
	[[nodiscard]] int height() const;

	virtual void setPosition(int position);
	[[nodiscard]] int position() const;

	[[nodiscard]] bool hasPoint(QPoint point) const;

	virtual ~AbstractLayoutItem();

protected:
	int _width = 0;
	int _height = 0;
	int _maxw = 0;
	int _minh = 0;
	int _position = 0; // < 0 means removed from layout

};
