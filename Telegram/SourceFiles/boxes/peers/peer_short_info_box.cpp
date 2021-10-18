/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/peer_short_info_box.h"

#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/wrap.h"
#include "ui/image/image_prepare.h"
#include "ui/text/text_utilities.h"
#include "info/profile/info_profile_text.h"
#include "media/streaming/media_streaming_instance.h"
#include "media/streaming/media_streaming_player.h"
#include "lang/lang_keys.h"
#include "styles/style_layers.h"
#include "styles/style_info.h"

namespace {

constexpr auto kShadowMaxAlpha = 80;

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
, _scroll(this, st::defaultSolidScroll)
, _rows(
	static_cast<Ui::VerticalLayout*>(
		_scroll->setOwnedWidget(
			object_ptr<Ui::OverrideMargins>(
				_scroll.data(),
				object_ptr<Ui::VerticalLayout>(
					_scroll.data())))->entity()))
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

	prepareRows();

	setNoContentMargin(true);

	_rows->move(0, 0);
	_rows->heightValue(
	) | rpl::start_with_next([=](int height) {
		setDimensions(st::shortInfoWidth, st::shortInfoWidth + height);
	}, lifetime());
}

void PeerShortInfoBox::prepareRows() {
	using namespace Info::Profile;

	auto addInfoLineGeneric = [&](
			rpl::producer<QString> &&label,
			rpl::producer<TextWithEntities> &&text,
			const style::FlatLabel &textSt = st::infoLabeled) {
		auto line = CreateTextWithLabel(
			_rows,
			rpl::duplicate(label) | Ui::Text::ToWithEntities(),
			rpl::duplicate(text),
			textSt,
			st::shortInfoLabeledPadding);
		_rows->add(std::move(line.wrap));

		rpl::combine(
			std::move(label),
			std::move(text)
		) | rpl::start_with_next([=] {
			_rows->resizeToWidth(st::shortInfoWidth);
		}, _rows->lifetime());

		//line.text->setClickHandlerFilter(infoClickFilter);
		return line.text;
	};
	auto addInfoLine = [&](
			rpl::producer<QString> &&label,
			rpl::producer<TextWithEntities> &&text,
			const style::FlatLabel &textSt = st::infoLabeled) {
		return addInfoLineGeneric(
			std::move(label),
			std::move(text),
			textSt);
	};
	auto addInfoOneLine = [&](
			rpl::producer<QString> &&label,
			rpl::producer<TextWithEntities> &&text,
			const QString &contextCopyText) {
		auto result = addInfoLine(
			std::move(label),
			std::move(text),
			st::infoLabeledOneLine);
		result->setDoubleClickSelectsParagraph(true);
		result->setContextCopyText(contextCopyText);
		return result;
	};
	addInfoOneLine(
		tr::lng_info_link_label(),
		linkValue(),
		tr::lng_context_copy_link(tr::now));
	addInfoOneLine(
		tr::lng_info_mobile_label(),
		phoneValue() | Ui::Text::ToWithEntities(),
		tr::lng_profile_copy_phone(tr::now));
	auto label = _fields.current().isBio
		? tr::lng_info_bio_label()
		: tr::lng_info_about_label();
	addInfoLine(std::move(label), aboutValue());
	addInfoOneLine(
		tr::lng_info_username_label(),
		usernameValue() | Ui::Text::ToWithEntities(),
		tr::lng_context_copy_mention(tr::now));

}

RectParts PeerShortInfoBox::customCornersFilling() {
	return RectPart::FullTop;
}

void PeerShortInfoBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_name->moveToLeft(
		st::shortInfoNamePosition.x(),
		st::shortInfoWidth - st::shortInfoNamePosition.y() - _name->height(),
		width());
	_status->moveToLeft(
		st::shortInfoStatusPosition.x(),
		(st::shortInfoWidth
			- st::shortInfoStatusPosition.y()
			- _status->height()),
		height());
	_rows->resizeToWidth(st::shortInfoWidth);
	_scroll->resize(st::shortInfoWidth, height() - st::shortInfoWidth);
	_scroll->move(0, st::shortInfoWidth);
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

	if (_shadow.isNull()) {
		_shadow = Images::GenerateShadow(
			st::shortInfoShadowHeight,
			0,
			kShadowMaxAlpha);
	}
	const auto shadowRect = QRect(
		0,
		st::shortInfoWidth - st::shortInfoShadowHeight,
		st::shortInfoWidth,
		st::shortInfoShadowHeight);
	const auto factor = style::DevicePixelRatio();
	p.drawImage(
		shadowRect,
		_shadow,
		QRect(
			0,
			0,
			_shadow.width(),
			st::shortInfoShadowHeight * factor));
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

rpl::producer<TextWithEntities> PeerShortInfoBox::linkValue() const {
	return _fields.value() | rpl::map([](const PeerShortInfoFields &fields) {
		return Ui::Text::Link(fields.link, fields.link);
	}) | rpl::distinct_until_changed();
}

rpl::producer<QString> PeerShortInfoBox::phoneValue() const {
	return _fields.value() | rpl::map([](const PeerShortInfoFields &fields) {
		return fields.phone;
	}) | rpl::distinct_until_changed();
}

rpl::producer<QString> PeerShortInfoBox::usernameValue() const {
	return _fields.value() | rpl::map([](const PeerShortInfoFields &fields) {
		return fields.username;
	}) | rpl::distinct_until_changed();
}

rpl::producer<TextWithEntities> PeerShortInfoBox::aboutValue() const {
	return _fields.value() | rpl::map([](const PeerShortInfoFields &fields) {
		return fields.about;
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
