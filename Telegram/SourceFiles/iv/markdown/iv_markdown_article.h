/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/markdown/iv_markdown_common.h"
#include "iv/markdown/iv_markdown_media_block.h"
#include "iv/markdown/iv_markdown_prepare.h"
#include "iv/iv_rich_page.h"

#include "base/flat_map.h"
#include "spellcheck/spellcheck_highlight_syntax.h"
#include "ui/chat/chat_style.h"
#include "ui/effects/radial_animation.h"
#include "ui/effects/ripple_animation.h"
#include "ui/style/style_core_types.h"
#include "ui/text/text.h"
#include "ui/click_handler.h"
#include "ui/painter.h"

#include <QtGui/QColor>

#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace style {
struct Markdown;
struct TextStyle;
} // namespace style

namespace Ui {
class PathShiftGradient;
} // namespace Ui

namespace Iv::Markdown {

struct PlaceholderBlockRuntime {
	explicit PlaceholderBlockRuntime(Fn<void()> repaint);

	ClickHandlerPtr clickHandler;
	bool loading = false;
	Ui::InfiniteRadialAnimation loadingAnimation;
	std::unique_ptr<Ui::RippleAnimation> ripple;
	QSize rippleSize;
};

struct TaskMarkerRippleRuntime {
	explicit TaskMarkerRippleRuntime(Fn<void()> repaint);

	Fn<void()> repaint;
	std::unique_ptr<Ui::RippleAnimation> ripple;
	QSize rippleSize;
};

struct MarkdownArticleSelectionPosition {
	int segment = -1;
	int offset = 0;

	[[nodiscard]] bool valid() const {
		return (segment >= 0);
	}
};

inline bool operator==(
		MarkdownArticleSelectionPosition a,
		MarkdownArticleSelectionPosition b) {
	return (a.segment == b.segment) && (a.offset == b.offset);
}

inline bool operator!=(
		MarkdownArticleSelectionPosition a,
		MarkdownArticleSelectionPosition b) {
	return !(a == b);
}

struct MarkdownArticleSelection {
	MarkdownArticleSelectionPosition from;
	MarkdownArticleSelectionPosition to;

	[[nodiscard]] bool empty() const;
};

inline bool MarkdownArticleSelection::empty() const {
	return !from.valid()
		|| !to.valid()
		|| (from == to);
}

inline bool operator==(
		MarkdownArticleSelection a,
		MarkdownArticleSelection b) {
	return (a.from == b.from) && (a.to == b.to);
}

inline bool operator!=(
		MarkdownArticleSelection a,
		MarkdownArticleSelection b) {
	return !(a == b);
}

struct MarkdownArticleSelectionEndpoint {
	int segment = -1;
	bool direct = false;

	[[nodiscard]] bool valid() const {
		return (segment >= 0);
	}
};

struct MarkdownArticleSelectionEndpoints {
	MarkdownArticleSelectionEndpoint from;
	MarkdownArticleSelectionEndpoint to;
};

struct SelectableSegment;

struct PaintSelectionState {
	const std::vector<SelectableSegment> *segments = nullptr;
	MarkdownArticleSelection selection;
	const MarkdownArticleSelectionEndpoints *endpoints = nullptr;
	const PreparedEditSelection *structuralSelection = nullptr;

	[[nodiscard]] bool empty() const {
		return !segments || selection.empty();
	}
	[[nodiscard]] bool hasStructuralSelection() const {
		return structuralSelection && !structuralSelection->empty();
	}
};

struct MarkdownArticleSearchMatch {
	int segment = -1;
	int from = 0;
	int to = 0;
};

struct MarkdownArticleSearchSource {
	QString text;
	QString hiddenText;
	QString detailsAnchorId;
};

struct PaintSearchState {
	const std::vector<MarkdownArticleSearchMatch> *matches = nullptr;
	int current = -1;

	[[nodiscard]] bool empty() const {
		return !matches || matches->empty();
	}
};

struct MarkdownArticleThinkingPaintCache {
	QImage mask;
	QImage gradient;
};

struct MarkdownArticlePaintCaches {
	Ui::Text::QuotePaintCache *pre = nullptr;
	Ui::Text::QuotePaintCache *blockquote = nullptr;
	MarkdownArticleThinkingPaintCache *thinking = nullptr;
	Ui::PathShiftGradient *pathShiftGradient = nullptr;
	std::span<Ui::Text::SpecialColor> colors;
	const style::Markdown *st = nullptr;
	Fn<void()> repaint;
	Fn<void(QRect)> repaintRect;
	std::optional<QColor> supplementaryColorOverride;
};

using MarkdownArticleRevealLine = Ui::Text::LineLayoutInfo;

struct MarkdownArticleRevealPostprocess {
	Fn<Fn<void(QImage&)>(int lineIndex, int availableWidth)> method;
	not_null<QImage*> cache;
};

// Caches per-block / per-leaf reveal line counts while the reveal
// animation is active, so that painting doesn't re-run line breaking
// for all the blocks above the clip on every frame. Owned by the
// article, valid for a single layout generation, freed on the first
// paint after the reveal finishes.
struct MarkdownArticleRevealLineCountsCache {
	int layoutGeneration = -1;
	base::flat_map<const void*, int> counts;
};

struct MarkdownArticleRevealPaintState {
	int activeLine = -1;
	int nextLine = 0;
	const MarkdownArticleRevealPostprocess *postprocess = nullptr;
	MarkdownArticleRevealLineCountsCache *lineCounts = nullptr;
};

struct MarkdownArticlePaintContext final : Ui::ChatPaintContext {
	explicit MarkdownArticlePaintContext(const Ui::ChatPaintContext &context)
	: Ui::ChatPaintContext(context) {
	}

	MarkdownArticlePaintCaches caches;
	PaintSelectionState selectionState;
	PaintSearchState searchState;
	MarkdownArticleRevealPaintState *reveal = nullptr;
	int hiddenTextSegmentIndex = -1;
	int hiddenSegmentIndex = -1;
	bool debugBlockGeometry = false;
	double mediaPixelScale = 1.;

	[[nodiscard]] MarkdownArticlePaintContext translated(int x, int y) const {
		auto result = *this;
		result.translate(x, y);
		return result;
	}
	[[nodiscard]] MarkdownArticlePaintContext translated(QPoint point) const {
		return translated(point.x(), point.y());
	}
	[[nodiscard]] const style::Markdown &paintMarkdownStyle(
			const style::Markdown &fallback) const {
		return caches.st ? *caches.st : fallback;
	}
};

struct MarkdownArticleHitTestResult {
	int segmentIndex = -1;
	Ui::Text::StateResult state;
	std::optional<PreparedLink> preparedLink;
	MediaActivation mediaActivation;
	QPoint placeholderLocalPoint;
	int forcedOffset = -1;
	bool direct = false;
	bool codeHeaderCopy = false;

	[[nodiscard]] bool valid() const {
		return (segmentIndex >= 0);
	}
};

struct MarkdownArticleHorizontalScrollHit {
	bool scrollable = false;
	bool overViewport = false;
	bool overScrollbar = false;
	bool overScrollbarThumb = false;
};

enum class MarkdownArticleEditControlHitKind {
	None,
	TaskMarker,
	DetailsToggle,
};

struct MarkdownArticleEditControlHit {
	MarkdownArticleEditControlHitKind kind
		= MarkdownArticleEditControlHitKind::None;
	std::optional<PreparedEditListItemSource> listItem;
	std::optional<PreparedEditBlockSource> block;

	[[nodiscard]] bool valid() const {
		switch (kind) {
		case MarkdownArticleEditControlHitKind::TaskMarker:
			return listItem.has_value();
		case MarkdownArticleEditControlHitKind::DetailsToggle:
			return block.has_value();
		case MarkdownArticleEditControlHitKind::None:
			break;
		}
		return false;
	}
};

inline bool operator==(
		MarkdownArticleEditControlHit a,
		MarkdownArticleEditControlHit b) {
	return (a.kind == b.kind)
		&& (a.listItem == b.listItem)
		&& (a.block == b.block);
}

inline bool operator!=(
		MarkdownArticleEditControlHit a,
		MarkdownArticleEditControlHit b) {
	return !(a == b);
}

struct MarkdownArticleDropLocation {
	std::optional<PreparedEditDropTarget> target;
	QRect indicatorRect;

	[[nodiscard]] bool valid() const {
		return target.has_value();
	}
};

inline bool operator==(
		MarkdownArticleDropLocation a,
		MarkdownArticleDropLocation b) {
	return (a.target == b.target)
		&& (a.indicatorRect == b.indicatorRect);
}

inline bool operator!=(
		MarkdownArticleDropLocation a,
		MarkdownArticleDropLocation b) {
	return !(a == b);
}

struct MarkdownArticleTextLeafStyle {
	const style::TextStyle *textStyle = nullptr;
	style::color textColor;
	QColor markBg;
	int lineHeight = 0;
	style::align align = style::al_left;
	bool italic = false;

	[[nodiscard]] bool valid() const {
		return textStyle && textColor;
	}
};

struct MarkdownArticleAnchorExpansion {
	bool found = false;
	bool changed = false;
};

struct MarkdownArticleScrollAnchor {
	int segmentIndex = -1;
	double fraction = 0.;
};

struct MarkdownArticleMediaGeometry {
	PreparedEditBlockSource block;
	QRect mediaRect;
	QRect visibleMediaRect;
	bool grouped = false;
	std::vector<QRect> itemRects;
	int activeItemIndex = -1;
};

class MarkdownArticle {
public:
	MarkdownArticle(
		const style::Markdown &st,
		std::shared_ptr<MathRenderer> renderer = nullptr);
	~MarkdownArticle();

	MarkdownArticle(MarkdownArticle &&) noexcept;
	MarkdownArticle &operator=(MarkdownArticle &&) noexcept;

	void setRenderer(std::shared_ptr<MathRenderer> renderer);
	void setMediaBlockHost(MediaBlockHost *host);
	void setMediaPixelScale(double scale);
	void setTextRepaintCallbacks(
		Fn<void()> repaint,
		Fn<void(QRect)> repaintRect,
		Fn<bool(const ClickContext&)> spoilerLinkFilter = nullptr);
	void setContent(MarkdownArticleContent content);
	void setSearchMatches(
		std::vector<MarkdownArticleSearchMatch> matches,
		int current);
	[[nodiscard]] auto searchSources() const
	-> std::vector<MarkdownArticleSearchSource>;
	void updatePreparedLeaf(
		const PreparedEditLeafSource &source,
		const MarkdownArticleContent &prepared);
	void setEditableMaxLineWidthOverride(
		const PreparedEditLeafSource &source,
		int width);
	void setEditableTextEmptyOverride(
		const PreparedEditLeafSource &source,
		bool empty);
	void setEditableHeightOverride(int editableIndex, int height);
	void setEditableHeightOverrideForSegment(int segmentIndex, int height);
	void clearEditableMaxLineWidthOverride();
	void clearEditableTextEmptyOverride();
	void clearEditableHeightOverride();
	void setTextLeafHeightOverride(int textLeafIndex, int height);
	void clearTextLeafHeightOverride();
	void invalidateLayout();
	[[nodiscard]] int maxWidth() const;
	[[nodiscard]] int lastLayoutWidth() const;
	[[nodiscard]] bool hasMissingMediaBlocks() const;
	[[nodiscard]] int resizeGetHeight(int width);
	[[nodiscard]] auto countRevealLinesGeometry(int width)
	-> std::vector<MarkdownArticleRevealLine>;
	void setVisibleTopBottom(int visibleTop, int visibleBottom);
	void paint(Painter &p, const MarkdownArticlePaintContext &context) const;
	[[nodiscard]] MarkdownArticleHitTestResult hitTest(
		QPoint point,
		Ui::Text::StateRequest::Flags flags) const;
	[[nodiscard]] PreparedEditHit editHitTest(QPoint point) const;
	[[nodiscard]] MarkdownArticleDropLocation editDropTarget(
		QPoint point) const;
	[[nodiscard]] MarkdownArticleDropLocation editBlockDropTarget(
		QPoint point) const;
	[[nodiscard]] MarkdownArticleDropLocation editStructuralDropTarget(
		QPoint point,
		const PreparedEditSelection &selection) const;
	[[nodiscard]] MarkdownArticleEditControlHit editControlHitTest(
		QPoint point) const;
	void addTaskMarkerRipple(
		const PreparedEditListItemSource &source,
		QPoint point);
	[[nodiscard]] MarkdownArticleHorizontalScrollHit horizontalScrollHit(
		QPoint point) const;
	[[nodiscard]] bool canConsumeHorizontalScroll(
		QPoint point,
		int delta) const;
	[[nodiscard]] bool consumeHorizontalScroll(QPoint point, int delta);
	[[nodiscard]] bool beginHorizontalScroll(QPoint point, bool fromTouch);
	[[nodiscard]] bool updateHorizontalScroll(QPoint point);
	void endHorizontalScroll();
	[[nodiscard]] int anchorTop(const QString &anchorId) const;
	[[nodiscard]] auto scrollAnchorForTop(int top) const
	-> std::optional<MarkdownArticleScrollAnchor>;
	[[nodiscard]] int scrollTopForAnchor(
		const MarkdownArticleScrollAnchor &anchor) const;
	[[nodiscard]] MarkdownArticleAnchorExpansion expandDetailsToAnchor(
		const QString &anchorId);
	[[nodiscard]] MarkdownArticleAnchorExpansion expandDetailsBlock(
		const QString &anchorId);
	[[nodiscard]] bool toggleDetails(const QString &anchorId);
	[[nodiscard]] bool segmentIsText(int index) const;
	[[nodiscard]] bool segmentIsDisplayMath(int index) const;
	[[nodiscard]] bool segmentIsEditable(int index) const;
	[[nodiscard]] int segmentLength(int index) const;
	[[nodiscard]] int firstTextSegmentIndex() const;
	[[nodiscard]] int firstEditableSegmentIndex() const;
	[[nodiscard]] int textLeafIndexForSegment(int segmentIndex) const;
	[[nodiscard]] int segmentIndexForTextLeafIndex(int textLeafIndex) const;
	[[nodiscard]] int editableIndexForSegment(int segmentIndex) const;
	[[nodiscard]] int segmentIndexForEditableIndex(int editableIndex) const;
	[[nodiscard]] auto editableLeafForSegment(int segmentIndex) const
	-> std::optional<PreparedEditLeafSource>;
	[[nodiscard]] int segmentIndexForEditableLeaf(
		const PreparedEditLeafSource &source) const;
	[[nodiscard]] QRect textSegmentRect(int segmentIndex) const;
	[[nodiscard]] QRect logicalSegmentRect(int segmentIndex) const;
	[[nodiscard]] QRect segmentRect(int segmentIndex) const;
	[[nodiscard]] std::vector<MarkdownArticleMediaGeometry>
		mediaBlockGeometries() const;
	void setGroupedActiveIndex(
		const PreparedEditBlockSource &source,
		int index);
	[[nodiscard]] QRect displayMathEditRect(int segmentIndex) const;
	[[nodiscard]] QRect displayMathBlockRect(int segmentIndex) const;
	[[nodiscard]] int pullquoteAvailableTextWidthForEditableLeaf(
		const PreparedEditLeafSource &source) const;
	[[nodiscard]] bool revealSegment(int segmentIndex);
	[[nodiscard]] MarkdownArticleTextLeafStyle textLeafStyleForSegment(
		int segmentIndex) const;
	[[nodiscard]] MarkdownArticleTextLeafStyle editableStyleForSegment(
		int segmentIndex) const;
	[[nodiscard]] int selectionOffsetFromHit(
		const MarkdownArticleHitTestResult &result,
		TextSelectType selectionType) const;
	[[nodiscard]] TextSelection adjustSelection(
		int segmentIndex,
		TextSelection selection,
		TextSelectType selectionType) const;
	[[nodiscard]] bool selectionContains(
		MarkdownArticleSelection selection,
		const MarkdownArticleSelectionEndpoints *endpoints,
		const MarkdownArticleHitTestResult &result) const;
	[[nodiscard]] TextForMimeData textForContext(
		const MarkdownArticleHitTestResult &result) const;
	[[nodiscard]] TextForMimeData textForSelection(
		MarkdownArticleSelection selection,
		const MarkdownArticleSelectionEndpoints *endpoints,
		const PreparedEditSelection *structuralSelection = nullptr) const;
	[[nodiscard]] std::vector<RichPage::Block> richPageSliceForSelection(
		MarkdownArticleSelection selection) const;
	[[nodiscard]] bool highlightProcessDone(
		Spellchecker::HighlightProcessId processId);
	void invalidatePaletteCache();
	void invalidateRasterCache();
	[[nodiscard]] bool hasHeavyPart() const;
	void unloadHeavyPart();
	void hideSpoilers();
	[[nodiscard]] MediaBlockHost *mediaBlockHost() const;
	void setPlaceholderLoading(PreparedPlaceholderBlockId id);
	void clearPlaceholderLoading(PreparedPlaceholderBlockId id);
	void clearAllPlaceholderLoading();
	void addPlaceholderRipple(PreparedPlaceholderBlockId id, QPoint point);
	void stopPlaceholderRipple(PreparedPlaceholderBlockId id);

    void clearBeforeDestroy();

private:
	class Impl;

	std::unique_ptr<Impl> _impl;

};

} // namespace Iv::Markdown
