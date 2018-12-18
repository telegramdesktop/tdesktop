/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/media/history_media.h"

struct HistoryMessageVia;
struct HistoryMessageReply;
struct HistoryMessageForwarded;

class HistorySticker : public HistoryMedia {
public:
	HistorySticker(
		not_null<Element*> parent,
		not_null<DocumentData*> document);

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
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

private:
	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	bool needInfoDisplay() const;
	int additionalWidth(const HistoryMessageVia *via, const HistoryMessageReply *reply) const;
	int additionalWidth() const;

	int _pixw = 1;
	int _pixh = 1;
	ClickHandlerPtr _packLink;
	not_null<DocumentData*> _data;
	QString _emoji;

};
