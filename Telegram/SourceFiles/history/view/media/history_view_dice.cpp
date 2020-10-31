/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_dice.h"

#include "data/data_session.h"
#include "chat_helpers/stickers_dice_pack.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/view/history_view_element.h"
#include "main/main_session.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

[[nodiscard]] DocumentData *Lookup(
		not_null<Element*> view,
		const QString &emoji,
		int value) {
	const auto &session = view->data()->history()->session();
	return session.diceStickersPacks().lookup(emoji, value);
}

} // namespace

Dice::Dice(not_null<Element*> parent, not_null<Data::MediaDice*> dice)
: _parent(parent)
, _dice(dice)
, _link(dice->makeHandler()) {
	if (const auto document = Lookup(parent, dice->emoji(), 0)) {
		_start.emplace(parent, document);
		_start->setDiceIndex(_dice->emoji(), 0);
	}
	_showLastFrame = _parent->data()->Has<HistoryMessageForwarded>();
	if (_showLastFrame) {
		_drawingEnd = true;
	}
}

Dice::~Dice() = default;

QSize Dice::size() {
	return _start
		? _start->size()
		: Sticker::GetAnimatedEmojiSize(&_parent->history()->session());
}

ClickHandlerPtr Dice::link() {
	return _link;
}

void Dice::draw(Painter &p, const QRect &r, bool selected) {
	if (!_start) {
		if (const auto document = Lookup(_parent, _dice->emoji(), 0)) {
			_start.emplace(_parent, document);
			_start->setDiceIndex(_dice->emoji(), 0);
			_start->initSize();
		}
	}
	if (const auto value = _end ? 0 : _dice->value()) {
		if (const auto document = Lookup(_parent, _dice->emoji(), value)) {
			_end.emplace(_parent, document);
			_end->setDiceIndex(_dice->emoji(), value);
			_end->initSize();
		}
	}
	if (!_end) {
		_drawingEnd = false;
	}
	if (_drawingEnd) {
		_end->draw(p, r, selected);
	} else if (_start) {
		_start->draw(p, r, selected);
		if (_end && _end->readyToDrawLottie() && _start->atTheEnd()) {
			_drawingEnd = true;
		}
	}
}

} // namespace HistoryView
