/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/binary_guard.h"

class DocumentData;
class HistoryItem;

namespace Window {
class SessionController;
} // namespace Window

namespace Data {

class DocumentMedia;

[[nodiscard]] QString FileExtension(const QString &filepath);
// [[nodiscard]] bool IsValidMediaFile(const QString &filepath);
[[nodiscard]] bool IsExecutableName(const QString &filepath);
[[nodiscard]] bool IsIpRevealingName(const QString &filepath);
base::binary_guard ReadImageAsync(
	not_null<Data::DocumentMedia*> media,
	FnMut<QImage(QImage)> postprocess,
	FnMut<void(QImage&&)> done);

void ResolveDocument(
	Window::SessionController *controller,
	not_null<DocumentData*> document,
	HistoryItem *item);

} // namespace Data
