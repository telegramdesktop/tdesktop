/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/binary_guard.h"

#include <gsl/util>

#include <optional>

class DocumentData;
class HistoryItem;

namespace Core {
class Settings;
enum class NameType : uchar;
} // namespace Core

namespace Window {
class SessionController;
} // namespace Window

namespace Data {

class DocumentMedia;

enum class ImageOpenSource : uchar {
	None,
	Location,
	Bytes,
};

struct ImageOpenCheck {
	QString mime;
	ImageOpenSource source = ImageOpenSource::None;
	bool sizeOverLimit = false;
	bool readable = false;
	bool openInApp = false;
	std::optional<gsl::final_action<Fn<void()>>> accessGuard;
};

[[nodiscard]] ImageOpenCheck CheckImageOpenInApp(
	not_null<DocumentData*> document,
	const std::shared_ptr<DocumentMedia> &media);

[[nodiscard]] bool LauncherWouldWarn(
	const Core::Settings &settings,
	Core::NameType nameType,
	bool isIpReveal,
	const QString &extension,
	HistoryItem *item);

base::binary_guard ReadBackgroundImageAsync(
	not_null<Data::DocumentMedia*> media,
	FnMut<QImage(QImage)> postprocess,
	FnMut<void(QImage&&)> done);

void ResolveDocument(
	Window::SessionController *controller,
	not_null<DocumentData*> document,
	HistoryItem *item,
	MsgId topicRootId,
	PeerId monoforumPeerId,
	bool showDrawButton);

} // namespace Data
