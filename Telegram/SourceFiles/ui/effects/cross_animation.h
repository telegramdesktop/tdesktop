/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "styles/style_widgets.h"

namespace Ui {

class CrossAnimation {
public:
	static void paint(
		Painter &p,
		const style::CrossAnimation &st,
		style::color color,
		int x,
		int y,
		int outerWidth,
		float64 shown,
		float64 loading = 0.);
	static void paintStaticLoading(
		Painter &p,
		const style::CrossAnimation &st,
		style::color color,
		int x,
		int y,
		int outerWidth,
		float64 shown);

};

} // namespace Ui
