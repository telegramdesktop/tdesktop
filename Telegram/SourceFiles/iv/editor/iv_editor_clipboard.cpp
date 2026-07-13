/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_clipboard.h"

#include "base/random.h"

#include <QtCore/QMimeData>
#include <QtCore/QPointer>

namespace Iv::Editor {
namespace {

struct ClipboardStorage {
	uint64 sessionId = base::RandomValue<uint64>();
	uint64 serial = 0;
	QPointer<QMimeData> mimeData;
	std::optional<ClipboardData> data;
};

[[nodiscard]] ClipboardStorage &Storage() {
	static auto storage = ClipboardStorage();
	return storage;
}

[[nodiscard]] bool MarkerMatches(const QMimeData *mimeData) {
	return mimeData
		&& mimeData->hasFormat(ClipboardMimeType())
		&& (mimeData->data(ClipboardMimeType()) == "1");
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
	return std::visit([&](const auto &payload) {
		return OriginMatches(payload.origin, storage);
	}, data);
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
	storage.data = StampClipboardData(std::move(data));
	auto result = std::make_unique<QMimeData>();
	result->setData(ClipboardMimeType(), "1");
	storage.mimeData = result.get();
	return result;
}

std::optional<ClipboardData> ClipboardDataFromMimeData(
		const QMimeData *mimeData) {
	const auto &storage = Storage();
	if (!MarkerMatches(mimeData)
		|| !storage.data
		|| !storage.mimeData
		|| (storage.mimeData.data() != mimeData)
		|| !StoredDataMatches(*storage.data, storage)) {
		return std::nullopt;
	}
	return storage.data;
}

} // namespace Iv::Editor
