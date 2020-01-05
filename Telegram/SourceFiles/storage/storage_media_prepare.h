/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/file_utilities.h"

struct FileMediaInformation;

namespace Storage {

enum class MimeDataState {
	None,
	Files,
	PhotoFiles,
	Image,
};

MimeDataState ComputeMimeDataState(const QMimeData *data);

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
	std::unique_ptr<FileMediaInformation> information;
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
	static PreparedList Reordered(
		PreparedList &&list,
		std::vector<int> order);
	static std::optional<PreparedList> PreparedFileFromFileDialog(
		FileDialog::OpenResult &&result,
		bool isAlbum,
		Fn<void()> errorCallback,
		Fn<bool(QString)> isValidFileCallback,
		int previewWidth);
	void mergeToEnd(PreparedList &&other);

	bool canAddCaption(bool isAlbum, bool compressImages) const;

	Error error = Error::None;
	QString errorData;
	std::vector<PreparedFile> files;
	bool allFilesForCompress = true;
	bool albumIsPossible = false;

};

bool ValidateThumbDimensions(int width, int height);
PreparedList PrepareMediaList(const QList<QUrl> &files, int previewWidth);
PreparedList PrepareMediaList(const QStringList &files, int previewWidth);
PreparedList PrepareMediaFromImage(
	QImage &&image,
	QByteArray &&content,
	int previewWidth);
int MaxAlbumItems();

} // namespace Storage
