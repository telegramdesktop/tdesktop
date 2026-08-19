/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_slideshow_chrome.h"

#include "ui/image/image_prepare.h"
#include "styles/palette.h"
#include "styles/style_iv.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Iv::Markdown {
namespace {

constexpr auto kInactiveDotOpacity = 0.5;

[[nodiscard]] QImage GenerateSlideshowDotsBackdrop(
		QSize outer,
		int fade,
		QColor color) {
	const auto ratio = style::DevicePixelRatio();
	const auto size = outer * ratio;
	auto result = QImage(size, QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	const auto f = fade * float64(ratio);
	if (f <= 0. || size.isEmpty()) {
		return result;
	}
	const auto cy = size.height() / 2.;
	const auto radius = cy - f;
	const auto from = f + radius;
	const auto till = size.width() - f - radius;
	const auto peak = color.alphaF();
	for (auto y = 0; y != size.height(); ++y) {
		const auto line = reinterpret_cast<uint32*>(result.scanLine(y));
		const auto py = y + 0.5;
		for (auto x = 0; x != size.width(); ++x) {
			const auto px = x + 0.5;
			const auto sx = std::clamp(px, from, till);
			const auto distance = std::hypot(px - sx, py - cy) - radius;
			const auto ramp = std::clamp((f - distance) / (2. * f), 0., 1.);
			line[x] = qPremultiply(qRgba(
				color.red(),
				color.green(),
				color.blue(),
				int(std::round(ramp * peak * 255.))));
		}
	}
	return result;
}

} // namespace

int MediaHeightForWidth(
		int width,
		int aspectWidth,
		int aspectHeight) {
	aspectWidth = std::max(aspectWidth, 1);
	aspectHeight = std::max(aspectHeight, 1);
	return std::max(
		int((int64(width) * aspectHeight + aspectWidth - 1) / aspectWidth),
		1);
}

QPainterPath RoundedRectPath(QRect rect, int radius) {
	auto path = QPainterPath();
	path.addRoundedRect(QRectF(rect), radius, radius);
	return path;
}

void PaintRoundButton(
		Painter &p,
		QRect rect,
		const style::color &bg,
		const style::icon &icon) {
	if (rect.isEmpty()) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(bg->c);
	p.drawEllipse(rect);
	icon.paintInCenter(p, rect);
}

QImage PrepareWithBlurredBackground(
		QSize outer,
		QSize inner,
		QImage large,
		QImage blurred) {
	const auto ratio = style::DevicePixelRatio();
	auto background = QImage(
		outer * ratio,
		QImage::Format_ARGB32_Premultiplied);
	background.setDevicePixelRatio(ratio);
	if (blurred.isNull()) {
		background.fill(Qt::black);
		if (large.isNull()) {
			return background;
		}
	}
	auto p = QPainter(&background);
	if (!blurred.isNull()) {
		auto cover = blurred.scaled(
			outer * ratio,
			Qt::KeepAspectRatioByExpanding,
			Qt::SmoothTransformation);
		if (cover.size() != outer * ratio) {
			cover = cover.copy(QRect(
				QPoint(
					(cover.width() - outer.width() * ratio) / 2,
					(cover.height() - outer.height() * ratio) / 2),
				outer * ratio));
		}
		cover = Images::Blur(std::move(cover), true);
		cover.setDevicePixelRatio(ratio);
		p.drawImage(QPoint(), cover);
		p.fillRect(QRect(QPoint(), outer), QColor(0, 0, 0, 48));
	}
	if (!large.isNull()) {
		auto image = large.scaled(
			inner * ratio,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
		image.setDevicePixelRatio(ratio);
		p.drawImage(
			(outer.width() - inner.width()) / 2,
			(outer.height() - inner.height()) / 2,
			image);
	}
	return background;
}

int SlideshowFrameHeight(
		int width,
		int slideshowMinHeight,
		gsl::span<const QSize> slideOriginalSizes) {
	auto result = std::numeric_limits<int>::max();
	for (const auto &original : slideOriginalSizes) {
		result = std::min(
			result,
			MediaHeightForWidth(width, original.width(), original.height()));
	}
	if (result == std::numeric_limits<int>::max()) {
		result = std::max(slideshowMinHeight, 1);
	}
	return std::max(result, std::max(slideshowMinHeight, 1));
}

SlideshowNavRects ComputeSlideshowNavRects(
		QRect frame,
		int frameHeight,
		int navButtonSize,
		int navButtonSkip) {
	const auto availableWidth = std::max(
		(frame.width() - 2 * navButtonSkip) / 2,
		0);
	const auto size = std::min({
		navButtonSize,
		std::max(frameHeight, 0),
		availableWidth,
	});
	if (size <= 0) {
		return {};
	}
	const auto top = frame.y() + std::max((frameHeight - size) / 2, 0);
	return {
		.previous = QRect(
			frame.x() + navButtonSkip,
			top,
			size,
			size),
		.next = QRect(
			frame.x() + frame.width() - navButtonSkip - size,
			top,
			size,
			size),
	};
}

SlideshowDotsGeometry ComputeSlideshowDots(
		QRect media,
		int count,
		int active,
		const style::MarkdownGroupedMedia &st) {
	const auto dot = st.dotSize;
	const auto advance = st.dotSkip;
	if (count < 2 || media.isEmpty() || dot <= 0 || advance <= 0) {
		return {};
	}
	const auto padding = (advance - dot) / 2;
	const auto bgHeight = dot + 2 * padding;
	const auto fade = bgHeight / 2;
	const auto available = media.width() - 2 * st.navButtonSkip;
	const auto visible = std::clamp(
		(available - 2 * fade) / advance,
		1,
		count);
	const auto first = (visible == count)
		? 0
		: std::clamp(active - visible / 2, 0, count - visible);
	const auto bgWidth = visible * advance;
	const auto left = media.x() + (media.width() - bgWidth) / 2;
	const auto bottom = media.y() + media.height() - st.dotsBottomSkip;
	const auto core = QRect(left, bottom - bgHeight, bgWidth, bgHeight);
	return {
		.core = core,
		.outer = core.marginsAdded({ fade, fade, fade, fade }),
		.first = first,
		.visible = visible,
	};
}

void PaintSlideshowDots(
		Painter &p,
		const SlideshowDotsGeometry &dots,
		int active,
		const style::MarkdownGroupedMedia &st,
		SlideshowDotsBackdrop &backdrop) {
	if (dots.visible <= 0 || dots.core.isEmpty()) {
		return;
	}
	const auto ratio = style::DevicePixelRatio();
	const auto fade = (dots.outer.height() - dots.core.height()) / 2;
	auto color = ::st::radialBg->c;
	color.setAlphaF(st.dotsBgOpacity);
	if (backdrop.image.size() != dots.outer.size() * ratio
		|| backdrop.color != color) {
		backdrop.image = GenerateSlideshowDotsBackdrop(
			dots.outer.size(),
			fade,
			color);
		backdrop.color = color;
	}
	p.drawImage(dots.outer.topLeft(), backdrop.image);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(::st::radialFg);
	const auto dot = st.dotSize;
	const auto advance = st.dotSkip;
	const auto cy = dots.core.y() + dots.core.height() / 2;
	auto cx = dots.core.x() + advance / 2;
	for (auto i = 0; i != dots.visible; ++i, cx += advance) {
		p.setOpacity((dots.first + i == active)
			? 1.
			: kInactiveDotOpacity);
		p.drawEllipse(cx - dot / 2, cy - dot / 2, dot, dot);
	}
	p.setOpacity(1.);
}

} // namespace Iv::Markdown
