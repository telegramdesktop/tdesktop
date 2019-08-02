/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_media.h"
#include "base/weak_ptr.h"
#include "base/timer.h"

struct HistoryMessageVia;
struct HistoryMessageReply;
struct HistoryMessageForwarded;

namespace Lottie {
class SinglePlayer;
} // namespace Lottie

namespace HistoryView {

class Sticker : public Media, public base::has_weak_ptr {
public:
	Sticker(
		not_null<Element*> parent,
		not_null<DocumentData*> document);
	~Sticker();

	void draw(Painter &p, const QRect &r, TextSelection selection, crl::time ms) const override;
	TextState textState(QPoint point, StateRequest request) const override;

	bool toggleSelectionByHandlerClick(const ClickHandlerPtr &p) const override {
		return true;
	}
	bool dragItem() const override {
		return true;
	}
	bool dragItemByHandler(const ClickHandlerPtr &p) const override {
		return true;
	}

	DocumentData *getDocument() const override {
		return _data;
	}

	bool needsBubble() const override {
		return false;
	}
	bool customInfoLayout() const override {
		return true;
	}
	bool hidesForwardedInfo() const override {
		return true;
	}
	void clearStickerLoopPlayed() override {
		_lottieOncePlayed = false;
	}

	void unloadHeavyPart() override {
		unloadLottie();
	}

private:
	[[nodiscard]] bool isEmojiSticker() const;

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	bool needInfoDisplay() const;
	int additionalWidth(const HistoryMessageVia *via, const HistoryMessageReply *reply) const;
	int additionalWidth() const;

	void setupLottie();
	void unloadLottie();

	int _pixw = 1;
	int _pixh = 1;
	ClickHandlerPtr _packLink;
	DocumentData *_data = nullptr;
	std::unique_ptr<Lottie::SinglePlayer> _lottie;
	mutable bool _lottieOncePlayed = false;

	rpl::lifetime _lifetime;

};

} // namespace HistoryView
