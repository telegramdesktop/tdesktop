/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "chat_helpers/tabbed_selector.h"
#include "base/timer.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "layout/layout_mosaic.h"

#include <QtCore/QTimer>

namespace style {
struct ComposeIcons;
} // namespace style

namespace Api {
struct SendOptions;
} // namespace Api

namespace InlineBots {
namespace Layout {
class ItemBase;
} // namespace Layout
class Result;
} // namespace InlineBots

namespace Ui {
class PopupMenu;
class RoundButton;
class TabbedSearch;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace SendMenu {
struct Details;
} // namespace SendMenu

namespace Data {
class StickersSet;
} // namespace Data

namespace ChatHelpers {

void AddGifAction(
	Fn<void(QString, Fn<void()> &&, const style::icon*)> callback,
	std::shared_ptr<Show> show,
	not_null<DocumentData*> document,
	const style::ComposeIcons *iconsOverride = nullptr);

class StickersListFooter;
struct StickerIcon;
struct GifSection;

struct GifsListDescriptor {
	std::shared_ptr<Show> show;
	Fn<bool()> paused;
	const style::EmojiPan *st = nullptr;
};

class GifsListWidget final
	: public TabbedSelector::Inner
	, public InlineBots::Layout::Context {
public:
	GifsListWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		PauseReason level);
	GifsListWidget(QWidget *parent, GifsListDescriptor &&descriptor);

	rpl::producer<FileChosen> fileChosen() const;
	rpl::producer<PhotoChosen> photoChosen() const;
	rpl::producer<InlineChosen> inlineResultChosen() const;

	void refreshRecent() override;
	void preloadImages() override;
	void clearSelection() override;
	object_ptr<TabbedSelector::InnerFooter> createFooter() override;

	void inlineItemLayoutChanged(const InlineBots::Layout::ItemBase *layout) override;
	void inlineItemRepaint(const InlineBots::Layout::ItemBase *layout) override;
	bool inlineItemVisible(const InlineBots::Layout::ItemBase *layout) override;
	Data::FileOrigin inlineItemFileOrigin() override;

	void afterShown() override;
	void beforeHiding() override;

	void setInlineQueryPeer(PeerData *peer) {
		_inlineQueryPeer = peer;
	}
	void searchForGifs(const QString &query);
	void sendInlineRequest();

	void cancelled();
	rpl::producer<> cancelRequests() const;

	base::unique_qptr<Ui::PopupMenu> fillContextMenu(
		const SendMenu::Details &details) override;

	~GifsListWidget();

protected:
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void leaveToChildEvent(QEvent *e, QWidget *child) override;
	void enterFromChildEvent(QEvent *e, QWidget *child) override;

	TabbedSelector::InnerFooter *getFooter() const override;
	void processHideFinished() override;
	void processPanelHideFinished() override;
	int countDesiredHeight(int newWidth) override;

private:
	enum class Section {
		Inlines,
		Gifs,
	};

	using InlineResult = InlineBots::Result;
	using InlineResults = std::vector<std::unique_ptr<InlineResult>>;
	using LayoutItem = InlineBots::Layout::ItemBase;

	struct InlineCacheEntry {
		QString nextOffset;
		InlineResults results;
	};

	void setupSearch();
	void clearHeavyData();
	void cancelGifsSearch();
	void switchToSavedGifs();
	void refreshSavedGifs();
	int refreshInlineRows(const InlineCacheEntry *results, bool resultsDeleted);
	void checkLoadMore();

	int32 showInlineRows(bool newResults);
	bool refreshInlineRows(int32 *added = 0);
	void inlineResultsDone(const MTPmessages_BotResults &result);

	void updateSelected();
	void paintInlineItems(Painter &p, QRect clip);
	void refreshIcons();
	[[nodiscard]] std::vector<StickerIcon> fillIcons();

	void updateInlineItems();
	void repaintItems(crl::time now = 0);
	void showPreview();

	void clearInlineRows(bool resultsDeleted);
	LayoutItem *layoutPrepareSavedGif(not_null<DocumentData*> document);
	LayoutItem *layoutPrepareInlineResult(not_null<InlineResult*> result);

	void deleteUnusedGifLayouts();

	void deleteUnusedInlineLayouts();

	int validateExistingInlineRows(const InlineResults &results);
	void selectInlineResult(
		int index,
		Api::SendOptions options,
		bool forceSend = false);

	const std::shared_ptr<Show> _show;
	std::unique_ptr<Ui::TabbedSearch> _search;

	MTP::Sender _api;

	Section _section = Section::Gifs;
	crl::time _lastScrolledAt = 0;
	crl::time _lastUpdatedAt = 0;
	base::Timer _updateInlineItems;
	bool _inlineWithThumb = false;

	std::map<
		not_null<DocumentData*>,
		std::unique_ptr<LayoutItem>> _gifLayouts;
	std::map<
		not_null<InlineResult*>,
		std::unique_ptr<LayoutItem>> _inlineLayouts;

	StickersListFooter *_footer = nullptr;
	std::vector<GifSection> _sections;
	base::flat_map<uint64, std::unique_ptr<Data::StickersSet>> _fakeSets;
	uint64 _chosenSetId = 0;

	Mosaic::Layout::MosaicLayout<LayoutItem> _mosaic;

	int _selected = -1;
	int _pressed = -1;
	QPoint _lastMousePos;

	base::Timer _previewTimer;
	bool _previewShown = false;

	std::map<QString, std::unique_ptr<InlineCacheEntry>> _inlineCache;
	QTimer _inlineRequestTimer;

	UserData *_searchBot = nullptr;
	mtpRequestId _searchBotRequestId = 0;
	PeerData *_inlineQueryPeer = nullptr;
	QString _inlineQuery, _inlineNextQuery, _inlineNextOffset;
	mtpRequestId _inlineRequestId = 0;

	rpl::event_stream<FileChosen> _fileChosen;
	rpl::event_stream<PhotoChosen> _photoChosen;
	rpl::event_stream<InlineChosen> _inlineResultChosen;
	rpl::event_stream<> _cancelled;

};

} // namespace ChatHelpers
