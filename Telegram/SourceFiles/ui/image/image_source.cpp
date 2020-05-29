/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/image/image_source.h"

#include "storage/cache/storage_cache_database.h"
#include "storage/file_download_mtproto.h"
#include "storage/file_download_web.h"
#include "data/data_session.h"
#include "data/data_file_origin.h"
#include "history/history_item.h"
#include "history/history.h"
#include "main/main_session.h"
#include "app.h"

#include <QtCore/QBuffer>

namespace Images {
namespace {

[[nodiscard]] QByteArray ReadContent(const QString &path) {
	auto file = QFile(path);
	const auto good = (file.size() <= App::kImageSizeLimit)
		&& file.open(QIODevice::ReadOnly);
	return good ? file.readAll() : QByteArray();
}

[[nodiscard]] QImage ReadImage(const QByteArray &content) {
	return App::readImage(content, nullptr, false, nullptr);
}

} // namespace

ImageSource::ImageSource(const QString &path)
: ImageSource(ReadContent(path)) {
}

ImageSource::ImageSource(const QByteArray &content)
: ImageSource(ReadImage(content)) {
}

ImageSource::ImageSource(QImage &&data) : _data(std::move(data)) {
}

void ImageSource::load() {
}

QImage ImageSource::takeLoaded() {
	return _data;
}

int ImageSource::width() {
	return _data.width();
}

int ImageSource::height() {
	return _data.height();
}

} // namespace Images
