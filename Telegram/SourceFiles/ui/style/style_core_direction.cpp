/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/style/style_core_direction.h"

namespace style {
namespace {

bool RightToLeftValue = false;

} // namespace

bool RightToLeft() {
	return RightToLeftValue;
}

void SetRightToLeft(bool rtl) {
	RightToLeftValue = rtl;
}

} // namespace style
