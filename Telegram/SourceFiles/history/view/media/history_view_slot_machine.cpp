/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_slot_machine.h"

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

constexpr auto kStartBackIndex = 0;
constexpr auto kWinBackIndex = 1;
constexpr auto kPullIndex = 2;
constexpr auto kShifts = std::array<int, 3>{ 3, 9, 15 };
constexpr auto kSevenWinIndex = 0;
constexpr auto kSevenIndex = 1;
constexpr auto kBarIndex = 2;
constexpr auto kBerriesIndex = 3;
constexpr auto kLemonIndex = 4;
constexpr auto kStartIndex = 5;
constexpr auto kWinValue = 64;
constexpr auto kSkipFramesBeforeWinEnding = 90;

const auto &kEmoji = ::Stickers::DicePacks::kSlotString;

[[nodiscard]] DocumentData *Lookup(
		not_null<Element*> view,
		int value) {
	const auto &session = view->data()->history()->session();
	return session.diceStickersPacks().lookup(kEmoji, value);
}

[[nodiscard]] int ComplexIndex(int partIndex, int inPartIndex) {
	Expects(partIndex >= 0 && partIndex < 3);

	return kShifts[partIndex] + inPartIndex;
}

[[nodiscard]] int ComputePartValue(int value, int partIndex) {
	return ((value - 1) >> (partIndex * 2)) & 0x03; // 0..3
}

[[nodiscard]] int ComputeComplexIndex(int value, int partIndex) {
	Expects(value > 0 && value <= 64);

	if (value == kWinValue) {
		return ComplexIndex(partIndex, kSevenWinIndex);
	}
	return ComplexIndex(partIndex, [&] {
		switch (ComputePartValue(value, partIndex)) {
		case 0: return kBarIndex;
		case 1: return kBerriesIndex;
		case 2: return kLemonIndex;
		case 3: return kSevenIndex;
		}
		Unexpected("Part value value in ComputeComplexIndex.");
	}());
}

} // namespace

SlotMachine::SlotMachine(
	not_null<Element*> parent,
	not_null<Data::MediaDice*> dice)
: _parent(parent)
, _dice(dice)
, _link(dice->makeHandler()) {
	resolveStarts();
	_showLastFrame = _parent->data()->Has<HistoryMessageForwarded>();
	if (_showLastFrame) {
		for (auto &drawingEnd : _drawingEnd) {
			drawingEnd = true;
		}
	}
}

SlotMachine::~SlotMachine() = default;

void SlotMachine::resolve(
		std::optional<Sticker> &sticker,
		int singleTimeIndex,
		int index,
		bool initSize) const {
	if (sticker) {
		return;
	}
	const auto document = Lookup(_parent, index);
	if (!document) {
		return;
	}
	sticker.emplace(_parent, document);
	sticker->setDiceIndex(kEmoji, singleTimeIndex);
	if (initSize) {
		sticker->initSize();
	}
}

void SlotMachine::resolveStarts(bool initSize) {
	resolve(_pull, kPullIndex, kPullIndex, initSize);
	resolve(_start[0], 0, kStartBackIndex, initSize);
	for (auto i = 0; i != 3; ++i) {
		resolve(_start[i + 1], 0, ComplexIndex(i, kStartIndex), initSize);
	}
}

void SlotMachine::resolveEnds(int value) {
	if (value <= 0 || value > 64) {
		return;
	}
	const auto firstPartValue = ComputePartValue(value, 0);
	if (ComputePartValue(value, 1) == firstPartValue
		&& ComputePartValue(value, 2) == firstPartValue) { // Three in a row.
		resolve(_end[0], kWinBackIndex, kWinBackIndex, true);
	}
	for (auto i = 0; i != 3; ++i) {
		const auto index = ComputeComplexIndex(value, i);
		resolve(_end[i + 1], index, index, true);
	}
}

bool SlotMachine::isEndResolved() const {
	for (auto i = 0; i != 3; ++i) {
		if (!_end[i + 1]) {
			return false;
		}
	}
	return _end[0].has_value() || (_dice->value() != kWinValue);
}

QSize SlotMachine::size() {
	return _pull
		? _pull->size()
		: Sticker::GetAnimatedEmojiSize(&_parent->history()->session());
}

ClickHandlerPtr SlotMachine::link() {
	return _link;
}

void SlotMachine::draw(Painter &p, const QRect &r, bool selected) {
	resolveStarts(true);
	resolveEnds(_dice->value());

	//const auto endResolved = isEndResolved();
	//if (!endResolved) {
	//	for (auto &drawingEnd : _drawingEnd) {
	//		drawingEnd = false;
	//	}
	//}
	auto switchedToEnd = _drawingEnd;
	const auto pullReady = _pull && _pull->readyToDrawLottie();
	const auto paintReady = [&] {
		auto result = pullReady;
		auto allPlayedEnough = true;
		for (auto i = 1; i != 4; ++i) {
			if (!_end[i] || !_end[i]->readyToDrawLottie()) {
				switchedToEnd[i] = false;
			}
			if (!switchedToEnd[i]
				&& (!_start[i] || !_start[i]->readyToDrawLottie())) {
				result = false;
			}
			const auto playedTillFrame = !switchedToEnd[i]
				? 0
				: _end[i]->frameIndex().value_or(0);
			if (playedTillFrame < kSkipFramesBeforeWinEnding) {
				allPlayedEnough = false;
			}
		}
		if (!_end[0] || !_end[0]->readyToDrawLottie() || !allPlayedEnough) {
			switchedToEnd[0] = false;
		}
		if (ranges::contains(switchedToEnd, false)
			&& (!_start[0] || !_start[0]->readyToDrawLottie())) {
			result = false;
		}
		return result;
	}();

	if (!paintReady) {
		return;
	}

	for (auto i = 0; i != 4; ++i) {
		if (switchedToEnd[i]) {
			_end[i]->draw(p, r, selected);
		} else {
			_start[i]->draw(p, r, selected);
			if (_end[i]
				&& _end[i]->readyToDrawLottie()
				&& _start[i]->atTheEnd()) {
				_drawingEnd[i] = true;
			}
		}
	}
	_pull->draw(p, r, selected);
}

} // namespace HistoryView
