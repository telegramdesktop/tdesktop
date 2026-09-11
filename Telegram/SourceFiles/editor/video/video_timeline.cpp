/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/video/video_timeline.h"

#include "media/media_video_frames.h"
#include "ui/painter.h"
#include "styles/style_editor.h"

namespace Editor {
namespace {

constexpr auto kMaxFrames = 24;
constexpr auto kDotDuration = crl::time(500);

// Frames are scaled on paint, so small width drift needs no re-extract.
constexpr auto kFrameWidthTolerance = 0.25;

[[nodiscard]] TrimTimelineDescriptor TrimDescriptor(
		const VideoTimelineDescriptor &descriptor) {
	return {
		.duration = descriptor.duration,
		.maxDuration = descriptor.maxDuration,
		.minDuration = descriptor.minDuration,
		.from = descriptor.from,
		.till = descriptor.till,
		.cover = descriptor.cover,
		.trimOnly = descriptor.trimOnly,
	};
}

} // namespace

VideoTimeline::VideoTimeline(
	not_null<Ui::RpWidget*> parent,
	VideoTimelineDescriptor descriptor)
: TrimTimeline(parent, TrimDescriptor(descriptor))
, _path(descriptor.path)
, _content(descriptor.content)
, _dimensions(descriptor.dimensions) {
	sizeValue(
	) | rpl::filter([=](QSize size) {
		return !size.isEmpty();
	}) | rpl::on_next([=] {
		reloadFrames();
	}, lifetime());
}

VideoTimeline::~VideoTimeline() {
	if (_framesCancel) {
		_framesCancel->store(true);
	}
}

QPoint VideoTimeline::coverDot() const {
	return QPoint(
		xAt(cover()),
		stripRect().y()
			- st::videoTimelinePlayheadOverflow
			- st::videoTimelinePlayheadOutline
			- st::videoTimelineDotSkip
			- st::videoTimelineDotActiveSize / 2);
}

void VideoTimeline::headGrabChanged(bool grabbed) {
	_dotActive.start(
		[=] { update(); },
		grabbed ? 0. : 1.,
		grabbed ? 1. : 0.,
		kDotDuration,
		anim::easeOutQuint);
}

void VideoTimeline::reloadFrames() {
	const auto strip = stripRect();
	const auto height = strip.height();
	if (strip.isEmpty() || _dimensions.isEmpty() || height <= 0) {
		return;
	}
	const auto aspectWidth = std::max(
		int(base::SafeRound(
			height * _dimensions.width() / float64(_dimensions.height()))),
		1);
	const auto count = std::clamp(
		(strip.width() + aspectWidth - 1) / aspectWidth,
		1,
		kMaxFrames);
	const auto frameWidth = (strip.width() + count - 1) / count;
	const auto kept = (int(_frames.size()) == count)
		&& (_framesBox.height() == height)
		&& (std::abs(frameWidth - _framesBox.width())
			<= _framesBox.width() * kFrameWidthTolerance);
	_frameWidth = frameWidth;
	if (kept) {
		return;
	}
	if (_framesCancel) {
		_framesCancel->store(true);
	}
	_framesBox = QSize(frameWidth, height);
	_frames = std::vector<QImage>(count);

	auto positions = std::vector<crl::time>();
	positions.reserve(count);
	for (auto i = 0; i != count; ++i) {
		positions.push_back(crl::time(
			base::SafeRound((i + 0.5) * duration() / count)));
	}
	const auto cancel = std::make_shared<std::atomic<bool>>(false);
	_framesCancel = cancel;

	const auto path = _path;
	const auto content = _content;
	const auto box = _framesBox * style::DevicePixelRatio();
	crl::async([=, weak = base::make_weak(this)] {
		Media::Video::ExtractFrames(path, content, {
			.positions = positions,
			.box = box,
			.cover = true,
		}, [&](int index, QImage &&frame) {
			if (cancel->load()) {
				return false;
			}
			frame.setDevicePixelRatio(style::DevicePixelRatio());
			crl::on_main(weak, [=, frame = std::move(frame)]() mutable {
				if (cancel->load() || index >= int(_frames.size())) {
					return;
				}
				_frames[index] = std::move(frame);
				update();
			});
			return true;
		});
	});
}

void VideoTimeline::paintStrip(QPainter &p, const QRect &strip) {
	p.fillRect(strip, st::videoTimelinePlaceholderBg);
	if (_frameWidth <= 0) {
		return;
	}
	const auto count = int(_frames.size());
	for (auto i = 0; i != count; ++i) {
		const auto &frame = _frames[i];
		if (frame.isNull()) {
			continue;
		}
		const auto x = strip.x() + i * strip.width() / count;
		p.drawImage(QRect(x, strip.y(), _frameWidth, strip.height()), frame);
	}
}

void VideoTimeline::paintOverlay(QPainter &p) {
	if (trimOnly()) {
		return;
	}
	const auto active = _dotActive.value(draggingHead() ? 1. : 0.);
	const auto size = st::videoTimelineDotSize
		+ (st::videoTimelineDotActiveSize - st::videoTimelineDotSize)
			* active;
	const auto centre = coverDot();
	p.setPen(Qt::NoPen);
	p.setBrush(st::videoTimelineDotFg);
	p.drawEllipse(QRectF(
		centre.x() - size / 2.,
		centre.y() - size / 2.,
		size,
		size));
}

} // namespace Editor
