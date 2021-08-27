/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/message_bubble.h"

#include "ui/cached_round_corners.h"
#include "ui/image/image_prepare.h"
#include "ui/chat/chat_style.h"
#include "styles/style_chat.h"

namespace Ui {
namespace {

template <
	typename FillBg, // fillBg(QRect rect)
	typename FillSh, // fillSh(QRect rect)
	typename FillRounded, // fillRounded(QRect rect, RectParts parts)
	typename PaintTail> // paintTail(QPoint bottomPosition) -> tailWidth
void PaintBubbleGeneric(
		const SimpleBubble &args,
		FillBg &&fillBg,
		FillSh &&fillSh,
		FillRounded &&fillRounded,
		PaintTail &&paintTail) {
	auto parts = RectPart::None | RectPart::NoTopBottom;
	auto rect = args.geometry;
	if (args.skip & RectPart::Top) {
		if (args.skip & RectPart::Bottom) {
			fillBg(rect);
			return;
		}
		rect.setTop(rect.y() - st::historyMessageRadius);
	} else {
		parts |= RectPart::FullTop;
	}
	const auto skipBottom = (args.skip & RectPart::Bottom);
	if (skipBottom) {
		rect.setHeight(rect.height() + st::historyMessageRadius);
	} else {
		parts |= RectPart::Bottom;
	}
	if (!skipBottom && args.tailSide == RectPart::Right) {
		parts |= RectPart::BottomLeft;
		fillBg({
			rect.x() + rect.width() - st::historyMessageRadius,
			rect.y() + rect.height() - st::historyMessageRadius,
			st::historyMessageRadius,
			st::historyMessageRadius });
		const auto tailWidth = paintTail({
			rect.x() + rect.width(),
			rect.y() + rect.height() });
		fillSh({
			rect.x() + rect.width() - st::historyMessageRadius,
			rect.y() + rect.height(),
			st::historyMessageRadius + tailWidth,
			st::msgShadow });
	} else if (!skipBottom && args.tailSide == RectPart::Left) {
		parts |= RectPart::BottomRight;
		fillBg({
			rect.x(),
			rect.y() + rect.height() - st::historyMessageRadius,
			st::historyMessageRadius,
			st::historyMessageRadius });
		const auto tailWidth = paintTail({
			rect.x(),
			rect.y() + rect.height() });
		fillSh({
			rect.x() - tailWidth,
			rect.y() + rect.height(),
			st::historyMessageRadius + tailWidth,
			st::msgShadow });
	} else if (!skipBottom) {
		parts |= RectPart::FullBottom;
	}
	fillRounded(rect, parts);
}

void PaintPatternBubble(Painter &p, const SimpleBubble &args) {
	const auto opacity = args.st->msgOutBg()->c.alphaF();
	const auto shadowOpacity = opacity * args.st->msgOutShadow()->c.alphaF();
	const auto pattern = args.pattern;
	const auto sh = !(args.skip & RectPart::Bottom);
	const auto &tail = (args.tailSide == RectPart::Right)
		? pattern->tailRight
		: pattern->tailLeft;
	const auto tailShift = (args.tailSide == RectPart::Right
		? QPoint(0, tail.height())
		: QPoint(tail.width(), tail.height())) / int(tail.devicePixelRatio());
	const auto fillBg = [&](const QRect &rect) {
		const auto fill = rect.intersected(args.patternViewport);
		if (!fill.isEmpty()) {
			p.setClipRect(fill);
			PaintPatternBubblePart(
				p,
				args.patternViewport,
				pattern->pixmap,
				fill);
			p.setClipping(false);
		}
	};
	const auto fillSh = [&](const QRect &rect) {
		if (!(args.skip & RectPart::Bottom)) {
			p.setOpacity(shadowOpacity);
			fillBg(rect);
			p.setOpacity(opacity);
		}
	};
	const auto fillPattern = [&](
			int x,
			int y,
			const QImage &mask,
			QImage &cache) {
		PaintPatternBubblePart(
			p,
			args.patternViewport,
			pattern->pixmap,
			QRect(QPoint(x, y), mask.size() / int(mask.devicePixelRatio())),
			mask,
			cache);
	};
	const auto fillCorner = [&](int x, int y, int index) {
		fillPattern(
			x,
			y,
			pattern->corners[index],
			(index < 2) ? pattern->cornerTopCache : pattern->cornerBottomCache);
	};
	const auto fillRounded = [&](const QRect &rect, RectParts parts) {
		const auto x = rect.x();
		const auto y = rect.y();
		const auto w = rect.width();
		const auto h = rect.height();

		const auto cornerWidth = pattern->corners[0].width()
			/ style::DevicePixelRatio();
		const auto cornerHeight = pattern->corners[0].height()
			/ style::DevicePixelRatio();
		if (w < 2 * cornerWidth || h < 2 * cornerHeight) {
			return;
		}
		if (w > 2 * cornerWidth) {
			if (parts & RectPart::Top) {
				fillBg({
					x + cornerWidth,
					y,
					w - 2 * cornerWidth,
					cornerHeight });
			}
			if (parts & RectPart::Bottom) {
				fillBg({
					x + cornerWidth,
					y + h - cornerHeight,
					w - 2 * cornerWidth,
					cornerHeight });
				if (sh) {
					fillSh({
						x + cornerWidth,
						y + h,
						w - 2 * cornerWidth,
						st::msgShadow });
				}
			}
		}
		if (h > 2 * cornerHeight) {
			if ((parts & RectPart::NoTopBottom) == RectPart::NoTopBottom) {
				fillBg({
					x,
					y + cornerHeight,
					w,
					h - 2 * cornerHeight });
			} else {
				if (parts & RectPart::Left) {
					fillBg({
						x,
						y + cornerHeight,
						cornerWidth,
						h - 2 * cornerHeight });
				}
				if ((parts & RectPart::Center) && w > 2 * cornerWidth) {
					fillBg({
						x + cornerWidth,
						y + cornerHeight,
						w - 2 * cornerWidth,
						h - 2 * cornerHeight });
				}
				if (parts & RectPart::Right) {
					fillBg({
						x + w - cornerWidth,
						y + cornerHeight,
						cornerWidth,
						h - 2 * cornerHeight });
				}
			}
		}
		if (parts & RectPart::TopLeft) {
			fillCorner(x, y, 0);
		}
		if (parts & RectPart::TopRight) {
			fillCorner(x + w - cornerWidth, y, 1);
		}
		if (parts & RectPart::BottomLeft) {
			fillCorner(x, y + h - cornerHeight, 2);
		}
		if (parts & RectPart::BottomRight) {
			fillCorner(x + w - cornerWidth, y + h - cornerHeight, 3);
		}
	};
	const auto paintTail = [&](QPoint bottomPosition) {
		const auto position = bottomPosition - tailShift;
		fillPattern(position.x(), position.y(), tail, pattern->tailCache);
		return tail.width() / int(tail.devicePixelRatio());
	};
	p.setOpacity(opacity);
	PaintBubbleGeneric(args, fillBg, fillSh, fillRounded, paintTail);
	p.setOpacity(1.);
}

void PaintSolidBubble(Painter &p, const SimpleBubble &args) {
	const auto &st = args.st->messageStyle(args.outbg, args.selected);
	const auto &bg = st.msgBg;
	const auto sh = (args.skip & RectPart::Bottom)
		? nullptr
		: &st.msgShadow;
	const auto &tail = (args.tailSide == RectPart::Right)
		? st.tailRight
		: st.tailLeft;
	const auto tailShift = (args.tailSide == RectPart::Right)
		? QPoint(0, tail.height())
		: QPoint(tail.width(), tail.height());
	PaintBubbleGeneric(args, [&](const QRect &rect) {
		p.fillRect(rect, bg);
	}, [&](const QRect &rect) {
		p.fillRect(rect, *sh);
	}, [&](const QRect &rect, RectParts parts) {
		Ui::FillRoundRect(p, rect.x(), rect.y(), rect.width(), rect.height(), bg, st.corners, sh, parts);
	}, [&](const QPoint &bottomPosition) {
		tail.paint(p, bottomPosition - tailShift, args.outerWidth);
		return tail.width();
	});
}

} // namespace

std::unique_ptr<BubblePattern> PrepareBubblePattern(
		not_null<const style::palette*> st) {
	auto result = std::make_unique<Ui::BubblePattern>();
	result->corners = Images::CornersMask(st::historyMessageRadius);
	const auto addShadow = [&](QImage &bottomCorner) {
		auto result = QImage(
			bottomCorner.width(),
			(bottomCorner.height()
				+ st::msgShadow * int(bottomCorner.devicePixelRatio())),
			QImage::Format_ARGB32_Premultiplied);
		result.fill(Qt::transparent);
		result.setDevicePixelRatio(bottomCorner.devicePixelRatio());
		auto p = QPainter(&result);
		p.setOpacity(st->msgInShadow()->c.alphaF());
		p.drawImage(0, st::msgShadow, bottomCorner);
		p.setOpacity(1.);
		p.drawImage(0, 0, bottomCorner);
		p.end();

		bottomCorner = std::move(result);
	};
	addShadow(result->corners[2]);
	addShadow(result->corners[3]);
	result->tailLeft = st::historyBubbleTailOutLeft.instance(Qt::white);
	result->tailRight = st::historyBubbleTailOutRight.instance(Qt::white);
	result->tailCache = QImage(
		result->tailLeft.size(),
		QImage::Format_ARGB32_Premultiplied);
	result->cornerTopCache = QImage(
		result->corners[0].size(),
		QImage::Format_ARGB32_Premultiplied);
	result->cornerBottomCache = QImage(
		result->corners[2].size(),
		QImage::Format_ARGB32_Premultiplied);
	return result;
}

void PaintBubble(Painter &p, const SimpleBubble &args) {
	if (!args.selected
		&& args.outbg
		&& args.pattern
		&& !args.patternViewport.isEmpty()
		&& !args.pattern->pixmap.size().isEmpty()) {
		PaintPatternBubble(p, args);
	} else {
		PaintSolidBubble(p, args);
	}
}

void PaintBubble(Painter &p, const ComplexBubble &args) {
	if (args.selection.empty()) {
		PaintBubble(p, args.simple);
		return;
	}
	const auto rect = args.simple.geometry;
	const auto left = rect.x();
	const auto width = rect.width();
	const auto top = rect.y();
	const auto bottom = top + rect.height();
	const auto paintOne = [&](QRect geometry, bool selected, RectParts skip) {
		auto simple = args.simple;
		simple.geometry = geometry;
		simple.selected = selected;
		simple.skip = skip;
		PaintBubble(p, simple);
	};
	auto from = top;
	for (const auto &selected : args.selection) {
		if (selected.top > from) {
			const auto skip = RectPart::Bottom
				| (from > top ? RectPart::Top : RectPart::None);
			paintOne(
				QRect(left, from, width, selected.top - from),
				false,
				skip);
		}
		const auto skip = ((selected.top > top)
			? RectPart::Top
			: RectPart::None)
			| ((selected.top + selected.height < bottom)
				? RectPart::Bottom
				: RectPart::None);
		paintOne(
			QRect(left, selected.top, width, selected.height),
			true,
			skip);
		from = selected.top + selected.height;
	}
	if (from < bottom) {
		paintOne(
			QRect(left, from, width, bottom - from),
			false,
			RectPart::Top);
	}
}

void PaintPatternBubblePart(
		QPainter &p,
		const QRect &viewport,
		const QPixmap &pixmap,
		const QRect &target) {
	// #TODO bubbles optimizes
	const auto to = viewport;
	const auto from = QRect(QPoint(), pixmap.size());
	p.drawPixmap(to, pixmap, from);
}

void PaintPatternBubblePart(
		QPainter &p,
		const QRect &viewport,
		const QPixmap &pixmap,
		const QRect &target,
		const QImage &mask,
		QImage &cache) {
	Expects(mask.bytesPerLine() == mask.width() * 4);
	Expects(mask.format() == QImage::Format_ARGB32_Premultiplied);

	if (cache.size() != mask.size()) {
		cache = QImage(
			mask.size(),
			QImage::Format_ARGB32_Premultiplied);
	}
	cache.setDevicePixelRatio(mask.devicePixelRatio());
	Assert(cache.bytesPerLine() == cache.width() * 4);
	memcpy(cache.bits(), mask.constBits(), mask.sizeInBytes());

	auto q = QPainter(&cache);
	q.setCompositionMode(QPainter::CompositionMode_SourceIn);
	PaintPatternBubblePart(
		q,
		viewport.translated(-target.topLeft()),
		pixmap,
		QRect(QPoint(), cache.size() / int(cache.devicePixelRatio())));
	q.end();

	p.drawImage(target, cache);
}

void PaintPatternBubblePart(
		QPainter &p,
		const QRect &viewport,
		const QPixmap &pixmap,
		const QRect &target,
		Fn<void(Painter&)> paintContent,
		QImage &cache) {
	Expects(paintContent != nullptr);

	if (cache.size() != target.size() * style::DevicePixelRatio()) {
		cache = QImage(
			target.size() * style::DevicePixelRatio(),
			QImage::Format_ARGB32_Premultiplied);
		cache.setDevicePixelRatio(style::DevicePixelRatio());
	}
	cache.fill(Qt::transparent);
	auto q = Painter(&cache);
	q.translate(-target.topLeft());
	paintContent(q);
	q.setCompositionMode(QPainter::CompositionMode_SourceIn);
	PaintPatternBubblePart(q, viewport, pixmap, target);
	q.end();

	p.drawImage(target, cache);
}

} // namespace Ui
