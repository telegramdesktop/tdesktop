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
#include "base/algorithm.h"
#include "base/unixtime.h"
#include "boxes/sticker_set_box.h"
#include "boxes/send_files_box.h"
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
#include "ui/widgets/buttons.h"
#include "ui/widgets/multi_select.h"
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
#include "styles/style_overview.h"
#include "styles/style_widgets.h"

#include <QtCore/QTimer>

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

#include <rpl/variable.h>

namespace {

constexpr auto kSearchRequestDelay = 400;
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
			rpl::producer<QString> title);

	[[nodiscard]] not_null<Info::Media::ListWidget*> list() const;
	[[nodiscard]] rpl::producer<Info::SelectedItems> selectedListValue() const;
	[[nodiscard]] bool hasResults() const;
	[[nodiscard]] std::optional<int> fullCount() const;
	[[nodiscard]] rpl::producer<std::optional<int>> fullCountValue() const;
	[[nodiscard]] auto globalMediaSliceView() const
		-> const std::optional<Info::Media::GlobalMediaSliceView> &;
	[[nodiscard]] auto globalMediaSliceViewValue() const
		-> rpl::producer<std::optional<Info::Media::GlobalMediaSliceView>>;
	[[nodiscard]] bool viewportInVirtualSpace() const;

	void setQuery(QString query);
	void setCollapsed(bool collapsed);
	void setExternalViewportHeight(int height);
	void setTitleEligible(bool eligible);
	void setHiddenTopCompensation(int height);
	void setTitleAndHiddenTopCompensation(bool eligible, int height);

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(int visibleTop, int visibleBottom) override;

private:
	struct VirtualTarget {
		QString query;
		uint64 generation = 0;
		int globalIndex = 0;
		int rowOffset = 0;
	};

	[[nodiscard]] int listHeightForCurrentMode() const;
	[[nodiscard]] bool virtualTrackEnabled() const;
	void clearVirtualTargeting();
	void updateTitleVisibility();
	void refreshHeight();

	const std::unique_ptr<MusicSectionController> _controller;
	object_ptr<Ui::VerticalLayout> _titleWrap;
	object_ptr<Info::Media::ListWidget> _list;
	std::optional<Info::Media::GlobalMediaSliceView> _sliceView;
	std::optional<VirtualTarget> _virtualTarget;
	int _hiddenTopCompensation = 0;
	int _outerViewportHeight = 0;
	int _visibleTop = 0;
	int _visibleBottom = 0;
	bool _collapsed = false;
	bool _titleEligible = true;
	bool _viewportInVirtualSpace = false;
	bool _inResize = false;

};

class SearchEmptyWidget final : public Info::Media::EmptyWidget {
public:
	explicit SearchEmptyWidget(QWidget *parent);
	void setQuery(QString query);

};

[[nodiscard]] style::margins MusicAttachSubsectionTitlePadding(
		int topSkip = 0) {
	const auto sideSkip = st::overviewFileLayout.songPadding.left();
	return style::margins(
		sideSkip - st::defaultSubsectionTitlePadding.left(),
		topSkip,
		sideSkip - st::defaultSubsectionTitlePadding.right(),
		0);
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
	[[nodiscard]] bool hasResults() const;
	[[nodiscard]] bool busy() const;
	[[nodiscard]] bool loading() const;
	[[nodiscard]] int rawSelectedCount() const;
	[[nodiscard]] rpl::producer<SelectedDocuments> selectedDocumentsValue() const;
	[[nodiscard]] rpl::producer<> stateChanged() const;

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(int visibleTop, int visibleBottom) override;
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
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
	QTimer _requestTimer;
	std::unique_ptr<StickerPremiumMark> _hiddenMark;
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
, _key(Info::GlobalMedia::Tag{ controller->session().user() })
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
		rpl::producer<QString> title)
: RpWidget(parent)
, _controller(std::move(controller))
, _titleWrap(this)
, _list(this, _controller.get()) {
	_titleWrap->show();
	Ui::AddSubsectionTitle(
		_titleWrap.data(),
		std::move(title),
		MusicAttachSubsectionTitlePadding());
	_list->show();
	_list->setViewportInVirtualSpace(false);
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
			std::optional<Info::Media::GlobalMediaSliceView> view) {
		_sliceView = std::move(view);
		refreshHeight();
		visibleTopBottomUpdated(_visibleTop, _visibleBottom);
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
	return (_list->heightNoMargins() > 0);
}

std::optional<int> HistoryMusicSection::fullCount() const {
	return _list->fullCount();
}

rpl::producer<std::optional<int>> HistoryMusicSection::fullCountValue() const {
	return _list->fullCountValue();
}

auto HistoryMusicSection::globalMediaSliceView() const
-> const std::optional<Info::Media::GlobalMediaSliceView> & {
	return _sliceView;
}

auto HistoryMusicSection::globalMediaSliceViewValue() const
-> rpl::producer<std::optional<Info::Media::GlobalMediaSliceView>> {
	return _list->globalMediaSliceViewValue();
}

bool HistoryMusicSection::viewportInVirtualSpace() const {
	return _viewportInVirtualSpace;
}

void HistoryMusicSection::setQuery(QString query) {
	_controller->setQuery(std::move(query));
}

void HistoryMusicSection::setCollapsed(bool collapsed) {
	if (_collapsed == collapsed) {
		return;
	}
	_collapsed = collapsed;
	_list->setPreloadEnabled(!collapsed);
	if (collapsed) {
		clearVirtualTargeting();
	}
	refreshHeight();
	visibleTopBottomUpdated(_visibleTop, _visibleBottom);
}

void HistoryMusicSection::setExternalViewportHeight(int height) {
	height = std::max(height, 0);
	if (_outerViewportHeight == height) {
		return;
	}
	_outerViewportHeight = height;
	_list->setExternalViewportHeight(height);
	visibleTopBottomUpdated(_visibleTop, _visibleBottom);
}

void HistoryMusicSection::setTitleEligible(bool eligible) {
	setTitleAndHiddenTopCompensation(
		eligible,
		_hiddenTopCompensation);
}

void HistoryMusicSection::setHiddenTopCompensation(int height) {
	setTitleAndHiddenTopCompensation(_titleEligible, height);
}

void HistoryMusicSection::setTitleAndHiddenTopCompensation(
		bool eligible,
		int height) {
	height = std::clamp(height, 0, QWIDGETSIZE_MAX);
	if (_titleEligible == eligible
		&& _hiddenTopCompensation == height) {
		return;
	}
	_titleEligible = eligible;
	_hiddenTopCompensation = height;
	updateTitleVisibility();
	refreshHeight();
	visibleTopBottomUpdated(_visibleTop, _visibleBottom);
}

int HistoryMusicSection::listHeightForCurrentMode() const {
	const auto height = _list->heightNoMargins();
	if (!_collapsed) {
		return height;
	}
	return std::min(height, _list->heightForFirstRows(kFirstLimit));
}

bool HistoryMusicSection::virtualTrackEnabled() const {
	if (_collapsed || !_sliceView) {
		return false;
	}
	const auto &view = *_sliceView;
	const auto &slice = view.slice;
	const auto loadedCount = int(view.rows.size());
	const auto sliceCount = int(slice.positions.size());
	const auto expectedListHeight = int64(view.topPadding)
		+ int64(loadedCount) * view.rowExtent
		+ view.bottomPadding;
	return (view.rowExtent > 0)
		&& (slice.fullCount >= 0)
		&& (slice.skippedAfter >= 0)
		&& (slice.skippedBefore >= 0)
		&& (view.topPadding >= 0)
		&& (view.bottomPadding >= 0)
		&& (loadedCount == sliceCount)
		&& (int64(slice.skippedAfter)
			+ loadedCount
			+ slice.skippedBefore == slice.fullCount)
		&& (expectedListHeight == _list->heightNoMargins());
}

void HistoryMusicSection::clearVirtualTargeting() {
	_viewportInVirtualSpace = false;
	_virtualTarget = std::nullopt;
	_list->setViewportInVirtualSpace(false);
	_list->cancelGlobalMediaAroundGlobalIndex();
}

int HistoryMusicSection::resizeGetHeight(int newWidth) {
	_inResize = true;

	_titleWrap->resizeToWidth(newWidth);
	_titleWrap->moveToLeft(0, 0, newWidth);

	const auto visibleTitleHeight = _titleWrap->isHidden()
		? 0
		: _titleWrap->heightNoMargins();
	_list->resizeToWidth(newWidth);

	const auto topBaseline = int64(visibleTitleHeight)
		+ _hiddenTopCompensation;
	const auto bounded = [](int64 value) {
		return int(std::clamp<int64>(value, 0, QWIDGETSIZE_MAX));
	};
	if (!virtualTrackEnabled()) {
		_list->moveToLeft(0, bounded(topBaseline), newWidth);
		_inResize = false;
		return bounded(topBaseline + listHeightForCurrentMode());
	}

	const auto &view = *_sliceView;
	const auto above = int64(view.slice.skippedAfter) * view.rowExtent;
	const auto below = int64(view.slice.skippedBefore) * view.rowExtent;
	const auto listHeight = _list->heightNoMargins();
	const auto listTop = topBaseline + above;
	const auto trackHeight = listTop + listHeight + below;
	const auto expectedTrackHeight = topBaseline
		+ view.topPadding
		+ int64(view.slice.fullCount) * view.rowExtent
		+ view.bottomPadding;
	Assert(trackHeight == expectedTrackHeight);
	_list->moveToLeft(0, bounded(listTop), newWidth);

	_inResize = false;
	return bounded(trackHeight);
}

void HistoryMusicSection::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;
	if (!virtualTrackEnabled() || visibleBottom <= visibleTop) {
		clearVirtualTargeting();
		setChildVisibleTopBottom(_list.data(), visibleTop, visibleBottom);
		return;
	}

	const auto &view = *_sliceView;
	if (view.rows.empty() || view.slice.fullCount <= 0) {
		clearVirtualTargeting();
		setChildVisibleTopBottom(_list.data(), visibleTop, visibleBottom);
		return;
	}

	const auto realTop = int64(_list->y())
		+ view.rows.front().geometry.y();
	const auto realBottom = int64(_list->y())
		+ view.rows.back().geometry.y()
		+ view.rows.back().geometry.height();
	const auto intersectsRealRows = (int64(visibleTop) < realBottom)
		&& (int64(visibleBottom) > realTop);
	if (intersectsRealRows) {
		clearVirtualTargeting();
		setChildVisibleTopBottom(_list.data(), visibleTop, visibleBottom);
		return;
	}

	_viewportInVirtualSpace = true;
	_list->setViewportInVirtualSpace(true);
	const auto rowsHeight = int64(view.slice.fullCount) * view.rowExtent;
	const auto baseline = int64(_list->y())
		- int64(view.slice.skippedAfter) * view.rowExtent
		+ view.topPadding;
	const auto targetOffset = std::clamp<int64>(
		int64(visibleTop) - baseline,
		0,
		rowsHeight - 1);
	const auto globalIndex = int(targetOffset / view.rowExtent);
	const auto rowOffset = int(targetOffset % view.rowExtent);
	const auto sameTarget = _virtualTarget
		&& (_virtualTarget->query == view.slice.query)
		&& (_virtualTarget->generation == view.slice.generation)
		&& (_virtualTarget->globalIndex == globalIndex);
	if (sameTarget) {
		_virtualTarget->rowOffset = rowOffset;
		return;
	}
	_virtualTarget = VirtualTarget{
		.query = view.slice.query,
		.generation = view.slice.generation,
		.globalIndex = globalIndex,
		.rowOffset = rowOffset,
	};
	_list->requestGlobalMediaAroundGlobalIndex(globalIndex);
}

void HistoryMusicSection::updateTitleVisibility() {
	_titleWrap->setVisible(
		_titleEligible && (_list->heightNoMargins() > 0));
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

	_requestTimer.setSingleShot(true);
	connect(
		&_requestTimer,
		&QTimer::timeout,
		this,
		[this] { sendInlineRequest(); });

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
		setLoading(false);
		cancelActiveRequest();
		_requestTimer.stop();
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

bool GlobalMusicSearchSection::hasResults() const {
	return !_rows.empty();
}

bool GlobalMusicSearchSection::busy() const {
	return _pending || _loading;
}

bool GlobalMusicSearchSection::loading() const {
	return _loading;
}

int GlobalMusicSearchSection::rawSelectedCount() const {
	return int(_selected.size());
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
		if (isSelected(row.owned->document)) {
			p.fillRect(geometry, st::windowBgOver);
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
	if (index < 0 || !isVisible()) {
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
		if (_bot && !_nextQuery.isEmpty()
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
		if (_bot && !_nextQuery.isEmpty()
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

void GlobalMusicSearchSection::cancelActiveRequest() {
	if (_requestId) {
		_api.request(base::take(_requestId)).cancel();
	}
}

void GlobalMusicSearchSection::cancelSearch() {
	setPending(false);
	setLoading(false);
	cancelActiveRequest();
	_requestTimer.stop();
	_query = QString();
	_nextQuery = QString();
	_nextOffset = QString();
	rebuildRows();
}

void GlobalMusicSearchSection::sendInlineRequest() {
	if (_requestId || !_bot || _nextQuery.isEmpty()) {
		return;
	}
	_requestTimer.stop();
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
	if (_pending
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

struct NormalizedMusicSelection {
	DocumentData *document = nullptr;
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

[[nodiscard]] std::vector<NormalizedMusicSelection> NormalizeSelections(
		not_null<Main::Session*> session,
		const Info::SelectedItems &savedMusic,
		const Info::SelectedItems &yourChats,
		const GlobalMusicSearchSection::SelectedDocuments &globalDocuments) {
	auto result = std::vector<NormalizedMusicSelection>();
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
		});
	}
	return result;
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
	const auto yourChats = content->add(object_ptr<HistoryMusicSection>(
		content,
		std::move(yourChatsController),
		tr::lng_reply_in_chats_list()));
	const auto yourChatsShowAllWrap = addShowAllButton(content);
	const auto globalSearch = content->add(
		object_ptr<GlobalMusicSearchSection>(
			content,
			controller,
			peer,
			tr::lng_music_attach_global_search()));
	const auto searchEmpty = content->add(object_ptr<SearchEmptyWidget>(content));
	searchEmpty->setFullHeight(rpl::combine(
		box->heightValue(),
		search->heightValue(),
		bottom->heightValue(),
		searchEmpty->topValue()
	) | rpl::map([](
			int boxHeight,
			int searchHeight,
			int bottomHeight,
			int top) {
		return std::max(boxHeight - searchHeight - bottomHeight - top, 0);
	}));
	globalSearch->hide();
	searchEmpty->hide();

	for (const auto section : { savedMusic, yourChats }) {
		section->list()->setSelectOnClick(true);
		section->list()->setSelectedLimit(MaxSelectedItems);
	}
	globalSearch->setSelectedLimit(MaxSelectedItems);

	const auto savedSelection = box->lifetime().make_state<Info::SelectedItems>();
	const auto yourChatsSelection = box->lifetime().make_state<
		Info::SelectedItems>();
	const auto globalSelection = box->lifetime().make_state<
		GlobalMusicSearchSection::SelectedDocuments>();
	const auto selection = box->lifetime().make_state<
		std::vector<NormalizedMusicSelection>>();
	const auto typedSearchQuery = box->lifetime().make_state<QString>();
	const auto committedSearchQuery = box->lifetime().make_state<QString>();
	const auto searchQueryCommitTimer = box->lifetime().make_state<QTimer>();
	const auto savedMusicExpanded = box->lifetime().make_state<bool>(false);
	const auto yourChatsExpanded = box->lifetime().make_state<bool>(false);
	const auto yourChatsQuery = box->lifetime().make_state<QString>();
	const auto yourChatsFullCount = box->lifetime().make_state<
		std::optional<int>>();
	const auto yourChatsFullCountQuery = box->lifetime().make_state<
		std::optional<QString>>();
	struct OuterAnchor {
		QString query;
		uint64 generation = 0;
		Data::MessagePosition position;
		int shift = 0;
	};
	struct GeometryState {
		std::optional<Info::Media::GlobalMediaSliceView> acceptedView;
		Fn<void()> update;
		uint64 authorityGenerationFloor = 0;
		int chatsTitleHeight = 0;
		int hiddenTopCompensation = 0;
		bool topAuthority = true;
		bool titleEligible = true;
		bool updating = false;
		bool pending = false;
	};
	const auto geometryState = box->lifetime().make_state<GeometryState>();
	geometryState->chatsTitleHeight = std::max(
		yourChats->list()->y(),
		0);
	const auto paymentHelper = box->lifetime().make_state<SendPaymentHelper>();
	const auto sendPreparedMusic = box->lifetime().make_state<
		Fn<void(std::shared_ptr<Ui::PreparedBundle>, Api::SendOptions, FullReplyTo)>>();
	const auto sendSelected = box->lifetime().make_state<
		Fn<void(Api::SendOptions)>>();
	searchQueryCommitTimer->setSingleShot(true);

	*sendPreparedMusic = [=](
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
			(*sendPreparedMusic)(bundle, copy, replyTo);
		};
		const auto ephemeralReply = controller->session().ephemeralMessages()
			.isEphemeralBotReply(replyTo.messageId);
		if (ephemeralReply && bundle->totalCount > 1) {
			controller->showToast(
				tr::lng_ephemeral_reply_single_message(tr::now));
			return;
		} else if (!ephemeralReply && !paymentHelper->check(
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
	*sendSelected = [=](Api::SendOptions options) {
		auto action = actionFactory();
		ApplySendOptions(action.options, options);

		auto text = caption->getTextWithAppliedMarkdown();
		const auto items = MakeMusicSelectionItems(
			&controller->session(),
			*savedSelection,
			*yourChatsSelection,
			*globalSelection);
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
			(*sendSelected)(copy);
		};
		if (!ephemeralReply && !paymentHelper->check(
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
		if (!selection->empty()) {
			(*sendSelected)({});
		}
	}, caption->lifetime());

	const auto refreshFooter = [=] {
		box->clearButtons();
		if (!selection->empty()) {
			const auto send = box->addButton(tr::lng_send_button(), [=] {
				if (!selection->empty()) {
					(*sendSelected)({});
				}
			});
			SendMenu::SetupMenuAndShortcuts(
				send.data(),
				show,
				[=] {
					return selection->empty()
						? SendMenu::Details()
						: show->sendMenuDetails();
				},
				SendMenu::DefaultCallback(show, [=](Api::SendOptions options) {
					(*sendSelected)(options);
				}));
		}
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	};
	const auto refreshSelection = [=] {
		const auto wasEmpty = selection->empty();
		const auto captionFocused = Ui::InFocusChain(caption);

		*selection = NormalizeSelections(
			&controller->session(),
			*savedSelection,
			*yourChatsSelection,
			*globalSelection);
		const auto remaining = std::max(
			0,
			MaxSelectedItems - int(selection->size()));
		savedMusic->list()->setSelectedLimit(
			int(savedSelection->list.size()) + remaining);
		yourChats->list()->setSelectedLimit(
			int(yourChatsSelection->list.size()) + remaining);
		globalSearch->setSelectedLimit(
			int(globalSelection->size()) + remaining);

		if (selection->empty()) {
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
	const auto captureOuterAnchor = [=]() -> std::optional<OuterAnchor> {
		const auto &view = geometryState->acceptedView;
		if (!view || yourChats->viewportInVirtualSpace()) {
			return std::nullopt;
		}
		const auto scrollTop = box->scrollTop();
		const auto scrollBottom = int64(scrollTop) + box->scrollHeight();
		for (const auto &row : view->rows) {
			const auto rowTop = yourChats->list()->mapTo(
				content,
				row.geometry.topLeft()).y();
			const auto rowBottom = int64(rowTop) + row.geometry.height();
			if (int64(rowTop) < scrollBottom && rowBottom > scrollTop) {
				return OuterAnchor{
					.query = view->slice.query,
					.generation = view->slice.generation,
					.position = row.position,
					.shift = scrollTop - rowTop,
				};
			}
		}
		return std::nullopt;
	};
	geometryState->update = [=] {
		if (geometryState->updating) {
			geometryState->pending = true;
			return;
		}
		geometryState->updating = true;
		const auto anchor = captureOuterAnchor();

		do {
			geometryState->pending = false;
			const auto searching = !typedSearchQuery->isEmpty();
			const auto commitPending = (
				*typedSearchQuery != *committedSearchQuery);
			const auto committedQuery = *committedSearchQuery;
			if (*yourChatsQuery != committedQuery) {
				*yourChatsQuery = committedQuery;
				geometryState->topAuthority = true;
				geometryState->authorityGenerationFloor = 0;
				geometryState->acceptedView = std::nullopt;
				*yourChatsFullCount = std::nullopt;
				*yourChatsFullCountQuery = std::nullopt;
			}
			yourChatsControllerRaw->setQuery(committedQuery);
			globalSearch->setQuery(committedQuery);

			const auto savedMusicCount = savedMusic->fullCount();
			const auto savedMusicCollapsed = committedQuery.isEmpty()
				&& savedMusicCount.has_value()
				&& (*savedMusicCount > kFirstLimit)
				&& !*savedMusicExpanded;
			const auto yourChatsCountMatchesQuery
				= *yourChatsFullCountQuery
				&& (**yourChatsFullCountQuery == committedQuery);
			const auto yourChatsCollapsed = !committedQuery.isEmpty()
				&& yourChatsCountMatchesQuery
				&& yourChatsFullCount->has_value()
				&& (**yourChatsFullCount > kFirstLimit)
				&& !*yourChatsExpanded;
			savedMusic->setCollapsed(savedMusicCollapsed);
			yourChats->setCollapsed(yourChatsCollapsed);
			savedMusicShowAllWrap->toggle(
				savedMusicCollapsed,
				anim::type::instant);
			yourChatsShowAllWrap->toggle(
				yourChatsCollapsed,
				anim::type::instant);

			if (browseTopLayout->width() > 0) {
				savedMusicBlock->resizeToWidth(browseTopLayout->width());
			}
			const auto savedBlockHeight
				= savedMusicBlockLayout->heightNoMargins();
			const auto &view = geometryState->acceptedView;
			if (geometryState->titleEligible
				&& view
				&& yourChats->hasResults()
				&& view->rowExtent > 0) {
				const auto measured = int64(yourChats->list()->y())
					- geometryState->hiddenTopCompensation
					- int64(view->slice.skippedAfter)
						* view->rowExtent;
				if (measured >= 0 && measured <= QWIDGETSIZE_MAX) {
					geometryState->chatsTitleHeight = int(measured);
				}
			}
			const auto topAuthority = geometryState->topAuthority;
			const auto hasChatsRows = yourChats->hasResults();
			const auto savedDisplayEligible = !searching
				&& savedMusic->hasResults();
			const auto savedBlockEligible = savedDisplayEligible
				&& topAuthority;
			const auto titleEligible = topAuthority;
			const auto activeSavedHeight = savedDisplayEligible
				? savedBlockHeight
				: 0;
			const auto activeTitleHeight = hasChatsRows
				? geometryState->chatsTitleHeight
				: 0;
			const auto hiddenTopCompensation = !topAuthority
				? int(std::clamp<int64>(
					int64(activeSavedHeight) + activeTitleHeight,
					0,
					QWIDGETSIZE_MAX))
				: 0;
			savedMusicBlock->toggle(
				savedBlockEligible,
				anim::type::instant);
			yourChats->setTitleAndHiddenTopCompensation(
				titleEligible,
				hiddenTopCompensation);
			geometryState->titleEligible = titleEligible;
			geometryState->hiddenTopCompensation
				= hiddenTopCompensation;

			searchEmpty->setQuery(
				searching ? *typedSearchQuery : QString());
			const auto savedMusicCountKnown = controller->session()
				.data()
				.savedMusic()
				.countKnown(controller->session().user()->id);
			const auto savedMusicVisible = savedMusic->hasResults();
			const auto yourChatsVisible = yourChats->hasResults();
			const auto countMatchesQuery = *yourChatsFullCountQuery
				&& (**yourChatsFullCountQuery == committedQuery);
			const auto yourChatsSettled = countMatchesQuery
				&& yourChatsFullCount->has_value();
			const auto yourChatsKnownEmpty = yourChatsSettled
				&& (**yourChatsFullCount == 0);
			const auto globalSearchVisible = globalSearch->hasResults();
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

			browseTop->toggle(!searching, anim::type::instant);
			yourChats->setVisible(true);
			globalSearch->setVisible(
				searching && globalSearch->available());
			searchEmpty->setLoading(showSearchLoading);
			searchEmpty->setVisible(
				showBrowseEmpty
				|| showSearchLoading
				|| showSearchEmpty);
			if (content->width() > 0) {
				content->resizeToWidth(content->width());
			}
			yourChats->setExternalViewportHeight(box->scrollHeight());
		} while (geometryState->pending);

		if (anchor) {
			const auto &view = geometryState->acceptedView;
			if (view
				&& view->slice.query == anchor->query
				&& view->slice.generation == anchor->generation) {
				const auto i = std::find_if(
					begin(view->rows),
					end(view->rows),
					[&](const auto &row) {
						return row.position == anchor->position;
					});
				if (i != end(view->rows)) {
					const auto rowTop = yourChats->list()->mapTo(
						content,
						i->geometry.topLeft()).y();
					const auto desired = int(std::clamp<int64>(
						int64(rowTop) + anchor->shift,
						0,
						QWIDGETSIZE_MAX));
					if (box->scrollTop() != desired) {
						box->scrollToY(desired);
					}
				}
			}
		}
		const auto visibleBottom = int(std::min<int64>(
			int64(box->scrollTop()) + box->scrollHeight(),
			QWIDGETSIZE_MAX));
		content->setVisibleTopBottom(box->scrollTop(), visibleBottom);
		geometryState->pending = false;
		geometryState->updating = false;
	};
	const auto updateSearchState = [=] {
		geometryState->update();
	};
	const auto commitSearchQuery = [=](QString query) {
		if (*committedSearchQuery == query) {
			return;
		}
		*yourChatsExpanded = false;
		*committedSearchQuery = std::move(query);
		updateSearchState();
	};
	QObject::connect(
		searchQueryCommitTimer,
		&QTimer::timeout,
		box.get(),
		[=] { commitSearchQuery(*typedSearchQuery); });
	refreshFooter();

	savedMusic->selectedListValue() | rpl::on_next([=](
			const Info::SelectedItems &items) {
		*savedSelection = items;
		refreshSelection();
	}, box->lifetime());
	yourChats->selectedListValue() | rpl::on_next([=](
			const Info::SelectedItems &items) {
		*yourChatsSelection = items;
		refreshSelection();
	}, box->lifetime());
	globalSearch->selectedDocumentsValue() | rpl::on_next([=](
			const GlobalMusicSearchSection::SelectedDocuments &items) {
		*globalSelection = items;
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
		*yourChatsFullCount = count;
		*yourChatsFullCountQuery = *committedSearchQuery;
	}, box->lifetime());
	yourChats->globalMediaSliceViewValue() | rpl::on_next([=](
			std::optional<Info::Media::GlobalMediaSliceView> view) {
		if (!view || view->slice.query != *committedSearchQuery) {
			if (geometryState->acceptedView) {
				geometryState->acceptedView = std::nullopt;
				updateSearchState();
			}
			return;
		}
		if (view->slice.generation
			< geometryState->authorityGenerationFloor) {
			return;
		}
		geometryState->authorityGenerationFloor = view->slice.generation;
		geometryState->topAuthority = (view->slice.skippedAfter == 0);
		geometryState->acceptedView = std::move(view);
		updateSearchState();
	}, box->lifetime());
	rpl::combine(
		box->heightValue(),
		top->heightValue(),
		bottom->heightValue()
	) | rpl::on_next([=](int, int, int) {
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
		*savedMusicExpanded = true;
		updateSearchState();
	});
	yourChatsShowAllWrap->entity()->setClickedCallback([=] {
		*yourChatsExpanded = true;
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
					(*sendPreparedMusic)(
						std::move(bundle),
						options,
						currentReplyTo);
				}));
				controller->show(std::move(sendBox));
			}),
			nullptr);
	});

	search->entity()->setQueryChangedCallback([=](const QString &query) {
		if (*typedSearchQuery == query) {
			return;
		}
		*typedSearchQuery = query;
		updateSearchState();
		searchQueryCommitTimer->start(kSearchRequestDelay);
	});
	search->entity()->setSubmittedCallback([=](Qt::KeyboardModifiers) {
		searchQueryCommitTimer->stop();
		commitSearchQuery(*typedSearchQuery);
	});
	search->entity()->setCancelledCallback([=] {
		searchQueryCommitTimer->stop();
		*typedSearchQuery = QString();
		*committedSearchQuery = QString();
		if (!search->entity()->getQuery().isEmpty()) {
			search->entity()->clearQuery();
		}
		updateSearchState();
	});

	refreshSelection();
	updateSearchState();
}
