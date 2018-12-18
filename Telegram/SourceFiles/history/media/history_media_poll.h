/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/media/history_media.h"

class HistoryPoll : public HistoryMedia {
public:
	HistoryPoll(
		not_null<Element*> parent,
		not_null<PollData*> poll);

	void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const override;
	TextState textState(QPoint point, StateRequest request) const override;

	bool toggleSelectionByHandlerClick(const ClickHandlerPtr &p) const override {
		return true;
	}
	bool dragItemByHandler(const ClickHandlerPtr &p) const override {
		return true;
	}

	bool needsBubble() const override {
		return true;
	}
	bool customInfoLayout() const override {
		return false;
	}

private:
	struct Answer {
		Answer();

		Text text;
		QByteArray option;
		mutable int votes = 0;
		mutable bool chosen = false;
	};
	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	void updateTexts();
	void updateVotes() const;
	void updateTotalVotes() const;

	not_null<PollData*> _poll;
	int _pollVersion = 0;
	mutable int _totalVotes = 0;
	mutable bool _voted = false;

	Text _question;
	Text _subtitle;
	std::vector<Answer> _answers;
	mutable Text _totalVotesLabel;

};
