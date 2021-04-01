/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/media/info_media_list_widget.h"

#include "info/info_controller.h"
#include "overview/overview_layout.h"
#include "data/data_media_types.h"
#include "data/data_photo.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_file_origin.h"
#include "history/history_item.h"
#include "history/history.h"
#include "history/view/history_view_cursor_state.h"
#include "window/themes/window_theme.h"
#include "window/window_session_controller.h"
#include "window/window_peer_menu.h"
#include "ui/widgets/popup_menu.h"
#include "ui/controls/delete_message_context_action.h"
#include "ui/ui_utility.h"
#include "ui/inactive_press.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "styles/style_overview.h"
#include "styles/style_info.h"
#include "base/platform/base_platform_info.h"
#include "media/player/media_player_instance.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/confirm_box.h"
#include "core/file_utilities.h"
#include "facades.h"

#include <QtWidgets/QApplication>
#include <QtGui/QClipboard>

namespace Layout = Overview::Layout;

namespace Info {
namespace Media {
namespace {

constexpr auto kPreloadedScreensCount = 4;
constexpr auto kPreloadIfLessThanScreens = 2;
constexpr auto kPreloadedScreensCountFull
	= kPreloadedScreensCount + 1 + kPreloadedScreensCount;
constexpr auto kMediaCountForSearch = 10;

UniversalMsgId GetUniversalId(FullMsgId itemId) {
	return (itemId.channel != 0)
		? UniversalMsgId(itemId.msg)
		: UniversalMsgId(itemId.msg - ServerMaxMsgId);
}

UniversalMsgId GetUniversalId(not_null<const HistoryItem*> item) {
	return GetUniversalId(item->fullId());
}

UniversalMsgId GetUniversalId(not_null<const BaseLayout*> layout) {
	return GetUniversalId(layout->getItem()->fullId());
}

} // namespace

struct ListWidget::Context {
	Layout::PaintContext layoutContext;
	not_null<SelectedMap*> selected;
	not_null<SelectedMap*> dragSelected;
	DragSelectAction dragSelectAction;
};

class ListWidget::Section {
public:
	Section(Type type) : _type(type) {
	}

	bool addItem(not_null<BaseLayout*> item);
	bool empty() const {
		return _items.empty();
	}

	UniversalMsgId minId() const {
		Expects(!empty());

		return _items.back().first;
	}
	UniversalMsgId maxId() const {
		Expects(!empty());

		return _items.front().first;
	}

	void setTop(int top) {
		_top = top;
	}
	int top() const {
		return _top;
	}
	void resizeToWidth(int newWidth);
	int height() const {
		return _height;
	}

	int bottom() const {
		return top() + height();
	}

	bool removeItem(UniversalMsgId universalId);
	FoundItem findItemNearId(UniversalMsgId universalId) const;
	FoundItem findItemDetails(not_null<BaseLayout*> item) const;
	FoundItem findItemByPoint(QPoint point) const;

	void paint(
		Painter &p,
		const Context &context,
		QRect clip,
		int outerWidth) const;

	static int MinItemHeight(Type type, int width);

private:
	using Items = base::flat_map<
		UniversalMsgId,
		not_null<BaseLayout*>,
		std::greater<>>;
	int headerHeight() const;
	void appendItem(not_null<BaseLayout*> item);
	void setHeader(not_null<BaseLayout*> item);
	bool belongsHere(not_null<BaseLayout*> item) const;
	Items::iterator findItemAfterTop(int top);
	Items::const_iterator findItemAfterTop(int top) const;
	Items::const_iterator findItemAfterBottom(
		Items::const_iterator from,
		int bottom) const;
	QRect findItemRect(not_null<const BaseLayout*> item) const;
	FoundItem completeResult(
		not_null<BaseLayout*> item,
		bool exact) const;
	TextSelection itemSelection(
		not_null<const BaseLayout*> item,
		const Context &context) const;

	int recountHeight() const;
	void refreshHeight();

	Type _type = Type::Photo;
	Ui::Text::String _header;
	Items _items;
	int _itemsLeft = 0;
	int _itemsTop = 0;
	int _itemWidth = 0;
	int _itemsInRow = 1;
	mutable int _rowsCount = 0;
	int _top = 0;
	int _height = 0;

};

bool ListWidget::IsAfter(
		const MouseState &a,
		const MouseState &b) {
	if (a.itemId != b.itemId) {
		return (a.itemId < b.itemId);
	}
	auto xAfter = a.cursor.x() - b.cursor.x();
	auto yAfter = a.cursor.y() - b.cursor.y();
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

ListWidget::CachedItem::CachedItem(std::unique_ptr<BaseLayout> item)
: item(std::move(item)) {
}

ListWidget::CachedItem::CachedItem(CachedItem &&other) = default;

ListWidget::CachedItem &ListWidget::CachedItem::operator=(
	CachedItem && other) = default;

ListWidget::CachedItem::~CachedItem() = default;

bool ListWidget::Section::addItem(not_null<BaseLayout*> item) {
	if (_items.empty() || belongsHere(item)) {
		if (_items.empty()) setHeader(item);
		appendItem(item);
		return true;
	}
	return false;
}

void ListWidget::Section::setHeader(not_null<BaseLayout*> item) {
	auto text = [&] {
		auto date = item->dateTime().date();
		switch (_type) {
		case Type::Photo:
		case Type::Video:
		case Type::RoundFile:
		case Type::RoundVoiceFile:
		case Type::File:
			return langMonthFull(date);

		case Type::Link:
			return langDayOfMonthFull(date);

		case Type::MusicFile:
			return QString();
		}
		Unexpected("Type in ListWidget::Section::setHeader()");
	}();
	_header.setText(st::infoMediaHeaderStyle, text);
}

bool ListWidget::Section::belongsHere(
		not_null<BaseLayout*> item) const {
	Expects(!_items.empty());

	auto date = item->dateTime().date();
	auto myDate = _items.back().second->dateTime().date();

	switch (_type) {
	case Type::Photo:
	case Type::Video:
	case Type::RoundFile:
	case Type::RoundVoiceFile:
	case Type::File:
		return date.year() == myDate.year()
			&& date.month() == myDate.month();

	case Type::Link:
		return date.year() == myDate.year()
			&& date.month() == myDate.month()
			&& date.day() == myDate.day();

	case Type::MusicFile:
		return true;
	}
	Unexpected("Type in ListWidget::Section::belongsHere()");
}

void ListWidget::Section::appendItem(not_null<BaseLayout*> item) {
	_items.emplace(GetUniversalId(item), item);
}

bool ListWidget::Section::removeItem(UniversalMsgId universalId) {
	if (auto it = _items.find(universalId); it != _items.end()) {
		it = _items.erase(it);
		refreshHeight();
		return true;
	}
	return false;
}

QRect ListWidget::Section::findItemRect(
		not_null<const BaseLayout*> item) const {
	auto position = item->position();
	auto top = position / _itemsInRow;
	auto indexInRow = position % _itemsInRow;
	auto left = _itemsLeft
		+ indexInRow * (_itemWidth + st::infoMediaSkip);
	return QRect(left, top, _itemWidth, item->height());
}

auto ListWidget::Section::completeResult(
		not_null<BaseLayout*> item,
		bool exact) const -> FoundItem {
	return { item, findItemRect(item), exact };
}

auto ListWidget::Section::findItemByPoint(
		QPoint point) const -> FoundItem {
	Expects(!_items.empty());
	auto itemIt = findItemAfterTop(point.y());
	if (itemIt == _items.end()) {
		--itemIt;
	}
	auto item = itemIt->second;
	auto rect = findItemRect(item);
	if (point.y() >= rect.top()) {
		auto shift = floorclamp(
			point.x(),
			(_itemWidth + st::infoMediaSkip),
			0,
			_itemsInRow);
		while (shift-- && itemIt != _items.end()) {
			++itemIt;
		}
		if (itemIt == _items.end()) {
			--itemIt;
		}
		item = itemIt->second;
		rect = findItemRect(item);
	}
	return { item, rect, rect.contains(point) };
}

auto ListWidget::Section::findItemNearId(UniversalMsgId universalId) const
-> FoundItem {
	Expects(!_items.empty());

	auto itemIt = ranges::lower_bound(
		_items,
		universalId,
		std::greater<>(),
		[](const auto &item) -> UniversalMsgId { return item.first; });
	if (itemIt == _items.end()) {
		--itemIt;
	}
	auto item = itemIt->second;
	auto exact = (GetUniversalId(item) == universalId);
	return { item, findItemRect(item), exact };
}

auto ListWidget::Section::findItemDetails(not_null<BaseLayout*> item) const
-> FoundItem {
	return { item, findItemRect(item), true };
}

auto ListWidget::Section::findItemAfterTop(
		int top) -> Items::iterator {
	return ranges::lower_bound(
		_items,
		top,
		std::less_equal<>(),
		[this](const auto &item) {
			auto itemTop = item.second->position() / _itemsInRow;
			return itemTop + item.second->height();
		});
}

auto ListWidget::Section::findItemAfterTop(
		int top) const -> Items::const_iterator {
	return ranges::lower_bound(
		_items,
		top,
		std::less_equal<>(),
		[this](const auto &item) {
			auto itemTop = item.second->position() / _itemsInRow;
			return itemTop + item.second->height();
		});
}

auto ListWidget::Section::findItemAfterBottom(
		Items::const_iterator from,
		int bottom) const -> Items::const_iterator {
	return ranges::lower_bound(
		from,
		_items.end(),
		bottom,
		std::less<>(),
		[this](const auto &item) {
			auto itemTop = item.second->position() / _itemsInRow;
			return itemTop;
		});
}

void ListWidget::Section::paint(
		Painter &p,
		const Context &context,
		QRect clip,
		int outerWidth) const {
	auto baseIndex = 0;
	auto header = headerHeight();
	if (QRect(0, 0, outerWidth, header).intersects(clip)) {
		p.setPen(st::infoMediaHeaderFg);
		_header.drawLeftElided(
			p,
			st::infoMediaHeaderPosition.x(),
			st::infoMediaHeaderPosition.y(),
			outerWidth - 2 * st::infoMediaHeaderPosition.x(),
			outerWidth);
	}
	auto top = header + _itemsTop;
	auto fromcol = floorclamp(
		clip.x() - _itemsLeft,
		_itemWidth,
		0,
		_itemsInRow);
	auto tillcol = ceilclamp(
		clip.x() + clip.width() - _itemsLeft,
		_itemWidth,
		0,
		_itemsInRow);
	auto localContext = context.layoutContext;
	localContext.isAfterDate = (header > 0);

	auto fromIt = findItemAfterTop(clip.y());
	auto tillIt = findItemAfterBottom(
		fromIt,
		clip.y() + clip.height());
	for (auto it = fromIt; it != tillIt; ++it) {
		auto item = it->second;
		auto rect = findItemRect(item);
		localContext.isAfterDate = (header > 0)
			&& (rect.y() <= header + _itemsTop);
		if (rect.intersects(clip)) {
			p.translate(rect.topLeft());
			item->paint(
				p,
				clip.translated(-rect.topLeft()),
				itemSelection(item, context),
				&localContext);
			p.translate(-rect.topLeft());
		}
	}
}

TextSelection ListWidget::Section::itemSelection(
		not_null<const BaseLayout*> item,
		const Context &context) const {
	auto universalId = GetUniversalId(item);
	auto dragSelectAction = context.dragSelectAction;
	if (dragSelectAction != DragSelectAction::None) {
		auto i = context.dragSelected->find(universalId);
		if (i != context.dragSelected->end()) {
			return (dragSelectAction == DragSelectAction::Selecting)
				? FullSelection
				: TextSelection();
		}
	}
	auto i = context.selected->find(universalId);
	return (i == context.selected->cend())
		? TextSelection()
		: i->second.text;
}

int ListWidget::Section::headerHeight() const {
	return _header.isEmpty() ? 0 : st::infoMediaHeaderHeight;
}

void ListWidget::Section::resizeToWidth(int newWidth) {
	auto minWidth = st::infoMediaMinGridSize + st::infoMediaSkip * 2;
	if (newWidth < minWidth) {
		return;
	}

	auto resizeOneColumn = [&](int itemsLeft, int itemWidth) {
		_itemsLeft = itemsLeft;
		_itemsTop = 0;
		_itemsInRow = 1;
		_itemWidth = itemWidth;
		for (auto &item : _items) {
			item.second->resizeGetHeight(_itemWidth);
		}
	};
	switch (_type) {
	case Type::Photo:
	case Type::Video:
	case Type::RoundFile: {
		_itemsLeft = st::infoMediaSkip;
		_itemsTop = st::infoMediaSkip;
		_itemsInRow = (newWidth - _itemsLeft)
			/ (st::infoMediaMinGridSize + st::infoMediaSkip);
		_itemWidth = ((newWidth - _itemsLeft) / _itemsInRow)
			- st::infoMediaSkip;
		for (auto &item : _items) {
			item.second->resizeGetHeight(_itemWidth);
		}
	} break;

	case Type::RoundVoiceFile:
	case Type::MusicFile:
		resizeOneColumn(0, newWidth);
		break;
	case Type::File:
	case Type::Link: {
		auto itemsLeft = st::infoMediaHeaderPosition.x();
		auto itemWidth = newWidth - 2 * itemsLeft;
		resizeOneColumn(itemsLeft, itemWidth);
	} break;
	}

	refreshHeight();
}

int ListWidget::Section::MinItemHeight(Type type, int width) {
	auto &songSt = st::overviewFileLayout;
	switch (type) {
	case Type::Photo:
	case Type::Video:
	case Type::RoundFile: {
		auto itemsLeft = st::infoMediaSkip;
		auto itemsInRow = (width - itemsLeft)
			/ (st::infoMediaMinGridSize + st::infoMediaSkip);
		return (st::infoMediaMinGridSize + st::infoMediaSkip) / itemsInRow;
	} break;

	case Type::RoundVoiceFile:
		return songSt.songPadding.top() + songSt.songThumbSize + songSt.songPadding.bottom() + st::lineWidth;
	case Type::File:
		return songSt.filePadding.top() + songSt.fileThumbSize + songSt.filePadding.bottom() + st::lineWidth;
	case Type::MusicFile:
		return songSt.songPadding.top() + songSt.songThumbSize + songSt.songPadding.bottom();
	case Type::Link:
		return st::linksPhotoSize + st::linksMargin.top() + st::linksMargin.bottom() + st::linksBorder;
	}
	Unexpected("Type in ListWidget::Section::MinItemHeight()");
}

int ListWidget::Section::recountHeight() const {
	auto result = headerHeight();

	switch (_type) {
	case Type::Photo:
	case Type::Video:
	case Type::RoundFile: {
		auto itemHeight = _itemWidth + st::infoMediaSkip;
		auto index = 0;
		result += _itemsTop;
		for (auto &item : _items) {
			item.second->setPosition(_itemsInRow * result + index);
			if (++index == _itemsInRow) {
				result += itemHeight;
				index = 0;
			}
		}
		if (_items.size() % _itemsInRow) {
			_rowsCount = int(_items.size()) / _itemsInRow + 1;
			result += itemHeight;
		} else {
			_rowsCount = int(_items.size()) / _itemsInRow;
		}
	} break;

	case Type::RoundVoiceFile:
	case Type::File:
	case Type::MusicFile:
	case Type::Link:
		for (auto &item : _items) {
			item.second->setPosition(result);
			result += item.second->height();
		}
		_rowsCount = _items.size();
		break;
	}

	return result;
}

void ListWidget::Section::refreshHeight() {
	_height = recountHeight();
}

ListWidget::ListWidget(
	QWidget *parent,
	not_null<AbstractController*> controller)
: RpWidget(parent)
, _controller(controller)
, _peer(_controller->key().peer())
, _migrated(_controller->migrated())
, _type(_controller->section().mediaType())
, _slice(sliceKey(_universalAroundId)) {
	setMouseTracking(true);
	start();
}

Main::Session &ListWidget::session() const {
	return _controller->session();
}

void ListWidget::start() {
	_controller->setSearchEnabledByContent(false);
	ObservableViewer(
		*Window::Theme::Background()
	) | rpl::start_with_next([this](const auto &update) {
		if (update.paletteChanged()) {
			invalidatePaletteCache();
		}
	}, lifetime());

	session().downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		update();
	}, lifetime());

	session().data().itemLayoutChanged(
	) | rpl::start_with_next([this](auto item) {
		itemLayoutChanged(item);
	}, lifetime());

	session().data().itemRemoved(
	) | rpl::start_with_next([this](auto item) {
		itemRemoved(item);
	}, lifetime());

	session().data().itemRepaintRequest(
	) | rpl::start_with_next([this](auto item) {
		repaintItem(item);
	}, lifetime());

	_controller->mediaSourceQueryValue(
	) | rpl::start_with_next([this]{
		restart();
	}, lifetime());
}

rpl::producer<int> ListWidget::scrollToRequests() const {
	return _scrollToRequests.events();
}

rpl::producer<SelectedItems> ListWidget::selectedListValue() const {
	return _selectedListStream.events_starting_with(
		collectSelectedItems());
}

QRect ListWidget::getCurrentSongGeometry() {
	const auto type = AudioMsgId::Type::Song;
	const auto current = ::Media::Player::instance()->current(type);
	const auto fullMsgId = current.contextId();
	if (fullMsgId && isPossiblyMyId(fullMsgId)) {
		if (const auto item = findItemById(GetUniversalId(fullMsgId))) {
			return item->geometry;
		}
	}
	return QRect(0, 0, width(), 0);
}

void ListWidget::restart() {
	mouseActionCancel();

	_overLayout = nullptr;
	_sections.clear();
	_layouts.clear();
	_heavyLayouts.clear();

	_universalAroundId = kDefaultAroundId;
	_idsLimit = kMinimalIdsLimit;
	_slice = SparseIdsMergedSlice(sliceKey(_universalAroundId));

	refreshViewer();
}

void ListWidget::itemRemoved(not_null<const HistoryItem*> item) {
	if (!isMyItem(item)) {
		return;
	}
	auto id = GetUniversalId(item);

	auto needHeightRefresh = false;
	auto sectionIt = findSectionByItem(id);
	if (sectionIt != _sections.end()) {
		if (sectionIt->removeItem(id)) {
			auto top = sectionIt->top();
			if (sectionIt->empty()) {
				_sections.erase(sectionIt);
			}
			needHeightRefresh = true;
		}
	}

	if (isItemLayout(item, _overLayout)) {
		_overLayout = nullptr;
	}

	if (const auto i = _layouts.find(id); i != _layouts.end()) {
		_heavyLayouts.remove(i->second.item.get());
		_layouts.erase(i);
	}
	_dragSelected.remove(id);

	if (const auto i = _selected.find(id); i != _selected.cend()) {
		removeItemSelection(i);
	}

	if (needHeightRefresh) {
		refreshHeight();
	}
	mouseActionUpdate(_mousePosition);
}

FullMsgId ListWidget::computeFullId(
		UniversalMsgId universalId) const {
	Expects(universalId != 0);

	return (universalId > 0)
		? FullMsgId(peerToChannel(_peer->id), universalId)
		: FullMsgId(NoChannel, ServerMaxMsgId + universalId);
}

auto ListWidget::collectSelectedItems() const -> SelectedItems {
	auto convert = [&](
			UniversalMsgId universalId,
			const SelectionData &selection) {
		auto result = SelectedItem(computeFullId(universalId));
		result.canDelete = selection.canDelete;
		result.canForward = selection.canForward;
		return result;
	};
	auto transformation = [&](const auto &item) {
		return convert(item.first, item.second);
	};
	auto items = SelectedItems(_type);
	if (hasSelectedItems()) {
		items.list.reserve(_selected.size());
		std::transform(
			_selected.begin(),
			_selected.end(),
			std::back_inserter(items.list),
			transformation);
	}
	return items;
}

MessageIdsList ListWidget::collectSelectedIds() const {
	const auto selected = collectSelectedItems();
	return ranges::views::all(
		selected.list
	) | ranges::views::transform([](const SelectedItem &item) {
		return item.msgId;
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
	if (item && isMyItem(item)) {
		repaintItem(GetUniversalId(item));
	}
}

void ListWidget::repaintItem(UniversalMsgId universalId) {
	if (auto item = findItemById(universalId)) {
		repaintItem(item->geometry);
	}
}

void ListWidget::repaintItem(const BaseLayout *item) {
	if (item) {
		repaintItem(GetUniversalId(item));
	}
}

void ListWidget::repaintItem(QRect itemGeometry) {
	rtlupdate(itemGeometry);
}

bool ListWidget::isMyItem(not_null<const HistoryItem*> item) const {
	auto peer = item->history()->peer;
	return (_peer == peer) || (_migrated == peer);
}

bool ListWidget::isPossiblyMyId(FullMsgId fullId) const {
	return fullId.channel
		? (_peer->isChannel() && peerToChannel(_peer->id) == fullId.channel)
		: (!_peer->isChannel() || _migrated);
}

bool ListWidget::isItemLayout(
		not_null<const HistoryItem*> item,
		BaseLayout *layout) const {
	return layout && (layout->getItem() == item);
}

void ListWidget::invalidatePaletteCache() {
	for (auto &layout : _layouts) {
		layout.second.item->invalidateCache();
	}
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

SparseIdsMergedSlice::Key ListWidget::sliceKey(
		UniversalMsgId universalId) const {
	using Key = SparseIdsMergedSlice::Key;
	if (_migrated) {
		return Key(_peer->id, _migrated->id, universalId);
	}
	if (universalId < 0) {
		// Convert back to plain id for non-migrated histories.
		universalId += ServerMaxMsgId;
	}
	return Key(_peer->id, 0, universalId);
}

void ListWidget::refreshViewer() {
	_viewerLifetime.destroy();
	auto idForViewer = sliceKey(_universalAroundId).universalId;
	_controller->mediaSource(
		idForViewer,
		_idsLimit,
		_idsLimit
	) | rpl::start_with_next([=](SparseIdsMergedSlice &&slice) {
		if (!slice.fullCount()) {
			// Don't display anything while full count is unknown.
			return;
		}
		_slice = std::move(slice);
		if (auto nearest = _slice.nearest(idForViewer)) {
			_universalAroundId = GetUniversalId(*nearest);
		}
		refreshRows();
	}, _viewerLifetime);
}

BaseLayout *ListWidget::getLayout(UniversalMsgId universalId) {
	auto it = _layouts.find(universalId);
	if (it == _layouts.end()) {
		if (auto layout = createLayout(universalId, _type)) {
			layout->initDimensions();
			it = _layouts.emplace(
				universalId,
				std::move(layout)).first;
		} else {
			return nullptr;
		}
	}
	it->second.stale = false;
	return it->second.item.get();
}

BaseLayout *ListWidget::getExistingLayout(
		UniversalMsgId universalId) const {
	auto it = _layouts.find(universalId);
	return (it != _layouts.end())
		? it->second.item.get()
		: nullptr;
}

std::unique_ptr<BaseLayout> ListWidget::createLayout(
		UniversalMsgId universalId,
		Type type) {
	auto item = session().data().message(computeFullId(universalId));
	if (!item) {
		return nullptr;
	}
	auto getPhoto = [&]() -> PhotoData* {
		if (const auto media = item->media()) {
			return media->photo();
		}
		return nullptr;
	};
	auto getFile = [&]() -> DocumentData* {
		if (auto media = item->media()) {
			return media->document();
		}
		return nullptr;
	};

	auto &songSt = st::overviewFileLayout;
	using namespace Layout;
	switch (type) {
	case Type::Photo:
		if (const auto photo = getPhoto()) {
			return std::make_unique<Photo>(this, item, photo);
		}
		return nullptr;
	case Type::Video:
		if (const auto file = getFile()) {
			return std::make_unique<Video>(this, item, file);
		}
		return nullptr;
	case Type::File:
		if (const auto file = getFile()) {
			return std::make_unique<Document>(this, item, file, songSt);
		}
		return nullptr;
	case Type::MusicFile:
		if (const auto file = getFile()) {
			return std::make_unique<Document>(this, item, file, songSt);
		}
		return nullptr;
	case Type::RoundVoiceFile:
		if (const auto file = getFile()) {
			return std::make_unique<Voice>(this, item, file, songSt);
		}
		return nullptr;
	case Type::Link:
		return std::make_unique<Link>(this, item, item->media());
	case Type::RoundFile:
		return nullptr;
	}
	Unexpected("Type in ListWidget::createLayout()");
}

void ListWidget::refreshRows() {
	saveScrollState();

	markLayoutsStale();

	_sections.clear();
	auto section = Section(_type);
	auto count = _slice.size();
	for (auto i = count; i != 0;) {
		auto universalId = GetUniversalId(_slice[--i]);
		if (auto layout = getLayout(universalId)) {
			if (!section.addItem(layout)) {
				_sections.push_back(std::move(section));
				section = Section(_type);
				section.addItem(layout);
			}
		}
	}
	if (!section.empty()) {
		_sections.push_back(std::move(section));
	}

	if (auto count = _slice.fullCount()) {
		if (*count > kMediaCountForSearch) {
			_controller->setSearchEnabledByContent(true);
		}
	}

	clearStaleLayouts();

	resizeToWidth(width());
	restoreScrollState();
	mouseActionUpdate();
}

void ListWidget::markLayoutsStale() {
	for (auto &layout : _layouts) {
		layout.second.stale = true;
	}
}

bool ListWidget::preventAutoHide() const {
	return (_contextMenu != nullptr) || (_actionBoxWeak != nullptr);
}

void ListWidget::saveState(not_null<Memento*> memento) {
	if (_universalAroundId != kDefaultAroundId) {
		auto state = countScrollState();
		if (state.item) {
			memento->setAroundId(computeFullId(_universalAroundId));
			memento->setIdsLimit(_idsLimit);
			memento->setScrollTopItem(computeFullId(state.item));
			memento->setScrollTopShift(state.shift);
		}
	}
}

void ListWidget::restoreState(not_null<Memento*> memento) {
	if (auto limit = memento->idsLimit()) {
		auto wasAroundId = memento->aroundId();
		if (isPossiblyMyId(wasAroundId)) {
			_idsLimit = limit;
			_universalAroundId = GetUniversalId(wasAroundId);
			_scrollTopState.item = GetUniversalId(memento->scrollTopItem());
			_scrollTopState.shift = memento->scrollTopShift();
			refreshViewer();
		}
	}
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

auto ListWidget::findItemById(
		UniversalMsgId universalId) -> std::optional<FoundItem> {
	auto sectionIt = findSectionByItem(universalId);
	if (sectionIt != _sections.end()) {
		auto item = sectionIt->findItemNearId(universalId);
		if (item.exact) {
			return foundItemInSection(item, *sectionIt);
		}
	}
	return std::nullopt;
}

auto ListWidget::findItemDetails(not_null<BaseLayout*> item)
-> FoundItem {
	const auto sectionIt = findSectionByItem(GetUniversalId(item));
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
		item.exact };
}

void ListWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;

	checkMoveToOtherViewer();
	clearHeavyItems();
}

void ListWidget::checkMoveToOtherViewer() {
	auto visibleHeight = (_visibleBottom - _visibleTop);
	if (width() <= 0
		|| visibleHeight <= 0
		|| _sections.empty()
		|| _scrollTopState.item) {
		return;
	}

	auto topItem = findItemByPoint({ 0, _visibleTop });
	auto bottomItem = findItemByPoint({ 0, _visibleBottom });

	auto preloadedHeight = kPreloadedScreensCountFull * visibleHeight;
	auto minItemHeight = Section::MinItemHeight(_type, width());
	auto preloadedCount = preloadedHeight / minItemHeight;
	auto preloadIdsLimitMin = (preloadedCount / 2) + 1;
	auto preloadIdsLimit = preloadIdsLimitMin
		+ (visibleHeight / minItemHeight);

	auto preloadBefore = kPreloadIfLessThanScreens * visibleHeight;
	auto after = _slice.skippedAfter();
	auto preloadTop = (_visibleTop < preloadBefore);
	auto topLoaded = after && (*after == 0);
	auto before = _slice.skippedBefore();
	auto preloadBottom = (height() - _visibleBottom < preloadBefore);
	auto bottomLoaded = before && (*before == 0);

	auto minScreenDelta = kPreloadedScreensCount
		- kPreloadIfLessThanScreens;
	auto minUniversalIdDelta = (minScreenDelta * visibleHeight)
		/ minItemHeight;
	auto preloadAroundItem = [&](const FoundItem &item) {
		auto preloadRequired = false;
		auto universalId = GetUniversalId(item.layout);
		if (!preloadRequired) {
			preloadRequired = (_idsLimit < preloadIdsLimitMin);
		}
		if (!preloadRequired) {
			auto delta = _slice.distance(
				sliceKey(_universalAroundId),
				sliceKey(universalId));
			Assert(delta != std::nullopt);
			preloadRequired = (qAbs(*delta) >= minUniversalIdDelta);
		}
		if (preloadRequired) {
			_idsLimit = preloadIdsLimit;
			_universalAroundId = universalId;
			refreshViewer();
		}
	};

	if (preloadTop && !topLoaded) {
		preloadAroundItem(topItem);
	} else if (preloadBottom && !bottomLoaded) {
		preloadAroundItem(bottomItem);
	}
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

auto ListWidget::countScrollState() const -> ScrollTopState {
	if (_sections.empty()) {
		return { 0, 0 };
	}
	auto topItem = findItemByPoint({ 0, _visibleTop });
	return {
		GetUniversalId(topItem.layout),
		_visibleTop - topItem.geometry.y()
	};
}

void ListWidget::saveScrollState() {
	if (!_scrollTopState.item) {
		_scrollTopState = countScrollState();
	}
}

void ListWidget::restoreScrollState() {
	if (_sections.empty() || !_scrollTopState.item) {
		return;
	}
	auto sectionIt = findSectionByItem(_scrollTopState.item);
	if (sectionIt == _sections.end()) {
		--sectionIt;
	}
	auto item = foundItemInSection(
		sectionIt->findItemNearId(_scrollTopState.item),
		*sectionIt);
	auto newVisibleTop = item.geometry.y() + _scrollTopState.shift;
	if (_visibleTop != newVisibleTop) {
		_scrollToRequests.fire_copy(newVisibleTop);
	}
	_scrollTopState = ScrollTopState();
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
	auto context = Context {
		Layout::PaintContext(ms, hasSelectedItems()),
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
		repaintItem(_contextUniversalId);
	}
	if (e->reason() == QContextMenuEvent::Mouse) {
		mouseActionUpdate(e->globalPos());
	}

	auto item = session().data().message(computeFullId(_overState.itemId));
	if (!item || !_overState.inside) {
		return;
	}
	auto universalId = _contextUniversalId = _overState.itemId;

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
		auto it = _selected.find(_overState.itemId);
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
		});
	};

	auto link = ClickHandler::getActive();

	const auto itemFullId = item->fullId();
	const auto owner = &session().data();
	_contextMenu = base::make_unique_q<Ui::PopupMenu>(this);
	_contextMenu->addAction(
		tr::lng_context_to_msg(tr::now),
		[=] {
			if (const auto item = owner->message(itemFullId)) {
				Ui::showPeerHistoryAtItem(item);
			}
		});

	auto photoLink = dynamic_cast<PhotoClickHandler*>(link.get());
	auto fileLink = dynamic_cast<DocumentClickHandler*>(link.get());
	if (photoLink || fileLink) {
		auto [isVideo, isVoice, isAudio] = [&] {
			if (fileLink) {
				auto document = fileLink->document();
				return std::make_tuple(
					document->isVideoFile(),
					document->isVoiceMessage(),
					document->isAudioFile()
				);
			}
			return std::make_tuple(false, false, false);
		}();

		if (photoLink) {
		} else {
			if (auto document = fileLink->document()) {
				if (document->loading()) {
					_contextMenu->addAction(
						tr::lng_context_cancel_download(tr::now),
						[document] {
							document->cancel();
						});
				} else {
					auto filepath = document->filepath(true);
					if (!filepath.isEmpty()) {
						auto handler = App::LambdaDelayed(
							st::defaultDropdownMenu.menu.ripple.hideDuration,
							this,
							[filepath] {
								File::ShowInFolder(filepath);
							});
						_contextMenu->addAction(
							(Platform::IsMac()
								? tr::lng_context_show_in_finder(tr::now)
								: tr::lng_context_show_in_folder(tr::now)),
							std::move(handler));
					}
					auto handler = App::LambdaDelayed(
						st::defaultDropdownMenu.menu.ripple.hideDuration,
						this,
						[=] {
							DocumentSaveClickHandler::Save(
								itemFullId,
								document,
								DocumentSaveClickHandler::Mode::ToNewFile);
						});
					_contextMenu->addAction(
						(isVideo
							? tr::lng_context_save_video(tr::now)
							: isVoice
							? tr::lng_context_save_audio(tr::now)
							: isAudio
							? tr::lng_context_save_audio_file(tr::now)
							: tr::lng_context_save_file(tr::now)),
						std::move(handler));
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
				});
		}
	}
	if (overSelected == SelectionState::OverSelectedItems) {
		if (canForwardAll()) {
			_contextMenu->addAction(
				tr::lng_context_forward_selected(tr::now),
				crl::guard(this, [this] {
					forwardSelected();
				}));
		}
		if (canDeleteAll()) {
			_contextMenu->addAction(
				tr::lng_context_delete_selected(tr::now),
				crl::guard(this, [this] {
					deleteSelected();
				}));
		}
		_contextMenu->addAction(
			tr::lng_context_clear_selection(tr::now),
			crl::guard(this, [this] {
				clearSelected();
			}));
	} else {
		if (overSelected != SelectionState::NotOverSelectedItems) {
			if (item->allowsForward()) {
				_contextMenu->addAction(
					tr::lng_context_forward_msg(tr::now),
					crl::guard(this, [this, universalId] {
						forwardItem(universalId);
					}));
			}
			if (item->canDelete()) {
				_contextMenu->addAction(Ui::DeleteMessageContextAction(
					_contextMenu->menu(),
					[=] { deleteItem(universalId); },
					item->ttlDestroyAt(),
					[=] { _contextMenu = nullptr; }));
			}
		}
		_contextMenu->addAction(
			tr::lng_context_select_msg(tr::now),
			crl::guard(this, [this, universalId] {
				if (hasSelectedText()) {
					clearSelected();
				} else if (_selected.size() == MaxSelectedItems) {
					return;
				} else if (_selected.empty()) {
					update();
				}
				applyItemSelection(universalId, FullSelection);
			}));
	}

	_contextMenu->setDestroyedCallback(crl::guard(
		this,
		[this, universalId] {
			mouseActionUpdate(QCursor::pos());
			repaintItem(universalId);
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

void ListWidget::forwardItem(UniversalMsgId universalId) {
	if (const auto item = session().data().message(computeFullId(universalId))) {
		forwardItems({ 1, item->fullId() });
	}
}

void ListWidget::forwardItems(MessageIdsList &&items) {
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

void ListWidget::deleteSelected() {
	if (const auto box = deleteItems(collectSelectedIds())) {
		const auto weak = Ui::MakeWeak(this);
		box->setDeleteConfirmedCallback([=]{
			if (const auto strong = weak.data()) {
				strong->clearSelected();
			}
		});
	}
}

void ListWidget::deleteItem(UniversalMsgId universalId) {
	if (const auto item = session().data().message(computeFullId(universalId))) {
		deleteItems({ 1, item->fullId() });
	}
}

DeleteMessagesBox *ListWidget::deleteItems(MessageIdsList &&items) {
	if (!items.empty()) {
		const auto box = Ui::show(
			Box<DeleteMessagesBox>(
				&_controller->session(),
				std::move(items))).data();
		setActionBoxWeak(box);
		return box;
	}
	return nullptr;
}

void ListWidget::setActionBoxWeak(QPointer<Ui::RpWidget> box) {
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
		applyItemSelection(_overState.itemId, selStatus);
	}
	mouseActionUpdate();

	_trippleClickPoint = _mousePosition;
	_trippleClickStartTime = crl::now();
}

void ListWidget::applyItemSelection(
		UniversalMsgId universalId,
		TextSelection selection) {
	if (changeItemSelection(
			_selected,
			universalId,
			selection)) {
		repaintItem(universalId);
		pushSelectedItems();
	}
}

void ListWidget::toggleItemSelection(UniversalMsgId universalId) {
	auto it = _selected.find(universalId);
	if (it == _selected.cend()) {
		applyItemSelection(universalId, FullSelection);
	} else {
		removeItemSelection(it);
	}
}

bool ListWidget::changeItemSelection(
		SelectedMap &selected,
		UniversalMsgId universalId,
		TextSelection selection) const {
	auto changeExisting = [&](auto it) {
		if (it == selected.cend()) {
			return false;
		} else if (it->second.text != selection) {
			it->second.text = selection;
			return true;
		}
		return false;
	};
	if (selected.size() < MaxSelectedItems) {
		auto [iterator, ok] = selected.try_emplace(
			universalId,
			selection);
		if (ok) {
			auto item = session().data().message(computeFullId(universalId));
			if (!item) {
				selected.erase(iterator);
				return false;
			}
			iterator->second.canDelete = item->canDelete();
			iterator->second.canForward = item->allowsForward();
			return true;
		}
		return changeExisting(iterator);
	}
	return changeExisting(selected.find(universalId));
}

bool ListWidget::isItemUnderPressSelected() const {
	return itemUnderPressSelection() != _selected.end();
}

auto ListWidget::itemUnderPressSelection() -> SelectedMap::iterator {
	return (_pressState.itemId && _pressState.inside)
		? _selected.find(_pressState.itemId)
		: _selected.end();
}

auto ListWidget::itemUnderPressSelection() const
-> SelectedMap::const_iterator {
	return (_pressState.itemId && _pressState.inside)
		? _selected.find(_pressState.itemId)
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

void ListWidget::enterEventHook(QEvent *e) {
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
		GetUniversalId(layout),
		geometry.size(),
		point - geometry.topLeft(),
		inside
	};
	auto item = layout ? layout->getItem() : nullptr;
	if (_overLayout != layout) {
		repaintItem(_overLayout);
		_overLayout = layout;
		repaintItem(geometry);
	}
	_overState = state;

	TextState dragState;
	ClickHandlerHost *lnkhost = nullptr;
	auto inTextSelection = _overState.inside
		&& (_overState.itemId == _pressState.itemId)
		&& hasSelectedText();
	if (_overLayout) {
		auto cursorDeltaLength = [&] {
			auto cursorDelta = (_overState.cursor - _pressState.cursor);
			return cursorDelta.manhattanLength();
		};
		auto dragStartLength = [] {
			return QApplication::startDragDistance();
		};
		if (_overState.itemId != _pressState.itemId
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
	ClickHandler::setActive(dragState.link, lnkhost);

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
			applyItemSelection(_overState.itemId, selState);
			auto hasSelection = (selState == FullSelection)
				|| (selState.from != selState.to);
			if (!_wasSelectedText && hasSelection) {
				_wasSelectedText = true;
				setFocus();
			}
			clearDragSelection();
		} else if (_pressState.itemId) {
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
	auto swapStates = IsAfter(fromState, tillState);
	if (swapStates) {
		std::swap(fromState, tillState);
	}
	if (!fromState.itemId || !tillState.itemId) {
		clearDragSelection();
		return;
	}
	auto fromId = SkipSelectFromItem(fromState)
		? (fromState.itemId - 1)
		: fromState.itemId;
	auto tillId = SkipSelectTillItem(tillState)
		? tillState.itemId
		: (tillState.itemId - 1);
	for (auto i = _dragSelected.begin(); i != _dragSelected.end();) {
		auto itemId = i->first;
		if (itemId > fromId || itemId <= tillId) {
			i = _dragSelected.erase(i);
		} else {
			++i;
		}
	}
	for (auto &layoutItem : _layouts) {
		auto &&universalId = layoutItem.first;
		auto &&layout = layoutItem.second;
		if (universalId <= fromId && universalId > tillId) {
			changeItemSelection(
				_dragSelected,
				universalId,
				FullSelection);
		}
	}
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
		if (_pressState.itemId != _overState.itemId) {
			repaintItem(_pressState.itemId);
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
					applyItemSelection(_pressState.itemId, selStatus);
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
						TextSelection selStatus = { _mouseTextSymbol, _mouseTextSymbol };
						if (selStatus != FullSelection && !hasSelectedItems()) {
							clearSelected();
							applyItemSelection(_pressState.itemId, selStatus);
							_mouseAction = MouseAction::Selecting;
							repaintItem(pressLayout);
						} else {
							_mouseAction = MouseAction::PrepareSelect;
						}
					}
				}
			} else if (!_pressWasInactive) {
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
	if (_pressState.itemId && _pressState.inside) {
		if (hasSelectedItems()) {
			uponSelected = isItemUnderPressSelected();
		} else if (auto pressLayout = getExistingLayout(
				_pressState.itemId)) {
			StateRequest request;
			request.flags |= Ui::Text::StateRequest::Flag::LookupSymbol;
			auto dragState = pressLayout->getState(
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
	//			mimeData->setData(qsl("application/x-td-forward"), "1");
	//		}
	//	}
	//	_controller->parentController()->window()->launchDrag(std::move(mimeData));
	//	return;
	//} else {
	//	auto forwardMimeType = QString();
	//	auto pressedMedia = static_cast<HistoryView::Media*>(nullptr);
	//	if (auto pressedItem = _pressState.layout) {
	//		pressedMedia = pressedItem->getMedia();
	//		if (_mouseCursorState == CursorState::Date || (pressedMedia && pressedMedia->dragItem())) {
	//			session().data().setMimeForwardIds(session().data().itemOrItsGroup(pressedItem));
	//			forwardMimeType = qsl("application/x-td-forward");
	//		}
	//	}
	//	if (auto pressedLnkItem = App::pressedLinkItem()) {
	//		if ((pressedMedia = pressedLnkItem->getMedia())) {
	//			if (forwardMimeType.isEmpty() && pressedMedia->dragItemByHandler(pressedHandler)) {
	//				session().data().setMimeForwardIds({ 1, pressedLnkItem->fullId() });
	//				forwardMimeType = qsl("application/x-td-forward");
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
	repaintItem(pressState.itemId);

	auto simpleSelectionChange = pressState.itemId
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
		ActivateClickHandler(window(), activated, button);
		return;
	}

	if (needSelectionToggle) {
		toggleItemSelection(pressState.itemId);
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
	applyDragSelection(_selected);
	clearDragSelection();
	pushSelectedItems();
}

void ListWidget::applyDragSelection(SelectedMap &applyTo) const {
	if (_dragSelectAction == DragSelectAction::Selecting) {
		for (auto &[universalId,data] : _dragSelected) {
			changeItemSelection(applyTo, universalId, FullSelection);
		}
	} else if (_dragSelectAction == DragSelectAction::Deselecting) {
		for (auto &[universalId,data] : _dragSelected) {
			applyTo.remove(universalId);
		}
	}
}

void ListWidget::refreshHeight() {
	resize(width(), recountHeight());
}

int ListWidget::recountHeight() {
	if (_sections.empty()) {
		if (auto count = _slice.fullCount()) {
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

void ListWidget::clearStaleLayouts() {
	for (auto i = _layouts.begin(); i != _layouts.end();) {
		if (i->second.stale) {
			if (i->second.item.get() == _overLayout) {
				_overLayout = nullptr;
			}
			_heavyLayouts.erase(i->second.item.get());
			i = _layouts.erase(i);
		} else {
			++i;
		}
	}
}

auto ListWidget::findSectionByItem(
		UniversalMsgId universalId) -> std::vector<Section>::iterator {
	return ranges::lower_bound(
		_sections,
		universalId,
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
