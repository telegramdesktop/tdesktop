/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/streaming/media_streaming_loader_local.h"

#include "storage/cache/storage_cache_types.h"

namespace Media {
namespace Streaming {

LoaderLocal::LoaderLocal(std::unique_ptr<QIODevice> device)
: _device(std::move(device)) {
	Expects(_device != nullptr);

	if (!_device->open(QIODevice::ReadOnly)) {
		fail();
	}
}

std::optional<Storage::Cache::Key> LoaderLocal::baseCacheKey() const {
	return std::nullopt;
}

int LoaderLocal::size() const {
	return _device->size();
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

void LoaderLocal::increasePriority() {
}

void LoaderLocal::stop() {
}

rpl::producer<LoadedPart> LoaderLocal::parts() const {
	return _parts.events();
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
