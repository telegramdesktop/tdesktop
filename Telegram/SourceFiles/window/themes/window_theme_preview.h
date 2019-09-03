/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "window/themes/window_theme.h"

namespace Data {
struct CloudTheme;
} // namespace Data

namespace Window {
namespace Theme {

struct CurrentData {
	int32 backgroundId = 0;
	QImage backgroundImage;
	bool backgroundTiled = false;
};

std::unique_ptr<Preview> PreviewFromFile(
	const QString &filepath,
	const QByteArray &bytes,
	const Data::CloudTheme &cloud);
std::unique_ptr<Preview> GeneratePreview(
	const QString &filepath,
	const QByteArray &bytes,
	const Data::CloudTheme &cloud,
	CurrentData &&data);

int DefaultPreviewTitleHeight();
void DefaultPreviewWindowFramePaint(
	QImage &preview,
	const style::palette &palette,
	QRect body,
	int outerWidth);

} // namespace Theme
} // namespace Window
