/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_media.h"
#include "ui/effects/animations.h"
#include "data/data_poll.h"
#include "base/weak_ptr.h"

namespace Data {
class CloudImageView;
} // namespace Data

namespace Ui {
class RippleAnimation;
class FireworksAnimation;
} // namespace Ui

namespace HistoryView {

class Poll : public Media, public base::has_weak_ptr {
public:
	Poll(
		not_null<Element*> parent,
		not_null<PollData*> poll);
	~Poll();

	void draw(Painter &p, const PaintContext &context) const override;
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

	[[nodiscard]] TextSelection adjustSelection(
		TextSelection selection,
		TextSelectType type) const override;
	uint16 fullSelectionLength() const override;
	TextForMimeData selectedText(TextSelection selection) const override;

	BubbleRoll bubbleRoll() const override;
	QMargins bubbleRollRepaintMargins() const override;
	void paintBubbleFireworks(
		Painter &p,
		const QRect &bubble,
		crl::time ms) const override;

	void clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) override;

	void unloadHeavyPart() override;
	bool hasHeavyPart() const override;

private:
	struct AnswerAnimation;
	struct AnswersAnimation;
	struct SendingAnimation;
	struct Answer;
	struct CloseInformation;

	struct RecentVoter {
		not_null<UserData*> user;
		mutable std::shared_ptr<Data::CloudImageView> userpic;
	};

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	[[nodiscard]] bool showVotes() const;
	[[nodiscard]] bool canVote() const;
	[[nodiscard]] bool canSendVotes() const;

	[[nodiscard]] int countAnswerTop(
		const Answer &answer,
		int innerWidth) const;
	[[nodiscard]] int countAnswerHeight(
		const Answer &answer,
		int innerWidth) const;
	[[nodiscard]] ClickHandlerPtr createAnswerClickHandler(
		const Answer &answer);
	void updateTexts();
	void updateRecentVoters();
	void updateAnswers();
	void updateVotes();
	void updateTotalVotes();
	bool showVotersCount() const;
	bool inlineFooter() const;
	void updateAnswerVotes();
	void updateAnswerVotesFromOriginal(
		Answer &answer,
		const PollAnswer &original,
		int percent,
		int maxVotes);
	void checkSendingAnimation() const;

	void paintRecentVoters(
		Painter &p,
		int left,
		int top,
		const PaintContext &context) const;
	void paintCloseByTimer(
		Painter &p,
		int right,
		int top,
		const PaintContext &context) const;
	void paintShowSolution(
		Painter &p,
		int right,
		int top,
		const PaintContext &context) const;
	int paintAnswer(
		Painter &p,
		const Answer &answer,
		const AnswerAnimation *animation,
		int left,
		int top,
		int width,
		int outerWidth,
		const PaintContext &context) const;
	void paintRadio(
		Painter &p,
		const Answer &answer,
		int left,
		int top,
		const PaintContext &context) const;
	void paintPercent(
		Painter &p,
		const QString &percent,
		int percentWidth,
		int left,
		int top,
		int outerWidth,
		const PaintContext &context) const;
	void paintFilling(
		Painter &p,
		bool chosen,
		bool correct,
		float64 filling,
		int left,
		int top,
		int width,
		int height,
		const PaintContext &context) const;
	void paintInlineFooter(
		Painter &p,
		int left,
		int top,
		int paintw,
		const PaintContext &context) const;
	void paintBottom(
		Painter &p,
		int left,
		int top,
		int paintw,
		const PaintContext &context) const;

	bool checkAnimationStart() const;
	bool answerVotesChanged() const;
	void saveStateInAnimation() const;
	void startAnswersAnimation() const;
	void resetAnswersAnimation() const;
	void radialAnimationCallback() const;

	void toggleRipple(Answer &answer, bool pressed);
	void toggleLinkRipple(bool pressed);
	void toggleMultiOption(const QByteArray &option);
	void sendMultiOptions();
	void showResults();
	void checkQuizAnswered();
	void showSolution() const;
	void solutionToggled(
		bool solutionShown,
		anim::type animated = anim::type::normal) const;

	[[nodiscard]] bool canShowSolution() const;
	[[nodiscard]] bool inShowSolution(
		QPoint point,
		int right,
		int top) const;

	[[nodiscard]] int bottomButtonHeight() const;

	const not_null<PollData*> _poll;
	int _pollVersion = 0;
	int _totalVotes = 0;
	bool _voted = false;
	PollData::Flags _flags = PollData::Flags();

	Ui::Text::String _question;
	Ui::Text::String _subtitle;
	std::vector<RecentVoter> _recentVoters;
	QImage _recentVotersImage;

	std::vector<Answer> _answers;
	Ui::Text::String _totalVotesLabel;
	ClickHandlerPtr _showResultsLink;
	ClickHandlerPtr _sendVotesLink;
	mutable ClickHandlerPtr _showSolutionLink;
	mutable std::unique_ptr<Ui::RippleAnimation> _linkRipple;

	mutable std::unique_ptr<AnswersAnimation> _answersAnimation;
	mutable std::unique_ptr<SendingAnimation> _sendingAnimation;
	mutable std::unique_ptr<Ui::FireworksAnimation> _fireworksAnimation;
	Ui::Animations::Simple _wrongAnswerAnimation;
	mutable QPoint _lastLinkPoint;
	mutable QImage _userpicCircleCache;
	mutable QImage _fillingIconCache;

	mutable std::unique_ptr<CloseInformation> _close;

	mutable Ui::Animations::Simple _solutionButtonAnimation;
	mutable bool _solutionShown = false;
	mutable bool _solutionButtonVisible = false;

	bool _hasSelected = false;
	bool _votedFromHere = false;
	mutable bool _wrongAnswerAnimated = false;

};

} // namespace HistoryView
