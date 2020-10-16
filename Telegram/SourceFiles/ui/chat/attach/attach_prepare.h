/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QSemaphore>
#include <deque>

namespace Ui {

struct PreparedFileInformation {
	struct Image {
		QImage data;
		bool animated = false;
	};
	struct Song {
		int duration = -1;
		QString title;
		QString performer;
		QImage cover;
	};
	struct Video {
		bool isGifv = false;
		bool supportsStreaming = false;
		int duration = -1;
		QImage thumbnail;
	};

	QString filemime;
	std::variant<v::null_t, Image, Song, Video> media;
};

struct PreparedFile {
	// File-s can be grouped if 'groupFiles'.
	// File-s + Photo-s can be grouped if 'groupFiles && !sendImagesAsPhotos'.
	// Photo-s can be grouped if '(groupFiles && !sendImagesAsPhotos)
	//   || (groupMediaInAlbums && sendImagesAsPhotos)'.
	// Photo-s + Video-s can be grouped if 'groupMediaInAlbums
	//   && sendImagesAsPhotos'.
	// Video-s can be grouped if 'groupMediaInAlbums'.
	enum class AlbumType {
		File,
		Photo,
		Video,
	};

	PreparedFile(const QString &path);
	PreparedFile(PreparedFile &&other);
	PreparedFile &operator=(PreparedFile &&other);
	~PreparedFile();

	QString path;
	QByteArray content;
	QString mime;
	int size = 0;
	std::unique_ptr<Ui::PreparedFileInformation> information;
	QImage preview;
	QSize shownDimensions;
	AlbumType type = AlbumType::File;
};

struct PreparedList {
	enum class Error {
		None,
		NonLocalUrl,
		Directory,
		EmptyFile,
		TooLargeFile,
	};

	PreparedList() = default;
	PreparedList(Error error, QString errorData)
		: error(error)
		, errorData(errorData) {
	}
	[[nodiscard]] static PreparedList Reordered(
		PreparedList &&list,
		std::vector<int> order);
	void mergeToEnd(PreparedList &&other, bool cutToAlbumSize = false);

	[[nodiscard]] bool canAddCaption(bool groupMediaInAlbums) const;
	[[nodiscard]] bool canBeSentInSlowmode() const;

	Error error = Error::None;
	QString errorData;
	std::vector<PreparedFile> files;
	std::deque<PreparedFile> filesToProcess;
	std::optional<bool> overrideSendImagesAsPhotos;
};

[[nodiscard]] int MaxAlbumItems();
[[nodiscard]] bool ValidateThumbDimensions(int width, int height);

} // namespace Ui
