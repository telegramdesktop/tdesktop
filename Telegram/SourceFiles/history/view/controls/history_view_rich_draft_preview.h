/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "iv/markdown/iv_markdown_article.h"
#include "rpl/lifetime.h"
#include "ui/rp_widget.h"

#include <QtGui/QPixmap>

#include <memory>

class QMouseEvent;
class QWidget;

namespace Data {
struct Draft;
struct FileOrigin;
} // namespace Data

namespace Iv {
struct RichPage;
namespace Markdown {
class MediaBlockHost;
class MediaRuntime;
} // namespace Markdown
} // namespace Iv

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class ChatStyle;
class ChatTheme;
} // namespace Ui

namespace HistoryView::Controls {

class RichDraftPreview final : public Ui::RpWidget {
public:
	RichDraftPreview(
		QWidget *parent,
		not_null<Main::Session*> session,
		Fn<bool()> paused,
		Fn<void()> activate,
		Fn<void()> relayout = nullptr);
	~RichDraftPreview();

	void setDraft(const Data::Draft &draft, Data::FileOrigin draftOrigin);

	[[nodiscard]] int resizeGetHeight(
		int width,
		int minHeight,
		int maxHeight);

private:
	void paint(QRect clip);
	void clearPreparedContent();
	void rebuildPreparedContent(
		const Data::Draft &draft,
		Data::FileOrigin draftOrigin);
	void refreshPaletteDependentCaches();
	void regenerateFadePixmap();
	void requestArticleRepaint(QRect articleRect);
	void requestArticleRelayout(QRect articleRect);
	void detachArticleBindings();
	[[nodiscard]] QRect articleRect() const;
	[[nodiscard]] QRect translatedArticleRect(QRect articleRect) const;
	[[nodiscard]] int contentHeightForWidth(int width);
	void mouseReleaseEvent(QMouseEvent *e) override;

	using RpWidget::resizeGetHeight;

	const not_null<Main::Session*> _session;
	const Fn<bool()> _paused;
	const Fn<void()> _activate;
	const Fn<void()> _relayout;
	std::shared_ptr<const Iv::RichPage> _page;
	std::shared_ptr<Iv::Markdown::MediaRuntime> _mediaRuntime;
	std::unique_ptr<Iv::Markdown::MediaBlockHost> _host;
	Iv::Markdown::MarkdownArticle _article;
	std::unique_ptr<Ui::ChatTheme> _theme;
	std::unique_ptr<Ui::ChatStyle> _style;
	rpl::lifetime _highlightReadyLifetime;
	int _paletteVersion = -1;
	QPixmap _fadePixmap;
	int _fullArticleHeight = 0;
	int _fullContentHeight = 0;
	bool _clippedBottom = false;

};

} // namespace HistoryView::Controls
