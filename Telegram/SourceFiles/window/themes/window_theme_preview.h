/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "window/themes/window_theme.h"

namespace Window {
namespace Theme {

struct CurrentData {
	int32 backgroundId = 0;
	QImage backgroundImage;
	bool backgroundTiled = false;
};

std::unique_ptr<Preview> GeneratePreview(
	const QString &filepath,
	CurrentData &&data);

int DefaultPreviewTitleHeight();
void DefaultPreviewWindowFramePaint(
	QImage &preview,
	const style::palette &palette,
	QRect body,
	int outerWidth);

} // namespace Theme
} // namespace Window
