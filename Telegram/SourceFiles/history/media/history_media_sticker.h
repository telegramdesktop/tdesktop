/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/media/history_media.h"
#include "base/timer.h"

struct HistoryMessageVia;
struct HistoryMessageReply;
struct HistoryMessageForwarded;

namespace Lottie {
class SinglePlayer;
} // namespace Lottie

class HistorySticker : public HistoryMedia {
public:
	HistorySticker(
		not_null<Element*> parent,
		not_null<DocumentData*> document);
	~HistorySticker();

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
	QString emoji() const {
		return _emoji;
	}
	bool hidesForwardedInfo() const override {
		return true;
	}

	void unloadHeavyPart() override {
		unloadLottie();
	}

private:
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
	not_null<DocumentData*> _data;
	QString _emoji;
	std::unique_ptr<Lottie::SinglePlayer> _lottie;
	rpl::lifetime _lifetime;

};
