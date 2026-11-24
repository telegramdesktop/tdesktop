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
constexpr auto kMaxCachedFrameSets = 8;
constexpr auto kDotDuration = crl::time(500);
constexpr auto kFramesFadeDuration = crl::time(200);
constexpr auto kReloadDelay = crl::time(150);

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

void PaintFramePart(
		QPainter &p,
		const QImage &frame,
		const QRect &target,
		int sourceLeft) {
	const auto ratio = frame.devicePixelRatio();
	p.drawImage(target, frame, QRect(
		int(base::SafeRound(sourceLeft * ratio)),
		0,
		int(base::SafeRound(target.width() * ratio)),
		frame.height()));
}

} // namespace

const VideoTimelineFrames *VideoTimelineFramesCache::find(
		Fn<bool(const VideoTimelineFrames &set)> matches) const {
	for (const auto &set : ranges::views::reverse(_sets)) {
		if (matches(set)) {
			return &set;
		}
	}
	return nullptr;
}

void VideoTimelineFramesCache::add(VideoTimelineFrames set) {
	_sets.push_back(std::move(set));
	if (_sets.size() > kMaxCachedFrameSets) {
		_sets.erase(_sets.begin());
	}
}

int VideoTimelineFramesCache::size() const {
	return int(_sets.size());
}

VideoTimeline::VideoTimeline(
	not_null<Ui::RpWidget*> parent,
	VideoTimelineDescriptor descriptor)
: TrimTimeline(parent, TrimDescriptor(descriptor))
, _path(descriptor.path)
, _content(descriptor.content)
, _preview(std::move(descriptor.preview))
, _dimensions(descriptor.dimensions)
, _cache(descriptor.cache)
, _reloadTimer([=] { reloadFrames(); }) {
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

void VideoTimeline::visibleRangeChanged() {
	_reloadTimer.callOnce(kReloadDelay);
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
	const auto from = visibleFrom();
	const auto span = visibleTill() - from;
	if (span <= 0) {
		return;
	}
	const auto matches = [&](const VideoTimelineFrames &set) {
		return (int(set.frames.size()) == count)
			&& (set.from == from)
			&& (set.span == span)
			&& (set.box.height() == height)
			&& (std::abs(frameWidth - set.box.width())
				<= set.box.width() * kFrameWidthTolerance);
	};
	if (_loading && matches(_loading->set)) {
		return;
	} else if (_loading) {
		_loading->cancel->store(true);
		_loading = nullptr;
	}
	if (matches(_frames)) {
		return;
	}
	const auto cached = _cache ? _cache->find(matches) : nullptr;
	if (cached) {
		_frames = *cached;
		update();
		return;
	}
	_loading = std::make_unique<Loading>(Loading{
		.set = {
			.frames = std::vector<QImage>(count),
			.from = from,
			.span = span,
			.box = QSize(frameWidth, height),
		},
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
	const auto box = _loading->set.box * style::DevicePixelRatio();
	crl::async([=, weak = base::make_weak(this)] {
		auto acceptFrame = [&](int index, QImage &&frame) {
			if (cancel->load()) {
				return false;
			}
			frame.setDevicePixelRatio(style::DevicePixelRatio());
			auto showFrame = [=, frame = std::move(frame)](
				) mutable {
				if (cancel->load()
					|| !_loading
					|| index >= int(_loading->set.frames.size())) {
					return;
				}
				_loading->set.frames[index] = std::move(frame);
				if (!_frames.frames.empty()
					|| _preview.isNull()) {
					update();
				}
			};
			crl::on_main(weak, std::move(showFrame));
			return true;
		};
		Media::Video::ExtractFrames(
			path,
			content,
			{
				.positions = positions,
				.box = box,
				.cover = true,
			},
			std::move(acceptFrame));
		auto finishLoading = [=] {
			if (cancel->load() || !_loading) {
				return;
			}
			const auto fade = _frames.frames.empty()
				&& !_preview.isNull()
				&& ranges::any_of(
					_loading->set.frames,
					[](const QImage &frame) {
						return !frame.isNull();
					});
			_frames = std::move(_loading->set);
			_loading = nullptr;
			if (_cache) {
				_cache->add(_frames);
			}
			if (fade) {
				_framesShown.start(
					[=] { update(); },
					0.,
					1.,
					kFramesFadeDuration,
					anim::linear);
			}
			update();
		};
		crl::on_main(weak, std::move(finishLoading));
	});
}

void VideoTimeline::paintStrip(QPainter &p, const QRect &strip) {
	p.fillRect(strip, st::videoTimelinePlaceholderBg);
	const auto initialPreview = _frames.frames.empty()
		&& !_preview.isNull();
	const auto opacity = _framesShown.value(1.);
	if (initialPreview || opacity < 1.) {
		const auto width = std::max(
			_preview.size().scaled(
				strip.width(),
				strip.height(),
				Qt::KeepAspectRatio).width(),
			(strip.width() + kMaxFrames - 1) / kMaxFrames);
		p.save();
		p.setOpacity(p.opacity() * 0.35);
		for (auto x = strip.x(); x < rect::right(strip); x += width) {
			p.drawImage(
				QRect(x, strip.y(), width, strip.height()),
				_preview);
		}
		p.restore();
	}
	p.save();
	p.setOpacity(p.opacity() * opacity);
	paintFrames(p, strip, _frames);
	if (_loading && !initialPreview) {
		paintFrames(p, strip, _loading->set);
	}
	p.restore();
}

void VideoTimeline::paintFrames(
		QPainter &p,
		const QRect &strip,
		const VideoTimelineFrames &set) {
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
		const auto natural = set.box.width();
		const auto slot = std::max(right - left, 1);
		if (slot <= natural) {
			PaintFramePart(
				p,
				frame,
				QRect(left, strip.y(), slot, strip.height()),
				(natural - slot) / 2);
			continue;
		}
		for (auto x = left; x < right; x += natural) {
			PaintFramePart(
				p,
				frame,
				QRect(x, strip.y(), std::min(natural, right - x), strip.height()),
				0);
		}
	}
}

void VideoTimeline::paintOverlay(QPainter &p) {
	if (trimOnly()) {
		return;
	}
	const auto strip = stripRect();
	const auto centre = coverDot();
	if (centre.x() < strip.x() || centre.x() > rect::right(strip)) {
		return;
	}
	const auto active = _dotActive.value(draggingHead() ? 1. : 0.);
	const auto size = st::videoTimelineDotSize
		+ (st::videoTimelineDotActiveSize - st::videoTimelineDotSize)
			* active;
	p.setPen(Qt::NoPen);
	p.setBrush(st::videoTimelineDotFg);
	p.drawEllipse(QRectF(
		centre.x() - size / 2.,
		centre.y() - size / 2.,
		size,
		size));
}

} // namespace Editor
