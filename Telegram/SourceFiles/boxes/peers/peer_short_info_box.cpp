/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/peer_short_info_box.h"

#include "ui/widgets/labels.h"
#include "ui/image/image_prepare.h"
#include "media/streaming/media_streaming_instance.h"
#include "media/streaming/media_streaming_player.h"
#include "lang/lang_keys.h"
#include "styles/style_layers.h"
#include "styles/style_info.h"

namespace {

} // namespace

PeerShortInfoBox::PeerShortInfoBox(
	QWidget*,
	PeerShortInfoType type,
	rpl::producer<PeerShortInfoFields> fields,
	rpl::producer<QString> status,
	rpl::producer<PeerShortInfoUserpic> userpic,
	Fn<bool()> videoPaused)
: _type(type)
, _fields(std::move(fields))
, _name(this, nameValue(), st::shortInfoName)
, _status(this, std::move(status), st::shortInfoStatus)
, _videoPaused(std::move(videoPaused)) {
	std::move(
		userpic
	) | rpl::start_with_next([=](PeerShortInfoUserpic &&value) {
		applyUserpic(std::move(value));
	}, lifetime());
}

PeerShortInfoBox::~PeerShortInfoBox() = default;

rpl::producer<> PeerShortInfoBox::openRequests() const {
	return _openRequests.events();
}

void PeerShortInfoBox::prepare() {
	addButton(tr::lng_close(), [=] { closeBox(); });

	// Perhaps a new lang key should be added for opening a group.
	addLeftButton((_type == PeerShortInfoType::User)
		? tr::lng_profile_send_message()
		: (_type == PeerShortInfoType::Group)
		? tr::lng_view_button_group()
		: tr::lng_profile_view_channel(), [=] { _openRequests.fire({}); });

	setNoContentMargin(true);
	setDimensions(st::shortInfoWidth, st::shortInfoWidth);
}

RectParts PeerShortInfoBox::customCornersFilling() {
	return RectPart::FullTop;
}

void PeerShortInfoBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);
}

void PeerShortInfoBox::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	checkStreamedIsStarted();
	const auto frame = currentVideoFrame();
	auto paused = _videoPaused && _videoPaused();
	if (frame.isNull() && _userpicImage.isNull()) {
		auto image = QImage(
			coverRect().size() * style::DevicePixelRatio(),
			QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::black);
		Images::prepareRound(
			image,
			ImageRoundRadius::Small,
			RectPart::TopLeft | RectPart::TopRight);
		_userpicImage = std::move(image);
	}
	p.drawImage(
		coverRect(),
		frame.isNull() ? _userpicImage : frame);
	if (_videoInstance && _videoInstance->ready() && !paused) {
		_videoInstance->markFrameShown();
	}
}

QImage PeerShortInfoBox::currentVideoFrame() const {
	const auto coverSize = st::shortInfoWidth;
	const auto size = QSize(coverSize, coverSize);
	const auto request = Media::Streaming::FrameRequest{
		.resize = size * style::DevicePixelRatio(),
		.outer = size,
		.radius = ImageRoundRadius::Small,
		.corners = RectPart::TopLeft | RectPart::TopRight,
	};
	return (_videoInstance
		&& _videoInstance->player().ready()
		&& !_videoInstance->player().videoSize().isEmpty())
		? _videoInstance->frame(request)
		: QImage();
}

rpl::producer<QString> PeerShortInfoBox::nameValue() const {
	return _fields.value() | rpl::map([](const PeerShortInfoFields &fields) {
		return fields.name;
	}) | rpl::distinct_until_changed();
}

void PeerShortInfoBox::applyUserpic(PeerShortInfoUserpic &&value) {
	if (!value.photo.isNull()
		&& _userpicImage.cacheKey() != value.photo.cacheKey()) {
		_userpicImage = std::move(value.photo);
		update();
	}
	if (value.videoDocument
		&& (!_videoInstance
			|| _videoInstance->shared() != value.videoDocument)) {
		const auto frame = currentVideoFrame();
		if (!frame.isNull()) {
			_userpicImage = frame;
		}
		using namespace Media::Streaming;
		_videoInstance = std::make_unique<Instance>(
			std::move(value.videoDocument),
			[=] { videoWaiting(); });
		_videoStartPosition = value.videoStartPosition;
		_videoInstance->lockPlayer();
		_videoInstance->player().updates(
		) | rpl::start_with_next_error([=](Update &&update) {
			handleStreamingUpdate(std::move(update));
		}, [=](Error &&error) {
			handleStreamingError(std::move(error));
		}, _videoInstance->lifetime());
		if (_videoInstance->ready()) {
			streamingReady(base::duplicate(_videoInstance->info()));
		}
		if (!_videoInstance->valid()) {
			_videoInstance = nullptr;
		}
	}
}

void PeerShortInfoBox::checkStreamedIsStarted() {
	if (!_videoInstance) {
		return;
	} else if (_videoInstance->paused()) {
		_videoInstance->resume();
	}
	if (!_videoInstance
		|| _videoInstance->active()
		|| _videoInstance->failed()) {
		return;
	}
	auto options = Media::Streaming::PlaybackOptions();
	options.position = _videoStartPosition;
	options.mode = Media::Streaming::Mode::Video;
	options.loop = true;
	_videoInstance->play(options);
}

void PeerShortInfoBox::handleStreamingUpdate(
		Media::Streaming::Update &&update) {
	using namespace Media::Streaming;

	v::match(update.data, [&](Information &update) {
		streamingReady(std::move(update));
	}, [&](const PreloadedVideo &update) {
	}, [&](const UpdateVideo &update) {
		this->update(coverRect());
	}, [&](const PreloadedAudio &update) {
	}, [&](const UpdateAudio &update) {
	}, [&](const WaitingForData &update) {
	}, [&](MutedByOther) {
	}, [&](Finished) {
	});
}

void PeerShortInfoBox::handleStreamingError(
		Media::Streaming::Error &&error) {
	//_streamedPhoto->setVideoPlaybackFailed();
	//_streamedPhoto = nullptr;
	_videoInstance = nullptr;
}

void PeerShortInfoBox::streamingReady(Media::Streaming::Information &&info) {
	update(coverRect());
}

QRect PeerShortInfoBox::coverRect() const {
	return QRect(0, 0, st::shortInfoWidth, st::shortInfoWidth);
}

QRect PeerShortInfoBox::radialRect() const {
	const auto cover = coverRect();
	return cover;
}

void PeerShortInfoBox::videoWaiting() {
	if (!anim::Disabled()) {
		update(radialRect());
	}
}
