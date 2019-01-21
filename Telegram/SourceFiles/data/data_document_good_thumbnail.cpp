/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_document_good_thumbnail.h"

#include "data/data_session.h"
#include "data/data_document.h"
#include "media/media_clip_reader.h"
#include "auth_session.h"

namespace Data {
namespace {

constexpr auto kGoodThumbQuality = 87;

} // namespace

GoodThumbSource::GoodThumbSource(not_null<DocumentData*> document)
: _document(document) {
}

void GoodThumbSource::generate(base::binary_guard &&guard) {
	if (!guard.alive()) {
		return;
	}
	const auto data = _document->data();
	auto location = _document->location().isEmpty()
		? nullptr
		: std::make_unique<FileLocation>(_document->location());
	if (data.isEmpty() && !location) {
		_empty = true;
		return;
	}
	crl::async([
		=,
		guard = std::move(guard),
		location = std::move(location)
	]() mutable {
		const auto filepath = (location && location->accessEnable())
			? location->name()
			: QString();
		auto result = Media::Clip::PrepareForSending(filepath, data);
		auto bytes = QByteArray();
		if (!result.thumbnail.isNull()) {
			QBuffer buffer(&bytes);
			result.thumbnail.save(&buffer, "JPG", kGoodThumbQuality);
		}
		if (!filepath.isEmpty()) {
			location->accessDisable();
		}
		const auto bytesSize = bytes.size();
		ready(
			std::move(guard),
			std::move(result.thumbnail),
			bytesSize,
			std::move(bytes));
	});
}

// NB: This method is called from crl::async(), 'this' is unreliable.
void GoodThumbSource::ready(
		base::binary_guard &&guard,
		QImage &&image,
		int bytesSize,
		QByteArray &&bytesForCache) {
	crl::on_main([
		=,
		guard = std::move(guard),
		image = std::move(image),
		bytes = std::move(bytesForCache)
	]() mutable {
		if (!guard.alive()) {
			return;
		}
		if (image.isNull()) {
			_empty = true;
			return;
		}
		_loaded = std::move(image);
		_width = _loaded.width();
		_height = _loaded.height();
		_bytesSize = bytesSize;
		if (!bytes.isEmpty()) {
			Auth().data().cache().put(
				_document->goodThumbnailCacheKey(),
				Storage::Cache::Database::TaggedValue{
					std::move(bytes),
					Data::kImageCacheTag });
		}
		Auth().downloaderTaskFinished().notify();
	});
}

void GoodThumbSource::load(
		Data::FileOrigin origin,
		bool loadFirst,
		bool prior) {
	if (loading() || _empty) {
		return;
	}
	auto [left, right] = base::make_binary_guard();
	_loading = std::move(left);

	auto callback = [=, guard = std::move(right)](
			QByteArray &&value) mutable {
		if (value.isEmpty()) {
			crl::on_main([=, guard = std::move(guard)]() mutable {
				generate(std::move(guard));
			});
			return;
		}
		crl::async([
			=,
			guard = std::move(guard),
			value = std::move(value)
		]() mutable {
			ready(std::move(guard), App::readImage(value), value.size());
		});
	};

	Auth().data().cache().get(
		_document->goodThumbnailCacheKey(),
		std::move(callback));
}

void GoodThumbSource::loadEvenCancelled(
		Data::FileOrigin origin,
		bool loadFirst,
		bool prior) {
	_empty = false;
	load(origin, loadFirst, prior);
}

QImage GoodThumbSource::takeLoaded() {
	return std::move(_loaded);
}

void GoodThumbSource::unload() {
	_loaded = QImage();
	cancel();
}

void GoodThumbSource::automaticLoad(
	Data::FileOrigin origin,
	const HistoryItem *item) {
}

void GoodThumbSource::automaticLoadSettingsChanged() {
}

bool GoodThumbSource::loading() {
	return _loading.alive();
}

bool GoodThumbSource::displayLoading() {
	return false;
}

void GoodThumbSource::cancel() {
	_loading.kill();
}

float64 GoodThumbSource::progress() {
	return 1.;
}

int GoodThumbSource::loadOffset() {
	return 0;
}

const StorageImageLocation &GoodThumbSource::location() {
	return StorageImageLocation::Null;
}

void GoodThumbSource::refreshFileReference(const QByteArray &data) {
}

std::optional<Storage::Cache::Key> GoodThumbSource::cacheKey() {
	return _document->goodThumbnailCacheKey();
}

void GoodThumbSource::setDelayedStorageLocation(
	const StorageImageLocation &location) {
}

void GoodThumbSource::performDelayedLoad(Data::FileOrigin origin) {
}

bool GoodThumbSource::isDelayedStorageImage() const {
	return false;
}

void GoodThumbSource::setImageBytes(const QByteArray &bytes) {
	if (!bytes.isEmpty()) {
		cancel();
		_loaded = App::readImage(bytes);
		_width = _loaded.width();
		_height = _loaded.height();
		_bytesSize = bytes.size();
	}
}

int GoodThumbSource::width() {
	return _width;
}

int GoodThumbSource::height() {
	return _height;
}

int GoodThumbSource::bytesSize() {
	return _bytesSize;
}

void GoodThumbSource::setInformation(int size, int width, int height) {
	_width = width;
	_height = height;
	_bytesSize = size;
}

QByteArray GoodThumbSource::bytesForCache() {
	return QByteArray();
}

} // namespace Data
