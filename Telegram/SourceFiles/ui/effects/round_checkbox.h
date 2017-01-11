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

#include "styles/style_widgets.h"

namespace Ui {

class RoundCheckbox {
public:
	RoundCheckbox(const style::RoundCheckbox &st, const base::lambda_copy<void()> &updateCallback);

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
	struct Icon {
		Animation fadeIn;
		Animation fadeOut;
		QPixmap wideCheckCache;
	};
	void removeFadeOutedIcons();
	void prepareWideCheckIconCache(Icon *icon);
	void prepareInactiveCache();
	QRect cacheDestRect(int x, int y, float64 scale) const;

	const style::RoundCheckbox &_st;
	base::lambda_copy<void()> _updateCallback;

	bool _checked = false;
	std_::vector_of_moveable<Icon> _icons;

	bool _displayInactive = false;
	QPixmap _inactiveCacheBg, _inactiveCacheFg;

	// Those pixmaps are shared among all checkboxes that have the same style.
	QPixmap _wideCheckBgCache, _wideCheckFullCache;

};

class RoundImageCheckbox {
public:
	using PaintRoundImage = base::lambda<void(Painter &p, int x, int y, int outerWidth, int size)>;
	RoundImageCheckbox(const style::RoundImageCheckbox &st, const base::lambda_copy<void()> &updateCallback, PaintRoundImage &&paintRoundImage);

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
	base::lambda_copy<void()> _updateCallback;
	PaintRoundImage _paintRoundImage;

	QPixmap _wideCache;
	Animation _selection;

	RoundCheckbox _check;

};

} // namespace Ui
