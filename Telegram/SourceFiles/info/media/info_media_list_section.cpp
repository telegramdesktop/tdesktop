/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/media/info_media_list_section.h"

#include "storage/storage_shared_media.h"
#include "layout/layout_selection.h"
#include "ui/rect.h"
#include "ui/painter.h"
#include "styles/style_chat_helpers.h"
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

void ListSection::setCanReorder(bool value) {
	_canReorder = value;
}

int ListSection::height() const {
	return _height;
}

int ListSection::bottom() const {
	return top() + height();
}

bool ListSection::isOneColumn() const {
	return _itemsInRow == 1;
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
		_mosaic.setPadding({
			st::infoMediaSkip,
			headerHeight(),
			st::infoMediaSkip,
			st::stickerPanPadding,
		});
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

void ListSection::reorderItems(int oldPosition, int newPosition) {
	base::reorder(_items, oldPosition, newPosition);
	refreshHeight();
}

QRect ListSection::findItemRect(
		not_null<const BaseLayout*> item) const {
	const auto position = item->position();
	if (!_mosaic.empty()) {
		return _mosaic.findRect(position);
	}
	const auto top = position / _itemsInRow;
	const auto indexInRow = position % _itemsInRow;
	const auto left = _itemsLeft
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
	const auto header = headerHeight();
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
		const auto paintItem = [&](not_null<BaseLayout*> item, QPoint point) {
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

	const auto fromIt = findItemAfterTop(clip.y());
	const auto tillIt = findItemAfterBottom(
		fromIt,
		clip.y() + clip.height());
	for (auto it = fromIt; it != tillIt; ++it) {
		const auto item = *it;
		if (item == context.draggedItem) {
			continue;
		}
		auto rect = findItemRect(item);
		rect.translate(item->shift());
		localContext.skipBorder = (rect.y() <= header + _itemsTop);
		if (rect.intersects(clip)) {
			p.translate(rect.topLeft());
			item->paint(
				p,
				clip.translated(-rect.topLeft()),
				itemSelection(item, context),
				&localContext);
			p.translate(-rect.topLeft());

			if (_canReorder && isOneColumn()) {
				st::stickersReorderIcon.paint(
					p,
					rect::right(rect) - oneColumnRightPadding(),
					(rect.height() - st::stickersReorderIcon.height()) / 2
						+ rect.y(),
					outerWidth);
			}
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
	const auto dragSelectAction = context.dragSelectAction;
	if (dragSelectAction != ListDragSelectAction::None) {
		const auto i = context.dragSelected->find(parent);
		if (i != context.dragSelected->end()) {
			return (dragSelectAction == ListDragSelectAction::Selecting)
				? FullSelection
				: TextSelection();
		}
	}
	const auto i = context.selected->find(parent);
	return (i == context.selected->cend())
		? TextSelection()
		: i->second.text;
}

int ListSection::headerHeight() const {
	return _header.isEmpty() ? 0 : st::infoMediaHeaderHeight;
}

int ListSection::oneColumnRightPadding() const {
	return !isOneColumn()
		? 0
		: _canReorder
		? st::stickersReorderIcon.width() + st::infoMediaLeft
		: 0;
}

void ListSection::resizeToWidth(int newWidth) {
	const auto minWidth = st::infoMediaMinGridSize + st::infoMediaSkip * 2;
	if (newWidth < minWidth) {
		return;
	}

	const auto resizeOneColumn = [&](int itemsLeft, int itemWidth) {
		const auto rightPadding = oneColumnRightPadding();
		_itemsLeft = itemsLeft;
		_itemsTop = 0;
		_itemsInRow = 1;
		_itemWidth = itemWidth - rightPadding;
		for (auto &item : _items) {
			item->resizeGetHeight(_itemWidth - rightPadding);
		}
	};
	switch (_type) {
	case Type::Photo:
	case Type::Video:
	case Type::PhotoVideo:
	case Type::RoundFile: {
		const auto skip = st::infoMediaSkip;
		_itemsLeft = st::infoMediaLeft;
		_itemsTop = st::infoMediaSkip;
		_itemsInRow = (newWidth - _itemsLeft * 2 + skip)
			/ (st::infoMediaMinGridSize + skip);
		_itemWidth = ((newWidth - _itemsLeft * 2 + skip) / _itemsInRow)
			- st::infoMediaSkip;
		_itemsLeft = (newWidth - (_itemWidth + skip) * _itemsInRow + skip)
			/ 2;
		for (auto &item : _items) {
			_itemHeight = item->resizeGetHeight(_itemWidth);
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
		const auto itemsLeft = st::infoMediaHeaderPosition.x();
		const auto itemWidth = newWidth - 2 * itemsLeft;
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
	case Type::PhotoVideo:
	case Type::RoundFile: {
		const auto itemHeight = _itemHeight + st::infoMediaSkip;
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
		return _mosaic.countDesiredHeight(0);
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
