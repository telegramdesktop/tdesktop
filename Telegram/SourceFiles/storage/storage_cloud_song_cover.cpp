/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/storage_cloud_song_cover.h"

#include "data/data_cloud_file.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "storage/file_download.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>

namespace Storage::CloudSongCover {
namespace {

constexpr auto kMaxResponseSize = 1024 * 1024;
constexpr auto kDefaultCoverSize = 100;

struct Responce {
	const QString artworkUrl;
	const int size;
};

auto Location(const QString &url) {
	return DownloadLocation{ PlainUrlLocation{ url } };
}

auto JsonUrl(not_null<SongData*> song) {
	return QString("https://itunes.apple.com/search?term=" \
		"%1 %2&entity=song&limit=4").arg(song->performer, song->title);
}

// Dummy JSON responce.
// {
//     "resultCount": 2,
//     "results": [
//         {
//             "artworkUrl100": ""
//         },
//         {
//             "artworkUrl100": ""
//         }
//     ]
// }

std::optional<Responce> ParseResponce(const QByteArray &response) {
	if (response.size() >= kMaxResponseSize) {
		return std::nullopt;
	}
	auto error = QJsonParseError{ 0, QJsonParseError::NoError };
	const auto document = QJsonDocument::fromJson(response, &error);

	const auto log = [](const QString &message) {
		DEBUG_LOG(("Parse Artwork JSON Error: %1.").arg(message));
	};

	if (error.error != QJsonParseError::NoError) {
		log(error.errorString());
		return std::nullopt;
	} else if (!document.isObject()) {
		log("not an object received in JSON");
		return std::nullopt;
	}
	const auto results = document.object().value("results");
	if (!results.isArray()) {
		log("'results' field not found");
		return std::nullopt;
	}
	const auto resultsArray = results.toArray();
	if (resultsArray.empty()) {
		return std::nullopt;
	}
	const auto artworkUrl = resultsArray.first().toObject()
		.value("artworkUrl100").toString();
	if (artworkUrl.isEmpty()) {
		log("'artworkUrl100' field is empty");
		return std::nullopt;
	}

	return Responce{ artworkUrl, kDefaultCoverSize };
}

void LoadAndApplyThumbnail(
		not_null<DocumentData*> document,
		const Responce &responce) {
	const auto size = responce.size;
	const auto imageWithLocation = ImageWithLocation{
		.location = ImageLocation(Location(responce.artworkUrl), size, size)
	};

	document->updateThumbnails(
		QByteArray(),
		imageWithLocation,
		ImageWithLocation{ .location = ImageLocation() });

	document->loadThumbnail(Data::FileOrigin());
}

}

void LoadThumbnailFromExternal(not_null<DocumentData*> document) {
	const auto songData = document->song();
	if (!songData
		|| songData->performer.isEmpty()
		|| songData->title.isEmpty()
		// Ignore cover for voice chat records.
		|| document->hasMimeType(qstr("audio/ogg"))) {
		return;
	}

	const auto &size = kDefaultCoverSize;
	const auto jsonLocation = ImageWithLocation{
		.location = ImageLocation(Location(JsonUrl(songData)), size, size)
	};

	const auto jsonCloudFile = std::make_shared<Data::CloudFile>();
	Data::UpdateCloudFile(
		*jsonCloudFile,
		jsonLocation,
		document->owner().cache(),
		0, // Cache tag.
		nullptr,
		nullptr);

	auto done = [=](const QByteArray &result) {
		if (!jsonCloudFile) {
			return;
		}
		if (const auto responce = ParseResponce(result)) {
			LoadAndApplyThumbnail(document, *responce);
		}
	};
	Data::LoadCloudFile(
		&document->session(),
		*jsonCloudFile,
		Data::FileOrigin(),
		LoadFromCloudOrLocal,
		true,
		0,
		[] { return true; },
		std::move(done));
}

} // namespace Storage::CloudSongCover
