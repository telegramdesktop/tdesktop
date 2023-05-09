/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/stories/media_stories_sibling.h"

#include "base/weak_ptr.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "media/stories/media_stories_controller.h"
#include "media/streaming/media_streaming_instance.h"
#include "media/streaming/media_streaming_player.h"

namespace Media::Stories {
namespace {

constexpr auto kGoodFadeDuration = crl::time(200);

} // namespace

class Sibling::Loader {
public:
	virtual ~Loader() = default;

	virtual QImage blurred() = 0;
	virtual QImage good() = 0;
};

class Sibling::LoaderPhoto final : public Sibling::Loader {
public:
	LoaderPhoto(
		not_null<PhotoData*> photo,
		Data::FileOrigin origin,
		Fn<void()> update);

	QImage blurred() override;
	QImage good() override;

private:
	const not_null<PhotoData*> _photo;
	const Fn<void()> _update;
	std::shared_ptr<Data::PhotoMedia> _media;
	rpl::lifetime _waitingLoading;

};

class Sibling::LoaderVideo final
	: public Sibling::Loader
	, public base::has_weak_ptr {
public:
	LoaderVideo(
		not_null<DocumentData*> video,
		Data::FileOrigin origin,
		Fn<void()> update);

	QImage blurred() override;
	QImage good() override;

private:
	void waitForGoodThumbnail();
	bool updateAfterGoodCheck();
	void streamedFailed();

	const not_null<DocumentData*> _video;
	const Data::FileOrigin _origin;
	const Fn<void()> _update;
	std::shared_ptr<Data::DocumentMedia> _media;
	std::unique_ptr<Streaming::Instance> _streamed;
	rpl::lifetime _waitingGoodGeneration;
	bool _checkingGoodInCache = false;
	bool _failed = false;

};

Sibling::LoaderPhoto::LoaderPhoto(
	not_null<PhotoData*> photo,
	Data::FileOrigin origin,
	Fn<void()> update)
: _photo(photo)
, _update(std::move(update))
, _media(_photo->createMediaView()) {
	_photo->load(origin, LoadFromCloudOrLocal, true);
}

QImage Sibling::LoaderPhoto::blurred() {
	if (const auto image = _media->thumbnailInline()) {
		return image->original();
	}
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(ratio, ratio, QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::black);
	result.setDevicePixelRatio(ratio);
	return result;
}

QImage Sibling::LoaderPhoto::good() {
	if (const auto image = _media->image(Data::PhotoSize::Large)) {
		return image->original();
	} else if (!_waitingLoading) {
		_photo->session().downloaderTaskFinished(
		) | rpl::start_with_next([=] {
			if (_media->loaded()) {
				_update();
			}
		}, _waitingLoading);
	}
	return QImage();
}

Sibling::LoaderVideo::LoaderVideo(
	not_null<DocumentData*> video,
	Data::FileOrigin origin,
	Fn<void()> update)
: _video(video)
, _origin(origin)
, _update(std::move(                                                                                                                     update))
, _media(_video->createMediaView()) {
	_media->goodThumbnailWanted();
}

QImage Sibling::LoaderVideo::blurred() {
	if (const auto image = _media->thumbnailInline()) {
		return image->original();
	}
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(ratio, ratio, QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::black);
	result.setDevicePixelRatio(ratio);
	return result;
}

QImage Sibling::LoaderVideo::good() {
	if (const auto image = _media->goodThumbnail()) {
		return image->original();
	} else if (!_video->goodThumbnailChecked()) {
		if (!_checkingGoodInCache) {
			waitForGoodThumbnail();
		}
	} else if (_failed) {
		return QImage();
	} else if (!_streamed) {
		_streamed = std::make_unique<Streaming::Instance>(
			_video,
			_origin,
			[] {}); // waitingCallback
		_streamed->lockPlayer();
		_streamed->player().updates(
		) | rpl::start_with_next_error([=](Streaming::Update &&update) {
			v::match(update.data, [&](Streaming::Information &update) {
				_update();
			}, [](const auto &update) {
			});
		}, [=](Streaming::Error &&error) {
			streamedFailed();
		}, _streamed->lifetime());
		if (_streamed->ready()) {
			_update();
		} else if (!_streamed->valid()) {
			streamedFailed();
		}
	} else if (_streamed->ready()) {
		return _streamed->info().video.cover;
	}
	return QImage();
}

void Sibling::LoaderVideo::streamedFailed() {
	_failed = true;
	_streamed = nullptr;
	_update();
}

void Sibling::LoaderVideo::waitForGoodThumbnail() {
	_checkingGoodInCache = true;
	const auto weak = make_weak(this);
	_video->owner().cache().get({}, [=](const auto &) {
		crl::on_main([=] {
			if (const auto strong = weak.get()) {
				if (!strong->updateAfterGoodCheck()) {
					strong->_video->session().downloaderTaskFinished(
					) | rpl::start_with_next([=] {
						strong->updateAfterGoodCheck();
					}, strong->_waitingGoodGeneration);
				}
			}
		});
	});
}

bool Sibling::LoaderVideo::updateAfterGoodCheck() {
	if (!_video->goodThumbnailChecked()) {
		return false;
	}
	_checkingGoodInCache = false;
	_waitingGoodGeneration.destroy();
	_update();
	return true;
}

Sibling::Sibling(
	not_null<Controller*> controller,
	const Data::StoriesList &list)
: _controller(controller)
, _id{ list.user, list.items.front().id } {
	const auto &item = list.items.front();
	const auto &data = item.media.data;
	const auto origin = Data::FileOrigin();
	if (const auto video = std::get_if<not_null<DocumentData*>>(&data)) {
		_loader = std::make_unique<LoaderVideo>((*video), origin, [=] {
			check();
		});
	} else if (const auto photo = std::get_if<not_null<PhotoData*>>(&data)) {
		_loader = std::make_unique<LoaderPhoto>((*photo), origin, [=] {
			check();
		});
	} else {
		Unexpected("Media type in stories list.");
	}
	_blurred = _loader->blurred();
	check();
	_goodShown.stop();
}

Sibling::~Sibling() = default;

Data::FullStoryId Sibling::shownId() const {
	return _id;
}

bool Sibling::shows(const Data::StoriesList &list) const {
	Expects(!list.items.empty());

	return _id == Data::FullStoryId{ list.user, list.items.front().id };
}

QImage Sibling::image() const {
	return _good.isNull() ? _blurred : _good;
}

void Sibling::check() {
	Expects(_loader != nullptr);

	auto good = _loader->good();
	if (good.isNull()) {
		return;
	}
	_loader = nullptr;
	_good = std::move(good);
	_goodShown.start([=] {
		_controller->repaintSibling(this);
	}, 0., 1., kGoodFadeDuration, anim::linear);
}

} // namespace Media::Stories
