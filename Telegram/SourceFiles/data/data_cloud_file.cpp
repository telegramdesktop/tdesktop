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

void CloudImageView::set(
		not_null<Main::Session*> session,
		QImage image) {
	_image.emplace(std::move(image));
	session->notifyDownloaderTaskFinished();
}

CloudImage::CloudImage() = default;

CloudImage::CloudImage(
		not_null<Main::Session*> session,
		const ImageWithLocation &data) {
	update(session, data);
}

Image *CloudImageView::image() {
	return _image ? &*_image : nullptr;
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
		_view = std::weak_ptr<CloudImageView>();
	} else if (was != now
		&& (!was.is<InMemoryLocation>() || now.is<InMemoryLocation>())) {
		_file.location = ImageLocation();
		_view = std::weak_ptr<CloudImageView>();
	}
	UpdateCloudFile(
		_file,
		data,
		session->data().cache(),
		kImageCacheTag,
		[=](FileOrigin origin) { load(session, origin); },
		[=](QImage preloaded) {
			if (const auto view = activeView()) {
				view->set(session, data.preloaded);
			}
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
		[=](QImage preloaded) {
			if (const auto view = activeView()) {
				view->set(session, data.preloaded);
			}
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

void CloudImage::load(not_null<Main::Session*> session, FileOrigin origin) {
	const auto autoLoading = false;
	const auto finalCheck = [=] {
		if (const auto active = activeView()) {
			return !active->image();
		}
		return true;
	};
	const auto done = [=](QImage result) {
		if (const auto active = activeView()) {
			active->set(session, std::move(result));
		}
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

std::shared_ptr<CloudImageView> CloudImage::createView() {
	if (auto active = activeView()) {
		return active;
	}
	auto view = std::make_shared<CloudImageView>();
	_view = view;
	return view;
}

std::shared_ptr<CloudImageView> CloudImage::activeView() {
	return _view.lock();
}

bool CloudImage::isCurrentView(
		const std::shared_ptr<CloudImageView> &view) const {
	if (!view) {
		return empty();
	}
	return !view.owner_before(_view) && !_view.owner_before(view);
}

void UpdateCloudFile(
		CloudFile &file,
		const ImageWithLocation &data,
		Storage::Cache::Database &cache,
		uint8 cacheTag,
		Fn<void(FileOrigin)> restartLoader,
		Fn<void(QImage)> usePreloaded) {
	if (!data.location.valid()) {
		if (data.progressivePartSize && !file.location.valid()) {
			file.progressivePartSize = data.progressivePartSize;
		}
		return;
	}

	const auto update = !file.location.valid()
		|| (data.location.file().cacheKey()
			&& (!file.location.file().cacheKey()
				|| (file.location.width() < data.location.width())
				|| (file.location.height() < data.location.height())));
	if (!update) {
		return;
	}
	auto cacheBytes = !data.bytes.isEmpty()
		? data.bytes
		: file.location.file().data.is<InMemoryLocation>()
		? file.location.file().data.get_unchecked<InMemoryLocation>().bytes
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
			usePreloaded(data.preloaded);
		}
	} else if (file.loader) {
		const auto origin = base::take(file.loader)->fileOrigin();
		restartLoader(origin);
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
		Fn<void(QImage)> done,
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
			onstack(std::move(read));
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
