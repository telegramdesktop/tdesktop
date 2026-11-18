/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/glass/glass_policy.h"

#include <QtGui/QColor>

#include <crl/crl_time.h>

namespace Glass {

enum class Easing {
	Standard = 0,
	Emphasized = 1,
};

struct SurfaceMaterial {
	QColor bg;
	QColor stroke;
	QColor highlight;
	QColor shadow;
	int strokeWidth = 0;
	int highlightWidth = 0;
	int shadowSpread = 0;
	int shadowOffsetY = 0;
};

struct RadiusTokens {
	int small = 0;
	int medium = 0;
	int large = 0;
};

struct SpacingTokens {
	int tiny = 0;
	int small = 0;
	int medium = 0;
	int large = 0;
};

struct MotionTokens {
	crl::time fast = 0;
	crl::time normal = 0;
	crl::time slow = 0;
	Easing easing = Easing::Standard;
};

[[nodiscard]] SurfaceMaterial Material(Weight weight);
[[nodiscard]] SurfaceMaterial CurrentMaterial();
[[nodiscard]] RadiusTokens Radii();
[[nodiscard]] SpacingTokens Spacing();
[[nodiscard]] MotionTokens Motion();

} // namespace Glass
