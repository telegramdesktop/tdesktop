/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

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
	void mergeToEnd(PreparedList &&other);

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
