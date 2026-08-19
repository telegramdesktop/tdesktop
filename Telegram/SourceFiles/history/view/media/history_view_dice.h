/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "history/view/media/history_view_media_unwrapped.h"
#include "history/view/media/history_view_sticker.h"

namespace Data {
class MediaDice;
} // namespace Data

namespace HistoryView {

class Dice final
	: public UnwrappedMedia::Content
	, public base::has_weak_ptr {
public:
	Dice(not_null<Element*> parent, not_null<Data::MediaDice*> dice);
	~Dice();

	QSize countOptimalSize() override;
	void draw(
		Painter &p,
		const PaintContext &context,
		const QRect &r) override;

	ClickHandlerPtr link() override;

	bool hasHeavyPart() const override {
		return (_start ? _start->hasHeavyPart() : false)
			|| (_end ? _end->hasHeavyPart() : false);
	}
	void unloadHeavyPart() override {
		if (_start) {
			_start->unloadHeavyPart();
		}
		if (_end) {
			_end->unloadHeavyPart();
		}
	}

	bool updateItemData() override;

private:
	void updateOutcomeMessage();

	const not_null<Element*> _parent;
	const not_null<Data::MediaDice*> _dice;
	ClickHandlerPtr _link;
	std::optional<Sticker> _start;
	std::optional<Sticker> _end;
	int64 _outcomeNanoTon = 0;
	int64 _outcomeStakeNanoTon = 0;
	int _outcomeValue = 0;
	mutable bool _showLastFrame = false;
	mutable bool _drawingEnd = false;
	bool _outcomeSet = false;
	bool _outcomeLastPainted = false;
	bool _outcomeStartedUnknown = false;

};

} // namespace HistoryView
