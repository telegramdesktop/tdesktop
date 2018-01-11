/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_element.h"

#include "history/history_item_components.h"
#include "history/history_media.h"
#include "data/data_session.h"
#include "auth_session.h"

namespace HistoryView {

Element::Element(not_null<HistoryItem*> data, Context context)
: _data(data)
, _context(context) {
}

not_null<HistoryItem*> Element::data() const {
	return _data;
}

void Element::attachToBlock(not_null<HistoryBlock*> block, int index) {
	Expects(!_data->isLogEntry());
	Expects(_block == nullptr);
	Expects(_indexInBlock < 0);
	Expects(index >= 0);

	_block = block;
	_indexInBlock = index;
	_data->setMainView(this);
	_data->setPendingResize();
}

void Element::removeFromBlock() {
	Expects(_block != nullptr);

	_block->remove(this);
}

Element *Element::previousInBlocks() const {
	if (_block && _indexInBlock >= 0) {
		if (_indexInBlock > 0) {
			return _block->messages[_indexInBlock - 1].get();
		}
		if (auto previous = _block->previousBlock()) {
			Assert(!previous->messages.empty());
			return previous->messages.back().get();
		}
	}
	return nullptr;
}

Element *Element::nextInBlocks() const {
	if (_block && _indexInBlock >= 0) {
		if (_indexInBlock + 1 < _block->messages.size()) {
			return _block->messages[_indexInBlock + 1].get();
		}
		if (auto next = _block->nextBlock()) {
			Assert(!next->messages.empty());
			return next->messages.front().get();
		}
	}
	return nullptr;
}

void Element::clickHandlerActiveChanged(
		const ClickHandlerPtr &handler,
		bool active) {
	if (const auto markup = _data->Get<HistoryMessageReplyMarkup>()) {
		if (const auto keyboard = markup->inlineKeyboard.get()) {
			keyboard->clickHandlerActiveChanged(handler, active);
		}
	}
	App::hoveredLinkItem(active ? this : nullptr);
	Auth().data().requestItemRepaint(_data);
	if (const auto media = _data->getMedia()) {
		media->clickHandlerActiveChanged(handler, active);
	}
}

void Element::clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) {
	if (const auto markup = _data->Get<HistoryMessageReplyMarkup>()) {
		if (const auto keyboard = markup->inlineKeyboard.get()) {
			keyboard->clickHandlerPressedChanged(handler, pressed);
		}
	}
	App::pressedLinkItem(pressed ? this : nullptr);
	Auth().data().requestItemRepaint(_data);
	if (const auto media = _data->getMedia()) {
		media->clickHandlerPressedChanged(handler, pressed);
	}
}

Element::~Element() {
	App::messageViewDestroyed(this);
}

} // namespace HistoryView
