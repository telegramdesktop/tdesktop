/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/media/info_media_list_section.h"

#include "storage/storage_shared_media.h"
#include "layout/layout_selection.h"
#include "styles/style_info.h"

namespace Info::Media {
namespace {

constexpr auto kFloatingHeaderAlpha = 0.9;

} // namespace

ListSection::ListSection(Type type, not_null<ListSectionDelegate*> delegate)
: _type(type)
, _delegate(delegate)
, _hasFloatingHeader(delegate->sectionHasFloatingHeader())
, _mosaic(st::emojiPanWidth - st::inlineResultsLeft) {
}

bool ListSection::empty() const {
	return _items.empty();
}

UniversalMsgId ListSection::minId() const {
	Expects(!empty());

	return GetUniversalId(_items.back()->getItem());
}

void ListSection::setTop(int top) {
	_top = top;
}

int ListSection::top() const {
	return _top;
}

int ListSection::height() const {
	return _height;
}

int ListSection::bottom() const {
	return top() + height();
}

bool ListSection::addItem(not_null<BaseLayout*> item) {
	if (_items.empty() || belongsHere(item)) {
		if (_items.empty()) {
			setHeader(item);
		}
		appendItem(item);
		return true;
	}
	return false;
}

void ListSection::finishSection() {
	if (_type == Type::GIF) {
		_mosaic.setOffset(st::infoMediaSkip, headerHeight());
		_mosaic.setRightSkip(st::infoMediaSkip);
		_mosaic.addItems(_items);
	}
}

void ListSection::setHeader(not_null<BaseLayout*> item) {
	_header.setText(st::infoMediaHeaderStyle, _delegate->sectionTitle(item));
}

bool ListSection::belongsHere(
		not_null<BaseLayout*> item) const {
	Expects(!_items.empty());

	return _delegate->sectionItemBelongsHere(item, _items.back());
}

void ListSection::appendItem(not_null<BaseLayout*> item) {
	_items.push_back(item);
	_byItem.emplace(item->getItem(), item);
}

bool ListSection::removeItem(not_null<const HistoryItem*> item) {
	if (const auto i = _byItem.find(item); i != end(_byItem)) {
		_items.erase(ranges::remove(_items, i->second), end(_items));
		_byItem.erase(i);
		refreshHeight();
		return true;
	}
	return false;
}

QRect ListSection::findItemRect(
		not_null<const BaseLayout*> item) const {
	auto position = item->position();
	if (!_mosaic.empty()) {
		return _mosaic.findRect(position);
	}
	auto top = position / _itemsInRow;
	auto indexInRow = position % _itemsInRow;
	auto left = _itemsLeft
		+ indexInRow * (_itemWidth + st::infoMediaSkip);
	return QRect(left, top, _itemWidth, item->height());
}

ListFoundItem ListSection::completeResult(
		not_null<BaseLayout*> item,
		bool exact) const {
	return { item, findItemRect(item), exact };
}

ListFoundItem ListSection::findItemByPoint(QPoint point) const {
	Expects(!_items.empty());

	if (!_mosaic.empty()) {
		const auto found = _mosaic.findByPoint(point);
		Assert(found.index != -1);
		const auto item = _mosaic.itemAt(found.index);
		const auto rect = findItemRect(item);
		return { item, rect, found.exact };
	}
	auto itemIt = findItemAfterTop(point.y());
	if (itemIt == end(_items)) {
		--itemIt;
	}
	auto item = *itemIt;
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
		item = *itemIt;
		rect = findItemRect(item);
	}
	return { item, rect, rect.contains(point) };
}

std::optional<ListFoundItem> ListSection::findItemByItem(
		not_null<const HistoryItem*> item) const {
	const auto i = _byItem.find(item);
	if (i != end(_byItem)) {
		return ListFoundItem{ i->second, findItemRect(i->second), true };
	}
	return std::nullopt;
}

ListFoundItem ListSection::findItemNearId(UniversalMsgId universalId) const {
	Expects(!_items.empty());

	// #TODO downloads
	auto itemIt = ranges::lower_bound(
		_items,
		universalId,
		std::greater<>(),
		[](const auto &item) { return GetUniversalId(item); });
	if (itemIt == _items.end()) {
		--itemIt;
	}
	const auto item = *itemIt;
	const auto exact = (GetUniversalId(item) == universalId);
	return { item, findItemRect(item), exact };
}

ListFoundItem ListSection::findItemDetails(
		not_null<BaseLayout*> item) const {
	return { item, findItemRect(item), true };
}

auto ListSection::findItemAfterTop(
		int top) -> Items::iterator {
	Expects(_mosaic.empty());

	return ranges::lower_bound(
		_items,
		top,
		std::less_equal<>(),
		[this](const auto &item) {
			const auto itemTop = item->position() / _itemsInRow;
			return itemTop + item->height();
		});
}

auto ListSection::findItemAfterTop(
		int top) const -> Items::const_iterator {
	Expects(_mosaic.empty());

	return ranges::lower_bound(
		_items,
		top,
		std::less_equal<>(),
		[this](const auto &item) {
			const auto itemTop = item->position() / _itemsInRow;
			return itemTop + item->height();
		});
}

auto ListSection::findItemAfterBottom(
		Items::const_iterator from,
		int bottom) const -> Items::const_iterator {
	Expects(_mosaic.empty());
	return ranges::lower_bound(
		from,
		_items.end(),
		bottom,
		std::less<>(),
		[this](const auto &item) {
			const auto itemTop = item->position() / _itemsInRow;
			return itemTop;
		});
}

const ListSection::Items &ListSection::items() const {
	return _items;
}

void ListSection::paint(
		Painter &p,
		const ListContext &context,
		QRect clip,
		int outerWidth) const {
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
	auto localContext = context.layoutContext;
	if (!_mosaic.empty()) {
		auto paintItem = [&](not_null<BaseLayout*> item, QPoint point) {
			p.translate(point.x(), point.y());
			item->paint(
				p,
				clip.translated(-point),
				itemSelection(item, context),
				&localContext);
			p.translate(-point.x(), -point.y());
		};
		_mosaic.paint(std::move(paintItem), clip);
		return;
	}

	auto fromIt = findItemAfterTop(clip.y());
	auto tillIt = findItemAfterBottom(
		fromIt,
		clip.y() + clip.height());
	for (auto it = fromIt; it != tillIt; ++it) {
		auto item = *it;
		auto rect = findItemRect(item);
		localContext.skipBorder = (rect.y() <= header + _itemsTop);
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

void ListSection::paintFloatingHeader(
		Painter &p,
		int visibleTop,
		int outerWidth) {
	if (!_hasFloatingHeader) {
		return;
	}
	const auto headerTop = st::infoMediaHeaderPosition.y() / 2;
	if (visibleTop <= (_top + headerTop)) {
		return;
	}
	const auto header = headerHeight();
	const auto headerLeft = st::infoMediaHeaderPosition.x();
	const auto floatingTop = std::min(
		visibleTop,
		bottom() - header + headerTop);
	p.save();
	p.resetTransform();
	p.setOpacity(kFloatingHeaderAlpha);
	p.fillRect(QRect(0, floatingTop, outerWidth, header), st::boxBg);
	p.setOpacity(1.0);
	p.setPen(st::infoMediaHeaderFg);
	_header.drawLeftElided(
		p,
		headerLeft,
		floatingTop + headerTop,
		outerWidth - 2 * headerLeft,
		outerWidth);
	p.restore();
}

TextSelection ListSection::itemSelection(
		not_null<const BaseLayout*> item,
		const ListContext &context) const {
	const auto parent = item->getItem();
	auto dragSelectAction = context.dragSelectAction;
	if (dragSelectAction != ListDragSelectAction::None) {
		auto i = context.dragSelected->find(parent);
		if (i != context.dragSelected->end()) {
			return (dragSelectAction == ListDragSelectAction::Selecting)
				? FullSelection
				: TextSelection();
		}
	}
	auto i = context.selected->find(parent);
	return (i == context.selected->cend())
		? TextSelection()
		: i->second.text;
}

int ListSection::headerHeight() const {
	return _header.isEmpty() ? 0 : st::infoMediaHeaderHeight;
}

void ListSection::resizeToWidth(int newWidth) {
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
			item->resizeGetHeight(_itemWidth);
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
			item->resizeGetHeight(_itemWidth);
		}
	} break;

	case Type::GIF: {
		_mosaic.setFullWidth(newWidth - st::infoMediaSkip);
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

int ListSection::recountHeight() {
	auto result = headerHeight();

	switch (_type) {
	case Type::Photo:
	case Type::Video:
	case Type::RoundFile: {
		auto itemHeight = _itemWidth + st::infoMediaSkip;
		auto index = 0;
		result += _itemsTop;
		for (auto &item : _items) {
			item->setPosition(_itemsInRow * result + index);
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

	case Type::GIF: {
		return _mosaic.countDesiredHeight(0) + result;
	} break;

	case Type::RoundVoiceFile:
	case Type::File:
	case Type::MusicFile:
	case Type::Link:
		for (auto &item : _items) {
			item->setPosition(result);
			result += item->height();
		}
		_rowsCount = _items.size();
		break;
	}

	return result;
}

void ListSection::refreshHeight() {
	_height = recountHeight();
}

} // namespace Info::Media
