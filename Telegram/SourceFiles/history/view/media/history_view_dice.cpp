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
#include "history/view/history_view_element.h"
#include "main/main_session.h"

namespace HistoryView {
namespace {

DocumentData *Lookup(not_null<Element*> view, int value) {
	const auto &session = view->data()->history()->session();
	return session.diceStickersPack().lookup(value);
}

} // namespace

Dice::Dice(not_null<Element*> parent, int value)
: _parent(parent)
, _start(parent, Lookup(parent, 0))
, _value(value) {
	_start.setDiceIndex(0);
}

Dice::~Dice() = default;

QSize Dice::size() {
	return _start.size();
}

void Dice::draw(Painter &p, const QRect &r, bool selected) {
	Expects(_end.has_value() || !_drawingEnd);

	if (_drawingEnd) {
		_end->draw(p, r, selected);
	} else {
		_start.draw(p, r, selected);
		if (!_end && _value) {
			if (const auto document = Lookup(_parent, _value)) {
				_end.emplace(_parent, document);
				_end->setDiceIndex(_value);
				_end->initSize();
			}
		}
		if (_end && _end->readyToDrawLottie() && _start.atTheEnd()) {
			_drawingEnd = true;
		}
	}
}

} // namespace HistoryView
