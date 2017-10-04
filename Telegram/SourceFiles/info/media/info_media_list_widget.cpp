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
#include "lang/lang_keys.h"
#include "styles/style_overview.h"
#include "styles/style_info.h"

namespace Layout = Overview::Layout;

namespace Info {
namespace Media {
namespace {

constexpr auto kIdsLimit = 256;

using ItemBase = Layout::ItemBase;

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

	void resizeToWidth(int newWidth);
	int height() const {
		return _height;
	}

	void paint(
		Painter &p,
		QRect clip,
		int outerWidth,
		TimeMs ms) const;

private:
	int headerHeight() const;
	void appendItem(not_null<ItemBase*> item);
	void setHeader(not_null<ItemBase*> item);
	bool belongsHere(not_null<ItemBase*> item) const;
	int countRowHeight(not_null<ItemBase*> item) const;

	Type _type = Type::Photo;
	Text _header;
	std::vector<not_null<ItemBase*>> _items;
	int _itemsLeft = 0;
	int _itemsTop = 0;
	int _itemWidth = 0;
	int _itemsInRow = 1;
	int _rowsCount = 0;
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
	auto myDate = _items.back()->getItem()->date.date();

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

int ListWidget::Section::countRowHeight(
		not_null<ItemBase*> item) const {
	switch (_type) {
	case Type::Photo:
	case Type::Video:
	case Type::RoundFile:
		return _itemWidth + st::infoMediaSkip;

	case Type::VoiceFile:
	case Type::File:
	case Type::Link:
	case Type::MusicFile:
		return item->height();
	}
	Unexpected("Type in ListWidget::Section::countRowHeight()");
}

void ListWidget::Section::appendItem(not_null<ItemBase*> item) {
	_items.push_back(item);
}

void ListWidget::Section::paint(
		Painter &p,
		QRect clip,
		int outerWidth,
		TimeMs ms) const {
	auto baseIndex = 0;
	auto top = _itemsTop;
	auto header = headerHeight();
	if (header) {
		p.setPen(st::infoMediaHeaderFg);
		_header.drawLeftElided(
			p,
			st::infoMediaHeaderPosition.x(),
			st::infoMediaHeaderPosition.y(),
			outerWidth - 2 * st::infoMediaHeaderPosition.x(),
			outerWidth);
		top += header;
	}
	auto fromitem = floorclamp(
		clip.x() - _itemsLeft,
		_itemWidth,
		0,
		_itemsInRow);
	auto tillitem = ceilclamp(
		clip.x() + clip.width() - _itemsLeft,
		_itemWidth,
		0,
		_itemsInRow);
	Layout::PaintContext context(ms, false);
	context.isAfterDate = (header > 0);

	// #TODO ranges, binary search for visible slice.
	for (auto row = 0; row != _rowsCount; ++row) {
		auto rowHeight = countRowHeight(_items[baseIndex]);
		auto increment = gsl::finally([&] {
			top += rowHeight;
			baseIndex += _itemsInRow;
			context.isAfterDate = false;
		});

		if (top >= clip.y() + clip.height()) {
			break;
		} else if (top + rowHeight <= clip.y()) {
			continue;
		}
		for (auto col = fromitem; col != tillitem; ++col) {
			auto index = baseIndex + col;
			if (index >= int(_items.size())) {
				break;
			}
			auto item = _items[index];
			auto left = _itemsLeft
				+ col * (_itemWidth + st::infoMediaSkip);
			p.translate(left, top);
			item->paint(
				p,
				clip.translated(-left, -top),
				TextSelection(),
				&context);
			p.translate(-left, -top);
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

	_height = headerHeight();
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
		auto itemHeight = _itemWidth + st::infoMediaSkip;
		_rowsCount = (int(_items.size()) + _itemsInRow - 1)
			/ _itemsInRow;
		_height += _itemsTop + _rowsCount * itemHeight;
	} break;

	case Type::VoiceFile:
	case Type::File:
	case Type::MusicFile: {
		_itemsLeft = 0;
		_itemsTop = 0;
		_itemsInRow = 1;
		_itemWidth = newWidth;
		auto itemHeight = _items.empty() ? 0 : _items.front()->height();
		_rowsCount = _items.size();
		_height += _rowsCount * itemHeight;
	} break;

	case Type::Link:
		_itemsLeft = 0;
		_itemsTop = 0;
		_itemsInRow = 1;
		_itemWidth = newWidth;
		auto top = 0;
		for (auto item : _items) {
			top += item->resizeGetHeight(_itemWidth);
		}
		_height += top;
		break;
	}

	if (_type != Type::Link) {
		for (auto item : _items) {
			item->resizeGetHeight(_itemWidth);
		}
	}
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
	refreshViewer();
	ObservableViewer(*Window::Theme::Background())
		| rpl::start_with_next([this](const auto &update) {
			if (update.paletteChanged()) {
				invalidatePaletteCache();
			}
		}, lifetime());
}

void ListWidget::invalidatePaletteCache() {
	for (auto &layout : _layouts) {
		layout.second.item->invalidateCache();
	}
}

SharedMediaMergedSlice::Key ListWidget::sliceKey() const {
	auto universalId = _universalAroundId;
	using Key = SharedMediaMergedSlice::Key;
	if (auto migrateTo = _peer->migrateTo()) {
		return Key(migrateTo->id, _peer->id, _type, universalId);
	} else if (auto migrateFrom = _peer->migrateFrom()) {
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
	auto it = _layouts.find(itemId);
	if (it == _layouts.end()) {
		if (auto layout = createLayout(itemId, _type)) {
			layout->initDimensions();
			it = _layouts.emplace(itemId, std::move(layout)).first;
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
	switch (type) {
	case Type::Photo:
		if (auto photo = getPhoto()) {
			return std::make_unique<Layout::Photo>(photo, item);
		}
		return nullptr;
	case Type::Video:
		if (auto file = getFile()) {
			return std::make_unique<Layout::Video>(file, item);
		}
		return nullptr;
	case Type::File:
		if (auto file = getFile()) {
			return std::make_unique<Layout::Document>(file, item, st::overviewFileLayout);
		}
		return nullptr;
	case Type::MusicFile:
		if (auto file = getFile()) {
			return std::make_unique<Layout::Document>(file, item, st::overviewFileLayout);
		}
		return nullptr;
	case Type::VoiceFile:
		if (auto file = getFile()) {
			return std::make_unique<Layout::Voice>(file, item, st::overviewFileLayout);
		}
		return nullptr;
	case Type::Link:
		return std::make_unique<Layout::Link>(item->getMedia(), item);
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
	for (auto &section : _sections) {
		section.resizeToWidth(newWidth);
	}
	return recountHeight();
}

void ListWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto outerWidth = width();
	auto clip = e->rect();
	auto ms = getms();
	auto top = st::infoMediaMargin.top();
	p.translate(0, top);
	clip = clip.translated(0, -top);
	for (auto &section : _sections) {
		section.paint(p, clip, outerWidth, ms);
		auto height = section.height();
		p.translate(0, height);
		clip = clip.translated(0, -height);
	}
}

int ListWidget::recountHeight() {
	auto result = 0;
	for (auto &section : _sections) {
		result += section.height();
	}
	return st::infoMediaMargin.top()
		+ result
		+ st::infoMediaMargin.bottom();
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

ListWidget::~ListWidget() = default;

} // namespace Media
} // namespace Info
