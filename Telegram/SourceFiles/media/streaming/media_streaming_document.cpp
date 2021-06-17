/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/streaming/media_streaming_document.h"

#include "media/streaming/media_streaming_instance.h"
#include "media/streaming/media_streaming_loader.h"
#include "media/streaming/media_streaming_reader.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_photo.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "main/main_session.h"
#include "storage/file_download.h" // Storage::kMaxFileInMemory.
#include "styles/style_widgets.h"

#include <QtCore/QBuffer>

namespace Media {
namespace Streaming {
namespace {

constexpr auto kWaitingFastDuration = crl::time(200);
constexpr auto kWaitingShowDuration = crl::time(500);
constexpr auto kWaitingShowDelay = crl::time(500);
constexpr auto kGoodThumbnailQuality = 87;

} // namespace

Document::Document(
	not_null<DocumentData*> document,
	std::shared_ptr<Reader> reader)
: Document(std::move(reader), document, nullptr) {
	_player.fullInCache(
	) | rpl::start_with_next([=](bool fullInCache) {
		_document->setLoadedInMediaCache(fullInCache);
	}, _player.lifetime());
}

Document::Document(
	not_null<PhotoData*> photo,
	std::shared_ptr<Reader> reader)
: Document(std::move(reader), nullptr, photo) {
}

Document::Document(std::unique_ptr<Loader> loader)
: Document(std::make_shared<Reader>(std::move(loader)), nullptr, nullptr) {
}

Document::Document(
	std::shared_ptr<Reader> reader,
	DocumentData *document,
	PhotoData *photo)
: _document(document)
, _photo(photo)
, _player(std::move(reader))
, _radial(
		[=] { waitingCallback(); },
		st::defaultInfiniteRadialAnimation) {
	_player.updates(
	) | rpl::start_with_next_error([=](Update &&update) {
		handleUpdate(std::move(update));
	}, [=](Streaming::Error &&error) {
		handleError(std::move(error));
	}, _player.lifetime());
}

Player &Document::player() {
	return _player;
}

const Player &Document::player() const {
	return _player;
}

const Information &Document::info() const {
	return _info;
}

void Document::play(const PlaybackOptions &options) {
	_player.play(options);
	_info.audio.state.position
		= _info.video.state.position
		= options.position;
	waitingChange(true);
}

void Document::saveFrameToCover() {
	_info.video.cover = _player.ready()
		? _player.currentFrameImage()
		: _info.video.cover;
}

void Document::registerInstance(not_null<Instance*> instance) {
	_instances.emplace(instance);
}

void Document::unregisterInstance(not_null<Instance*> instance) {
	_instances.remove(instance);
	_player.unregisterInstance(instance);
	refreshPlayerPriority();
}

void Document::refreshPlayerPriority() {
	if (_instances.empty()) {
		return;
	}
	const auto max = ranges::max_element(
		_instances,
		ranges::less(),
		&Instance::priority);
	_player.setLoaderPriority((*max)->priority());
}

bool Document::waitingShown() const {
	if (!_fading.animating() && !_waiting) {
		_radial.stop(anim::type::instant);
		return false;
	}
	return _radial.animating();
}

float64 Document::waitingOpacity() const {
	return _fading.value(_waiting ? 1. : 0.);
}

Ui::RadialState Document::waitingState() const {
	return _radial.computeState();
}

void Document::handleUpdate(Update &&update) {
	v::match(update.data, [&](Information &update) {
		ready(std::move(update));
	}, [&](const PreloadedVideo &update) {
		_info.video.state.receivedTill = update.till;
	}, [&](const UpdateVideo &update) {
		_info.video.state.position = update.position;
	}, [&](const PreloadedAudio &update) {
		_info.audio.state.receivedTill = update.till;
	}, [&](const UpdateAudio &update) {
		_info.audio.state.position = update.position;
	}, [&](const WaitingForData &update) {
		waitingChange(update.waiting);
	}, [&](MutedByOther) {
	}, [&](Finished) {
		const auto finishTrack = [](TrackState &state) {
			state.position = state.receivedTill = state.duration;
		};
		finishTrack(_info.audio.state);
		finishTrack(_info.video.state);
	});
}

void Document::handleError(Error &&error) {
	if (_document) {
		if (error == Error::NotStreamable) {
			_document->setNotSupportsStreaming();
		} else if (error == Error::OpenFailed) {
			_document->setInappPlaybackFailed();
		}
	} else if (_photo) {
		if (error == Error::NotStreamable || error == Error::OpenFailed) {
			_photo->setVideoPlaybackFailed();
		}
	}
	waitingChange(false);
}

void Document::ready(Information &&info) {
	_info = std::move(info);
	validateGoodThumbnail();
	waitingChange(false);
}

void Document::waitingChange(bool waiting) {
	if (_waiting == waiting) {
		return;
	}
	_waiting = waiting;
	const auto fade = [=](crl::time duration) {
		if (!_radial.animating()) {
			_radial.start(
				st::defaultInfiniteRadialAnimation.sineDuration);
		}
		_fading.start(
			[=] { waitingCallback(); },
			_waiting ? 0. : 1.,
			_waiting ? 1. : 0.,
			duration);
	};
	if (waiting) {
		if (_radial.animating()) {
			_timer.cancel();
			fade(kWaitingFastDuration);
		} else {
			_timer.callOnce(kWaitingShowDelay);
			_timer.setCallback([=] {
				fade(kWaitingShowDuration);
			});
		}
	} else {
		_timer.cancel();
		if (_radial.animating()) {
			fade(kWaitingFastDuration);
		}
	}
}

void Document::validateGoodThumbnail() {
	if (_info.video.cover.isNull()
		|| !_document
		|| _document->goodThumbnailChecked()) {
		return;
	}
	const auto document = _document;
	const auto information = _info.video;
	const auto key = document->goodThumbnailCacheKey();
	const auto guard = base::make_weak(&document->session());
	document->owner().cache().get(key, [=](QByteArray value) {
		if (!value.isEmpty()) {
			return;
		}
		const auto image = [&] {
			auto result = information.cover;
			if (information.rotation != 0) {
				auto transform = QTransform();
				transform.rotate(information.rotation);
				result = result.transformed(transform);
			}
			if (result.size() != information.size) {
				result = result.scaled(
					information.size,
					Qt::IgnoreAspectRatio,
					Qt::SmoothTransformation);
			}
			return result;
		}();
		auto bytes = QByteArray();
		{
			auto buffer = QBuffer(&bytes);
			image.save(&buffer, "JPG", kGoodThumbnailQuality);
		}
		const auto length = bytes.size();
		if (!length || length > Storage::kMaxFileInMemory) {
			LOG(("App Error: Bad thumbnail data for saving to cache."));
			bytes = "(failed)";
		}
		crl::on_main(guard, [=] {
			if (const auto active = document->activeMediaView()) {
				active->setGoodThumbnail(image);
			}
			document->owner().cache().putIfEmpty(
				document->goodThumbnailCacheKey(),
				Storage::Cache::Database::TaggedValue(
					base::duplicate(bytes),
					Data::kImageCacheTag));
		});
	});
}

void Document::waitingCallback() {
	for (const auto &instance : _instances) {
		instance->callWaitingCallback();
	}
}

} // namespace Streaming
} // namespace Media
