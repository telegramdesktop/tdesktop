/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_rich_draft_preview.h"

#include "base/weak_ptr.h"
#include "chat_helpers/spellchecker_common.h"
#include "data/data_drafts.h"
#include "iv/iv_cached_media.h"
#include "iv/markdown/iv_markdown_article_layout_blocks.h"
#include "iv/markdown/iv_markdown_prepare.h"
#include "iv/iv_rich_page.h"
#include "main/main_session.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/painter.h"
#include "window/themes/window_theme.h"

#include <QtGui/QLinearGradient>
#include <QtGui/QMouseEvent>

#include <algorithm>
#include <utility>

#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"

namespace HistoryView::Controls {
namespace {

class PreviewMediaBlockHost final : public Iv::Markdown::MediaBlockHost {
public:
	PreviewMediaBlockHost(
		Fn<void(QRect)> repaint,
		Fn<void(QRect)> relayout)
	: _repaint(std::move(repaint))
	, _relayout(std::move(relayout)) {
	}

	void requestRepaint(QRect articleRect) override {
		crl::on_main([repaint = _repaint, articleRect] {
			if (repaint) {
				repaint(articleRect);
			}
		});
	}

	void requestRelayout(QRect articleRect) override {
		crl::on_main([relayout = _relayout, articleRect] {
			if (relayout) {
				relayout(articleRect);
			}
		});
	}

private:
	const Fn<void(QRect)> _repaint;
	const Fn<void(QRect)> _relayout;

};

[[nodiscard]] int PreviewBottomFadeHeight() {
	return 2 * Iv::Markdown::TextLineHeight(st::messageMarkdown.body);
}

} // namespace

RichDraftPreview::RichDraftPreview(
	QWidget *parent,
	not_null<Main::Session*> session,
	Fn<bool()> paused,
	Fn<void()> activate,
	Fn<void()> relayout)
: RpWidget(parent)
, _session(session)
, _paused(std::move(paused))
, _activate(std::move(activate))
, _relayout(std::move(relayout))
, _article(st::messageMarkdown)
, _theme(Window::Theme::DefaultChatThemeOn(lifetime()))
, _style(std::make_unique<Ui::ChatStyle>(session->colorIndicesValue())) {
	setCursor(style::cur_pointer);

	_style->apply(_theme.get());

	const auto weak = base::make_weak(this);
	_host = std::make_unique<PreviewMediaBlockHost>(
		[weak](QRect articleRect) {
			if (const auto owner = weak.get()) {
				owner->requestArticleRepaint(articleRect);
			}
		},
		[weak](QRect articleRect) {
			if (const auto owner = weak.get()) {
				owner->requestArticleRelayout(articleRect);
			}
		});
	_article.setMediaBlockHost(_host.get());
	_article.setTextRepaintCallbacks(
		[weak] {
			if (const auto owner = weak.get()) {
				owner->requestArticleRepaint(QRect());
			}
		},
		[weak](QRect articleRect) {
			if (const auto owner = weak.get()) {
				owner->requestArticleRepaint(articleRect);
			}
		});

	paintRequest(
	) | rpl::on_next([=](QRect clip) {
		paint(clip);
	}, lifetime());

	Spellchecker::HighlightReady(
	) | rpl::on_next([=](Spellchecker::HighlightProcessId processId) {
		if (_article.highlightProcessDone(processId)) {
			requestArticleRepaint(QRect());
		}
	}, _highlightReadyLifetime);

	_style->paletteChanged(
	) | rpl::on_next([=] {
		refreshPaletteDependentCaches();
	}, lifetime());

	Window::Theme::Background()->updates(
	) | rpl::on_next([=](
			const Window::Theme::BackgroundUpdate &backgroundUpdate) {
		if (backgroundUpdate.paletteChanged()) {
			refreshPaletteDependentCaches();
		} else {
			update();
		}
	}, lifetime());

	refreshPaletteDependentCaches();
}

RichDraftPreview::~RichDraftPreview() {
	_highlightReadyLifetime.destroy();
	detachArticleBindings();
}

void RichDraftPreview::setDraft(
		const Data::Draft &draft,
		Data::FileOrigin draftOrigin) {
	rebuildPreparedContent(draft, std::move(draftOrigin));
}

int RichDraftPreview::resizeGetHeight(
		int width,
		int minHeight,
		int maxHeight) {
	const auto fullHeight = contentHeightForWidth(width);
	const auto height = std::min(
		std::max(fullHeight, minHeight),
		maxHeight);
	const auto visibleArticleHeight = std::max(
		0,
		height - st::msgReplyPadding.top() - st::msgReplyPadding.bottom());
	_clippedBottom = (_fullArticleHeight > visibleArticleHeight);
	resize(width, height);
	return height;
}

void RichDraftPreview::paint(QRect clip) {
	Painter p(this);
	const auto paused = _paused && _paused();
	p.setInactive(paused);
	p.fillRect(rect(), st::historyComposeAreaBg);

	if (_style && (_paletteVersion != _style->paletteVersion())) {
		_paletteVersion = _style->paletteVersion();
		_article.invalidatePaletteCache();
	}

	const auto content = articleRect();
	if (content.isEmpty()) {
		return;
	}
	const auto visibleBodyHeight = content.height();
	const auto articleClip = content.intersected(clip).translated(
		-content.topLeft());
	if (!articleClip.isEmpty()) {
		auto articleContext = Iv::Markdown::MarkdownArticlePaintContext(
			_theme->preparePaintContext(
				_style.get(),
				QRect(QPoint(), content.size()),
				QRect(QPoint(), content.size()),
				articleClip,
				paused));
		const auto messageStyle = articleContext.messageStyle();
		articleContext.caches = {
			.pre = messageStyle->preCache.get(),
			.blockquote = articleContext.quoteCache({}, 0),
			.colors = _style->highlightColors(),
			.st = &messageStyle->richPageStyle,
			.repaint = [weak = base::make_weak(this)] {
				if (const auto owner = weak.get()) {
					owner->requestArticleRepaint(QRect());
				}
			},
			.repaintRect = [weak = base::make_weak(this)](QRect articleRect) {
				if (const auto owner = weak.get()) {
					owner->requestArticleRepaint(articleRect);
				}
			},
		};
		_article.setVisibleTopBottom(0, visibleBodyHeight);
		p.save();
		p.setClipRect(content.intersected(clip));
		p.translate(content.topLeft());
		_article.paint(p, articleContext);
		p.restore();
	}
	if (!_clippedBottom
		|| (_fullArticleHeight <= visibleBodyHeight)
		|| _fadePixmap.isNull()) {
		return;
	}
	// The article is clipped at the content rect bottom, so the fade must
	// reach full opacity exactly there, not at the widget bottom - otherwise
	// the last padding-covered rows of the gradient never fully hide the
	// article and its clip line stays visible through the fade.
	const auto contentBottom = content.y() + content.height();
	const auto fadeHeight = std::min(
		PreviewBottomFadeHeight(),
		contentBottom);
	if (fadeHeight <= 0) {
		return;
	}
	const auto fadeRect = QRect(
		0,
		contentBottom - fadeHeight,
		width(),
		fadeHeight);
	// No explicit source rect: it would be in device pixels, and passing the
	// logical size sampled only the top part of the gradient on hidpi, so the
	// fade ended half-transparent with a visible clipping line. Scaling the
	// whole pixmap always fades out to fully opaque background at the bottom.
	p.drawPixmap(fadeRect, _fadePixmap);
}

void RichDraftPreview::clearPreparedContent() {
	_page = nullptr;
	_mediaRuntime = nullptr;
	_article.setContent({});
	_article.invalidateLayout();
	_fullArticleHeight = 0;
	_fullContentHeight = 0;
	_clippedBottom = false;
}

void RichDraftPreview::rebuildPreparedContent(
		const Data::Draft &draft,
		Data::FileOrigin draftOrigin) {
	const auto next = draft.hasRichMessage()
		? draft.richMessage
		: std::shared_ptr<const Iv::RichPage>();
	if (!next) {
		if (_page
			|| _mediaRuntime
			|| _fullArticleHeight
			|| _fullContentHeight
			|| _clippedBottom) {
			clearPreparedContent();
			if (_relayout) {
				_relayout();
			}
			update();
		}
		return;
	}
	if (_page && Iv::RichPagesEqual(*_page, *next)) {
		return;
	}
	const auto richLimits = Iv::ResolveRichMessageLimits(_session);
	auto mediaRuntime = Iv::CreateMessageMediaRuntime(
		_session,
		FullMsgId(),
		[](QString) {},
		[](QString) {},
		std::move(draftOrigin));
	auto prepared = Iv::Markdown::TryPrepareNativeInstantView({
		.richPage = next,
		.mediaRuntime = mediaRuntime,
		.dimensionsOverride = Iv::Markdown::CaptureMarkdownPrepareDimensions(
			st::messageMarkdown),
		.tableRenderLimits
			= Iv::Markdown::PrepareTableRenderLimitsForRichMessage(richLimits),
	});
	if (!prepared.supported()) {
		clearPreparedContent();
		_page = next;
		if (_relayout) {
			_relayout();
		}
		update();
		return;
	}
	_page = next;
	_mediaRuntime = std::move(mediaRuntime);
	_article.setContent(std::move(prepared.content));
	_fullArticleHeight = 0;
	_fullContentHeight = 0;
	_clippedBottom = false;
	if (_relayout) {
		_relayout();
	}
	update();
}

void RichDraftPreview::refreshPaletteDependentCaches() {
	if (_style) {
		_paletteVersion = _style->paletteVersion();
	}
	_article.invalidatePaletteCache();
	regenerateFadePixmap();
	update();
}

void RichDraftPreview::regenerateFadePixmap() {
	const auto height = PreviewBottomFadeHeight();
	if (height <= 0) {
		_fadePixmap = QPixmap();
		return;
	}
	const auto size = QSize(1, height);
	auto fade = QPixmap(size * style::DevicePixelRatio());
	fade.setDevicePixelRatio(style::DevicePixelRatio());
	fade.fill(Qt::transparent);
	{
		auto p = QPainter(&fade);
		auto gradient = QLinearGradient(0, 0, 0, size.height());
		gradient.setStops({
			{ 0., QColor(0, 0, 0, 0) },
			{ 1., st::historyComposeAreaBg->c },
		});
		p.fillRect(QRect(QPoint(), size), gradient);
	}
	_fadePixmap = std::move(fade);
}

void RichDraftPreview::requestArticleRepaint(QRect articleRect) {
	const auto translated = translatedArticleRect(articleRect);
	if (!translated.isEmpty()) {
		update(translated);
	}
}

void RichDraftPreview::requestArticleRelayout(QRect articleRect) {
	const auto hadClippedBottom = _clippedBottom;
	_article.invalidateLayout();
	requestArticleRepaint(QRect());
	if (_relayout) {
		_relayout();
	}
	if (hadClippedBottom || _clippedBottom || !articleRect.isEmpty()) {
		update();
	}
}

void RichDraftPreview::detachArticleBindings() {
	if (_article.mediaBlockHost() == _host.get()) {
		_article.setTextRepaintCallbacks(nullptr, nullptr);
		_article.setMediaBlockHost(nullptr);
	}
}

QRect RichDraftPreview::articleRect() const {
	const auto left = st::msgReplyPadding.left();
	const auto top = st::msgReplyPadding.top();
	const auto width = std::max(
		0,
		this->width() - left - st::msgReplyPadding.right());
	const auto height = std::max(
		0,
		this->height() - top - st::msgReplyPadding.bottom());
	return QRect(left, top, width, height);
}

QRect RichDraftPreview::translatedArticleRect(QRect articleRect) const {
	const auto content = this->articleRect();
	if (content.isEmpty()) {
		return QRect();
	}
	if (articleRect.isNull()) {
		return content.intersected(rect());
	}
	return articleRect.translated(content.topLeft()).intersected(content);
}

int RichDraftPreview::contentHeightForWidth(int width) {
	const auto available = std::max(
		0,
		width - st::msgReplyPadding.left() - st::msgReplyPadding.right());
	_fullArticleHeight = _article.resizeGetHeight(available);
	_fullContentHeight = st::msgReplyPadding.top()
		+ _fullArticleHeight
		+ st::msgReplyPadding.bottom();
	return _fullContentHeight;
}

void RichDraftPreview::mouseReleaseEvent(QMouseEvent *e) {
	if ((e->button() == Qt::LeftButton) && _activate) {
		_activate();
	}
	RpWidget::mouseReleaseEvent(e);
}

} // namespace HistoryView::Controls
