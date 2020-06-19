/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Window {

enum class HitTestResult {
	None = 0,
	Client,
	SysButton,
	Caption,
	Top,
	TopRight,
	Right,
	BottomRight,
	Bottom,
	BottomLeft,
	Left,
	TopLeft,
};

class TitleWidget : public Ui::RpWidget {
public:
	using RpWidget::RpWidget;

	virtual void init() {
	}
	virtual HitTestResult hitTest(const QPoint &p) const {
		return HitTestResult::None;
	}
	virtual QRect iconRect() const {
		return QRect();
	}

};

} // namespace Window
