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

class Dice final : public UnwrappedMedia::Content {
public:
	Dice(not_null<Element*> parent, not_null<Data::MediaDice*> dice);
	~Dice();

	QSize size() override;
	void draw(Painter &p, const QRect &r, bool selected) override;

	ClickHandlerPtr link() override;

	void clearStickerLoopPlayed() override {
	}
	void unloadHeavyPart() override {
		_start.unloadHeavyPart();
		if (_end) {
			_end->unloadHeavyPart();
		}
	}
	bool hidesForwardedInfo() override {
		return false;
	}

private:
	const not_null<Element*> _parent;
	const not_null<Data::MediaDice*> _dice;
	std::optional<Sticker> _end;
	Sticker _start;
	mutable bool _showLastFrame = false;
	mutable bool _drawingEnd = false;

};

} // namespace HistoryView
