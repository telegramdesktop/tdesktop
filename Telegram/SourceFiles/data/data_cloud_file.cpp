/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_cloud_file.h"

#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "storage/cache/storage_cache_database.h"
#include "storage/file_download.h"
#include "ui/image/image.h"
#include "main/main_session.h"

namespace Data {

CloudFile::~CloudFile() {
	// Destroy loader with still alive CloudFile with already zero '.loader'.
	// Otherwise in ~FileLoader it tries to clear file.loader and crashes.
	base::take(loader);
}

CloudImage::CloudImage() = default;

CloudImage::CloudImage(
		not_null<Main::Session*> session,
		const ImageWithLocation &data) {
	update(session, data);
}

void CloudImage::set(
		not_null<Main::Session*> session,
		const ImageWithLocation &data) {
	const auto &was = _file.location.file().data;
	const auto &now = data.location.file().data;
	if (!data.location.valid()) {
		_file.flags |= CloudFile::Flag::Cancelled;
		_file.loader = nullptr;
		_file.location = ImageLocation();
		_file.byteSize = 0;
		_file.flags = CloudFile::Flag();
		_view = std::weak_ptr<QImage>();
	} else if (was != now
		&& (!v::is<InMemoryLocation>(was) || v::is<InMemoryLocation>(now))) {
		_file.location = ImageLocation();
		_view = std::weak_ptr<QImage>();
	}
	UpdateCloudFile(
		_file,
		data,
		session->data().cache(),
		kImageCacheTag,
		[=](FileOrigin origin) { load(session, origin); },
		[=](QImage preloaded, QByteArray) {
			setToActive(session, std::move(preloaded));
		});
}

void CloudImage::update(
		not_null<Main::Session*> session,
		const ImageWithLocation &data) {
	UpdateCloudFile(
		_file,
		data,
		session->data().cache(),
		kImageCacheTag,
		[=](FileOrigin origin) { load(session, origin); },
		[=](QImage preloaded, QByteArray) {
			setToActive(session, std::move(preloaded));
		});
}

bool CloudImage::empty() const {
	return !_file.location.valid();
}

bool CloudImage::loading() const {
	return (_file.loader != nullptr);
}

bool CloudImage::failed() const {
	return (_file.flags & CloudFile::Flag::Failed);
}

bool CloudImage::loadedOnce() const {
	return (_file.flags & CloudFile::Flag::Loaded);
}

void CloudImage::load(not_null<Main::Session*> session, FileOrigin origin) {
	const auto autoLoading = false;
	const auto finalCheck = [=] {
		if (const auto active = activeView()) {
			return active->isNull();
		} else if (_file.flags & CloudFile::Flag::Loaded) {
			return false;
		}
		return !(_file.flags & CloudFile::Flag::Loaded);
	};
	const auto done = [=](QImage result, QByteArray) {
		setToActive(session, std::move(result));
	};
	LoadCloudFile(
		session,
		_file,
		origin,
		LoadFromCloudOrLocal,
		autoLoading,
		kImageCacheTag,
		finalCheck,
		done);
}

const ImageLocation &CloudImage::location() const {
	return _file.location;
}

int CloudImage::byteSize() const {
	return _file.byteSize;
}

std::shared_ptr<QImage> CloudImage::createView() {
	if (auto active = activeView()) {
		return active;
	}
	auto view = std::make_shared<QImage>();
	_view = view;
	return view;
}

std::shared_ptr<QImage> CloudImage::activeView() const {
	return _view.lock();
}

bool CloudImage::isCurrentView(const std::shared_ptr<QImage> &view) const {
	if (!view) {
		return empty();
	}
	return !view.owner_before(_view) && !_view.owner_before(view);
}

void CloudImage::setToActive(
		not_null<Main::Session*> session,
		QImage image) {
	if (const auto view = activeView()) {
		*view = image.isNull()
			? Image::Empty()->original()
			: std::move(image);
		session->notifyDownloaderTaskFinished();
	}
}

void UpdateCloudFile(
		CloudFile &file,
		const ImageWithLocation &data,
		Storage::Cache::Database &cache,
		uint8 cacheTag,
		Fn<void(FileOrigin)> restartLoader,
		Fn<void(QImage, QByteArray)> usePreloaded) {
	if (!data.location.valid()) {
		if (data.progressivePartSize && !file.location.valid()) {
			file.progressivePartSize = data.progressivePartSize;
		}
		return;
	}

	const auto needStickerThumbnailUpdate = [&] {
		const auto was = std::get_if<StorageFileLocation>(
			&file.location.file().data);
		const auto now = std::get_if<StorageFileLocation>(
			&data.location.file().data);
		using Type = StorageFileLocation::Type;
		if (!was || !now || was->type() != Type::StickerSetThumb) {
			return false;
		}
		return now->valid()
			&& (now->type() != Type::StickerSetThumb
				|| now->cacheKey() != was->cacheKey());
	};
	const auto update = !file.location.valid()
		|| (data.location.file().cacheKey()
			&& (!file.location.file().cacheKey()
				|| (file.location.width() < data.location.width())
				|| (file.location.height() < data.location.height())
				|| needStickerThumbnailUpdate()));
	if (!update) {
		return;
	}
	auto cacheBytes = !data.bytes.isEmpty()
		? data.bytes
		: v::is<InMemoryLocation>(file.location.file().data)
		? v::get<InMemoryLocation>(file.location.file().data).bytes
		: QByteArray();
	if (!cacheBytes.isEmpty()) {
		if (const auto cacheKey = data.location.file().cacheKey()) {
			cache.putIfEmpty(
				cacheKey,
				Storage::Cache::Database::TaggedValue(
					std::move(cacheBytes),
					cacheTag));
		}
	}
	file.location = data.location;
	file.byteSize = data.bytesCount;
	if (!data.preloaded.isNull()) {
		file.loader = nullptr;
		if (usePreloaded) {
			usePreloaded(data.preloaded, data.bytes);
		}
	} else if (file.loader) {
		const auto origin = base::take(file.loader)->fileOrigin();
		restartLoader(origin);
	} else if (file.flags & CloudFile::Flag::Failed) {
		file.flags &= ~CloudFile::Flag::Failed;
	}
}

void LoadCloudFile(
		not_null<Main::Session*> session,
		CloudFile &file,
		FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading,
		uint8 cacheTag,
		Fn<bool()> finalCheck,
		Fn<void(CloudFile&)> done,
		Fn<void(bool)> fail,
		Fn<void()> progress,
		int downloadFrontPartSize = 0) {
	const auto loadSize = downloadFrontPartSize
		? std::min(downloadFrontPartSize, file.byteSize)
		: file.byteSize;
	if (file.loader) {
		if (fromCloud == LoadFromCloudOrLocal) {
			file.loader->permitLoadFromCloud();
		}
		if (file.loader->loadSize() < loadSize) {
			file.loader->increaseLoadSize(loadSize, autoLoading);
		}
		return;
	} else if ((file.flags & CloudFile::Flag::Failed)
		|| !file.location.valid()
		|| (finalCheck && !finalCheck())) {
		return;
	}
	file.flags &= ~CloudFile::Flag::Cancelled;
	file.loader = CreateFileLoader(
		session,
		file.location.file(),
		origin,
		QString(),
		loadSize,
		file.byteSize,
		UnknownFileLocation,
		LoadToCacheAsWell,
		fromCloud,
		autoLoading,
		cacheTag);

	const auto finish = [done](CloudFile &file) {
		if (!file.loader || file.loader->cancelled()) {
			file.flags |= CloudFile::Flag::Cancelled;
		} else {
			file.flags |= CloudFile::Flag::Loaded;
			done(file);
		}
		// NB! file.loader may be in ~FileLoader() already.
		if (const auto loader = base::take(file.loader)) {
			if ((file.flags & CloudFile::Flag::Cancelled)
				&& !loader->cancelled()) {
				loader->cancel();
			}
		}
	};

	file.loader->updates(
	) | rpl::start_with_next_error_done([=] {
		if (const auto onstack = progress) {
			onstack();
		}
	}, [=, &file](bool started) {
		finish(file);
		file.flags |= CloudFile::Flag::Failed;
		if (const auto onstack = fail) {
			onstack(started);
		}
	}, [=, &file] {
		finish(file);
	}, file.loader->lifetime());

	file.loader->start();
}

void LoadCloudFile(
		not_null<Main::Session*> session,
		CloudFile &file,
		FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading,
		uint8 cacheTag,
		Fn<bool()> finalCheck,
		Fn<void(QImage, QByteArray)> done,
		Fn<void(bool)> fail,
		Fn<void()> progress,
		int downloadFrontPartSize) {
	const auto callback = [=](CloudFile &file) {
		if (auto read = file.loader->imageData(); read.isNull()) {
			file.flags |= CloudFile::Flag::Failed;
			if (const auto onstack = fail) {
				onstack(true);
			}
		} else if (const auto onstack = done) {
			onstack(std::move(read), file.loader->bytes());
		}
	};
	LoadCloudFile(
		session,
		file,
		origin,
		fromCloud,
		autoLoading,
		cacheTag,
		finalCheck,
		callback,
		std::move(fail),
		std::move(progress),
		downloadFrontPartSize);
}

void LoadCloudFile(
		not_null<Main::Session*> session,
		CloudFile &file,
		FileOrigin origin,
		LoadFromCloudSetting fromCloud,
		bool autoLoading,
		uint8 cacheTag,
		Fn<bool()> finalCheck,
		Fn<void(QByteArray)> done,
		Fn<void(bool)> fail,
		Fn<void()> progress) {
	const auto callback = [=](CloudFile &file) {
		if (auto bytes = file.loader->bytes(); bytes.isEmpty()) {
			file.flags |= CloudFile::Flag::Failed;
			if (const auto onstack = fail) {
				onstack(true);
			}
		} else if (const auto onstack = done) {
			onstack(std::move(bytes));
		}
	};
	LoadCloudFile(
		session,
		file,
		origin,
		fromCloud,
		autoLoading,
		cacheTag,
		finalCheck,
		callback,
		std::move(fail),
		std::move(progress));
}

} // namespace Data
