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
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "media/stories/media_stories_controller.h"
#include "media/stories/media_stories_view.h"
#include "media/streaming/media_streaming_instance.h"
#include "media/streaming/media_streaming_player.h"
#include "ui/painter.h"
#include "styles/style_media_view.h"

namespace Media::Stories {
namespace {

constexpr auto kGoodFadeDuration = crl::time(200);
constexpr auto kSiblingFade = 0.5;
constexpr auto kSiblingFadeOver = 0.4;
constexpr auto kSiblingNameOpacity = 0.8;
constexpr auto kSiblingNameOpacityOver = 1.;
constexpr auto kSiblingScaleOver = 0.05;

[[nodiscard]] StoryId LookupShownId(
		const Data::StoriesSource &source,
		StoryId suggestedId) {
	const auto i = suggestedId
		? source.ids.lower_bound(Data::StoryIdDates{ suggestedId })
		: end(source.ids);
	return (i != end(source.ids) && i->id == suggestedId)
		? suggestedId
		: source.toOpen().id;
}

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
	void createStreamedPlayer();
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
	} else if (!_video->goodThumbnailChecked()
		&& !_video->goodThumbnailNoData()) {
		if (!_checkingGoodInCache) {
			waitForGoodThumbnail();
		}
	} else if (_failed) {
		return QImage();
	} else if (!_streamed) {
		createStreamedPlayer();
	} else if (_streamed->ready()) {
		return _streamed->info().video.cover;
	}
	return QImage();
}

void Sibling::LoaderVideo::createStreamedPlayer() {
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
	} else if (!_streamed->player().active()
		&& !_streamed->player().finished()) {
		_streamed->play({
			.mode = Streaming::Mode::Video,
		});
		_streamed->pause();
	}
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
	if (!_video->goodThumbnailChecked()
		&& !_video->goodThumbnailNoData()) {
		return false;
	}
	_checkingGoodInCache = false;
	_waitingGoodGeneration.destroy();
	_update();
	return true;
}

Sibling::Sibling(
	not_null<Controller*> controller,
	const Data::StoriesSource &source,
	StoryId suggestedId)
: _controller(controller)
, _id{ source.peer->id, LookupShownId(source, suggestedId) }
, _peer(source.peer) {
	checkStory();
	_goodShown.stop();
}

Sibling::~Sibling() = default;

void Sibling::checkStory() {
	const auto maybeStory = _peer->owner().stories().lookup(_id);
	if (!maybeStory) {
		if (_blurred.isNull()) {
			setBlackThumbnail();
			if (maybeStory.error() == Data::NoStory::Unknown) {
				_peer->owner().stories().resolve(_id, crl::guard(this, [=] {
					checkStory();
				}));
			}
		}
		return;
	}
	const auto story = *maybeStory;
	const auto origin = Data::FileOrigin();
	v::match(story->media().data, [&](not_null<PhotoData*> photo) {
		_loader = std::make_unique<LoaderPhoto>(photo, origin, [=] {
			check();
		});
	}, [&](not_null<DocumentData*> document) {
		_loader = std::make_unique<LoaderVideo>(document, origin, [=] {
			check();
		});
	}, [&](v::null_t) {
		_loader = nullptr;
	});
	if (!_loader) {
		setBlackThumbnail();
		return;
	}
	_blurred = _loader->blurred();
	check();
}

void Sibling::setBlackThumbnail() {
	_blurred = QImage(
		st::storiesMaxSize,
		QImage::Format_ARGB32_Premultiplied);
	_blurred.fill(Qt::black);
}

FullStoryId Sibling::shownId() const {
	return _id;
}

not_null<PeerData*> Sibling::peer() const {
	return _peer;
}

bool Sibling::shows(
		const Data::StoriesSource &source,
		StoryId suggestedId) const {
	const auto fullId = FullStoryId{
		source.peer->id,
		LookupShownId(source, suggestedId),
	};
	return (_id == fullId);
}

SiblingView Sibling::view(const SiblingLayout &layout, float64 over) {
	const auto name = nameImage(layout);
	return {
		.image = _good.isNull() ? _blurred : _good,
		.layout = {
			.geometry = layout.geometry,
			.fade = kSiblingFade * (1 - over) + kSiblingFadeOver * over,
			.radius = st::storiesRadius,
		},
		.userpic = userpicImage(layout),
		.userpicPosition = layout.userpic.topLeft(),
		.name = name,
		.namePosition = namePosition(layout, name),
		.nameOpacity = (kSiblingNameOpacity * (1 - over)
			+ kSiblingNameOpacityOver * over),
		.scale = 1. + (over * kSiblingScaleOver),
	};
}

QImage Sibling::userpicImage(const SiblingLayout &layout) {
	const auto ratio = style::DevicePixelRatio();
	const auto size = layout.userpic.width() * ratio;
	const auto key = _peer->userpicUniqueKey(_userpicView);
	if (_userpicImage.width() != size || _userpicKey != key) {
		_userpicKey = key;
		_userpicImage = _peer->generateUserpicImage(_userpicView, size);
		_userpicImage.setDevicePixelRatio(ratio);
	}
	return _userpicImage;
}

QImage Sibling::nameImage(const SiblingLayout &layout) {
	if (_nameFontSize != layout.nameFontSize) {
		_nameFontSize = layout.nameFontSize;

		const auto family = 0; // Default font family.
		const auto font = style::font(
			_nameFontSize,
			style::internal::FontSemibold,
			family);
		_name.reset();
		_nameStyle = std::make_unique<style::TextStyle>(style::TextStyle{
			.font = font,
			.linkFont = font,
			.linkFontOver = font,
		});
	};
	const auto text = _peer->isSelf()
		? tr::lng_stories_my_name(tr::now)
		: _peer->shortName();
	if (_nameText != text) {
		_name.reset();
		_nameText = text;
	}
	if (!_name) {
		_nameAvailableWidth = 0;
		_name.emplace(*_nameStyle, _nameText);
	}
	const auto available = layout.nameBoundingRect.width();
	const auto wasCut = (_nameAvailableWidth < _name->maxWidth());
	const auto nowCut = (available < _name->maxWidth());
	if (_nameImage.isNull()
		|| _nameAvailableWidth != layout.nameBoundingRect.width()) {
		_nameAvailableWidth = layout.nameBoundingRect.width();
		if (_nameImage.isNull() || nowCut || wasCut) {
			const auto w = std::min(_nameAvailableWidth, _name->maxWidth());
			const auto h = _nameStyle->font->height;
			const auto ratio = style::DevicePixelRatio();
			_nameImage = QImage(
				QSize(w, h) * ratio,
				QImage::Format_ARGB32_Premultiplied);
			_nameImage.setDevicePixelRatio(ratio);
			_nameImage.fill(Qt::transparent);
			auto p = Painter(&_nameImage);
			auto hq = PainterHighQualityEnabler(p);
			p.setFont(_nameStyle->font);
			p.setPen(Qt::white);
			_name->drawLeftElided(p, 0, 0, w, w);
		}
	}
	return _nameImage;
}

QPoint Sibling::namePosition(
		const SiblingLayout &layout,
		const QImage &image) const {
	const auto size = image.size() / image.devicePixelRatio();
	const auto width = size.width();
	const auto bounding = layout.nameBoundingRect;
	const auto left = layout.geometry.x()
		+ (layout.geometry.width() - width) / 2;
	const auto top = bounding.y() + bounding.height() - size.height();
	if (left < bounding.x()) {
		return { bounding.x(), top };
	} else if (left + width > bounding.x() + bounding.width()) {
		return { bounding.x() + bounding.width() - width, top };
	}
	return { left, top };
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
