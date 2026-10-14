/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "renderer-litehtml/src/display_list.h"

namespace Tgw {

class QtBackend;

[[nodiscard]] QImage PaintFrame(
	const twlite::DisplayList &list,
	const QtBackend &backend,
	QSize deviceSize,
	float64 cssToDevice);

} // namespace Tgw
