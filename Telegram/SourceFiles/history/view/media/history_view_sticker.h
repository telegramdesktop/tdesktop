/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_media_unwrapped.h"
#include "base/weak_ptr.h"

namespace Data {
struct FileOrigin;
} // namespace Data

namespace Lottie {
class SinglePlayer;
struct ColorReplacements;
} // namespace Lottie

namespace HistoryView {

class Sticker final
	: public UnwrappedMedia::Content
	, public base::has_weak_ptr {
public:
	Sticker(
		not_null<Element*> parent,
		not_null<DocumentData*> document,
		const Lottie::ColorReplacements *replacements = nullptr);
	~Sticker();

	void initSize();
	QSize size() override;
	void draw(Painter &p, const QRect &r, bool selected) override;
	ClickHandlerPtr link() override {
		return _link;
	}

	DocumentData *document() override {
		return _document;
	}
	void clearStickerLoopPlayed() override {
		_lottieOncePlayed = false;
	}
	void unloadHeavyPart() override {
		unloadLottie();
	}
	void refreshLink() override;

	void setDiceIndex(int index);
	[[nodiscard]] bool atTheEnd() const {
		return _atTheEnd;
	}
	[[nodiscard]] bool readyToDrawLottie();

private:
	[[nodiscard]] bool isEmojiSticker() const;
	void paintLottie(Painter &p, const QRect &r, bool selected);
	void paintPixmap(Painter &p, const QRect &r, bool selected);
	[[nodiscard]] QPixmap paintedPixmap(bool selected) const;

	void setupLottie();
	void unloadLottie();

	const not_null<Element*> _parent;
	const not_null<DocumentData*> _document;
	const Lottie::ColorReplacements *_replacements = nullptr;
	std::unique_ptr<Lottie::SinglePlayer> _lottie;
	ClickHandlerPtr _link;
	QSize _size;
	QImage _lastDiceFrame;
	int _diceIndex = -1;
	mutable bool _lottieOncePlayed = false;
	mutable bool _atTheEnd = false;
	mutable bool _nextLastDiceFrame = false;

	rpl::lifetime _lifetime;

};

} // namespace HistoryView
