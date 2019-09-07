/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/toast/toast.h"
#include "ui/twidget.h"
#include "ui/text/text.h"

namespace Ui {
namespace Toast {
namespace internal {

class Widget : public TWidget {
public:
	Widget(QWidget *parent, const Config &config);

	// shownLevel=1 completely visible, shownLevel=0 completely invisible
	void setShownLevel(float64 shownLevel);

	void onParentResized();

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	inline int widthWithoutPadding(int w) {
		return w - _padding.left() - _padding.right();
	}

	float64 _shownLevel = 0;
	bool _multiline = false;
	int _maxWidth = 0;
	QMargins _padding;

	int _maxTextWidth = 0;
	int _maxTextHeight = 0;
	int _textWidth = 0;
	Text::String _text;

};

} // namespace internal
} // namespace Toast
} // namespace Ui
