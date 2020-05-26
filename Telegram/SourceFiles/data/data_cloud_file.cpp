/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_cloud_file.h"

#include "data/data_file_origin.h"
#include "storage/cache/storage_cache_database.h"
#include "storage/file_download.h"

namespace Data {

void UpdateCloudFile(
		CloudFile &file,
		const ImageWithLocation &data,
		Storage::Cache::Database &cache,
		Fn<void(FileOrigin)> restartLoader,
		Fn<void(QImage)> usePreloaded) {
	if (!data.location.valid()) {
		return;
	}

	const auto update = !file.location.valid()
		|| (data.location.file().cacheKey()
			&& (!file.location.file().cacheKey()
				|| (file.location.width() < data.location.width())
				|| (file.location.height() < data.location.height())));
	if (!update) {
		return;
	}
	auto cacheBytes = !data.bytes.isEmpty()
		? data.bytes
		: file.location.file().data.is<InMemoryLocation>()
		? file.location.file().data.get_unchecked<InMemoryLocation>().bytes
		: QByteArray();
	if (!cacheBytes.isEmpty()) {
		if (const auto cacheKey = data.location.file().cacheKey()) {
			cache.putIfEmpty(
				cacheKey,
				Storage::Cache::Database::TaggedValue(
					std::move(cacheBytes),
					Data::kImageCacheTag));
		}
	}
	file.location = data.location;
	file.byteSize = data.bytesCount;
	if (!data.preloaded.isNull()) {
		file.loader = nullptr;
		if (usePreloaded) {
			usePreloaded(data.preloaded);
		}
	} else if (file.loader) {
		const auto origin = base::take(file.loader)->fileOrigin();
		restartLoader(origin);
	}
}

} // namespace Data