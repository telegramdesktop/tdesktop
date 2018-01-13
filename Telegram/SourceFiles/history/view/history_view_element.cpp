/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_element.h"

#include "history/history_item_components.h"
#include "history/history_media.h"
#include "history/history.h"
#include "data/data_session.h"
#include "auth_session.h"

namespace HistoryView {
namespace {

// A new message from the same sender is attached to previous within 15 minutes.
constexpr int kAttachMessageToPreviousSecondsDelta = 900;

} // namespace
Element::Element(not_null<HistoryItem*> data, Context context)
: _data(data)
, _context(context) {
}

not_null<HistoryItem*> Element::data() const {
	return _data;
}

Context Element::context() const {
	return _context;
}

int Element::y() const {
	return _y;
}

void Element::setY(int y) {
	_y = y;
}

int Element::marginTop() const {
	const auto item = data();
	auto result = 0;
	if (!item->isHiddenByGroup()) {
		if (isAttachedToPrevious()) {
			result += st::msgMarginTopAttached;
		} else {
			result += st::msgMargin.top();
		}
	}
	result += item->displayedDateHeight();
	if (const auto unreadbar = item->Get<HistoryMessageUnreadBar>()) {
		result += unreadbar->height();
	}
	return result;
}

int Element::marginBottom() const {
	const auto item = data();
	return item->isHiddenByGroup() ? 0 : st::msgMargin.bottom();
}

void Element::setPendingResize() {
	_flags |= Flag::NeedsResize;
	if (_context == Context::History) {
		data()->_history->setHasPendingResizedItems();
	}
}

bool Element::pendingResize() const {
	return _flags & Flag::NeedsResize;
}

bool Element::isAttachedToPrevious() const {
	return _flags & Flag::AttachedToPrevious;
}

bool Element::isAttachedToNext() const {
	return _flags & Flag::AttachedToNext;
}

void Element::previousInBlocksChanged() {
	recountDisplayDateInBlocks();
	recountAttachToPreviousInBlocks();
}

// Called only if there is no more next item! Not always when it changes!
void Element::nextInBlocksChanged() {
	setAttachToNext(false);
}

bool Element::computeIsAttachToPrevious(not_null<Element*> previous) {
	const auto item = data();
	if (!item->Has<HistoryMessageDate>() && !item->Has<HistoryMessageUnreadBar>()) {
		const auto prev = previous->data();
		const auto possible = !item->isPost() && !prev->isPost()
			&& !item->serviceMsg() && !prev->serviceMsg()
			&& !item->isEmpty() && !prev->isEmpty()
			&& (qAbs(prev->date.secsTo(item->date)) < kAttachMessageToPreviousSecondsDelta);
		if (possible) {
			if (item->history()->peer->isSelf()) {
				return prev->senderOriginal() == item->senderOriginal()
					&& (prev->Has<HistoryMessageForwarded>() == item->Has<HistoryMessageForwarded>());
			} else {
				return prev->from() == item->from();
			}
		}
	}
	return false;
}

void Element::recountAttachToPreviousInBlocks() {
	auto attachToPrevious = false;
	if (const auto previous = previousInBlocks()) {
		attachToPrevious = computeIsAttachToPrevious(previous);
		previous->setAttachToNext(attachToPrevious);
	}
	setAttachToPrevious(attachToPrevious);
}

void Element::recountDisplayDateInBlocks() {
	setDisplayDate([&] {
		const auto item = data();
		if (item->isEmpty()) {
			return false;
		}

		if (auto previous = previousInBlocks()) {
			const auto prev = previous->data();
			return prev->isEmpty() || (prev->date.date() != item->date.date());
		}
		return true;
	}());
}

QSize Element::countOptimalSize() {
	return performCountOptimalSize();
}

QSize Element::countCurrentSize(int newWidth) {
	if (_flags & Flag::NeedsResize) {
		_flags &= ~Flag::NeedsResize;
		initDimensions();
	}
	return performCountCurrentSize(newWidth);
}

void Element::setDisplayDate(bool displayDate) {
	const auto item = data();
	if (displayDate && !item->Has<HistoryMessageDate>()) {
		item->AddComponents(HistoryMessageDate::Bit());
		item->Get<HistoryMessageDate>()->init(item->date);
		setPendingResize();
	} else if (!displayDate && item->Has<HistoryMessageDate>()) {
		item->RemoveComponents(HistoryMessageDate::Bit());
		setPendingResize();
	}
}

void Element::setAttachToNext(bool attachToNext) {
	if (attachToNext && !(_flags & Flag::AttachedToNext)) {
		_flags |= Flag::AttachedToNext;
		Auth().data().requestItemRepaint(data());
	} else if (!attachToNext && (_flags & Flag::AttachedToNext)) {
		_flags &= ~Flag::AttachedToNext;
		Auth().data().requestItemRepaint(data());
	}
}

void Element::setAttachToPrevious(bool attachToPrevious) {
	if (attachToPrevious && !(_flags & Flag::AttachedToPrevious)) {
		_flags |= Flag::AttachedToPrevious;
		setPendingResize();
	} else if (!attachToPrevious && (_flags & Flag::AttachedToPrevious)) {
		_flags &= ~Flag::AttachedToPrevious;
		setPendingResize();
	}
}

bool Element::displayFromPhoto() const {
	return false;
}

bool Element::hasFromPhoto() const {
	return false;
}

bool Element::hasFromName() const {
	return false;
}

bool Element::displayFromName() const {
	return false;
}

HistoryBlock *Element::block() {
	return _block;
}

const HistoryBlock *Element::block() const {
	return _block;
}

void Element::attachToBlock(not_null<HistoryBlock*> block, int index) {
	Expects(!_data->isLogEntry());
	Expects(_block == nullptr);
	Expects(_indexInBlock < 0);
	Expects(index >= 0);

	_block = block;
	_indexInBlock = index;
	_data->setMainView(this);
}

void Element::removeFromBlock() {
	Expects(_block != nullptr);

	_block->remove(this);
}

void Element::setIndexInBlock(int index) {
	Expects(_block != nullptr);
	Expects(index >= 0);

	_indexInBlock = index;
}

int Element::indexInBlock() const {
	Expects((_indexInBlock >= 0) == (_block != nullptr));
	Expects((_block == nullptr) || (_block->messages[_indexInBlock].get() == this));

	return _indexInBlock;
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
