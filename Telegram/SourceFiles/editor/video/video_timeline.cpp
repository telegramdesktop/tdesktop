/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/video/video_timeline.h"

#include "media/media_video_frames.h"
#include "ui/painter.h"
#include "ui/rect.h"
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
	if (_loading) {
		_loading->cancel->store(true);
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
	const auto from = crl::time(0);
	const auto span = duration();
	const auto matches = [&](const FrameSet &set) {
		return (int(set.frames.size()) == count)
			&& (set.from == from)
			&& (set.span == span)
			&& (set.box.height() == height)
			&& (std::abs(frameWidth - set.box.width())
				<= set.box.width() * kFrameWidthTolerance);
	};
	if (_loading && matches(*_loading)) {
		return;
	} else if (_loading) {
		_loading->cancel->store(true);
		_loading = nullptr;
	}
	if (matches(_frames)) {
		return;
	}
	_loading = std::make_unique<FrameSet>(FrameSet{
		.frames = std::vector<QImage>(count),
		.from = from,
		.span = span,
		.box = QSize(frameWidth, height),
		.cancel = std::make_shared<std::atomic<bool>>(false),
	});

	auto positions = std::vector<crl::time>();
	positions.reserve(count);
	for (auto i = 0; i != count; ++i) {
		positions.push_back(from + crl::time(
			base::SafeRound((i + 0.5) * span / count)));
	}
	const auto cancel = _loading->cancel;
	const auto path = _path;
	const auto content = _content;
	const auto box = _loading->box * style::DevicePixelRatio();
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
				if (cancel->load()
					|| !_loading
					|| index >= int(_loading->frames.size())) {
					return;
				}
				_loading->frames[index] = std::move(frame);
				update();
			});
			return true;
		});
		crl::on_main(weak, [=] {
			if (cancel->load() || !_loading) {
				return;
			}
			_frames = std::move(*_loading);
			_loading = nullptr;
			update();
		});
	});
}

void VideoTimeline::paintStrip(QPainter &p, const QRect &strip) {
	p.fillRect(strip, st::videoTimelinePlaceholderBg);
	paintFrames(p, strip, _frames);
	if (_loading) {
		paintFrames(p, strip, *_loading);
	}
}

void VideoTimeline::paintFrames(
		QPainter &p,
		const QRect &strip,
		const FrameSet &set) {
	const auto count = int(set.frames.size());
	if (!count || set.span <= 0) {
		return;
	}
	const auto stripRight = rect::right(strip);
	for (auto i = 0; i != count; ++i) {
		const auto &frame = set.frames[i];
		if (frame.isNull()) {
			continue;
		}
		const auto left = xAt(set.from + i * set.span / count);
		const auto right = xAt(set.from + (i + 1) * set.span / count);
		if (right <= strip.x() || left >= stripRight) {
			continue;
		}
		const auto width = (std::abs(right - left - set.box.width()) <= 1)
			? set.box.width()
			: std::max(right - left, 1);
		p.drawImage(QRect(left, strip.y(), width, strip.height()), frame);
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
