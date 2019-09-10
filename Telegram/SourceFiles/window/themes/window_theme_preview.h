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
	WallPaperId backgroundId = 0;
	QImage backgroundImage;
	bool backgroundTiled = false;
};

enum class PreviewType {
	Normal,
	Extended,
};

[[nodiscard]] QString CachedThemePath(uint64 documentId);

std::unique_ptr<Preview> PreviewFromFile(
	const QByteArray &bytes,
	const QString &filepath,
	const Data::CloudTheme &cloud);
std::unique_ptr<Preview> GeneratePreview(
	const QByteArray &bytes,
	const QString &filepath,
	const Data::CloudTheme &cloud,
	CurrentData &&data,
	PreviewType type);
QImage GeneratePreview(
	const QByteArray &bytes,
	const QString &filepath);

int DefaultPreviewTitleHeight();
void DefaultPreviewWindowFramePaint(
	QImage &preview,
	const style::palette &palette,
	QRect body,
	int outerWidth);

} // namespace Theme
} // namespace Window
