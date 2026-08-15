/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/markdown/iv_markdown_article.h"
#include "iv/markdown/iv_markdown_view.h"

#include "base/timer.h"
#include "base/unique_qptr.h"
#include "rpl/lifetime.h"
#include "ui/click_handler.h"
#include "ui/rp_widget.h"
#include "ui/style/style_core_types.h"
#include "ui/ui_utility.h"
#include "ui/widgets/tooltip.h"

#include <functional>
#include <memory>
#include <optional>
#include <vector>
#include <QtCore/QVariant>

namespace Ui {
class ChatStyle;
class ChatTheme;
class PopupMenu;
namespace Text {
struct QuotePaintCache;
} // namespace Text
} // namespace Ui

class QTouchEvent;
class QWheelEvent;

namespace Iv::Markdown {

class MarkdownDocumentWidget final
	: public Ui::RpWidget
	, public ClickHandlerHost
	, public MediaBlockHost
	, public Ui::AbstractTooltipShower {
public:
	explicit MarkdownDocumentWidget(QWidget *parent);
	~MarkdownDocumentWidget() override;

	void setLinkActivationCallback(
		std::function<void(const PreparedLink &, Qt::MouseButton)> callback);
	void setMediaActivationCallback(
		std::function<bool(const MediaActivation &, Qt::MouseButton)> callback);
	void setZoomStepCallback(std::function<void(int)> callback);
	void setClickHandlerContext(
		QVariant context,
		std::shared_ptr<QVariant> contextRef = nullptr);
	void setArticle(std::shared_ptr<MarkdownArticle> article);
	void articleContentChanged();
	void setSearchMatches(
		std::vector<MarkdownArticleSearchMatch> matches,
		int current);
	void setZoom(int value);
	void refreshPalette();
	void invalidateRasterCache();
	[[nodiscard]] int maxWidth() const;
	[[nodiscard]] int anchorTop(const QString &anchorId) const;
	[[nodiscard]] auto scrollAnchorForTop(int top) const
	-> std::optional<MarkdownArticleScrollAnchor>;
	[[nodiscard]] int scrollTopForAnchor(
		const MarkdownArticleScrollAnchor &anchor) const;
	[[nodiscard]] bool expandDetailsToAnchor(const QString &anchorId);
	[[nodiscard]] bool expandDetailsBlock(const QString &anchorId);
	[[nodiscard]] QRect segmentRect(int segmentIndex) const;
	[[nodiscard]] bool toggleDetails(const QString &anchorId);
	[[nodiscard]] int lastRelayoutMs() const;
	int resizeGetHeight(int newWidth) override;
	void requestRepaint(QRect articleRect) override;
	void requestRelayout(QRect articleRect) override;
	void setPlaceholderLoading(PreparedPlaceholderBlockId id);
	void clearPlaceholderLoading(PreparedPlaceholderBlockId id);
	void clearAllPlaceholderLoading();
	void addPlaceholderRipple(PreparedPlaceholderBlockId id, QPoint point);
	void stopPlaceholderRipple(PreparedPlaceholderBlockId id);

	QString tooltipText() const override;
	QPoint tooltipPos() const override;
	bool tooltipWindowActive() const override;

protected:
	void paintEvent(QPaintEvent *e) override;
	void visibleTopBottomUpdated(int visibleTop, int visibleBottom) override;
	void keyPressEvent(QKeyEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;
	void focusOutEvent(QFocusEvent *e) override;
	void focusInEvent(QFocusEvent *e) override;
	bool eventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void clickHandlerActiveChanged(const ClickHandlerPtr &, bool) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &, bool) override;

private:
	enum DragAction {
		NoDrag = 0x00,
		PrepareDrag = 0x01,
		Dragging = 0x02,
		Selecting = 0x04,
	};

	[[nodiscard]] ClickHandlerPtr linkAt(QPoint point) const;
	[[nodiscard]] MarkdownArticleHitTestResult hitTest(
		QPoint point,
		Ui::Text::StateRequest::Flags flags) const;
	[[nodiscard]] MarkdownArticleSelection selectionForCopy() const;
	[[nodiscard]] MarkdownArticleSelectionEndpoints selectionEndpointsForCopy() const;
	[[nodiscard]] bool selectionContains(
		MarkdownArticleSelection selection,
		const MarkdownArticleHitTestResult &result) const;
	[[nodiscard]] int selectionOffsetFromHit(
		const MarkdownArticleHitTestResult &result) const;
	[[nodiscard]] MarkdownArticleSelection selectionFromHit(
		const MarkdownArticleHitTestResult &result) const;
	[[nodiscard]] TextForMimeData getSelectedText() const;
	[[nodiscard]] QVariant clickHandlerContext() const;
	[[nodiscard]] QVariant viewerToastClickHandlerContext() const;
	void showToast(const QString &text) const;
	void copySelectedText();
	void copyCodeBlock(const MarkdownArticleHitTestResult &state);

	void syncArticleVisibleTopBottom();
	void relayoutCurrentWidth(bool clearSelection);
	void forceRelayoutCurrentWidth();
	void retryMissingMediaBlocks();
	void updateHover(const MarkdownArticleHitTestResult &state);
	void updateHoverAtCursor();
	void resetSelection();
	void clearSelection();
	void resetTextPaintCaches();
	[[nodiscard]] QRect articleRectToWidget(QRect articleRect) const;
	[[nodiscard]] Ui::Text::QuotePaintCache *ensurePrePaintCache();
	[[nodiscard]] Ui::Text::QuotePaintCache *ensureBlockquotePaintCache();
	[[nodiscard]] MarkdownArticlePaintContext textPaintContext(QRect clip);
	void touchEvent(QTouchEvent *e);
	void stopPressedPlaceholderRipple();
	void dragActionStart(QPoint point, Qt::MouseButton button);
	MarkdownArticleHitTestResult dragActionUpdate(QPoint point);
	MarkdownArticleHitTestResult dragActionFinish(
		QPoint point,
		Qt::MouseButton button);
	void applyCursor(style::cursor cursor);
	[[nodiscard]] double zoomScale() const;
	[[nodiscard]] int articleLayoutWidth(int widgetWidth) const;

	std::shared_ptr<MarkdownArticle> _article;
	std::shared_ptr<MarkdownArticle> _retainedArticle;
	std::unique_ptr<Ui::ChatTheme> _theme;
	std::unique_ptr<Ui::ChatStyle> _style;
	std::vector<Ui::Text::SpecialColor> _highlightColors;
	std::unique_ptr<Ui::Text::QuotePaintCache> _prePaintCache;
	std::unique_ptr<Ui::Text::QuotePaintCache> _blockquotePaintCache;
	rpl::lifetime _highlightReadyLifetime;
	std::function<void(const PreparedLink &, Qt::MouseButton)> _activateLink;
	std::function<bool(const MediaActivation &, Qt::MouseButton)> _activateMedia;
	std::function<void(int)> _zoomStepCallback;
	QVariant _clickHandlerContext;
	std::shared_ptr<QVariant> _clickHandlerContextRef;
	MarkdownArticleSelection _selection;
	MarkdownArticleSelection _savedSelection;
	MarkdownArticleSelectionEndpoints _selectionEndpoints;
	MarkdownArticleSelectionEndpoints _savedSelectionEndpoints;
	TextSelectType _selectionType = TextSelectType::Letters;
	style::cursor _cursor = style::cur_default;
	DragAction _dragAction = NoDrag;
	QPoint _dragStartPosition;
	int _dragSegment = -1;
	int _dragSymbol = 0;
	TextSelection _dragExpandedSelection;
	QPoint _tripleClickPoint;
	base::Timer _tripleClickTimer;
	std::optional<PreparedLink> _selectionClickPreparedLink;
	PreparedPlaceholderBlockId _pressedPlaceholderId;
	bool _dragStartHadSelection = false;
	int _lastRelayoutMs = 0;
	int _zoom = 100;
	int _wheelZoomAccumulated = 0;
	Ui::VisibleRange _visibleRange;
	Ui::ScrollDirectionLock _scrollDirectionLock;
	base::unique_qptr<Ui::PopupMenu> _contextMenu;
	bool _activeHorizontalScrollDrag = false;
	bool _articlePainted = false;
	bool _mediaCreationRetried = false;
	bool _activeTouchHorizontalScroll = false;
	std::optional<QPoint> _pendingTouchHorizontalScrollPoint;

};

} // namespace Iv::Markdown
