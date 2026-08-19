/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui::Premium {

[[nodiscard]] float64 CubicBezier(
	float64 x1,
	float64 y1,
	float64 x2,
	float64 y2,
	float64 x);

[[nodiscard]] bool Object3dSupported();

} // namespace Ui::Premium
