/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_poll.h"

#include "lang/lang_keys.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "calls/calls_instance.h"
#include "ui/text_options.h"
#include "ui/effects/animations.h"
#include "ui/effects/radial_animation.h"
#include "ui/effects/ripple_animation.h"
#include "data/data_media_types.h"
#include "data/data_poll.h"
#include "data/data_session.h"
#include "layout.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "styles/style_history.h"
#include "styles/style_widgets.h"

namespace HistoryView {
namespace {

struct PercentCounterItem {
	int index = 0;
	int percent = 0;
	int remainder = 0;

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
	auto &&zipped = ranges::view::zip(
		votes,
		items,
		ranges::view::ints(0));
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

	void fillText(const PollAnswer &original);

	Ui::Text::String text;
	QByteArray option;
	int votes = 0;
	int votesPercent = 0;
	int votesPercentWidth = 0;
	float64 filling = 0.;
	QString votesPercentString;
	bool chosen = false;
	ClickHandlerPtr handler;
	mutable std::unique_ptr<Ui::RippleAnimation> ripple;
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

void Poll::Answer::fillText(const PollAnswer &original) {
	if (!text.isEmpty() && text.toString() == original.text) {
		return;
	}
	text.setText(
		st::historyPollAnswerStyle,
		original.text,
		Ui::WebpageTextTitleOptions());
}

Poll::Poll(
	not_null<Element*> parent,
	not_null<PollData*> poll)
: Media(parent)
, _poll(poll)
, _question(st::msgMinWidth / 2) {
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

	const auto answersHeight = ranges::accumulate(ranges::view::all(
		_answers
	) | ranges::view::transform([](const Answer &answer) {
		return st::historyPollAnswerPadding.top()
			+ answer.text.minHeight()
			+ st::historyPollAnswerPadding.bottom();
	}), 0);

	auto minHeight = st::historyPollQuestionTop
		+ _question.minHeight()
		+ st::historyPollSubtitleSkip
		+ st::msgDateFont->height
		+ st::historyPollAnswersSkip
		+ answersHeight
		+ st::msgPadding.bottom()
		+ st::msgDateFont->height
		+ st::msgPadding.bottom();
	if (!isBubbleTop()) {
		minHeight -= st::msgFileTopMinus;
	}
	return { maxWidth, minHeight };
}

bool Poll::showVotes() const {
	return _voted || _closed;
}

bool Poll::canVote() const {
	return !showVotes() && IsServerMsgId(_parent->data()->id);
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
	auto &&answers = ranges::view::zip(
		_answers,
		ranges::view::ints(0, int(_answers.size())));
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
	const auto paddings = st::msgPadding.left() + st::msgPadding.right();

	accumulate_min(newWidth, maxWidth());
	const auto innerWidth = newWidth
		- st::msgPadding.left()
		- st::msgPadding.right();

	const auto answersHeight = ranges::accumulate(ranges::view::all(
		_answers
	) | ranges::view::transform([&](const Answer &answer) {
		return countAnswerHeight(answer, innerWidth);
	}), 0);

	auto newHeight = st::historyPollQuestionTop
		+ _question.countHeight(innerWidth)
		+ st::historyPollSubtitleSkip
		+ st::msgDateFont->height
		+ st::historyPollAnswersSkip
		+ answersHeight
		+ st::historyPollTotalVotesSkip
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
	_pollVersion = _poll->version;

	const auto willStartAnimation = checkAnimationStart();

	if (_question.toString() != _poll->question) {
		_question.setText(
			st::historyPollQuestionStyle,
			_poll->question,
			Ui::WebpageTextTitleOptions());
	}
	if (_closed != _poll->closed || _subtitle.isEmpty()) {
		_closed = _poll->closed;
		_subtitle.setText(
			st::msgDateTextStyle,
			_closed ? tr::lng_polls_closed(tr::now) : tr::lng_polls_anonymous(tr::now));
	}

	updateAnswers();
	updateVotes();

	if (willStartAnimation) {
		startAnswersAnimation();
	}
}

void Poll::updateAnswers() {
	const auto changed = !ranges::equal(
		_answers,
		_poll->answers,
		ranges::equal_to(),
		&Answer::option,
		&PollAnswer::option);
	if (!changed) {
		auto &&answers = ranges::view::zip(_answers, _poll->answers);
		for (auto &&[answer, original] : answers) {
			answer.fillText(original);
		}
		return;
	}
	_answers = ranges::view::all(
		_poll->answers
	) | ranges::view::transform([](const PollAnswer &answer) {
		auto result = Answer();
		result.option = answer.option;
		result.fillText(answer);
		return result;
	}) | ranges::to_vector;

	for (auto &answer : _answers) {
		answer.handler = createAnswerClickHandler(answer);
	}

	resetAnswersAnimation();
}

ClickHandlerPtr Poll::createAnswerClickHandler(
		const Answer &answer) const {
	const auto option = answer.option;
	const auto itemId = _parent->data()->fullId();
	return std::make_shared<LambdaClickHandler>([=] {
		history()->session().api().sendPollVotes(itemId, { option });
	});
}

void Poll::updateVotes() {
	_voted = _poll->voted();
	updateAnswerVotes();
	updateTotalVotes();
}

void Poll::checkSendingAnimation() const {
	const auto &sending = _poll->sendingVote;
	if (sending.isEmpty() == !_sendingAnimation) {
		if (_sendingAnimation) {
			_sendingAnimation->option = sending;
		}
		return;
	}
	if (sending.isEmpty()) {
		if (!_answersAnimation) {
			_sendingAnimation = nullptr;
		}
		return;
	}
	_sendingAnimation = std::make_unique<SendingAnimation>(
		sending,
		[=] { radialAnimationCallback(); });
	_sendingAnimation->animation.start();
}

void Poll::updateTotalVotes() {
	if (_totalVotes == _poll->totalVoters && !_totalVotesLabel.isEmpty()) {
		return;
	}
	_totalVotes = _poll->totalVoters;
	const auto string = !_totalVotes
		? tr::lng_polls_votes_none(tr::now)
		: tr::lng_polls_votes_count(tr::now, lt_count_short, _totalVotes);
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
		ranges::view::all(
			_poll->answers
		) | ranges::view::transform(&PollAnswer::votes),
		ranges::begin(VotesStorage));

	CountNicePercent(
		gsl::make_span(VotesStorage).subspan(0, count),
		totalVotes,
		gsl::make_span(PercentsStorage).subspan(0, count));

	auto &&answers = ranges::view::zip(
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

void Poll::draw(Painter &p, const QRect &r, TextSelection selection, crl::time ms) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	auto paintx = 0, painty = 0, paintw = width(), painth = height();

	checkSendingAnimation();
	_poll->checkResultsReload(_parent->data(), ms);

	const auto outbg = _parent->hasOutLayout();
	const auto selected = (selection == FullSelection);
	const auto &regular = selected ? (outbg ? st::msgOutDateFgSelected : st::msgInDateFgSelected) : (outbg ? st::msgOutDateFg : st::msgInDateFg);

	const auto padding = st::msgPadding;
	auto tshift = st::historyPollQuestionTop;
	if (!isBubbleTop()) {
		tshift -= st::msgFileTopMinus;
	}
	paintw -= padding.left() + padding.right();

	p.setPen(outbg ? st::webPageTitleOutFg : st::webPageTitleInFg);
	_question.drawLeft(p, padding.left(), tshift, paintw, width(), style::al_left, 0, -1, selection);
	tshift += _question.countHeight(paintw) + st::historyPollSubtitleSkip;

	p.setPen(regular);
	_subtitle.drawLeftElided(p, padding.left(), tshift, paintw, width());
	tshift += st::msgDateFont->height + st::historyPollAnswersSkip;

	const auto progress = _answersAnimation
		? _answersAnimation->progress.value(1.)
		: 1.;
	if (progress == 1.) {
		resetAnswersAnimation();
	}

	auto &&answers = ranges::view::zip(
		_answers,
		ranges::view::ints(0, int(_answers.size())));
	for (const auto &[answer, index] : answers) {
		const auto animation = _answersAnimation
			? &_answersAnimation->data[index]
			: nullptr;
		if (animation) {
			animation->percent.update(progress, anim::linear);
			animation->filling.update(progress, anim::linear);
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
			selection);
		tshift += height;
	}
	if (!_totalVotesLabel.isEmpty()) {
		tshift += st::msgPadding.bottom();
		p.setPen(regular);
		_totalVotesLabel.drawLeftElided(
			p,
			padding.left(),
			tshift,
			std::min(
				_totalVotesLabel.maxWidth(),
				paintw - _parent->infoWidth()),
			width());
	}
}

void Poll::resetAnswersAnimation() const {
	_answersAnimation = nullptr;
	if (_poll->sendingVote.isEmpty()) {
		_sendingAnimation = nullptr;
	}
}

void Poll::radialAnimationCallback() const {
	if (!anim::Disabled()) {
		history()->owner().requestViewRepaint(_parent);
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
		TextSelection selection) const {
	const auto height = countAnswerHeight(answer, width);
	const auto outbg = _parent->hasOutLayout();
	const auto aleft = left + st::historyPollAnswerPadding.left();
	const auto awidth = width
		- st::historyPollAnswerPadding.left()
		- st::historyPollAnswerPadding.right();

	if (answer.ripple) {
		p.setOpacity(st::historyPollRippleOpacity);
		answer.ripple->paint(p, left - st::msgPadding.left(), top, outerWidth);
		if (answer.ripple->empty()) {
			answer.ripple.reset();
		}
		p.setOpacity(1.);
	}

	if (animation) {
		const auto opacity = animation->opacity.current();
		if (opacity < 1.) {
			p.setOpacity(1. - opacity);
			paintRadio(p, answer, left, top, selection);
		}
		if (opacity > 0.) {
			const auto percent = QString::number(
				int(std::round(animation->percent.current()))) + '%';
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
				selection);
			p.setOpacity(sqrt(opacity));
			paintFilling(
				p,
				animation->filling.current(),
				left,
				top,
				width,
				height,
				selection);
			p.setOpacity(1.);
		}
	} else if (!showVotes()) {
		paintRadio(p, answer, left, top, selection);
	} else {
		paintPercent(
			p,
			answer.votesPercentString,
			answer.votesPercentWidth,
			left,
			top,
			outerWidth,
			selection);
		paintFilling(
			p,
			answer.filling,
			left,
			top,
			width,
			height,
			selection);
	}

	top += st::historyPollAnswerPadding.top();
	p.setPen(outbg ? st::webPageDescriptionOutFg : st::webPageDescriptionInFg);
	answer.text.drawLeft(p, aleft, top, awidth, outerWidth);

	return height;
}

void Poll::paintRadio(
		Painter &p,
		const Answer &answer,
		int left,
		int top,
		TextSelection selection) const {
	top += st::historyPollAnswerPadding.top();

	const auto outbg = _parent->hasOutLayout();
	const auto selected = (selection == FullSelection);

	PainterHighQualityEnabler hq(p);
	const auto &st = st::historyPollRadio;
	const auto over = ClickHandler::showAsActive(answer.handler);
	const auto &regular = selected ? (outbg ? st::msgOutDateFgSelected : st::msgInDateFgSelected) : (outbg ? st::msgOutDateFg : st::msgInDateFg);

	p.setBrush(Qt::NoBrush);
	const auto o = p.opacity();
	p.setOpacity(o * (over ? st::historyPollRadioOpacityOver : st::historyPollRadioOpacity));

	const auto rect = QRectF(left, top, st.diameter, st.diameter).marginsRemoved(QMarginsF(st.thickness / 2., st.thickness / 2., st.thickness / 2., st.thickness / 2.));
	if (_sendingAnimation && _sendingAnimation->option == answer.option) {
		const auto &active = selected ? (outbg ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (outbg ? st::msgOutServiceFg : st::msgInServiceFg);
		if (anim::Disabled()) {
			anim::DrawStaticLoading(p, rect, st.thickness, active);
		} else {
			const auto state = _sendingAnimation->animation.computeState();
			auto pen = anim::pen(regular, active, state.shown);
			pen.setWidth(st.thickness);
			pen.setCapStyle(Qt::RoundCap);
			p.setPen(pen);
			p.drawArc(
				rect,
				state.arcFrom,
				state.arcLength);
		}
	} else {
		auto pen = regular->p;
		pen.setWidth(st.thickness);
		p.setPen(pen);
		p.drawEllipse(rect);
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
		TextSelection selection) const {
	const auto outbg = _parent->hasOutLayout();
	const auto selected = (selection == FullSelection);
	const auto aleft = left + st::historyPollAnswerPadding.left();

	top += st::historyPollAnswerPadding.top();

	p.setFont(st::historyPollPercentFont);
	p.setPen(outbg ? st::webPageDescriptionOutFg : st::webPageDescriptionInFg);
	const auto pleft = aleft - percentWidth - st::historyPollPercentSkip;
	p.drawTextLeft(pleft, top + st::historyPollPercentTop, outerWidth, percent, percentWidth);
}

void Poll::paintFilling(
		Painter &p,
		float64 filling,
		int left,
		int top,
		int width,
		int height,
		TextSelection selection) const {
	const auto bottom = top + height;
	const auto outbg = _parent->hasOutLayout();
	const auto selected = (selection == FullSelection);
	const auto aleft = left + st::historyPollAnswerPadding.left();
	const auto awidth = width
		- st::historyPollAnswerPadding.left()
		- st::historyPollAnswerPadding.right();

	top += st::historyPollAnswerPadding.top();

	const auto bar = outbg ? (selected ? st::msgWaveformOutActiveSelected : st::msgWaveformOutActive) : (selected ? st::msgWaveformInActiveSelected : st::msgWaveformInActive);
	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);
	p.setBrush(bar);
	const auto max = awidth - st::historyPollFillingRight;
	const auto size = anim::interpolate(st::historyPollFillingMin, max, filling);
	const auto radius = st::historyPollFillingRadius;
	const auto ftop = bottom - st::historyPollFillingBottom - st::historyPollFillingHeight;
	p.drawRoundedRect(aleft, ftop, size, st::historyPollFillingHeight, radius, radius);
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
	const auto result = (showVotes() != (_poll->voted() || _poll->closed))
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
	auto &&both = ranges::view::zip(_answers, _answersAnimation->data);
	for (auto &&[answer, data] : both) {
		data.percent.start(show ? float64(answer.votesPercent) : 0.);
		data.filling.start(show ? answer.filling : 0.);
		data.opacity.start(show ? 1. : 0.);
	}
	_answersAnimation->progress.start(
		[=] { history()->owner().requestViewRepaint(_parent); },
		0.,
		1.,
		st::historyPollDuration);
}

TextState Poll::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);
	if (!_poll->sendingVote.isEmpty()) {
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

	tshift += _question.countHeight(paintw) + st::historyPollSubtitleSkip;
	tshift += st::msgDateFont->height + st::historyPollAnswersSkip;
	const auto awidth = paintw
		- st::historyPollAnswerPadding.left()
		- st::historyPollAnswerPadding.right();
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
					result.customTooltipText = answer.votes
						? tr::lng_polls_votes_count(tr::now, lt_count_decimal, answer.votes)
						: tr::lng_polls_votes_none(tr::now);
				}
			}
			return result;
		}
		tshift += height;
	}
	return result;
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
	}
}

void Poll::toggleRipple(Answer &answer, bool pressed) {
	if (pressed) {
		const auto outerWidth = width();
		const auto innerWidth = outerWidth
			- st::msgPadding.left()
			- st::msgPadding.right();
		if (!answer.ripple) {
			auto mask = Ui::RippleAnimation::rectMask(QSize(
				outerWidth,
				countAnswerHeight(answer, innerWidth)));
			answer.ripple = std::make_unique<Ui::RippleAnimation>(
				(_parent->hasOutLayout()
					? st::historyPollRippleOut
					: st::historyPollRippleIn),
				std::move(mask),
				[=] { history()->owner().requestViewRepaint(_parent); });
		}
		const auto top = countAnswerTop(answer, innerWidth);
		answer.ripple->add(_lastLinkPoint - QPoint(0, top));
	} else {
		if (answer.ripple) {
			answer.ripple->lastStop();
		}
	}
}

Poll::~Poll() {
	history()->owner().unregisterPollView(_poll, _parent);
}

} // namespace HistoryView
