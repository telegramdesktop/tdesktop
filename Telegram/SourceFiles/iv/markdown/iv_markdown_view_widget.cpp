/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_view_widget.h"

#include "base/qt/qt_common_adapters.h"
#include "base/weak_ptr.h"
#include "core/click_handler_types.h"
#include "core/credits_amount.h"
#include "core/file_utilities.h"
#include "iv/editor/iv_editor_clipboard.h"
#include "iv/markdown/iv_markdown_article_text.h"
#include "iv/markdown/iv_markdown_prepare_native_richtext.h"
#include "lang/lang_keys.h"
#include "spellcheck/spellcheck_highlight_syntax.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/layers/show.h"
#include "ui/text/text_extended_data.h"
#include "ui/toast/toast.h"
#include "ui/ui_utility.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/tooltip.h"
#include "ui/color_contrast.h"
#include "ui/integration.h"

#include "styles/palette.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_iv.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QMimeData>
#include <QtCore/QPointer>
#include <QtGui/QClipboard>
#include <QtGui/QContextMenuEvent>
#include <QtGui/QCursor>
#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>
#include <QtGui/QKeySequence>
#include <QtGui/QMouseEvent>
#include <QtGui/QTouchEvent>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QApplication>

#include <algorithm>
#include <cmath>
#include <utility>

namespace Iv::Markdown {
namespace {

void EnsureBlockquotePaintCache(
		std::unique_ptr<Ui::Text::QuotePaintCache> &cache,
		const style::color &color) {
	if (cache) {
		return;
	}
	cache = std::make_unique<Ui::Text::QuotePaintCache>();
	cache->bg = color->c;
	cache->bg.setAlpha(Ui::kDefaultBgOpacity * 255);
	cache->outlines[0] = color->c;
	cache->outlines[0].setAlpha(Ui::kDefaultOutline1Opacity * 255);
	cache->outlines[1] = cache->outlines[2] = QColor(0, 0, 0, 0);
	cache->header = color->c;
	cache->header.setAlpha(Ui::kDefaultOutline2Opacity * 255);
	cache->icon = color->c;
	cache->icon.setAlpha(Ui::kDefaultOutline3Opacity * 255);
}

[[nodiscard]] bool UseDarkPrePaintBackground() {
	const auto withBg = [](const QColor &color) {
		return Ui::CountContrast(st::windowBg->c, color);
	};
	return withBg({ 0, 0, 0 }) < withBg({ 255, 255, 255 });
}

void EnsurePrePaintCache(
		std::unique_ptr<Ui::Text::QuotePaintCache> &cache,
		const style::color &color) {
	if (cache) {
		return;
	}
	cache = std::make_unique<Ui::Text::QuotePaintCache>();
	if (UseDarkPrePaintBackground()) {
		cache->bg = QColor(0, 0, 0, 192);
	} else {
		cache->bg = color->c;
		cache->bg.setAlpha(Ui::kDefaultBgOpacity * 255);
	}
	cache->outlines[0] = color->c;
	cache->outlines[0].setAlpha(Ui::kDefaultOutline1Opacity * 255);
	cache->outlines[1] = cache->outlines[2] = QColor(0, 0, 0, 0);
	cache->header = color->c;
	cache->header.setAlpha(Ui::kDefaultOutline2Opacity * 255);
	cache->icon = color->c;
	cache->icon.setAlpha(Ui::kDefaultOutline3Opacity * 255);
}

[[nodiscard]] int CompareSelectionPositions(
		MarkdownArticleSelectionPosition a,
		MarkdownArticleSelectionPosition b) {
	if (a.segment != b.segment) {
		return (a.segment < b.segment) ? -1 : 1;
	}
	if (a.offset != b.offset) {
		return (a.offset < b.offset) ? -1 : 1;
	}
	return 0;
}

[[nodiscard]] MarkdownArticleSelection NormalizeSelection(
		MarkdownArticleSelection selection) {
	if (selection.empty()) {
		return {};
	}
	if (CompareSelectionPositions(selection.from, selection.to) > 0) {
		std::swap(selection.from, selection.to);
	}
	return selection;
}

[[nodiscard]] MarkdownArticleSelectionEndpoint MakeSelectionEndpoint(
		const MarkdownArticleHitTestResult &result) {
	return {
		.segment = result.segmentIndex,
		.direct = result.direct,
	};
}

[[nodiscard]] std::vector<Ui::Text::SpecialColor> HighlightColors(
		not_null<const Ui::ChatStyle*> style) {
	auto result = Ui::SyntaxHighlightColors(style);

	const auto &fg = style->lightButtonFg();
	const auto &bg = style->lightButtonBgOver();
	result.push_back({ &fg->p, &fg->p, &bg->b, &bg->b });

	Ensures(result.size() == kNativeIvLinkSpecialColorIndex);
	return result;
}

[[nodiscard]] std::unique_ptr<Ui::ChatTheme> CreateStandaloneChatTheme() {
	const auto palette = style::main_palette::get();
	return std::make_unique<Ui::ChatTheme>(Ui::ChatThemeDescriptor{
		.preparePalette = [=](style::palette &copy) {
			copy = *palette;
		},
		.backgroundData = {
			.colors = { palette->windowBg()->c },
		},
	});
}

[[nodiscard]] QPoint LocalPosition(QWheelEvent *e) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	return e->position().toPoint();
#else // Qt >= 6.0
	return e->pos();
#endif // Qt >= 6.0
}

[[nodiscard]] QPoint ArticlePointFromWidget(QPoint point, double scale) {
	if (scale != 1.) {
		point = QPoint(
			int(std::floor(point.x() / scale)),
			int(std::floor(point.y() / scale)));
	}
	return point;
}

} // namespace

MarkdownDocumentWidget::MarkdownDocumentWidget(QWidget *parent)
: Ui::RpWidget(parent)
, _theme(CreateStandaloneChatTheme())
, _style(std::make_unique<Ui::ChatStyle>(style::main_palette::get())) {
	_style->apply(_theme.get());
	_highlightColors = HighlightColors(_style.get());

	setMouseTracking(true);
	setAttribute(Qt::WA_AcceptTouchEvents);
	setFocusPolicy(Qt::StrongFocus);

	Spellchecker::HighlightReady(
	) | rpl::on_next([=](Spellchecker::HighlightProcessId processId) {
		if (_article && _article->highlightProcessDone(processId)) {
			update();
		}
	}, _highlightReadyLifetime);
}

MarkdownDocumentWidget::~MarkdownDocumentWidget() {
	if (_article && (_article->mediaBlockHost() == this)) {
		_article->setTextRepaintCallbacks(nullptr, nullptr);
		_article->setMediaBlockHost(nullptr);
	}
}

void MarkdownDocumentWidget::setLinkActivationCallback(
		std::function<void(const PreparedLink &, Qt::MouseButton)> callback) {
	_activateLink = std::move(callback);
}

void MarkdownDocumentWidget::setMediaActivationCallback(
		std::function<bool(const MediaActivation &, Qt::MouseButton)> callback) {
	_activateMedia = std::move(callback);
}

void MarkdownDocumentWidget::setZoomStepCallback(
		std::function<void(int)> callback) {
	_zoomStepCallback = std::move(callback);
}

void MarkdownDocumentWidget::setClickHandlerContext(
		QVariant context,
		std::shared_ptr<QVariant> contextRef) {
	_clickHandlerContext = std::move(context);
	_clickHandlerContextRef = std::move(contextRef);
}

void MarkdownDocumentWidget::setArticle(
		std::shared_ptr<MarkdownArticle> article) {
	ClickHandler::clearActive(this);
	applyCursor(style::cur_default);
	auto previous = std::move(_article);
	if (previous && (previous->mediaBlockHost() == this)) {
		previous->setTextRepaintCallbacks(nullptr, nullptr);
		previous->setMediaBlockHost(nullptr);
	}
	if (previous && article && _articlePainted) {
		_retainedArticle = std::move(previous);
	} else if (!article) {
		_retainedArticle = nullptr;
	}
	_article = std::move(article);
	_articlePainted = false;
	if (_article) {
		const auto weak = QPointer<MarkdownDocumentWidget>(this);
		_article->setTextRepaintCallbacks(
			[=] {
				if (weak) {
					weak->requestRepaint(QRect());
				}
			},
			[=](QRect articleRect) {
				if (weak) {
					weak->requestRepaint(articleRect);
				}
			});
		_article->setMediaBlockHost(this);
	}
	_lastRelayoutMs = 0;
	_mediaCreationRetried = false;
	resetTextPaintCaches();
	resetSelection();
	forceRelayoutCurrentWidth();
}

void MarkdownDocumentWidget::articleContentChanged() {
	ClickHandler::clearActive(this);
	applyCursor(style::cur_default);
	stopPressedPlaceholderRipple();
	clearSelection();
	_articlePainted = false;
	resetTextPaintCaches();
	_mediaCreationRetried = false;
	forceRelayoutCurrentWidth();
}

void MarkdownDocumentWidget::setSearchMatches(
		std::vector<MarkdownArticleSearchMatch> matches,
		int current) {
	if (_article) {
		_article->setSearchMatches(std::move(matches), current);
	}
	update();
}

void MarkdownDocumentWidget::setZoom(int value) {
	value = (value > 0) ? value : 100;
	if (_zoom == value) {
		return;
	}
	_zoom = value;
	clearSelection();
	forceRelayoutCurrentWidth();
}

void MarkdownDocumentWidget::refreshPalette() {
	ClickHandler::clearActive(this);
	applyCursor(style::cur_default);
	_theme = CreateStandaloneChatTheme();
	_style->apply(_theme.get());
	_highlightColors = HighlightColors(_style.get());
	resetTextPaintCaches();
	if (_article) {
		_article->invalidatePaletteCache();
	}
	update();
}

void MarkdownDocumentWidget::invalidateRasterCache() {
	ClickHandler::clearActive(this);
	applyCursor(style::cur_default);
	if (_article) {
		_article->invalidateRasterCache();
	}
	relayoutCurrentWidth(false);
	update();
}

int MarkdownDocumentWidget::maxWidth() const {
	return _article
		? std::max(int(std::ceil(_article->maxWidth() * zoomScale())), 1)
		: 1;
}

int MarkdownDocumentWidget::anchorTop(const QString &anchorId) const {
	const auto top = _article ? _article->anchorTop(anchorId) : -1;
	if (top < 0) {
		return -1;
	}
	return int(std::floor(top * zoomScale()));
}

auto MarkdownDocumentWidget::scrollAnchorForTop(int top) const
-> std::optional<MarkdownArticleScrollAnchor> {
	if (!_article) {
		return std::nullopt;
	}
	return _article->scrollAnchorForTop(int(std::floor(top / zoomScale())));
}

int MarkdownDocumentWidget::scrollTopForAnchor(
		const MarkdownArticleScrollAnchor &anchor) const {
	const auto top = _article ? _article->scrollTopForAnchor(anchor) : -1;
	if (top < 0) {
		return -1;
	}
	return int(std::floor(top * zoomScale()));
}

bool MarkdownDocumentWidget::expandDetailsToAnchor(const QString &anchorId) {
	if (!_article) {
		return false;
	}
	const auto result = _article->expandDetailsToAnchor(anchorId);
	if (!result.found) {
		return false;
	}
	if (result.changed) {
		clearSelection();
		forceRelayoutCurrentWidth();
		updateHoverAtCursor();
	}
	return true;
}

bool MarkdownDocumentWidget::expandDetailsBlock(const QString &anchorId) {
	if (!_article) {
		return false;
	}
	const auto result = _article->expandDetailsBlock(anchorId);
	if (!result.found || !result.changed) {
		return false;
	}
	clearSelection();
	forceRelayoutCurrentWidth();
	updateHoverAtCursor();
	return true;
}

QRect MarkdownDocumentWidget::segmentRect(int segmentIndex) const {
	const auto rect = _article
		? _article->segmentRect(segmentIndex)
		: QRect();
	return rect.isEmpty() ? QRect() : articleRectToWidget(rect);
}

bool MarkdownDocumentWidget::toggleDetails(const QString &anchorId) {
	if (!_article || !_article->toggleDetails(anchorId)) {
		return false;
	}
	clearSelection();
	forceRelayoutCurrentWidth();
	updateHoverAtCursor();
	return true;
}

int MarkdownDocumentWidget::lastRelayoutMs() const {
	return _lastRelayoutMs;
}

int MarkdownDocumentWidget::resizeGetHeight(int newWidth) {
	ClickHandler::clearActive(this);
	applyCursor(style::cur_default);
	clearSelection();
	if (!_article) {
		return 1;
	}
	const auto scale = zoomScale();
	const auto layoutWidth = articleLayoutWidth(newWidth);
	_article->setMediaPixelScale(scale);
	auto timer = QElapsedTimer();
	timer.start();
	const auto layoutHeight = _article->resizeGetHeight(layoutWidth);
	syncArticleVisibleTopBottom();
	_lastRelayoutMs = int(timer.elapsed());
	retryMissingMediaBlocks();
	return std::max(int(std::ceil(layoutHeight * scale)), 1);
}

void MarkdownDocumentWidget::requestRepaint(QRect articleRect) {
	crl::on_main(this, [=] {
		if (!_article) {
			return;
		}
		if (articleRect.isEmpty()) {
			update();
		} else {
			update(articleRectToWidget(articleRect));
		}
	});
}

void MarkdownDocumentWidget::requestRelayout(QRect articleRect) {
	crl::on_main(this, [=] {
		if (!_article) {
			return;
		}
		_article->invalidateLayout();
		const auto previousHeight = height();
		const auto scale = zoomScale();
		const auto layoutWidth = articleLayoutWidth(width());
		_article->setMediaPixelScale(scale);
		auto timer = QElapsedTimer();
		timer.start();
		const auto articleHeight = _article->resizeGetHeight(layoutWidth);
		syncArticleVisibleTopBottom();
		_lastRelayoutMs = int(timer.elapsed());
		const auto newHeight = std::max(int(std::ceil(articleHeight * scale)), 1);
		if (previousHeight != newHeight) {
			resize(width(), newHeight);
			update();
		} else if (articleRect.isEmpty()) {
			update();
		} else {
			update(articleRectToWidget(articleRect));
		}
		updateHoverAtCursor();
		retryMissingMediaBlocks();
	});
}

void MarkdownDocumentWidget::retryMissingMediaBlocks() {
	if (_mediaCreationRetried
		|| !_article
		|| !_article->hasMissingMediaBlocks()) {
		return;
	}
	_mediaCreationRetried = true;
	requestRelayout(QRect());
}

void MarkdownDocumentWidget::setPlaceholderLoading(
		PreparedPlaceholderBlockId id) {
	if (_article) {
		_article->setPlaceholderLoading(id);
	}
}

void MarkdownDocumentWidget::clearPlaceholderLoading(
		PreparedPlaceholderBlockId id) {
	if (_article) {
		_article->clearPlaceholderLoading(id);
	}
}

void MarkdownDocumentWidget::clearAllPlaceholderLoading() {
	if (_article) {
		_article->clearAllPlaceholderLoading();
	}
}

void MarkdownDocumentWidget::addPlaceholderRipple(
		PreparedPlaceholderBlockId id,
		QPoint point) {
	if (_article) {
		_article->addPlaceholderRipple(id, point);
	}
}

void MarkdownDocumentWidget::stopPlaceholderRipple(
		PreparedPlaceholderBlockId id) {
	if (_article) {
		_article->stopPlaceholderRipple(id);
	}
}

void MarkdownDocumentWidget::paintEvent(QPaintEvent *e) {
	if (!_article) {
		return;
	}
	auto p = Painter(this);
	p.setTextPalette(st::inTextPalette);
	const auto scale = zoomScale();
	const auto clip = (scale == 1.)
		? e->rect()
		: QRect(
			int(std::floor(e->rect().x() / scale)),
			int(std::floor(e->rect().y() / scale)),
			int(std::ceil(e->rect().width() / scale)) + 1,
			int(std::ceil(e->rect().height() / scale)) + 1);
	auto context = textPaintContext(clip);
	if (scale == 1.) {
		_article->paint(p, context);
		_articlePainted = true;
		_retainedArticle = nullptr;
		return;
	}
	p.save();
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	p.scale(scale, scale);
	_article->paint(p, context);
	p.restore();
	_articlePainted = true;
	_retainedArticle = nullptr;
}

void MarkdownDocumentWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	_visibleRange = Ui::VisibleRange{
		.top = visibleTop,
		.bottom = visibleBottom,
	};
	syncArticleVisibleTopBottom();
}

void MarkdownDocumentWidget::keyPressEvent(QKeyEvent *e) {
	if (e == QKeySequence::Copy && !selectionForCopy().empty()) {
		copySelectedText();
		return;
	}
	Ui::RpWidget::keyPressEvent(e);
}

void MarkdownDocumentWidget::contextMenuEvent(QContextMenuEvent *e) {
	const auto globalPoint = (e->reason() == QContextMenuEvent::Mouse)
		? e->globalPos()
		: QCursor::pos();
	const auto localPoint = (e->reason() == QContextMenuEvent::Mouse)
		? e->pos()
		: mapFromGlobal(globalPoint);
	const auto state = hitTest(
		localPoint,
		Ui::Text::StateRequest::Flag::LookupLink
			| Ui::Text::StateRequest::Flag::LookupSymbol);
	const auto selection = selectionForCopy();
	const auto uponSelection = !selection.empty()
		&& ((e->reason() != QContextMenuEvent::Mouse)
			|| selectionContains(selection, state));
	const auto link = state.preparedLink;

	_contextMenu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	if (uponSelection) {
		_contextMenu->addAction(
			Ui::Integration::Instance().phraseContextCopySelected(),
			[=] { copySelectedText(); },
			&st::menuIconCopy);
	}

	if (link) {
		const auto handler = CreatePreparedLinkHandler(*link);
		const auto copyText = handler ? handler->copyToClipboardText() : QString();
		const auto copyLabel = handler
			? handler->copyToClipboardContextItemText()
			: QString();
		if (!copyText.isEmpty() && !copyLabel.isEmpty()) {
			_contextMenu->addAction(
				copyLabel,
				[text = copyText] {
					QGuiApplication::clipboard()->setText(text);
				},
				&st::menuIconCopy);
		}
	}

	if (_contextMenu->empty()) {
		_contextMenu = nullptr;
		return;
	}
	_contextMenu->popup(globalPoint);
	e->accept();
}

void MarkdownDocumentWidget::wheelEvent(QWheelEvent *e) {
	if (!_article) {
		(void)_scrollDirectionLock.update(e->phase(), {});
		e->ignore();
		return;
	}
	if (e->modifiers().testFlag(Qt::ControlModifier)) {
		(void)_scrollDirectionLock.update(e->phase(), {});
		const auto angle = e->angleDelta().y();
		const auto wheel = angle ? angle : e->pixelDelta().y();
		if (wheel) {
			constexpr auto step = int(QWheelEvent::DefaultDeltasPerStep);
			_wheelZoomAccumulated += wheel;
			while (std::abs(_wheelZoomAccumulated) >= step) {
				if (_wheelZoomAccumulated < 0) {
					_wheelZoomAccumulated += step;
					if (_zoomStepCallback) {
						_zoomStepCallback(-1);
					}
				} else {
					_wheelZoomAccumulated -= step;
					if (_zoomStepCallback) {
						_zoomStepCallback(1);
					}
				}
			}
		}
		e->accept();
		return;
	}
	const auto delta = Ui::ScrollDeltaF(e);
	const auto locked = _scrollDirectionLock.update(e->phase(), delta);
	const auto horizontal = locked
		? (*locked == Qt::Horizontal)
		: (std::abs(delta.x()) > std::abs(delta.y()));
	const auto local = ArticlePointFromWidget(LocalPosition(e), zoomScale());
	if (!_article->horizontalScrollHit(local).scrollable) {
		e->ignore();
		return;
	}
	if (horizontal) {
		(void)_article->consumeHorizontalScroll(
			local,
			int(std::round(delta.x())));
		e->accept();
	} else {
		e->ignore();
	}
}

void MarkdownDocumentWidget::touchEvent(QTouchEvent *e) {
	if (e->type() == QEvent::TouchCancel) {
		_pendingTouchHorizontalScrollPoint = std::nullopt;
		if (!_activeTouchHorizontalScroll) {
			return;
		}
		_activeTouchHorizontalScroll = false;
		if (_article) {
			_article->endHorizontalScroll();
		}
		e->accept();
		return;
	}
	if (!_article || e->touchPoints().isEmpty()) {
		return;
	}
	const auto point = mapFromGlobal(
		e->touchPoints().cbegin()->screenPos().toPoint());
	const auto local = ArticlePointFromWidget(point, zoomScale());
	switch (e->type()) {
	case QEvent::TouchBegin: {
		_pendingTouchHorizontalScrollPoint = std::nullopt;
		const auto hit = _article->horizontalScrollHit(local);
		_activeTouchHorizontalScroll = hit.overScrollbar
			&& _article->beginHorizontalScroll(local, false);
		if (!_activeTouchHorizontalScroll && hit.overViewport) {
			_pendingTouchHorizontalScrollPoint = local;
		}
		if (_activeTouchHorizontalScroll) {
			e->accept();
		}
	} break;
	case QEvent::TouchUpdate:
		if (_activeTouchHorizontalScroll) {
			(void)_article->updateHorizontalScroll(local);
			e->accept();
		} else if (_pendingTouchHorizontalScrollPoint) {
			const auto delta = local - *_pendingTouchHorizontalScrollPoint;
			if (delta.manhattanLength() < QApplication::startDragDistance()) {
				break;
			}
			const auto horizontal = (std::abs(delta.x()) > std::abs(delta.y()));
			if (!horizontal) {
				_pendingTouchHorizontalScrollPoint = std::nullopt;
				break;
			}
			_activeTouchHorizontalScroll = _article->beginHorizontalScroll(
				*_pendingTouchHorizontalScrollPoint,
				true);
			_pendingTouchHorizontalScrollPoint = std::nullopt;
			if (_activeTouchHorizontalScroll) {
				(void)_article->updateHorizontalScroll(local);
				e->accept();
			}
		}
		break;
	case QEvent::TouchEnd:
		_pendingTouchHorizontalScrollPoint = std::nullopt;
		if (_activeTouchHorizontalScroll) {
			_activeTouchHorizontalScroll = false;
			_article->endHorizontalScroll();
			e->accept();
		}
		break;
	default:
		break;
	}
}

void MarkdownDocumentWidget::mouseMoveEvent(QMouseEvent *e) {
	if (_activeHorizontalScrollDrag) {
		if (_article) {
			(void)_article->updateHorizontalScroll(
				ArticlePointFromWidget(e->pos(), zoomScale()));
		}
		e->accept();
		return;
	}
	dragActionUpdate(e->pos());
}

void MarkdownDocumentWidget::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		if (_article && _article->beginHorizontalScroll(
				ArticlePointFromWidget(e->pos(), zoomScale()),
				false)) {
			_activeHorizontalScrollDrag = true;
			e->accept();
			return;
		}
		dragActionStart(e->pos(), e->button());
		return;
	}
	updateHover(hitTest(
		e->pos(),
		Ui::Text::StateRequest::Flag::LookupLink
			| Ui::Text::StateRequest::Flag::LookupSymbol));
	if (e->button() == Qt::MiddleButton) {
		ClickHandler::pressed();
	}
}

void MarkdownDocumentWidget::mouseReleaseEvent(QMouseEvent *e) {
	const auto weak = base::make_weak(this);
	if (_activeHorizontalScrollDrag && e->button() == Qt::LeftButton) {
		if (_article) {
			(void)_article->updateHorizontalScroll(
				ArticlePointFromWidget(e->pos(), zoomScale()));
		}
		_activeHorizontalScrollDrag = false;
		if (_article) {
			_article->endHorizontalScroll();
		}
		if (weak && rect().contains(e->pos())) {
			updateHover(hitTest(
				e->pos(),
				Ui::Text::StateRequest::Flag::LookupLink
					| Ui::Text::StateRequest::Flag::LookupSymbol));
		} else if (weak) {
			ClickHandler::clearActive(this);
			applyCursor(style::cur_default);
		}
		e->accept();
		return;
	}
	dragActionFinish(e->pos(), e->button());
	if (weak && !rect().contains(e->pos())) {
		ClickHandler::clearActive(this);
		applyCursor(style::cur_default);
	}
}

void MarkdownDocumentWidget::mouseDoubleClickEvent(QMouseEvent *e) {
	if (_article && _article->horizontalScrollHit(
			ArticlePointFromWidget(e->pos(), zoomScale())).overScrollbar) {
		return;
	}
	dragActionStart(e->pos(), e->button());
	if (_dragAction != Selecting || _selectionType != TextSelectType::Letters) {
		return;
	}
	const auto state = hitTest(
		e->pos(),
		Ui::Text::StateRequest::Flag::LookupLink
			| Ui::Text::StateRequest::Flag::LookupSymbol);
	if (!_article
		|| !_article->segmentIsText(state.segmentIndex)
		|| !state.direct
		|| !state.state.uponSymbol) {
		return;
	}
	_dragSegment = state.segmentIndex;
	_dragSymbol = selectionOffsetFromHit(state);
	_selectionType = TextSelectType::Words;
	_selection = selectionFromHit(state);
	_savedSelection = {};
	_selectionEndpoints = {
		.from = MakeSelectionEndpoint(state),
		.to = MakeSelectionEndpoint(state),
	};
	_savedSelectionEndpoints = {};
	if (_selection.from.segment == _dragSegment
		&& _selection.to.segment == _dragSegment) {
		_dragExpandedSelection = TextSelection(
			uint16(_selection.from.offset),
			uint16(_selection.to.offset));
	}
	_tripleClickPoint = e->pos();
	_tripleClickTimer.callOnce(QApplication::doubleClickInterval());
	setFocus();
	updateHover(state);
	update();
}

void MarkdownDocumentWidget::focusOutEvent(QFocusEvent *e) {
	stopPressedPlaceholderRipple();
	if (!_selection.empty()) {
		_savedSelection = _selection;
		_savedSelectionEndpoints = _selectionEndpoints;
		_selection = {};
		_selectionEndpoints = {};
		update();
	}
	ClickHandler::clearActive(this);
	applyCursor(style::cur_default);
	Ui::RpWidget::focusOutEvent(e);
}

void MarkdownDocumentWidget::focusInEvent(QFocusEvent *e) {
	if (!_savedSelection.empty()) {
		_selection = _savedSelection;
		_selectionEndpoints = _savedSelectionEndpoints;
		_savedSelection = {};
		_savedSelectionEndpoints = {};
		update();
	}
	Ui::RpWidget::focusInEvent(e);
}

bool MarkdownDocumentWidget::eventHook(QEvent *e) {
	if (e->type() == QEvent::TouchBegin
		|| e->type() == QEvent::TouchUpdate
		|| e->type() == QEvent::TouchEnd
		|| e->type() == QEvent::TouchCancel) {
		auto *ev = static_cast<QTouchEvent*>(e);
		if (ev->device()->type() == base::TouchDevice::TouchScreen) {
			const auto active = _activeTouchHorizontalScroll;
			touchEvent(ev);
			if (active || _activeTouchHorizontalScroll) {
				return true;
			}
		}
	}
	return Ui::RpWidget::eventHook(e);
}

void MarkdownDocumentWidget::leaveEventHook(QEvent *e) {
	ClickHandler::clearActive(this);
	Ui::Tooltip::Hide();
	applyCursor((_dragAction == Selecting)
		? style::cur_text
		: style::cur_default);
	Ui::RpWidget::leaveEventHook(e);
}

void MarkdownDocumentWidget::clickHandlerActiveChanged(
		const ClickHandlerPtr &,
		bool) {
	update();
}

void MarkdownDocumentWidget::clickHandlerPressedChanged(
		const ClickHandlerPtr &,
		bool) {
	update();
}

QString MarkdownDocumentWidget::tooltipText() const {
	if (const auto lnk = ClickHandler::getActive()) {
		return lnk->tooltip();
	}
	return QString();
}

QPoint MarkdownDocumentWidget::tooltipPos() const {
	return QCursor::pos();
}

bool MarkdownDocumentWidget::tooltipWindowActive() const {
	return Ui::AppInFocus() && Ui::InFocusChain(window());
}

ClickHandlerPtr MarkdownDocumentWidget::linkAt(QPoint point) const {
	return hitTest(
		point,
		Ui::Text::StateRequest::Flag::LookupLink
			| Ui::Text::StateRequest::Flag::LookupSymbol).state.link;
}

MarkdownArticleHitTestResult MarkdownDocumentWidget::hitTest(
		QPoint point,
		Ui::Text::StateRequest::Flags flags) const {
	if (!_article) {
		return {};
	}
	return _article->hitTest(
		ArticlePointFromWidget(point, zoomScale()),
		flags);
}

MarkdownArticleSelection MarkdownDocumentWidget::selectionForCopy() const {
	return !_selection.empty()
		? _selection
		: _contextMenu
		? _savedSelection
		: MarkdownArticleSelection();
}

MarkdownArticleSelectionEndpoints MarkdownDocumentWidget::selectionEndpointsForCopy() const {
	return !_selection.empty()
		? _selectionEndpoints
		: _contextMenu
		? _savedSelectionEndpoints
		: MarkdownArticleSelectionEndpoints();
}

bool MarkdownDocumentWidget::selectionContains(
		MarkdownArticleSelection selection,
		const MarkdownArticleHitTestResult &result) const {
	const auto endpoints = selectionEndpointsForCopy();
	return _article
		? _article->selectionContains(
			selection,
			&endpoints,
			result)
		: false;
}

int MarkdownDocumentWidget::selectionOffsetFromHit(
		const MarkdownArticleHitTestResult &result) const {
	return _article
		? _article->selectionOffsetFromHit(result, _selectionType)
		: 0;
}

MarkdownArticleSelection MarkdownDocumentWidget::selectionFromHit(
		const MarkdownArticleHitTestResult &result) const {
	if (!_article || _dragSegment < 0 || !result.valid()) {
		return {};
	}
	auto first = _dragSymbol;
	auto second = selectionOffsetFromHit(result);
	if (_selectionType != TextSelectType::Letters
		&& !_dragExpandedSelection.empty()
		&& result.segmentIndex != _dragSegment) {
		const auto targetBeforeAnchor = CompareSelectionPositions(
			MarkdownArticleSelectionPosition{ result.segmentIndex, second },
			MarkdownArticleSelectionPosition{ _dragSegment, _dragSymbol }) < 0;
		first = targetBeforeAnchor
			? _dragExpandedSelection.to
			: _dragExpandedSelection.from;
		if (_article->segmentIsText(result.segmentIndex)) {
			const auto expanded = _article->adjustSelection(
				result.segmentIndex,
				TextSelection(uint16(second), uint16(second)),
				_selectionType);
			second = targetBeforeAnchor ? expanded.from : expanded.to;
		}
	}
	if (result.segmentIndex == _dragSegment
		&& _article->segmentIsText(_dragSegment)) {
		const auto adjusted = _article->adjustSelection(
			_dragSegment,
			TextSelection(
				uint16(std::min(first, second)),
				uint16(std::max(first, second))),
			_selectionType);
		return {
			{ _dragSegment, adjusted.from },
			{ _dragSegment, adjusted.to },
		};
	}
	return NormalizeSelection({
		{ _dragSegment, first },
		{ result.segmentIndex, second },
	});
}

TextForMimeData MarkdownDocumentWidget::getSelectedText() const {
	const auto endpoints = selectionEndpointsForCopy();
	return _article
		? _article->textForSelection(
			selectionForCopy(),
			&endpoints)
		: TextForMimeData();
}

QVariant MarkdownDocumentWidget::clickHandlerContext() const {
	return _clickHandlerContextRef
		? *_clickHandlerContextRef
		: _clickHandlerContext;
}

QVariant MarkdownDocumentWidget::viewerToastClickHandlerContext() const {
	const auto context = clickHandlerContext().value<ClickHandlerContext>();
	if (!context.show) {
		return clickHandlerContext();
	}
	auto sanitized = context;
	sanitized.sessionWindow = base::weak_ptr<Window::SessionController>();
	return QVariant::fromValue(sanitized);
}

void MarkdownDocumentWidget::showToast(const QString &text) const {
	const auto context = clickHandlerContext().value<ClickHandlerContext>();
	if (context.show) {
		context.show->showToast({
			.text = { text },
			.iconLottie = u"toast/copy"_q,
			.iconLottieSize = st::toastLottieIconSize,
		});
	}
}

void MarkdownDocumentWidget::copySelectedText() {
	const auto text = getSelectedText();
	if (text.empty()) {
		return;
	}
	auto blocks = _article
		? _article->richPageSliceForSelection(selectionForCopy())
		: std::vector<RichPage::Block>();
	if (blocks.empty()) {
		TextUtilities::SetClipboardText(text);
	} else {
		auto data = Editor::ClipboardBlockData();
		data.blocks = std::move(blocks);
		auto mimeData = Editor::MimeDataFromClipboardData(
			Editor::ClipboardData(std::move(data)));
		if (const auto textMimeData = TextUtilities::MimeDataFromText(text)) {
			for (const auto &format : textMimeData->formats()) {
				mimeData->setData(format, textMimeData->data(format));
			}
		}
		QGuiApplication::clipboard()->setMimeData(mimeData.release());
	}
	showToast(tr::lng_text_copied(tr::now));
}

void MarkdownDocumentWidget::copyCodeBlock(
		const MarkdownArticleHitTestResult &state) {
	if (!_article) {
		return;
	}
	auto text = _article->textForContext(state);
	if (text.empty()) {
		return;
	}
	if (!text.rich.text.endsWith('\n')) {
		text.rich.text.append('\n');
	}
	if (!text.expanded.endsWith('\n')) {
		text.expanded.append('\n');
	}
	if (Ui::Integration::Instance().copyPreOnClick(
			viewerToastClickHandlerContext())) {
		TextUtilities::SetClipboardText(std::move(text));
	}
}

void MarkdownDocumentWidget::syncArticleVisibleTopBottom() {
	if (!_article) {
		return;
	}
	const auto scale = zoomScale();
	_article->setVisibleTopBottom(
		int(std::floor(_visibleRange.top / scale)),
		int(std::ceil(_visibleRange.bottom / scale)));
}

void MarkdownDocumentWidget::relayoutCurrentWidth(bool clearSelection) {
	if (clearSelection) {
		this->clearSelection();
	}
	if (!_article) {
		_lastRelayoutMs = 0;
		return;
	}
	const auto scale = zoomScale();
	const auto layoutWidth = articleLayoutWidth(width());
	_article->setMediaPixelScale(scale);
	auto timer = QElapsedTimer();
	timer.start();
	const auto articleHeight = _article->resizeGetHeight(layoutWidth);
	syncArticleVisibleTopBottom();
	(void)articleHeight;
	_lastRelayoutMs = int(timer.elapsed());
}

void MarkdownDocumentWidget::forceRelayoutCurrentWidth() {
	resizeToWidth(width());
	update();
}

void MarkdownDocumentWidget::updateHover(
		const MarkdownArticleHitTestResult &state) {
	const auto changed = ClickHandler::setActive(state.state.link, this);
	if (changed) {
		Ui::Tooltip::Hide();
	}
	if (state.state.link && _dragAction == NoDrag) {
		Ui::Tooltip::Show(1000, this);
	}
	auto cursor = style::cur_default;
	if (_dragAction == NoDrag) {
		if (state.codeHeaderCopy
			|| state.state.link
			|| (state.preparedLink
				&& state.preparedLink->kind == PreparedLinkKind::ToggleDetails)
			|| state.mediaActivation.kind != MediaActivationKind::None) {
			cursor = style::cur_pointer;
		} else if (state.direct) {
			cursor = style::cur_text;
		}
	} else {
		if (_dragAction == Selecting) {
			const auto selection = selectionFromHit(state);
			const auto endpoints = MarkdownArticleSelectionEndpoints{
				.from = _selectionEndpoints.from.valid()
					? _selectionEndpoints.from
					: MarkdownArticleSelectionEndpoint{ _dragSegment, false },
				.to = MakeSelectionEndpoint(state),
			};
			const auto endpointsChanged
				= (_selectionEndpoints.from.segment != endpoints.from.segment)
				|| (_selectionEndpoints.from.direct != endpoints.from.direct)
				|| (_selectionEndpoints.to.segment != endpoints.to.segment)
				|| (_selectionEndpoints.to.direct != endpoints.to.direct);
			if (_selection != selection || endpointsChanged) {
				_selection = selection;
				_selectionEndpoints = endpoints;
				_savedSelection = {};
				_savedSelectionEndpoints = {};
				setFocus();
				update();
			} else {
				_selectionEndpoints = endpoints;
			}
			cursor = style::cur_text;
		} else if (ClickHandler::getPressed()) {
			cursor = style::cur_pointer;
		}
	}
	if (changed || cursor != _cursor) {
		applyCursor(cursor);
	}
}

void MarkdownDocumentWidget::updateHoverAtCursor() {
	const auto point = mapFromGlobal(QCursor::pos());
	if (rect().contains(point)) {
		updateHover(hitTest(
			point,
			Ui::Text::StateRequest::Flag::LookupLink
				| Ui::Text::StateRequest::Flag::LookupSymbol));
	} else {
		ClickHandler::clearActive(this);
		applyCursor(style::cur_default);
	}
}

void MarkdownDocumentWidget::resetSelection() {
	_selection = {};
	_savedSelection = {};
	_selectionEndpoints = {};
	_savedSelectionEndpoints = {};
	_selectionType = TextSelectType::Letters;
	_dragAction = NoDrag;
	_dragStartPosition = QPoint();
	_dragSegment = -1;
	_dragSymbol = 0;
	_dragExpandedSelection = {};
	_selectionClickPreparedLink = std::nullopt;
	_dragStartHadSelection = false;
	_tripleClickTimer.cancel();
}

void MarkdownDocumentWidget::clearSelection() {
	const auto hadSelection = !_selection.empty()
		|| !_savedSelection.empty()
		|| (_dragAction != NoDrag);
	resetSelection();
	if (hadSelection) {
		update();
	}
}

void MarkdownDocumentWidget::resetTextPaintCaches() {
	_prePaintCache = nullptr;
	_blockquotePaintCache = nullptr;
}

QRect MarkdownDocumentWidget::articleRectToWidget(QRect articleRect) const {
	if (articleRect.isEmpty()) {
		return rect();
	}
	const auto scale = zoomScale();
	const auto left = int(std::floor(articleRect.x() * scale));
	const auto top = int(std::floor(articleRect.y() * scale));
	const auto right = int(std::ceil(
		(articleRect.x() + articleRect.width()) * scale));
	const auto bottom = int(std::ceil(
		(articleRect.y() + articleRect.height()) * scale));
	return QRect(
		left,
		top,
		std::max(right - left, 1),
		std::max(bottom - top, 1));
}

Ui::Text::QuotePaintCache *MarkdownDocumentWidget::ensurePrePaintCache() {
	EnsurePrePaintCache(_prePaintCache, st::inTextPalette.monoFg);
	return _prePaintCache.get();
}

Ui::Text::QuotePaintCache *MarkdownDocumentWidget::ensureBlockquotePaintCache() {
	EnsureBlockquotePaintCache(
		_blockquotePaintCache,
		st::defaultMarkdown.quotePaintColors.blockquote);
	return _blockquotePaintCache.get();
}

MarkdownArticlePaintContext MarkdownDocumentWidget::textPaintContext(
		QRect clip) {
	const auto scale = zoomScale();
	const auto logicalRect = QRect(QPoint(), QSize(
		articleLayoutWidth(width()),
		std::max(int(std::floor(height() / scale)), 1)));
	auto context = MarkdownArticlePaintContext(_theme->preparePaintContext(
		_style.get(),
		logicalRect,
		logicalRect,
		clip,
		!window()->isActiveWindow()));
	context.mediaPixelScale = scale;
	context.caches = {
		.pre = ensurePrePaintCache(),
		.blockquote = ensureBlockquotePaintCache(),
		.colors = _highlightColors,
		.repaint = [=] {
			crl::on_main(this, [=] {
				update();
			});
		},
		.repaintRect = [=](QRect articleRect) {
			crl::on_main(this, [=] {
				if (articleRect.isEmpty()) {
					update();
				} else {
					update(articleRectToWidget(articleRect));
				}
			});
		},
	};
	context.selectionState.selection = !_selection.empty()
		? _selection
		: _savedSelection;
	context.selectionState.endpoints = !_selection.empty()
		? &_selectionEndpoints
		: &_savedSelectionEndpoints;
	return context;
}

void MarkdownDocumentWidget::stopPressedPlaceholderRipple() {
	if (_pressedPlaceholderId) {
		stopPlaceholderRipple(_pressedPlaceholderId);
		_pressedPlaceholderId = {};
	}
}

void MarkdownDocumentWidget::dragActionStart(
		QPoint point,
		Qt::MouseButton button) {
	stopPressedPlaceholderRipple();
	const auto state = hitTest(
		point,
		Ui::Text::StateRequest::Flag::LookupLink
			| Ui::Text::StateRequest::Flag::LookupSymbol);
	updateHover(state);
	if (button != Qt::LeftButton) {
		return;
	}
	if (state.codeHeaderCopy) {
		clearSelection();
		_dragStartPosition = point;
		_selectionClickPreparedLink = std::nullopt;
		_dragAction = PrepareDrag;
		return;
	}
	if (state.mediaActivation.kind == MediaActivationKind::Embed
		&& state.mediaActivation.placeholderId) {
		_pressedPlaceholderId = state.mediaActivation.placeholderId;
		addPlaceholderRipple(
			state.mediaActivation.placeholderId,
			state.placeholderLocalPoint);
	}
	_dragStartPosition = point;
	_dragStartHadSelection = !selectionForCopy().empty();
	_selectionClickPreparedLink = (state.preparedLink
		&& state.preparedLink->kind == PreparedLinkKind::ToggleDetails)
		? state.preparedLink
		: std::nullopt;
	ClickHandler::pressed();
	_dragAction = NoDrag;
	_dragExpandedSelection = {};
	_dragSegment = -1;
	_dragSymbol = 0;
	if (ClickHandler::getPressed()) {
		_dragStartPosition = point;
		_dragAction = PrepareDrag;
		return;
	}
	if (!state.valid()) {
		clearSelection();
		return;
	}
	_dragSegment = state.segmentIndex;
	_dragSymbol = selectionOffsetFromHit(state);
	_selection = {
		{ _dragSegment, _dragSymbol },
		{ _dragSegment, _dragSymbol },
	};
	_savedSelection = {};
	_selectionEndpoints = {
		.from = MakeSelectionEndpoint(state),
		.to = MakeSelectionEndpoint(state),
	};
	_savedSelectionEndpoints = {};
	_dragAction = Selecting;
	if (_tripleClickTimer.isActive()
		&& (point - _tripleClickPoint).manhattanLength()
			< QApplication::startDragDistance()
		&& _article->segmentIsText(state.segmentIndex)
		&& state.direct
		&& state.state.uponSymbol) {
		_selectionType = TextSelectType::Paragraphs;
		_selection = selectionFromHit(state);
		if (_selection.from.segment == _dragSegment
			&& _selection.to.segment == _dragSegment) {
			_dragExpandedSelection = TextSelection(
				uint16(_selection.from.offset),
				uint16(_selection.to.offset));
		}
		_tripleClickPoint = point;
		_tripleClickTimer.callOnce(QApplication::doubleClickInterval());
	}
	update();
}

MarkdownArticleHitTestResult MarkdownDocumentWidget::dragActionUpdate(QPoint point) {
	const auto state = hitTest(
		point,
		Ui::Text::StateRequest::Flag::LookupLink
			| Ui::Text::StateRequest::Flag::LookupSymbol);
	if (_dragAction == PrepareDrag
		&& (point - _dragStartPosition).manhattanLength()
			>= QApplication::startDragDistance()) {
		_dragAction = Dragging;
	}
	updateHover(state);
	return state;
}

MarkdownArticleHitTestResult MarkdownDocumentWidget::dragActionFinish(
		QPoint point,
		Qt::MouseButton button) {
	const auto state = dragActionUpdate(point);
	stopPressedPlaceholderRipple();
	auto activated = ClickHandler::unpressed();
	const auto dragStartHadSelection = _dragStartHadSelection;
	const auto wasClick = (_dragAction == NoDrag)
		|| (_dragAction == PrepareDrag);
	const auto toggleFromDetailsClick = !dragStartHadSelection
		&& _selection.empty()
		&& _selectionClickPreparedLink
		&& (point - _dragStartPosition).manhattanLength()
			< QApplication::startDragDistance()
		&& state.preparedLink
		&& state.preparedLink->kind == PreparedLinkKind::ToggleDetails
		&& state.preparedLink->target == _selectionClickPreparedLink->target;
	if (_dragAction == Dragging
		|| (_dragAction == Selecting && !_selection.empty())) {
		activated = nullptr;
	} else if (_dragAction == PrepareDrag && button != Qt::RightButton) {
		clearSelection();
	}
	const auto preparedToggle = toggleFromDetailsClick
		? state.preparedLink
		: std::nullopt;
	_dragStartHadSelection = false;
	_dragAction = NoDrag;
	_selectionType = TextSelectType::Letters;
	_dragExpandedSelection = {};
	updateHover(state);
	if (state.codeHeaderCopy
		&& (button == Qt::LeftButton || button == Qt::MiddleButton)
		&& wasClick) {
		copyCodeBlock(state);
		return state;
	}
	if (activated
		&& (button == Qt::LeftButton || button == Qt::MiddleButton)) {
		if (state.mediaActivation.kind != MediaActivationKind::None
			&& _activateMedia
			&& _activateMedia(state.mediaActivation, button)) {
			return state;
		}
		if (state.preparedLink && _activateLink) {
			if (state.preparedLink->kind == PreparedLinkKind::ToggleDetails
				&& dragStartHadSelection) {
				return state;
			}
			_activateLink(*state.preparedLink, button);
		} else {
			auto clickHandlerContext = this->clickHandlerContext();
			const auto monospace = std::dynamic_pointer_cast<MonospaceClickHandler>(
				activated);
			const auto pre = dynamic_cast<Ui::Text::PreClickHandler*>(
				activated.get());
			if (monospace || pre) {
				clickHandlerContext = viewerToastClickHandlerContext();
			}
			if (monospace) {
				const auto context = clickHandlerContext.value<ClickHandlerContext>();
				if (context.show) {
					const auto handled = Ui::Integration::Instance().copyPreOnClick(
						clickHandlerContext);
					static_cast<void>(handled);
				}
			}
			auto context = ClickContext();
			context.button = button;
			context.other = std::move(clickHandlerContext);
			ActivateClickHandler(window(), activated, context);
		}
	} else if (preparedToggle
		&& (button == Qt::LeftButton || button == Qt::MiddleButton)
		&& _activateLink) {
		clearSelection();
		_activateLink(*preparedToggle, button);
		return state;
	} else if ((button == Qt::LeftButton || button == Qt::MiddleButton)
		&& state.mediaActivation.kind != MediaActivationKind::None
		&& _activateMedia
		&& _activateMedia(state.mediaActivation, button)) {
		return state;
	}
	if (QGuiApplication::clipboard()->supportsSelection()
		&& !_selection.empty()) {
		if (const auto text = getSelectedText(); !text.empty()) {
			TextUtilities::SetClipboardText(text, QClipboard::Selection);
		}
	}
	return state;
}

void MarkdownDocumentWidget::applyCursor(style::cursor cursor) {
	if (_cursor != cursor) {
		_cursor = cursor;
		setCursor(_cursor);
	}
}

double MarkdownDocumentWidget::zoomScale() const {
	return std::max(_zoom, 1) / 100.;
}

int MarkdownDocumentWidget::articleLayoutWidth(int widgetWidth) const {
	const auto layoutWidth = int(std::floor(widgetWidth / zoomScale()));
	const auto limit = _article ? _article->maxWidth() : layoutWidth;
	return std::clamp(layoutWidth, 1, std::max(limit, 1));
}

} // namespace Iv::Markdown
