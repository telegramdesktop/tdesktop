/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "styles/style_widgets.h"

namespace Ui {

class RoundCheckbox {
public:
	RoundCheckbox(const style::RoundCheckbox &st, base::lambda<void()> updateCallback);

	void paint(Painter &p, TimeMs ms, int x, int y, int outerWidth, float64 masterScale = 1.);

	void setDisplayInactive(bool displayInactive);
	bool checked() const {
		return _checked;
	}
	enum class SetStyle {
		Animated,
		Fast,
	};
	void setChecked(bool newChecked, SetStyle speed = SetStyle::Animated);

	void invalidateCache();

private:
	void prepareInactiveCache();

	const style::RoundCheckbox &_st;
	base::lambda<void()> _updateCallback;

	bool _checked = false;
	Animation _checkedProgress;

	bool _displayInactive = false;
	QPixmap _inactiveCacheBg, _inactiveCacheFg;

};

class RoundImageCheckbox {
public:
	using PaintRoundImage = base::lambda<void(Painter &p, int x, int y, int outerWidth, int size)>;
	RoundImageCheckbox(const style::RoundImageCheckbox &st, base::lambda<void()> updateCallback, PaintRoundImage &&paintRoundImage);

	void paint(Painter &p, TimeMs ms, int x, int y, int outerWidth);
	float64 checkedAnimationRatio() const;

	bool checked() const {
		return _check.checked();
	}
	using SetStyle = RoundCheckbox::SetStyle;
	void setChecked(bool newChecked, SetStyle speed = SetStyle::Animated);

	void invalidateCache() {
		_check.invalidateCache();
	}

private:
	void prepareWideCache();

	const style::RoundImageCheckbox &_st;
	base::lambda<void()> _updateCallback;
	PaintRoundImage _paintRoundImage;

	QPixmap _wideCache;
	Animation _selection;

	RoundCheckbox _check;

};

} // namespace Ui
