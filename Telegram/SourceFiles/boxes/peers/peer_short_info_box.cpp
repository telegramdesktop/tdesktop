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
constexpr auto kInactiveBarOpacity = 0.5;

} // namespace

struct PeerShortInfoBox::CustomLabelStyle {
	explicit CustomLabelStyle(const style::FlatLabel &original);

	style::complex_color textFg;
	style::FlatLabel st;
	float64 opacity = 1.;
};

PeerShortInfoBox::CustomLabelStyle::CustomLabelStyle(
	const style::FlatLabel &original)
: textFg([=, c = original.textFg]{
	auto result = c->c;
	result.setAlphaF(result.alphaF() * opacity);
	return result;
})
, st(original) {
	st.textFg = textFg.color();
}

PeerShortInfoBox::PeerShortInfoBox(
	QWidget*,
	PeerShortInfoType type,
	rpl::producer<PeerShortInfoFields> fields,
	rpl::producer<QString> status,
	rpl::producer<PeerShortInfoUserpic> userpic,
	Fn<bool()> videoPaused)
: _type(type)
, _fields(std::move(fields))
, _topRoundBackground(this)
, _scroll(this, st::shortInfoScroll)
, _rows(
	_scroll->setOwnedWidget(
		object_ptr<Ui::VerticalLayout>(
			_scroll.data())))
, _cover(_rows->add(object_ptr<Ui::RpWidget>(_rows.get())))
, _nameStyle(std::make_unique<CustomLabelStyle>(st::shortInfoName))
, _name(_cover.get(), nameValue(), _nameStyle->st)
, _statusStyle(std::make_unique<CustomLabelStyle>(st::shortInfoStatus))
, _status(_cover.get(), std::move(status), _statusStyle->st)
, _videoPaused(std::move(videoPaused)) {
	std::move(
		userpic
	) | rpl::start_with_next([=](PeerShortInfoUserpic &&value) {
		applyUserpic(std::move(value));
	}, lifetime());

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		refreshBarImages();
	}, lifetime());
}

PeerShortInfoBox::~PeerShortInfoBox() = default;

rpl::producer<> PeerShortInfoBox::openRequests() const {
	return _openRequests.events();
}

rpl::producer<int> PeerShortInfoBox::moveRequests() const {
	return _moveRequests.events();
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

	_cover->resize(st::shortInfoWidth, st::shortInfoWidth);
	_cover->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(_cover.get());
		paintCover(p);
	}, _cover->lifetime());
	_cover->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return (e->type() == QEvent::MouseButtonPress)
			|| (e->type() == QEvent::MouseButtonDblClick);
	}) | rpl::start_with_next([=](not_null<QEvent*> e) {
		const auto mouse = static_cast<QMouseEvent*>(e.get());
		const auto x = mouse->pos().x();
		if (mouse->button() != Qt::LeftButton) {
			return;
		} else if (/*_index > 0 && */x < st::shortInfoWidth / 3) {
			_moveRequests.fire(-1);
		} else if (/*_index + 1 < _count && */x >= st::shortInfoWidth / 3) {
			_moveRequests.fire(1);
		}
	}, _cover->lifetime());

	_topRoundBackground->resize(st::shortInfoWidth, st::boxRadius);
	_topRoundBackground->paintRequest(
	) | rpl::start_with_next([=] {
		if (const auto use = fillRoundedTopHeight()) {
			const auto width = _topRoundBackground->width();
			const auto top = _topRoundBackground->height() - use;
			const auto factor = style::DevicePixelRatio();
			QPainter(_topRoundBackground.data()).drawImage(
				QRect(0, top, width, use),
				_roundedTop,
				QRect(0, top * factor, width * factor, use * factor));
		}
	}, _topRoundBackground->lifetime());

	_roundedTop = QImage(
		_topRoundBackground->size() * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	_roundedTop.setDevicePixelRatio(style::DevicePixelRatio());
	_roundedTopImage = _roundedTop;
	_roundedTopImage.fill(Qt::transparent);
	refreshRoundedTopImage(getDelegate()->style().bg->c);

	setDimensionsToContent(st::shortInfoWidth, _rows);
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
		_rows->add(object_ptr<Ui::OverrideMargins>(
			_rows.get(),
			std::move(line.wrap)));

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
	_scroll->resize(st::shortInfoWidth, height());
	_scroll->move(0, 0);
	_topRoundBackground->move(0, 0);
}

void PeerShortInfoBox::paintCover(QPainter &p) {
	checkStreamedIsStarted();
	const auto frame = currentVideoFrame();
	auto paused = _videoPaused && _videoPaused();
	if (frame.isNull() && _userpicImage.isNull()) {
		auto image = QImage(
			_cover->size() * style::DevicePixelRatio(),
			QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::black);
		Images::prepareRound(
			image,
			ImageRoundRadius::Small,
			RectPart::TopLeft | RectPart::TopRight);
		_userpicImage = std::move(image);
	}

	paintCoverImage(p, frame.isNull() ? _userpicImage : frame);
	paintBars(p);
	paintShadow(p);
	if (_videoInstance && _videoInstance->ready() && !paused) {
		_videoInstance->markFrameShown();
	}
}

void PeerShortInfoBox::paintCoverImage(QPainter &p, const QImage &image) {
	const auto roundedWidth = _topRoundBackground->width();
	const auto roundedHeight = _topRoundBackground->height();
	const auto scrollTop = _scroll->scrollTop();
	const auto covered = (st::shortInfoWidth - scrollTop);
	if (covered <= 0) {
		return;
	} else if (!scrollTop) {
		p.drawImage(_cover->rect(), image);
		return;
	}
	const auto fill = covered - roundedHeight;
	const auto top = _cover->height() - fill;
	const auto factor = style::DevicePixelRatio();
	if (fill > 0) {
		p.drawImage(
			QRect(0, top, roundedWidth, fill),
			image,
			QRect(0, top * factor, roundedWidth * factor, fill * factor));
	}
	if (covered <= 0) {
		return;
	}
	const auto rounded = std::min(covered, roundedHeight);
	const auto from = top - rounded;
	auto q = QPainter(&_roundedTopImage);
	q.drawImage(
		QRect(0, 0, roundedWidth, rounded),
		image,
		QRect(0, from * factor, roundedWidth * factor, rounded * factor));
	q.end();
	Images::prepareRound(
		_roundedTopImage,
		ImageRoundRadius::Small,
		RectPart::TopLeft | RectPart::TopRight);
	p.drawImage(
		QRect(0, from, roundedWidth, rounded),
		_roundedTopImage,
		QRect(0, 0, roundedWidth * factor, rounded * factor));
}

int PeerShortInfoBox::fillRoundedTopHeight() {
	const auto roundedWidth = _topRoundBackground->width();
	const auto roundedHeight = _topRoundBackground->height();
	const auto scrollTop = _scroll->scrollTop();
	const auto covered = (st::shortInfoWidth - scrollTop);
	if (covered >= roundedHeight) {
		return 0;
	}
	const auto &color = getDelegate()->style().bg->c;
	if (_roundedTopColor != color) {
		refreshRoundedTopImage(color);
	}
	return roundedHeight - covered;
}

void PeerShortInfoBox::refreshRoundedTopImage(const QColor &color) {
	_roundedTopColor = color;
	_roundedTop.fill(color);
	Images::prepareRound(
		_roundedTop,
		ImageRoundRadius::Small,
		RectPart::TopLeft | RectPart::TopRight);
}

void PeerShortInfoBox::paintBars(QPainter &p) {
	const auto height = st::shortInfoLinePadding * 2 + st::shortInfoLine;
	const auto factor = style::DevicePixelRatio();
	if (_shadowTop.isNull()) {
		_shadowTop = Images::GenerateShadow(height, kShadowMaxAlpha, 0);
		_shadowTop = _shadowTop.scaled(
			QSize(st::shortInfoWidth, height) * factor);
		Images::prepareRound(
			_shadowTop,
			ImageRoundRadius::Small,
			RectPart::TopLeft | RectPart::TopRight);
	}
	const auto scrollTop = _scroll->scrollTop();
	const auto shadowRect = QRect(0, scrollTop, st::shortInfoWidth, height);
	p.drawImage(
		shadowRect,
		_shadowTop,
		QRect(0, 0, _shadowTop.width(), height * factor));
	const auto hiddenAt = st::shortInfoWidth - st::shortInfoNamePosition.y();
	if (!_smallWidth || scrollTop >= hiddenAt) {
		return;
	}
	const auto top = st::shortInfoLinePadding;
	const auto skip = st::shortInfoLineSkip;
	const auto full = (st::shortInfoWidth - 2 * top - (_count - 1) * skip);
	const auto width = full / float64(_count);
	const auto masterOpacity = 1. - (scrollTop / float64(hiddenAt));
	const auto inactiveOpacity = masterOpacity * kInactiveBarOpacity;
	for (auto i = 0; i != _count; ++i) {
		const auto left = top + i * (width + skip);
		const auto right = left + width;
		p.setOpacity((i == _index) ? masterOpacity : inactiveOpacity);
		p.drawImage(
			qRound(left),
			scrollTop + top,
			((qRound(right) == qRound(left) + _smallWidth)
				? _barSmall
				: _barLarge));
	}
	p.setOpacity(1.);
}

void PeerShortInfoBox::paintShadow(QPainter &p) {
	if (_shadowBottom.isNull()) {
		_shadowBottom = Images::GenerateShadow(
			st::shortInfoShadowHeight,
			0,
			kShadowMaxAlpha);
	}
	const auto scrollTop = _scroll->scrollTop();
	const auto shadowTop = st::shortInfoWidth - st::shortInfoShadowHeight;
	if (scrollTop >= shadowTop) {
		_name->hide();
		_status->hide();
		return;
	}
	const auto opacity = 1. - (scrollTop / float64(shadowTop));
	_nameStyle->opacity = opacity;
	_nameStyle->textFg.refresh();
	_name->show();
	_statusStyle->opacity = opacity;
	_statusStyle->textFg.refresh();
	_status->show();
	p.setOpacity(opacity);
	const auto shadowRect = QRect(
		0,
		shadowTop,
		st::shortInfoWidth,
		st::shortInfoShadowHeight);
	const auto factor = style::DevicePixelRatio();
	p.drawImage(
		shadowRect,
		_shadowBottom,
		QRect(
			0,
			0,
			_shadowBottom.width(),
			st::shortInfoShadowHeight * factor));
	p.setOpacity(1.);
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
	if (_index != value.index) {
		_index = value.index;
		update();
	}
	if (_count != value.count) {
		_count = value.count;
		refreshCoverCursor();
		refreshBarImages();
		update();
	}
	if (value.photo.isNull()) {
		const auto videoChanged = _videoInstance
			? (_videoInstance->shared() != value.videoDocument)
			: (value.videoDocument != nullptr);
		const auto frame = videoChanged ? currentVideoFrame() : QImage();
		if (!frame.isNull()) {
			_userpicImage = frame;
		}
	} else if (_userpicImage.cacheKey() != value.photo.cacheKey()) {
		_userpicImage = std::move(value.photo);
		update();
	}
	if (!value.videoDocument) {
		_videoInstance = nullptr;
	} else if (!_videoInstance
		|| _videoInstance->shared() != value.videoDocument) {
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
		_cover->update();
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
	_cover->update();
}

void PeerShortInfoBox::refreshCoverCursor() {
	const auto cursor = (_count > 1)
		? style::cur_pointer
		: style::cur_default;
	if (_cursor != cursor) {
		_cursor = cursor;
		_cover->setCursor(_cursor);
	}
}

void PeerShortInfoBox::refreshBarImages() {
	if (_count < 2) {
		_smallWidth = _largeWidth = 0;
		_barSmall = _barLarge = QImage();
		return;
	}
	const auto width = st::shortInfoWidth - 2 * st::shortInfoLinePadding;
	_smallWidth = (width - (_count - 1) * st::shortInfoLineSkip) / _count;
	if (_smallWidth < st::shortInfoLine) {
		_smallWidth = _largeWidth = 0;
		_barSmall = _barLarge = QImage();
		return;
	}
	_largeWidth = _smallWidth + 1;
	const auto makeBar = [](int size) {
		const auto radius = st::shortInfoLine / 2.;
		auto result = QImage(
			QSize(size, st::shortInfoLine) * style::DevicePixelRatio(),
			QImage::Format_ARGB32_Premultiplied);
		result.setDevicePixelRatio(style::DevicePixelRatio());
		result.fill(Qt::transparent);
		auto p = QPainter(&result);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::groupCallVideoTextFg);
		p.drawRoundedRect(0, 0, size, st::shortInfoLine, radius, radius);
		p.end();

		return result;
	};
	_barSmall = makeBar(_smallWidth);
	_barLarge = makeBar(_largeWidth);
}

QRect PeerShortInfoBox::radialRect() const {
	const auto cover = _cover->rect();
	return cover;
}

void PeerShortInfoBox::videoWaiting() {
	if (!anim::Disabled()) {
		update(radialRect());
	}
}
