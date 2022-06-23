/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/dialogs_video_userpic.h"

#include "core/file_location.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"

namespace Dialogs::Ui {

VideoUserpic::VideoUserpic(not_null<PeerData*> peer, Fn<void()> repaint)
: _peer(peer)
, _repaint(std::move(repaint)) {
}

VideoUserpic::~VideoUserpic() = default;

int VideoUserpic::frameIndex() const {
	return -1;
}

void VideoUserpic::paintLeft(
		Painter &p,
		std::shared_ptr<Data::CloudImageView> &view,
		int x,
		int y,
		int w,
		int size,
		bool paused) {
	_lastSize = size;

	const auto photoId = _peer->userpicPhotoId();
	if (_videoPhotoId != photoId) {
		_videoPhotoId = photoId;
		_video = nullptr;
		_videoPhotoMedia = nullptr;
		const auto photo = _peer->owner().photo(photoId);
		if (photo->isNull()) {
			_peer->updateFullForced();
		} else {
			_videoPhotoMedia = photo->createMediaView();
			_videoPhotoMedia->videoWanted(
				Data::PhotoSize::Small,
				_peer->userpicPhotoOrigin());
		}
	}
	if (!_video) {
		if (!_videoPhotoMedia) {
			const auto photo = _peer->owner().photo(photoId);
			if (!photo->isNull()) {
				_videoPhotoMedia = photo->createMediaView();
				_videoPhotoMedia->videoWanted(
					Data::PhotoSize::Small,
					_peer->userpicPhotoOrigin());
			}
		}
		if (_videoPhotoMedia) {
			auto small = _videoPhotoMedia->videoContent(
				Data::PhotoSize::Small);
			auto bytes = small.isEmpty()
				? _videoPhotoMedia->videoContent(Data::PhotoSize::Large)
				: small;
			if (!bytes.isEmpty()) {
				auto callback = [=](Media::Clip::Notification notification) {
					clipCallback(notification);
				};
				_video = Media::Clip::MakeReader(
					Core::FileLocation(),
					std::move(bytes),
					std::move(callback));
			}
		}
	}
	if (rtl()) {
		x = w - x - size;
	}
	if (_video && _video->ready()) {
		startReady();

		const auto now = paused ? crl::time(0) : crl::now();
		p.drawPixmap(x, y, _video->current(request(size), now));
	} else {
		_peer->paintUserpicLeft(p, view, x, y, w, size);
	}
}

Media::Clip::FrameRequest VideoUserpic::request(int size) const {
	return {
		.frame = { size, size },
		.outer = { size, size },
		.factor = cIntRetinaFactor(),
		.radius = ImageRoundRadius::Ellipse,
	};
}

bool VideoUserpic::startReady(int size) {
	if (!_video->ready() || _video->started()) {
		return false;
	} else if (!_lastSize) {
		_lastSize = size ? size : _video->width();
	}
	_video->start(request(_lastSize));
	_repaint();
	return true;
}

void VideoUserpic::clipCallback(Media::Clip::Notification notification) {
	using namespace Media::Clip;

	switch (notification) {
	case Notification::Reinit: {
		if (_video->state() == State::Error) {
			_video.setBad();
		} else if (startReady()) {
			_repaint();
		}
	} break;

	case Notification::Repaint: _repaint(); break;
	}
}

void PaintUserpic(
		Painter &p,
		not_null<PeerData*> peer,
		Ui::VideoUserpic *videoUserpic,
		std::shared_ptr<Data::CloudImageView> &view,
		int x,
		int y,
		int outerWidth,
		int size,
		bool paused) {
	if (videoUserpic) {
		videoUserpic->paintLeft(p, view, x, y, outerWidth, size, paused);
	} else {
		peer->paintUserpicLeft(p, view, x, y, outerWidth, size);
	}
}

} // namespace Dialogs::Ui
