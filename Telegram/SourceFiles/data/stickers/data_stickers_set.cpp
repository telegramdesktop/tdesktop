/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/stickers/data_stickers_set.h"

#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_file_origin.h"
#include "storage/file_download.h"
#include "ui/image/image.h"
#include "app.h"

namespace Data {

StickersSetThumbnailView::StickersSetThumbnailView(
	not_null<StickersSet*> owner)
: _owner(owner) {
}

not_null<StickersSet*> StickersSetThumbnailView::owner() const {
	return _owner;
}

void StickersSetThumbnailView::set(
		not_null<Main::Session*> session,
		QByteArray content) {
	auto image = App::readImage(content, nullptr, false);
	if (image.isNull()) {
		_content = std::move(content);
	} else {
		_image = std::make_unique<Image>(std::move(image));
	}
	session->notifyDownloaderTaskFinished();
}

Image *StickersSetThumbnailView::image() const {
	return _image.get();
}

QByteArray StickersSetThumbnailView::content() const {
	return _content;
}

StickersSet::StickersSet(
	not_null<Data::Session*> owner,
	uint64 id,
	uint64 access,
	const QString &title,
	const QString &shortName,
	int count,
	int32 hash,
	MTPDstickerSet::Flags flags,
	TimeId installDate)
: id(id)
, access(access)
, title(title)
, shortName(shortName)
, count(count)
, hash(hash)
, flags(flags)
, installDate(installDate)
, _owner(owner) {
}

Data::Session &StickersSet::owner() const {
	return *_owner;
}

Main::Session &StickersSet::session() const {
	return _owner->session();
}

MTPInputStickerSet StickersSet::mtpInput() const {
	return (id && access)
		? MTP_inputStickerSetID(MTP_long(id), MTP_long(access))
		: MTP_inputStickerSetShortName(MTP_string(shortName));
}

void StickersSet::setThumbnail(const ImageWithLocation &data) {
	Data::UpdateCloudFile(
		_thumbnail,
		data,
		_owner->cache(),
		Data::kImageCacheTag,
		[=](Data::FileOrigin origin) { loadThumbnail(); });
	if (!data.bytes.isEmpty()) {
		if (_thumbnail.loader) {
			_thumbnail.loader->cancel();
		}
		if (const auto view = activeThumbnailView()) {
			view->set(&_owner->session(), data.bytes);
		}
	}
}

bool StickersSet::hasThumbnail() const {
	return _thumbnail.location.valid();
}

bool StickersSet::thumbnailLoading() const {
	return (_thumbnail.loader != nullptr);
}

bool StickersSet::thumbnailFailed() const {
	return (_thumbnail.flags & Data::CloudFile::Flag::Failed);
}

void StickersSet::loadThumbnail() {
	const auto autoLoading = false;
	const auto finalCheck = [=] {
		if (const auto active = activeThumbnailView()) {
			return !active->image() && active->content().isEmpty();
		}
		return true;
	};
	const auto done = [=](QByteArray result) {
		if (const auto active = activeThumbnailView()) {
			active->set(&_owner->session(), std::move(result));
		}
	};
	Data::LoadCloudFile(
		&_owner->session(),
		_thumbnail,
		Data::FileOriginStickerSet(id, access),
		LoadFromCloudOrLocal,
		autoLoading,
		Data::kImageCacheTag,
		finalCheck,
		done);
}

const ImageLocation &StickersSet::thumbnailLocation() const {
	return _thumbnail.location;
}

int StickersSet::thumbnailByteSize() const {
	return _thumbnail.byteSize;
}

std::shared_ptr<StickersSetThumbnailView> StickersSet::createThumbnailView() {
	if (auto active = activeThumbnailView()) {
		return active;
	}
	auto view = std::make_shared<StickersSetThumbnailView>(this);
	_thumbnailView = view;
	return view;
}

std::shared_ptr<StickersSetThumbnailView> StickersSet::activeThumbnailView() {
	return _thumbnailView.lock();
}

} // namespace Stickers
