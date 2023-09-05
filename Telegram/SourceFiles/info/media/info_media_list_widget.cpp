/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/media/info_media_list_widget.h"

#include "info/media/info_media_common.h"
#include "info/media/info_media_provider.h"
#include "info/media/info_media_list_section.h"
#include "info/downloads/info_downloads_provider.h"
#include "info/stories/info_stories_provider.h"
#include "info/info_controller.h"
#include "layout/layout_mosaic.h"
#include "layout/layout_selection.h"
#include "data/data_media_types.h"
#include "data/data_photo.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_peer_values.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "data/data_file_click_handler.h"
#include "data/data_file_origin.h"
#include "data/data_download_manager.h"
#include "data/data_forum_topic.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "history/history.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_service_message.h"
#include "media/stories/media_stories_controller.h" // ...TogglePinnedToast.
#include "media/stories/media_stories_share.h" // PrepareShareBox.
#include "window/window_session_controller.h"
#include "window/window_peer_menu.h"
#include "ui/widgets/popup_menu.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/delete_message_context_action.h"
#include "ui/chat/chat_style.h"
#include "ui/cached_round_corners.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/inactive_press.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "base/platform/base_platform_info.h"
#include "base/weak_ptr.h"
#include "base/call_delayed.h"
#include "media/player/media_player_instance.h"
#include "boxes/delete_messages_box.h"
#include "boxes/peer_list_controllers.h"
#include "core/file_utilities.h"
#include "core/application.h"
#include "ui/toast/toast.h"
#include "styles/style_overview.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_chat.h"

#include <QtWidgets/QApplication>
#include <QtGui/QClipboard>

namespace Info {
namespace Media {
namespace {

constexpr auto kMediaCountForSearch = 10;

} // namespace

struct ListWidget::DateBadge {
	DateBadge(Type type, Fn<void()> checkCallback, Fn<void()> hideCallback);

	SingleQueuedInvokation check;
	base::Timer hideTimer;
	Ui::Animations::Simple opacity;
	Ui::CornersPixmaps corners;
	bool goodType = false;
	bool shown = false;
	QString text;
	int textWidth = 0;
	QRect rect;
};

[[nodiscard]] std::unique_ptr<ListProvider> MakeProvider(
		not_null<AbstractController*> controller) {
	if (controller->isDownloads()) {
		return std::make_unique<Downloads::Provider>(controller);
	} else if (controller->storiesPeer()) {
		return std::make_unique<Stories::Provider>(controller);
	}
	return std::make_unique<Provider>(controller);
}

bool ListWidget::isAfter(
		const MouseState &a,
		const MouseState &b) const {
	if (a.item != b.item) {
		return _provider->isAfter(a.item, b.item);
	}
	const auto xAfter = a.cursor.x() - b.cursor.x();
	const auto yAfter = a.cursor.y() - b.cursor.y();
	return (xAfter + yAfter >= 0);
}

bool ListWidget::SkipSelectFromItem(const MouseState &state) {
	if (state.cursor.y() >= state.size.height()
		|| state.cursor.x() >= state.size.width()) {
		return true;
	}
	return false;
}

bool ListWidget::SkipSelectTillItem(const MouseState &state) {
	if (state.cursor.x() < 0 || state.cursor.y() < 0) {
		return true;
	}
	return false;
}

ListWidget::DateBadge::DateBadge(
	Type type,
	Fn<void()> checkCallback,
	Fn<void()> hideCallback)
: check(std::move(checkCallback))
, hideTimer(std::move(hideCallback))
, goodType(type == Type::Photo
	|| type == Type::Video
	|| type == Type::PhotoVideo
	|| type == Type::GIF) {
}

ListWidget::ListWidget(
	QWidget *parent,
	not_null<AbstractController*> controller)
: RpWidget(parent)
, _controller(controller)
, _provider(MakeProvider(_controller))
, _dateBadge(std::make_unique<DateBadge>(
		_provider->type(),
		[=] { scrollDateCheck(); },
		[=] { scrollDateHide(); })) {
	start();
}

Main::Session &ListWidget::session() const {
	return _controller->session();
}

void ListWidget::start() {
	setMouseTracking(true);

	_controller->setSearchEnabledByContent(false);

	_provider->layoutRemoved(
	) | rpl::start_with_next([=](not_null<BaseLayout*> layout) {
		if (_overLayout == layout) {
			_overLayout = nullptr;
		}
		_heavyLayouts.remove(layout);
	}, lifetime());

	_provider->refreshed(
	) | rpl::start_with_next([=] {
		refreshRows();
	}, lifetime());

	if (_controller->isDownloads()) {
		_provider->refreshViewer();

		_controller->searchQueryValue(
		) | rpl::start_with_next([this](QString &&query) {
			_provider->setSearchQuery(std::move(query));
		}, lifetime());
	} else if (_controller->storiesPeer()) {
		trackSession(&session());
		restart();
	} else {
		trackSession(&session());

		_controller->mediaSourceQueryValue(
		) | rpl::start_with_next([this] {
			restart();
		}, lifetime());

		if (_provider->type() == Type::File) {
			// For downloads manager.
			session().data().itemVisibilityQueries(
			) | rpl::filter([=](
					const Data::Session::ItemVisibilityQuery &query) {
				return _provider->isPossiblyMyItem(query.item)
					&& isVisible();
			}) | rpl::start_with_next([=](
					const Data::Session::ItemVisibilityQuery &query) {
				if (const auto found = findItemByItem(query.item)) {
					if (itemVisible(found->layout)) {
						*query.isVisible = true;
					}
				}
			}, lifetime());
		}
	}

	setupSelectRestriction();
}

void ListWidget::subscribeToSession(
		not_null<Main::Session*> session,
		rpl::lifetime &lifetime) {
	session->downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		update();
	}, lifetime);

	session->data().itemLayoutChanged(
	) | rpl::start_with_next([this](auto item) {
		itemLayoutChanged(item);
	}, lifetime);

	session->data().itemRemoved(
	) | rpl::start_with_next([this](auto item) {
		itemRemoved(item);
	}, lifetime);

	session->data().itemRepaintRequest(
	) | rpl::start_with_next([this](auto item) {
		repaintItem(item);
	}, lifetime);
}

void ListWidget::setupSelectRestriction() {
	_provider->hasSelectRestrictionChanges(
	) | rpl::filter([=] {
		return _provider->hasSelectRestriction() && hasSelectedItems();
	}) | rpl::start_with_next([=] {
		clearSelected();
		if (_mouseAction == MouseAction::PrepareSelect) {
			mouseActionCancel();
		}
	}, lifetime());
}

rpl::producer<int> ListWidget::scrollToRequests() const {
	return _scrollToRequests.events();
}

rpl::producer<SelectedItems> ListWidget::selectedListValue() const {
	return _selectedListStream.events_starting_with(
		collectSelectedItems());
}

void ListWidget::selectionAction(SelectionAction action) {
	switch (action) {
	case SelectionAction::Clear: clearSelected(); return;
	case SelectionAction::Forward: forwardSelected(); return;
	case SelectionAction::Delete: deleteSelected(); return;
	case SelectionAction::ToggleStoryPin: toggleStoryPinSelected(); return;
	}
}

QRect ListWidget::getCurrentSongGeometry() {
	const auto type = AudioMsgId::Type::Song;
	const auto current = ::Media::Player::instance()->current(type);
	if (const auto document = current.audio()) {
		const auto contextId = current.contextId();
		if (const auto item = document->owner().message(contextId)) {
			if (const auto found = findItemByItem(item)) {
				return found->geometry;
			}
		}
	}
	return QRect(0, 0, width(), 0);
}

void ListWidget::restart() {
	mouseActionCancel();

	_overLayout = nullptr;
	_sections.clear();
	_heavyLayouts.clear();

	_provider->restart();
}

void ListWidget::itemRemoved(not_null<const HistoryItem*> item) {
	if (!_provider->isMyItem(item)) {
		return;
	}

	if (_contextItem == item) {
		_contextItem = nullptr;
	}

	auto needHeightRefresh = false;
	auto sectionIt = findSectionByItem(item);
	if (sectionIt != _sections.end()) {
		if (sectionIt->removeItem(item)) {
			if (sectionIt->empty()) {
				_sections.erase(sectionIt);
			}
			needHeightRefresh = true;
		}
	}

	if (isItemLayout(item, _overLayout)) {
		_overLayout = nullptr;
	}
	_dragSelected.remove(item);

	if (_pressState.item == item) {
		mouseActionCancel();
	}
	if (_overState.item == item) {
		_mouseAction = MouseAction::None;
		_overState = {};
	}

	if (const auto i = _selected.find(item); i != _selected.cend()) {
		removeItemSelection(i);
	}

	if (needHeightRefresh) {
		refreshHeight();
	}
	mouseActionUpdate(_mousePosition);
}

auto ListWidget::collectSelectedItems() const -> SelectedItems {
	auto convert = [&](
			not_null<const HistoryItem*> item,
			const SelectionData &selection) {
		auto result = SelectedItem(item->globalId());
		result.canDelete = selection.canDelete;
		result.canForward = selection.canForward;
		result.canToggleStoryPin = selection.canToggleStoryPin;
		return result;
	};
	auto transformation = [&](const auto &item) {
		return convert(item.first, item.second);
	};
	auto items = SelectedItems(_provider->type());
	if (hasSelectedItems()) {
		items.list.reserve(_selected.size());
		std::transform(
			_selected.begin(),
			_selected.end(),
			std::back_inserter(items.list),
			transformation);
	}
	if (_controller->storiesPeer() && items.list.size() > 1) {
		// Don't allow forwarding more than one story.
		for (auto &entry : items.list) {
			entry.canForward = false;
		}
	}
	return items;
}

MessageIdsList ListWidget::collectSelectedIds() const {
	return collectSelectedIds(collectSelectedItems());
}

MessageIdsList ListWidget::collectSelectedIds(
		const SelectedItems &items) const {
	const auto session = &_controller->session();
	return ranges::views::all(
		items.list
	) | ranges::views::transform([](auto &&item) {
		return item.globalId;
	}) | ranges::views::filter([&](const GlobalMsgId &globalId) {
		return (globalId.sessionUniqueId == session->uniqueId())
			&& (session->data().message(globalId.itemId) != nullptr);
	}) | ranges::views::transform([](const GlobalMsgId &globalId) {
		return globalId.itemId;
	}) | ranges::to_vector;
}

void ListWidget::pushSelectedItems() {
	_selectedListStream.fire(collectSelectedItems());
}

bool ListWidget::hasSelected() const {
	return !_selected.empty();
}

bool ListWidget::isSelectedItem(
		const SelectedMap::const_iterator &i) const {
	return (i != _selected.end())
		&& (i->second.text == FullSelection);
}

void ListWidget::removeItemSelection(
		const SelectedMap::const_iterator &i) {
	Expects(i != _selected.cend());
	_selected.erase(i);
	if (_selected.empty()) {
		update();
	}
	pushSelectedItems();
}

bool ListWidget::hasSelectedText() const {
	return hasSelected()
		&& !hasSelectedItems();
}

bool ListWidget::hasSelectedItems() const {
	return isSelectedItem(_selected.cbegin());
}

void ListWidget::itemLayoutChanged(
		not_null<const HistoryItem*> item) {
	if (isItemLayout(item, _overLayout)) {
		mouseActionUpdate();
	}
}

void ListWidget::repaintItem(const HistoryItem *item) {
	if (const auto found = findItemByItem(item)) {
		repaintItem(found->geometry);
	}
}

void ListWidget::repaintItem(const BaseLayout *item) {
	if (item) {
		repaintItem(item->getItem());
	}
}

void ListWidget::repaintItem(not_null<const BaseLayout*> item) {
	repaintItem(item->getItem());
}

void ListWidget::repaintItem(QRect itemGeometry) {
	rtlupdate(itemGeometry);
}

bool ListWidget::isItemLayout(
		not_null<const HistoryItem*> item,
		BaseLayout *layout) const {
	return layout && (layout->getItem() == item);
}

void ListWidget::registerHeavyItem(not_null<const BaseLayout*> item) {
	if (!_heavyLayouts.contains(item)) {
		_heavyLayouts.emplace(item);
		_heavyLayoutsInvalidated = true;
	}
}

void ListWidget::unregisterHeavyItem(not_null<const BaseLayout*> item) {
	const auto i = _heavyLayouts.find(item);
	if (i != _heavyLayouts.end()) {
		_heavyLayouts.erase(i);
		_heavyLayoutsInvalidated = true;
	}
}

bool ListWidget::itemVisible(not_null<const BaseLayout*> item) {
	if (const auto &found = findItemByItem(item->getItem())) {
		const auto geometry = found->geometry;
		return (geometry.top() < _visibleBottom)
			&& (geometry.top() + geometry.height() > _visibleTop);
	}
	return true;
}

QString ListWidget::tooltipText() const {
	if (const auto link = ClickHandler::getActive()) {
		return link->tooltip();
	}
	return QString();
}

QPoint ListWidget::tooltipPos() const {
	return _mousePosition;
}

bool ListWidget::tooltipWindowActive() const {
	return Ui::AppInFocus() && Ui::InFocusChain(window());
}

void ListWidget::openPhoto(not_null<PhotoData*> photo, FullMsgId id) {
	using namespace Data;

	const auto tab = _controller->storiesTab();
	const auto context = (tab == Stories::Tab::Archive)
		? Data::StoriesContext{ Data::StoriesContextArchive() }
		: Data::StoriesContext{ Data::StoriesContextSaved() };
	_controller->parentController()->openPhoto(
		photo,
		{ id, topicRootId() },
		_controller->storiesPeer() ? &context : nullptr);
}

void ListWidget::openDocument(
		not_null<DocumentData*> document,
		FullMsgId id,
		bool showInMediaView) {
	const auto tab = _controller->storiesTab();
	const auto context = (tab == Stories::Tab::Archive)
		? Data::StoriesContext{ Data::StoriesContextArchive() }
		: Data::StoriesContext{ Data::StoriesContextSaved() };
	_controller->parentController()->openDocument(
		document,
		showInMediaView,
		{ id, topicRootId() },
		_controller->storiesPeer() ? &context : nullptr);
}

void ListWidget::trackSession(not_null<Main::Session*> session) {
	if (_trackedSessions.contains(session)) {
		return;
	}
	auto &lifetime = _trackedSessions.emplace(session).first->second;
	subscribeToSession(session, lifetime);
	session->account().sessionChanges(
	) | rpl::take(1) | rpl::start_with_next([=] {
		_trackedSessions.remove(session);
	}, lifetime);
}

void ListWidget::refreshRows() {
	saveScrollState();

	_sections.clear();
	_sections = _provider->fillSections(this);

	if (_controller->isDownloads() && !_sections.empty()) {
		for (const auto &item : _sections.back().items()) {
			trackSession(&item->getItem()->history()->session());
		}
	}

	if (const auto count = _provider->fullCount()) {
		if (*count > kMediaCountForSearch) {
			_controller->setSearchEnabledByContent(true);
		}
	}

	resizeToWidth(width());
	restoreScrollState();
	mouseActionUpdate();
	update();
}

bool ListWidget::preventAutoHide() const {
	return (_contextMenu != nullptr) || (_actionBoxWeak != nullptr);
}

void ListWidget::saveState(not_null<Memento*> memento) {
	_provider->saveState(memento, countScrollState());
	_trackedSessions.clear();
}

void ListWidget::restoreState(not_null<Memento*> memento) {
	_provider->restoreState(memento, [&](ListScrollTopState state) {
		_scrollTopState = state;
	});
}

int ListWidget::resizeGetHeight(int newWidth) {
	if (newWidth > 0) {
		for (auto &section : _sections) {
			section.resizeToWidth(newWidth);
		}
	}
	return recountHeight();
}

auto ListWidget::findItemByPoint(QPoint point) const -> FoundItem {
	Expects(!_sections.empty());

	auto sectionIt = findSectionAfterTop(point.y());
	if (sectionIt == _sections.end()) {
		--sectionIt;
	}
	auto shift = QPoint(0, sectionIt->top());
	return foundItemInSection(
		sectionIt->findItemByPoint(point - shift),
		*sectionIt);
}

auto ListWidget::findItemByItem(const HistoryItem *item)
-> std::optional<FoundItem> {
	if (!item || !_provider->isPossiblyMyItem(item)) {
		return std::nullopt;
	}
	auto sectionIt = findSectionByItem(item);
	if (sectionIt != _sections.end()) {
		if (const auto found = sectionIt->findItemByItem(item)) {
			return foundItemInSection(*found, *sectionIt);
		}
	}
	return std::nullopt;
}

auto ListWidget::findItemDetails(not_null<BaseLayout*> item)
-> FoundItem {
	const auto sectionIt = findSectionByItem(item->getItem());
	Assert(sectionIt != _sections.end());
	return foundItemInSection(sectionIt->findItemDetails(item), *sectionIt);
}

auto ListWidget::foundItemInSection(
	const FoundItem &item,
	const Section &section) const
-> FoundItem {
	return {
		item.layout,
		item.geometry.translated(0, section.top()),
		item.exact,
	};
}

void ListWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;

	checkMoveToOtherViewer();
	clearHeavyItems();

	if (_dateBadge->goodType) {
		updateDateBadgeFor(_visibleTop);
		if (!_visibleTop) {
			if (_dateBadge->shown) {
				scrollDateHide();
			} else {
				update(_dateBadge->rect);
			}
		} else {
			_dateBadge->check.call();
		}
	}

	session().data().itemVisibilitiesUpdated();
}

void ListWidget::updateDateBadgeFor(int top) {
	if (_sections.empty()) {
		return;
	}
	const auto layout = findItemByPoint({ st::infoMediaSkip, top }).layout;
	const auto rectHeight = st::msgServiceMargin.top()
		+ st::msgServicePadding.top()
		+ st::msgServiceFont->height
		+ st::msgServicePadding.bottom();

	_dateBadge->text = ItemDateText(layout->getItem(), false);
	_dateBadge->textWidth = st::msgServiceFont->width(_dateBadge->text);
	_dateBadge->rect = QRect(0, top, width(), rectHeight);
}

void ListWidget::scrollDateCheck() {
	if (!_dateBadge->shown) {
		toggleScrollDateShown();
	}
	_dateBadge->hideTimer.callOnce(st::infoScrollDateHideTimeout);
}

void ListWidget::scrollDateHide() {
	if (_dateBadge->shown) {
		toggleScrollDateShown();
	}
}

void ListWidget::toggleScrollDateShown() {
	_dateBadge->shown = !_dateBadge->shown;
	_dateBadge->opacity.start(
		[=] { update(_dateBadge->rect); },
		_dateBadge->shown ? 0. : 1.,
		_dateBadge->shown ? 1. : 0.,
		st::infoDateFadeDuration);
}

void ListWidget::checkMoveToOtherViewer() {
	auto visibleHeight = (_visibleBottom - _visibleTop);
	if (width() <= 0
		|| visibleHeight <= 0
		|| _sections.empty()
		|| _scrollTopState.item) {
		return;
	}

	auto topItem = findItemByPoint({ st::infoMediaSkip, _visibleTop });
	auto bottomItem = findItemByPoint({ st::infoMediaSkip, _visibleBottom });

	auto preloadBefore = kPreloadIfLessThanScreens * visibleHeight;
	auto preloadTop = (_visibleTop < preloadBefore);
	auto preloadBottom = (height() - _visibleBottom < preloadBefore);

	_provider->checkPreload(
		{ width(), visibleHeight },
		topItem.layout,
		bottomItem.layout,
		preloadTop,
		preloadBottom);
}

void ListWidget::clearHeavyItems() {
	const auto visibleHeight = _visibleBottom - _visibleTop;
	if (!visibleHeight) {
		return;
	}
	_heavyLayoutsInvalidated = false;
	const auto above = _visibleTop - visibleHeight;
	const auto below = _visibleBottom + visibleHeight;
	for (auto i = _heavyLayouts.begin(); i != _heavyLayouts.end();) {
		const auto item = const_cast<BaseLayout*>(i->get());
		const auto rect = findItemDetails(item).geometry;
		if (rect.top() + rect.height() <= above || rect.top() >= below) {
			i = _heavyLayouts.erase(i);
			item->clearHeavyPart();
			if (_heavyLayoutsInvalidated) {
				break;
			}
		} else {
			++i;
		}
	}
	if (_heavyLayoutsInvalidated) {
		clearHeavyItems();
	}
}

ListScrollTopState ListWidget::countScrollState() const {
	if (_sections.empty() || _visibleTop <= 0) {
		return {};
	}
	const auto topItem = findItemByPoint({ st::infoMediaSkip, _visibleTop });
	const auto item = topItem.layout->getItem();
	return {
		.position = _provider->scrollTopStatePosition(item),
		.item = item,
		.shift = _visibleTop - topItem.geometry.y(),
	};
}

void ListWidget::saveScrollState() {
	if (!_scrollTopState.item) {
		_scrollTopState = countScrollState();
	}
}

void ListWidget::restoreScrollState() {
	if (_sections.empty() || !_scrollTopState.position) {
		return;
	}
	_scrollTopState.item = _provider->scrollTopStateItem(_scrollTopState);
	if (!_scrollTopState.item) {
		return;
	}
	auto sectionIt = findSectionByItem(_scrollTopState.item);
	if (sectionIt == _sections.end()) {
		--sectionIt;
	}
	const auto found = sectionIt->findItemByItem(_scrollTopState.item);
	if (!found) {
		return;
	}
	auto item = foundItemInSection(*found, *sectionIt);
	auto newVisibleTop = item.geometry.y() + _scrollTopState.shift;
	if (_visibleTop != newVisibleTop) {
		_scrollToRequests.fire_copy(newVisibleTop);
	}
	_scrollTopState = ListScrollTopState();
}

MsgId ListWidget::topicRootId() const {
	const auto topic = _controller->key().topic();
	return topic ? topic->rootId() : MsgId(0);
}

QMargins ListWidget::padding() const {
	return st::infoMediaMargin;
}

void ListWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto outerWidth = width();
	auto clip = e->rect();
	auto ms = crl::now();
	auto fromSectionIt = findSectionAfterTop(clip.y());
	auto tillSectionIt = findSectionAfterBottom(
		fromSectionIt,
		clip.y() + clip.height());
	const auto window = _controller->parentController();
	const auto paused = window->isGifPausedAtLeastFor(
		Window::GifPauseReason::Layer);
	auto context = ListContext{
		Overview::Layout::PaintContext(ms, hasSelectedItems(), paused),
		&_selected,
		&_dragSelected,
		_dragSelectAction
	};
	for (auto it = fromSectionIt; it != tillSectionIt; ++it) {
		auto top = it->top();
		p.translate(0, top);
		it->paint(p, context, clip.translated(0, -top), outerWidth);
		p.translate(0, -top);
	}
	if (fromSectionIt != _sections.end()) {
		fromSectionIt->paintFloatingHeader(p, _visibleTop, outerWidth);
	}

	if (_dateBadge->goodType && clip.intersects(_dateBadge->rect)) {
		const auto scrollDateOpacity =
			_dateBadge->opacity.value(_dateBadge->shown ? 1. : 0.);
		if (scrollDateOpacity > 0.) {
			p.setOpacity(scrollDateOpacity);
			if (_dateBadge->corners.p[0].isNull()) {
				_dateBadge->corners = Ui::PrepareCornerPixmaps(
					Ui::HistoryServiceMsgRadius(),
					st::roundedBg);
			}
			HistoryView::ServiceMessagePainter::PaintDate(
				p,
				st::roundedBg,
				_dateBadge->corners,
				st::roundedFg,
				_dateBadge->text,
				_dateBadge->textWidth,
				_visibleTop,
				outerWidth,
				false);
		}
	}
}

void ListWidget::mousePressEvent(QMouseEvent *e) {
	if (_contextMenu) {
		e->accept();
		return; // ignore mouse press, that was hiding context menu
	}
	mouseActionStart(e->globalPos(), e->button());
}

void ListWidget::mouseMoveEvent(QMouseEvent *e) {
	auto buttonsPressed = (e->buttons() & (Qt::LeftButton | Qt::MiddleButton));
	if (!buttonsPressed && _mouseAction != MouseAction::None) {
		mouseReleaseEvent(e);
	}
	mouseActionUpdate(e->globalPos());
}

void ListWidget::mouseReleaseEvent(QMouseEvent *e) {
	mouseActionFinish(e->globalPos(), e->button());
	if (!rect().contains(e->pos())) {
		leaveEvent(e);
	}
}

void ListWidget::mouseDoubleClickEvent(QMouseEvent *e) {
	mouseActionStart(e->globalPos(), e->button());
	trySwitchToWordSelection();
}

void ListWidget::showContextMenu(
		QContextMenuEvent *e,
		ContextMenuSource source) {
	if (_contextMenu) {
		_contextMenu = nullptr;
		repaintItem(_contextItem);
	}
	if (e->reason() == QContextMenuEvent::Mouse) {
		mouseActionUpdate(e->globalPos());
	}

	const auto item = _overState.item;
	if (!item || !_overState.inside) {
		return;
	}
	_contextItem = item;
	const auto globalId = item->globalId();

	enum class SelectionState {
		NoSelectedItems,
		NotOverSelectedItems,
		OverSelectedItems,
		NotOverSelectedText,
		OverSelectedText,
	};
	auto overSelected = SelectionState::NoSelectedItems;
	if (source == ContextMenuSource::Touch) {
		if (hasSelectedItems()) {
			overSelected = SelectionState::OverSelectedItems;
		} else if (hasSelectedText()) {
			overSelected = SelectionState::OverSelectedItems;
		}
	} else if (hasSelectedText()) {
		// #TODO text selection
	} else if (hasSelectedItems()) {
		auto it = _selected.find(_overState.item);
		if (isSelectedItem(it) && _overState.inside) {
			overSelected = SelectionState::OverSelectedItems;
		} else {
			overSelected = SelectionState::NotOverSelectedItems;
		}
	}

	auto canDeleteAll = [&] {
		return ranges::none_of(_selected, [](auto &&item) {
			return !item.second.canDelete;
		});
	};
	auto canForwardAll = [&] {
		return ranges::none_of(_selected, [](auto &&item) {
			return !item.second.canForward;
		}) && (!_controller->key().storiesPeer() || _selected.size() == 1);
	};
	auto canToggleStoryPinAll = [&] {
		return ranges::none_of(_selected, [](auto &&item) {
			return !item.second.canToggleStoryPin;
		});
	};

	auto link = ClickHandler::getActive();

	_contextMenu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	if (item->isHistoryEntry()) {
		_contextMenu->addAction(
			tr::lng_context_to_msg(tr::now),
			[=] {
				if (const auto item = MessageByGlobalId(globalId)) {
					JumpToMessageClickHandler(item)->onClick({});
				}
			},
			&st::menuIconShowInChat);
	}

	const auto lnkPhoto = link
		? reinterpret_cast<PhotoData*>(
			link->property(kPhotoLinkMediaProperty).toULongLong())
		: nullptr;
	const auto lnkDocument = link
		? reinterpret_cast<DocumentData*>(
			link->property(kDocumentLinkMediaProperty).toULongLong())
		: nullptr;
	if (lnkPhoto || lnkDocument) {
		auto [isVideo, isVoice, isAudio] = [&] {
			if (lnkDocument) {
				return std::make_tuple(
					lnkDocument->isVideoFile(),
					lnkDocument->isVoiceMessage(),
					lnkDocument->isAudioFile()
				);
			}
			return std::make_tuple(false, false, false);
		}();

		if (lnkPhoto) {
		} else {
			if (lnkDocument->loading()) {
				_contextMenu->addAction(
					tr::lng_context_cancel_download(tr::now),
					[lnkDocument] {
						lnkDocument->cancel();
					},
					&st::menuIconCancel);
			} else {
				const auto filepath = _provider->showInFolderPath(
					item,
					lnkDocument);
				if (!filepath.isEmpty()) {
					auto handler = base::fn_delayed(
						st::defaultDropdownMenu.menu.ripple.hideDuration,
						this,
						[filepath] {
							File::ShowInFolder(filepath);
						});
					_contextMenu->addAction(
						(Platform::IsMac()
							? tr::lng_context_show_in_finder(tr::now)
							: tr::lng_context_show_in_folder(tr::now)),
						std::move(handler),
						&st::menuIconShowInFolder);
				}
				auto handler = base::fn_delayed(
					st::defaultDropdownMenu.menu.ripple.hideDuration,
					this,
					[=] {
						DocumentSaveClickHandler::SaveAndTrack(
							globalId.itemId,
							lnkDocument,
							DocumentSaveClickHandler::Mode::ToNewFile);
					});
				if (_provider->allowSaveFileAs(item, lnkDocument)) {
					_contextMenu->addAction(
						(isVideo
							? tr::lng_context_save_video(tr::now)
							: isVoice
							? tr::lng_context_save_audio(tr::now)
							: isAudio
							? tr::lng_context_save_audio_file(tr::now)
							: tr::lng_context_save_file(tr::now)),
						std::move(handler),
						&st::menuIconDownload);
				}
			}
		}
	} else if (link) {
		const auto actionText = link->copyToClipboardContextItemText();
		if (!actionText.isEmpty()) {
			_contextMenu->addAction(
				actionText,
				[text = link->copyToClipboardText()] {
					QGuiApplication::clipboard()->setText(text);
				},
				&st::menuIconCopy);
		}
	}
	if (overSelected == SelectionState::OverSelectedItems) {
		if (canToggleStoryPinAll()) {
			const auto tab = _controller->key().storiesTab();
			const auto pin = (tab == Stories::Tab::Archive);
			_contextMenu->addAction(
				(pin
					? tr::lng_mediaview_save_to_profile
					: tr::lng_archived_add)(tr::now),
				crl::guard(this, [this] { toggleStoryPinSelected(); }),
				(pin
					? &st::menuIconStoriesSave
					: &st::menuIconStoriesArchive));
		}
		if (canForwardAll()) {
			_contextMenu->addAction(
				tr::lng_context_forward_selected(tr::now),
				crl::guard(this, [this] {
					forwardSelected();
				}),
				&st::menuIconForward);
		}
		if (canDeleteAll()) {
			_contextMenu->addAction(
				(_controller->isDownloads()
					? tr::lng_context_delete_from_disk(tr::now)
					: tr::lng_context_delete_selected(tr::now)),
				crl::guard(this, [this] {
					deleteSelected();
				}),
				&st::menuIconDelete);
		}
		_contextMenu->addAction(
			tr::lng_context_clear_selection(tr::now),
			crl::guard(this, [this] {
				clearSelected();
			}),
			&st::menuIconSelect);
	} else {
		if (overSelected != SelectionState::NotOverSelectedItems) {
			const auto selectionData = _provider->computeSelectionData(
				item,
				FullSelection);
			if (selectionData.canToggleStoryPin) {
				const auto tab = _controller->key().storiesTab();
				const auto pin = (tab == Stories::Tab::Archive);
				_contextMenu->addAction(
					(pin
						? tr::lng_mediaview_save_to_profile
						: tr::lng_mediaview_archive_story)(tr::now),
					crl::guard(this, [=] {
						toggleStoryPin({ 1, globalId.itemId });
					}),
					(pin
						? &st::menuIconStoriesSave
						: &st::menuIconStoriesArchive));
			}
			if (selectionData.canForward) {
				_contextMenu->addAction(
					tr::lng_context_forward_msg(tr::now),
					crl::guard(this, [=] { forwardItem(globalId); }),
					&st::menuIconForward);
			}
			if (selectionData.canDelete) {
				if (_controller->isDownloads()) {
					_contextMenu->addAction(
						tr::lng_context_delete_from_disk(tr::now),
						crl::guard(this, [=] { deleteItem(globalId); }),
						&st::menuIconDelete);
				} else {
					_contextMenu->addAction(Ui::DeleteMessageContextAction(
						_contextMenu->menu(),
						crl::guard(this, [=] { deleteItem(globalId); }),
						item->ttlDestroyAt(),
						[=] { _contextMenu = nullptr; }));
				}
			}
		}
		if (const auto peer = _controller->key().storiesPeer()) {
			if (!peer->isSelf() && IsStoryMsgId(globalId.itemId.msg)) {
				const auto storyId = FullStoryId{
					globalId.itemId.peer,
					StoryIdFromMsgId(globalId.itemId.msg),
				};
				_contextMenu->addAction(
					tr::lng_profile_report(tr::now),
					[=] { ::Media::Stories::ReportRequested(
						_controller->uiShow(),
						storyId); },
					&st::menuIconReport);
			}
		}
		if (!_provider->hasSelectRestriction()) {
			_contextMenu->addAction(
				tr::lng_context_select_msg(tr::now),
				crl::guard(this, [=] {
					if (hasSelectedText()) {
						clearSelected();
					} else if (_selected.size() == MaxSelectedItems) {
						return;
					} else if (_selected.empty()) {
						update();
					}
					applyItemSelection(
						MessageByGlobalId(globalId),
						FullSelection);
				}),
				&st::menuIconSelect);
		}
	}

	_contextMenu->setDestroyedCallback(crl::guard(
		this,
		[=] {
			mouseActionUpdate(QCursor::pos());
			repaintItem(MessageByGlobalId(globalId));
			_checkForHide.fire({});
		}));
	_contextMenu->popup(e->globalPos());
	e->accept();
}

void ListWidget::contextMenuEvent(QContextMenuEvent *e) {
	showContextMenu(
		e,
		(e->reason() == QContextMenuEvent::Mouse)
			? ContextMenuSource::Mouse
			: ContextMenuSource::Other);
}

void ListWidget::forwardSelected() {
	if (auto items = collectSelectedIds(); !items.empty()) {
		forwardItems(std::move(items));
	}
}

void ListWidget::forwardItem(GlobalMsgId globalId) {
	const auto session = &_controller->session();
	if (globalId.sessionUniqueId == session->uniqueId()) {
		if (const auto item = session->data().message(globalId.itemId)) {
			forwardItems({ 1, item->fullId() });
		}
	}
}

void ListWidget::forwardItems(MessageIdsList &&items) {
	if (_controller->storiesPeer()) {
		if (items.size() == 1 && IsStoryMsgId(items.front().msg)) {
			const auto id = items.front();
			_controller->parentController()->show(
				::Media::Stories::PrepareShareBox(
					_controller->parentController()->uiShow(),
					{ id.peer, StoryIdFromMsgId(id.msg) }));
		}
	} else {
		auto callback = [weak = Ui::MakeWeak(this)] {
			if (const auto strong = weak.data()) {
				strong->clearSelected();
			}
		};
		setActionBoxWeak(Window::ShowForwardMessagesBox(
			_controller,
			std::move(items),
			std::move(callback)));
	}
}

void ListWidget::deleteSelected() {
	deleteItems(collectSelectedItems(), crl::guard(this, [=]{
		clearSelected();
	}));
}

void ListWidget::toggleStoryPinSelected() {
	toggleStoryPin(collectSelectedIds(), crl::guard(this, [=] {
		clearSelected();
	}));
}

void ListWidget::toggleStoryPin(
		MessageIdsList &&items,
		Fn<void()> confirmed) {
	auto list = std::vector<FullStoryId>();
	for (const auto &id : items) {
		if (IsStoryMsgId(id.msg)) {
			list.push_back({ id.peer, StoryIdFromMsgId(id.msg) });
		}
	}
	if (list.empty()) {
		return;
	}
	const auto channel = peerIsChannel(list.front().peer);
	const auto count = int(list.size());
	const auto pin = (_controller->storiesTab() == Stories::Tab::Archive);
	const auto controller = _controller;
	const auto sure = [=](Fn<void()> close) {
		using namespace ::Media::Stories;
		controller->session().data().stories().togglePinnedList(list, pin);
		controller->showToast(
			PrepareTogglePinnedToast(channel, count, pin));
		close();
		if (confirmed) {
			confirmed();
		}
	};
	const auto onePhrase = pin
		? (channel
			? tr::lng_stories_channel_save_sure
			: tr::lng_stories_save_sure)
		: (channel
			? tr::lng_stories_channel_archive_sure
			: tr::lng_stories_archive_sure);
	const auto manyPhrase = pin
		? (channel
			? tr::lng_stories_channel_save_sure_many
			: tr::lng_stories_save_sure_many)
		: (channel
			? tr::lng_stories_channel_archive_sure_many
			: tr::lng_stories_archive_sure_many);
	_controller->parentController()->show(Ui::MakeConfirmBox({
		.text = (count == 1
			? onePhrase()
			: manyPhrase(lt_count, rpl::single(count) | tr::to_count())),
		.confirmed = sure,
		.confirmText = tr::lng_box_ok(),
	}));
}

void ListWidget::deleteItem(GlobalMsgId globalId) {
	if (const auto item = MessageByGlobalId(globalId)) {
		auto items = SelectedItems(_provider->type());
		items.list.push_back(SelectedItem(item->globalId()));
		const auto selectionData = _provider->computeSelectionData(
			item,
			FullSelection);
		items.list.back().canDelete = selectionData.canDelete;
		deleteItems(std::move(items));
	}
}

void ListWidget::deleteItems(SelectedItems &&items, Fn<void()> confirmed) {
	const auto window = _controller->parentController();
	if (items.list.empty()) {
		return;
	} else if (_controller->isDownloads()) {
		const auto count = items.list.size();
		const auto allInCloud = ranges::all_of(items.list, [](
				const SelectedItem &entry) {
			const auto item = MessageByGlobalId(entry.globalId);
			return item && item->isHistoryEntry();
		});
		const auto phrase = (count == 1)
			? tr::lng_downloads_delete_sure_one(tr::now)
			: tr::lng_downloads_delete_sure(tr::now, lt_count, count);
		const auto added = !allInCloud
			? QString()
			: (count == 1
				? tr::lng_downloads_delete_in_cloud_one(tr::now)
				: tr::lng_downloads_delete_in_cloud(tr::now));
		const auto deleteSure = [=] {
			Ui::PostponeCall(this, [=] {
				if (const auto box = _actionBoxWeak.data()) {
					box->closeBox();
				}
			});
			const auto ids = ranges::views::all(
				items.list
			) | ranges::views::transform([](const SelectedItem &item) {
				return item.globalId;
			}) | ranges::to_vector;
			Core::App().downloadManager().deleteFiles(ids);
			if (confirmed) {
				confirmed();
			}
		};
		setActionBoxWeak(window->show(Ui::MakeConfirmBox({
			.text = phrase + (added.isEmpty() ? QString() : "\n\n" + added),
			.confirmed = deleteSure,
			.confirmText = tr::lng_box_delete(tr::now),
			.confirmStyle = &st::attentionBoxButton,
		})));
	} else if (_controller->storiesPeer()) {
		auto list = std::vector<FullStoryId>();
		for (const auto &item : items.list) {
			const auto id = item.globalId.itemId;
			if (IsStoryMsgId(id.msg)) {
				list.push_back({ id.peer, StoryIdFromMsgId(id.msg) });
			}
		}
		const auto session = &_controller->session();
		const auto sure = [=](Fn<void()> close) {
			session->data().stories().deleteList(list);
			close();
			if (confirmed) {
				confirmed();
			}
		};
		const auto count = int(list.size());
		window->show(Ui::MakeConfirmBox({
			.text = (count == 1
				? tr::lng_stories_delete_one_sure()
				: tr::lng_stories_delete_sure(
					lt_count,
					rpl::single(count) | tr::to_count())),
			.confirmed = sure,
			.confirmText = tr::lng_selected_delete(),
			.confirmStyle = &st::attentionBoxButton,
		}));
	} else if (auto list = collectSelectedIds(items); !list.empty()) {
		auto box = Box<DeleteMessagesBox>(
			&_controller->session(),
			std::move(list));
		const auto weak = box.data();
		window->show(std::move(box));
		setActionBoxWeak(weak);
		if (confirmed) {
			weak->setDeleteConfirmedCallback(std::move(confirmed));
		}
	}
}

void ListWidget::setActionBoxWeak(QPointer<Ui::BoxContent> box) {
	if ((_actionBoxWeak = box)) {
		_actionBoxWeakLifetime = _actionBoxWeak->alive(
		) | rpl::start_with_done([weak = Ui::MakeWeak(this)]{
			if (weak) {
				weak->_checkForHide.fire({});
			}
		});
	}
}

void ListWidget::trySwitchToWordSelection() {
	auto selectingSome = (_mouseAction == MouseAction::Selecting)
		&& hasSelectedText();
	auto willSelectSome = (_mouseAction == MouseAction::None)
		&& !hasSelectedItems();
	auto checkSwitchToWordSelection = _overLayout
		&& (_mouseSelectType == TextSelectType::Letters)
		&& (selectingSome || willSelectSome);
	if (checkSwitchToWordSelection) {
		switchToWordSelection();
	}
}

void ListWidget::switchToWordSelection() {
	Expects(_overLayout != nullptr);

	StateRequest request;
	request.flags |= Ui::Text::StateRequest::Flag::LookupSymbol;
	auto dragState = _overLayout->getState(_pressState.cursor, request);
	if (dragState.cursor != CursorState::Text) {
		return;
	}
	_mouseTextSymbol = dragState.symbol;
	_mouseSelectType = TextSelectType::Words;
	if (_mouseAction == MouseAction::None) {
		_mouseAction = MouseAction::Selecting;
		clearSelected();
		auto selStatus = TextSelection {
			dragState.symbol,
			dragState.symbol
		};
		applyItemSelection(_overState.item, selStatus);
	}
	mouseActionUpdate();

	_trippleClickPoint = _mousePosition;
	_trippleClickStartTime = crl::now();
}

void ListWidget::applyItemSelection(
		HistoryItem *item,
		TextSelection selection) {
	if (item
		&& ChangeItemSelection(
			_selected,
			item,
			_provider->computeSelectionData(item, selection))) {
		repaintItem(item);
		pushSelectedItems();
	}
}

void ListWidget::toggleItemSelection(not_null<HistoryItem*> item) {
	auto it = _selected.find(item);
	if (it == _selected.cend()) {
		applyItemSelection(item, FullSelection);
	} else {
		removeItemSelection(it);
	}
}

bool ListWidget::isItemUnderPressSelected() const {
	return itemUnderPressSelection() != _selected.end();
}

auto ListWidget::itemUnderPressSelection() -> SelectedMap::iterator {
	return (_pressState.item && _pressState.inside)
		? _selected.find(_pressState.item)
		: _selected.end();
}

auto ListWidget::itemUnderPressSelection() const
-> SelectedMap::const_iterator {
	return (_pressState.item && _pressState.inside)
		? _selected.find(_pressState.item)
		: _selected.end();
}

bool ListWidget::requiredToStartDragging(
		not_null<BaseLayout*> layout) const {
	if (_mouseCursorState == CursorState::Date) {
		return true;
	}
//	return dynamic_cast<Sticker*>(layout->getMedia());
	return false;
}

bool ListWidget::isPressInSelectedText(TextState state) const {
	if (state.cursor != CursorState::Text) {
		return false;
	}
	if (!hasSelectedText()
		|| !isItemUnderPressSelected()) {
		return false;
	}
	auto pressedSelection = itemUnderPressSelection();
	auto from = pressedSelection->second.text.from;
	auto to = pressedSelection->second.text.to;
	return (state.symbol >= from && state.symbol < to);
}

void ListWidget::clearSelected() {
	if (_selected.empty()) {
		return;
	}
	if (hasSelectedText()) {
		repaintItem(_selected.begin()->first);
		_selected.clear();
	} else {
		_selected.clear();
		pushSelectedItems();
		update();
	}
}

void ListWidget::validateTrippleClickStartTime() {
	if (_trippleClickStartTime) {
		auto elapsed = (crl::now() - _trippleClickStartTime);
		if (elapsed >= QApplication::doubleClickInterval()) {
			_trippleClickStartTime = 0;
		}
	}
}

void ListWidget::enterEventHook(QEnterEvent *e) {
	mouseActionUpdate(QCursor::pos());
	return RpWidget::enterEventHook(e);
}

void ListWidget::leaveEventHook(QEvent *e) {
	if (const auto item = _overLayout) {
		if (_overState.inside) {
			repaintItem(item);
			_overState.inside = false;
		}
	}
	ClickHandler::clearActive();
	Ui::Tooltip::Hide();
	if (!ClickHandler::getPressed() && _cursor != style::cur_default) {
		_cursor = style::cur_default;
		setCursor(_cursor);
	}
	return RpWidget::leaveEventHook(e);
}

QPoint ListWidget::clampMousePosition(QPoint position) const {
	return {
		std::clamp(position.x(), 0, qMax(0, width() - 1)),
		std::clamp(position.y(), _visibleTop, _visibleBottom - 1)
	};
}

void ListWidget::mouseActionUpdate(const QPoint &globalPosition) {
	if (_sections.empty() || _visibleBottom <= _visibleTop) {
		return;
	}

	_mousePosition = globalPosition;

	auto local = mapFromGlobal(_mousePosition);
	auto point = clampMousePosition(local);
	auto [layout, geometry, inside] = findItemByPoint(point);
	auto state = MouseState{
		layout->getItem(),
		geometry.size(),
		point - geometry.topLeft(),
		inside
	};
	if (_overLayout != layout) {
		repaintItem(_overLayout);
		_overLayout = layout;
		repaintItem(geometry);
	}
	_overState = state;

	TextState dragState;
	ClickHandlerHost *lnkhost = nullptr;
	auto inTextSelection = _overState.inside
		&& (_overState.item == _pressState.item)
		&& hasSelectedText();
	if (_overLayout) {
		auto cursorDeltaLength = [&] {
			auto cursorDelta = (_overState.cursor - _pressState.cursor);
			return cursorDelta.manhattanLength();
		};
		auto dragStartLength = [] {
			return QApplication::startDragDistance();
		};
		if (_overState.item != _pressState.item
			|| cursorDeltaLength() >= dragStartLength()) {
			if (_mouseAction == MouseAction::PrepareDrag) {
				_mouseAction = MouseAction::Dragging;
				InvokeQueued(this, [this] { performDrag(); });
			} else if (_mouseAction == MouseAction::PrepareSelect) {
				_mouseAction = MouseAction::Selecting;
			}
		}
		StateRequest request;
		if (_mouseAction == MouseAction::Selecting) {
			request.flags |= Ui::Text::StateRequest::Flag::LookupSymbol;
		} else {
			inTextSelection = false;
		}
		dragState = _overLayout->getState(_overState.cursor, request);
		lnkhost = _overLayout;
	}
	const auto lnkChanged = ClickHandler::setActive(dragState.link, lnkhost);
	if (lnkChanged || dragState.cursor != _mouseCursorState) {
		Ui::Tooltip::Hide();
	}
	if (dragState.link) {
		Ui::Tooltip::Show(1000, this);
	}

	if (_mouseAction == MouseAction::None) {
		_mouseCursorState = dragState.cursor;
		auto cursor = computeMouseCursor();
		if (_cursor != cursor) {
			setCursor(_cursor = cursor);
		}
	} else if (_mouseAction == MouseAction::Selecting) {
		if (inTextSelection) {
			auto second = dragState.symbol;
			if (dragState.afterSymbol && _mouseSelectType == TextSelectType::Letters) {
				++second;
			}
			auto selState = TextSelection {
				qMin(second, _mouseTextSymbol),
				qMax(second, _mouseTextSymbol)
			};
			if (_mouseSelectType != TextSelectType::Letters) {
				selState = _overLayout->adjustSelection(selState, _mouseSelectType);
			}
			applyItemSelection(_overState.item, selState);
			auto hasSelection = (selState == FullSelection)
				|| (selState.from != selState.to);
			if (!_wasSelectedText && hasSelection) {
				_wasSelectedText = true;
				setFocus();
			}
			clearDragSelection();
		} else if (_pressState.item) {
			updateDragSelection();
		}
	} else if (_mouseAction == MouseAction::Dragging) {
	}

	// #TODO scroll by drag
	//if (_mouseAction == MouseAction::Selecting) {
	//	_widget->checkSelectingScroll(mousePos);
	//} else {
	//	clearDragSelection();
	//	_widget->noSelectingScroll();
	//}
}

style::cursor ListWidget::computeMouseCursor() const {
	if (ClickHandler::getPressed() || ClickHandler::getActive()) {
		return style::cur_pointer;
	} else if (!hasSelectedItems()
		&& (_mouseCursorState == CursorState::Text)) {
		return style::cur_text;
	}
	return style::cur_default;
}

void ListWidget::updateDragSelection() {
	auto fromState = _pressState;
	auto tillState = _overState;
	auto swapStates = isAfter(fromState, tillState);
	if (swapStates) {
		std::swap(fromState, tillState);
	}
	if (!fromState.item
		|| !tillState.item
		|| _provider->hasSelectRestriction()) {
		clearDragSelection();
		return;
	}
	_provider->applyDragSelection(
		_dragSelected,
		fromState.item,
		SkipSelectFromItem(fromState),
		tillState.item,
		SkipSelectTillItem(tillState));
	_dragSelectAction = [&] {
		if (_dragSelected.empty()) {
			return DragSelectAction::None;
		}
		auto &[firstDragItem, data] = swapStates
			? _dragSelected.front()
			: _dragSelected.back();
		if (isSelectedItem(_selected.find(firstDragItem))) {
			return DragSelectAction::Deselecting;
		} else {
			return DragSelectAction::Selecting;
		}
	}();
	if (!_wasSelectedText
		&& !_dragSelected.empty()
		&& _dragSelectAction == DragSelectAction::Selecting) {
		_wasSelectedText = true;
		setFocus();
	}
	update();
}

void ListWidget::clearDragSelection() {
	_dragSelectAction = DragSelectAction::None;
	if (!_dragSelected.empty()) {
		_dragSelected.clear();
		update();
	}
}

void ListWidget::mouseActionStart(
		const QPoint &globalPosition,
		Qt::MouseButton button) {
	mouseActionUpdate(globalPosition);
	if (button != Qt::LeftButton) {
		return;
	}

	ClickHandler::pressed();
	if (_pressState != _overState) {
		if (_pressState.item != _overState.item) {
			repaintItem(_pressState.item);
		}
		_pressState = _overState;
		repaintItem(_overLayout);
	}
	auto pressLayout = _overLayout;

	_mouseAction = MouseAction::None;
	_pressWasInactive = Ui::WasInactivePress(
		_controller->parentController()->widget());
	if (_pressWasInactive) {
		Ui::MarkInactivePress(
			_controller->parentController()->widget(),
			false);
	}

	if (ClickHandler::getPressed() && !hasSelected()) {
		_mouseAction = MouseAction::PrepareDrag;
	} else if (hasSelectedItems()) {
		if (isItemUnderPressSelected() && ClickHandler::getPressed()) {
			// In shared media overview drag only by click handlers.
			_mouseAction = MouseAction::PrepareDrag; // start items drag
		} else if (!_pressWasInactive) {
			_mouseAction = MouseAction::PrepareSelect; // start items select
		}
	}
	if (_mouseAction == MouseAction::None && pressLayout) {
		validateTrippleClickStartTime();
		TextState dragState;
		auto startDistance = (globalPosition - _trippleClickPoint).manhattanLength();
		auto validStartPoint = startDistance < QApplication::startDragDistance();
		if (_trippleClickStartTime != 0 && validStartPoint) {
			StateRequest request;
			request.flags = Ui::Text::StateRequest::Flag::LookupSymbol;
			dragState = pressLayout->getState(_pressState.cursor, request);
			if (dragState.cursor == CursorState::Text) {
				TextSelection selStatus = { dragState.symbol, dragState.symbol };
				if (selStatus != FullSelection && !hasSelectedItems()) {
					clearSelected();
					applyItemSelection(_pressState.item, selStatus);
					_mouseTextSymbol = dragState.symbol;
					_mouseAction = MouseAction::Selecting;
					_mouseSelectType = TextSelectType::Paragraphs;
					mouseActionUpdate(_mousePosition);
					_trippleClickStartTime = crl::now();
				}
			}
		} else {
			StateRequest request;
			request.flags = Ui::Text::StateRequest::Flag::LookupSymbol;
			dragState = pressLayout->getState(_pressState.cursor, request);
		}
		if (_mouseSelectType != TextSelectType::Paragraphs) {
			if (_pressState.inside) {
				_mouseTextSymbol = dragState.symbol;
				if (isPressInSelectedText(dragState)) {
					_mouseAction = MouseAction::PrepareDrag; // start text drag
				} else if (!_pressWasInactive) {
					if (requiredToStartDragging(pressLayout)) {
						_mouseAction = MouseAction::PrepareDrag;
					} else {
						if (dragState.afterSymbol) ++_mouseTextSymbol;
						TextSelection selStatus = {
							_mouseTextSymbol,
							_mouseTextSymbol,
						};
						if (selStatus != FullSelection && !hasSelectedItems()) {
							clearSelected();
							applyItemSelection(_pressState.item, selStatus);
							_mouseAction = MouseAction::Selecting;
							repaintItem(pressLayout);
						} else if (!_provider->hasSelectRestriction()) {
							_mouseAction = MouseAction::PrepareSelect;
						}
					}
				}
			} else if (!_pressWasInactive
				&& !_provider->hasSelectRestriction()) {
				_mouseAction = MouseAction::PrepareSelect; // start items select
			}
		}
	}

	if (!pressLayout) {
		_mouseAction = MouseAction::None;
	} else if (_mouseAction == MouseAction::None) {
		mouseActionCancel();
	}
}

void ListWidget::mouseActionCancel() {
	_pressState = MouseState();
	_mouseAction = MouseAction::None;
	clearDragSelection();
	_wasSelectedText = false;
//	_widget->noSelectingScroll(); // #TODO scroll by drag
}

void ListWidget::performDrag() {
	if (_mouseAction != MouseAction::Dragging) return;

	auto uponSelected = false;
	if (_pressState.item && _pressState.inside) {
		if (hasSelectedItems()) {
			uponSelected = isItemUnderPressSelected();
		} else if (const auto pressLayout = _provider->lookupLayout(
				_pressState.item)) {
			StateRequest request;
			request.flags |= Ui::Text::StateRequest::Flag::LookupSymbol;
			const auto dragState = pressLayout->getState(
				_pressState.cursor,
				request);
			uponSelected = isPressInSelectedText(dragState);
		}
	}
	auto pressedHandler = ClickHandler::getPressed();

	if (dynamic_cast<VoiceSeekClickHandler*>(pressedHandler.get())) {
		return;
	}

	TextWithEntities sel;
	//QList<QUrl> urls;
	if (uponSelected) {
//		sel = getSelectedText();
	} else if (pressedHandler) {
		sel = { pressedHandler->dragText(), EntitiesInText() };
		//if (!sel.isEmpty() && sel.at(0) != '/' && sel.at(0) != '@' && sel.at(0) != '#') {
		//	urls.push_back(QUrl::fromEncoded(sel.toUtf8())); // Google Chrome crashes in Mac OS X O_o
		//}
	}
	//if (auto mimeData = MimeDataFromText(sel)) {
	//	clearDragSelection();
	//	_widget->noSelectingScroll();

	//	if (!urls.isEmpty()) mimeData->setUrls(urls);
	//	if (uponSelected && !Adaptive::OneColumn()) {
	//		auto selectedState = getSelectionState();
	//		if (selectedState.count > 0 && selectedState.count == selectedState.canForwardCount) {
	//			session().data().setMimeForwardIds(collectSelectedIds());
	//			mimeData->setData(u"application/x-td-forward"_q, "1");
	//		}
	//	}
	//	_controller->parentController()->window()->launchDrag(std::move(mimeData));
	//	return;
	//} else {
	//	auto forwardMimeType = QString();
	//	auto pressedMedia = static_cast<HistoryView::Media*>(nullptr);
	//	if (auto pressedItem = _pressState.layout) {
	//		pressedMedia = pressedItem->getMedia();
	//		if (_mouseCursorState == CursorState::Date) {
	//			session().data().setMimeForwardIds(session().data().itemOrItsGroup(pressedItem));
	//			forwardMimeType = u"application/x-td-forward"_q;
	//		}
	//	}
	//	if (auto pressedLnkItem = App::pressedLinkItem()) {
	//		if ((pressedMedia = pressedLnkItem->getMedia())) {
	//			if (forwardMimeType.isEmpty() && pressedMedia->dragItemByHandler(pressedHandler)) {
	//				session().data().setMimeForwardIds({ 1, pressedLnkItem->fullId() });
	//				forwardMimeType = u"application/x-td-forward"_q;
	//			}
	//		}
	//	}
	//	if (!forwardMimeType.isEmpty()) {
	//		auto mimeData = std::make_unique<QMimeData>();
	//		mimeData->setData(forwardMimeType, "1");
	//		if (auto document = (pressedMedia ? pressedMedia->getDocument() : nullptr)) {
	//			auto filepath = document->filepath(true);
	//			if (!filepath.isEmpty()) {
	//				QList<QUrl> urls;
	//				urls.push_back(QUrl::fromLocalFile(filepath));
	//				mimeData->setUrls(urls);
	//			}
	//		}

	//		// This call enters event loop and can destroy any QObject.
	//		_controller->parentController()->window()->launchDrag(std::move(mimeData));
	//		return;
	//	}
	//}
}

void ListWidget::mouseActionFinish(
		const QPoint &globalPosition,
		Qt::MouseButton button) {
	mouseActionUpdate(globalPosition);

	auto pressState = base::take(_pressState);
	repaintItem(pressState.item);

	auto simpleSelectionChange = pressState.item
		&& pressState.inside
		&& !_pressWasInactive
		&& (button != Qt::RightButton)
		&& (_mouseAction == MouseAction::PrepareDrag
			|| _mouseAction == MouseAction::PrepareSelect);
	auto needSelectionToggle = simpleSelectionChange
		&& hasSelectedItems();
	auto needSelectionClear = simpleSelectionChange
		&& hasSelectedText();

	auto activated = ClickHandler::unpressed();
	if (_mouseAction == MouseAction::Dragging
		|| _mouseAction == MouseAction::Selecting) {
		activated = nullptr;
	} else if (needSelectionToggle) {
		activated = nullptr;
	}

	_wasSelectedText = false;
	if (activated) {
		mouseActionCancel();
		const auto found = findItemByItem(pressState.item);
		const auto fullId = found
			? found->layout->getItem()->fullId()
			: FullMsgId();
		ActivateClickHandler(window(), activated, {
			button,
			QVariant::fromValue(ClickHandlerContext{
				.itemId = fullId,
				.sessionWindow = base::make_weak(
					_controller->parentController()),
			})
		});
		return;
	}

	if (needSelectionToggle) {
		toggleItemSelection(pressState.item);
	} else if (needSelectionClear) {
		clearSelected();
	} else if (_mouseAction == MouseAction::Selecting) {
		if (!_dragSelected.empty()) {
			applyDragSelection();
		} else if (!_selected.empty() && !_pressWasInactive) {
			auto selection = _selected.cbegin()->second;
			if (selection.text != FullSelection
				&& selection.text.from == selection.text.to) {
				clearSelected();
				//_controller->parentController()->window()->setInnerFocus(); // #TODO focus
			}
		}
	}
	_mouseAction = MouseAction::None;
	_mouseSelectType = TextSelectType::Letters;
	//_widget->noSelectingScroll(); // #TODO scroll by drag
	//_widget->updateTopBarSelection();

	//if (QGuiApplication::clipboard()->supportsSelection() && hasSelectedText()) { // #TODO linux clipboard
	//	TextUtilities::SetClipboardText(_selected.cbegin()->first->selectedText(_selected.cbegin()->second), QClipboard::Selection);
	//}
}

void ListWidget::applyDragSelection() {
	if (!_provider->hasSelectRestriction()) {
		applyDragSelection(_selected);
	}
	clearDragSelection();
	pushSelectedItems();
}

void ListWidget::applyDragSelection(SelectedMap &applyTo) const {
	if (_dragSelectAction == DragSelectAction::Selecting) {
		for (auto &[item, data] : _dragSelected) {
			ChangeItemSelection(
				applyTo,
				item,
				_provider->computeSelectionData(item, FullSelection));
		}
	} else if (_dragSelectAction == DragSelectAction::Deselecting) {
		for (auto &[item, data] : _dragSelected) {
			applyTo.remove(item);
		}
	}
}

void ListWidget::refreshHeight() {
	resize(width(), recountHeight());
	update();
}

int ListWidget::recountHeight() {
	if (_sections.empty()) {
		if (const auto count = _provider->fullCount()) {
			if (*count == 0) {
				return 0;
			}
		}
	}
	auto cachedPadding = padding();
	auto result = cachedPadding.top();
	for (auto &section : _sections) {
		section.setTop(result);
		result += section.height();
	}
	return result + cachedPadding.bottom();
}

void ListWidget::mouseActionUpdate() {
	mouseActionUpdate(_mousePosition);
}

std::vector<ListSection>::iterator ListWidget::findSectionByItem(
		not_null<const HistoryItem*> item) {
	if (_sections.size() < 2) {
		return _sections.begin();
	}
	Assert(!_controller->isDownloads());
	return ranges::lower_bound(
		_sections,
		GetUniversalId(item),
		std::greater<>(),
		[](const Section &section) { return section.minId(); });
}

auto ListWidget::findSectionAfterTop(
		int top) -> std::vector<Section>::iterator {
	return ranges::lower_bound(
		_sections,
		top,
		std::less_equal<>(),
		[](const Section &section) { return section.bottom(); });
}

auto ListWidget::findSectionAfterTop(
		int top) const -> std::vector<Section>::const_iterator {
	return ranges::lower_bound(
		_sections,
		top,
		std::less_equal<>(),
		[](const Section &section) { return section.bottom(); });
}

auto ListWidget::findSectionAfterBottom(
		std::vector<Section>::const_iterator from,
		int bottom) const -> std::vector<Section>::const_iterator {
	return ranges::lower_bound(
		from,
		_sections.end(),
		bottom,
		std::less<>(),
		[](const Section &section) { return section.top(); });
}

ListWidget::~ListWidget() {
	if (_contextMenu) {
		// We don't want it to be called after ListWidget is destroyed.
		_contextMenu->setDestroyedCallback(nullptr);
	}
}

} // namespace Media
} // namespace Info
