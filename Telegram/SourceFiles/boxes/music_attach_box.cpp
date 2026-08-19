/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/music_attach_box.h"

#include "api/api_common.h"
#include "api/api_sending.h"
#include "apiwrap.h"
#include "base/platform/base_platform_info.h"
#include "base/algorithm.h"
#include "base/call_delayed.h"
#include "base/unique_qptr.h"
#include "base/unixtime.h"
#include "boxes/send_files_box.h"
#include "boxes/sticker_set_box.h"
#include "chat_helpers/message_field.h"
#include "config.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/file_utilities.h"
#include "data/components/ephemeral_messages.h"
#include "data/data_chat_participant_status.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_peer.h"
#include "data/data_saved_music.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/view/media/history_view_save_document_action.h"
#include "history/view/history_view_cursor_state.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "info/media/info_media_empty_widget.h"
#include "info/media/info_media_list_widget.h"
#include "info/profile/info_profile_icon.h"
#include "info/info_controller.h"
#include "info/info_wrap_widget.h"
#include "inline_bots/inline_bot_result.h"
#include "lang/lang_keys.h"
#include "layout/layout_selection.h"
#include "main/session/session_show.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "menu/menu_send.h"
#include "mtproto/sender.h"
#include "overview/overview_layout.h"
#include "storage/localimageloader.h"
#include "storage/storage_media_prepare.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/multi_select.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"

#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_credits.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_overview.h"
#include "styles/style_share_box.h"
#include "styles/style_widgets.h"

#include <QtCore/QPointer>
#include <QtCore/QTimer>

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

#include <rpl/variable.h>

namespace {

constexpr auto kSearchRequestDelay = crl::time(400);
constexpr auto kFirstLimit = 10;

class MusicSectionController final : public Info::AbstractController {
public:
	struct SavedMusicSectionTag {
	};
	struct GlobalMediaSectionTag {
	};

	MusicSectionController(
			not_null<Window::SessionController*> controller,
			SavedMusicSectionTag);

	MusicSectionController(
			not_null<Window::SessionController*> controller,
			GlobalMediaSectionTag);

	[[nodiscard]] Info::Key key() const override;
	[[nodiscard]] PeerData *migrated() const override;
	[[nodiscard]] Info::Section section() const override;
	[[nodiscard]] rpl::producer<QString> searchQueryValue() const override;

	void setQuery(QString query);

private:
	const Info::Key _key;
	const Info::Section _section;
	rpl::variable<QString> _query;
	const bool _globalMedia = false;

};

class HistoryMusicSection final : public Ui::RpWidget {
public:
	HistoryMusicSection(
			QWidget *parent,
			std::unique_ptr<MusicSectionController> controller,
			rpl::producer<QString> title,
			int titleBottomSkip = 0);

	[[nodiscard]] not_null<Info::Media::ListWidget*> list() const;
	[[nodiscard]] rpl::producer<Info::SelectedItems> selectedListValue() const;
	[[nodiscard]] bool hasResults() const;
	[[nodiscard]] std::optional<int> fullCount() const;
	[[nodiscard]] rpl::producer<std::optional<int>> fullCountValue() const;
	[[nodiscard]] auto globalMediaSliceViewValue() const
		-> rpl::producer<std::optional<Info::Media::GlobalMediaSliceView>>;

	void setCollapsed(bool collapsed);
	void setPagingEnabled(bool enabled);
	void setExternalViewportHeight(int height);
	void setTitleEligible(bool eligible);
	void setTopSkip(int topSkip);

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(int visibleTop, int visibleBottom) override;

private:
	[[nodiscard]] int listHeightForCurrentMode() const;
	void updatePreloadEnabled();
	void updateTitleVisibility();
	void refreshHeight();

	const std::unique_ptr<MusicSectionController> _controller;
	object_ptr<Ui::VerticalLayout> _titleWrap;
	object_ptr<Info::Media::ListWidget> _list;
	bool _collapsed = false;
	bool _pagingEnabled = true;
	bool _titleEligible = true;
	bool _inResize = false;
	int _topSkip = 0;

};

class SearchEmptyWidget final : public Info::Media::EmptyWidget {
public:
	explicit SearchEmptyWidget(QWidget *parent);
	void setQuery(QString query);

};

[[nodiscard]] style::margins MusicAttachSubsectionTitlePadding(
		int topSkip = 0,
		int bottomSkip = 0) {
	const auto sideSkip = st::overviewFileLayout.songPadding.left();
	return style::margins(
		sideSkip - st::defaultSubsectionTitlePadding.left(),
		topSkip,
		sideSkip - st::defaultSubsectionTitlePadding.right(),
		bottomSkip);
}

class GlobalMusicSearchSection final
	: public Ui::RpWidget
	, private Overview::Layout::Delegate {
public:
	using SelectedDocuments = std::vector<DocumentData*>;

	GlobalMusicSearchSection(
			QWidget *parent,
			not_null<Window::SessionController*> controller,
			not_null<PeerData*> queryPeer,
			rpl::producer<QString> title);
	~GlobalMusicSearchSection();

	[[nodiscard]] bool available() const;
	void setQuery(QString query);
	void setSelectedLimit(int limit);
	void setPagingEnabled(bool enabled);
	[[nodiscard]] bool hasResults() const;
	[[nodiscard]] bool busy() const;
	[[nodiscard]] bool awayFromTop() const;
	[[nodiscard]] rpl::producer<bool> awayFromTopValue() const;
	[[nodiscard]] rpl::producer<SelectedDocuments> selectedDocumentsValue() const;
	[[nodiscard]] rpl::producer<> stateChanged() const;

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(int visibleTop, int visibleBottom) override;
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	struct OwnedRow {
		DocumentData *document = nullptr;
		std::unique_ptr<HistoryItem, HistoryItem::Destroyer> item;
		std::unique_ptr<Overview::Layout::Document> layout;
	};

	struct CacheEntry {
		QString nextOffset;
		std::vector<std::unique_ptr<OwnedRow>> rows;
	};

	struct Row {
		OwnedRow *owned = nullptr;
		int top = 0;
		int height = 0;
	};

	void registerHeavyItem(
		not_null<const Overview::Layout::ItemBase*> item) override;
	void unregisterHeavyItem(
		not_null<const Overview::Layout::ItemBase*> item) override;
	void repaintItem(
		not_null<const Overview::Layout::ItemBase*> item) override;
	bool itemVisible(
		not_null<const Overview::Layout::ItemBase*> item) override;
	[[nodiscard]] not_null<StickerPremiumMark*> hiddenMark() override;
	void openPhoto(not_null<PhotoData*> photo, FullMsgId id) override;
	void openDocument(
		not_null<DocumentData*> document,
		FullMsgId id,
		bool showInMediaView = false) override;
	void setPending(bool pending);
	void setLoading(bool loading);
	void resolveBot();
	void applyResolvedBot();
	void cancelActiveRequest();
	void cancelSearch();
	void sendInlineRequest();
	void inlineResultsDone(
		const QString &query,
		const MTPmessages_BotResults &result);
	void rebuildRows();
	void refreshHeight();
	void updateTitleVisibility();
	void updateHovered();
	void clearHovered();
	void showContextMenu(int index, QPoint globalPosition);
	void clearHeavyItems();
	void pruneHeavyItems();
	void toggleSelected(DocumentData *document);
	[[nodiscard]] bool isSelected(DocumentData *document) const;
	[[nodiscard]] int findRowByLayout(
		const Overview::Layout::ItemBase *layout) const;
	[[nodiscard]] int findRowByItem(const HistoryItem *item) const;
	[[nodiscard]] int findRowByPoint(QPoint point, QPoint *relative) const;
	OwnedRow *prepareRow(
		CacheEntry &entry,
		std::shared_ptr<InlineBots::Result> result);
	[[nodiscard]] static bool SameDocumentKey(
		DocumentData *a,
		DocumentData *b);
	void maybeRequestMore();

	const not_null<Window::SessionController*> _controller;
	const not_null<PeerData*> _queryPeer;
	const QString _botUsername;
	MTP::Sender _api;
	object_ptr<Ui::VerticalLayout> _titleWrap;
	SelectedDocuments _selected;
	std::vector<Row> _rows;
	std::map<QString, std::unique_ptr<CacheEntry>> _cache;
	std::vector<const Overview::Layout::ItemBase*> _heavyLayouts;
	std::unique_ptr<StickerPremiumMark> _hiddenMark;
	base::unique_qptr<Ui::PopupMenu> _contextMenu;
	UserData *_bot = nullptr;
	QString _query;
	QString _nextQuery;
	QString _nextOffset;
	mtpRequestId _resolveRequestId = 0;
	mtpRequestId _requestId = 0;
	int _selectedLimit = MaxSelectedItems;
	int _titleHeight = 0;
	int _visibleTop = 0;
	int _visibleBottom = 0;
	int _hovered = -1;
	int _pressed = -1;
	QPoint _lastMousePos;
	bool _pending = false;
	bool _loading = false;
	bool _botLookupStarted = false;
	bool _inResize = false;
	bool _pagingEnabled = false;
	rpl::variable<bool> _awayFromTop = false;
	rpl::event_stream<SelectedDocuments> _selectedDocuments;
	rpl::event_stream<> _stateChanged;

};

MusicSectionController::MusicSectionController(
		not_null<Window::SessionController*> controller,
		SavedMusicSectionTag)
: AbstractController(controller)
, _key(Info::Saved::MusicTag{ controller->session().user() })
, _section(Info::Section::Type::SavedMusic) {
}

MusicSectionController::MusicSectionController(
		not_null<Window::SessionController*> controller,
		GlobalMediaSectionTag)
: AbstractController(controller)
, _key(Info::GlobalMedia::Tag{ controller->session().user(), true })
, _section(
	Storage::SharedMediaType::MusicFile,
	Info::Section::Type::GlobalMedia)
, _globalMedia(true) {
}

Info::Key MusicSectionController::key() const {
	return _key;
}

PeerData *MusicSectionController::migrated() const {
	return nullptr;
}

Info::Section MusicSectionController::section() const {
	return _section;
}

rpl::producer<QString> MusicSectionController::searchQueryValue() const {
	return _globalMedia ? _query.value() : rpl::single(QString());
}

void MusicSectionController::setQuery(QString query) {
	if (_globalMedia) {
		_query = std::move(query);
	}
}

HistoryMusicSection::HistoryMusicSection(
		QWidget *parent,
		std::unique_ptr<MusicSectionController> controller,
		rpl::producer<QString> title,
		int titleBottomSkip)
: RpWidget(parent)
, _controller(std::move(controller))
, _titleWrap(this)
, _list(this, _controller.get()) {
	_titleWrap->show();
	Ui::AddSubsectionTitle(
		_titleWrap.data(),
		std::move(title),
		MusicAttachSubsectionTitlePadding(0, titleBottomSkip));
	_list->show();
	_list->setGlobalMediaEmbeddedViewport();
	updatePreloadEnabled();
	updateTitleVisibility();

	_titleWrap->heightValue() | rpl::on_next([this] {
		refreshHeight();
	}, lifetime());
	_list->heightValue() | rpl::on_next([this] {
		if (_list->globalMediaSliceRefreshInProgress()) {
			return;
		}
		updateTitleVisibility();
		refreshHeight();
	}, lifetime());
	_list->globalMediaSliceViewValue() | rpl::on_next([this](
			const std::optional<Info::Media::GlobalMediaSliceView> &) {
		if (!_list->globalMediaSliceRefreshInProgress()) {
			refreshHeight();
		}
	}, lifetime());
}

not_null<Info::Media::ListWidget*> HistoryMusicSection::list() const {
	return _list.data();
}

rpl::producer<Info::SelectedItems> HistoryMusicSection::selectedListValue()
		const {
	return _list->selectedListValue();
}

bool HistoryMusicSection::hasResults() const {
	return _list->hasRows();
}

std::optional<int> HistoryMusicSection::fullCount() const {
	return _list->fullCount();
}

rpl::producer<std::optional<int>> HistoryMusicSection::fullCountValue() const {
	return _list->fullCountValue();
}

auto HistoryMusicSection::globalMediaSliceViewValue() const
-> rpl::producer<std::optional<Info::Media::GlobalMediaSliceView>> {
	return _list->globalMediaSliceViewValue();
}

void HistoryMusicSection::setCollapsed(bool collapsed) {
	if (_collapsed == collapsed) {
		return;
	}
	_collapsed = collapsed;
	updatePreloadEnabled();
	refreshHeight();
}

void HistoryMusicSection::setPagingEnabled(bool enabled) {
	if (_pagingEnabled == enabled) {
		return;
	}
	_pagingEnabled = enabled;
	updatePreloadEnabled();
}

void HistoryMusicSection::setExternalViewportHeight(int height) {
	_list->setExternalViewportHeight(height);
}

void HistoryMusicSection::setTitleEligible(bool eligible) {
	if (_titleEligible == eligible) {
		return;
	}
	_titleEligible = eligible;
	updateTitleVisibility();
	refreshHeight();
}

void HistoryMusicSection::setTopSkip(int topSkip) {
	if (_topSkip == topSkip) {
		return;
	}
	_topSkip = topSkip;
	refreshHeight();
}

int HistoryMusicSection::listHeightForCurrentMode() const {
	const auto height = _list->heightNoMargins();
	if (!_collapsed) {
		return height;
	}
	return std::min(height, _list->heightForFirstRows(kFirstLimit));
}

int HistoryMusicSection::resizeGetHeight(int newWidth) {
	_inResize = true;
	updateTitleVisibility();

	_titleWrap->resizeToWidth(newWidth);
	const auto titleHeight = _titleWrap->isHidden()
		? 0
		: _titleWrap->heightNoMargins();
	const auto topSkip = titleHeight ? _topSkip : 0;
	_titleWrap->moveToLeft(0, topSkip, newWidth);
	_list->resizeToWidth(newWidth);
	_list->moveToLeft(0, topSkip + titleHeight, newWidth);

	_inResize = false;
	return topSkip + titleHeight + listHeightForCurrentMode();
}

void HistoryMusicSection::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	setChildVisibleTopBottom(_list.data(), visibleTop, visibleBottom);
}

void HistoryMusicSection::updatePreloadEnabled() {
	_list->setPreloadEnabled(_pagingEnabled && !_collapsed);
}

void HistoryMusicSection::updateTitleVisibility() {
	_titleWrap->setVisible(_titleEligible && _list->hasRows());
}

void HistoryMusicSection::refreshHeight() {
	if (_inResize) {
		return;
	}
	updateTitleVisibility();
	resizeToWidth(width());
}

SearchEmptyWidget::SearchEmptyWidget(QWidget *parent)
: Info::Media::EmptyWidget(parent) {
	setType(Info::Media::Type::MusicFile);
	setSearchQuery(QString());
}

void SearchEmptyWidget::setQuery(QString query) {
	setSearchQuery(query);
}

GlobalMusicSearchSection::GlobalMusicSearchSection(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> queryPeer,
		rpl::producer<QString> title)
: RpWidget(parent)
, _controller(controller)
, _queryPeer(queryPeer)
, _botUsername(controller->session().appConfig().get<QString>(
	u"music_search_username"_q,
	QString()))
, _api(&controller->session().mtp())
, _titleWrap(this)
, _hiddenMark(std::make_unique<StickerPremiumMark>(
		&controller->session(),
		st::giftBoxHiddenMark,
		RectPart::Center)) {
	setMouseTracking(true);
	_titleWrap->show();
	Ui::AddSubsectionTitle(
		_titleWrap.data(),
		std::move(title),
		MusicAttachSubsectionTitlePadding(
			st::musicAttachGlobalSearchTopSkip));
	updateTitleVisibility();

	_titleWrap->heightValue() | rpl::on_next([this] {
		refreshHeight();
	}, lifetime());

	controller->session().data().itemRepaintRequest(
	) | rpl::on_next([=](not_null<const HistoryItem*> item) {
		const auto index = findRowByItem(item);
		if (index >= 0) {
			rtlupdate(QRect(0, _rows[index].top, width(), _rows[index].height));
		}
	}, lifetime());
	controller->session().data().itemDataChanges(
	) | rpl::on_next([=](not_null<HistoryItem*> item) {
		const auto index = findRowByItem(item);
		if (index < 0) {
			return;
		}
		_rows[index].owned->layout->itemDataChanged();
		rtlupdate(QRect(0, _rows[index].top, width(), _rows[index].height));
	}, lifetime());
}

GlobalMusicSearchSection::~GlobalMusicSearchSection() {
	clearHovered();
	if (_resolveRequestId) {
		_api.request(_resolveRequestId).cancel();
	}
	cancelActiveRequest();
}

bool GlobalMusicSearchSection::available() const {
	return !_botUsername.isEmpty();
}

void GlobalMusicSearchSection::setQuery(QString query) {
	if (!available() || query.isEmpty()) {
		if (!_query.isEmpty()
			|| !_nextQuery.isEmpty()
			|| _pending
			|| _loading) {
			cancelSearch();
		}
		return;
	} else if (_nextQuery != query) {
		_awayFromTop = false;
		setLoading(false);
		cancelActiveRequest();
		if (const auto i = _cache.find(query); i != end(_cache)) {
			setPending(false);
			_query = _nextQuery = std::move(query);
			rebuildRows();
		} else {
			_nextQuery = std::move(query);
			_nextOffset = QString();
			setPending(true);
			if (_bot) {
				sendInlineRequest();
			}
		}
	}
	if (!_bot && !_botLookupStarted) {
		resolveBot();
	}
}

void GlobalMusicSearchSection::setSelectedLimit(int limit) {
	_selectedLimit = std::max(limit, 0);
}

void GlobalMusicSearchSection::setPagingEnabled(bool enabled) {
	if (_pagingEnabled == enabled) {
		return;
	}
	_pagingEnabled = enabled;
	if (_pagingEnabled) {
		maybeRequestMore();
	}
}

bool GlobalMusicSearchSection::hasResults() const {
	return !_rows.empty();
}

bool GlobalMusicSearchSection::busy() const {
	return _pending || _loading;
}

bool GlobalMusicSearchSection::awayFromTop() const {
	return _awayFromTop.current();
}

rpl::producer<bool> GlobalMusicSearchSection::awayFromTopValue() const {
	return _awayFromTop.value();
}

rpl::producer<GlobalMusicSearchSection::SelectedDocuments>
GlobalMusicSearchSection::selectedDocumentsValue() const {
	return _selectedDocuments.events_starting_with(SelectedDocuments(_selected));
}

rpl::producer<> GlobalMusicSearchSection::stateChanged() const {
	return _stateChanged.events();
}

int GlobalMusicSearchSection::resizeGetHeight(int newWidth) {
	_inResize = true;

	_titleWrap->resizeToWidth(newWidth);
	_titleWrap->moveToLeft(0, 0, newWidth);
	_titleHeight = _rows.empty() ? 0 : _titleWrap->heightNoMargins();

	auto top = _titleHeight;
	for (auto &row : _rows) {
		row.top = top;
		row.height = row.owned->layout->resizeGetHeight(newWidth);
		top += row.height;
	}

	_inResize = false;
	return top;
}

void GlobalMusicSearchSection::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;
	_awayFromTop = (visibleTop > std::max(visibleBottom - visibleTop, 0));
	pruneHeavyItems();
	maybeRequestMore();
}

void GlobalMusicSearchSection::paintEvent(QPaintEvent *e) {
	Painter p(this);
	const auto clip = e->rect();
	const auto paused = _controller->isGifPausedAtLeastFor(
		Window::GifPauseReason::Layer);
	const auto context = Overview::Layout::PaintContext(
		crl::now(),
		!_selected.empty(),
		paused);
	for (auto i = 0; i != int(_rows.size()); ++i) {
		const auto &row = _rows[i];
		const auto geometry = QRect(0, row.top, width(), row.height);
		if (!clip.intersects(geometry)) {
			continue;
		}
		p.save();
		p.translate(0, row.top);
		row.owned->layout->paint(
			p,
			clip.translated(0, -row.top),
			isSelected(row.owned->document)
				? FullSelection
				: TextSelection(),
			&context);
		p.restore();
	}
}

void GlobalMusicSearchSection::mousePressEvent(QMouseEvent *e) {
	if (_contextMenu) {
		e->accept();
		return;
	}
	if (e->button() != Qt::LeftButton) {
		return;
	}
	_lastMousePos = e->globalPos();
	updateHovered();
	_pressed = _hovered;
	ClickHandler::pressed();
}

void GlobalMusicSearchSection::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = std::exchange(_pressed, -1);
	const auto activated = ClickHandler::unpressed();
	_lastMousePos = e->globalPos();
	updateHovered();
	if (pressed < 0 || _hovered != pressed || pressed >= int(_rows.size())) {
		return;
	}
	const auto point = mapFromGlobal(_lastMousePos);
	QPoint relative;
	if (findRowByPoint(point, &relative) != pressed) {
		return;
	}
	const auto sx = rtl() ? (width() - point.x()) : point.x();
	if (_rows[pressed].owned->layout->selectionConsumesClick({
			sx,
			relative.y(),
		})) {
		toggleSelected(_rows[pressed].owned->document);
	} else if (activated) {
		ActivateClickHandler(window(), activated, {
			e->button(),
			QVariant::fromValue(ClickHandlerContext{
				.sessionWindow = base::make_weak(_controller),
			})
		});
	}
}

void GlobalMusicSearchSection::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateHovered();
}

void GlobalMusicSearchSection::contextMenuEvent(QContextMenuEvent *e) {
	_contextMenu = nullptr;
	_pressed = -1;
	ClickHandler::unpressed();
	if (e->reason() == QContextMenuEvent::Mouse) {
		_lastMousePos = e->globalPos();
		updateHovered();
	}
	if (_hovered >= 0 && _hovered < int(_rows.size())) {
		showContextMenu(_hovered, e->globalPos());
		e->accept();
	}
}

void GlobalMusicSearchSection::leaveEventHook(QEvent *) {
	clearHovered();
}

void GlobalMusicSearchSection::registerHeavyItem(
		not_null<const Overview::Layout::ItemBase*> item) {
	if (std::find(_heavyLayouts.begin(), _heavyLayouts.end(), item.get())
		== end(_heavyLayouts)) {
		_heavyLayouts.push_back(item.get());
	}
}

void GlobalMusicSearchSection::unregisterHeavyItem(
		not_null<const Overview::Layout::ItemBase*> item) {
	const auto i = std::find(_heavyLayouts.begin(), _heavyLayouts.end(), item.get());
	if (i != end(_heavyLayouts)) {
		_heavyLayouts.erase(i);
	}
}

void GlobalMusicSearchSection::repaintItem(
		not_null<const Overview::Layout::ItemBase*> item) {
	const auto index = findRowByLayout(item.get());
	if (index >= 0) {
		rtlupdate(QRect(0, _rows[index].top, width(), _rows[index].height));
	}
}

bool GlobalMusicSearchSection::itemVisible(
		not_null<const Overview::Layout::ItemBase*> item) {
	const auto index = findRowByLayout(item.get());
	if (index < 0 || isHidden()) {
		return false;
	}
	const auto &row = _rows[index];
	return (row.top < _visibleBottom)
		&& (row.top + row.height > _visibleTop);
}

not_null<StickerPremiumMark*> GlobalMusicSearchSection::hiddenMark() {
	return _hiddenMark.get();
}

void GlobalMusicSearchSection::openPhoto(
		not_null<PhotoData*> photo,
		FullMsgId id) {
	_controller->openPhoto(photo, { id });
}

void GlobalMusicSearchSection::openDocument(
		not_null<DocumentData*> document,
		FullMsgId id,
		bool showInMediaView) {
	_controller->openDocument(document, showInMediaView, { id });
}

void GlobalMusicSearchSection::setPending(bool pending) {
	if (_pending == pending) {
		return;
	}
	_pending = pending;
	_stateChanged.fire({});
}

void GlobalMusicSearchSection::setLoading(bool loading) {
	if (_loading == loading) {
		return;
	}
	_loading = loading;
	_stateChanged.fire({});
}

void GlobalMusicSearchSection::resolveBot() {
	if (_botLookupStarted || !available()) {
		return;
	}
	_botLookupStarted = true;
	if (const auto peer = _controller->session().data().peerByUsername(
			_botUsername)) {
		_bot = peer->asUser();
		applyResolvedBot();
		return;
	}
	_resolveRequestId = _api.request(MTPcontacts_ResolveUsername(
		MTP_flags(0),
		MTP_string(_botUsername),
		MTP_string()
	)).done(crl::guard(this, [this](
			const MTPcontacts_ResolvedPeer &result) {
		_resolveRequestId = 0;
		result.match([&](const MTPDcontacts_resolvedPeer &data) {
			_controller->session().data().processUsers(data.vusers());
			_controller->session().data().processChats(data.vchats());
			const auto peer = _controller->session().data().peerLoaded(
				peerFromMTP(data.vpeer()));
			_bot = peer ? peer->asUser() : nullptr;
		});
		applyResolvedBot();
	})).fail(crl::guard(this, [this](const MTP::Error &) {
		_resolveRequestId = 0;
		setPending(false);
		setLoading(false);
		if (_query != _nextQuery) {
			_query = _nextQuery;
			rebuildRows();
		}
	})).send();
}

void GlobalMusicSearchSection::applyResolvedBot() {
	if (_bot
		&& !_nextQuery.isEmpty()
		&& (_cache.find(_nextQuery) == end(_cache))) {
		sendInlineRequest();
	} else if (!_bot && _pending) {
		setPending(false);
		if (_query != _nextQuery) {
			_query = _nextQuery;
			rebuildRows();
			return;
		}
	}
	_stateChanged.fire({});
}

void GlobalMusicSearchSection::cancelActiveRequest() {
	if (_requestId) {
		_api.request(base::take(_requestId)).cancel();
	}
}

void GlobalMusicSearchSection::cancelSearch() {
	_awayFromTop = false;
	setPending(false);
	setLoading(false);
	cancelActiveRequest();
	_query = QString();
	_nextQuery = QString();
	_nextOffset = QString();
	rebuildRows();
}

void GlobalMusicSearchSection::sendInlineRequest() {
	if (_requestId || !_bot || _nextQuery.isEmpty()) {
		return;
	}
	setPending(false);

	const auto query = _nextQuery;

	auto nextOffset = QString();
	if (const auto i = _cache.find(query); i != end(_cache)) {
		nextOffset = i->second->nextOffset;
		if (nextOffset.isEmpty()) {
			setLoading(false);
			if (_query != query) {
				_query = query;
				rebuildRows();
			}
			return;
		}
	}

	setLoading(true);
	_requestId = _api.request(MTPmessages_GetInlineBotResults(
		MTP_flags(0),
		_bot->inputUser(),
		_queryPeer->input(),
		MTPInputGeoPoint(),
		MTP_string(query),
		MTP_string(nextOffset)
	)).done(crl::guard(this, [=](
			const MTPmessages_BotResults &result) {
		inlineResultsDone(query, result);
	})).fail(crl::guard(this, [=](const MTP::Error &) {
		_requestId = 0;
		setLoading(false);
		if ((_nextQuery == query) && (_query != query)) {
			_query = query;
			rebuildRows();
		}
	})).handleAllErrors().send();
}

void GlobalMusicSearchSection::inlineResultsDone(
		const QString &query,
		const MTPmessages_BotResults &result) {
	setLoading(false);
	_requestId = 0;

	auto it = _cache.find(query);
	const auto adding = (it != end(_cache));
	if (result.type() == mtpc_messages_botResults) {
		const auto &data = result.c_messages_botResults();
		_controller->session().data().processUsers(data.vusers());

		if (it == end(_cache)) {
			it = _cache.emplace(query, std::make_unique<CacheEntry>()).first;
		}
		auto &entry = *it->second;
		entry.nextOffset = qs(data.vnext_offset().value_or_empty());

		auto accepted = false;
		const auto queryId = data.vquery_id().v;
		for (const auto &item : data.vresults().v) {
			const auto parsed = InlineBots::Result::Create(
				&_controller->session(),
				queryId,
				item);
			const auto document = parsed ? parsed->document() : nullptr;
			if (!document
				|| !document->isSong()
				|| !parsed->contentUrl().isEmpty()) {
				continue;
			}
			prepareRow(entry, parsed);
			accepted = true;
		}
		const auto consumeFilteredPage = !accepted && !entry.nextOffset.isEmpty();
		if (consumeFilteredPage && (_nextQuery == query)) {
			if (_query == query) {
				rebuildRows();
			}
			sendInlineRequest();
			return;
		}
		_query = query;
		rebuildRows();
	} else if (adding) {
		it->second->nextOffset = QString();
		_query = query;
		rebuildRows();
	} else {
		_query = query;
		rebuildRows();
	}
	maybeRequestMore();
}

void GlobalMusicSearchSection::rebuildRows() {
	clearHovered();
	_rows.clear();

	if (!_query.isEmpty()) {
		if (const auto i = _cache.find(_query); i != end(_cache)) {
			_nextOffset = i->second->nextOffset;
			for (const auto &row : i->second->rows) {
				_rows.push_back({ row.get() });
			}
		} else {
			_nextOffset = QString();
		}
	} else {
		_nextOffset = QString();
	}

	updateTitleVisibility();
	refreshHeight();
	_stateChanged.fire({});
}

GlobalMusicSearchSection::OwnedRow *GlobalMusicSearchSection::prepareRow(
		CacheEntry &entry,
		std::shared_ptr<InlineBots::Result> result) {
	const auto document = result->document();
	const auto history = _controller->session().data().history(
		_controller->session().user());
	auto item = std::unique_ptr<HistoryItem, HistoryItem::Destroyer>(
		result->makeMessage(history, {
			.id = history->nextNonHistoryEntryId(),
			.flags = (MessageFlag::FakeHistoryItem | MessageFlag::HasFromId),
			.from = history->peer->id,
			.date = base::unixtime::now(),
			.viaBotId = _bot ? peerToUser(_bot->id) : UserId(0),
		}));
	const auto delegate = static_cast<Overview::Layout::Delegate*>(this);
	auto layout = std::make_unique<Overview::Layout::Document>(
		delegate,
		item.get(),
		Overview::Layout::DocumentFields{ .document = document },
		st::overviewFileLayout);
	layout->initDimensions();

	auto owned = std::make_unique<OwnedRow>();
	owned->document = document;
	owned->item = std::move(item);
	owned->layout = std::move(layout);
	entry.rows.push_back(std::move(owned));
	return entry.rows.back().get();
}

void GlobalMusicSearchSection::refreshHeight() {
	if (_inResize) {
		return;
	}
	updateTitleVisibility();
	resizeToWidth(width());
	pruneHeavyItems();
	update();
}

void GlobalMusicSearchSection::updateTitleVisibility() {
	_titleWrap->setVisible(!_rows.empty());
}

void GlobalMusicSearchSection::updateHovered() {
	if (_pressed >= 0) {
		return;
	}
	const auto point = mapFromGlobal(_lastMousePos);
	QPoint relative;
	const auto hovered = findRowByPoint(point, &relative);
	const auto layout = (hovered >= 0 && hovered < int(_rows.size()))
		? _rows[hovered].owned->layout.get()
		: nullptr;
	const auto sx = rtl() ? (width() - point.x()) : point.x();
	const auto link = layout
		? layout->getState({ sx, relative.y() }, HistoryView::StateRequest())
			.link
		: ClickHandlerPtr();
	_hovered = hovered;
	if (ClickHandler::setActive(link, layout)) {
		setCursor(link ? style::cur_pointer : style::cur_default);
	}
}

void GlobalMusicSearchSection::clearHovered() {
	if (_hovered >= 0 && _hovered < int(_rows.size())) {
		ClickHandler::clearActive(_rows[_hovered].owned->layout.get());
	}
	_hovered = -1;
	_pressed = -1;
	setCursor(style::cur_default);
}

void GlobalMusicSearchSection::showContextMenu(
		int index,
		QPoint globalPosition) {
	const auto owned = _rows[index].owned;
	const auto document = owned->document;
	const auto item = owned->item.get();
	if (!document || !item) {
		return;
	}
	_contextMenu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	if (document->loading()) {
		_contextMenu->addAction(
			tr::lng_context_cancel_download(tr::now),
			[document] {
				document->cancel();
			},
			&st::menuIconCancel);
	} else {
		const auto filepath = document->filepath(true);
		if (!filepath.isEmpty()) {
			_contextMenu->addAction(
				(Platform::IsMac()
					? tr::lng_context_show_in_finder(tr::now)
					: tr::lng_context_show_in_folder(tr::now)),
				base::fn_delayed(
					st::defaultDropdownMenu.menu.ripple.hideDuration,
					this,
					[filepath] {
						File::ShowInFolder(filepath);
					}),
				&st::menuIconShowInFolder);
		}
		HistoryView::AddSaveDocumentAction(
			Ui::Menu::CreateAddActionCallback(_contextMenu),
			item,
			document,
			_controller);
	}
	const auto selected = isSelected(document);
	_contextMenu->addAction(
		(selected
			? tr::lng_context_deselect_msg
			: tr::lng_context_select_msg)(tr::now),
		crl::guard(this, [=] {
			toggleSelected(document);
		}),
		&st::menuIconSelect);
	if (_contextMenu->empty()) {
		_contextMenu = nullptr;
		return;
	}
	_contextMenu->setDestroyedCallback(crl::guard(this, [=] {
		_lastMousePos = QCursor::pos();
		updateHovered();
	}));
	_contextMenu->popup(globalPosition);
}

void GlobalMusicSearchSection::clearHeavyItems() {
	const auto visibleHeight = _visibleBottom - _visibleTop;
	if (visibleHeight <= 0) {
		return;
	}
	const auto above = _visibleTop - visibleHeight;
	const auto below = _visibleBottom + visibleHeight;
	for (auto i = _heavyLayouts.begin(); i != end(_heavyLayouts);) {
		const auto index = findRowByLayout(*i);
		if (index < 0) {
			const_cast<Overview::Layout::ItemBase*>(*i)->clearHeavyPart();
			i = _heavyLayouts.erase(i);
			continue;
		}
		const auto &row = _rows[index];
		if (row.top + row.height <= above || row.top >= below) {
			const_cast<Overview::Layout::ItemBase*>(*i)->clearHeavyPart();
			i = _heavyLayouts.erase(i);
		} else {
			++i;
		}
	}
}

void GlobalMusicSearchSection::pruneHeavyItems() {
	for (auto i = _heavyLayouts.begin(); i != end(_heavyLayouts);) {
		if (findRowByLayout(*i) < 0) {
			const_cast<Overview::Layout::ItemBase*>(*i)->clearHeavyPart();
			i = _heavyLayouts.erase(i);
		} else {
			++i;
		}
	}
	clearHeavyItems();
}

void GlobalMusicSearchSection::toggleSelected(DocumentData *document) {
	if (!document) {
		return;
	}
	const auto i = std::find_if(
		_selected.begin(),
		_selected.end(),
		[=](DocumentData *selected) {
			return SameDocumentKey(selected, document);
		});
	if (i != end(_selected)) {
		_selected.erase(i);
	} else if (_selected.size() < _selectedLimit) {
		_selected.push_back(document);
	} else {
		return;
	}
	update();
	_selectedDocuments.fire(SelectedDocuments(_selected));
}

bool GlobalMusicSearchSection::isSelected(DocumentData *document) const {
	return std::find_if(
		_selected.begin(),
		_selected.end(),
		[=](DocumentData *selected) {
			return SameDocumentKey(selected, document);
		}) != end(_selected);
}

int GlobalMusicSearchSection::findRowByLayout(
		const Overview::Layout::ItemBase *layout) const {
	for (auto i = 0; i != int(_rows.size()); ++i) {
		if (_rows[i].owned->layout.get() == layout) {
			return i;
		}
	}
	return -1;
}

int GlobalMusicSearchSection::findRowByItem(const HistoryItem *item) const {
	for (auto i = 0; i != int(_rows.size()); ++i) {
		if (_rows[i].owned->item.get() == item) {
			return i;
		}
	}
	return -1;
}

int GlobalMusicSearchSection::findRowByPoint(
		QPoint point,
		QPoint *relative) const {
	if (point.y() < _titleHeight) {
		return -1;
	}
	for (auto i = 0; i != int(_rows.size()); ++i) {
		const auto &row = _rows[i];
		const auto geometry = QRect(0, row.top, width(), row.height);
		if (!geometry.contains(point)) {
			continue;
		}
		if (relative) {
			*relative = point - geometry.topLeft();
		}
		return i;
	}
	return -1;
}

void GlobalMusicSearchSection::maybeRequestMore() {
	if (!_pagingEnabled
		|| _pending
		|| _requestId
		|| (_nextQuery != _query)
		|| _query.isEmpty()
		|| _nextOffset.isEmpty()) {
		return;
	}
	const auto contentHeight = std::max(height() - _titleHeight, 0);
	if (contentHeight <= 0) {
		return;
	}
	const auto visibleTop = std::clamp(
		_visibleTop - _titleHeight,
		0,
		contentHeight);
	const auto visibleBottom = std::clamp(
		_visibleBottom - _titleHeight,
		0,
		contentHeight);
	const auto visibleHeight = std::max(visibleBottom - visibleTop, 0);
	if (visibleBottom + visibleHeight > contentHeight) {
		_nextQuery = _query;
		sendInlineRequest();
	}
}

struct ResolvedHistorySelection {
	not_null<HistoryItem*> item;
	not_null<DocumentData*> document;
};

struct SelectionKey {
	DocumentId documentId = 0;
	DocumentData *document = nullptr;
};

[[nodiscard]] bool operator==(
		const SelectionKey &a,
		const SelectionKey &b) {
	return (a.documentId == b.documentId)
		&& (a.document == b.document);
}

[[nodiscard]] SelectionKey MakeSelectionKey(DocumentData *document) {
	return (document && (document->id != 0))
		? SelectionKey{ document->id, nullptr }
		: SelectionKey{ 0, document };
}

[[nodiscard]] bool HasSelectionKey(
		const std::vector<SelectionKey> &keys,
		const SelectionKey &key) {
	return std::find(keys.begin(), keys.end(), key) != end(keys);
}

bool GlobalMusicSearchSection::SameDocumentKey(
		DocumentData *a,
		DocumentData *b) {
	return (a && b)
		? (MakeSelectionKey(a) == MakeSelectionKey(b))
		: (a == b);
}

[[nodiscard]] HistoryItem *LookupSavedMusicItem(
		not_null<Main::Session*> session,
		const Info::SelectedItem &selected) {
	const auto &saved = session->data().savedMusic().list(session->user()->id);
	const auto i = std::find_if(saved.begin(), saved.end(), [&](auto item) {
		return item->globalId() == selected.globalId;
	});
	if (i != saved.end()) {
		return *i;
	}
	return nullptr;
}

[[nodiscard]] std::optional<ResolvedHistorySelection> ResolveSelection(
		not_null<Main::Session*> session,
		const Info::SelectedItem &selected) {
	auto item = MessageByGlobalId(selected.globalId);
	if (!item) {
		item = LookupSavedMusicItem(session, selected);
	}
	const auto document = item
		&& item->media()
		? item->media()->document()
		: nullptr;
	if (!item || !document) {
		return std::nullopt;
	}
	return ResolvedHistorySelection{
		item,
		document,
	};
}

void ApplySendOptions(
		Api::SendOptions &base,
		const Api::SendOptions &options) {
	const auto empty = Api::SendOptions();
	if (options.price != empty.price) {
		base.price = options.price;
	}
	if (options.sendAs != empty.sendAs) {
		base.sendAs = options.sendAs;
	}
	if (options.scheduled != empty.scheduled) {
		base.scheduled = options.scheduled;
	}
	if (options.scheduleRepeatPeriod != empty.scheduleRepeatPeriod) {
		base.scheduleRepeatPeriod = options.scheduleRepeatPeriod;
	}
	if (options.shortcutId != empty.shortcutId) {
		base.shortcutId = options.shortcutId;
	}
	if (options.effectId != empty.effectId) {
		base.effectId = options.effectId;
	}
	if (options.stakeSeedHash != empty.stakeSeedHash) {
		base.stakeSeedHash = options.stakeSeedHash;
	}
	if (options.stakeNanoTon != empty.stakeNanoTon) {
		base.stakeNanoTon = options.stakeNanoTon;
	}
	if (options.starsApproved != empty.starsApproved) {
		base.starsApproved = options.starsApproved;
	}
	if (options.silent != empty.silent) {
		base.silent = options.silent;
	}
	if (options.handleSupportSwitch != empty.handleSupportSwitch) {
		base.handleSupportSwitch = options.handleSupportSwitch;
	}
	if (options.invertCaption != empty.invertCaption) {
		base.invertCaption = options.invertCaption;
	}
	if (options.hideViaBot != empty.hideViaBot) {
		base.hideViaBot = options.hideViaBot;
	}
	if (options.mediaSpoiler != empty.mediaSpoiler) {
		base.mediaSpoiler = options.mediaSpoiler;
	}
	if (options.ttlSeconds != empty.ttlSeconds) {
		base.ttlSeconds = options.ttlSeconds;
	}
	if (options.suggest != empty.suggest) {
		base.suggest = options.suggest;
	}
}

[[nodiscard]] Api::SendType CurrentSendType(const Api::SendAction &action) {
	if (!action.options.scheduled) {
		return Api::SendType::Normal;
	}
	return (action.options.scheduled == Api::kScheduledUntilOnlineTimestamp)
		? Api::SendType::ScheduledToUser
		: Api::SendType::Scheduled;
}

void PushPreparedMusicFile(
		Ui::PreparedList &list,
		Ui::PreparedFile &&file) {
	if (file.type != Ui::PreparedFile::Type::Music) {
		return;
	}
	if (list.files.size() >= Ui::MaxAlbumItems()) {
		list.filesToProcess.push_back(std::move(file));
	} else {
		list.files.push_back(std::move(file));
	}
}

[[nodiscard]] Ui::PreparedList FilterMusicPreparedList(
		Ui::PreparedList &&list,
		int previewWidth,
		bool premium) {
	auto result = Ui::PreparedList(list.error, list.errorData);
	result.overrideSendImagesAsPhotos = list.overrideSendImagesAsPhotos;
	for (auto &file : list.files) {
		PushPreparedMusicFile(result, std::move(file));
	}
	while (!list.filesToProcess.empty()) {
		auto file = std::move(list.filesToProcess.front());
		list.filesToProcess.pop_front();
		if (file.path.isEmpty()) {
			continue;
		}
		auto prepared = Storage::PrepareMediaList(
			QStringList{ file.path },
			previewWidth,
			premium);
		if (prepared.error != Ui::PreparedList::Error::None) {
			return prepared;
		}
		for (auto &preparedFile : prepared.files) {
			PushPreparedMusicFile(result, std::move(preparedFile));
		}
		while (!prepared.filesToProcess.empty()) {
			list.filesToProcess.push_back(
				std::move(prepared.filesToProcess.front()));
			prepared.filesToProcess.pop_front();
		}
	}
	return result;
}

[[nodiscard]] std::vector<Api::MusicSelectionItem> MakeMusicSelectionItems(
		not_null<Main::Session*> session,
		const Info::SelectedItems &savedMusic,
		const Info::SelectedItems &yourChats,
		const GlobalMusicSearchSection::SelectedDocuments &globalDocuments) {
	auto result = std::vector<Api::MusicSelectionItem>();
	auto keys = std::vector<SelectionKey>();
	const auto appendHistory = [&](const Info::SelectedItems &items) {
		for (const auto &selected : items.list) {
			const auto resolved = ResolveSelection(session, selected);
			if (!resolved) {
				continue;
			}
			const auto key = MakeSelectionKey(resolved->document);
			if (HasSelectionKey(keys, key)) {
				continue;
			}
			keys.push_back(key);
			result.push_back({
				.document = resolved->document,
				.origin = Data::FileOriginMessage(resolved->item->fullId()),
			});
		}
	};
	appendHistory(savedMusic);
	appendHistory(yourChats);
	for (const auto document : globalDocuments) {
		const auto key = MakeSelectionKey(document);
		if (HasSelectionKey(keys, key)) {
			continue;
		}
		keys.push_back(key);
		result.push_back({
			.document = document,
			.origin = Data::FileOrigin(),
		});
	}
	return result;
}

} // namespace

void MusicAttachBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		Fn<Api::SendAction()> actionFactory) {
	box->setTitle(tr::lng_all_music());
	box->setWidth(st::boxWideWidth);
	box->setMinHeight(st::boxMaxListHeight);
	box->setMaxHeight(st::boxMaxListHeight);

	const auto top = box->setPinnedToTopContent(
		object_ptr<Ui::VerticalLayout>(box));
	const auto search = top->add(
		object_ptr<Ui::SlideWrap<Ui::MultiSelect>>(
			top,
			object_ptr<Ui::MultiSelect>(
				top,
				st::defaultMultiSelect,
				tr::lng_participant_filter())));
	search->show(anim::type::instant);
	const auto show = controller->uiShow();

	const auto bottom = box->setPinnedToBottomContent(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			box,
			object_ptr<Ui::VerticalLayout>(box)));
	const auto bottomContent = bottom->entity();
	const auto captionWrap = bottomContent->add(
		object_ptr<Ui::RpWidget>(bottomContent),
		st::shareCommentPadding);
	const auto caption = Ui::CreateChild<Ui::InputField>(
		captionWrap,
		st::shareComment,
		Ui::InputField::Mode::MultiLine,
		tr::lng_photo_caption());
	if (show && show->valid()) {
		InitMessageFieldHandlers({
			.session = &controller->session(),
			.show = Main::MakeSessionShow(show, &controller->session()),
			.field = caption,
			.fieldStyle = &st::shareComment,
		});
	}
	caption->setSubmitSettings(Core::App().settings().sendSubmitWay());
	const auto captionSideSkip = st::overviewFileLayout.songPadding.left();
	caption->setAdditionalMargins({
		captionSideSkip
			- st::shareCommentPadding.left()
			- st::shareComment.textMargins.left(),
		0,
		captionSideSkip
			- st::shareCommentPadding.right()
			- st::shareComment.textMargins.right(),
		0,
	});
	Ui::ResizeFitChild(captionWrap, caption);
	bottom->hide(anim::type::instant);

	const auto content = box->verticalLayout();
	const auto browseTop = content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			content,
			object_ptr<Ui::VerticalLayout>(content)));
	browseTop->setDuration(0);
	const auto browseTopLayout = browseTop->entity();
	const auto chooseFromFiles = browseTopLayout->add(
		object_ptr<Ui::SettingsButton>(
			browseTopLayout,
			tr::lng_music_attach_choose_from_files(),
			st::musicAttachChooseFromFilesButton),
		{ 0, st::membersMarginTop, 0, st::membersMarginTop });
	const auto chooseFromFilesPadding = chooseFromFiles->st().padding;
	const auto songIconCenter = st::overviewFileLayout.songPadding.left()
		+ (st::overviewFileLayout.songThumbSize / 2);
	const auto songTextLeft = st::overviewFileLayout.songPadding.left()
		+ st::overviewFileLayout.songThumbSize
		+ st::overviewFileLayout.songPadding.right();
	chooseFromFiles->setPaddingOverride(style::margins(
		songTextLeft,
		chooseFromFilesPadding.top(),
		chooseFromFilesPadding.right(),
		chooseFromFilesPadding.bottom()));
	const auto chooseFromFilesIcon = Ui::CreateChild<Info::Profile::FloatingIcon>(
		chooseFromFiles,
		st::musicAttachChooseFromFilesIcon,
		QPoint());
	chooseFromFiles->heightValue() | rpl::on_next([=](int height) {
		chooseFromFilesIcon->moveToLeft(
			songIconCenter - (st::musicAttachChooseFromFilesIcon.width() / 2),
			(height - st::musicAttachChooseFromFilesIcon.height()) / 2);
	}, chooseFromFilesIcon->lifetime());
	const auto addShowAllButton = [&](not_null<Ui::VerticalLayout*> parent) {
		const auto wrap = parent->add(
			object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
				parent,
				object_ptr<Ui::SettingsButton>(
					parent,
					tr::lng_recent_frequent_all(),
					st::musicAttachShowAllButton)));
		const auto button = wrap->entity();
		const auto padding = button->st().padding;
		button->setPaddingOverride(style::margins(
			songTextLeft,
			padding.top(),
			padding.right(),
			padding.bottom()));
		const auto icon = Ui::CreateChild<Info::Profile::FloatingIcon>(
			button,
			st::musicAttachShowAllIcon,
			QPoint());
		button->heightValue() | rpl::on_next([=](int height) {
			icon->moveToLeft(
				songIconCenter - (st::musicAttachShowAllIcon.width() / 2),
				(height - st::musicAttachShowAllIcon.height()) / 2);
		}, icon->lifetime());
		wrap->setDuration(0);
		wrap->toggle(false, anim::type::instant);
		return wrap;
	};

	auto savedMusicController = std::make_unique<MusicSectionController>(
		controller,
		MusicSectionController::SavedMusicSectionTag());
	const auto savedMusicBlock = browseTopLayout->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			browseTopLayout,
			object_ptr<Ui::VerticalLayout>(browseTopLayout)));
	savedMusicBlock->setDuration(0);
	const auto savedMusicBlockLayout = savedMusicBlock->entity();
	const auto savedMusic = savedMusicBlockLayout->add(
		object_ptr<HistoryMusicSection>(
			savedMusicBlockLayout,
			std::move(savedMusicController),
			tr::lng_music_attach_saved_music()));
	const auto savedMusicShowAllWrap = addShowAllButton(
		savedMusicBlockLayout);

	auto yourChatsController = std::make_unique<MusicSectionController>(
		controller,
		MusicSectionController::GlobalMediaSectionTag());
	const auto yourChatsControllerRaw = yourChatsController.get();
	const auto yourChatsBlock = content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			content,
			object_ptr<Ui::VerticalLayout>(content)));
	yourChatsBlock->setDuration(0);
	yourChatsBlock->toggle(true, anim::type::instant);
	const auto yourChatsBlockLayout = yourChatsBlock->entity();
	const auto yourChats = yourChatsBlockLayout->add(
		object_ptr<HistoryMusicSection>(
			yourChatsBlockLayout,
			std::move(yourChatsController),
			tr::lng_reply_in_chats_list(),
			-st::infoMediaMargin.top()));
	const auto yourChatsShowAllWrap = addShowAllButton(
		yourChatsBlockLayout);
	const auto globalSearchWrap = content->add(
		object_ptr<Ui::SlideWrap<GlobalMusicSearchSection>>(
			content,
			object_ptr<GlobalMusicSearchSection>(
				content,
				controller,
				peer,
				tr::lng_music_attach_global_search())));
	globalSearchWrap->setDuration(0);
	globalSearchWrap->toggle(false, anim::type::instant);
	const auto globalSearch = globalSearchWrap->entity();
	const auto searchEmptyWrap = content->add(
		object_ptr<Ui::SlideWrap<SearchEmptyWidget>>(
			content,
			object_ptr<SearchEmptyWidget>(content)));
	searchEmptyWrap->setDuration(0);
	searchEmptyWrap->toggle(false, anim::type::instant);
	const auto searchEmpty = searchEmptyWrap->entity();
	searchEmpty->setFullHeight(rpl::combine(
		box->heightValue(),
		search->heightValue(),
		bottom->heightValue(),
		searchEmptyWrap->topValue()
	) | rpl::map([](
			int boxHeight,
			int searchHeight,
			int bottomHeight,
			int top) {
		return std::max(boxHeight - searchHeight - bottomHeight - top, 0);
	}));
	for (const auto section : { savedMusic, yourChats }) {
		section->list()->setSelectOnClick(true);
		section->list()->setSelectedLimit(MaxSelectedItems);
	}
	globalSearch->setSelectedLimit(MaxSelectedItems);

	struct ViewportAnchor {
		enum class Kind : uchar {
			Raw,
			Widget,
			ChatsRow,
			ListOffset,
			GlobalSearch,
		};

		Kind kind = Kind::Raw;
		int scrollTop = 0;
		int offset = 0;
		bool inBrowseTop = false;
		bool inChatsList = false;
		QPointer<QWidget> widget;
		QString query;
		Data::MessagePosition position;
	};
	struct GeometryState {
		std::optional<Info::Media::GlobalMediaSliceView> acceptedView;
		std::optional<ViewportAnchor> pendingViewportAnchor;
		Fn<void()> update;
		uint64 authorityGenerationFloor = 0;
		std::optional<int> pendingChatsScrollTop;
		bool pendingChatsScrollReady = false;
		bool pendingChatsScrollScheduled = false;
		bool topAuthority = true;
		bool updating = false;
		bool pending = false;
		bool searchMode = false;
	};
	struct State {
		Info::SelectedItems savedSelection;
		Info::SelectedItems yourChatsSelection;
		GlobalMusicSearchSection::SelectedDocuments globalSelection;
		std::vector<Api::MusicSelectionItem> selection;
		QString typedSearchQuery;
		QString committedSearchQuery;
		QTimer searchQueryCommitTimer;
		QString yourChatsQuery;
		std::optional<int> yourChatsFullCount;
		std::optional<QString> yourChatsFullCountQuery;
		GeometryState geometry;
		SendPaymentHelper paymentHelper;
		Fn<void(
			std::shared_ptr<Ui::PreparedBundle>,
			Api::SendOptions,
			FullReplyTo)> sendPreparedMusic;
		Fn<void(Api::SendOptions)> sendSelected;
		bool savedMusicExpanded = false;
		bool yourChatsExpanded = false;
	};
	const auto state = box->lifetime().make_state<State>();
	state->searchQueryCommitTimer.setSingleShot(true);

	state->sendPreparedMusic = [=](
			std::shared_ptr<Ui::PreparedBundle> bundle,
			Api::SendOptions options,
			FullReplyTo replyTo) {
		if (!bundle || Data::ShowSendError(show, peer, *bundle)) {
			return;
		}

		auto action = actionFactory();
		ApplySendOptions(action.options, options);
		action.clearDraft = false;
		action.replyTo = replyTo;

		const auto resend = [=](int approved) {
			auto copy = options;
			copy.starsApproved = approved;
			state->sendPreparedMusic(bundle, copy, replyTo);
		};
		const auto ephemeralReply = controller->session().ephemeralMessages()
			.isEphemeralBotReply(replyTo.messageId);
		if (ephemeralReply && bundle->totalCount > 1) {
			controller->showToast(
				tr::lng_ephemeral_reply_single_message(tr::now));
			return;
		} else if (!ephemeralReply && !state->paymentHelper.check(
				show,
				peer,
				action.options,
				bundle->totalCount,
				resend)) {
			return;
		}

		const auto type = bundle->way.sendImagesAsPhotos()
			? SendMediaType::Photo
			: SendMediaType::File;
		auto &api = controller->session().api();
		for (auto &group : bundle->groups) {
			const auto optionsRequireSingle
				= action.options.scheduleRepeatPeriod
				|| action.options.suggest;
			const auto album = (group.type != Ui::AlbumType::None)
				&& !optionsRequireSingle
				? std::make_shared<SendingAlbum>()
				: nullptr;
			if (album && (group.type == Ui::AlbumType::Music)) {
				album->musicPreparedBatching = true;
			}
			api.sendFiles(std::move(group.list), type, album, action);
		}
		box->closeBox();
	};
	state->sendSelected = [=](Api::SendOptions options) {
		auto action = actionFactory();
		ApplySendOptions(action.options, options);

		auto text = caption->getTextWithAppliedMarkdown();
		const auto items = MakeMusicSelectionItems(
			&controller->session(),
			state->savedSelection,
			state->yourChatsSelection,
			state->globalSelection);
		if (items.empty()) {
			return;
		}
		const auto ephemeralReply = controller->session().ephemeralMessages()
			.isEphemeralBotReply(action.replyTo.messageId);
		if (ephemeralReply && items.size() > 1) {
			controller->showToast(
				tr::lng_ephemeral_reply_single_message(tr::now));
			return;
		}

		const auto thread = not_null<Data::Thread*>(
			static_cast<Data::Thread*>(action.history.get()));
		const auto error = GetErrorForSending(thread, {
			.topicRootId = action.replyTo.topicRootId,
			.text = &text,
			.messagesCount = [&] {
				const auto optionsRequireSingle = action.options.price
					|| action.options.scheduleRepeatPeriod
					|| action.options.suggest;
				return optionsRequireSingle
					? int(items.size())
					: int((items.size() + Ui::MaxAlbumItems() - 1)
						/ Ui::MaxAlbumItems());
			}(),
		});
		if (error) {
			show->showBox(MakeSendErrorBox({
				.error = error,
				.thread = thread,
			}, false));
			return;
		}
		const auto resend = [=](int approved) {
			auto copy = options;
			copy.starsApproved = approved;
			state->sendSelected(copy);
		};
		if (!ephemeralReply && !state->paymentHelper.check(
				show,
				peer,
				action.options,
				int(items.size()),
				resend)) {
			return;
		}

		auto message = Api::MessageToSend(action);
		message.textWithTags = std::move(text);
		Api::SendMusicSelection(std::move(message), std::move(items));
		box->closeBox();
	};
	caption->submits() | rpl::on_next([=](Qt::KeyboardModifiers) {
		if (!state->selection.empty()) {
			state->sendSelected({});
		}
	}, caption->lifetime());

	const auto refreshFooter = [=] {
		box->clearButtons();
		if (!state->selection.empty()) {
			const auto send = box->addButton(tr::lng_send_button(), [=] {
				if (!state->selection.empty()) {
					state->sendSelected({});
				}
			});
			SendMenu::SetupMenuAndShortcuts(
				send.data(),
				show,
				[=] {
					return state->selection.empty()
						? SendMenu::Details()
						: show->sendMenuDetails();
				},
				SendMenu::DefaultCallback(show, [=](Api::SendOptions options) {
					state->sendSelected(options);
				}));
		}
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	};
	const auto refreshSelection = [=] {
		const auto wasEmpty = state->selection.empty();
		const auto captionFocused = Ui::InFocusChain(caption);

		state->selection = MakeMusicSelectionItems(
			&controller->session(),
			state->savedSelection,
			state->yourChatsSelection,
			state->globalSelection);
		const auto remaining = std::max(
			0,
			MaxSelectedItems - int(state->selection.size()));
		savedMusic->list()->setSelectedLimit(
			int(state->savedSelection.list.size()) + remaining);
		yourChats->list()->setSelectedLimit(
			int(state->yourChatsSelection.list.size()) + remaining);
		globalSearch->setSelectedLimit(
			int(state->globalSelection.size()) + remaining);

		if (state->selection.empty()) {
			caption->setText(QString());
			if (bottom->toggled()) {
				bottom->hide(anim::type::normal);
			}
			if (!wasEmpty && captionFocused) {
				search->entity()->setInnerFocus();
			}
		} else {
			if (!bottom->toggled()) {
				bottom->show(anim::type::normal);
			}
			if (wasEmpty) {
				caption->setFocusFast();
			}
		}
		refreshFooter();
	};
	const auto contentY = [=](const QWidget *widget) {
		return widget->mapTo(content, QPoint()).y();
	};
	const auto listOwnsScrollTop = [=](
			not_null<Info::Media::ListWidget*> list) {
		const auto listTop = contentY(list);
		const auto scrollTop = box->scrollTop();
		return (scrollTop >= listTop)
			&& (int64(scrollTop) < int64(listTop) + list->height());
	};
	const auto captureViewportAnchor = [=] {
		auto result = ViewportAnchor{
			.scrollTop = box->scrollTop(),
		};
		const auto scrollBottom = int64(result.scrollTop)
			+ box->scrollHeight();
		if (yourChatsBlock->toggled()) {
			const auto list = yourChats->list();
			const auto listTop = contentY(list);
			result.inChatsList = (result.scrollTop >= listTop)
				&& (int64(result.scrollTop) < int64(listTop) + list->height());
			if (result.inChatsList) {
				if (const auto &view = state->geometry.acceptedView) {
					for (const auto &row : view->rows) {
						const auto rowTop = list->mapTo(
							content,
							row.geometry.topLeft()).y();
						const auto rowBottom = int64(rowTop)
							+ row.geometry.height();
						if (int64(rowTop) < scrollBottom
							&& rowBottom > result.scrollTop) {
							result.kind = ViewportAnchor::Kind::ChatsRow;
							result.query = view->slice.query;
							result.position = row.position;
							result.offset = result.scrollTop - rowTop;
							return result;
						}
					}
				}
				result.kind = ViewportAnchor::Kind::ListOffset;
				result.widget = list;
				result.offset = result.scrollTop - listTop;
				return result;
			}
		}
		if (savedMusicBlock->toggled() && !savedMusic->list()->isHidden()) {
			const auto list = savedMusic->list();
			const auto listTop = contentY(list);
			if (result.scrollTop >= listTop
				&& (int64(result.scrollTop)
					< int64(listTop) + list->height())) {
				result.kind = ViewportAnchor::Kind::ListOffset;
				result.widget = list;
				result.offset = result.scrollTop - listTop;
				result.inBrowseTop = browseTop->toggled();
				return result;
			}
		}
		if (globalSearchWrap->toggled()) {
			const auto sectionTop = contentY(globalSearch);
			const auto sectionBottom = int64(sectionTop)
				+ globalSearch->height();
			if (result.scrollTop >= sectionTop
				&& int64(result.scrollTop) < sectionBottom) {
				result.kind = ViewportAnchor::Kind::GlobalSearch;
				result.offset = result.scrollTop - sectionTop;
				return result;
			}
		}
		result.inBrowseTop = browseTop->toggled()
			&& (!yourChatsBlock->toggled()
				|| result.scrollTop < contentY(yourChats->list()));
		const auto tryWidget = [&](QWidget *widget) {
			if (!widget || widget->isHidden()) {
				return false;
			}
			const auto top = contentY(widget);
			if (result.scrollTop >= top
				&& result.scrollTop < top + widget->height()) {
				result.kind = ViewportAnchor::Kind::Widget;
				result.widget = widget;
				result.offset = result.scrollTop - top;
				return true;
			}
			return false;
		};
		if (browseTop->toggled()) {
			if (tryWidget(chooseFromFiles)) {
				return result;
			} else if (savedMusicBlock->toggled() && tryWidget(savedMusic)) {
				return result;
			}
		}
		if (yourChatsBlock->toggled() && tryWidget(yourChats)) {
			return result;
		}
		result.kind = ViewportAnchor::Kind::Raw;
		return result;
	};
	const auto restoreViewportAnchor = [=](const ViewportAnchor &anchor) {
		const auto scrollTo = [&](int64 desired) {
			const auto clamped = int(std::clamp<int64>(
				desired,
				0,
				QWIDGETSIZE_MAX));
			if (box->scrollTop() != clamped) {
				box->scrollToY(clamped);
			}
		};
		if (anchor.kind == ViewportAnchor::Kind::ChatsRow) {
			const auto &view = state->geometry.acceptedView;
			if (view && view->slice.query == anchor.query) {
				const auto i = std::find_if(
					begin(view->rows),
					end(view->rows),
					[&](const auto &row) {
						return row.position == anchor.position;
					});
				if (i != end(view->rows)) {
					const auto rowTop = yourChats->list()->mapTo(
						content,
						i->geometry.topLeft()).y();
					scrollTo(int64(rowTop) + anchor.offset);
					return;
				}
			}
			if (yourChatsBlock->toggled()) {
				scrollTo(int64(contentY(yourChats->list())) + anchor.offset);
			} else {
				scrollTo(anchor.scrollTop);
			}
			return;
		} else if (anchor.kind == ViewportAnchor::Kind::ListOffset) {
			if (const auto widget = anchor.widget.data()) {
				if (!widget->isHidden()) {
					scrollTo(int64(contentY(widget)) + anchor.offset);
					return;
				}
			}
			scrollTo(anchor.scrollTop);
			return;
		} else if (anchor.kind == ViewportAnchor::Kind::GlobalSearch) {
			if (globalSearchWrap->toggled()) {
				scrollTo(int64(contentY(globalSearch)) + anchor.offset);
			}
			return;
		} else if (anchor.kind == ViewportAnchor::Kind::Widget) {
			if (const auto widget = anchor.widget.data()) {
				if (!widget->isHidden()) {
					scrollTo(int64(contentY(widget)) + anchor.offset);
					return;
				}
			}
		}
		scrollTo(anchor.scrollTop);
	};
	state->geometry.update = [=] {
		if (state->geometry.updating) {
			state->geometry.pending = true;
			return;
		}
		state->geometry.updating = true;
		auto anchor = state->geometry.pendingViewportAnchor
			? *base::take(state->geometry.pendingViewportAnchor)
			: captureViewportAnchor();

		do {
			state->geometry.pending = false;
			const auto searching = !state->typedSearchQuery.isEmpty();
			const auto commitPending = (
				state->typedSearchQuery != state->committedSearchQuery);
			const auto committedQuery = state->committedSearchQuery;
			const auto queryChanged = (state->yourChatsQuery != committedQuery);
			const auto searchModeChanged = (
				searching != state->geometry.searchMode);
			if (queryChanged) {
				state->yourChatsQuery = committedQuery;
				state->geometry.topAuthority = true;
				state->geometry.authorityGenerationFloor = 0;
				state->geometry.acceptedView = std::nullopt;
				state->yourChatsFullCount = std::nullopt;
				state->yourChatsFullCountQuery = std::nullopt;
			}
			if (queryChanged || searchModeChanged) {
				state->geometry.searchMode = searching;
				state->geometry.pendingViewportAnchor = std::nullopt;
				state->geometry.pendingChatsScrollTop = std::nullopt;
				state->geometry.pendingChatsScrollReady = false;
				anchor = ViewportAnchor{ .scrollTop = 0 };
			}
			yourChatsControllerRaw->setQuery(committedQuery);
			globalSearch->setQuery(committedQuery);

			const auto savedMusicCount = savedMusic->fullCount();
			const auto savedMusicCollapsed = !searching
				&& savedMusicCount.has_value()
				&& (*savedMusicCount > kFirstLimit)
				&& !state->savedMusicExpanded;
			const auto yourChatsCountMatchesQuery
				= state->yourChatsFullCountQuery
				&& (*state->yourChatsFullCountQuery == committedQuery);
			const auto yourChatsCollapsed = !committedQuery.isEmpty()
				&& yourChatsCountMatchesQuery
				&& state->yourChatsFullCount.has_value()
				&& (*state->yourChatsFullCount > kFirstLimit)
				&& !state->yourChatsExpanded;
			const auto &view = state->geometry.acceptedView;
			const auto yourChatsFullyLoaded = view
				&& (view->slice.query == committedQuery)
				&& view->slice.fullyLoaded;
			const auto yourChatsAtBottom = view
				&& (view->slice.query == committedQuery)
				&& (view->slice.skippedBefore == 0);
			const auto savedMusicFullyLoaded = controller->session()
				.data()
				.savedMusic()
				.fullyLoaded(controller->session().user()->id)
				&& savedMusic->list()->allRowsDisplayed();
			const auto savedMusicJoinReady = savedMusicCollapsed
				|| savedMusicFullyLoaded;
			const auto savedMusicLoading = !searching
				&& !savedMusicJoinReady;
			const auto yourChatsJoinReady = yourChatsCollapsed
				|| (yourChatsFullyLoaded && yourChatsAtBottom);
			const auto yourChatsLoading = searching
				&& !yourChatsJoinReady;
			const auto globalSearchSectionVisible = searching
				&& !commitPending
				&& !yourChatsLoading
				&& globalSearch->available();
			const auto yourChatsSectionVisible = !savedMusicLoading
				&& !(globalSearchSectionVisible
					&& globalSearch->awayFromTop());
			savedMusic->setCollapsed(savedMusicCollapsed);
			yourChats->setCollapsed(yourChatsCollapsed);
			yourChats->setTopSkip(
				searching ? st::defaultVerticalListSkip : 0);
			savedMusic->setPagingEnabled(
				!searching && !savedMusicCollapsed);
			yourChats->setPagingEnabled(
				yourChatsSectionVisible
					&& !yourChatsCollapsed
					&& !commitPending);
			globalSearch->setPagingEnabled(globalSearchSectionVisible);
			const auto viewportHeight = box->scrollHeight();
			savedMusic->setExternalViewportHeight(viewportHeight);
			yourChats->setExternalViewportHeight(viewportHeight);
			savedMusicShowAllWrap->toggle(
				savedMusicCollapsed,
				anim::type::instant);
			yourChatsShowAllWrap->toggle(
				yourChatsSectionVisible && yourChatsCollapsed,
				anim::type::instant);

			if (browseTopLayout->width() > 0) {
				savedMusicBlock->resizeToWidth(browseTopLayout->width());
			}
			const auto topAuthority = savedMusicLoading
				|| state->geometry.topAuthority;
			const auto savedDisplayEligible = !searching
				&& savedMusic->hasResults();
			const auto showBrowseTop = !searching
				&& (topAuthority || anchor.inBrowseTop);
			const auto yourChatsHasResults = yourChats->hasResults();
			const auto titleEligible = yourChatsHasResults
				&& (topAuthority || !anchor.inChatsList);
			savedMusicBlock->toggle(
				savedDisplayEligible,
				anim::type::instant);
			yourChats->setTitleEligible(titleEligible);

			searchEmpty->setQuery(
				searching ? state->typedSearchQuery : QString());
			const auto savedMusicCountKnown = controller->session()
				.data()
				.savedMusic()
				.countKnown(controller->session().user()->id);
			const auto savedMusicVisible = savedMusic->hasResults();
			const auto yourChatsVisible = yourChatsSectionVisible
				&& yourChatsHasResults
				&& !commitPending;
			const auto countMatchesQuery = state->yourChatsFullCountQuery
				&& (*state->yourChatsFullCountQuery == committedQuery);
			const auto yourChatsSettled = countMatchesQuery
				&& state->yourChatsFullCount.has_value();
			const auto yourChatsKnownEmpty = yourChatsSettled
				&& (*state->yourChatsFullCount == 0);
			const auto globalSearchVisible = globalSearchSectionVisible
				&& globalSearch->hasResults();
			const auto globalSearchBusy = globalSearch->busy();
			const auto resultsVisible = yourChatsVisible
				|| globalSearchVisible;
			const auto showBrowseEmpty = !searching
				&& !commitPending
				&& savedMusicCountKnown
				&& !savedMusicVisible
				&& yourChatsKnownEmpty;
			const auto showSearchLoading = searching
				&& !resultsVisible
				&& (commitPending
					|| !yourChatsSettled
					|| globalSearchBusy);
			const auto showSearchEmpty = searching
				&& !resultsVisible
				&& !showSearchLoading;

			browseTop->toggle(showBrowseTop, anim::type::instant);
			yourChatsBlock->toggle(
				yourChatsVisible,
				anim::type::instant);
			globalSearchWrap->toggle(
				globalSearchSectionVisible,
				anim::type::instant);
			searchEmpty->setLoading(showSearchLoading);
			searchEmptyWrap->toggle(
				showBrowseEmpty
					|| showSearchLoading
					|| showSearchEmpty,
				anim::type::instant);
			if (content->width() > 0) {
				content->resizeToWidth(content->width());
			}
		} while (state->geometry.pending);

		if (state->geometry.pendingChatsScrollTop
			&& state->geometry.pendingChatsScrollReady
			&& anchor.inChatsList) {
			const auto listTop = contentY(yourChats->list());
			const auto desired = int(std::clamp<int64>(
				int64(listTop) + *state->geometry.pendingChatsScrollTop,
				0,
				QWIDGETSIZE_MAX));
			state->geometry.pendingChatsScrollTop = std::nullopt;
			state->geometry.pendingChatsScrollReady = false;
			if (box->scrollTop() != desired) {
				box->scrollToY(desired);
			}
		} else {
			state->geometry.pendingChatsScrollReady = false;
			if (!anchor.inChatsList) {
				state->geometry.pendingChatsScrollTop = std::nullopt;
			}
			restoreViewportAnchor(anchor);
		}
		const auto visibleBottom = int(std::min<int64>(
			int64(box->scrollTop()) + box->scrollHeight(),
			QWIDGETSIZE_MAX));
		content->setVisibleTopBottom(box->scrollTop(), visibleBottom);
		state->geometry.updating = false;
		if (std::exchange(state->geometry.pending, false)) {
			state->geometry.update();
		}
	};
	const auto updateSearchState = [=] {
		state->geometry.update();
	};
	const auto schedulePendingChatsScroll = [=] {
		if (state->geometry.pendingChatsScrollScheduled) {
			return;
		}
		state->geometry.pendingChatsScrollScheduled = true;
		crl::on_main(box.get(), [=] {
			state->geometry.pendingChatsScrollScheduled = false;
			state->geometry.pendingChatsScrollReady
				= state->geometry.pendingChatsScrollTop.has_value();
			updateSearchState();
		});
	};
	const auto commitSearchQuery = [=](QString query) {
		if (state->committedSearchQuery == query) {
			return;
		}
		state->yourChatsExpanded = false;
		state->committedSearchQuery = std::move(query);
		updateSearchState();
	};
	QObject::connect(
		&state->searchQueryCommitTimer,
		&QTimer::timeout,
		box.get(),
		[=] { commitSearchQuery(state->typedSearchQuery); });
	refreshFooter();

	savedMusic->selectedListValue() | rpl::on_next([=](
			const Info::SelectedItems &items) {
		state->savedSelection = items;
		refreshSelection();
	}, box->lifetime());
	yourChats->selectedListValue() | rpl::on_next([=](
			const Info::SelectedItems &items) {
		state->yourChatsSelection = items;
		refreshSelection();
	}, box->lifetime());
	globalSearch->selectedDocumentsValue() | rpl::on_next([=](
			const GlobalMusicSearchSection::SelectedDocuments &items) {
		state->globalSelection = items;
		refreshSelection();
	}, box->lifetime());
	savedMusicBlockLayout->heightValue() | rpl::on_next([=] {
		updateSearchState();
	}, box->lifetime());
	savedMusic->fullCountValue() | rpl::on_next([=](std::optional<int>) {
		updateSearchState();
	}, box->lifetime());
	yourChats->fullCountValue() | rpl::on_next([=](
			std::optional<int> count) {
		state->yourChatsFullCount = count;
		state->yourChatsFullCountQuery = state->committedSearchQuery;
	}, box->lifetime());
	const auto rememberViewportAnchor = [=] {
		if (!state->geometry.updating
			&& !state->geometry.pendingViewportAnchor) {
			state->geometry.pendingViewportAnchor = captureViewportAnchor();
		}
	};
	savedMusic->list()->globalMediaSliceRefreshStarts(
	) | rpl::on_next(rememberViewportAnchor, box->lifetime());
	yourChats->list()->globalMediaSliceRefreshStarts(
	) | rpl::on_next(rememberViewportAnchor, box->lifetime());
	yourChats->list()->scrollToRequests() | rpl::on_next([=](int top) {
		if (!listOwnsScrollTop(yourChats->list())) {
			state->geometry.pendingChatsScrollTop = std::nullopt;
			state->geometry.pendingChatsScrollReady = false;
			return;
		}
		state->geometry.pendingChatsScrollTop = top;
		state->geometry.pendingChatsScrollReady = false;
		if (!state->geometry.updating) {
			schedulePendingChatsScroll();
		}
	}, box->lifetime());
	yourChats->globalMediaSliceViewValue() | rpl::on_next([=](
			std::optional<Info::Media::GlobalMediaSliceView> view) {
		if (!view || view->slice.query != state->committedSearchQuery) {
			state->geometry.pendingChatsScrollTop = std::nullopt;
			state->geometry.pendingChatsScrollReady = false;
			if (state->geometry.acceptedView) {
				state->geometry.acceptedView = std::nullopt;
				updateSearchState();
			}
			return;
		}
		if (view->slice.generation
			< state->geometry.authorityGenerationFloor) {
			return;
		}
		state->geometry.authorityGenerationFloor = view->slice.generation;
		state->geometry.topAuthority = (view->slice.skippedAfter == 0);
		state->geometry.acceptedView = std::move(view);
		if (state->geometry.pendingChatsScrollTop) {
			state->geometry.pendingChatsScrollReady = true;
		}
		updateSearchState();
	}, box->lifetime());
	rpl::combine(
		box->heightValue(),
		top->heightValue(),
		bottom->heightValue()
	) | rpl::on_next([=](int, int, int) {
		const auto viewportHeight = box->scrollHeight();
		savedMusic->setExternalViewportHeight(viewportHeight);
		yourChats->setExternalViewportHeight(viewportHeight);
		updateSearchState();
	}, box->lifetime());
	controller->session().data().savedMusic().changed()
	| rpl::filter([=](PeerId peerId) {
		return (peerId == controller->session().user()->id);
	}) | rpl::on_next([=](PeerId) {
		updateSearchState();
	}, box->lifetime());
	globalSearch->stateChanged() | rpl::on_next([=] {
		updateSearchState();
	}, box->lifetime());
	globalSearch->awayFromTopValue() | rpl::distinct_until_changed(
	) | rpl::on_next([=](bool) {
		updateSearchState();
	}, box->lifetime());
	savedMusic->list()->scrollToRequests() | rpl::on_next([=](int top) {
		const auto list = savedMusic->list();
		if (!listOwnsScrollTop(list)) {
			return;
		}
		crl::on_main(box.get(), [=] {
			if (!listOwnsScrollTop(list)) {
				return;
			}
			const auto listTop = list->mapTo(content, QPoint()).y();
			box->scrollToY(std::max(listTop + top, 0));
		});
	}, box->lifetime());

	box->addTopButton(st::boxTitleClose, [=] {
		box->closeBox();
	});
	box->setFocusCallback([=] {
		if (bottom->toggled()) {
			caption->setFocusFast();
			return;
		}
		search->entity()->setInnerFocus();
	});
	savedMusicShowAllWrap->entity()->setClickedCallback([=] {
		state->savedMusicExpanded = true;
		updateSearchState();
	});
	yourChatsShowAllWrap->entity()->setClickedCallback([=] {
		state->yourChatsExpanded = true;
		updateSearchState();
	});
	chooseFromFiles->setClickedCallback([=] {
		FileDialog::GetOpenPaths(
			box.get(),
			tr::lng_choose_files(tr::now),
			FileDialog::MusicFilesFilter(),
			crl::guard(box, [=](FileDialog::OpenResult &&result) {
				if (result.paths.isEmpty()) {
					return;
				}

				const auto premium = controller->session().user()->isPremium();
				auto list = Storage::PrepareMediaList(
					result.paths,
					st::sendMediaPreviewSize,
					premium);
				if (Data::ShowSendError(show, peer, list, std::nullopt)) {
					return;
				}
				list = FilterMusicPreparedList(
					std::move(list),
					st::sendMediaPreviewSize,
					premium);
				if (Data::ShowSendError(show, peer, list, std::nullopt)) {
					return;
				} else if (list.files.empty() && list.filesToProcess.empty()) {
					controller->showToast(tr::lng_send_media_invalid_files(tr::now));
					return;
				}

				const auto action = actionFactory();
				auto sendBox = Box<SendFilesBox>(
					controller,
					std::move(list),
					TextWithTags(),
					peer,
					CurrentSendType(action),
					show->sendMenuDetails());
				sendBox->setReplyTo(action.replyTo);
				sendBox->setConfirmedCallback(crl::guard(box, [=](
						std::shared_ptr<Ui::PreparedBundle> bundle,
						Api::SendOptions options,
						FullReplyTo currentReplyTo) {
					state->sendPreparedMusic(
						std::move(bundle),
						options,
						currentReplyTo);
				}));
				controller->show(std::move(sendBox));
			}),
			nullptr);
	});

	search->entity()->setQueryChangedCallback([=](const QString &query) {
		if (state->typedSearchQuery == query) {
			return;
		}
		state->typedSearchQuery = query;
		updateSearchState();
		state->searchQueryCommitTimer.start(kSearchRequestDelay);
	});
	search->entity()->setSubmittedCallback([=](Qt::KeyboardModifiers) {
		state->searchQueryCommitTimer.stop();
		commitSearchQuery(state->typedSearchQuery);
	});
	search->entity()->setCancelledCallback([=] {
		state->searchQueryCommitTimer.stop();
		state->typedSearchQuery = QString();
		state->committedSearchQuery = QString();
		state->yourChatsExpanded = false;
		if (!search->entity()->getQuery().isEmpty()) {
			search->entity()->clearQuery();
		}
		updateSearchState();
	});

	refreshSelection();
	updateSearchState();
}
