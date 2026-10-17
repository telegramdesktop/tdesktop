/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_clipboard.h"

#include "base/random.h"
#include "base/weak_ptr.h"
#include "main/main_session.h"

#include <QtCore/QMimeData>

namespace Iv::Editor {
namespace {

struct ClipboardStorage {
	uint64 sessionId = base::RandomValue<uint64>();
	uint64 serial = 0;
	base::weak_ptr<Main::Session> mediaSession;
	bool hasMedia = false;
	std::optional<ClipboardData> data;
};

[[nodiscard]] Main::Session *PayloadMediaSession(
		const ClipboardBlockData &payload) {
	return RichBlocksMediaSession(payload.blocks);
}

[[nodiscard]] Main::Session *PayloadMediaSession(
		const ClipboardListItemsData &payload) {
	return RichListItemsMediaSession(payload.items);
}

[[nodiscard]] Main::Session *MediaSession(const ClipboardData &data) {
	return std::visit([](const auto &payload) {
		return PayloadMediaSession(payload);
	}, data);
}

[[nodiscard]] ClipboardStorage &Storage() {
	static auto storage = ClipboardStorage();
	return storage;
}

[[nodiscard]] ClipboardOrigin StoredOrigin(const ClipboardData &data) {
	return std::visit([](const auto &payload) {
		return payload.origin;
	}, data);
}

[[nodiscard]] QByteArray MarkerPayload(const ClipboardOrigin &origin) {
	return QByteArray::number(qulonglong(origin.sessionId))
		+ ':'
		+ QByteArray::number(qulonglong(origin.serial));
}

[[nodiscard]] bool OriginMatches(
		const ClipboardOrigin &origin,
		const ClipboardStorage &storage) {
	return (origin.sessionId == storage.sessionId)
		&& (origin.serial == storage.serial)
		&& (origin.serial != 0);
}

[[nodiscard]] bool StoredDataMatches(
		const ClipboardData &data,
		const ClipboardStorage &storage) {
	return OriginMatches(StoredOrigin(data), storage);
}

[[nodiscard]] bool MarkerMatches(
		const QMimeData *mimeData,
		const ClipboardData &data) {
	return mimeData
		&& mimeData->hasFormat(ClipboardMimeType())
		&& (mimeData->data(ClipboardMimeType())
			== MarkerPayload(StoredOrigin(data)));
}

[[nodiscard]] ClipboardData StampClipboardData(ClipboardData data) {
	auto &storage = Storage();
	const auto serial = ++storage.serial;
	return std::visit([&](auto payload) -> ClipboardData {
		payload.origin.sessionId = storage.sessionId;
		payload.origin.serial = serial;
		return ClipboardData(std::move(payload));
	}, std::move(data));
}

} // namespace

QString ClipboardMimeType() {
	return u"application/x-td-iv-editor"_q;
}

std::unique_ptr<QMimeData> MimeDataFromClipboardData(ClipboardData data) {
	auto &storage = Storage();
	const auto media = MediaSession(data);
	storage.mediaSession = media;
	storage.hasMedia = (media != nullptr);
	storage.data = StampClipboardData(std::move(data));
	auto result = std::make_unique<QMimeData>();
	result->setData(
		ClipboardMimeType(),
		MarkerPayload(StoredOrigin(*storage.data)));
	return result;
}

std::optional<ClipboardData> ClipboardDataFromMimeData(
		const QMimeData *mimeData,
		not_null<Main::Session*> session) {
	auto &storage = Storage();
	if (storage.hasMedia && !storage.mediaSession.get()) {
		storage.data = std::nullopt;
		storage.hasMedia = false;
	}
	if (!storage.data
		|| (storage.hasMedia && storage.mediaSession.get() != session)
		|| !StoredDataMatches(*storage.data, storage)
		|| !MarkerMatches(mimeData, *storage.data)) {
		return std::nullopt;
	}
	return storage.data;
}

} // namespace Iv::Editor
