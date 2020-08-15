/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_userpic.h"

#include "data/data_peer.h"
#include "main/main_session.h"
#include "data/data_changes.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_cloud_file.h"
#include "data/data_photo_media.h"
#include "data/data_file_origin.h"
#include "ui/empty_userpic.h"
#include "apiwrap.h" // requestFullPeer.
#include "styles/style_calls.h"

namespace Calls {
namespace {

} // namespace

Userpic::Userpic(
	not_null<QWidget*> parent,
	not_null<PeerData*> peer,
	rpl::producer<bool> muted)
: _content(parent)
, _peer(peer) {
	setGeometry(0, 0, 0);
	setup(std::move(muted));
}

Userpic::~Userpic() = default;

void Userpic::setVisible(bool visible) {
	_content.setVisible(visible);
}

void Userpic::setGeometry(int x, int y, int size) {
	if (this->size() != size) {
		_userPhoto = QPixmap();
		_userPhotoFull = false;
	}
	_content.setGeometry(x, y, size, size);
	_content.update();
	if (_userPhoto.isNull()) {
		refreshPhoto();
	}
}

void Userpic::setup(rpl::producer<bool> muted) {
	_content.show();
	_content.setAttribute(Qt::WA_TransparentForMouseEvents);

	_content.paintRequest(
	) | rpl::start_with_next([=] {
		paint();
	}, lifetime());

	std::move(
		muted
	) | rpl::start_with_next([=](bool muted) {
		setMuted(muted);
	}, lifetime());

	_peer->session().changes().peerFlagsValue(
		_peer,
		Data::PeerUpdate::Flag::Photo
	) | rpl::start_with_next([=] {
		processPhoto();
	}, lifetime());

	_peer->session().downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		refreshPhoto();
	}, lifetime());

	_mutedAnimation.stop();
}

void Userpic::setMuteLayout(QPoint position, int size, int stroke) {
	_mutePosition = position;
	_muteSize = size;
	_muteStroke = stroke;
	_content.update();
}

void Userpic::paint() {
	Painter p(&_content);

	p.drawPixmap(0, 0, _userPhoto);
	if (_muted && _muteSize > 0) {
		auto hq = PainterHighQualityEnabler(p);
		auto pen = st::callBgOpaque->p;
		pen.setWidth(_muteStroke);
		p.setPen(pen);
		p.setBrush(st::callHangupBg);
		const auto rect = QRect(
			_mutePosition.x() - _muteSize / 2,
			_mutePosition.y() - _muteSize / 2,
			_muteSize,
			_muteSize);
		p.drawEllipse(rect);
		st::callMutedPeerIcon.paintInCenter(p, rect);
	}
}

void Userpic::setMuted(bool muted) {
	if (_muted == muted) {
		return;
	}
	_muted = muted;
	_content.update();
	//_mutedAnimation.start(
	//	[=] { _content.update(); },
	//	_muted ? 0. : 1.,
	//	_muted ? 1. : 0.,
	//	st::fadeWrapDuration);
}

int Userpic::size() const {
	return _content.width();
}

void Userpic::processPhoto() {
	_userpic = _peer->createUserpicView();
	_peer->loadUserpic();
	const auto photo = _peer->userpicPhotoId()
		? _peer->owner().photo(_peer->userpicPhotoId()).get()
		: nullptr;
	if (isGoodPhoto(photo)) {
		_photo = photo->createMediaView();
		_photo->wanted(Data::PhotoSize::Thumbnail, _peer->userpicPhotoOrigin());
	} else {
		_photo = nullptr;
		if (_peer->userpicPhotoUnknown() || (photo && !photo->date)) {
			_peer->session().api().requestFullPeer(_peer);
		}
	}
	refreshPhoto();
}

void Userpic::refreshPhoto() {
	if (!size()) {
		return;
	}
	const auto isNewBigPhoto = [&] {
		return _photo
			&& (_photo->image(Data::PhotoSize::Thumbnail) != nullptr)
			&& (_photo->owner()->id != _userPhotoId || !_userPhotoFull);
	}();
	if (isNewBigPhoto) {
		_userPhotoId = _photo->owner()->id;
		_userPhotoFull = true;
		createCache(_photo->image(Data::PhotoSize::Thumbnail));
	} else if (_userPhoto.isNull()) {
		createCache(_userpic ? _userpic->image() : nullptr);
	}
}

void Userpic::createCache(Image *image) {
	const auto size = this->size();
	const auto real = size * cIntRetinaFactor();
	auto options = Images::Option::Smooth | Images::Option::Circled;
	// _useTransparency ? (Images::Option::RoundedLarge | Images::Option::RoundedTopLeft | Images::Option::RoundedTopRight | Images::Option::Smooth) : Images::Option::None;
	if (image) {
		auto width = image->width();
		auto height = image->height();
		if (width > height) {
			width = qMax((width * real) / height, 1);
			height = real;
		} else {
			height = qMax((height * real) / width, 1);
			width = real;
		}
		_userPhoto = image->pixNoCache(
			width,
			height,
			options,
			size,
			size);
		_userPhoto.setDevicePixelRatio(cRetinaFactor());
	} else {
		auto filled = QImage(QSize(real, real), QImage::Format_ARGB32_Premultiplied);
		filled.setDevicePixelRatio(cRetinaFactor());
		filled.fill(Qt::transparent);
		{
			Painter p(&filled);
			Ui::EmptyUserpic(
				Data::PeerUserpicColor(_peer->id),
				_peer->name
			).paint(p, 0, 0, size, size);
		}
		//Images::prepareRound(filled, ImageRoundRadius::Large, RectPart::TopLeft | RectPart::TopRight);
		_userPhoto = Images::PixmapFast(std::move(filled));
	}

	_content.update();
}

bool Userpic::isGoodPhoto(PhotoData *photo) const {
	if (!photo || photo->isNull()) {
		return false;
	}
	const auto badAspect = [](int a, int b) {
		return a > 10 * b;
	};
	const auto width = photo->width();
	const auto height = photo->height();
	return !badAspect(width, height) && !badAspect(height, width);
}

} // namespace Calls
