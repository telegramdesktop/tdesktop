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
#include "history/history_item_helpers.h"
#include "history/view/history_view_element.h"
#include "lang/lang_keys.h"
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
	if (const auto outcome = _dice->diceGameOutcome()) {
		_outcomeSet = true;
		_outcomeValue = _dice->value();
		_outcomeNanoTon = outcome.nanoTon;
		_outcomeStakeNanoTon = outcome.stakeNanoTon;
		_outcomeStartedUnknown = (_outcomeValue == 0);
		updateOutcomeMessage();
	}
}

Dice::~Dice() = default;

void Dice::updateOutcomeMessage() {
	if ((_outcomeStartedUnknown && !_outcomeLastPainted)
		|| (!_outcomeValue && !_outcomeNanoTon)) {
		_parent->setServicePostMessage({});
		return;
	}
	const auto item = _parent->data();
	const auto from = item->from();
	const auto out = item->out() || from->isSelf();
	const auto won = (_outcomeNanoTon - _outcomeStakeNanoTon);
	const auto amount = tr::marked(QString::fromUtf8("\xf0\x9f\x92\x8e")
		+ " "
		+ QString::number(std::abs(won) / 1e9));
	const auto text = out
		? (won >= 0
			? tr::lng_action_stake_game_won_you
			: tr::lng_action_stake_game_lost_you)(
				tr::now,
				lt_amount,
				amount,
				tr::marked)
		: (won >= 0
			? tr::lng_action_stake_game_won
			: tr::lng_action_stake_game_lost)(
				tr::now,
				lt_from,
				tr::link(st::wrap_rtl(from->name()), 1),
				lt_amount,
				amount,
				tr::marked);
	auto prepared = PreparedServiceText{ text };
	if (!out) {
		prepared.links.push_back(from->createOpenLink());
	}
	_parent->setServicePostMessage(prepared, _link);
}

QSize Dice::countOptimalSize() {
	return _start ? _start->countOptimalSize() : Sticker::EmojiSize();
}

ClickHandlerPtr Dice::link() {
	return _link;
}

bool Dice::updateItemData() {
	const auto outcome = _dice->diceGameOutcome();
	const auto outcomeSet = !!outcome;
	const auto outcomeNanoTon = outcomeSet ? outcome.nanoTon : 0;
	const auto outcomeStakeNanoTon = outcomeSet ? outcome.stakeNanoTon : 0;
	const auto outcomeValue = _dice->value();
	if (_outcomeSet == outcomeSet
		&& _outcomeNanoTon == outcomeNanoTon
		&& _outcomeStakeNanoTon == outcomeStakeNanoTon
		&& _outcomeValue == outcomeValue) {
		return false;
	}
	_outcomeSet = outcomeSet;
	_outcomeNanoTon = outcomeNanoTon;
	_outcomeStakeNanoTon = outcomeStakeNanoTon;
	_outcomeValue = outcomeValue;
	if (_outcomeSet) {
		updateOutcomeMessage();
	}
	return true;
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
		if (!_outcomeLastPainted && _end->stoppedOnLastFrame()) {
			_outcomeLastPainted = true;
			if (_outcomeSet) {
				crl::on_main(this, [=] {
					updateOutcomeMessage();
					_parent->history()->owner().requestViewResize(_parent);
				});
			}
		}
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
