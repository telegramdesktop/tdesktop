/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/streaming/media_streaming_loader_local.h"

#include "logs.h"
#include "storage/cache/storage_cache_types.h"

#include <QtCore/QBuffer>
#include <QtCore/QFile>

namespace Media {
namespace Streaming {
namespace {

// This is the maximum file size in Telegram API.
constexpr auto kMaxFileSize = 8000 * int64(512 * 1024);

[[nodiscard]] int64 ValidateLocalSize(int64 size) {
	return (size > 0 && size <= kMaxFileSize) ? size : 0;
}

} // namespace

LoaderLocal::LoaderLocal(std::unique_ptr<QIODevice> device)
: _device(std::move(device))
, _size(ValidateLocalSize(_device->size())) {
	Expects(_device != nullptr);

	if (!_size || !_device->open(QIODevice::ReadOnly)) {
		const auto file = qobject_cast<const QFile*>(_device.get());
		LOG(("Streaming Error: local loader failed, path: %1, size: %2, "
			"opened: %3, error: %4"
			).arg(file ? file->fileName() : QString()
			).arg(_size
			).arg(_device->isOpen()
			).arg(_device->errorString()));
		fail();
	}
}

Storage::Cache::Key LoaderLocal::baseCacheKey() const {
	return {};
}

int64 LoaderLocal::size() const {
	return _size;
}

void LoaderLocal::load(int64 offset) {
	if (_device->pos() != offset && !_device->seek(offset)) {
		LOG(("Streaming Error: local loader seek %1 failed: %2"
			).arg(offset
			).arg(_device->errorString()));
		fail();
		return;
	}
	auto result = _device->read(kPartSize);
	if (result.isEmpty()
		|| ((result.size() != kPartSize)
			&& (offset + result.size() != size()))) {
		LOG(("Streaming Error: local loader short read at %1: %2 of %3, "
			"error: %4"
			).arg(offset
			).arg(result.size()
			).arg(kPartSize
			).arg(_device->errorString()));
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

void LoaderLocal::cancel(int64 offset) {
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

rpl::producer<SpeedEstimate> LoaderLocal::speedEstimate() const {
	return rpl::never<SpeedEstimate>();
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
