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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "styles/style_widgets.h"

namespace Ui {

class RoundImageCheckbox {
public:
	using PaintRoundImage = base::lambda_unique<void(Painter &p, int x, int y, int outerWidth, int size)>;
	RoundImageCheckbox(const style::RoundImageCheckbox &st, base::lambda_wrap<void()> updateCallback, PaintRoundImage paintRoundImage);

	void paint(Painter &p, int x, int y, int outerWidth);
	float64 checkedAnimationRatio() const;

	bool checked() const {
		return _checked;
	}
	enum class SetStyle {
		Animated,
		Fast,
	};
	void setChecked(bool checked, SetStyle speed = SetStyle::Animated);

private:
	struct Icon {
		FloatAnimation fadeIn;
		FloatAnimation fadeOut;
		QPixmap wideCheckCache;
	};
	void removeFadeOutedIcons();
	void prepareWideCache();
	void prepareWideCheckIconCache(Icon *icon);

	const style::RoundImageCheckbox &_st;
	base::lambda_wrap<void()> _updateCallback;
	PaintRoundImage _paintRoundImage;

	bool _checked = false;
	QPixmap _wideCache;
	FloatAnimation _selection;
	std_::vector_of_moveable<Icon> _icons;

	// Those pixmaps are shared among all checkboxes that have the same style.
	QPixmap _wideCheckBgCache, _wideCheckFullCache;

};

} // namespace Ui
