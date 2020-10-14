/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_media_unwrapped.h"
#include "history/view/media/history_view_sticker.h"

namespace Data {
class MediaDice;
} // namespace Data

namespace HistoryView {

class SlotMachine final : public UnwrappedMedia::Content {
public:
	SlotMachine(not_null<Element*> parent, not_null<Data::MediaDice*> dice);
	~SlotMachine();

	QSize size() override;
	void draw(Painter &p, const QRect &r, bool selected) override;

	ClickHandlerPtr link() override;

	bool hasHeavyPart() const override {
		if (_pull && _pull->hasHeavyPart()) {
			return true;
		}
		for (auto i = 0; i != 4; ++i) {
			if ((_start[i] && _start[i]->hasHeavyPart())
				|| (_end[i] && _end[i]->hasHeavyPart())) {
				return true;
			}
		}
		return false;
	}
	void unloadHeavyPart() override {
		if (_pull) {
			_pull->unloadHeavyPart();
		}
		for (auto i = 0; i != 4; ++i) {
			if (_start[i]) {
				_start[i]->unloadHeavyPart();
			}
			if (_end[i]) {
				_end[i]->unloadHeavyPart();
			}
		}
	}
	bool hidesForwardedInfo() override {
		return false;
	}

private:
	void resolveStarts(bool initSize = false);
	void resolveEnds(int value);
	[[nodiscard]] bool isEndResolved() const;
	void resolve(
		std::optional<Sticker> &sticker,
		int singleTimeIndex,
		int index,
		bool initSize) const;

	const not_null<Element*> _parent;
	const not_null<Data::MediaDice*> _dice;
	ClickHandlerPtr _link;
	std::optional<Sticker> _pull;
	std::array<std::optional<Sticker>, 4> _start;
	std::array<std::optional<Sticker>, 4> _end;
	mutable bool _showLastFrame = false;
	mutable std::array<bool, 4> _drawingEnd = { { false } };

};

} // namespace HistoryView
