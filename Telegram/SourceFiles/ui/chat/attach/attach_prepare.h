/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

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
	enum class AlbumType {
		None,
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
	std::unique_ptr<Ui::PreparedFileInformation> information;
	QImage preview;
	QSize shownDimensions;
	AlbumType type = AlbumType::None;
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

	[[nodiscard]] bool canAddCaption(bool isAlbum, bool compressImages) const;

	Error error = Error::None;
	QString errorData;
	std::vector<PreparedFile> files;
	bool allFilesForCompress = true;
	bool albumIsPossible = false;
};

[[nodiscard]] int MaxAlbumItems();

} // namespace Ui
