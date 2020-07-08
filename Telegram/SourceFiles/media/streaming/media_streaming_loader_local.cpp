/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/streaming/media_streaming_loader_local.h"

#include "storage/cache/storage_cache_types.h"

#include <QtCore/QBuffer>

namespace Media {
namespace Streaming {
namespace {

// This is the maximum file size in Telegram API.
constexpr auto kMaxFileSize = 4000 * 512 * 1024;

int ValidateLocalSize(int64 size) {
	return (size > 0 && size <= kMaxFileSize) ? int(size) : 0;
}

} // namespace

LoaderLocal::LoaderLocal(std::unique_ptr<QIODevice> device)
: _device(std::move(device))
, _size(ValidateLocalSize(_device->size())) {
	Expects(_device != nullptr);

	if (!_size || !_device->open(QIODevice::ReadOnly)) {
		fail();
	}
}

Storage::Cache::Key LoaderLocal::baseCacheKey() const {
	return {};
}

int LoaderLocal::size() const {
	return _size;
}

void LoaderLocal::load(int offset) {
	if (_device->pos() != offset && !_device->seek(offset)) {
		fail();
		return;
	}
	auto result = _device->read(kPartSize);
	if (result.isEmpty()
		|| ((result.size() != kPartSize)
			&& (offset + result.size() != size()))) {
		fail();
		return;
	}
	crl::on_main(this, [=, result = std::move(result)]() mutable {
		_parts.fire({ offset, std::move(result) });
	});
}

void LoaderLocal::fail() {
	crl::on_main(this, [=] {
		_parts.fire({ LoadedPart::kFailedOffset });
	});
}

void LoaderLocal::cancel(int offset) {
}

void LoaderLocal::resetPriorities() {
}

void LoaderLocal::setPriority(int priority) {
}

void LoaderLocal::stop() {
}

void LoaderLocal::tryRemoveFromQueue() {
}

rpl::producer<LoadedPart> LoaderLocal::parts() const {
	return _parts.events();
}

void LoaderLocal::attachDownloader(
		not_null<Storage::StreamedFileDownloader*> downloader) {
	Unexpected("Downloader attached to a local streaming loader.");
}

void LoaderLocal::clearAttachedDownloader() {
	Unexpected("Downloader detached from a local streaming loader.");
}

std::unique_ptr<LoaderLocal> MakeFileLoader(const QString &path) {
	return std::make_unique<LoaderLocal>(std::make_unique<QFile>(path));
}

std::unique_ptr<LoaderLocal> MakeBytesLoader(const QByteArray &bytes) {
	auto device = std::make_unique<QBuffer>();
	auto copy = new QByteArray(bytes);
	QObject::connect(device.get(), &QBuffer::destroyed, [=] {
		delete copy;
	});
	device->setBuffer(copy);
	return std::make_unique<LoaderLocal>(std::move(device));
}

} // namespace Streaming
} // namespace Media
