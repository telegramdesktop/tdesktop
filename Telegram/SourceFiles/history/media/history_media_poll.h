/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/media/history_media.h"

struct PollAnswer;

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

	~HistoryPoll();

private:
	struct Answer {
		Answer();

		Text text;
		QByteArray option;
		mutable int votes = 0;
		mutable int votesPercentWidth = 0;
		mutable float64 filling = 0.;
		mutable QString votesPercent;
		mutable bool chosen = false;
		ClickHandlerPtr handler;
	};
	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	int countAnswerHeight(const Answer &answer, int innerWidth) const;
	[[nodiscard]] ClickHandlerPtr createAnswerClickHandler(
		const Answer &answer) const;
	void updateTexts();
	void updateAnswers();
	void updateVotes() const;
	void updateTotalVotes() const;
	void updateAnswerVotes() const;
	void updateAnswerVotesFromOriginal(
		const Answer &answer,
		const PollAnswer &original,
		int totalVotes,
		int maxVotes) const;

	int paintAnswer(
		Painter &p,
		const Answer &answer,
		int left,
		int top,
		int width,
		int outerWidth,
		TextSelection selection,
		TimeMs ms) const;

	not_null<PollData*> _poll;
	int _pollVersion = 0;
	mutable int _totalVotes = 0;
	mutable bool _voted = false;
	bool _closed = false;

	Text _question;
	Text _subtitle;
	std::vector<Answer> _answers;
	mutable Text _totalVotesLabel;

};
