/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "info/media/info_media_list_widget.h"

#include "overview/overview_layout.h"
#include "history/history_media_types.h"
#include "window/themes/window_theme.h"
#include "storage/file_download.h"
#include "lang/lang_keys.h"
#include "auth_session.h"
#include "styles/style_overview.h"
#include "styles/style_info.h"

namespace Layout = Overview::Layout;

namespace Info {
namespace Media {
namespace {

constexpr auto kIdsLimit = 256;

using ItemBase = Layout::ItemBase;
using UniversalMsgId = int32;

UniversalMsgId GetUniversalId(FullMsgId itemId) {
	return (itemId.channel != 0)
		? itemId.msg
		: (itemId.msg - ServerMaxMsgId);
}

UniversalMsgId GetUniversalId(not_null<const HistoryItem*> item) {
	return GetUniversalId(item->fullId());
}

UniversalMsgId GetUniversalId(not_null<const ItemBase*> layout) {
	return GetUniversalId(layout->getItem()->fullId());
}

} // namespace

ListWidget::CachedItem::CachedItem(std::unique_ptr<ItemBase> item)
: item(std::move(item)) {
}

ListWidget::CachedItem::~CachedItem() = default;

class ListWidget::Section {
public:
	Section(Type type) : _type(type) {
	}

	bool addItem(not_null<ItemBase*> item);
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

	bool removeItem(UniversalMsgId universalId);
	base::optional<QRect> findItemRect(
		UniversalMsgId universalId) const;

	void paint(
		Painter &p,
		QRect clip,
		int outerWidth,
		TimeMs ms) const;

private:
	using Items = base::flat_map<
		UniversalMsgId,
		not_null<ItemBase*>,
		std::greater<>>;
	int headerHeight() const;
	void appendItem(not_null<ItemBase*> item);
	void setHeader(not_null<ItemBase*> item);
	bool belongsHere(not_null<ItemBase*> item) const;
	Items::iterator findItemAfterTop(int top);
	Items::const_iterator findItemAfterTop(int top) const;
	Items::const_iterator findItemAfterBottom(
		Items::const_iterator from,
		int bottom) const;
	QRect findItemRect(not_null<ItemBase*> item) const;

	int recountHeight() const;
	void refreshHeight();

	Type _type = Type::Photo;
	Text _header;
	Items _items;
	int _itemsLeft = 0;
	int _itemsTop = 0;
	int _itemWidth = 0;
	int _itemsInRow = 1;
	mutable int _rowsCount = 0;
	int _top = 0;
	int _height = 0;

};

bool ListWidget::Section::addItem(not_null<ItemBase*> item) {
	if (_items.empty() || belongsHere(item)) {
		if (_items.empty()) setHeader(item);
		appendItem(item);
		return true;
	}
	return false;
}

void ListWidget::Section::setHeader(not_null<ItemBase*> item) {
	auto text = [&] {
		auto date = item->getItem()->date.date();
		switch (_type) {
		case Type::Photo:
		case Type::Video:
		case Type::RoundFile:
		case Type::VoiceFile:
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
		not_null<ItemBase*> item) const {
	Expects(!_items.empty());
	auto date = item->getItem()->date.date();
	auto myDate = _items.back().second->getItem()->date.date();

	switch (_type) {
	case Type::Photo:
	case Type::Video:
	case Type::RoundFile:
	case Type::VoiceFile:
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

void ListWidget::Section::appendItem(not_null<ItemBase*> item) {
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

base::optional<QRect> ListWidget::Section::findItemRect(
		UniversalMsgId universalId) const {
	if (auto it = _items.find(universalId); it != _items.end()) {
		return findItemRect(it->second);
	}
	return base::none;
}

QRect ListWidget::Section::findItemRect(
		not_null<ItemBase*> item) const {
	auto position = item->position();
	auto top = position / _itemsInRow;
	auto indexInRow = position % _itemsInRow;
	auto left = _itemsLeft
		+ indexInRow * (_itemWidth + st::infoMediaSkip);
	return QRect(left, top, _itemWidth, item->height());
}

auto ListWidget::Section::findItemAfterTop(
		int top) -> Items::iterator {
	return base::lower_bound(
		_items,
		top,
		[this](const auto &item, int top) {
			auto itemTop = item.second->position() / _itemsInRow;
			return (itemTop + item.second->height()) <= top;
		});
}

auto ListWidget::Section::findItemAfterTop(
		int top) const -> Items::const_iterator {
	return base::lower_bound(
		_items,
		top,
		[this](const auto &item, int top) {
		auto itemTop = item.second->position() / _itemsInRow;
		return (itemTop + item.second->height()) <= top;
	});
}

auto ListWidget::Section::findItemAfterBottom(
		Items::const_iterator from,
		int bottom) const -> Items::const_iterator {
	return std::lower_bound(
		from,
		_items.end(),
		bottom,
		[this](const auto &item, int bottom) {
			auto itemTop = item.second->position() / _itemsInRow;
			return itemTop < bottom;
		});
}

void ListWidget::Section::paint(
		Painter &p,
		QRect clip,
		int outerWidth,
		TimeMs ms) const {
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
	Layout::PaintContext context(ms, false);
	context.isAfterDate = (header > 0);

	auto fromIt = findItemAfterTop(clip.y());
	auto tillIt = findItemAfterBottom(
		fromIt,
		clip.y() + clip.height());
	for (auto it = fromIt; it != tillIt; ++it) {
		auto item = it->second;
		auto rect = findItemRect(item);
		context.isAfterDate = (header > 0)
			&& (rect.y() <= header + _itemsTop);
		if (rect.intersects(clip)) {
			p.translate(rect.topLeft());
			item->paint(
				p,
				clip.translated(-rect.topLeft()),
				TextSelection(),
				&context);
			p.translate(-rect.topLeft());
		}
	}
}

int ListWidget::Section::headerHeight() const {
	return _header.isEmpty() ? 0 : st::infoMediaHeaderHeight;
}

void ListWidget::Section::resizeToWidth(int newWidth) {
	auto minWidth = st::infoMediaMinGridSize + st::infoMediaSkip * 2;
	if (newWidth < minWidth) {
		return;
	}

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

	case Type::VoiceFile:
	case Type::File:
	case Type::MusicFile:
	case Type::Link:
		_itemsLeft = 0;
		_itemsTop = 0;
		_itemsInRow = 1;
		_itemWidth = newWidth;
		for (auto &item : _items) {
			item.second->resizeGetHeight(_itemWidth);
		}
		break;
	}

	refreshHeight();
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

	case Type::VoiceFile:
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
	not_null<Window::Controller*> controller,
	not_null<PeerData*> peer,
	Type type)
: RpWidget(parent)
, _controller(controller)
, _peer(peer)
, _type(type)
, _slice(sliceKey()) {
	start();
	refreshViewer();
}

void ListWidget::start() {
	ObservableViewer(*Window::Theme::Background())
		| rpl::start_with_next([this](const auto &update) {
			if (update.paletteChanged()) {
				invalidatePaletteCache();
			}
		}, lifetime());
	ObservableViewer(Auth().downloader().taskFinished())
		| rpl::start_with_next([this] { update(); }, lifetime());
	Auth().data().itemLayoutChanged()
		| rpl::start_with_next([this](auto item) {
			if ((item == App::mousedItem())
				|| (item == App::hoveredItem())
				|| (item == App::hoveredLinkItem())) {
				updateSelected();
			}
		}, lifetime());
	Auth().data().itemRemoved()
		| rpl::start_with_next([this](auto item) {
			itemRemoved(item);
		}, lifetime());
	Auth().data().itemRepaintRequest()
		| rpl::start_with_next([this](auto item) {
			repaintItem(item);
		}, lifetime());
}

void ListWidget::itemRemoved(not_null<const HistoryItem*> item) {
	if (myItem(item)) {
		auto universalId = GetUniversalId(item);
		auto sectionIt = findSectionByItem(universalId);
		if (sectionIt != _sections.end()) {
			if (sectionIt->removeItem(universalId)) {
				auto top = sectionIt->top();
				if (sectionIt->empty()) {
					_sections.erase(sectionIt);
				}
				refreshHeight();
			}
		}
	}
}

void ListWidget::repaintItem(not_null<const HistoryItem*> item) {
	if (myItem(item)) {
		repaintItem(GetUniversalId(item));
	}
}

void ListWidget::repaintItem(UniversalMsgId universalId) {
	auto sectionIt = findSectionByItem(universalId);
	if (sectionIt != _sections.end()) {
		if (auto rect = sectionIt->findItemRect(universalId)) {
			auto top = padding().top() + sectionIt->top();
			rtlupdate(rect->translated(0, top));
		}
	}
}

bool ListWidget::myItem(not_null<const HistoryItem*> item) const {
	auto peer = item->history()->peer;
	return (_peer == peer || _peer == peer->migrateTo());
}

void ListWidget::invalidatePaletteCache() {
	for (auto &layout : _layouts) {
		layout.second.item->invalidateCache();
	}
}

SharedMediaMergedSlice::Key ListWidget::sliceKey() const {
	auto universalId = _universalAroundId;
	using Key = SharedMediaMergedSlice::Key;
	if (auto migrateFrom = _peer->migrateFrom()) {
		return Key(_peer->id, migrateFrom->id, _type, universalId);
	}
	return Key(_peer->id, 0, _type, universalId);
}

void ListWidget::refreshViewer() {
	SharedMediaMergedViewer(
		sliceKey(),
		countIdsLimit(),
		countIdsLimit())
		| rpl::start_with_next([this](SharedMediaMergedSlice &&slice) {
			_slice = std::move(slice);
			refreshRows();
		}, _viewerLifetime);
}

int ListWidget::countIdsLimit() const {
	return kIdsLimit;
}

ItemBase *ListWidget::getLayout(const FullMsgId &itemId) {
	auto universalId = GetUniversalId(itemId);
	auto it = _layouts.find(universalId);
	if (it == _layouts.end()) {
		if (auto layout = createLayout(itemId, _type)) {
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

std::unique_ptr<ItemBase> ListWidget::createLayout(
		const FullMsgId &itemId,
		Type type) {
	auto item = App::histItemById(itemId);
	if (!item) {
		return nullptr;
	}
	auto getPhoto = [&]() -> PhotoData* {
		if (auto media = item->getMedia()) {
			if (media->type() == MediaTypePhoto) {
				return static_cast<HistoryPhoto*>(media)->photo();
			}
		}
		return nullptr;
	};
	auto getFile = [&]() -> DocumentData* {
		if (auto media = item->getMedia()) {
			return media->getDocument();
		}
		return nullptr;
	};

	auto &fileSt = st::overviewFileLayout;
	using namespace Layout;
	switch (type) {
	case Type::Photo:
		if (auto photo = getPhoto()) {
			return std::make_unique<Photo>(item, photo);
		}
		return nullptr;
	case Type::Video:
		if (auto file = getFile()) {
			return std::make_unique<Video>(item, file);
		}
		return nullptr;
	case Type::File:
		if (auto file = getFile()) {
			return std::make_unique<Document>(item, file, fileSt);
		}
		return nullptr;
	case Type::MusicFile:
		if (auto file = getFile()) {
			return std::make_unique<Document>(item, file, fileSt);
		}
		return nullptr;
	case Type::VoiceFile:
		if (auto file = getFile()) {
			return std::make_unique<Voice>(item, file, fileSt);
		}
		return nullptr;
	case Type::Link:
		return std::make_unique<Link>(item, item->getMedia());
	case Type::RoundFile:
		return nullptr;
	}
	Unexpected("Type in ListWidget::createLayout()");
}

void ListWidget::refreshRows() {
	markLayoutsStale();

	_sections.clear();
	auto section = Section(_type);
	auto count = _slice.size();
	for (auto i = count; i != 0;) {
		auto itemId = _slice[--i];
		if (auto layout = getLayout(itemId)) {
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

	clearStaleLayouts();

	resizeToWidth(width());
}

void ListWidget::markLayoutsStale() {
	for (auto &layout : _layouts) {
		layout.second.stale = true;
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

void ListWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	if (width() <= 0) {
		return;
	}
}

QMargins ListWidget::padding() const {
	return st::infoMediaMargin;
}

void ListWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto outerWidth = width();
	auto clip = e->rect();
	auto ms = getms();
	auto fromSectionIt = findSectionAfterTop(clip.y());
	auto tillSectionIt = findSectionAfterBottom(
		fromSectionIt,
		clip.y() + clip.height());
	for (auto it = fromSectionIt; it != tillSectionIt; ++it) {
		auto top = it->top();
		p.translate(0, top);
		it->paint(p, clip.translated(0, -top), outerWidth, ms);
		p.translate(0, -top);
	}
}

void ListWidget::refreshHeight() {
	resize(width(), recountHeight());
}

int ListWidget::recountHeight() {
	auto cachedPadding = padding();
	auto result = cachedPadding.top();
	for (auto &section : _sections) {
		section.setTop(result);
		result += section.height();
	}
	return result
		+ cachedPadding.bottom();
}

void ListWidget::updateSelected() {
}

void ListWidget::clearStaleLayouts() {
	for (auto i = _layouts.begin(); i != _layouts.end();) {
		if (i->second.stale) {
			i = _layouts.erase(i);
		} else {
			++i;
		}
	}
}

auto ListWidget::findSectionByItem(
		UniversalMsgId universalId) -> std::vector<Section>::iterator {
	return base::lower_bound(
		_sections,
		universalId,
		[](const Section &section, int universalId) {
			return section.minId() > universalId;
		});
}

auto ListWidget::findSectionAfterTop(
		int top) -> std::vector<Section>::iterator {
	return base::lower_bound(
		_sections,
		top,
		[](const Section &section, int top) {
			return (section.top() + section.height()) <= top;
		});
}

auto ListWidget::findSectionAfterTop(
		int top) const -> std::vector<Section>::const_iterator {
	return base::lower_bound(
		_sections,
		top,
		[](const Section &section, int top) {
		return (section.top() + section.height()) <= top;
	});
}
auto ListWidget::findSectionAfterBottom(
		std::vector<Section>::const_iterator from,
		int bottom) const -> std::vector<Section>::const_iterator {
	return std::lower_bound(
		from,
		_sections.end(),
		bottom,
		[](const Section &section, int bottom) {
			return section.top() < bottom;
		});
}

ListWidget::~ListWidget() = default;

} // namespace Media
} // namespace Info
