/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_article_paint.h"
#include "iv/markdown/iv_markdown_article.h"
#include "iv/markdown/iv_markdown_article_text.h"
#include "ui/dynamic_image.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/effects/toggle_arrow.h"
#include "ui/integration.h"
#include "ui/style/style_core_scale.h"
#include "ui/widgets/checkbox.h"

#include "styles/palette.h"
#include "styles/style_iv.h"
#include "styles/style_window.h"
#include "styles/style_widgets.h"

#include <QtGui/QPainterPath>

#include <algorithm>
#include <cmath>
#include <optional>

namespace Iv::Markdown {
namespace {

constexpr auto kThinkingGradientSlideDuration = crl::time(1000);
constexpr auto kThinkingGradientWaitDuration = crl::time(1000);
constexpr auto kThinkingGradientFullDuration = kThinkingGradientSlideDuration
	+ kThinkingGradientWaitDuration;

[[nodiscard]] QRectF CenterCropSourceRect(QSize source, QSize target) {
	const auto sourceWidth = std::max(source.width(), 1);
	const auto sourceHeight = std::max(source.height(), 1);
	const auto targetWidth = std::max(target.width(), 1);
	const auto targetHeight = std::max(target.height(), 1);
	const auto sourceRatio = float64(sourceWidth) / sourceHeight;
	const auto targetRatio = float64(targetWidth) / targetHeight;
	if (sourceRatio > targetRatio) {
		const auto width = sourceHeight * targetRatio;
		return QRectF(
			(sourceWidth - width) / 2.,
			0.,
			width,
			sourceHeight);
	}
	const auto height = sourceWidth / targetRatio;
	return QRectF(
		0.,
		(sourceHeight - height) / 2.,
		sourceWidth,
		height);
}

void PaintImageCenterCrop(Painter &p, QRect rect, const QImage &image) {
	p.drawImage(
		QRectF(rect),
		image,
		CenterCropSourceRect(image.size(), rect.size()));
}

[[nodiscard]] bool ImageCoversRect(
		const QImage &image,
		QRect rect,
		double pixelScale) {
	const auto ratio = std::max(image.devicePixelRatio(), 1.);
	return (image.width() / ratio >= rect.width() * pixelScale)
		&& (image.height() / ratio >= rect.height() * pixelScale);
}

[[nodiscard]] QSize ScaledImageRequestSize(QSize size, double scale) {
	return (scale == 1.)
		? size
		: QSize(
			std::max(int(std::ceil(size.width() * scale)), 1),
			std::max(int(std::ceil(size.height() * scale)), 1));
}

[[nodiscard]] int PullquoteIconReserveWidth(
		const style::QuoteStyle &style) {
	return style.icon.empty()
		? 0
		: (style.icon.width() + style.iconPosition.x());
}

[[nodiscard]] bool PaintDynamicImage(
		Painter &p,
		const std::shared_ptr<Ui::DynamicImage> &image,
		QRect rect,
		double pixelScale,
		bool requireCovering = false) {
	if (!image || rect.isEmpty()) {
		return false;
	}
	const auto requested = int(std::ceil(
		std::max(rect.width(), rect.height()) * pixelScale));
	if (const auto frame = image->image(requested); !frame.isNull()) {
		if (requireCovering && !ImageCoversRect(frame, rect, pixelScale)) {
			return false;
		}
		PaintImageCenterCrop(p, rect, frame);
		return true;
	}
	return false;
}

[[nodiscard]] bool PaintThumbnailImage(
		Painter &p,
		QRect rect,
		const std::shared_ptr<Ui::DynamicImage> &thumbnail,
		const std::shared_ptr<Ui::DynamicImage> &previousThumbnail,
		double pixelScale) {
	return PaintDynamicImage(p, thumbnail, rect, pixelScale, true)
		|| PaintDynamicImage(p, previousThumbnail, rect, pixelScale, true)
		|| PaintDynamicImage(p, previousThumbnail, rect, pixelScale)
		|| PaintDynamicImage(p, thumbnail, rect, pixelScale);
}

void UpdateResolvedImage(
		std::shared_ptr<Ui::DynamicImage> *current,
		std::shared_ptr<Ui::DynamicImage> *previous,
		const std::shared_ptr<Ui::DynamicImage> &next) {
	if (!current || !previous || !next) {
		return;
	} else if (next == *current) {
		return;
	} else if (next == *previous) {
		std::swap(*current, *previous);
		return;
	}
	*previous = std::move(*current);
	*current = next;
}

template <typename RequestImage>
void RefreshResolvedBlockImage(
		const LaidOutBlock &block,
		const MarkdownArticlePaintContext &context,
		QSize size,
		QSize *requestedSize,
		std::shared_ptr<Ui::DynamicImage> *current,
		std::shared_ptr<Ui::DynamicImage> *previous,
		std::shared_ptr<Ui::DynamicImage> *subscribed,
		RequestImage &&request) {
	if (size.isEmpty() || !requestedSize || !current || !previous || !subscribed) {
		return;
	} else if (*requestedSize == size) {
		return;
	}
	*requestedSize = size;
	const auto image = request(size);
	if (!image) {
		return;
	}
	UpdateResolvedImage(current, previous, image);
	if (image == *subscribed) {
		return;
	}
	*subscribed = image;
	const auto repaint = context.caches.repaint;
	const auto repaintRect = context.caches.repaintRect;
	const auto rect = block.mediaRect;
	image->subscribeToUpdates([repaint, repaintRect, rect] {
		if (repaintRect && !rect.isEmpty()) {
			repaintRect(rect);
		} else if (repaint) {
			repaint();
		}
	});
}

[[nodiscard]] bool PaintRelatedArticleImage(
		Painter &p,
		QRect rect,
		const std::shared_ptr<Ui::DynamicImage> &thumbnail,
		const std::shared_ptr<Ui::DynamicImage> &full,
		const std::shared_ptr<Ui::DynamicImage> &previousThumbnail,
		const std::shared_ptr<Ui::DynamicImage> &previousFull,
		double pixelScale) {
	return PaintDynamicImage(p, full, rect, pixelScale, true)
		|| PaintDynamicImage(p, previousFull, rect, pixelScale, true)
		|| PaintDynamicImage(p, full, rect, pixelScale)
		|| PaintDynamicImage(p, previousFull, rect, pixelScale)
		|| PaintDynamicImage(p, thumbnail, rect, pixelScale, true)
		|| PaintDynamicImage(p, previousThumbnail, rect, pixelScale, true)
		|| PaintDynamicImage(p, previousThumbnail, rect, pixelScale)
		|| PaintDynamicImage(p, thumbnail, rect, pixelScale);
}

[[nodiscard]] const style::Markdown &PaintStyle(
		const MarkdownArticlePaintContext &context,
		const style::Markdown &st) {
	return context.paintMarkdownStyle(st);
}

[[nodiscard]] MarkdownArticlePaintContext ClippedContext(
		const MarkdownArticlePaintContext &context,
		QRect clip) {
	auto result = context;
	result.clip = clip;
	return result;
}

[[nodiscard]] MarkdownArticlePaintContext RevealSuppressedContext(
		const MarkdownArticlePaintContext &context) {
	auto result = context;
	result.reveal = nullptr;
	return result;
}

[[nodiscard]] std::optional<int> ConsumeRevealLine(
		const MarkdownArticlePaintContext &context) {
	if (!context.reveal) {
		return std::nullopt;
	}
	return context.reveal->nextLine++;
}

[[nodiscard]] int CountGenericRevealBand(QRect rect) {
	return rect.isEmpty() ? 0 : 1;
}

[[nodiscard]] int CountTextRevealLines(
		const Ui::Text::String &leaf,
		QRect textRect,
		int textWidth) {
	if (textRect.isEmpty() || (textWidth <= 0)) {
		return 0;
	}
	return int(leaf.countLinesGeometry(textWidth).size());
}

void PaintSelectableTextLeaf(
	Painter &p,
	const Ui::Text::String &leaf,
	const MarkdownArticlePaintContext &context,
	QRect rect,
	int width,
	int segmentIndex,
	style::align align,
	std::optional<TextSelection> selection,
	int elisionLines);

[[nodiscard]] int CountRevealLinesForBlocks(
		const std::vector<LaidOutBlock> &blocks,
		const style::Markdown &st);

[[nodiscard]] int CountTableRevealLines(const LaidOutBlock &block) {
	if (block.visibleTableRect.isEmpty()) {
		return 0;
	}
	auto result = 0;
	for (const auto &row : block.tableRows) {
		if (row.outer.height() > 0) {
			++result;
		}
	}
	return result;
}

[[nodiscard]] const Ui::Text::String &DisplayedEditableLeaf(
		const LaidOutBlock &block) {
	return block.placeholderLeaf.isEmpty()
		? block.leaf
		: block.placeholderLeaf;
}

[[nodiscard]] int CountMediaRevealLines(const LaidOutBlock &block) {
	return CountGenericRevealBand(block.visibleMediaRect)
		+ CountTextRevealLines(
			DisplayedEditableLeaf(block),
			block.textRect,
			block.textWidth);
}

[[nodiscard]] int CountEmbedPostRevealLines(
		const LaidOutBlock &block,
		const style::Markdown &st) {
	auto result = 0;
	if (!block.headerRect.isEmpty()) {
		result += (block.mediaRect.width() > 0) ? 1 : 0;
	} else if (block.children.empty()) {
		result += CountGenericRevealBand(block.mediaRect);
	}
	result += CountRevealLinesForBlocks(block.children, st);
	result += CountTextRevealLines(
		DisplayedEditableLeaf(block),
		block.textRect,
		block.textWidth);
	return result;
}

[[nodiscard]] int CountRevealLinesForBlock(
		const LaidOutBlock &block,
		const style::Markdown &st) {
	switch (block.kind) {
	case PreparedBlockKind::Paragraph:
	case PreparedBlockKind::Thinking:
	case PreparedBlockKind::Heading:
	case PreparedBlockKind::CodeBlock:
		return CountTextRevealLines(
			DisplayedEditableLeaf(block),
			block.textRect,
			block.textWidth);
	case PreparedBlockKind::Rule:
		return CountGenericRevealBand(block.outer);
	case PreparedBlockKind::List:
	case PreparedBlockKind::ListItem:
	case PreparedBlockKind::Quote:
		return CountRevealLinesForBlocks(block.children, st);
	case PreparedBlockKind::DisplayMath:
		return CountGenericRevealBand(block.visibleFormulaRect);
	case PreparedBlockKind::Table:
		return CountTextRevealLines(
			DisplayedEditableLeaf(block),
			block.textRect,
			block.textWidth) + CountTableRevealLines(block);
	case PreparedBlockKind::Details:
		return CountTextRevealLines(
			DisplayedEditableLeaf(block),
			block.textRect,
			block.textWidth) + CountRevealLinesForBlocks(block.children, st);
	case PreparedBlockKind::Photo:
	case PreparedBlockKind::Video:
	case PreparedBlockKind::Audio:
	case PreparedBlockKind::Map:
	case PreparedBlockKind::Channel:
	case PreparedBlockKind::GroupedMedia:
	case PreparedBlockKind::Placeholder:
		return CountMediaRevealLines(block);
	case PreparedBlockKind::RelatedArticle:
		return CountGenericRevealBand(block.visibleMediaRect);
	case PreparedBlockKind::EmbedPost:
		return CountEmbedPostRevealLines(block, st);
	}
	Unexpected("Prepared block kind in CountRevealLinesForBlock.");
}

[[nodiscard]] int CountRevealLinesForBlocks(
		const std::vector<LaidOutBlock> &blocks,
		const style::Markdown &st) {
	auto result = 0;
	for (const auto &block : blocks) {
		result += CountRevealLinesForBlock(block, st);
	}
	return result;
}

template <typename CountCallback>
[[nodiscard]] int CachedRevealLineCount(
		const MarkdownArticlePaintContext &context,
		const void *key,
		CountCallback &&count) {
	const auto cache = context.reveal ? context.reveal->lineCounts : nullptr;
	if (!cache) {
		return count();
	}
	const auto i = cache->counts.find(key);
	if (i != end(cache->counts)) {
		return i->second;
	}
	const auto result = count();
	cache->counts.emplace(key, result);
	return result;
}

void AdvanceRevealLinesForBlock(
		const MarkdownArticlePaintContext &context,
		const LaidOutBlock &block,
		const style::Markdown &st) {
	if (context.reveal) {
		context.reveal->nextLine += CachedRevealLineCount(
			context,
			&block,
			[&] { return CountRevealLinesForBlock(block, st); });
	}
}

template <typename Callback>
void PaintRevealBand(
		Painter &p,
		const MarkdownArticlePaintContext &context,
		QRect band,
		Callback paint) {
	if (band.isEmpty()) {
		return;
	}
	const auto lineIndex = ConsumeRevealLine(context);
	const auto visible = context.clip.intersected(band);
	if (visible.isEmpty()) {
		return;
	}

	const auto paintDirect = [&] {
		const auto local = ClippedContext(
			RevealSuppressedContext(context),
			visible);
		p.save();
		p.setClipRect(visible, Qt::IntersectClip);
		paint(p, local);
		p.restore();
	};
	if (!lineIndex) {
		paintDirect();
		return;
	}

	const auto reveal = context.reveal;
	if (*lineIndex > reveal->activeLine) {
		return;
	} else if (*lineIndex < reveal->activeLine
		|| !reveal->postprocess
		|| !reveal->postprocess->method) {
		paintDirect();
		return;
	}

	auto postprocess = reveal->postprocess->method(*lineIndex, band.width());
	if (!postprocess) {
		paintDirect();
		return;
	}

	const auto ratio = std::max(style::DevicePixelRatio(), 1);
	const auto cacheWidth = band.width() * ratio;
	const auto cacheHeight = band.height() * ratio;
	auto &cache = *reveal->postprocess->cache;
	if (cache.devicePixelRatio() != ratio
		|| cache.width() < cacheWidth
		|| cache.height() < cacheHeight) {
		cache = QImage(
			QSize(
				std::max(cache.width(), cacheWidth),
				std::max(cache.height(), cacheHeight)),
			QImage::Format_ARGB32_Premultiplied);
		cache.setDevicePixelRatio(ratio);
	}
	cache.fill(Qt::transparent);

	{
		auto cachePainter = Painter(&cache);
		cachePainter.setFont(p.font());
		cachePainter.setPen(p.pen());
		cachePainter.setBrush(p.brush());
		cachePainter.setRenderHints(p.renderHints());
		cachePainter.setInactive(p.inactive());
		cachePainter.setTextPalette(p.textPalette());
		cachePainter.translate(-band.x(), -band.y());
		cachePainter.setClipRect(band);
		const auto local = ClippedContext(
			RevealSuppressedContext(context),
			band);
		paint(cachePainter, local);
	}

	postprocess(cache);

	p.save();
	p.setClipRect(visible, Qt::IntersectClip);
	p.drawImage(
		QRectF(band),
		cache,
		QRectF(0, 0, cacheWidth, cacheHeight));
	p.restore();
}

void EnsureThinkingPaintCacheImage(
		QImage *image,
		QSize logicalSize,
		int ratio) {
	if (!image || logicalSize.isEmpty()) {
		return;
	}
	ratio = std::max(ratio, 1);
	const auto neededSize = QSize(
		logicalSize.width() * ratio,
		logicalSize.height() * ratio);
	if (image->devicePixelRatio() == ratio
		&& image->format() == QImage::Format_ARGB32_Premultiplied
		&& image->width() >= neededSize.width()
		&& image->height() >= neededSize.height()) {
		return;
	}
	*image = QImage(
		QSize(
			std::max(image->width(), neededSize.width()),
			std::max(image->height(), neededSize.height())),
		QImage::Format_ARGB32_Premultiplied);
	image->setDevicePixelRatio(ratio);
}

[[nodiscard]] int ThinkingTextWidth(const LaidOutBlock &block) {
	return std::clamp(
		block.leaf.maxWidth(),
		1,
		std::max(block.textRect.width(), 1));
}

void FillThinkingGradientImage(
		QImage *image,
		QRect logicalRect,
		QRect textRect,
		int textWidth,
		QColor baseColor,
		QColor highlightColor) {
	if (!image) {
		return;
	}
	image->fill(Qt::transparent);
	if (logicalRect.isEmpty()) {
		return;
	}
	auto imagePainter = Painter(image);
	const auto localRect = QRect(QPoint(), logicalRect.size());
	const auto period = crl::now() % kThinkingGradientFullDuration;
	if (period >= kThinkingGradientSlideDuration) {
		imagePainter.fillRect(localRect, baseColor);
		return;
	}
	const auto progress = period / float64(kThinkingGradientSlideDuration);
	textWidth = std::max(textWidth, 1);
	const auto gradientWidth = std::max(textWidth, 1);
	const auto textLeft = textRect.x() - logicalRect.x();
	const auto start = anim::interpolate(
		textLeft - gradientWidth,
		textLeft + textWidth,
		progress);
	auto gradient = QLinearGradient(start, 0, start + gradientWidth, 0);
	gradient.setStops({
		{ 0., baseColor },
		{ 0.5, highlightColor },
		{ 1., baseColor },
	});
	imagePainter.fillRect(localRect, QBrush(gradient));
}

[[nodiscard]] QColor WithOpacity(style::color color, double opacity) {
	auto result = color->c;
	result.setAlphaF(result.alphaF() * std::clamp(opacity, 0., 1.));
	return result;
}

[[nodiscard]] QPen EditPlaceholderPen(
		const style::Markdown &st,
		const MarkdownArticlePaintContext &context) {
	auto color = context.caches.supplementaryColorOverride.value_or(
		PaintStyle(context, st).supplementaryTextColor->c);
	color.setAlphaF(color.alphaF() * 0.5);
	return QPen(color);
}

[[nodiscard]] bool PaintEditPlaceholderLeaf(
		Painter &p,
		const Ui::Text::String &placeholderLeaf,
		bool empty,
		const MarkdownArticlePaintContext &context,
		QRect rect,
		int width,
		int segmentIndex,
		const style::Markdown &st,
		style::align align = style::al_left,
		std::optional<TextSelection> selection = std::nullopt) {
	if (!empty || placeholderLeaf.isEmpty() || rect.isEmpty()) {
		return false;
	}
	p.save();
	p.setPen(EditPlaceholderPen(st, context));
	PaintSelectableTextLeaf(
		p,
		placeholderLeaf,
		context,
		rect,
		width,
		segmentIndex,
		align,
		selection,
		0);
	p.restore();
	return true;
}

[[nodiscard]] QColor TableHeaderBg(const style::MarkdownTable &st) {
	return WithOpacity(st.headerBg, st.headerBgOpacity);
}

[[nodiscard]] bool UseIncomingMessageDefaultPalette(
		const MarkdownArticlePaintContext &context) {
	return !context.outbg
		&& (context.caches.st == &context.messageStyle()->richPageStyle);
}

[[nodiscard]] QColor ApplySelectedIncomingOverlay(
		QColor base,
		const MarkdownArticlePaintContext &context) {
	if (!context.selected() || context.outbg) {
		return base;
	}
	auto overlay = context.st->msgSelectOverlay()->c;
	const auto alpha = overlay.alphaF();
	overlay.setAlpha(base.alpha());
	auto result = anim::color(base, overlay, alpha);
	result.setAlpha(base.alpha());
	return result;
}

[[nodiscard]] QColor EffectiveTableHeaderBg(
		const style::Markdown &paintSt,
		const MarkdownArticlePaintContext &context) {
	return UseIncomingMessageDefaultPalette(context)
		? ApplySelectedIncomingOverlay(
			TableHeaderBg(st::defaultMarkdown.table),
			context)
		: TableHeaderBg(paintSt.table);
}

[[nodiscard]] QColor EffectiveTableBorderFg(
		const style::Markdown &paintSt,
		const MarkdownArticlePaintContext &context) {
	const auto useIncoming = UseIncomingMessageDefaultPalette(context);
	const auto &table = useIncoming
		? st::defaultMarkdown.table
		: paintSt.table;
	auto result = WithOpacity(table.borderFg, table.headerBgOpacity * 3);
	return useIncoming
		? ApplySelectedIncomingOverlay(result, context)
		: result;
}

[[nodiscard]] QColor EffectiveDividerFg(
		const style::Markdown &paintSt,
		const MarkdownArticlePaintContext &context) {
	auto result = UseIncomingMessageDefaultPalette(context)
		? ApplySelectedIncomingOverlay(
			st::defaultMarkdown.rule.fg->c,
			context)
		: paintSt.rule.fg->c;
	if (context.outbg) {
		result.setAlphaF(result.alphaF() * 0.5);
	}
	return result;
}

void PaintDetailsIcon(
		Painter &p,
		QRect rect,
		QColor color,
		bool collapsed) {
	const auto size = std::min(rect.width(), rect.height()) / 3.;
	if (size <= 0.) {
		return;
	}
	const auto center = QRectF(rect).center();
	const auto path = Ui::ToggleUpDownArrowPath(
		center.x(),
		center.y(),
		size,
		st::mainMenuToggleFourStrokes,
		collapsed ? 0. : 1.);
	auto hq = PainterHighQualityEnabler(p);
	p.fillPath(path, color);
}

void RefreshBlockThumbnail(
		const LaidOutBlock &block,
		const MarkdownArticlePaintContext &context) {
	if (!block.photoRuntime || block.thumbnailRect.isEmpty()) {
		return;
	}
	const auto size = ScaledImageRequestSize(
		block.thumbnailRect.size(),
		context.mediaPixelScale);
	if (size.isEmpty() || block.thumbnailRequestSize == size) {
		return;
	}
	block.thumbnailRequestSize = size;
	if (const auto image = block.photoRuntime->thumbnail(size)) {
		if (image != block.thumbnailImage) {
			block.previousThumbnailImage = std::move(block.thumbnailImage);
			block.thumbnailImage = image;
		}
		if (image != block.subscribedThumbnailImage) {
			block.subscribedThumbnailImage = image;
			const auto repaint = context.caches.repaint;
			const auto repaintRect = context.caches.repaintRect;
			const auto rect = block.mediaRect;
			image->subscribeToUpdates([repaint, repaintRect, rect] {
				if (repaintRect && !rect.isEmpty()) {
					repaintRect(rect);
				} else if (repaint) {
					repaint();
				}
			});
		}
	}
}

void RefreshRelatedArticleImages(
		const LaidOutBlock &block,
		const MarkdownArticlePaintContext &context) {
	if (!block.photoRuntime || block.thumbnailRect.isEmpty()) {
		return;
	}
	const auto size = ScaledImageRequestSize(
		block.thumbnailRect.size(),
		context.mediaPixelScale);
	RefreshResolvedBlockImage(
		block,
		context,
		size,
		&block.thumbnailRequestSize,
		&block.thumbnailImage,
		&block.previousThumbnailImage,
		&block.subscribedThumbnailImage,
		[&](QSize requested) {
			return block.photoRuntime->thumbnail(requested);
		});
	RefreshResolvedBlockImage(
		block,
		context,
		size,
		&block.fullRequestSize,
		&block.fullImage,
		&block.previousFullImage,
		&block.subscribedFullImage,
		[&](QSize requested) {
			return block.photoRuntime->full(requested);
		});
}

void PaintTextLeaf(
		Painter &p,
		const Ui::Text::String &leaf,
		const MarkdownArticlePaintContext &context,
		QRect rect,
		int width,
		style::align align = style::al_left,
		std::optional<TextSelection> selection = std::nullopt,
		int elisionLines = 0,
		int segmentIndex = -1) {
	const auto availableWidth = std::max(width, 1);
	auto linePostprocess = std::optional<Ui::Text::LinePostprocess>();
	if (context.reveal && !elisionLines) {
		const auto lineCount = CachedRevealLineCount(
			context,
			&leaf,
			[&] {
				return int(leaf.countLinesGeometry(
					availableWidth).size());
			});
		const auto baseLine = context.reveal->nextLine;
		context.reveal->nextLine += lineCount;
		if (const auto articlePostprocess = context.reveal->postprocess) {
			const auto activeLine = context.reveal->activeLine;
			linePostprocess.emplace(Ui::Text::LinePostprocess{
				.method = [=](int lineIndex) -> Fn<void(QImage&)> {
					const auto globalLine = baseLine + lineIndex;
					if (globalLine != activeLine
						|| !articlePostprocess->method) {
						return nullptr;
					}
					return articlePostprocess->method(
						globalLine,
						availableWidth);
				},
				.cache = articlePostprocess->cache,
			});
		}
	}
	p.save();
	if (!context.clip.isNull()) {
		p.setClipRect(context.clip, Qt::IntersectClip);
	}
	const auto makeContext = [&] {
		return Ui::Text::PaintContext{
			.position = rect.topLeft(),
			.availableWidth = availableWidth,
			.geometry = (elisionLines
				? Ui::Text::SimpleGeometry(availableWidth, elisionLines, 0, false)
				: TextGeometry(availableWidth)),
			.align = align,
			.clip = context.clip,
			.palette = &p.textPalette(),
			.pre = context.caches.pre,
			.blockquote = context.caches.blockquote,
			.colors = context.caches.colors,
			.spoiler = Ui::Text::DefaultSpoilerCache(),
			.now = context.now,
			.elisionLines = elisionLines,
		};
	};
	auto drawContext = makeContext();
	drawContext.selection = selection.value_or(TextSelection());
	drawContext.linePostprocess = linePostprocess ? &*linePostprocess : nullptr;
	leaf.draw(p, drawContext);
	const auto searchRanges = PaintSearchRangesForSegmentIndex(
		context.selectionState,
		context.searchState,
		segmentIndex);
	if (!searchRanges.empty()) {
		const auto makePalette = [&](
				const style::color &bg,
				const style::color &fg) {
			auto result = p.textPalette();
			result.selectBg = bg;
			result.selectFg = fg;
			result.selectLinkFg = fg;
			result.selectMonoFg = fg;
			result.selectSpoilerFg = fg;
			return result;
		};
		const auto otherPalette = makePalette(
			st::searchedTextMatchBg,
			st::searchedTextMatchFg);
		const auto currentPalette = makePalette(
			st::searchedTextCurrentMatchBg,
			st::searchedTextCurrentMatchFg);
		const auto paintMatch = [&](
				TextSelection range,
				const style::TextPalette &palette) {
			auto path = QPainterPath();
			auto request = Ui::Text::HighlightInfoRequest{
				.range = range,
				.outPath = &path,
			};
			auto composeContext = makeContext();
			composeContext.highlight = &request;
			p.save();
			p.setClipRect(QRect(), Qt::ReplaceClip);
			leaf.draw(p, composeContext);
			p.restore();
			if (path.isEmpty()) {
				return;
			}
			path.setFillRule(Qt::WindingFill);
			auto matchContext = makeContext();
			matchContext.palette = &palette;
			matchContext.selection = range;
			p.save();
			p.setClipPath(path, Qt::IntersectClip);
			leaf.draw(p, matchContext);
			p.restore();
		};
		for (const auto range : searchRanges.other) {
			paintMatch(range, otherPalette);
		}
		if (searchRanges.current) {
			paintMatch(*searchRanges.current, currentPalette);
		}
	}
	p.restore();
}

void PaintSelectableTextLeaf(
		Painter &p,
		const Ui::Text::String &leaf,
		const MarkdownArticlePaintContext &context,
		QRect rect,
		int width,
		int segmentIndex,
		style::align align = style::al_left,
		std::optional<TextSelection> selection = std::nullopt,
		int elisionLines = 0) {
	if ((segmentIndex >= 0)
		&& ((context.hiddenTextSegmentIndex == segmentIndex)
			|| (context.hiddenSegmentIndex == segmentIndex))) {
		if (context.reveal && !elisionLines) {
			context.reveal->nextLine += CachedRevealLineCount(
				context,
				&leaf,
				[&] { return CountTextRevealLines(leaf, rect, width); });
		}
		return;
	}
	PaintTextLeaf(
		p,
		leaf,
		context,
		rect,
		width,
		align,
		selection,
		elisionLines,
		segmentIndex);
}

[[nodiscard]] QRect FlowTextViewportRect(const LaidOutBlock &block) {
	return block.scrollViewportRect.isEmpty()
		? block.outer
		: block.scrollViewportRect;
}

void SetTextLeafPen(
		Painter &p,
		const LaidOutBlock &block,
		const style::Markdown &st,
		const MarkdownArticlePaintContext &context) {
	const auto &paintSt = PaintStyle(context, st);
	p.setPen(!block.supplementary
		? paintSt.textColor->c
		: context.caches.supplementaryColorOverride.value_or(
			paintSt.supplementaryTextColor->c));
}

void PaintThinkingTextLeafDirect(
		Painter &p,
		const LaidOutBlock &block,
		const style::Markdown &st,
		const MarkdownArticlePaintContext &context) {
	const auto &paintSt = PaintStyle(context, st);
	p.setPen(paintSt.supplementaryTextColor->c);
	PaintSelectableTextLeaf(
		p,
		block.leaf,
		context,
		block.textRect,
		block.textWidth,
		block.segmentIndex,
		style::al_left,
		PaintTextSelectionForSegmentIndex(
			context.selectionState,
			block.segmentIndex));
}

void PaintRelatedArticleTextLeaf(
		Painter &p,
		const Ui::Text::String &leaf,
		const MarkdownArticlePaintContext &context,
		QRect rect,
		int width,
		int elisionLines) {
	const auto textClip = context.clip.intersected(rect);
	if (textClip.isEmpty()) {
		return;
	}
	const auto clipped = ClippedContext(context, textClip);
	p.save();
	p.setClipRect(textClip, Qt::IntersectClip);
	PaintTextLeaf(
		p,
		leaf,
		clipped,
		rect,
		width,
		style::al_left,
		std::nullopt,
		elisionLines);
	p.restore();
}

void PaintTaskMarker(
		Painter &p,
		const LaidOutBlock &block,
		const style::Markdown &st,
		const MarkdownArticlePaintContext &context,
		int outerWidth) {
	const auto rect = block.markerRect;
	if (rect.isEmpty()) {
		return;
	}
	const auto &paintSt = PaintStyle(context, st);
	if (block.taskMarkerRippleRuntime && block.taskMarkerRippleRuntime->ripple) {
		const auto rippleTopLeft = rect.topLeft()
			+ st::defaultCheckbox.rippleAreaPosition;
		block.taskMarkerRippleRuntime->ripple->paint(
			p,
			rippleTopLeft.x(),
			rippleTopLeft.y(),
			outerWidth);
	}
	auto view = Ui::CheckView(
		paintSt.list.taskCheck,
		block.taskState == TaskState::Checked);
	view.finishAnimating();
	view.paint(p, rect.left(), rect.top(), outerWidth);
}

void PaintBulletMarker(
		Painter &p,
		const LaidOutBlock &block,
		const style::Markdown &st,
		const MarkdownArticlePaintContext &context) {
	const auto radius = st.list.bulletRadius;
	if (radius <= 0) {
		return;
	}
	const auto &paintSt = PaintStyle(context, st);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(paintSt.list.bulletFg->c);
	p.drawEllipse(QPointF(block.markerCenter), radius, radius);
}

[[nodiscard]] const QImage &ColorizedDisplayFormulaImage(
		const LaidOutBlock &block,
		const RenderedFormula &formula,
		QColor color) {
	const auto size = formula.image.size();
	if (block.colorizedFormulaImage.isNull()
		|| (block.colorizedFormulaSize != size)
		|| (block.colorizedFormulaColor != color)) {
		block.colorizedFormulaImage = QImage(
			size,
			QImage::Format_ARGB32_Premultiplied);
		style::colorizeImage(
			formula.image,
			color,
			&block.colorizedFormulaImage,
			QRect(),
			QPoint(),
			true);
		block.colorizedFormulaColor = color;
		block.colorizedFormulaSize = size;
	}
	return block.colorizedFormulaImage;
}

[[nodiscard]] int TableBorder(
		const LaidOutBlock &block,
		const style::Markdown &st) {
	return block.tableBordered ? st.table.border : 0;
}

struct TableOwnershipSlot {
	const LaidOutTableCell *cell = nullptr;
};

using TableOwnershipGrid = std::vector<std::vector<TableOwnershipSlot>>;

[[nodiscard]] TableOwnershipGrid BuildTableOwnershipGrid(
		const LaidOutBlock &block) {
	const auto rowCount = int(block.tableRows.size());
	const auto columnCount = int(block.tableColumnWidths.size());
	auto result = TableOwnershipGrid(
		std::max(rowCount, 0),
		std::vector<TableOwnershipSlot>(std::max(columnCount, 0)));
	for (auto row = 0; row != rowCount; ++row) {
		for (const auto &cell : block.tableRows[row].cells) {
			const auto fromRow = std::clamp(row, 0, rowCount);
			const auto toRow = std::clamp(row + cell.rowspan, 0, rowCount);
			const auto fromColumn = std::clamp(cell.column, 0, columnCount);
			const auto toColumn = std::clamp(
				cell.column + cell.colspan,
				0,
				columnCount);
			for (auto currentRow = fromRow; currentRow != toRow; ++currentRow) {
				for (auto currentColumn = fromColumn;
					currentColumn != toColumn;
					++currentColumn) {
					result[currentRow][currentColumn].cell = &cell;
				}
			}
		}
	}
	return result;
}

[[nodiscard]] QPainterPath TableShapePath(
		const LaidOutBlock &block,
		int border,
		int radius) {
	auto path = QPainterPath();
	if (block.visibleTableRect.isEmpty()) {
		return path;
	}
	if (border > 0) {
		const auto half = border / 2.;
		path.addRoundedRect(
			QRectF(block.visibleTableRect).marginsRemoved({
				half,
				half,
				half,
				half,
			}),
			radius,
			radius);
	} else {
		path.addRect(block.visibleTableRect);
	}
	return path;
}

[[nodiscard]] auto TableColumnLefts(
		const LaidOutBlock &block,
		int columnCount,
		int border) -> std::vector<int> {
	auto result = std::vector<int>(
		std::max(columnCount, 0),
		block.visibleTableRect.x() + border - block.horizontalScrollLeft);
	auto separatorLeft = block.visibleTableRect.x()
		+ border
		- block.horizontalScrollLeft;
	for (auto column = 0; column != columnCount; ++column) {
		result[column] = separatorLeft;
		if (column < int(block.tableColumnWidths.size())) {
			separatorLeft += block.tableColumnWidths[column] + border;
		}
	}
	return result;
}

void AddTableHorizontalBorderSegments(
		QPainterPath *path,
		const LaidOutBlock &block,
		const TableOwnershipGrid &ownership,
		const std::vector<int> &columnLefts,
		QRectF inner,
		float64 half) {
	const auto rowCount = int(ownership.size());
	const auto columnCount = rowCount ? int(ownership.front().size()) : 0;
	for (auto boundaryRow = 1; boundaryRow != rowCount; ++boundaryRow) {
		auto segmentStart = -1;
		for (auto column = 0; column != columnCount; ++column) {
			const auto split = ownership[boundaryRow - 1][column].cell
				!= ownership[boundaryRow][column].cell;
			if (split && (segmentStart < 0)) {
				segmentStart = column;
			} else if (!split && (segmentStart >= 0)) {
				const auto fromX = (segmentStart == 0)
					? inner.x()
					: (columnLefts[segmentStart] - half);
				const auto toX = columnLefts[column] - half;
				const auto y = block.tableRows[boundaryRow].outer.y() - half;
				path->moveTo(fromX, y);
				path->lineTo(toX, y);
				segmentStart = -1;
			}
		}
		if (segmentStart >= 0) {
			const auto fromX = (segmentStart == 0)
				? inner.x()
				: (columnLefts[segmentStart] - half);
			const auto y = block.tableRows[boundaryRow].outer.y() - half;
			path->moveTo(fromX, y);
			path->lineTo(inner.x() + inner.width(), y);
		}
	}
}

void AddTableVerticalBorderSegments(
		QPainterPath *path,
		const LaidOutBlock &block,
		const TableOwnershipGrid &ownership,
		const std::vector<int> &columnLefts,
		QRectF inner,
		float64 half) {
	const auto rowCount = int(ownership.size());
	const auto columnCount = rowCount ? int(ownership.front().size()) : 0;
	for (auto boundaryColumn = 1;
		boundaryColumn != columnCount;
		++boundaryColumn) {
		auto segmentStart = -1;
		for (auto row = 0; row != rowCount; ++row) {
			const auto split = ownership[row][boundaryColumn - 1].cell
				!= ownership[row][boundaryColumn].cell;
			if (split && (segmentStart < 0)) {
				segmentStart = row;
			} else if (!split && (segmentStart >= 0)) {
				const auto fromY = (segmentStart == 0)
					? inner.y()
					: (block.tableRows[segmentStart].outer.y() - half);
				const auto toY = block.tableRows[row].outer.y() - half;
				const auto x = columnLefts[boundaryColumn] - half;
				path->moveTo(x, fromY);
				path->lineTo(x, toY);
				segmentStart = -1;
			}
		}
		if (segmentStart >= 0) {
			const auto fromY = (segmentStart == 0)
				? inner.y()
				: (block.tableRows[segmentStart].outer.y() - half);
			const auto x = columnLefts[boundaryColumn] - half;
			path->moveTo(x, fromY);
			path->lineTo(x, inner.y() + inner.height());
		}
	}
}

[[nodiscard]] QPainterPath TableBorderPath(
		const LaidOutBlock &block,
		int border,
		QPainterPath path) {
	const auto ownership = BuildTableOwnershipGrid(block);
	const auto rowCount = int(ownership.size());
	const auto columnCount = rowCount
		? int(ownership.front().size())
		: int(block.tableColumnWidths.size());
	if (!rowCount || !columnCount) {
		return path;
	}
	const auto half = border / 2.;
	const auto columnLefts = TableColumnLefts(block, columnCount, border);
	const auto inner = QRectF(block.visibleTableRect).marginsRemoved({
		half,
		half,
		half,
		half,
	});
	AddTableHorizontalBorderSegments(
		&path,
		block,
		ownership,
		columnLefts,
		inner,
		half);
	AddTableVerticalBorderSegments(
		&path,
		block,
		ownership,
		columnLefts,
		inner,
		half);
	return path;
}

[[nodiscard]] QRect TableRowRevealBand(
		const LaidOutBlock &block,
		const style::Markdown &st,
		int rowIndex) {
	if (block.visibleTableRect.isEmpty()
		|| rowIndex < 0
		|| rowIndex >= int(block.tableRows.size())) {
		return QRect();
	}
	const auto &row = block.tableRows[rowIndex];
	if (row.outer.height() <= 0) {
		return QRect();
	}
	const auto border = std::max(TableBorder(block, st), 0);
	const auto top = (rowIndex == 0)
		? block.visibleTableRect.y()
		: row.outer.y();
	const auto tableBottom = block.visibleTableRect.y()
		+ block.visibleTableRect.height();
	const auto bottom = std::min(
		row.outer.y() + row.outer.height() + border,
		tableBottom);
	if (bottom <= top) {
		return QRect();
	}
	return QRect(
		block.visibleTableRect.x(),
		top,
		block.visibleTableRect.width(),
		bottom - top);
}

[[nodiscard]] auto TableCellsForRowBand(
		const TableOwnershipGrid &ownership,
		int rowIndex,
		QRect rowBand) -> std::vector<const LaidOutTableCell*> {
	if (rowIndex < 0 || rowIndex >= int(ownership.size())) {
		return {};
	}
	auto result = std::vector<const LaidOutTableCell*>();
	result.reserve(ownership[rowIndex].size());
	for (const auto &slot : ownership[rowIndex]) {
		const auto cell = slot.cell;
		if (!cell || !cell->outer.intersects(rowBand)) {
			continue;
		} else if (std::find(result.begin(), result.end(), cell)
			!= result.end()) {
			continue;
		}
		result.push_back(cell);
	}
	return result;
}

[[nodiscard]] int TableCellOriginRow(
		const LaidOutBlock &block,
		const LaidOutTableCell *cell) {
	for (auto rowIndex = 0;
		rowIndex != int(block.tableRows.size());
		++rowIndex) {
		for (const auto &current : block.tableRows[rowIndex].cells) {
			if (&current == cell) {
				return rowIndex;
			}
		}
	}
	return -1;
}

[[nodiscard]] bool StructuralTableRowOverlaySelected(
		const PaintSelectionState &selectionState,
		const LaidOutTableRow &row) {
	return row.editRow
		&& StructuralTableRowSelected(selectionState, *row.editRow);
}

[[nodiscard]] bool StructuralTableCellRowOverlaySelected(
		const PaintSelectionState &selectionState,
		const LaidOutTableCell &cell) {
	if (!cell.editCell) {
		return false;
	}
	auto row = PreparedEditTableRowSource();
	row.block = cell.editCell->block;
	row.tableRowIndex = cell.editCell->tableRowIndex;
	return StructuralTableRowSelected(selectionState, row);
}

void PaintStructuralTableOverlays(
		Painter &p,
		const LaidOutBlock &block,
		const QPainterPath &shapePath,
		QRect clip,
		const MarkdownArticlePaintContext &context) {
	if (!context.selectionState.hasStructuralSelection() || clip.isEmpty()) {
		return;
	}
	p.save();
	p.setClipRect(clip, Qt::IntersectClip);
	p.setClipPath(shapePath, Qt::IntersectClip);
	for (const auto &row : block.tableRows) {
		if (!row.outer.intersects(clip)
			|| !StructuralTableRowOverlaySelected(
				context.selectionState,
				row)) {
			continue;
		}
		p.fillRect(row.outer, p.textPalette().selectOverlay);
	}
	for (const auto &row : block.tableRows) {
		if (!row.outer.intersects(clip)
			|| StructuralTableRowOverlaySelected(
				context.selectionState,
				row)) {
			continue;
		}
		for (const auto &cell : row.cells) {
			if (!cell.outer.intersects(clip)
				|| !cell.editCell
				|| StructuralTableCellRowOverlaySelected(
					context.selectionState,
					cell)
				|| !StructuralTableCellSelected(
					context.selectionState,
					*cell.editCell)) {
				continue;
			}
			p.fillRect(cell.outer, p.textPalette().selectOverlay);
		}
	}
	p.restore();
}

void PaintStructuralTableRowBandOverlays(
		Painter &p,
		const LaidOutBlock &block,
		const std::vector<const LaidOutTableCell*> &cells,
		int rowIndex,
		const QPainterPath &shapePath,
		QRect clip,
		const MarkdownArticlePaintContext &context) {
	if (!context.selectionState.hasStructuralSelection()
		|| clip.isEmpty()
		|| rowIndex < 0
		|| rowIndex >= int(block.tableRows.size())) {
		return;
	}
	const auto &row = block.tableRows[rowIndex];
	p.save();
	p.setClipRect(clip, Qt::IntersectClip);
	p.setClipPath(shapePath, Qt::IntersectClip);
	if (StructuralTableRowOverlaySelected(context.selectionState, row)) {
		p.fillRect(row.outer, p.textPalette().selectOverlay);
	}
	for (const auto cell : cells) {
		if (!cell
			|| !cell->outer.intersects(clip)
			|| !cell->editCell
			|| StructuralTableCellRowOverlaySelected(
				context.selectionState,
				*cell)
			|| !StructuralTableCellSelected(
				context.selectionState,
				*cell->editCell)) {
			continue;
		}
		p.fillRect(cell->outer, p.textPalette().selectOverlay);
	}
	p.restore();
}

void PaintTableCaption(
		Painter &p,
		const LaidOutBlock &block,
		const style::Markdown &st,
		const MarkdownArticlePaintContext &context) {
	if (!block.textRect.isEmpty()) {
		const auto selection = PaintTextSelectionForSegmentIndex(
			context.selectionState,
			block.secondarySegmentIndex);
		if (!PaintEditPlaceholderLeaf(
				p,
				block.placeholderLeaf,
				block.leaf.isEmpty(),
				context,
				block.textRect,
				block.textWidth,
				block.secondarySegmentIndex,
				st,
				block.flowTextAlign,
				selection)) {
			SetTextLeafPen(p, block, st, context);
			PaintSelectableTextLeaf(
				p,
				block.leaf,
				context,
				block.textRect,
				block.textWidth,
				block.secondarySegmentIndex,
				block.flowTextAlign,
				selection);
		}
	}
}

[[nodiscard]] int OverflowIndicatorWidth(
		const LaidOutBlock &block,
		const style::Markdown &st) {
	return (block.kind == PreparedBlockKind::DisplayMath)
		? st.displayMath.overflowWidth
		: st.table.overflowWidth;
}

[[nodiscard]] QColor ScrollOwnerAccentColor(
		const LaidOutBlock &block,
		const style::Markdown &st,
		const MarkdownArticlePaintContext &context) {
	const auto &paintSt = PaintStyle(context, st);
	switch (block.kind) {
	case PreparedBlockKind::Table:
		return paintSt.table.overflowFg->c;
	case PreparedBlockKind::DisplayMath:
		return paintSt.displayMath.overflowFg->c;
	case PreparedBlockKind::CodeBlock:
		return paintSt.textPalette.monoFg->c;
	case PreparedBlockKind::Quote:
		return NonPullquoteQuoteCaptionColor(context, st);
	case PreparedBlockKind::Details:
		return paintSt.supplementaryTextColor->c;
	case PreparedBlockKind::EmbedPost:
		return paintSt.embedPost.accentFg->c;
	case PreparedBlockKind::Paragraph:
	case PreparedBlockKind::Thinking:
	case PreparedBlockKind::Heading:
	case PreparedBlockKind::Rule:
	case PreparedBlockKind::List:
	case PreparedBlockKind::ListItem:
	case PreparedBlockKind::Photo:
	case PreparedBlockKind::Video:
	case PreparedBlockKind::Audio:
	case PreparedBlockKind::Map:
	case PreparedBlockKind::Channel:
	case PreparedBlockKind::GroupedMedia:
	case PreparedBlockKind::RelatedArticle:
	case PreparedBlockKind::Placeholder:
		return paintSt.table.overflowFg->c;
	}
	return paintSt.table.overflowFg->c;
}

[[nodiscard]] bool HorizontalRightEdgeHidden(const LaidOutBlock &block) {
	return block.horizontalScrollMax > 0
		&& (block.horizontalScrollLeft < block.horizontalScrollMax);
}

[[nodiscard]] QRect HorizontalOverflowContentClip(
		const LaidOutBlock &block,
		const style::Markdown &st,
		QRect viewport) {
	if (block.horizontalScrollMax <= 0 || viewport.isEmpty()) {
		return viewport;
	}
	const auto indicatorWidth = std::min(
		std::max(OverflowIndicatorWidth(block, st), 1),
		viewport.width());
	const auto left = (block.horizontalScrollLeft > 0)
		? indicatorWidth
		: 0;
	const auto right = HorizontalRightEdgeHidden(block)
		? indicatorWidth
		: 0;
	const auto width = std::max(viewport.width() - left - right, 0);
	return QRect(
		viewport.x() + left,
		viewport.y(),
		width,
		viewport.height());
}

[[nodiscard]] QRect HorizontalScrollLogicalPaintRect(
		const LaidOutBlock &block) {
	if (block.insideHorizontalScroll) {
		return block.logicalGeometry.outer.translated(
			block.horizontalScrollAncestorShift,
			0);
	}
	return block.outer;
}

void PaintHorizontalOverflowIndicators(
		Painter &p,
		const LaidOutBlock &block,
		const style::Markdown &st,
		const MarkdownArticlePaintContext &context,
		QRect viewport,
		const QPainterPath *clipPath = nullptr) {
	if (block.horizontalScrollMax <= 0 || viewport.isEmpty()) {
		return;
	}
	const auto indicatorWidth = std::min(
		std::max(OverflowIndicatorWidth(block, st), 1),
		viewport.width());
	if (indicatorWidth <= 0) {
		return;
	}
	const auto left = (block.horizontalScrollLeft > 0);
	const auto right = HorizontalRightEdgeHidden(block);
	if (!left && !right) {
		return;
	}
	const auto color = ScrollOwnerAccentColor(block, st, context);
	p.save();
	p.setClipRect(viewport, Qt::IntersectClip);
	if (clipPath) {
		p.setClipPath(*clipPath, Qt::IntersectClip);
	}
	if (left) {
		p.fillRect(
			QRect(
				viewport.x(),
				viewport.y(),
				indicatorWidth,
				viewport.height()),
			color);
	}
	if (right) {
		p.fillRect(
			QRect(
				viewport.x()
					+ viewport.width()
					- indicatorWidth,
				viewport.y(),
				indicatorWidth,
				viewport.height()),
			color);
	}
	p.restore();
}

void PaintHorizontalScrollbar(
		Painter &p,
		const LaidOutBlock &block,
		const style::Markdown &st,
		const MarkdownArticlePaintContext &context) {
	if (block.horizontalScrollMax <= 0
		|| block.scrollScrollbarTrackRect.isEmpty()) {
		return;
	}
	const auto scrollbarRect = block.scrollScrollbarTrackRect.united(
		block.scrollScrollbarThumbRect);
	const auto scrollbarClip = context.clip.intersected(scrollbarRect);
	if (scrollbarClip.isEmpty()) {
		return;
	}
	const auto &paintSt = PaintStyle(context, st);
	auto trackBg = QColor();
	auto thumbBg = QColor();
	if (block.kind == PreparedBlockKind::Table) {
		trackBg = EffectiveTableHeaderBg(paintSt, context);
		thumbBg = EffectiveTableBorderFg(paintSt, context);
	} else {
		thumbBg = ScrollOwnerAccentColor(block, st, context);
		trackBg = thumbBg;
		trackBg.setAlphaF(trackBg.alphaF() * 0.25);
	}
	const auto radius = block.scrollScrollbarTrackRect.height() / 2.;
	p.save();
	p.setClipRect(scrollbarClip, Qt::IntersectClip);
	if (radius > 0.) {
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(trackBg);
		p.drawRoundedRect(
			QRectF(block.scrollScrollbarTrackRect),
			radius,
			radius);
		if (!block.scrollScrollbarThumbRect.isEmpty()) {
			p.setBrush(thumbBg);
			p.drawRoundedRect(
				QRectF(block.scrollScrollbarThumbRect),
				radius,
				radius);
		}
	} else {
		p.fillRect(block.scrollScrollbarTrackRect, trackBg);
		if (!block.scrollScrollbarThumbRect.isEmpty()) {
			p.fillRect(block.scrollScrollbarThumbRect, thumbBg);
		}
	}
	p.restore();
}

void PaintWholeTable(
		Painter &p,
		const LaidOutBlock &block,
		const style::Markdown &st,
		const MarkdownArticlePaintContext &context) {
	const auto tableClip = context.clip.intersected(block.visibleTableRect);
	if (tableClip.isEmpty()) {
		return;
	}
	const auto textClip = tableClip.intersected(
		HorizontalOverflowContentClip(block, st, block.visibleTableRect));
	const auto tableContext = ClippedContext(context, textClip);

	const auto border = TableBorder(block, st);
	const auto radius = st.table.radius;
	const auto shapePath = TableShapePath(block, border, radius);
	const auto &paintSt = PaintStyle(context, st);
	const auto headerBg = EffectiveTableHeaderBg(paintSt, context);
	const auto borderFg = EffectiveTableBorderFg(paintSt, context);

	p.save();
	p.setClipRect(tableClip);
	p.save();
	p.setClipPath(shapePath, Qt::IntersectClip);
	for (auto rowIndex = 0, rowCount = int(block.tableRows.size()); rowIndex != rowCount; ++rowIndex) {
		const auto striped = block.tableStriped && ((rowIndex % 2) == 0);
		for (const auto &cell : block.tableRows[rowIndex].cells) {
			if (!cell.outer.intersects(tableClip)) {
				continue;
			}
			if (!cell.header && !striped) {
				continue;
			}
			p.fillRect(cell.outer, headerBg);
		}
	}
	p.restore();

	if (border > 0 && !block.visibleTableRect.isEmpty()) {
		const auto path = TableBorderPath(block, border, shapePath);
		auto hq = PainterHighQualityEnabler(p);
		auto pen = QPen(borderFg, border);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		p.drawPath(path);
	}

	p.setPen(paintSt.textColor->c);
	for (const auto &row : block.tableRows) {
		if (!row.outer.intersects(tableClip)) {
			continue;
		}
		for (const auto &cell : row.cells) {
			if (!cell.textRect.intersects(tableClip)) {
				continue;
			}
			const auto selection = PaintTextSelectionForSegmentIndex(
				context.selectionState,
				cell.segmentIndex);
			if (!PaintEditPlaceholderLeaf(
					p,
					cell.placeholderLeaf,
					cell.leaf.isEmpty(),
					tableContext,
					cell.textRect,
					cell.textWidth,
					cell.segmentIndex,
					st,
					cell.align,
					selection)) {
				PaintSelectableTextLeaf(
					p,
					cell.leaf,
					tableContext,
					cell.textRect,
					cell.textWidth,
					cell.segmentIndex,
					cell.align,
					selection);
			}
		}
	}

	if (block.segmentIndex >= 0
		&& WholeSegmentSelected(
			context.selectionState,
			block.segmentIndex)) {
		p.save();
		p.setClipPath(shapePath, Qt::IntersectClip);
		p.fillRect(block.visibleTableRect, p.textPalette().selectOverlay);
		p.restore();
	}
	PaintStructuralTableOverlays(
		p,
		block,
		shapePath,
		tableClip,
		context);

	PaintHorizontalOverflowIndicators(
		p,
		block,
		st,
		context,
		block.visibleTableRect,
		&shapePath);

	p.restore();
}

void PaintTableRowBand(
		Painter &p,
		const LaidOutBlock &block,
		const TableOwnershipGrid &ownership,
		int rowIndex,
		QRect rowBand,
		const style::Markdown &st,
		const MarkdownArticlePaintContext &context) {
	const auto rowClip = context.clip.intersected(rowBand);
	if (rowClip.isEmpty()) {
		return;
	}

	const auto border = TableBorder(block, st);
	const auto radius = st.table.radius;
	const auto shapePath = TableShapePath(block, border, radius);
	const auto &paintSt = PaintStyle(context, st);
	const auto headerBg = EffectiveTableHeaderBg(paintSt, context);
	const auto borderFg = EffectiveTableBorderFg(paintSt, context);
	const auto cells = TableCellsForRowBand(ownership, rowIndex, rowBand);
	const auto rowContext = ClippedContext(
		RevealSuppressedContext(context),
		rowClip.intersected(
			HorizontalOverflowContentClip(block, st, block.visibleTableRect)));

	p.save();
	p.setClipRect(rowClip, Qt::IntersectClip);
	p.save();
	p.setClipPath(shapePath, Qt::IntersectClip);
	for (const auto cell : cells) {
		if (!cell || !cell->outer.intersects(rowClip)) {
			continue;
		}
		const auto originRow = TableCellOriginRow(block, cell);
		const auto striped = block.tableStriped
			&& (originRow >= 0)
			&& ((originRow % 2) == 0);
		if (!cell->header && !striped) {
			continue;
		}
		p.fillRect(cell->outer, headerBg);
	}
	p.restore();

	if (border > 0 && !block.visibleTableRect.isEmpty()) {
		const auto path = TableBorderPath(block, border, shapePath);
		auto hq = PainterHighQualityEnabler(p);
		auto pen = QPen(borderFg, border);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		p.drawPath(path);
	}

	p.setPen(paintSt.textColor->c);
	for (const auto cell : cells) {
		if (!cell || !cell->textRect.intersects(rowClip)) {
			continue;
		}
		const auto selection = PaintTextSelectionForSegmentIndex(
			context.selectionState,
			cell->segmentIndex);
		if (!PaintEditPlaceholderLeaf(
				p,
				cell->placeholderLeaf,
				cell->leaf.isEmpty(),
				rowContext,
				cell->textRect,
				cell->textWidth,
				cell->segmentIndex,
				st,
				cell->align,
				selection)) {
			PaintSelectableTextLeaf(
				p,
				cell->leaf,
				rowContext,
				cell->textRect,
				cell->textWidth,
				cell->segmentIndex,
				cell->align,
				selection);
		}
	}

	if (block.segmentIndex >= 0
		&& WholeSegmentSelected(
			context.selectionState,
			block.segmentIndex)) {
		p.save();
		p.setClipPath(shapePath, Qt::IntersectClip);
		p.fillRect(block.visibleTableRect, p.textPalette().selectOverlay);
		p.restore();
	}
	PaintStructuralTableRowBandOverlays(
		p,
		block,
		cells,
		rowIndex,
		shapePath,
		rowClip,
		context);

	PaintHorizontalOverflowIndicators(
		p,
		block,
		st,
		context,
		block.visibleTableRect,
		&shapePath);

	p.restore();
}

void PaintTableBlock(
		Painter &p,
		const LaidOutBlock &block,
		const style::Markdown &st,
		const MarkdownArticlePaintContext &context) {
	PaintTableCaption(p, block, st, context);
	if (!context.reveal) {
		PaintWholeTable(p, block, st, context);
	} else {
		const auto ownership = BuildTableOwnershipGrid(block);
		for (auto rowIndex = 0;
			rowIndex != int(block.tableRows.size());
			++rowIndex) {
			const auto rowBand = TableRowRevealBand(block, st, rowIndex);
			PaintRevealBand(
				p,
				context,
				rowBand,
				[&](Painter &p, const MarkdownArticlePaintContext &rowContext) {
					PaintTableRowBand(
						p,
						block,
						ownership,
						rowIndex,
						rowBand,
						st,
						rowContext);
				});
		}
	}
	PaintHorizontalScrollbar(p, block, st, context);
}

void PaintDisplayMathBlock(
		Painter &p,
		const LaidOutBlock &block,
		std::vector<PreparedFormulaSlot> *formulas,
		std::vector<RenderedFormula> *renderedFormulas,
		MathRenderer *renderer,
		int devicePixelRatio,
		const style::Markdown &st,
		const MarkdownArticlePaintContext &context) {
	PaintRevealBand(
		p,
		context,
		block.visibleFormulaRect,
		[&](Painter &p, const MarkdownArticlePaintContext &formulaContext) {
			if (block.segmentIndex >= 0
				&& (formulaContext.hiddenSegmentIndex == block.segmentIndex)) {
				return;
			}
			const auto &paintSt = PaintStyle(formulaContext, st);
			const auto formula = PreparedFormulaFor(formulas, block.formulaIndex);
			p.setPen(paintSt.textColor->c);
			const auto rendered = EnsureFormulaRendered(
				formula,
				FormulaRasterSlot(renderedFormulas, block.formulaIndex),
				renderer,
				devicePixelRatio,
				st);
			if (rendered.success) {
				p.drawImage(
					block.formulaRect.topLeft(),
					ColorizedDisplayFormulaImage(
						block,
						rendered,
						p.pen().color()));
			}
			if (!rendered.success) {
				if (!PaintEditPlaceholderLeaf(
						p,
						block.placeholderLeaf,
						block.copyText.trimmed().isEmpty(),
						formulaContext,
						block.textRect,
						block.textWidth,
						block.segmentIndex,
						st,
						block.formulaAlign)) {
					p.setPen(paintSt.textColor->c);
					PaintSelectableTextLeaf(
						p,
						block.fallbackLeaf,
						formulaContext,
						block.textRect,
						block.textWidth,
						block.segmentIndex,
						block.formulaAlign);
				}
			}

			if (block.segmentIndex >= 0
				&& WholeSegmentSelected(
					formulaContext.selectionState,
					block.segmentIndex)) {
				p.fillRect(
					block.visibleFormulaRect,
					p.textPalette().selectOverlay);
			}

		});
	PaintHorizontalOverflowIndicators(
		p,
		block,
		st,
		context,
		block.scrollViewportRect.isEmpty()
			? block.visibleFormulaRect
			: block.scrollViewportRect);
	PaintHorizontalScrollbar(p, block, st, context);
}

void PaintQuoteBlock(
		Painter &p,
		const LaidOutBlock &block,
		std::vector<PreparedFormulaSlot> *formulas,
		std::vector<RenderedFormula> *renderedFormulas,
		MathRenderer *renderer,
		int devicePixelRatio,
		int outerWidth,
		const style::Markdown &st,
		const MarkdownArticlePaintContext &context) {
	const auto quoteClip = context.clip.intersected(block.outer);
	if (quoteClip.isEmpty()) {
		return;
	}

	if (context.caches.blockquote) {
		const auto &quoteStyle = st.body.blockquote;
		Ui::Text::ValidateQuotePaintCache(
			*context.caches.blockquote,
			quoteStyle);
		if (!block.pullquote) {
			p.save();
			p.setClipRect(quoteClip);
			Ui::Text::FillQuotePaint(
				p,
				HorizontalScrollLogicalPaintRect(block),
				*context.caches.blockquote,
				quoteStyle);
			p.restore();
		} else {
			p.save();
			p.setClipRect(quoteClip);
			p.setPen(Qt::NoPen);
			p.setBrush(context.caches.blockquote->bg);
			auto hq = PainterHighQualityEnabler(p);
			p.drawRoundedRect(block.outer, quoteStyle.radius, quoteStyle.radius);
			if (!quoteStyle.icon.empty()) {
				const auto icon = quoteStyle.icon.instance(
					context.caches.blockquote->icon);
				if (!icon.isNull()) {
					const auto reserve = PullquoteIconReserveWidth(quoteStyle);
					const auto left = block.contentRect.x()
						- reserve
						+ quoteStyle.iconPosition.x();
					const auto top = block.outer.y()
						+ st.pullquote.padding.top()
						+ quoteStyle.iconPosition.y();
					const auto right = block.contentRect.x()
						+ block.contentRect.width()
						+ reserve
						- quoteStyle.iconPosition.x()
						- quoteStyle.icon.width();
					const auto bottom = block.outer.y()
						+ block.outer.height()
						- st.pullquote.padding.bottom()
						- quoteStyle.iconPosition.y()
						- quoteStyle.icon.height();
					p.drawImage(
						QRect(
							left,
							top,
							quoteStyle.icon.width(),
							quoteStyle.icon.height()),
						icon);
					p.drawImage(
						QRect(
							right,
							bottom,
							quoteStyle.icon.width(),
							quoteStyle.icon.height()),
						icon.mirrored(true, false));
				}
			}
			p.restore();
		}
	}

	auto local = ClippedContext(
		context,
		context.clip.intersected(block.contentRect));
	local.caches.supplementaryColorOverride
		= NonPullquoteQuoteCaptionColor(context, st);
	PaintBlocks(
		p,
		block.children,
		formulas,
		renderedFormulas,
		renderer,
		devicePixelRatio,
		outerWidth,
		st,
		local);
	PaintHorizontalOverflowIndicators(
		p,
		block,
		st,
		context,
		block.scrollViewportRect);
	PaintHorizontalScrollbar(p, block, st, context);
}

void PaintCodeBlock(
		Painter &p,
		const LaidOutBlock &block,
		const style::Markdown &st,
		const MarkdownArticlePaintContext &context) {
	const auto clip = context.clip.intersected(block.outer);
	if (clip.isEmpty()) {
		return;
	}

	const auto &pre = st.code.pre;
	if (context.caches.pre) {
		Ui::Text::ValidateQuotePaintCache(*context.caches.pre, pre);

		p.save();
		p.setClipRect(clip);
		Ui::Text::FillQuotePaint(
			p,
			block.outer,
			*context.caches.pre,
			pre);
		p.restore();
	}

	if (!block.headerRect.isEmpty()) {
		const auto headerText = block.codeLanguage.isEmpty()
			? Ui::Integration::Instance().phraseQuoteHeaderCopy()
			: block.codeLanguage;
		const auto font = st.code.font->monospace();
		const auto availableWidth = block.outer.width()
			- pre.headerPosition.x()
			- pre.iconPosition.x()
			- (!pre.icon.empty() ? pre.icon.width() : 0);
		if (availableWidth > 0) {
			const auto position = block.outer.topLeft()
				+ pre.headerPosition;
			p.save();
			p.setClipRect(clip);
			p.setFont(font);
			p.setPen(p.textPalette().monoFg->c);
			p.drawText(
				position + QPoint(0, font->ascent),
				font->elided(headerText, availableWidth));
			p.restore();
		}
	}

	const auto textClip = context.clip.intersected(block.contentRect);
	const auto textContext = ClippedContext(context, textClip);
	p.save();
	const auto selection = PaintTextSelectionForSegmentIndex(
		textContext.selectionState,
		block.segmentIndex);
	if (!PaintEditPlaceholderLeaf(
			p,
			block.placeholderLeaf,
			block.codeText.text.isEmpty(),
			textContext,
			block.textRect,
			block.textWidth,
			block.segmentIndex,
			st,
			style::al_left,
			selection)) {
		p.setPen(p.textPalette().monoFg->c);
		PaintSelectableTextLeaf(
			p,
			block.leaf,
			textContext,
			block.textRect,
			block.textWidth,
			block.segmentIndex,
			style::al_left,
			selection);
	}
	p.restore();
	PaintHorizontalOverflowIndicators(
		p,
		block,
		st,
		context,
		block.scrollViewportRect.isEmpty()
			? block.contentRect
			: block.scrollViewportRect);
	PaintHorizontalScrollbar(p, block, st, context);
}

void PaintPlaceholderBlock(
		Painter &p,
		const LaidOutBlock &block,
		int outerWidth,
		const style::Markdown &st,
		const MarkdownArticlePaintContext &context) {
	PaintRevealBand(
		p,
		context,
		block.visibleMediaRect,
		[&](Painter &p, const MarkdownArticlePaintContext &visibleContext) {
			auto hq = PainterHighQualityEnabler(p);
			if (block.activation.kind == MediaActivationKind::Embed
				&& block.placeholderRuntime) {
				const auto border = st.placeholder.border;
				const auto radius = st.placeholder.radius;
				const auto borderSkip = border / 2;
				const auto borderRect = block.mediaRect.marginsRemoved(QMargins(
					borderSkip,
					borderSkip,
					borderSkip,
					borderSkip));
				const auto active = ClickHandler::showAsActive(
					block.placeholderRuntime->clickHandler);
				const auto pressed = ClickHandler::showAsPressed(
					block.placeholderRuntime->clickHandler);
				p.setPen(Qt::NoPen);
				p.setBrush(st.placeholder.bg);
				p.drawRoundedRect(block.mediaRect, radius, radius);
				if (active || pressed) {
					p.setBrush(st.placeholder.bgActive);
					p.drawRoundedRect(block.mediaRect, radius, radius);
				}
				if (const auto &ripple = block.placeholderRuntime->ripple) {
					ripple->paint(
						p,
						block.mediaRect.x(),
						block.mediaRect.y(),
						outerWidth,
						&st.placeholder.rippleBg->c);
				}
				auto pen = QPen(st.placeholder.borderFg->c);
				pen.setWidth(border);
				p.setPen(pen);
				p.setBrush(Qt::NoBrush);
				p.drawRoundedRect(borderRect, radius, radius);
				if (block.placeholderRuntime->loading) {
					const auto size = QSize(
						st.placeholder.spinnerSize,
						st.placeholder.spinnerSize);
					const auto spinner = style::centerrect(
						block.mediaRect,
						QRect(QPoint(), size));
					Ui::InfiniteRadialAnimation::Draw(
						p,
						block.placeholderRuntime->loadingAnimation.computeState(),
						spinner.topLeft(),
						spinner.size(),
						outerWidth,
						QPen(st.placeholder.spinnerFg->c),
						st.placeholder.spinnerWidth);
				} else {
					p.setPen(st.placeholder.labelFgActive->c);
					PaintTextLeaf(
						p,
						block.labelLeaf,
						visibleContext,
						block.labelRect,
						block.labelWidth,
						style::al_center);
				}
			} else {
				const auto max = block.labelLeaf.maxWidth();
				const auto radius = st.placeholder.radius;
				p.setBrush(st.placeholder.bg);
				p.setPen(Qt::NoPen);
				const auto skip = (max < block.labelRect.width())
					? ((block.labelRect.width() - max) / 2)
					: 0;
				p.drawRoundedRect(
					block.labelRect.marginsRemoved(
						{ skip, 0, skip, 0 }
					).marginsAdded(st.placeholder.padding),
					radius,
					radius);
				p.setPen(st.placeholder.labelFg->c);
				PaintTextLeaf(
					p,
					block.labelLeaf,
					visibleContext,
					block.labelRect,
					block.labelWidth,
					style::al_center);
			}
			if (block.segmentIndex >= 0
				&& WholeSegmentSelected(
					visibleContext.selectionState,
					block.segmentIndex)) {
				p.fillRect(block.visibleMediaRect, p.textPalette().selectOverlay);
			}
		});
	if (!block.textRect.isEmpty()) {
		const auto selection = PaintTextSelectionForSegmentIndex(
			context.selectionState,
			block.secondarySegmentIndex);
		if (!PaintEditPlaceholderLeaf(
				p,
				block.placeholderLeaf,
				block.leaf.isEmpty(),
				context,
				block.textRect,
				block.textWidth,
				block.secondarySegmentIndex,
				st,
				style::al_left,
				selection)) {
			SetTextLeafPen(p, block, st, context);
			PaintSelectableTextLeaf(
				p,
				block.leaf,
				context,
				block.textRect,
				block.textWidth,
				block.secondarySegmentIndex,
				style::al_left,
				selection);
		}
	}
}

[[nodiscard]] QPainterPath RoundedRectPath(QRect rect, int radius) {
	auto path = QPainterPath();
	path.addRoundedRect(QRectF(rect), radius, radius);
	return path;
}

void PaintEmbedPostBlock(
		Painter &p,
		const LaidOutBlock &block,
		std::vector<PreparedFormulaSlot> *formulas,
		std::vector<RenderedFormula> *renderedFormulas,
		MathRenderer *renderer,
		int devicePixelRatio,
		int outerWidth,
		const style::Markdown &st,
		const MarkdownArticlePaintContext &context) {
	const auto &style = st.embedPost;
	const auto paintHeader = [&](
			Painter &p,
			const MarkdownArticlePaintContext &headerContext) {
		RefreshBlockThumbnail(block, headerContext);
		if (style.accentWidth > 0) {
			p.fillRect(
				QRect(
					block.mediaRect.x(),
					block.mediaRect.y(),
					style.accentWidth,
					block.mediaRect.height()),
				style.accentFg->c);
		}
		if (block.photoRuntime && !block.thumbnailRect.isEmpty()) {
			auto hq = PainterHighQualityEnabler(p);
			const auto avatarPath = RoundedRectPath(
				block.thumbnailRect,
				style.avatarRadius);
			p.save();
			p.setClipPath(
				avatarPath,
				Qt::IntersectClip);
			(void)PaintThumbnailImage(
				p,
				block.thumbnailRect,
				block.thumbnailImage,
				block.previousThumbnailImage,
				headerContext.mediaPixelScale);
			p.restore();
		}
		if (!block.labelRect.isEmpty()) {
			p.setPen(style.authorFg->c);
			PaintSelectableTextLeaf(
				p,
				block.labelLeaf,
				headerContext,
				block.labelRect,
				block.labelWidth,
				block.segmentIndex,
				style::al_left,
				PaintTextSelectionForSegmentIndex(
					headerContext.selectionState,
					block.segmentIndex));
		}
		if (!block.subtitleRect.isEmpty()) {
			p.setPen(style.dateFg->c);
			PaintSelectableTextLeaf(
				p,
				block.subtitleLeaf,
				headerContext,
				block.subtitleRect,
				block.subtitleWidth,
				block.secondarySegmentIndex,
				style::al_left,
				PaintTextSelectionForSegmentIndex(
					headerContext.selectionState,
					block.secondarySegmentIndex));
		}
	};
	if (context.reveal) {
		if (!block.headerRect.isEmpty()) {
			const auto headerBottom = block.headerRect.y()
				+ block.headerRect.height();
			PaintRevealBand(
				p,
				context,
				QRect(
					block.mediaRect.x(),
					block.mediaRect.y(),
					block.mediaRect.width(),
					headerBottom - block.mediaRect.y()),
				paintHeader);
		} else if (block.children.empty()) {
			PaintRevealBand(p, context, block.mediaRect, paintHeader);
		}
	} else {
		const auto mediaClip = context.clip.intersected(block.mediaRect);
		if (!mediaClip.isEmpty()) {
			const auto mediaContext = ClippedContext(context, mediaClip);
			p.save();
			p.setClipRect(mediaClip);
			paintHeader(p, mediaContext);
			p.restore();
		}
	}
	if (context.reveal
		&& style.accentWidth > 0
		&& !block.children.empty()) {
		const auto top = !block.headerRect.isEmpty()
			? block.headerRect.y() + block.headerRect.height()
			: block.mediaRect.y();
		const auto bottom = block.mediaRect.y() + block.mediaRect.height();
		const auto accentRect = QRect(
			block.mediaRect.x(),
			top,
			style.accentWidth,
			std::max(bottom - top, 0));
		const auto accentClip = context.clip.intersected(accentRect);
		if (!accentClip.isEmpty()) {
			p.save();
			p.setClipRect(accentClip);
			p.fillRect(accentRect, style.accentFg->c);
			p.restore();
		}
	}
	if (!block.bodyRect.isEmpty()) {
		const auto bodyContext = ClippedContext(
			context,
			context.clip.intersected(
				block.scrollViewportRect.isEmpty()
					? block.bodyRect
					: block.scrollViewportRect));
		PaintBlocks(
			p,
			block.children,
			formulas,
			renderedFormulas,
			renderer,
			devicePixelRatio,
			outerWidth,
			st,
			bodyContext);
	}
	PaintHorizontalOverflowIndicators(
		p,
		block,
		st,
		context,
		block.scrollViewportRect);
	PaintHorizontalScrollbar(p, block, st, context);
	if (!block.textRect.isEmpty()) {
		SetTextLeafPen(p, block, st, context);
		PaintSelectableTextLeaf(
			p,
			block.leaf,
			context,
			block.textRect,
			block.textWidth,
			block.tertiarySegmentIndex,
			style::al_left,
			PaintTextSelectionForSegmentIndex(
				context.selectionState,
				block.tertiarySegmentIndex));
	}
}

void PaintMediaCaption(
		Painter &p,
		const LaidOutBlock &block,
		const style::Markdown &st,
		const MarkdownArticlePaintContext &context) {
	if (block.textRect.isEmpty()) {
		return;
	}
	const auto selection = PaintTextSelectionForSegmentIndex(
		context.selectionState,
		block.secondarySegmentIndex);
	if (!PaintEditPlaceholderLeaf(
			p,
			block.placeholderLeaf,
			block.leaf.isEmpty(),
			context,
			block.textRect,
			block.textWidth,
			block.secondarySegmentIndex,
			st,
			style::al_left,
			selection)) {
		SetTextLeafPen(p, block, st, context);
		PaintSelectableTextLeaf(
			p,
			block.leaf,
			context,
			block.textRect,
			block.textWidth,
			block.secondarySegmentIndex,
			style::al_left,
			selection);
	}
}

void PaintPersistentMediaBlock(
		Painter &p,
		const LaidOutBlock &block,
		const MarkdownArticlePaintContext &context) {
	PaintRevealBand(
		p,
		context,
		block.visibleMediaRect,
		[&](Painter &p, const MarkdownArticlePaintContext &mediaContext) {
			if (block.mediaBlock) {
				block.mediaBlock->paint(p, mediaContext);
			}
			if (block.segmentIndex >= 0
				&& WholeSegmentSelected(
					mediaContext.selectionState,
					block.segmentIndex)) {
				p.fillRect(block.mediaRect, p.textPalette().selectOverlay);
			}
		});
}

void PaintCardSurface(
		Painter &p,
		QRect rect,
		int border,
		const style::color &borderFg,
		const style::color &bg,
		int radius) {
	if (rect.isEmpty()) {
		return;
	}
	if (border <= 0) {
		if (radius > 0) {
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(bg->c);
			p.drawRoundedRect(rect, radius, radius);
		} else {
			p.fillRect(rect, bg->c);
		}
		return;
	}
	const auto half = border / 2.;
	const auto inner = QRectF(rect).marginsRemoved({
		half,
		half,
		half,
		half,
	});
	if (radius > 0) {
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(QPen(borderFg->c, border));
		p.setBrush(bg->c);
		p.drawRoundedRect(inner, radius, radius);
	} else {
		p.fillRect(rect, bg->c);
		p.setPen(QPen(borderFg->c, border));
		p.setBrush(Qt::NoBrush);
		p.drawRect(inner);
	}
}

void PaintAudioBlock(
		Painter &p,
		const LaidOutBlock &block,
		const style::Markdown &st,
		const MarkdownArticlePaintContext &context) {
	PaintPersistentMediaBlock(p, block, context);
	PaintMediaCaption(p, block, st, context);
}

void PaintChannelBlock(
		Painter &p,
		const LaidOutBlock &block,
		const style::Markdown &st,
		const MarkdownArticlePaintContext &context) {
	PaintPersistentMediaBlock(p, block, context);
	PaintMediaCaption(p, block, st, context);
}

void PaintRelatedArticleBlock(
		Painter &p,
		const LaidOutBlock &block,
		const style::Markdown &st,
		const MarkdownArticlePaintContext &context) {
	PaintRevealBand(
		p,
		context,
		block.visibleMediaRect,
		[&](Painter &p, const MarkdownArticlePaintContext &visibleContext) {
			const auto &style = st.relatedArticle;
			RefreshRelatedArticleImages(block, visibleContext);
			PaintCardSurface(
				p,
				block.mediaRect,
				style.border,
				style.borderFg,
				style.bg,
				style.radius);
			if (!block.thumbnailRect.isEmpty()) {
				p.fillRect(block.thumbnailRect, style.bg->c);
				if (style.thumbnailRadius > 0) {
					auto hq = PainterHighQualityEnabler(p);
					auto path = RoundedRectPath(
						block.thumbnailRect,
						style.thumbnailRadius);
					p.save();
					p.setClipPath(path, Qt::IntersectClip);
					(void)PaintRelatedArticleImage(
						p,
						block.thumbnailRect,
						block.thumbnailImage,
						block.fullImage,
						block.previousThumbnailImage,
						block.previousFullImage,
						visibleContext.mediaPixelScale);
					p.restore();
				} else {
					(void)PaintRelatedArticleImage(
						p,
						block.thumbnailRect,
						block.thumbnailImage,
						block.fullImage,
						block.previousThumbnailImage,
						block.previousFullImage,
						visibleContext.mediaPixelScale);
				}
			}
			if (!block.labelRect.isEmpty()) {
				p.setPen(st.textColor->c);
				PaintRelatedArticleTextLeaf(
					p,
					block.labelLeaf,
					visibleContext,
					block.labelRect,
					block.labelWidth,
					style.titleLines);
			}
			if (!block.subtitleRect.isEmpty()) {
				p.setPen(st.textColor->c);
				PaintRelatedArticleTextLeaf(
					p,
					block.subtitleLeaf,
					visibleContext,
					block.subtitleRect,
					block.subtitleWidth,
					style.subtitleLines);
			}
			if (!block.actionRect.isEmpty()) {
				p.setPen(st.supplementaryTextColor->c);
				PaintRelatedArticleTextLeaf(
					p,
					block.actionLeaf,
					visibleContext,
					block.actionRect,
					block.actionWidth,
					style.footerLines);
			}
			if (block.segmentIndex >= 0
				&& WholeSegmentSelected(
					visibleContext.selectionState,
					block.segmentIndex)) {
				p.fillRect(
					block.visibleMediaRect,
					p.textPalette().selectOverlay);
			}
			if (style.separator > 0) {
				p.fillRect(
					QRect(
						block.mediaRect.x(),
						block.mediaRect.y()
							+ block.mediaRect.height()
							- style.separator,
						block.mediaRect.width(),
						style.separator),
					style.separatorFg->c);
			}
		});
}

void PaintPhotoBlock(
		Painter &p,
		const LaidOutBlock &block,
		const style::Markdown &st,
		const MarkdownArticlePaintContext &context) {
	PaintPersistentMediaBlock(p, block, context);
	PaintMediaCaption(p, block, st, context);
}

void PaintVideoBlock(
		Painter &p,
		const LaidOutBlock &block,
		const style::Markdown &st,
		const MarkdownArticlePaintContext &context) {
	PaintPersistentMediaBlock(p, block, context);
	PaintMediaCaption(p, block, st, context);
}

void PaintMapBlock(
		Painter &p,
		const LaidOutBlock &block,
		const style::Markdown &st,
		const MarkdownArticlePaintContext &context) {
	PaintPersistentMediaBlock(p, block, context);
	PaintMediaCaption(p, block, st, context);
}

void PaintGroupedMediaBlock(
		Painter &p,
		const LaidOutBlock &block,
		const style::Markdown &st,
		const MarkdownArticlePaintContext &context) {
	PaintRevealBand(
		p,
		context,
		block.visibleMediaRect,
		[&](Painter &p, const MarkdownArticlePaintContext &mediaContext) {
			if (block.mediaBlock) {
				block.mediaBlock->paint(p, mediaContext);
				if ((block.segmentIndex >= 0)
					&& WholeSegmentSelected(
						mediaContext.selectionState,
						block.segmentIndex)) {
					const auto &style = PaintStyle(
						mediaContext,
						st).groupedMedia;
					auto overlay = p.textPalette().selectOverlay->c;
					overlay.setAlphaF(std::clamp(
						style.overlayOpacity,
						0.,
						1.));
					p.fillPath(
						RoundedRectPath(block.mediaRect, style.radius),
						overlay);
				}
			}
		});
	PaintMediaCaption(p, block, st, context);
}

void PaintDetailsBlock(
		Painter &p,
		const LaidOutBlock &block,
		std::vector<PreparedFormulaSlot> *formulas,
		std::vector<RenderedFormula> *renderedFormulas,
		MathRenderer *renderer,
		int devicePixelRatio,
		int outerWidth,
		const style::Markdown &st,
		const MarkdownArticlePaintContext &context) {
	const auto visible = context.clip.intersected(block.outer);
	if (visible.isEmpty()) {
		return;
	}

	const auto &details = st.details;
	const auto &paintSt = PaintStyle(context, st);
	const auto lineHeight = details.border;
	const auto lineRect = QRect(
		block.outer.x(),
		block.outer.y() + block.outer.height() - lineHeight,
		block.outer.width(),
		lineHeight);
	const auto lineFg = EffectiveDividerFg(paintSt, context);

	p.save();
	p.setClipRect(visible);
	if (lineHeight > 0 && !lineRect.isEmpty()) {
		p.fillRect(lineRect, lineFg);
	}
	if (!block.iconRect.isEmpty()) {
		const auto collapsed = block.actionRect.isEmpty()
			? block.collapsed
			: !block.detailsOpen;
		PaintDetailsIcon(
			p,
			block.iconRect,
			paintSt.supplementaryTextColor->c,
			collapsed);
	}
	const auto selection = PaintTextSelectionForSegmentIndex(
		context.selectionState,
		block.segmentIndex);
	if (!PaintEditPlaceholderLeaf(
			p,
			block.placeholderLeaf,
			block.leaf.isEmpty(),
			context,
			block.textRect,
			block.textWidth,
			block.segmentIndex,
			st,
			style::al_left,
			selection)) {
		p.setPen(paintSt.textColor->c);
		PaintSelectableTextLeaf(
			p,
			block.leaf,
			context,
			block.textRect,
			block.textWidth,
			block.segmentIndex,
			style::al_left,
			selection);
	}
	if (!block.actionRect.isEmpty()) {
		p.setPen(paintSt.supplementaryTextColor->c);
		PaintTextLeaf(
			p,
			block.actionLeaf,
			context,
			block.actionRect,
			block.actionRect.width(),
			block.rtl ? style::al_left : style::al_right);
	}
	p.restore();

	if (!block.bodyRect.isEmpty()) {
		const auto bodyContext = ClippedContext(
			context,
			context.clip.intersected(block.bodyRect));
		PaintBlocks(
			p,
			block.children,
			formulas,
			renderedFormulas,
			renderer,
			devicePixelRatio,
			outerWidth,
			st,
			bodyContext);
	}
	PaintHorizontalOverflowIndicators(
		p,
		block,
		st,
		context,
		block.scrollViewportRect);
	PaintHorizontalScrollbar(p, block, st, context);
}

void PaintThinkingBlock(
		Painter &p,
		const LaidOutBlock &block,
		int devicePixelRatio,
		const style::Markdown &st,
		const MarkdownArticlePaintContext &context) {
	const auto viewport = FlowTextViewportRect(block);
	const auto contentClip = context.clip.intersected(viewport);
	const auto &paintSt = PaintStyle(context, st);
	const auto baseColor = paintSt.supplementaryTextColor;
	const auto selection = PaintTextSelectionForSegmentIndex(
		context.selectionState,
		block.segmentIndex);
	const auto logicalRect = viewport;
	const auto visible = context.clip.intersected(block.outer);
	if (visible.isEmpty()) {
		AdvanceRevealLinesForBlock(context, block, st);
		return;
	}

	const auto thinking = context.caches.thinking;
	const auto pathShiftGradient = context.caches.pathShiftGradient;
	if (!thinking || !pathShiftGradient) {
		if (!contentClip.isEmpty()) {
			PaintThinkingTextLeafDirect(
				p,
				block,
				st,
				ClippedContext(context, contentClip));
		}
		PaintHorizontalOverflowIndicators(
			p,
			block,
			st,
			context,
			viewport);
		PaintHorizontalScrollbar(p, block, st, context);
		return;
	}
	if (selection && !selection->empty()) {
		if (!contentClip.isEmpty()) {
			PaintThinkingTextLeafDirect(
				p,
				block,
				st,
				ClippedContext(context, contentClip));
		}
		PaintHorizontalOverflowIndicators(
			p,
			block,
			st,
			context,
			viewport);
		PaintHorizontalScrollbar(p, block, st, context);
		return;
	}

	const auto ratio = std::max(devicePixelRatio, 1);
	const auto cacheWidth = logicalRect.width() * ratio;
	const auto cacheHeight = logicalRect.height() * ratio;
	EnsureThinkingPaintCacheImage(
		&thinking->mask,
		logicalRect.size(),
		ratio);
	EnsureThinkingPaintCacheImage(
		&thinking->gradient,
		logicalRect.size(),
		ratio);
	thinking->mask.fill(Qt::transparent);

	{
		auto maskPainter = Painter(&thinking->mask);
		maskPainter.translate(-logicalRect.topLeft());
		maskPainter.setClipRect(logicalRect);
		maskPainter.setPen(QColor(255, 255, 255));
		PaintSelectableTextLeaf(
			maskPainter,
			block.leaf,
			ClippedContext(context, logicalRect),
			block.textRect,
			block.textWidth,
			block.segmentIndex,
			style::al_left,
			TextSelection());
	}

	const auto textWidth = ThinkingTextWidth(block);
	const auto highlightColor = anim::color(
		baseColor->c,
		::st::radialFg->c,
		0.8);
	const auto localRect = QRect(QPoint(), logicalRect.size());
	const auto painted = pathShiftGradient->paint(
		[&](const Ui::PathShiftGradient::Background &) {
			FillThinkingGradientImage(
				&thinking->gradient,
				logicalRect,
				block.textRect,
				textWidth,
				baseColor->c,
				highlightColor);
			auto gradientPainter = Painter(&thinking->gradient);
			gradientPainter.setCompositionMode(
				QPainter::CompositionMode_DestinationIn);
			gradientPainter.drawImage(
				QRectF(localRect),
				thinking->mask,
				QRectF(0, 0, cacheWidth, cacheHeight));
			gradientPainter.setCompositionMode(
				QPainter::CompositionMode_SourceOver);
			return true;
		});
	if (!painted) {
		PaintHorizontalOverflowIndicators(
			p,
			block,
			st,
			context,
			viewport);
		PaintHorizontalScrollbar(p, block, st, context);
		return;
	}

	p.save();
	p.setClipRect(contentClip, Qt::IntersectClip);
	p.drawImage(
		QRectF(logicalRect),
		thinking->gradient,
		QRectF(0, 0, cacheWidth, cacheHeight));
	p.restore();
	PaintHorizontalOverflowIndicators(
		p,
		block,
		st,
		context,
		viewport);
	PaintHorizontalScrollbar(p, block, st, context);
}

void PaintStructuralBlockOverlay(
		Painter &p,
		const LaidOutBlock &block,
		const MarkdownArticlePaintContext &context) {
	if (!context.selectionState.hasStructuralSelection()
		|| block.outer.isEmpty()) {
		return;
	}
	const auto selected = (block.editBlock
			&& StructuralBlockSelected(
				context.selectionState,
				*block.editBlock))
		|| (block.editListItem
			&& StructuralListItemSelected(
				context.selectionState,
				*block.editListItem));
	if (!selected) {
		return;
	}
	const auto visible = context.clip.intersected(block.outer);
	if (visible.isEmpty()) {
		return;
	}
	p.save();
	p.setClipRect(visible, Qt::IntersectClip);
	p.fillRect(block.outer, p.textPalette().selectOverlay);
	p.restore();
}

void PaintBlock(
		Painter &p,
		const LaidOutBlock &block,
		std::vector<PreparedFormulaSlot> *formulas,
		std::vector<RenderedFormula> *renderedFormulas,
		MathRenderer *renderer,
		int devicePixelRatio,
		int outerWidth,
		const style::Markdown &st,
		const MarkdownArticlePaintContext &context) {
	if (!block.outer.intersects(context.clip)
		&& block.kind != PreparedBlockKind::Thinking) {
		return;
	}
	if (context.debugBlockGeometry && !block.outer.isEmpty()) {
		switch (reinterpret_cast<quintptr>(&block) % 4) {
		case 0:
			p.fillRect(block.outer, QColor(128, 0, 0, 32));
			break;
		case 1:
			p.fillRect(block.outer, QColor(0, 128, 0, 32));
			break;
		case 2:
			p.fillRect(block.outer, QColor(0, 0, 128, 32));
			break;
		default:
			p.fillRect(block.outer, QColor(128, 128, 128, 32));
			break;
		}
	}
	const auto &paintSt = PaintStyle(context, st);

	switch (block.kind) {
	case PreparedBlockKind::Thinking:
		PaintThinkingBlock(p, block, devicePixelRatio, st, context);
		break;
	case PreparedBlockKind::Paragraph:
	case PreparedBlockKind::Heading:
		if (!block.headerRect.isEmpty()) {
			p.fillRect(block.headerRect, paintSt.relatedArticle.headerBg->c);
		}
		{
			const auto flowContext = ClippedContext(
				context,
				context.clip.intersected(FlowTextViewportRect(block)));
			const auto selection = PaintTextSelectionForSegmentIndex(
				flowContext.selectionState,
				block.segmentIndex);
			if (!PaintEditPlaceholderLeaf(
					p,
					block.placeholderLeaf,
					block.leaf.isEmpty(),
					flowContext,
					block.textRect,
					block.textWidth,
					block.segmentIndex,
					st,
					block.flowTextAlign,
					selection)) {
				SetTextLeafPen(p, block, st, context);
				PaintSelectableTextLeaf(
					p,
					block.leaf,
					flowContext,
					block.textRect,
					block.textWidth,
					block.segmentIndex,
					block.flowTextAlign,
					selection);
			}
		}
		PaintHorizontalOverflowIndicators(
			p,
			block,
			st,
			context,
			FlowTextViewportRect(block));
		PaintHorizontalScrollbar(p, block, st, context);
		break;
	case PreparedBlockKind::CodeBlock:
		PaintCodeBlock(p, block, st, context);
		break;
	case PreparedBlockKind::Rule:
		PaintRevealBand(
			p,
			context,
			block.outer,
			[&](Painter &p, const MarkdownArticlePaintContext &context) {
				p.fillRect(block.outer, EffectiveDividerFg(paintSt, context));
			});
		break;
	case PreparedBlockKind::List:
		{
			const auto childContext = block.scrollViewportRect.isEmpty()
				? context
				: ClippedContext(
					context,
					context.clip.intersected(block.scrollViewportRect));
			PaintBlocks(
				p,
				block.children,
				formulas,
				renderedFormulas,
				renderer,
				devicePixelRatio,
				outerWidth,
				st,
				childContext);
			PaintHorizontalOverflowIndicators(
				p,
				block,
				st,
				context,
				block.scrollViewportRect);
			PaintHorizontalScrollbar(p, block, st, context);
		}
		break;
	case PreparedBlockKind::ListItem:
		if (!context.reveal
			|| context.reveal->activeLine >= context.reveal->nextLine) {
			const auto markerContext = RevealSuppressedContext(context);
			if (block.taskState != TaskState::None) {
				PaintTaskMarker(p, block, st, markerContext, outerWidth);
			} else if (block.listKind == ListKind::Ordered
				&& !block.markerRect.isEmpty()) {
				p.setPen(paintSt.textColor->c);
				PaintTextLeaf(
					p,
					block.marker,
					markerContext,
					block.markerRect,
					block.markerWidth);
			} else if (block.listKind == ListKind::Bullet) {
				PaintBulletMarker(p, block, st, markerContext);
			}
		}
		{
			const auto childContext = block.scrollViewportRect.isEmpty()
				? context
				: ClippedContext(
					context,
					context.clip.intersected(block.scrollViewportRect));
			PaintBlocks(
				p,
				block.children,
				formulas,
				renderedFormulas,
				renderer,
				devicePixelRatio,
				outerWidth,
				st,
				childContext);
			PaintHorizontalOverflowIndicators(
				p,
				block,
				st,
				context,
				block.scrollViewportRect);
			PaintHorizontalScrollbar(p, block, st, context);
		}
		break;
	case PreparedBlockKind::Quote:
		PaintQuoteBlock(
			p,
			block,
			formulas,
			renderedFormulas,
			renderer,
			devicePixelRatio,
			outerWidth,
			st,
			context);
		break;
	case PreparedBlockKind::DisplayMath:
		PaintDisplayMathBlock(
			p,
			block,
			formulas,
			renderedFormulas,
			renderer,
			devicePixelRatio,
			st,
			context);
		break;
	case PreparedBlockKind::Table:
		PaintTableBlock(p, block, st, context);
		break;
	case PreparedBlockKind::Photo:
		PaintPhotoBlock(p, block, st, context);
		break;
	case PreparedBlockKind::Video:
		PaintVideoBlock(p, block, st, context);
		break;
	case PreparedBlockKind::Audio:
		PaintAudioBlock(p, block, st, context);
		break;
	case PreparedBlockKind::Map:
		PaintMapBlock(p, block, st, context);
		break;
	case PreparedBlockKind::Channel:
		PaintChannelBlock(p, block, st, context);
		break;
	case PreparedBlockKind::RelatedArticle:
		PaintRelatedArticleBlock(p, block, st, context);
		break;
	case PreparedBlockKind::EmbedPost:
		PaintEmbedPostBlock(
			p,
			block,
			formulas,
			renderedFormulas,
			renderer,
			devicePixelRatio,
			outerWidth,
			st,
			context);
		break;
	case PreparedBlockKind::Placeholder:
		PaintPlaceholderBlock(p, block, outerWidth, st, context);
		break;
	case PreparedBlockKind::GroupedMedia:
		PaintGroupedMediaBlock(p, block, st, context);
		break;
	case PreparedBlockKind::Details:
		PaintDetailsBlock(
			p,
			block,
			formulas,
			renderedFormulas,
			renderer,
			devicePixelRatio,
			outerWidth,
			st,
			context);
		break;
	}
	PaintStructuralBlockOverlay(p, block, context);
}

} // namespace

QColor NonPullquoteQuoteCaptionColor(
		const MarkdownArticlePaintContext &context,
		const style::Markdown &st) {
	if (!context.caches.blockquote) {
		return PaintStyle(context, st).supplementaryTextColor->c;
	}
	return context.caches.blockquote->icon;
}

void PaintBlocks(
		Painter &p,
		const std::vector<LaidOutBlock> &blocks,
		std::vector<PreparedFormulaSlot> *formulas,
		std::vector<RenderedFormula> *renderedFormulas,
		MathRenderer *renderer,
		int devicePixelRatio,
		int outerWidth,
		const style::Markdown &st,
		const MarkdownArticlePaintContext &context) {
	for (const auto &block : blocks) {
		if (block.outer.bottom() < context.clip.top()) {
			AdvanceRevealLinesForBlock(context, block, st);
			continue;
		} else if (block.outer.top() > context.clip.bottom()) {
			break;
		}
		PaintBlock(
			p,
			block,
			formulas,
			renderedFormulas,
			renderer,
			devicePixelRatio,
			outerWidth,
			st,
			context);
	}
}

} // namespace Iv::Markdown
