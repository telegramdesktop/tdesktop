/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Media::Video {

struct FileInfo {
	QSize dimensions;
	crl::time duration = 0;

	[[nodiscard]] bool valid() const {
		return !dimensions.isEmpty() && (duration > 0);
	}
};

[[nodiscard]] FileInfo ReadFileInfo(
	const QString &path,
	const QByteArray &content = QByteArray());

struct ExtractRequest {
	std::vector<crl::time> positions;
	QSize box;
	bool cover = false;
};

using ExtractCallback = Fn<bool(int index, QImage &&frame)>;

void ExtractFrames(
	const QString &path,
	const QByteArray &content,
	const ExtractRequest &request,
	ExtractCallback callback);

[[nodiscard]] QImage ExtractFrame(
	const QString &path,
	const QByteArray &content,
	crl::time position,
	QSize box = QSize());

} // namespace Media::Video
