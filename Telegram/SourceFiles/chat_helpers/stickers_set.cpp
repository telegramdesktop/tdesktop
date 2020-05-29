/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/stickers_set.h"

#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_file_origin.h"
#include "storage/file_download.h"
#include "ui/image/image.h"
#include "app.h"

namespace Stickers {

SetThumbnailView::SetThumbnailView(not_null<Set*> owner) : _owner(owner) {
}

not_null<Set*> SetThumbnailView::owner() const {
	return _owner;
}

void SetThumbnailView::set(
		not_null<Main::Session*> session,
		QByteArray content) {
	auto image = App::readImage(content, nullptr, false);
	if (image.isNull()) {
		_content = std::move(content);
	} else {
		_image = std::make_unique<Image>(std::move(image));
	}
	session->downloaderTaskFinished().notify();
}

Image *SetThumbnailView::image() const {
	return _image.get();
}

QByteArray SetThumbnailView::content() const {
	return _content;
}

Set::Set(
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

Data::Session &Set::owner() const {
	return *_owner;
}

Main::Session &Set::session() const {
	return _owner->session();
}

MTPInputStickerSet Set::mtpInput() const {
	return (id && access)
		? MTP_inputStickerSetID(MTP_long(id), MTP_long(access))
		: MTP_inputStickerSetShortName(MTP_string(shortName));
}

void Set::setThumbnail(const ImageWithLocation &data) {
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

bool Set::hasThumbnail() const {
	return _thumbnail.location.valid();
}

bool Set::thumbnailLoading() const {
	return (_thumbnail.loader != nullptr);
}

bool Set::thumbnailFailed() const {
	return (_thumbnail.flags & Data::CloudFile::Flag::Failed);
}

void Set::loadThumbnail() {
	auto &file = _thumbnail;
	const auto origin = Data::FileOriginStickerSet(id, access);
	const auto fromCloud = LoadFromCloudOrLocal;
	const auto cacheTag = Data::kImageCacheTag;
	const auto autoLoading = false;
	Data::LoadCloudFile(file, origin, fromCloud, autoLoading, cacheTag, [=] {
		if (const auto active = activeThumbnailView()) {
			return !active->image() && active->content().isEmpty();
		}
		return true;
	}, [=](QByteArray result) {
		if (const auto active = activeThumbnailView()) {
			active->set(&_owner->session(), std::move(result));
		}
	});
}

const ImageLocation &Set::thumbnailLocation() const {
	return _thumbnail.location;
}

int Set::thumbnailByteSize() const {
	return _thumbnail.byteSize;
}

std::shared_ptr<SetThumbnailView> Set::createThumbnailView() {
	if (auto active = activeThumbnailView()) {
		return active;
	}
	auto view = std::make_shared<SetThumbnailView>(this);
	_thumbnailView = view;
	return view;
}

std::shared_ptr<SetThumbnailView> Set::activeThumbnailView() {
	return _thumbnailView.lock();
}

} // namespace Stickers
