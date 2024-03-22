/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/peer_short_info_box.h"

#include "ui/effects/radial_animation.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/wrap.h"
#include "ui/image/image_prepare.h"
#include "ui/text/text_utilities.h"
#include "ui/painter.h"
#include "info/profile/info_profile_text.h"
#include "info/profile/info_profile_values.h"
#include "media/streaming/media_streaming_instance.h"
#include "media/streaming/media_streaming_player.h"
#include "base/event_filter.h"
#include "lang/lang_keys.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_info.h"

namespace {

constexpr auto kShadowMaxAlpha = 80;
constexpr auto kInactiveBarOpacity = 0.5;

} // namespace

struct PeerShortInfoCover::CustomLabelStyle {
	explicit CustomLabelStyle(const style::FlatLabel &original);

	style::complex_color textFg;
	style::FlatLabel st;
	float64 opacity = 1.;
};

struct PeerShortInfoCover::Radial {
	explicit Radial(Fn<void()> &&callback);

	void toggle(bool visible);

	Ui::RadialAnimation radial;
	Ui::Animations::Simple shownAnimation;
	Fn<void()> callback;
	base::Timer showTimer;
	bool shown = false;
};

PeerShortInfoCover::Radial::Radial(Fn<void()> &&callback)
: radial(callback)
, callback(callback)
, showTimer([=] { toggle(true); }) {
}

void PeerShortInfoCover::Radial::toggle(bool visible) {
	if (shown == visible) {
		return;
	}
	shown = visible;
	shownAnimation.start(
		callback,
		shown ? 0. : 1.,
		shown ? 1. : 0.,
		st::fadeWrapDuration);
}

PeerShortInfoCover::CustomLabelStyle::CustomLabelStyle(
	const style::FlatLabel &original)
: textFg([=, c = original.textFg]{
	auto result = c->c;
	result.setAlphaF(result.alphaF() * opacity);
	return result;
})
, st(original) {
	st.textFg = textFg.color();
}

PeerShortInfoCover::PeerShortInfoCover(
	not_null<QWidget*> parent,
	const style::ShortInfoCover &st,
	rpl::producer<QString> name,
	rpl::producer<QString> status,
	rpl::producer<PeerShortInfoUserpic> userpic,
	Fn<bool()> videoPaused)
: _st(st)
, _owned(parent.get())
, _widget(_owned.data())
, _nameStyle(std::make_unique<CustomLabelStyle>(_st.name))
, _name(_widget.get(), std::move(name), _nameStyle->st)
, _statusStyle(std::make_unique<CustomLabelStyle>(_st.status))
, _status(_widget.get(), std::move(status), _statusStyle->st)
, _roundMask(Images::CornersMask(_st.radius))
, _videoPaused(std::move(videoPaused)) {
	_widget->setCursor(_cursor);

	_widget->resize(_st.size, _st.size);

	std::move(
		userpic
	) | rpl::start_with_next([=](PeerShortInfoUserpic &&value) {
		applyUserpic(std::move(value));
		applyAdditionalStatus(value.additionalStatus);
	}, lifetime());

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		refreshBarImages();
	}, lifetime());

	_widget->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(_widget.get());
		paint(p);
	}, lifetime());

	base::install_event_filter(_widget.get(), [=](not_null<QEvent*> e) {
		if (e->type() != QEvent::MouseButtonPress
			&& e->type() != QEvent::MouseButtonDblClick) {
			return base::EventFilterResult::Continue;
		}
		const auto mouse = static_cast<QMouseEvent*>(e.get());
		const auto x = mouse->pos().x();
		if (mouse->button() != Qt::LeftButton) {
			return base::EventFilterResult::Continue;
		} else if (/*_index > 0 && */x < _st.size / 3) {
			_moveRequests.fire(-1);
		} else if (/*_index + 1 < _count && */x >= _st.size / 3) {
			_moveRequests.fire(1);
		}
		e->accept();
		return base::EventFilterResult::Cancel;
	});

	refreshLabelsGeometry();

	_roundedTopImage = QImage(
		QSize(_st.size, _st.radius) * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	_roundedTopImage.setDevicePixelRatio(style::DevicePixelRatio());
	_roundedTopImage.fill(Qt::transparent);
}

PeerShortInfoCover::~PeerShortInfoCover() = default;

not_null<Ui::RpWidget*> PeerShortInfoCover::widget() const {
	return _widget;
}

object_ptr<Ui::RpWidget> PeerShortInfoCover::takeOwned() {
	return std::move(_owned);
}

gsl::span<const QImage, 4> PeerShortInfoCover::roundMask() const {
	return _roundMask;
}

void PeerShortInfoCover::setScrollTop(int scrollTop) {
	_scrollTop = scrollTop;
	_widget->update();
}

rpl::producer<int> PeerShortInfoCover::moveRequests() const {
	return _moveRequests.events();
}

rpl::lifetime &PeerShortInfoCover::lifetime() {
	return _widget->lifetime();
}

void PeerShortInfoCover::paint(QPainter &p) {
	checkStreamedIsStarted();
	auto frame = currentVideoFrame();
	auto paused = _videoPaused && _videoPaused();
	if (!frame.isNull()) {
		frame = Images::Round(
			std::move(frame),
			_roundMask,
			RectPart::TopLeft | RectPart::TopRight);
	} else if (_userpicImage.isNull()) {
		auto image = QImage(
			_widget->size() * style::DevicePixelRatio(),
			QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::black);
		_userpicImage = Images::Round(
			std::move(image),
			_roundMask,
			RectPart::TopLeft | RectPart::TopRight);
	}

	paintCoverImage(p, frame.isNull() ? _userpicImage : frame);
	paintBars(p);
	paintShadow(p);
	paintRadial(p);
	if (_videoInstance && _videoInstance->ready() && !paused) {
		_videoInstance->markFrameShown();
	}
}

void PeerShortInfoCover::paintCoverImage(QPainter &p, const QImage &image) {
	const auto roundedWidth = _st.size;
	const auto roundedHeight = _st.radius;
	const auto covered = (_st.size - _scrollTop);
	if (covered <= 0) {
		return;
	} else if (!_scrollTop) {
		p.drawImage(_widget->rect(), image);
		return;
	}
	const auto fill = covered - roundedHeight;
	const auto top = _widget->height() - fill;
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
	_roundedTopImage = Images::Round(
		std::move(_roundedTopImage),
		_roundMask,
		RectPart::TopLeft | RectPart::TopRight);
	p.drawImage(
		QRect(0, from, roundedWidth, rounded),
		_roundedTopImage,
		QRect(0, 0, roundedWidth * factor, rounded * factor));
}

void PeerShortInfoCover::paintBars(QPainter &p) {
	const auto height = _st.linePadding * 2 + _st.line;
	const auto factor = style::DevicePixelRatio();
	if (_shadowTop.isNull()) {
		_shadowTop = Images::GenerateShadow(height, kShadowMaxAlpha, 0);
		_shadowTop = Images::Round(
			_shadowTop.scaled(QSize(_st.size, height) * factor),
			_roundMask,
			RectPart::TopLeft | RectPart::TopRight);
	}
	const auto shadowRect = QRect(0, _scrollTop, _st.size, height);
	p.drawImage(
		shadowRect,
		_shadowTop,
		QRect(0, 0, _shadowTop.width(), height * factor));
	const auto hiddenAt = _st.size - _st.namePosition.y();
	if (!_smallWidth || _scrollTop >= hiddenAt) {
		return;
	}
	const auto start = _st.linePadding;
	const auto y = _scrollTop + start;
	const auto skip = _st.lineSkip;
	const auto full = (_st.size - 2 * start - (_count - 1) * skip);
	const auto single = full / float64(_count);
	const auto masterOpacity = 1. - (_scrollTop / float64(hiddenAt));
	const auto inactiveOpacity = masterOpacity * kInactiveBarOpacity;
	for (auto i = 0; i != _count; ++i) {
		const auto left = start + i * (single + skip);
		const auto right = left + single;
		const auto x = qRound(left);
		const auto small = (qRound(right) == qRound(left) + _smallWidth);
		const auto width = small ? _smallWidth : _largeWidth;
		const auto &image = small ? _barSmall : _barLarge;
		const auto min = 2 * ((_st.line + 1) / 2);
		const auto minProgress = min / float64(width);
		const auto videoProgress = (_videoInstance && _videoDuration > 0);
		const auto progress = (i != _index)
			? 0.
			: videoProgress
			? std::max(_videoPosition / float64(_videoDuration), minProgress)
			: (_videoInstance ? 0. : 1.);
		if (progress == 1. && !videoProgress) {
			p.setOpacity(masterOpacity);
			p.drawImage(x, y, image);
		} else {
			p.setOpacity(inactiveOpacity);
			p.drawImage(x, y, image);
			if (progress > 0.) {
				const auto paint = qRound(progress * width);
				const auto right = paint / 2;
				const auto left = paint - right;
				p.setOpacity(masterOpacity);
				p.drawImage(
					QRect(x, y, left, _st.line),
					image,
					QRect(0, 0, left * factor, image.height()));
				p.drawImage(
					QRect(x + left, y, right, _st.line),
					image,
					QRect(left * factor, 0, right * factor, image.height()));
			}
		}
	}
	p.setOpacity(1.);
}

void PeerShortInfoCover::paintShadow(QPainter &p) {
	if (_shadowBottom.isNull()) {
		_shadowBottom = Images::GenerateShadow(
			_st.shadowHeight,
			0,
			kShadowMaxAlpha);
	}
	const auto shadowTop = _st.size - _st.shadowHeight;
	if (_scrollTop >= shadowTop) {
		_name->hide();
		_status->hide();
		return;
	}
	const auto opacity = 1. - (_scrollTop / float64(shadowTop));
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
		_st.size,
		_st.shadowHeight);
	const auto factor = style::DevicePixelRatio();
	p.drawImage(
		shadowRect,
		_shadowBottom,
		QRect(
			0,
			0,
			_shadowBottom.width(),
			_st.shadowHeight * factor));
	p.setOpacity(1.);
}

void PeerShortInfoCover::paintRadial(QPainter &p) {
	const auto infinite = _videoInstance && _videoInstance->waitingShown();
	if (!_radial && !infinite) {
		return;
	}
	const auto radial = radialRect();
	const auto line = _st.radialAnimation.thickness;
	const auto arc = radial.marginsRemoved(
		{ line, line, line, line });
	const auto infiniteOpacity = _videoInstance
		? _videoInstance->waitingOpacity()
		: 0.;
	const auto radialState = _radial
		? _radial->radial.computeState()
		: Ui::RadialState();
	if (_radial) {
		updateRadialState();
	}
	const auto radialOpacity = _radial
		? (_radial->shownAnimation.value(_radial->shown ? 1. : 0.)
			* radialState.shown)
		: 0.;
	auto hq = PainterHighQualityEnabler(p);
	p.setOpacity(std::max(infiniteOpacity, radialOpacity));
	p.setPen(Qt::NoPen);
	p.setBrush(st::radialBg);
	p.drawEllipse(radial);
	if (radialOpacity > 0.) {
		p.setOpacity(radialOpacity);
		auto pen = _st.radialAnimation.color->p;
		pen.setWidth(line);
		pen.setCapStyle(Qt::RoundCap);
		p.setPen(pen);
		p.drawArc(arc, radialState.arcFrom, radialState.arcLength);
	}
	if (infinite) {
		p.setOpacity(1.);
		Ui::InfiniteRadialAnimation::Draw(
			p,
			_videoInstance->waitingState(),
			arc.topLeft(),
			arc.size(),
			_st.size,
			_st.radialAnimation.color,
			line);
	}
}

QImage PeerShortInfoCover::currentVideoFrame() const {
	const auto size = QSize(_st.size, _st.size);
	const auto request = Media::Streaming::FrameRequest{
		.resize = size * style::DevicePixelRatio(),
		.outer = size,
	};
	return (_videoInstance
		&& _videoInstance->player().ready()
		&& !_videoInstance->player().videoSize().isEmpty())
		? _videoInstance->frame(request)
		: QImage();
}

void PeerShortInfoCover::applyAdditionalStatus(const QString &status) {
	if (status.isEmpty()) {
		if (_additionalStatus) {
			_additionalStatus.destroy();
			refreshLabelsGeometry();
		}
		return;
	}
	if (_additionalStatus) {
		_additionalStatus->setText(status);
	} else {
		_additionalStatus.create(_widget.get(), status, _statusStyle->st);
		_additionalStatus->show();
		refreshLabelsGeometry();
	}
}

void PeerShortInfoCover::applyUserpic(PeerShortInfoUserpic &&value) {
	if (_index != value.index) {
		_index = value.index;
		_widget->update();
	}
	if (_count != value.count) {
		_count = value.count;
		refreshCoverCursor();
		refreshBarImages();
		_widget->update();
	}
	if (value.photo.isNull()) {
		const auto videoChanged = _videoInstance
			? (_videoInstance->shared() != value.videoDocument)
			: (value.videoDocument != nullptr);
		auto frame = videoChanged ? currentVideoFrame() : QImage();
		if (!frame.isNull()) {
			_userpicImage = Images::Round(
				std::move(frame),
				_roundMask,
				RectPart::TopLeft | RectPart::TopRight);
		}
	} else if (_userpicImage.cacheKey() != value.photo.cacheKey()) {
		_userpicImage = std::move(value.photo);
		_widget->update();
	}
	if (!value.videoDocument) {
		clearVideo();
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
			clearVideo();
		}
	}
	_photoLoadingProgress = value.photoLoadingProgress;
	updateRadialState();
}

void PeerShortInfoCover::updateRadialState() {
	const auto progress = _videoInstance ? 1. : _photoLoadingProgress;
	if (_radial) {
		_radial->radial.update(progress, (progress == 1.), crl::now());
	}
	_widget->update(radialRect());

	if (progress == 1.) {
		if (!_radial) {
			return;
		}
		_radial->showTimer.cancel();
		_radial->toggle(false);
		if (!_radial->shownAnimation.animating()) {
			_radial = nullptr;
		}
		return;
	} else if (!_radial) {
		_radial = std::make_unique<Radial>([=] { updateRadialState(); });
		_radial->radial.update(progress, false, crl::now());
		_radial->showTimer.callOnce(st::fadeWrapDuration);
		return;
	} else if (!_radial->showTimer.isActive()) {
		_radial->toggle(true);
	}
}

void PeerShortInfoCover::clearVideo() {
	_videoInstance = nullptr;
	_videoStartPosition = _videoPosition = _videoDuration = 0;
}

void PeerShortInfoCover::checkStreamedIsStarted() {
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

void PeerShortInfoCover::handleStreamingUpdate(
		Media::Streaming::Update &&update) {
	using namespace Media::Streaming;

	v::match(update.data, [&](Information &update) {
		streamingReady(std::move(update));
	}, [&](const PreloadedVideo &update) {
	}, [&](const UpdateVideo &update) {
		_videoPosition = update.position;
		_widget->update();
	}, [&](const PreloadedAudio &update) {
	}, [&](const UpdateAudio &update) {
	}, [&](const WaitingForData &update) {
	}, [&](MutedByOther) {
	}, [&](Finished) {
	});
}

void PeerShortInfoCover::handleStreamingError(
		Media::Streaming::Error &&error) {
	//_streamedPhoto->setVideoPlaybackFailed();
	//_streamedPhoto = nullptr;
	clearVideo();
}

void PeerShortInfoCover::streamingReady(Media::Streaming::Information &&info) {
	_videoPosition = info.video.state.position;
	_videoDuration = info.video.state.duration;
	_widget->update();
}

void PeerShortInfoCover::refreshCoverCursor() {
	const auto cursor = (_count > 1)
		? style::cur_pointer
		: style::cur_default;
	if (_cursor != cursor) {
		_cursor = cursor;
		_widget->setCursor(_cursor);
	}
}

void PeerShortInfoCover::refreshBarImages() {
	if (_count < 2) {
		_smallWidth = _largeWidth = 0;
		_barSmall = _barLarge = QImage();
		return;
	}
	const auto width = _st.size - 2 * _st.linePadding;
	_smallWidth = (width - (_count - 1) * _st.lineSkip) / _count;
	if (_smallWidth < _st.line) {
		_smallWidth = _largeWidth = 0;
		_barSmall = _barLarge = QImage();
		return;
	}
	_largeWidth = _smallWidth + 1;
	const auto makeBar = [&](int size) {
		const auto radius = _st.line / 2.;
		auto result = QImage(
			QSize(size, _st.line) * style::DevicePixelRatio(),
			QImage::Format_ARGB32_Premultiplied);
		result.setDevicePixelRatio(style::DevicePixelRatio());
		result.fill(Qt::transparent);
		auto p = QPainter(&result);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::groupCallVideoTextFg);
		p.drawRoundedRect(0, 0, size, _st.line, radius, radius);
		p.end();

		return result;
	};
	_barSmall = makeBar(_smallWidth);
	_barLarge = makeBar(_largeWidth);
}

void PeerShortInfoCover::refreshLabelsGeometry() {
	const auto statusTop = _st.size
		- _st.statusPosition.y()
		- _status->height();
	const auto diff = _st.namePosition.y()
		- _name->height()
		- _st.statusPosition.y();
	if (_additionalStatus) {
		_additionalStatus->moveToLeft(
			_status->x(),
			statusTop - diff - _additionalStatus->height());
	}
	_name->moveToLeft(
		_st.namePosition.x(),
		_st.size
			- _st.namePosition.y()
			- _name->height()
			- (_additionalStatus ? (diff + _additionalStatus->height()) : 0),
		_st.size);
	_status->moveToLeft(_st.statusPosition.x(), statusTop, _st.size);
}

QRect PeerShortInfoCover::radialRect() const {
	const auto cover = _widget->rect();
	const auto size = st::boxLoadingSize;
	return QRect(
		cover.x() + (cover.width() - size) / 2,
		cover.y() + (cover.height() - size) / 2,
		size,
		size);
}

void PeerShortInfoCover::videoWaiting() {
	if (!anim::Disabled()) {
		_widget->update(radialRect());
	}
}

PeerShortInfoBox::PeerShortInfoBox(
	QWidget*,
	PeerShortInfoType type,
	rpl::producer<PeerShortInfoFields> fields,
	rpl::producer<QString> status,
	rpl::producer<PeerShortInfoUserpic> userpic,
	Fn<bool()> videoPaused,
	const style::ShortInfoBox *stOverride)
: _st(stOverride ? *stOverride : st::shortInfoBox)
, _type(type)
, _fields(std::move(fields))
, _topRoundBackground(this)
, _scroll(this, st::shortInfoScroll)
, _rows(
	_scroll->setOwnedWidget(
		object_ptr<Ui::VerticalLayout>(
			_scroll.data())))
, _cover(
		_rows.get(),
		st::shortInfoCover,
		nameValue(),
		std::move(status),
		std::move(userpic),
		std::move(videoPaused)) {
	_rows->add(_cover.takeOwned());

	_scroll->scrolls(
	) | rpl::start_with_next([=] {
		_cover.setScrollTop(_scroll->scrollTop());
	}, _cover.lifetime());
}

PeerShortInfoBox::~PeerShortInfoBox() = default;

rpl::producer<> PeerShortInfoBox::openRequests() const {
	return _openRequests.events();
}

rpl::producer<int> PeerShortInfoBox::moveRequests() const {
	return _cover.moveRequests();
}

void PeerShortInfoBox::prepare() {
	addButton(tr::lng_close(), [=] { closeBox(); });

	if (_type != PeerShortInfoType::Self) {
		// Perhaps a new lang key should be added for opening a group.
		addLeftButton(
			(_type == PeerShortInfoType::User)
				? tr::lng_profile_send_message()
				: (_type == PeerShortInfoType::Group)
				? tr::lng_view_button_group()
				: tr::lng_profile_view_channel(),
			[=] { _openRequests.fire({}); });
	}

	prepareRows();

	setNoContentMargin(true);

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
	refreshRoundedTopImage(getDelegate()->style().bg->c);

	setDimensionsToContent(st::shortInfoWidth, _rows);
}

void PeerShortInfoBox::prepareRows() {
	using namespace Info::Profile;

	auto addInfoLineGeneric = [&](
			rpl::producer<QString> &&label,
			rpl::producer<TextWithEntities> &&text,
			const style::FlatLabel &textSt) {
		auto line = CreateTextWithLabel(
			_rows,
			rpl::duplicate(label) | Ui::Text::ToWithEntities(),
			rpl::duplicate(text),
			_st.label,
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
			const style::FlatLabel &textSt) {
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
			_st.labeledOneLine);
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
	addInfoLine(std::move(label), aboutValue(), _st.labeled);
	addInfoOneLine(
		tr::lng_info_username_label(),
		usernameValue() | Ui::Text::ToWithEntities(),
		tr::lng_context_copy_mention(tr::now));
	addInfoOneLine(
		birthdayLabel(),
		birthdayValue() | Ui::Text::ToWithEntities(),
		tr::lng_mediaview_copy(tr::now));
}

RectParts PeerShortInfoBox::customCornersFilling() {
	return RectPart::FullTop;
}

void PeerShortInfoBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_rows->resizeToWidth(st::shortInfoWidth);
	_scroll->resize(st::shortInfoWidth, height());
	_scroll->move(0, 0);
	_topRoundBackground->move(0, 0);
}

int PeerShortInfoBox::fillRoundedTopHeight() {
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
	_roundedTop = Images::Round(
		std::move(_roundedTop),
		_cover.roundMask(),
		RectPart::TopLeft | RectPart::TopRight);
}

rpl::producer<QString> PeerShortInfoBox::nameValue() const {
	return _fields.value(
	) | rpl::map([](const PeerShortInfoFields &fields) {
		return fields.name;
	}) | rpl::distinct_until_changed();
}

rpl::producer<TextWithEntities> PeerShortInfoBox::linkValue() const {
	return _fields.value(
	) | rpl::map([](const PeerShortInfoFields &fields) {
		return Ui::Text::Link(fields.link, fields.link);
	}) | rpl::distinct_until_changed();
}

rpl::producer<QString> PeerShortInfoBox::phoneValue() const {
	return _fields.value(
	) | rpl::map([](const PeerShortInfoFields &fields) {
		return fields.phone;
	}) | rpl::distinct_until_changed();
}

rpl::producer<QString> PeerShortInfoBox::usernameValue() const {
	return _fields.value(
	) | rpl::map([](const PeerShortInfoFields &fields) {
		return fields.username;
	}) | rpl::distinct_until_changed();
}

rpl::producer<QString> PeerShortInfoBox::birthdayLabel() const {
	return Info::Profile::BirthdayLabelText(_fields.value(
	) | rpl::map([](const PeerShortInfoFields &fields) {
		return fields.birthday;
	}) | rpl::distinct_until_changed());
}

rpl::producer<QString> PeerShortInfoBox::birthdayValue() const {
	return Info::Profile::BirthdayValueText(_fields.value(
	) | rpl::map([](const PeerShortInfoFields &fields) {
		return fields.birthday;
	}) | rpl::distinct_until_changed());
}

rpl::producer<TextWithEntities> PeerShortInfoBox::aboutValue() const {
	return _fields.value() | rpl::map([](const PeerShortInfoFields &fields) {
		return fields.about;
	}) | rpl::distinct_until_changed();
}
