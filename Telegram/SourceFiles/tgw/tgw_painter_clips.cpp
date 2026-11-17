/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_painter_clips.h"

#include <QtGui/QPainter>

#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
#include <private/qpaintengine_raster_p.h>
#include <private/qpainter_p.h>
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API

namespace Tgw {
namespace {

constexpr auto kClipStructBytes = int64(256);
constexpr auto kClipRowBytes = int64(32);

} // namespace

int64 ClipBaseBytes(const QPainter &painter) {
	return kClipStructBytes + kClipRowBytes * painter.device()->height();
}

#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
bool ShortensClipHistory() {
	return true;
}

void ShortenClipHistory(QPainter &painter) {
	auto &history = QPainterPrivate::get(&painter)->state->clipInfo;
	if (history.size() < 2) {
		return;
	}
	auto bounds = QRectF();
	auto first = true;
	// The same fold as QPainter::clipBoundingRect(), so it reads the same rect.
	for (const auto &info : std::as_const(history)) {
		auto r = QRectF();
		if (info.clipType == QPainterClipInfo::RectClip) {
			r = info.rect;
		} else if (info.clipType == QPainterClipInfo::RectFClip) {
			r = info.rectf;
		} else if (info.clipType == QPainterClipInfo::RegionClip) {
			r = info.region.boundingRect();
		} else {
			r = info.path.boundingRect();
		}
		r = info.matrix.mapRect(r);
		if (first) {
			bounds = r;
		} else if (info.operation == Qt::IntersectClip) {
			bounds &= r;
		}
		first = false;
	}
	history = {
		QPainterClipInfo(bounds, Qt::IntersectClip, QTransform()),
	};
}

int64 ClipDataBytes(QPainter &painter) {
	const auto engine = painter.paintEngine();
	if (!engine || engine->type() != QPaintEngine::Raster) {
		return ClipBaseBytes(painter);
	}
	const auto state = static_cast<QRasterPaintEngine*>(engine)->state();
	if (!state->clip || !state->flags.has_clip_ownership) {
		return 0;
	}
	const auto clip = state->clip;
	const auto rows = int64(clip->clipSpanHeight);
	const auto regions = clip->hasRegionClip
		? (int64(clip->clipRegion.rectCount()) * int64(sizeof(QRect)))
		: int64();
	return int64(sizeof(QClipData))
		+ rows * int64(sizeof(QClipData::ClipLine))
		+ std::max(int64(clip->allocated), rows) * int64(sizeof(QT_FT_Span))
		+ regions;
}
#else // !DESKTOP_APP_DISABLE_PRIVATE_QT_API
bool ShortensClipHistory() {
	return false;
}

void ShortenClipHistory(QPainter &) {
}

int64 ClipDataBytes(QPainter &) {
	return 0;
}
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API

} // namespace Tgw
