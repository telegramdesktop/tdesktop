/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "base/timer.h"
#include "base/unique_qptr.h"
#include "base/weak_ptr.h"
#include "iv/markdown/iv_markdown_document.h"
#include "iv/markdown/iv_markdown_prepare.h"
#include "ui/effects/animations.h"
#include "ui/widgets/rp_window.h"

#include <optional>
#include <QtCore/QString>
#include <QtCore/QVariant>

namespace Ui {
class DropdownMenu;
class FlatLabel;
class IconButton;
class LayerManager;
class PopupMenu;
class Show;
class FadeShadow;
template <typename Widget>
class FadeWrapScaled;
} // namespace Ui

namespace Iv {
class SearchController;
} // namespace Iv

namespace Iv::Markdown {

class Controller final : public base::has_weak_ptr {
public:
	Controller(
		not_null<Delegate*> delegate,
		PreparedDocument document,
		QString title,
		OpenOptions options = {});
	Controller(
		not_null<Delegate*> delegate,
		MarkdownArticleContent content,
		QString title,
		std::shared_ptr<MathRenderer> renderer = nullptr,
		OpenOptions options = {});
	~Controller();

	void activate();
	void show(
		MarkdownArticleContent content,
		QString title,
		OpenOptions options = {});
	void update(
		MarkdownArticleContent content,
		QString title,
		OpenOptions options = {});
	void updateOptions(OpenOptions options = {});

	[[nodiscard]] bool active() const;
	void showJoinedTooltip();
	void minimize();

	[[nodiscard]] rpl::producer<Event> events() const {
		return _events.events();
	}

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	struct HistoryEntry;

	void close();
	void createWindow();
	void createLayerManager();
	void createPreview();
	[[nodiscard]] bool updateExistingPreview(
		MarkdownArticleContent content,
		OpenOptions options,
		int scrollTop);
	void setContent(
		MarkdownArticleContent content,
		QString title,
		OpenOptions options,
		bool preserveScroll);
	void updateTitleGeometry(int newWidth) const;
	void showMenu();
	void createZoomDropdown();
	void showZoomDropdown();
	void hideZoomDropdown();
	void updateZoomDropdownGeometry();
	[[nodiscard]] Fn<void()> zoomActivatedCallback();
	void createSearchController();
	void toggleSearchBar();
	void openSource();
	[[nodiscard]] ViewerKind viewerKind() const;
	[[nodiscard]] QString subtitleText() const;
	[[nodiscard]] bool canOpenSource() const;
	[[nodiscard]] bool canShare() const;
	[[nodiscard]] bool historyEnabled(const OpenOptions &options) const;
	[[nodiscard]] bool sameHistoryPage(
		const HistoryEntry &entry,
		uint64 pageId,
		const QString &sourceUrl) const;
	[[nodiscard]] bool sameCurrentPage(
		uint64 pageId,
		const QString &sourceUrl) const;
	[[nodiscard]] bool sameHistoryLocation(
		const HistoryEntry &entry,
		uint64 pageId,
		const QString &sourceUrl,
		const QString &hash) const;
	[[nodiscard]] int findHistoryEntry(
		uint64 pageId,
		const QString &sourceUrl,
		const QString &hash) const;
	void saveCurrentHistoryScroll(std::optional<int> scrollTop = std::nullopt);
	void updateCurrentHistoryEntry(
		const MarkdownArticleContent &content,
		const QString &title,
		const OpenOptions &options);
	void handleOpenPage(Event event);
	[[nodiscard]] bool showHistoryEntry(int index);
	void stepHistory(int delta);
	void updateHistoryButtons();
	void refreshTitle();

	struct HistoryEntry {
		uint64 pageId = 0;
		QString sourceUrl;
		QString hash;
		QString title;
		std::shared_ptr<MarkdownArticleContent> preparedContent;
		OpenOptions options;
		int scrollTop = 0;

		[[nodiscard]] bool hydrated() const {
			return preparedContent != nullptr;
		}
	};

	const not_null<Delegate*> _delegate;

	const std::shared_ptr<const PreparedDocument> _document;
	std::optional<MarkdownArticleContent> _preparedContent;
	QString _title;
	const std::shared_ptr<MathRenderer> _renderer;
	OpenOptions _options;
	std::shared_ptr<QVariant> _clickHandlerContextRef;
	std::unique_ptr<Ui::RpWindow> _window;
	std::unique_ptr<Ui::RpWidget> _subtitleWrap;
	std::unique_ptr<Ui::FlatLabel> _subtitle;
	Ui::Animations::Simple _subtitleBackShift;
	Ui::Animations::Simple _subtitleForwardShift;
	object_ptr<Ui::IconButton> _menuToggle = { nullptr };
	object_ptr<Ui::FadeWrapScaled<Ui::IconButton>> _back = { nullptr };
	object_ptr<Ui::FadeWrapScaled<Ui::IconButton>> _forward = { nullptr };
	object_ptr<Ui::FadeShadow> _titleShadow = { nullptr };
	base::unique_qptr<Ui::PopupMenu> _menu;
	base::unique_qptr<Ui::DropdownMenu> _zoomDropdown;
	base::Timer _zoomDropdownHideTimer;
	Ui::RpWidget *_container = nullptr;
	std::unique_ptr<Ui::LayerManager> _layerManager;
	std::shared_ptr<Ui::Show> _show;
	std::unique_ptr<Ui::RpWidget> _preview;
	std::unique_ptr<SearchController> _search;
	rpl::variable<int> _searchBarHeight = 0;
	std::vector<HistoryEntry> _history;
	int _historyIndex = -1;
	int _shownHistoryIndex = -1;

	rpl::event_stream<Event> _events;

	rpl::lifetime _lifetime;

};

[[nodiscard]] std::unique_ptr<Controller> TryOpenLocalFile(
	not_null<Delegate*> delegate,
	const QString &path,
	OpenOptions options = {});

} // namespace Iv::Markdown
