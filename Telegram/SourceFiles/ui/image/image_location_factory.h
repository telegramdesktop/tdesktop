/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/image/image_location.h"

namespace Main {
class Session;
} // namespace Main

namespace Images {

[[nodiscard]] ImageWithLocation FromPhotoSize(
	not_null<Main::Session*> session,
	const MTPDdocument &document,
	const MTPPhotoSize &size);
[[nodiscard]] ImageWithLocation FromImageInMemory(
	const QImage &image,
	const char *format);
[[nodiscard]] ImageLocation FromWebDocument(const MTPWebDocument &document);

} // namespace Images
