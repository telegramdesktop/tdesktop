/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/style/style_core_scale.h"

#include "base/assertion.h"

namespace style {
namespace {

int DevicePixelRatioValue = 1;
int ScaleValue = kScaleDefault;

} // namespace

int DevicePixelRatio() {
	return DevicePixelRatioValue;
}

void SetDevicePixelRatio(int ratio) {
	DevicePixelRatioValue = ratio;
}

int Scale() {
	return ScaleValue;
}

void SetScale(int scale) {
	Expects(scale != 0);

	ScaleValue = scale;
}

} // namespace style
