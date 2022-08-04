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
	const auto &session = view->history()->session();
	return session.diceStickersPacks().lookup(emoji, value);
}

} // namespace

Dice::Dice(not_null<Element*> parent, not_null<Data::MediaDice*> dice)
: _parent(parent)
, _dice(dice)
, _link(dice->makeHandler()) {
	if (const auto document = Lookup(parent, dice->emoji(), 0)) {
		const auto skipPremiumEffect = false;
		_start.emplace(parent, document, skipPremiumEffect);
		_start->setDiceIndex(_dice->emoji(), 0);
	}
	_showLastFrame = _parent->data()->Has<HistoryMessageForwarded>();
	if (_showLastFrame) {
		_drawingEnd = true;
	}
}

Dice::~Dice() = default;

QSize Dice::countOptimalSize() {
	return _start ? _start->countOptimalSize() : Sticker::EmojiSize();
}

ClickHandlerPtr Dice::link() {
	return _link;
}

void Dice::draw(Painter &p, const PaintContext &context, const QRect &r) {
	if (!_start) {
		if (const auto document = Lookup(_parent, _dice->emoji(), 0)) {
			const auto skipPremiumEffect = false;
			_start.emplace(_parent, document, skipPremiumEffect);
			_start->setDiceIndex(_dice->emoji(), 0);
			_start->initSize();
		}
	}
	if (const auto value = _end ? 0 : _dice->value()) {
		if (const auto document = Lookup(_parent, _dice->emoji(), value)) {
			const auto skipPremiumEffect = false;
			_end.emplace(_parent, document, skipPremiumEffect);
			_end->setDiceIndex(_dice->emoji(), value);
			_end->initSize();
		}
	}
	if (!_end) {
		_drawingEnd = false;
	}
	if (_drawingEnd) {
		_end->draw(p, context, r);
	} else if (_start) {
		_start->draw(p, context, r);
		if (_end
			&& _end->readyToDrawAnimationFrame()
			&& _start->atTheEnd()) {
			_drawingEnd = true;
		}
	}
}

} // namespace HistoryView
