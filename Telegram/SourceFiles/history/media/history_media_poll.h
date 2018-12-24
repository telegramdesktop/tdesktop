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

	void clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) override;

	~HistoryPoll();

private:
	struct AnswerAnimation;
	struct AnswersAnimation;
	struct SendingAnimation;
	struct Answer;

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	bool canVote() const;

	[[nodiscard]] int countAnswerTop(
		const Answer &answer,
		int innerWidth) const;
	[[nodiscard]] int countAnswerHeight(
		const Answer &answer,
		int innerWidth) const;
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
	void updateVotesCheckAnimations() const;

	int paintAnswer(
		Painter &p,
		const Answer &answer,
		const AnswerAnimation *animation,
		int left,
		int top,
		int width,
		int outerWidth,
		TextSelection selection,
		TimeMs ms) const;
	void paintRadio(
		Painter &p,
		const Answer &answer,
		int left,
		int top,
		TextSelection selection) const;
	void paintPercent(
		Painter &p,
		const QString &percent,
		int percentWidth,
		int left,
		int top,
		int outerWidth,
		TextSelection selection) const;
	void paintFilling(
		Painter &p,
		float64 filling,
		int left,
		int top,
		int width,
		int height,
		TextSelection selection) const;

	bool checkAnimationStart() const;
	bool answerVotesChanged() const;
	void saveStateInAnimation() const;
	void startAnswersAnimation() const;
	void resetAnswersAnimation() const;
	void step_radial(TimeMs ms, bool timer);

	void checkPollResultsReload(TimeMs ms) const;
	void toggleRipple(Answer &answer, bool pressed);

	not_null<PollData*> _poll;
	int _pollVersion = 0;
	mutable int _totalVotes = 0;
	mutable bool _voted = false;
	bool _closed = false;

	Text _question;
	Text _subtitle;
	std::vector<Answer> _answers;
	mutable Text _totalVotesLabel;

	mutable std::unique_ptr<AnswersAnimation> _answersAnimation;
	mutable std::unique_ptr<SendingAnimation> _sendingAnimation;
	mutable QPoint _lastLinkPoint;

};
