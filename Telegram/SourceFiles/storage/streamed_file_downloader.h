/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "storage/file_download.h"
#include "storage/cache/storage_cache_types.h"

namespace Media {
namespace Streaming {
class Reader;
struct LoadedPart;
} // namespace Streaming
} // namespace Media

namespace Storage {

class StreamedFileDownloader final : public FileLoader {
public:
	StreamedFileDownloader(
		uint64 objectId,
		MTP::DcId dcId,
		Data::FileOrigin origin,
		std::optional<Cache::Key> cacheKey,
		std::shared_ptr<Media::Streaming::Reader> reader,

		// For FileLoader
		const QString &toFile,
		int32 size,
		LocationType locationType,
		LoadToCacheSetting toCache,
		LoadFromCloudSetting fromCloud,
		bool autoLoading,
		uint8 cacheTag);
	~StreamedFileDownloader();

	uint64 objId() const override;
	Data::FileOrigin fileOrigin() const override;
	void stop() override;

private:
	std::optional<Storage::Cache::Key> cacheKey() const override;
	std::optional<MediaKey> fileLocationKey() const override;
	void cancelRequests() override;
	bool loadPart() override;

	void savePart(const Media::Streaming::LoadedPart &part);

	uint64 _objectId = 0;
	Data::FileOrigin _origin;
	std::optional<Cache::Key> _cacheKey;
	std::shared_ptr<Media::Streaming::Reader> _reader;

	std::vector<bool> _partIsSaved; // vector<bool> :D
	int _nextPartIndex = 0;

	rpl::lifetime _lifetime;

};

} // namespace Storage
