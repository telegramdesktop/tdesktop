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
#include "ui/toast/toast.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"

namespace HistoryView {
namespace {

[[nodiscard]] DocumentData *Lookup(
		not_null<Element*> view,
		const QString &emoji,
		int value) {
	const auto &session = view->data()->history()->session();
	return session.diceStickersPacks().lookup(emoji, value);
}

[[nodiscard]] ClickHandlerPtr MakeDiceHandler(const QString &emoji) {
	return std::make_shared<LambdaClickHandler>([=] {
		auto config = Ui::Toast::Config();
		config.multiline = true;
		config.minWidth = st::msgMinWidth;
		config.text = { tr::lng_about_random(tr::now, lt_emoji, emoji) };
		Ui::Toast::Show(config);
	});
}

} // namespace

Dice::Dice(not_null<Element*> parent, not_null<Data::MediaDice*> dice)
: _parent(parent)
, _dice(dice)
, _link(_parent->data()->Has<HistoryMessageForwarded>()
	? nullptr
	: MakeDiceHandler(dice->emoji()))
, _start(parent, Lookup(parent, dice->emoji(), 0)) {
	_showLastFrame = _parent->data()->Has<HistoryMessageForwarded>();
	_start.setDiceIndex(_dice->emoji(), 0);
	if (_showLastFrame) {
		_drawingEnd = true;
	}
}

Dice::~Dice() = default;

QSize Dice::size() {
	return _start.size();
}

ClickHandlerPtr Dice::link() {
	return _link;
}

void Dice::draw(Painter &p, const QRect &r, bool selected) {
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
	} else {
		_start.draw(p, r, selected);
		if (_end && _end->readyToDrawLottie() && _start.atTheEnd()) {
			_drawingEnd = true;
		}
	}
}

} // namespace HistoryView
