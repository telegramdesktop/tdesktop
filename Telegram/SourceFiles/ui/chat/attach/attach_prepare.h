/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "editor/photo_editor_common.h"
#include "ui/rect_part.h"

#include <QtCore/QSemaphore>
#include <deque>

namespace Ui {

class RpWidget;
class SendFilesWay;

struct PreparedFileInformation {
	struct Image {
		QImage data;
		QByteArray bytes;
		QByteArray format;
		bool animated = false;
		Editor::PhotoModifications modifications;
	};
	struct Song {
		crl::time duration = -1;
		QString title;
		QString performer;
		QImage cover;
	};
	struct Video {
		bool isGifv = false;
		bool isWebmSticker = false;
		bool supportsStreaming = false;
		crl::time duration = -1;
		QImage thumbnail;
	};

	QString filemime;
	std::variant<v::null_t, Image, Song, Video> media;
};

enum class AlbumType {
	None,
	PhotoVideo,
	Music,
	File,
};

struct PreparedFile {
	// File-s can be grouped if 'groupFiles'.
	// File-s + Photo-s can be grouped if 'groupFiles && !sendImagesAsPhotos'.
	// Photo-s can be grouped if 'groupFiles'.
	// Photo-s + Video-s can be grouped if 'groupFiles && sendImagesAsPhotos'.
	// Video-s can be grouped if 'groupFiles'.
	// Music-s can be grouped if 'groupFiles'.
	enum class Type {
		None,
		Photo,
		Video,
		Music,
		File,
	};

	PreparedFile(const QString &path);
	PreparedFile(PreparedFile &&other);
	PreparedFile &operator=(PreparedFile &&other);
	~PreparedFile();

	[[nodiscard]] bool canBeInAlbumType(AlbumType album) const;
	[[nodiscard]] AlbumType albumType(bool sendImagesAsPhotos) const;
	[[nodiscard]] bool isSticker() const;
	[[nodiscard]] bool isVideoFile() const;
	[[nodiscard]] bool isGifv() const;

	QString path;
	QByteArray content;
	int64 size = 0;
	std::unique_ptr<PreparedFileInformation> information;
	std::unique_ptr<PreparedFile> videoCover;
	QImage preview;
	QSize shownDimensions;
	QSize originalDimensions;
	Type type = Type::File;
	bool spoiler = false;
};

[[nodiscard]] bool CanBeInAlbumType(PreparedFile::Type type, AlbumType album);
[[nodiscard]] bool InsertTextOnImageCancel(const QString &text);

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
	PreparedList(PreparedList &&other) = default;
	PreparedList &operator=(PreparedList &&other) = default;

	[[nodiscard]] static PreparedList Reordered(
		PreparedList &&list,
		std::vector<int> order);
	void mergeToEnd(PreparedList &&other, bool cutToAlbumSize = false);

	[[nodiscard]] bool canAddCaption(bool sendingAlbum, bool compress) const;
	[[nodiscard]] bool canMoveCaption(
		bool sendingAlbum,
		bool compress) const;
	[[nodiscard]] bool canChangePrice(
		bool sendingAlbum,
		bool compress) const;
	[[nodiscard]] bool canBeSentInSlowmode() const;
	[[nodiscard]] bool canBeSentInSlowmodeWith(
		const PreparedList &other) const;

	[[nodiscard]] bool hasGroupOption(bool slowmode) const;
	[[nodiscard]] bool hasSendImagesAsPhotosOption(bool slowmode) const;
	[[nodiscard]] bool canHaveEditorHintLabel() const;
	[[nodiscard]] bool hasSticker() const;
	[[nodiscard]] bool hasSpoilerMenu(bool compress) const;

	Error error = Error::None;
	QString errorData;
	std::vector<PreparedFile> files;
	std::deque<PreparedFile> filesToProcess;
	std::optional<bool> overrideSendImagesAsPhotos;
};

struct PreparedGroup {
	PreparedList list;
	AlbumType type = AlbumType::None;

	[[nodiscard]] bool sentWithCaption() const {
		return (list.files.size() == 1)
			|| (type == AlbumType::PhotoVideo);
	}
};

[[nodiscard]] std::vector<PreparedGroup> DivideByGroups(
	PreparedList &&list,
	SendFilesWay way,
	bool slowmode);

[[nodiscard]] int MaxAlbumItems();
[[nodiscard]] bool ValidateThumbDimensions(int width, int height);

[[nodiscard]] QPixmap PrepareSongCoverForThumbnail(QImage image, int size);

[[nodiscard]] QPixmap BlurredPreviewFromPixmap(
	QPixmap pixmap,
	RectParts corners);

} // namespace Ui
