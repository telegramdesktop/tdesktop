/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_poll.h"

#include "core/ui_integration.h" // TextContext
#include "lang/lang_keys.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_message.h"
#include "history/view/history_view_cursor_state.h"
#include "calls/calls_instance.h"
#include "ui/chat/message_bubble.h"
#include "ui/chat/chat_style.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/text/format_values.h"
#include "ui/effects/animations.h"
#include "ui/effects/radial_animation.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/fireworks_animation.h"
#include "ui/toast/toast.h"
#include "ui/painter.h"
#include "data/data_media_types.h"
#include "data/data_poll.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "base/unixtime.h"
#include "base/timer.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "api/api_polls.h"
#include "styles/style_chat.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

namespace HistoryView {
namespace {

constexpr auto kShowRecentVotersCount = 3;
constexpr auto kRotateSegments = 8;
constexpr auto kRotateAmplitude = 3.;
constexpr auto kScaleSegments = 2;
constexpr auto kScaleAmplitude = 0.03;
constexpr auto kRollDuration = crl::time(400);
constexpr auto kLargestRadialDuration = 30 * crl::time(1000);
constexpr auto kCriticalCloseDuration = 5 * crl::time(1000);

struct PercentCounterItem {
	int index = 0;
	int percent = 0;
	int remainder = 0;

	inline bool operator==(const PercentCounterItem &o) const {
		return remainder == o.remainder && percent == o.percent;
	}

	inline bool operator<(const PercentCounterItem &other) const {
		if (remainder > other.remainder) {
			return true;
		} else if (remainder < other.remainder) {
			return false;
		}
		return percent < other.percent;
	}
};

void AdjustPercentCount(gsl::span<PercentCounterItem> items, int left) {
	ranges::sort(items, std::less<>());
	for (auto i = 0, count = int(items.size()); i != count;) {
		const auto &item = items[i];
		auto j = i + 1;
		for (; j != count; ++j) {
			if (items[j].percent != item.percent
				|| items[j].remainder != item.remainder) {
				break;
			}
		}
		if (!items[i].remainder) {
			// If this item has correct value in 'percent' we don't want
			// to increment it to an incorrect one. This fixes a case with
			// four items with three votes for three different items.
			break;
		}
		const auto equal = j - i;
		if (equal <= left) {
			left -= equal;
			for (; i != j; ++i) {
				++items[i].percent;
			}
		} else {
			i = j;
		}
	}
}

void CountNicePercent(
		gsl::span<const int> votes,
		int total,
		gsl::span<int> result) {
	Expects(result.size() >= votes.size());
	Expects(votes.size() <= PollData::kMaxOptions);

	const auto count = size_type(votes.size());
	PercentCounterItem ItemsStorage[PollData::kMaxOptions];
	const auto items = gsl::make_span(ItemsStorage).subspan(0, count);
	auto left = 100;
	auto &&zipped = ranges::views::zip(
		votes,
		items,
		ranges::views::ints(0, int(items.size())));
	for (auto &&[votes, item, index] : zipped) {
		item.index = index;
		item.percent = (votes * 100) / total;
		item.remainder = (votes * 100) - (item.percent * total);
		left -= item.percent;
	}
	if (left > 0 && left <= count) {
		AdjustPercentCount(items, left);
	}
	for (const auto &item : items) {
		result[item.index] = item.percent;
	}
}

} // namespace

struct Poll::AnswerAnimation {
	anim::value percent;
	anim::value filling;
	anim::value opacity;
	bool chosen = false;
	bool correct = false;
};

struct Poll::AnswersAnimation {
	std::vector<AnswerAnimation> data;
	Ui::Animations::Simple progress;
};

struct Poll::SendingAnimation {
	template <typename Callback>
	SendingAnimation(
		const QByteArray &option,
		Callback &&callback);

	QByteArray option;
	Ui::InfiniteRadialAnimation animation;
};

struct Poll::Answer {
	Answer();

	void fillData(
		not_null<PollData*> poll,
		const PollAnswer &original,
		Ui::Text::MarkedContext context);

	Ui::Text::String text;
	QByteArray option;
	int votes = 0;
	int votesPercent = 0;
	int votesPercentWidth = 0;
	float64 filling = 0.;
	QString votesPercentString;
	bool chosen = false;
	bool correct = false;
	bool selected = false;
	ClickHandlerPtr handler;
	Ui::Animations::Simple selectedAnimation;
	mutable std::unique_ptr<Ui::RippleAnimation> ripple;
};

struct Poll::CloseInformation {
	CloseInformation(TimeId date, TimeId period, Fn<void()> repaint);

	crl::time start = 0;
	crl::time finish = 0;
	crl::time duration = 0;
	base::Timer timer;
	Ui::Animations::Basic radial;
};

struct Poll::RecentVoter {
	not_null<PeerData*> peer;
	mutable Ui::PeerUserpicView userpic;
};

template <typename Callback>
Poll::SendingAnimation::SendingAnimation(
	const QByteArray &option,
	Callback &&callback)
: option(option)
, animation(
	std::forward<Callback>(callback),
	st::historyPollRadialAnimation) {
}

Poll::Answer::Answer() : text(st::msgMinWidth / 2) {
}

void Poll::Answer::fillData(
		not_null<PollData*> poll,
		const PollAnswer &original,
		Ui::Text::MarkedContext context) {
	chosen = original.chosen;
	correct = poll->quiz() ? original.correct : chosen;
	if (!text.isEmpty() && text.toTextWithEntities() == original.text) {
		return;
	}
	text.setMarkedText(
		st::historyPollAnswerStyle,
		original.text,
		Ui::WebpageTextTitleOptions(),
		context);
}

Poll::CloseInformation::CloseInformation(
	TimeId date,
	TimeId period,
	Fn<void()> repaint)
: duration(period * crl::time(1000))
, timer(std::move(repaint)) {
	const auto left = std::clamp(date - base::unixtime::now(), 0, period);
	finish = crl::now() + left * crl::time(1000);
}

Poll::Poll(
	not_null<Element*> parent,
	not_null<PollData*> poll)
: Media(parent)
, _poll(poll)
, _question(st::msgMinWidth / 2)
, _showResultsLink(
	std::make_shared<LambdaClickHandler>(crl::guard(
		this,
		[=] { showResults(); })))
, _sendVotesLink(
	std::make_shared<LambdaClickHandler>(crl::guard(
		this,
		[=] { sendMultiOptions(); }))) {
	history()->owner().registerPollView(_poll, _parent);
}

QSize Poll::countOptimalSize() {
	updateTexts();

	const auto paddings = st::msgPadding.left() + st::msgPadding.right();

	auto maxWidth = st::msgFileMinWidth;
	accumulate_max(maxWidth, paddings + _question.maxWidth());
	for (const auto &answer : _answers) {
		accumulate_max(
			maxWidth,
			paddings
			+ st::historyPollAnswerPadding.left()
			+ answer.text.maxWidth()
			+ st::historyPollAnswerPadding.right());
	}

	const auto answersHeight = ranges::accumulate(ranges::views::all(
		_answers
	) | ranges::views::transform([](const Answer &answer) {
		return st::historyPollAnswerPadding.top()
			+ answer.text.minHeight()
			+ st::historyPollAnswerPadding.bottom();
	}), 0);

	const auto bottomButtonHeight = inlineFooter()
		? 0
		: st::historyPollBottomButtonSkip;
	auto minHeight = st::historyPollQuestionTop
		+ _question.minHeight()
		+ st::historyPollSubtitleSkip
		+ st::msgDateFont->height
		+ st::historyPollAnswersSkip
		+ answersHeight
		+ st::historyPollTotalVotesSkip
		+ bottomButtonHeight
		+ st::msgDateFont->height
		+ st::msgPadding.bottom();
	if (!isBubbleTop()) {
		minHeight -= st::msgFileTopMinus;
	}
	return { maxWidth, minHeight };
}

bool Poll::showVotes() const {
	return _voted || (_flags & PollData::Flag::Closed);
}

bool Poll::canVote() const {
	return !showVotes() && _parent->data()->isRegular();
}

bool Poll::canSendVotes() const {
	return canVote() && _hasSelected;
}

bool Poll::showVotersCount() const {
	return showVotes()
		? (!_totalVotes || !(_flags & PollData::Flag::PublicVotes))
		: !(_flags & PollData::Flag::MultiChoice);
}

bool Poll::inlineFooter() const {
	return !(_flags
		& (PollData::Flag::PublicVotes | PollData::Flag::MultiChoice));
}

int Poll::countAnswerTop(
		const Answer &answer,
		int innerWidth) const {
	auto tshift = st::historyPollQuestionTop;
	if (!isBubbleTop()) {
		tshift -= st::msgFileTopMinus;
	}
	tshift += _question.countHeight(innerWidth) + st::historyPollSubtitleSkip;
	tshift += st::msgDateFont->height + st::historyPollAnswersSkip;
	const auto i = ranges::find(
		_answers,
		&answer,
		[](const Answer &answer) { return &answer; });
	const auto countHeight = [&](const Answer &answer) {
		return countAnswerHeight(answer, innerWidth);
	};
	tshift += ranges::accumulate(
		begin(_answers),
		i,
		0,
		ranges::plus(),
		countHeight);
	return tshift;
}

int Poll::countAnswerHeight(
		const Answer &answer,
		int innerWidth) const {
	const auto answerWidth = innerWidth
		- st::historyPollAnswerPadding.left()
		- st::historyPollAnswerPadding.right();
	return st::historyPollAnswerPadding.top()
		+ answer.text.countHeight(answerWidth)
		+ st::historyPollAnswerPadding.bottom();
}

QSize Poll::countCurrentSize(int newWidth) {
	accumulate_min(newWidth, maxWidth());
	const auto innerWidth = newWidth
		- st::msgPadding.left()
		- st::msgPadding.right();

	const auto answersHeight = ranges::accumulate(ranges::views::all(
		_answers
	) | ranges::views::transform([&](const Answer &answer) {
		return countAnswerHeight(answer, innerWidth);
	}), 0);

	const auto bottomButtonHeight = inlineFooter()
		? 0
		: st::historyPollBottomButtonSkip;
	auto newHeight = st::historyPollQuestionTop
		+ _question.countHeight(innerWidth)
		+ st::historyPollSubtitleSkip
		+ st::msgDateFont->height
		+ st::historyPollAnswersSkip
		+ answersHeight
		+ st::historyPollTotalVotesSkip
		+ bottomButtonHeight
		+ st::msgDateFont->height
		+ st::msgPadding.bottom();
	if (!isBubbleTop()) {
		newHeight -= st::msgFileTopMinus;
	}
	return { newWidth, newHeight };
}

void Poll::updateTexts() {
	if (_pollVersion == _poll->version) {
		return;
	}
	const auto first = !_pollVersion;
	_pollVersion = _poll->version;

	const auto willStartAnimation = checkAnimationStart();
	const auto voted = _voted;

	if (_question.toTextWithEntities() != _poll->question) {
		auto options = Ui::WebpageTextTitleOptions();
		options.maxw = options.maxh = 0;
		_question.setMarkedText(
			st::historyPollQuestionStyle,
			_poll->question,
			options,
			Core::TextContext({
				.session = &_poll->session(),
				.repaint = [=] { repaint(); },
				.customEmojiLoopLimit = 2,
			}));
	}
	if (_flags != _poll->flags() || _subtitle.isEmpty()) {
		using Flag = PollData::Flag;
		_flags = _poll->flags();
		_subtitle.setText(
			st::msgDateTextStyle,
			((_flags & Flag::Closed)
				? tr::lng_polls_closed(tr::now)
				: (_flags & Flag::Quiz)
				? ((_flags & Flag::PublicVotes)
					? tr::lng_polls_public_quiz(tr::now)
					: tr::lng_polls_anonymous_quiz(tr::now))
				: ((_flags & Flag::PublicVotes)
					? tr::lng_polls_public(tr::now)
					: tr::lng_polls_anonymous(tr::now))));
	}
	updateRecentVoters();
	updateAnswers();
	updateVotes();

	if (willStartAnimation) {
		startAnswersAnimation();
		if (!voted) {
			checkQuizAnswered();
		}
	}
	solutionToggled(
		_solutionShown,
		first ? anim::type::instant : anim::type::normal);
}

void Poll::checkQuizAnswered() {
	if (!_voted || !_votedFromHere || !_poll->quiz() || anim::Disabled()) {
		return;
	}
	const auto i = ranges::find(_answers, true, &Answer::chosen);
	if (i == end(_answers)) {
		return;
	}
	if (i->correct) {
		_fireworksAnimation = std::make_unique<Ui::FireworksAnimation>(
			[=] { repaint(); });
	} else {
		_wrongAnswerAnimation.start(
			[=] { repaint(); },
			0.,
			1.,
			kRollDuration,
			anim::linear);
		showSolution();
	}
}

void Poll::showSolution() const {
	if (!_poll->solution.text.isEmpty()) {
		solutionToggled(true);
		_parent->delegate()->elementShowTooltip(
			_poll->solution,
			crl::guard(this, [=] { solutionToggled(false); }));
	}
}

void Poll::solutionToggled(
		bool solutionShown,
		anim::type animated) const {
	_solutionShown = solutionShown;
	const auto visible = canShowSolution() && !_solutionShown;
	if (_solutionButtonVisible == visible) {
		if (animated == anim::type::instant
			&& _solutionButtonAnimation.animating()) {
			_solutionButtonAnimation.stop();
			repaint();
		}
		return;
	}
	_solutionButtonVisible = visible;
	history()->owner().notifyViewLayoutChange(_parent);
	if (animated == anim::type::instant) {
		_solutionButtonAnimation.stop();
		repaint();
	} else {
		_solutionButtonAnimation.start(
			[=] { repaint(); },
			visible ? 0. : 1.,
			visible ? 1. : 0.,
			st::fadeWrapDuration);
	}
}

void Poll::updateRecentVoters() {
	auto &&sliced = ranges::views::all(
		_poll->recentVoters
	) | ranges::views::take(kShowRecentVotersCount);
	const auto changed = !ranges::equal(
		_recentVoters,
		sliced,
		ranges::equal_to(),
		&RecentVoter::peer);
	if (changed) {
		auto updated = ranges::views::all(
			sliced
		) | ranges::views::transform([](not_null<PeerData*> peer) {
			return RecentVoter{ peer };
		}) | ranges::to_vector;
		const auto has = hasHeavyPart();
		if (has) {
			for (auto &voter : updated) {
				const auto i = ranges::find(
					_recentVoters,
					voter.peer,
					&RecentVoter::peer);
				if (i != end(_recentVoters)) {
					voter.userpic = std::move(i->userpic);
				}
			}
		}
		_recentVoters = std::move(updated);
		if (has && !hasHeavyPart()) {
			_parent->checkHeavyPart();
		}
	}
}

void Poll::updateAnswers() {
	const auto context = Core::TextContext({
		.session = &_poll->session(),
		.repaint = [=] { repaint(); },
		.customEmojiLoopLimit = 2,
	});
	const auto changed = !ranges::equal(
		_answers,
		_poll->answers,
		ranges::equal_to(),
		&Answer::option,
		&PollAnswer::option);
	if (!changed) {
		auto &&answers = ranges::views::zip(_answers, _poll->answers);
		for (auto &&[answer, original] : answers) {
			answer.fillData(_poll, original, context);
		}
		return;
	}
	_answers = ranges::views::all(
		_poll->answers
	) | ranges::views::transform([&](const PollAnswer &answer) {
		auto result = Answer();
		result.option = answer.option;
		result.fillData(_poll, answer, context);
		return result;
	}) | ranges::to_vector;

	for (auto &answer : _answers) {
		answer.handler = createAnswerClickHandler(answer);
	}

	resetAnswersAnimation();
}

ClickHandlerPtr Poll::createAnswerClickHandler(
		const Answer &answer) {
	const auto option = answer.option;
	if (_flags & PollData::Flag::MultiChoice) {
		return std::make_shared<LambdaClickHandler>(crl::guard(this, [=] {
			toggleMultiOption(option);
		}));
	}
	return std::make_shared<LambdaClickHandler>(crl::guard(this, [=] {
		_votedFromHere = true;
		history()->session().api().polls().sendVotes(
			_parent->data()->fullId(),
			{ option });
	}));
}

void Poll::toggleMultiOption(const QByteArray &option) {
	const auto i = ranges::find(
		_answers,
		option,
		&Answer::option);
	if (i != end(_answers)) {
		const auto selected = i->selected;
		i->selected = !selected;
		i->selectedAnimation.start(
			[=] { repaint(); },
			selected ? 1. : 0.,
			selected ? 0. : 1.,
			st::defaultCheck.duration);
		if (selected) {
			const auto j = ranges::find(
				_answers,
				true,
				&Answer::selected);
			_hasSelected = (j != end(_answers));
		} else {
			_hasSelected = true;
		}
		repaint();
	}
}

void Poll::sendMultiOptions() {
	auto chosen = _answers | ranges::views::filter(
		&Answer::selected
	) | ranges::views::transform(
		&Answer::option
	) | ranges::to_vector;
	if (!chosen.empty()) {
		_votedFromHere = true;
		history()->session().api().polls().sendVotes(
			_parent->data()->fullId(),
			std::move(chosen));
	}
}

void Poll::showResults() {
	_parent->delegate()->elementShowPollResults(
		_poll,
		_parent->data()->fullId());
}

void Poll::updateVotes() {
	const auto voted = _poll->voted();
	if (_voted != voted) {
		_voted = voted;
		if (_voted) {
			for (auto &answer : _answers) {
				answer.selected = false;
			}
		} else {
			_votedFromHere = false;
		}
	}
	updateAnswerVotes();
	updateTotalVotes();
}

void Poll::checkSendingAnimation() const {
	const auto &sending = _poll->sendingVotes;
	const auto sendingRadial = (sending.size() == 1)
		&& !(_flags & PollData::Flag::MultiChoice);
	if (sendingRadial == (_sendingAnimation != nullptr)) {
		if (_sendingAnimation) {
			_sendingAnimation->option = sending.front();
		}
		return;
	}
	if (!sendingRadial) {
		if (!_answersAnimation) {
			_sendingAnimation = nullptr;
		}
		return;
	}
	_sendingAnimation = std::make_unique<SendingAnimation>(
		sending.front(),
		[=] { radialAnimationCallback(); });
	_sendingAnimation->animation.start();
}

void Poll::updateTotalVotes() {
	if (_totalVotes == _poll->totalVoters && !_totalVotesLabel.isEmpty()) {
		return;
	}
	_totalVotes = _poll->totalVoters;
	const auto quiz = _poll->quiz();
	const auto string = !_totalVotes
		? (quiz
			? tr::lng_polls_answers_none
			: tr::lng_polls_votes_none)(tr::now)
		: (quiz
			? tr::lng_polls_answers_count
			: tr::lng_polls_votes_count)(
				tr::now,
				lt_count_short,
				_totalVotes);
	_totalVotesLabel.setText(st::msgDateTextStyle, string);
}

void Poll::updateAnswerVotesFromOriginal(
		Answer &answer,
		const PollAnswer &original,
		int percent,
		int maxVotes) {
	if (!showVotes()) {
		answer.votesPercent = 0;
		answer.votesPercentString.clear();
		answer.votesPercentWidth = 0;
	} else if (answer.votesPercentString.isEmpty()
		|| answer.votesPercent != percent) {
		answer.votesPercent = percent;
		answer.votesPercentString = QString::number(percent) + '%';
		answer.votesPercentWidth = st::historyPollPercentFont->width(
			answer.votesPercentString);
	}
	answer.votes = original.votes;
	answer.filling = answer.votes / float64(maxVotes);
}

void Poll::updateAnswerVotes() {
	if (_poll->answers.size() != _answers.size()
		|| _poll->answers.empty()) {
		return;
	}
	const auto totalVotes = std::max(1, _poll->totalVoters);
	const auto maxVotes = std::max(1, ranges::max_element(
		_poll->answers,
		ranges::less(),
		&PollAnswer::votes)->votes);

	constexpr auto kMaxCount = PollData::kMaxOptions;
	const auto count = size_type(_poll->answers.size());
	Assert(count <= kMaxCount);
	int PercentsStorage[kMaxCount] = { 0 };
	int VotesStorage[kMaxCount] = { 0 };

	ranges::copy(
		ranges::views::all(
			_poll->answers
		) | ranges::views::transform(&PollAnswer::votes),
		ranges::begin(VotesStorage));

	CountNicePercent(
		gsl::make_span(VotesStorage).subspan(0, count),
		totalVotes,
		gsl::make_span(PercentsStorage).subspan(0, count));

	auto &&answers = ranges::views::zip(
		_answers,
		_poll->answers,
		PercentsStorage);
	for (auto &&[answer, original, percent] : answers) {
		updateAnswerVotesFromOriginal(
			answer,
			original,
			percent,
			maxVotes);
	}
}

void Poll::draw(Painter &p, const PaintContext &context) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	auto paintw = width();

	checkSendingAnimation();
	if (_poll->checkResultsReload(context.now)) {
		history()->session().api().polls().reloadResults(_parent->data());
	}

	const auto stm = context.messageStyle();
	const auto padding = st::msgPadding;
	auto tshift = st::historyPollQuestionTop;
	if (!isBubbleTop()) {
		tshift -= st::msgFileTopMinus;
	}
	paintw -= padding.left() + padding.right();

	p.setPen(stm->historyTextFg);
	_question.drawLeft(p, padding.left(), tshift, paintw, width(), style::al_left, 0, -1, context.selection);
	tshift += _question.countHeight(paintw) + st::historyPollSubtitleSkip;

	p.setPen(stm->msgDateFg);
	_subtitle.drawLeftElided(p, padding.left(), tshift, paintw, width());
	paintRecentVoters(p, padding.left() + _subtitle.maxWidth(), tshift, context);
	paintCloseByTimer(p, padding.left() + paintw, tshift, context);
	paintShowSolution(p, padding.left() + paintw, tshift, context);
	tshift += st::msgDateFont->height + st::historyPollAnswersSkip;

	const auto progress = _answersAnimation
		? _answersAnimation->progress.value(1.)
		: 1.;
	if (progress == 1.) {
		resetAnswersAnimation();
	}

	auto &&answers = ranges::views::zip(
		_answers,
		ranges::views::ints(0, int(_answers.size())));
	for (const auto &[answer, index] : answers) {
		const auto animation = _answersAnimation
			? &_answersAnimation->data[index]
			: nullptr;
		if (animation) {
			animation->percent.update(progress, anim::linear);
			animation->filling.update(
				progress,
				showVotes() ? anim::easeOutCirc : anim::linear);
			animation->opacity.update(progress, anim::linear);
		}
		const auto height = paintAnswer(
			p,
			answer,
			animation,
			padding.left(),
			tshift,
			paintw,
			width(),
			context);
		tshift += height;
	}
	if (!inlineFooter()) {
		paintBottom(p, padding.left(), tshift, paintw, context);
	} else if (!_totalVotesLabel.isEmpty()) {
		tshift += st::msgPadding.bottom();
		paintInlineFooter(p, padding.left(), tshift, paintw, context);
	}
}

void Poll::paintInlineFooter(
		Painter &p,
		int left,
		int top,
		int paintw,
		const PaintContext &context) const {
	const auto stm = context.messageStyle();
	p.setPen(stm->msgDateFg);
	_totalVotesLabel.drawLeftElided(
		p,
		left,
		top,
		_parent->data()->reactions().empty()
			? std::min(
				_totalVotesLabel.maxWidth(),
				paintw - _parent->bottomInfoFirstLineWidth())
			: _totalVotesLabel.maxWidth(),
		width());
}

void Poll::paintBottom(
		Painter &p,
		int left,
		int top,
		int paintw,
		const PaintContext &context) const {
	const auto stringtop = top
		+ st::msgPadding.bottom()
		+ st::historyPollBottomButtonTop;
	const auto stm = context.messageStyle();
	if (showVotersCount()) {
		p.setPen(stm->msgDateFg);
		_totalVotesLabel.draw(p, left, stringtop, paintw, style::al_top);
	} else {
		const auto link = showVotes()
			? _showResultsLink
			: canSendVotes()
			? _sendVotesLink
			: nullptr;
		if (_linkRipple) {
			const auto linkHeight = bottomButtonHeight();
			p.setOpacity(st::historyPollRippleOpacity);
			_linkRipple->paint(
				p,
				left - st::msgPadding.left() - _linkRippleShift,
				height() - linkHeight,
				width(),
				&stm->msgWaveformInactive->c);
			if (_linkRipple->empty()) {
				_linkRipple.reset();
			}
			p.setOpacity(1.);
		}
		p.setFont(st::semiboldFont);
		p.setPen(link ? stm->msgFileThumbLinkFg : stm->msgDateFg);
		const auto string = showVotes()
			? tr::lng_polls_view_results(tr::now, Ui::Text::Upper)
			: tr::lng_polls_submit_votes(tr::now, Ui::Text::Upper);
		const auto stringw = st::semiboldFont->width(string);
		p.drawTextLeft(
			left + (paintw - stringw) / 2,
			stringtop,
			width(),
			string,
			stringw);
	}
}

void Poll::resetAnswersAnimation() const {
	_answersAnimation = nullptr;
	if (_poll->sendingVotes.size() != 1
		|| (_flags & PollData::Flag::MultiChoice)) {
		_sendingAnimation = nullptr;
	}
}

void Poll::radialAnimationCallback() const {
	if (!anim::Disabled()) {
		repaint();
	}
}

void Poll::paintRecentVoters(
		Painter &p,
		int left,
		int top,
		const PaintContext &context) const {
	const auto count = int(_recentVoters.size());
	if (!count) {
		return;
	}
	auto x = left
		+ st::historyPollRecentVotersSkip
		+ (count - 1) * st::historyPollRecentVoterSkip;
	auto y = top;
	const auto size = st::historyPollRecentVoterSize;
	const auto stm = context.messageStyle();
	auto pen = stm->msgBg->p;
	pen.setWidth(st::lineWidth);

	auto created = false;
	for (auto &recent : _recentVoters) {
		const auto was = !recent.userpic.null();
		recent.peer->paintUserpic(p, recent.userpic, x, y, size);
		if (!was && !recent.userpic.null()) {
			created = true;
		}
		const auto paintContent = [&](QPainter &p) {
			p.setPen(pen);
			p.setBrush(Qt::NoBrush);
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(x, y, size, size);
		};
		if (usesBubblePattern(context)) {
			const auto add = st::lineWidth * 2;
			const auto target = QRect(x, y, size, size).marginsAdded(
				{ add, add, add, add });
			Ui::PaintPatternBubblePart(
				p,
				context.viewport,
				context.bubblesPattern->pixmap,
				target,
				paintContent,
				_userpicCircleCache);
		} else {
			paintContent(p);
		}
		x -= st::historyPollRecentVoterSkip;
	}
	if (created) {
		history()->owner().registerHeavyViewPart(_parent);
	}
}

void Poll::paintCloseByTimer(
		Painter &p,
		int right,
		int top,
		const PaintContext &context) const {
	if (!canVote() || _poll->closeDate <= 0 || _poll->closePeriod <= 0) {
		_close = nullptr;
		return;
	}
	if (!_close) {
		_close = std::make_unique<CloseInformation>(
			_poll->closeDate,
			_poll->closePeriod,
			[=] { repaint(); });
	}
	const auto now = crl::now();
	const auto left = std::max(_close->finish - now, crl::time(0));
	const auto radial = std::min(_close->duration, kLargestRadialDuration);
	if (!left) {
		_close->radial.stop();
	} else if (left < radial && !anim::Disabled()) {
		if (!_close->radial.animating()) {
			_close->radial.init([=] {
				repaint();
			});
			_close->radial.start();
		}
	} else {
		_close->radial.stop();
	}
	const auto time = Ui::FormatDurationText(int(std::ceil(left / 1000.)));
	const auto st = context.st;
	const auto stm = context.messageStyle();
	const auto &icon = stm->historyQuizTimer;
	const auto x = right - icon.width();
	const auto y = top
		+ (st::normalFont->height - icon.height()) / 2
		- st::lineWidth;
	const auto &regular = (left < kCriticalCloseDuration)
		? st->boxTextFgError()
		: stm->msgDateFg;
	p.setPen(regular);
	const auto timeWidth = st::normalFont->width(time);
	p.drawTextLeft(x - timeWidth, top, width(), time, timeWidth);
	if (left < radial) {
		auto hq = PainterHighQualityEnabler(p);
		const auto part = std::max(
			left / float64(radial),
			1. / arc::kFullLength);
		const auto length = int(base::SafeRound(arc::kFullLength * part));
		auto pen = regular->p;
		pen.setWidth(st::historyPollRadio.thickness);
		pen.setCapStyle(Qt::RoundCap);
		p.setPen(pen);
		const auto size = icon.width() / 2;
		const auto left = (x + (icon.width() - size) / 2);
		const auto top = (y + (icon.height() - size) / 2) + st::lineWidth;
		p.drawArc(left, top, size, size, (arc::kFullLength / 4), length);
	} else {
		icon.paint(p, x, y, width());
	}

	if (left > (anim::Disabled() ? 0 : (radial - 1))) {
		const auto next = (left % 1000);
		_close->timer.callOnce((next ? next : 1000) + 1);
	}
}

void Poll::paintShowSolution(
		Painter &p,
		int right,
		int top,
		const PaintContext &context) const {
	const auto shown = _solutionButtonAnimation.value(
		_solutionButtonVisible ? 1. : 0.);
	if (!shown) {
		return;
	}
	if (!_showSolutionLink) {
		_showSolutionLink = std::make_shared<LambdaClickHandler>(
			crl::guard(this, [=] { showSolution(); }));
	}
	const auto stm = context.messageStyle();
	const auto &icon = stm->historyQuizExplain;
	const auto x = right - icon.width();
	const auto y = top + (st::normalFont->height - icon.height()) / 2;
	if (shown == 1.) {
		icon.paint(p, x, y, width());
	} else {
		p.save();
		p.translate(x + icon.width() / 2, y + icon.height() / 2);
		p.scale(shown, shown);
		p.setOpacity(shown);
		icon.paint(p, -icon.width() / 2, -icon.height() / 2, width());
		p.restore();
	}
}

int Poll::paintAnswer(
		Painter &p,
		const Answer &answer,
		const AnswerAnimation *animation,
		int left,
		int top,
		int width,
		int outerWidth,
		const PaintContext &context) const {
	const auto height = countAnswerHeight(answer, width);
	const auto stm = context.messageStyle();
	const auto aleft = left + st::historyPollAnswerPadding.left();
	const auto awidth = width
		- st::historyPollAnswerPadding.left()
		- st::historyPollAnswerPadding.right();

	if (answer.ripple) {
		p.setOpacity(st::historyPollRippleOpacity);
		answer.ripple->paint(
			p,
			left - st::msgPadding.left(),
			top,
			outerWidth,
			&stm->msgWaveformInactive->c);
		if (answer.ripple->empty()) {
			answer.ripple.reset();
		}
		p.setOpacity(1.);
	}

	if (animation) {
		const auto opacity = animation->opacity.current();
		if (opacity < 1.) {
			p.setOpacity(1. - opacity);
			paintRadio(p, answer, left, top, context);
		}
		if (opacity > 0.) {
			const auto percent = QString::number(
				int(base::SafeRound(animation->percent.current()))) + '%';
			const auto percentWidth = st::historyPollPercentFont->width(
				percent);
			p.setOpacity(opacity);
			paintPercent(
				p,
				percent,
				percentWidth,
				left,
				top,
				outerWidth,
				context);
			p.setOpacity(sqrt(opacity));
			paintFilling(
				p,
				animation->chosen,
				animation->correct,
				animation->filling.current(),
				left,
				top,
				width,
				height,
				context);
			p.setOpacity(1.);
		}
	} else if (!showVotes()) {
		paintRadio(p, answer, left, top, context);
	} else {
		paintPercent(
			p,
			answer.votesPercentString,
			answer.votesPercentWidth,
			left,
			top,
			outerWidth,
			context);
		paintFilling(
			p,
			answer.chosen,
			answer.correct,
			answer.filling,
			left,
			top,
			width,
			height,
			context);
	}

	top += st::historyPollAnswerPadding.top();
	p.setPen(stm->historyTextFg);
	answer.text.drawLeft(p, aleft, top, awidth, outerWidth);

	return height;
}

void Poll::paintRadio(
		Painter &p,
		const Answer &answer,
		int left,
		int top,
		const PaintContext &context) const {
	top += st::historyPollAnswerPadding.top();

	const auto stm = context.messageStyle();

	PainterHighQualityEnabler hq(p);
	const auto &radio = st::historyPollRadio;
	const auto over = ClickHandler::showAsActive(answer.handler);
	const auto &regular = stm->msgDateFg;

	const auto checkmark = answer.selectedAnimation.value(answer.selected ? 1. : 0.);

	const auto o = p.opacity();
	if (checkmark < 1.) {
		p.setBrush(Qt::NoBrush);
		p.setOpacity(o * (over ? st::historyPollRadioOpacityOver : st::historyPollRadioOpacity));
	}

	const auto rect = QRectF(left, top, radio.diameter, radio.diameter).marginsRemoved(QMarginsF(radio.thickness / 2., radio.thickness / 2., radio.thickness / 2., radio.thickness / 2.));
	if (_sendingAnimation && _sendingAnimation->option == answer.option) {
		const auto &active = stm->msgServiceFg;
		if (anim::Disabled()) {
			anim::DrawStaticLoading(p, rect, radio.thickness, active);
		} else {
			const auto state = _sendingAnimation->animation.computeState();
			auto pen = anim::pen(regular, active, state.shown);
			pen.setWidth(radio.thickness);
			pen.setCapStyle(Qt::RoundCap);
			p.setPen(pen);
			p.drawArc(
				rect,
				state.arcFrom,
				state.arcLength);
		}
	} else {
		if (checkmark < 1.) {
			auto pen = regular->p;
			pen.setWidth(radio.thickness);
			p.setPen(pen);
			p.drawEllipse(rect);
		}
		if (checkmark > 0.) {
			const auto removeFull = (radio.diameter / 2 - radio.thickness);
			const auto removeNow = removeFull * (1. - checkmark);
			const auto color = stm->msgFileThumbLinkFg;
			auto pen = color->p;
			pen.setWidth(radio.thickness);
			p.setPen(pen);
			p.setBrush(color);
			p.drawEllipse(rect.marginsRemoved({ removeNow, removeNow, removeNow, removeNow }));
			const auto &icon = stm->historyPollChosen;
			icon.paint(p, left + (radio.diameter - icon.width()) / 2, top + (radio.diameter - icon.height()) / 2, width());
		}
	}

	p.setOpacity(o);
}

void Poll::paintPercent(
		Painter &p,
		const QString &percent,
		int percentWidth,
		int left,
		int top,
		int outerWidth,
		const PaintContext &context) const {
	const auto stm = context.messageStyle();
	const auto aleft = left + st::historyPollAnswerPadding.left();

	top += st::historyPollAnswerPadding.top();

	p.setFont(st::historyPollPercentFont);
	p.setPen(stm->historyTextFg);
	const auto pleft = aleft - percentWidth - st::historyPollPercentSkip;
	p.drawTextLeft(pleft, top + st::historyPollPercentTop, outerWidth, percent, percentWidth);
}

void Poll::paintFilling(
		Painter &p,
		bool chosen,
		bool correct,
		float64 filling,
		int left,
		int top,
		int width,
		int height,
		const PaintContext &context) const {
	const auto bottom = top + height;
	const auto st = context.st;
	const auto stm = context.messageStyle();
	const auto aleft = left + st::historyPollAnswerPadding.left();
	const auto awidth = width
		- st::historyPollAnswerPadding.left()
		- st::historyPollAnswerPadding.right();

	top += st::historyPollAnswerPadding.top();

	const auto thickness = st::historyPollFillingHeight;
	const auto max = awidth - st::historyPollFillingRight;
	const auto size = anim::interpolate(st::historyPollFillingMin, max, filling);
	const auto radius = st::historyPollFillingRadius;
	const auto ftop = bottom - st::historyPollFillingBottom - thickness;

	enum class Style {
		Incorrect,
		Correct,
		Default,
	};
	const auto style = [&] {
		if (chosen && !correct) {
			return Style::Incorrect;
		} else if (chosen && correct && _poll->quiz() && !context.outbg) {
			return Style::Correct;
		} else {
			return Style::Default;
		}
	}();
	auto barleft = aleft;
	auto barwidth = size;
	const auto &color = (style == Style::Incorrect)
		? st->boxTextFgError()
		: (style == Style::Correct)
		? st->boxTextFgGood()
		: stm->msgFileBg;
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	PainterHighQualityEnabler hq(p);
	if (chosen || correct) {
		const auto &icon = (style == Style::Incorrect)
			? st->historyPollChoiceWrong()
			: (style == Style::Correct)
			? st->historyPollChoiceRight()
			: stm->historyPollChoiceRight;
		const auto cleft = aleft - st::historyPollPercentSkip - icon.width();
		const auto ctop = ftop - (icon.height() - thickness) / 2;
		p.drawEllipse(cleft, ctop, icon.width(), icon.height());

		const auto paintContent = [&](QPainter &p) {
			icon.paint(p, cleft, ctop, width);
		};
		if (style == Style::Default && usesBubblePattern(context)) {
			const auto add = st::lineWidth * 2;
			const auto target = QRect(
				cleft,
				ctop,
				icon.width(),
				icon.height()
			).marginsAdded({ add, add, add, add });
			Ui::PaintPatternBubblePart(
				p,
				context.viewport,
				context.bubblesPattern->pixmap,
				target,
				paintContent,
				_fillingIconCache);
		} else {
			paintContent(p);
		}
		//barleft += icon.width() - radius;
		//barwidth -= icon.width() - radius;
	}
	if (barwidth > 0) {
		p.drawRoundedRect(barleft, ftop, barwidth, thickness, radius, radius);
	}
}

bool Poll::answerVotesChanged() const {
	if (_poll->answers.size() != _answers.size()
		|| _poll->answers.empty()) {
		return false;
	}
	return !ranges::equal(
		_answers,
		_poll->answers,
		ranges::equal_to(),
		&Answer::votes,
		&PollAnswer::votes);
}

void Poll::saveStateInAnimation() const {
	if (_answersAnimation) {
		return;
	}
	const auto show = showVotes();
	_answersAnimation = std::make_unique<AnswersAnimation>();
	_answersAnimation->data.reserve(_answers.size());
	const auto convert = [&](const Answer &answer) {
		auto result = AnswerAnimation();
		result.percent = show ? float64(answer.votesPercent) : 0.;
		result.filling = show ? answer.filling : 0.;
		result.opacity = show ? 1. : 0.;
		result.chosen = answer.chosen;
		result.correct = answer.correct;
		return result;
	};
	ranges::transform(
		_answers,
		ranges::back_inserter(_answersAnimation->data),
		convert);
}

bool Poll::checkAnimationStart() const {
	if (_poll->answers.size() != _answers.size()) {
		// Skip initial changes.
		return false;
	}
	const auto result = (showVotes() != (_poll->voted() || _poll->closed()))
		|| answerVotesChanged();
	if (result) {
		saveStateInAnimation();
	}
	return result;
}

void Poll::startAnswersAnimation() const {
	if (!_answersAnimation) {
		return;
	}

	const auto show = showVotes();
	auto &&both = ranges::views::zip(_answers, _answersAnimation->data);
	for (auto &&[answer, data] : both) {
		data.percent.start(show ? float64(answer.votesPercent) : 0.);
		data.filling.start(show ? answer.filling : 0.);
		data.opacity.start(show ? 1. : 0.);
		data.chosen = data.chosen || answer.chosen;
		data.correct = data.correct || answer.correct;
	}
	_answersAnimation->progress.start(
		[=] { repaint(); },
		0.,
		1.,
		st::historyPollDuration);
}

TextSelection Poll::adjustSelection(
		TextSelection selection,
		TextSelectType type) const {
	return _question.adjustSelection(selection, type);
}

uint16 Poll::fullSelectionLength() const {
	return _question.length();
}

TextForMimeData Poll::selectedText(TextSelection selection) const {
	return _question.toTextForMimeData(selection);
}

TextState Poll::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);
	if (!_poll->sendingVotes.empty()) {
		return result;
	}

	const auto can = canVote();
	const auto show = showVotes();
	const auto padding = st::msgPadding;
	auto paintw = width();
	auto tshift = st::historyPollQuestionTop;
	if (!isBubbleTop()) {
		tshift -= st::msgFileTopMinus;
	}
	paintw -= padding.left() + padding.right();

	const auto questionH = _question.countHeight(paintw);
	if (QRect(padding.left(), tshift, paintw, questionH).contains(point)) {
		result = TextState(_parent, _question.getState(
			point - QPoint(padding.left(), tshift),
			paintw,
			request.forText()));
		return result;
	}
	tshift += questionH + st::historyPollSubtitleSkip;
	if (inShowSolution(point, padding.left() + paintw, tshift)) {
		result.link = _showSolutionLink;
		return result;
	}
	tshift += st::msgDateFont->height + st::historyPollAnswersSkip;
	for (const auto &answer : _answers) {
		const auto height = countAnswerHeight(answer, paintw);
		if (point.y() >= tshift && point.y() < tshift + height) {
			if (can) {
				_lastLinkPoint = point;
				result.link = answer.handler;
			} else if (show) {
				result.customTooltip = true;
				using Flag = Ui::Text::StateRequest::Flag;
				if (request.flags & Flag::LookupCustomTooltip) {
					const auto quiz = _poll->quiz();
					result.customTooltipText = answer.votes
						? (quiz
							? tr::lng_polls_answers_count
							: tr::lng_polls_votes_count)(
								tr::now,
								lt_count_decimal,
								answer.votes)
						: (quiz
							? tr::lng_polls_answers_none
							: tr::lng_polls_votes_none)(tr::now);
				}
			}
			return result;
		}
		tshift += height;
	}
	if (!showVotersCount()) {
		const auto link = showVotes()
			? _showResultsLink
			: canSendVotes()
			? _sendVotesLink
			: nullptr;
		if (link) {
			const auto linkHeight = bottomButtonHeight();
			const auto linkTop = height() - linkHeight;
			if (QRect(0, linkTop, width(), linkHeight).contains(point)) {
				_lastLinkPoint = point;
				result.link = link;
				return result;
			}
		}
	}
	return result;
}

auto Poll::bubbleRoll() const -> BubbleRoll {
	const auto value = _wrongAnswerAnimation.value(1.);
	_wrongAnswerAnimated = (value < 1.);
	if (!_wrongAnswerAnimated) {
		return BubbleRoll();
	}
	const auto progress = [](float64 full) {
		const auto lower = std::floor(full);
		const auto shift = (full - lower);
		switch (int(lower) % 4) {
		case 0: return -shift;
		case 1: return (shift - 1.);
		case 2: return shift;
		case 3: return (1. - shift);
		}
		Unexpected("Value in Poll::getBubbleRollDegrees.");
	};
	return {
		.rotate = progress(value * kRotateSegments) * kRotateAmplitude,
		.scale = 1. + progress(value * kScaleSegments) * kScaleAmplitude
	};
}

QMargins Poll::bubbleRollRepaintMargins() const {
	if (!_wrongAnswerAnimated) {
		return QMargins();
	}
	static const auto kAdd = int(std::ceil(
		st::msgMaxWidth * std::sin(kRotateAmplitude * M_PI / 180.)));
	return QMargins(kAdd, kAdd, kAdd, kAdd);
}

void Poll::paintBubbleFireworks(
		Painter &p,
		const QRect &bubble,
		crl::time ms) const {
	if (!_fireworksAnimation || _fireworksAnimation->paint(p, bubble)) {
		return;
	}
	_fireworksAnimation = nullptr;
}

void Poll::clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) {
	if (!handler) return;

	const auto i = ranges::find(
		_answers,
		handler,
		&Answer::handler);
	if (i != end(_answers)) {
		toggleRipple(*i, pressed);
	} else if (handler == _sendVotesLink || handler == _showResultsLink) {
		toggleLinkRipple(pressed);
	}
}

void Poll::unloadHeavyPart() {
	for (auto &recent : _recentVoters) {
		recent.userpic = {};
	}
}

bool Poll::hasHeavyPart() const {
	for (auto &recent : _recentVoters) {
		if (!recent.userpic.null()) {
			return true;
		}
	}
	return false;
}

void Poll::toggleRipple(Answer &answer, bool pressed) {
	if (pressed) {
		const auto outerWidth = width();
		const auto innerWidth = outerWidth
			- st::msgPadding.left()
			- st::msgPadding.right();
		if (!answer.ripple) {
			auto mask = Ui::RippleAnimation::RectMask(QSize(
				outerWidth,
				countAnswerHeight(answer, innerWidth)));
			answer.ripple = std::make_unique<Ui::RippleAnimation>(
				st::defaultRippleAnimation,
				std::move(mask),
				[=] { repaint(); });
		}
		const auto top = countAnswerTop(answer, innerWidth);
		answer.ripple->add(_lastLinkPoint - QPoint(0, top));
	} else if (answer.ripple) {
		answer.ripple->lastStop();
	}
}

bool Poll::canShowSolution() const {
	return showVotes() && !_poll->solution.text.isEmpty();
}

bool Poll::inShowSolution(
		QPoint point,
		int right,
		int top) const {
	if (!canShowSolution() || !_solutionButtonVisible) {
		return false;
	}
	const auto &icon = st::historyQuizExplainIn;
	const auto x = right - icon.width();
	const auto y = top + (st::normalFont->height - icon.height()) / 2;
	return QRect(x, y, icon.width(), icon.height()).contains(point);
}

int Poll::bottomButtonHeight() const {
	const auto skip = st::historyPollChoiceRight.height()
		- st::historyPollFillingBottom
		- st::historyPollFillingHeight
		- (st::historyPollChoiceRight.height() - st::historyPollFillingHeight) / 2;
	return st::historyPollTotalVotesSkip
		- skip
		+ st::historyPollBottomButtonSkip
		+ st::msgDateFont->height
		+ st::msgPadding.bottom();
}

void Poll::toggleLinkRipple(bool pressed) {
	if (pressed) {
		const auto linkWidth = width();
		const auto linkHeight = bottomButtonHeight();
		if (!_linkRipple) {
			auto mask = isRoundedInBubbleBottom()
				? static_cast<Message*>(_parent.get())->bottomRippleMask(
					bottomButtonHeight())
				: BottomRippleMask{
					Ui::RippleAnimation::RectMask({ linkWidth, linkHeight }),
				};
			_linkRipple = std::make_unique<Ui::RippleAnimation>(
				st::defaultRippleAnimation,
				std::move(mask.image),
				[=] { repaint(); });
			_linkRippleShift = mask.shift;
		}
		_linkRipple->add(_lastLinkPoint
			+ QPoint(_linkRippleShift, linkHeight - height()));
	} else if (_linkRipple) {
		_linkRipple->lastStop();
	}
}

Poll::~Poll() {
	history()->owner().unregisterPollView(_poll, _parent);
	if (hasHeavyPart()) {
		unloadHeavyPart();
		_parent->checkHeavyPart();
	}
}

} // namespace HistoryView
