/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/media/history_media_poll.h"

#include "lang/lang_keys.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "calls/calls_instance.h"
#include "ui/text_options.h"
#include "data/data_media_types.h"
#include "data/data_poll.h"
#include "data/data_session.h"
#include "layout.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "styles/style_history.h"
#include "styles/style_widgets.h"

namespace {

using TextState = HistoryView::TextState;

struct FormattedLargeNumber {
	int rounded = 0;
	bool shortened = false;
	QString text;
};

FormattedLargeNumber FormatLargeNumber(int64 number) {
	auto result = FormattedLargeNumber();
	const auto abs = std::abs(number);
	const auto shorten = [&](int64 divider, char multiplier) {
		const auto sign = (number > 0) ? 1 : -1;
		const auto rounded = abs / (divider / 10);
		result.rounded = sign * rounded * (divider / 10);
		result.text = QString::number(sign * rounded / 10);
		if (rounded % 10) {
			result.text += '.' + QString::number(rounded % 10) + multiplier;
		} else {
			result.text += multiplier;
		}
		result.shortened = true;
	};
	if (abs >= 1'000'000) {
		shorten(1'000'000, 'M');
	} else if (abs >= 10'000) {
		shorten(1'000, 'K');
	} else {
		result.rounded = number;
		result.text = QString::number(number);
	}
	return result;
}

} // namespace

HistoryPoll::Answer::Answer() : text(st::msgMinWidth / 2) {
}

HistoryPoll::HistoryPoll(
	not_null<Element*> parent,
	not_null<PollData*> poll)
: HistoryMedia(parent)
, _poll(poll)
, _question(st::msgMinWidth / 2) {
	Auth().data().registerPollView(_poll, _parent);
}

QSize HistoryPoll::countOptimalSize() {
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
			+ 2 * st::defaultCheckbox.textPosition.y()
			+ answer.text.minHeight()
			+ st::historyPollAnswerPadding.bottom()
			+ st::lineWidth;
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

int HistoryPoll::countAnswerHeight(
		const Answer &answer,
		int innerWidth) const {
	const auto answerWidth = innerWidth
		- st::historyPollAnswerPadding.left()
		- st::historyPollAnswerPadding.right();
	return st::historyPollAnswerPadding.top()
		+ 2 * st::defaultCheckbox.textPosition.y()
		+ answer.text.countHeight(answerWidth)
		+ st::historyPollAnswerPadding.bottom()
		+ st::lineWidth;
}

QSize HistoryPoll::countCurrentSize(int newWidth) {
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
		+ st::msgPadding.bottom()
		+ st::msgDateFont->height
		+ st::msgPadding.bottom();
	if (!isBubbleTop()) {
		newHeight -= st::msgFileTopMinus;
	}
	return { newWidth, newHeight };
}

void HistoryPoll::updateTexts() {
	if (_pollVersion == _poll->version) {
		return;
	}
	_pollVersion = _poll->version;
	_question.setText(
		st::historyPollQuestionStyle,
		_poll->question,
		Ui::WebpageTextTitleOptions());
	_subtitle.setText(
		st::msgDateTextStyle,
		lang(lng_polls_anonymous));

	_answers = ranges::view::all(
		_poll->answers
	) | ranges::view::transform([](const PollAnswer &answer) {
		auto result = Answer();
		result.option = answer.option;
		result.text.setText(
			st::historyPollAnswerStyle,
			answer.text,
			Ui::WebpageTextTitleOptions());
		return result;
	}) | ranges::to_vector;

	for (auto &answer : _answers) {
		answer.handler = createAnswerClickHandler(answer);
	}

	_closed = _poll->closed;
}

ClickHandlerPtr HistoryPoll::createAnswerClickHandler(
		const Answer &answer) const {
	const auto option = answer.option;
	const auto itemId = _parent->data()->fullId();
	return std::make_shared<LambdaClickHandler>([=] {
		Auth().api().sendPollVotes(itemId, { option });
	});
}

void HistoryPoll::updateVotes() const {
	updateTotalVotes();
	_voted = _poll->voted();
	updateAnswerVotes();
}

void HistoryPoll::updateTotalVotes() const {
	if (_totalVotes == _poll->totalVoters) {
		return;
	}
	_totalVotes = _poll->totalVoters;
	if (!_totalVotes) {
		_totalVotesLabel.clear();
		return;
	}
	const auto formatted = FormatLargeNumber(_totalVotes);
	auto string = lng_polls_votes_count(lt_count, formatted.rounded);
	if (formatted.shortened) {
		string.replace(QString::number(formatted.rounded), formatted.text);
	}
	_totalVotesLabel.setText(st::msgDateTextStyle, string);
}

void HistoryPoll::updateAnswerVotesFromOriginal(
		const Answer &answer,
		const PollAnswer &original,
		int totalVotes,
		int maxVotes) const {
	if (!_voted && !_closed) {
		answer.votesPercent.clear();
	} else if (answer.votes != original.votes
		|| answer.votesPercent.isEmpty()) {
		const auto percent = original.votes * 100 / totalVotes;
		answer.votesPercent = QString::number(percent) + '%';
		answer.votesPercentWidth = st::historyPollPercentFont->width(
			answer.votesPercent);
	}
	answer.votes = original.votes;
	answer.filling = answer.votes / float64(maxVotes);
}

void HistoryPoll::updateAnswerVotes() const {
	if (_poll->answers.size() != _answers.size()
		|| _poll->answers.empty()) {
		return;
	}
	const auto totalVotes = std::max(1, _totalVotes);
	const auto maxVotes = std::max(1, ranges::max_element(
		_poll->answers,
		ranges::less(),
		&PollAnswer::votes)->votes);
	auto &&answers = ranges::view::zip(_answers, _poll->answers);
	for (auto &&[answer, original] : answers) {
		updateAnswerVotesFromOriginal(
			answer,
			original,
			totalVotes,
			maxVotes);
	}
}

void HistoryPoll::draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	auto paintx = 0, painty = 0, paintw = width(), painth = height();

	updateVotes();

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

	for (const auto &answer : _answers) {
		const auto height = paintAnswer(
			p,
			answer,
			padding.left(),
			tshift,
			paintw,
			width(),
			selection,
			ms);
		tshift += height;
	}
	if (!_totalVotesLabel.isEmpty()) {
		tshift += st::msgPadding.bottom();
		p.setPen(regular);
		_totalVotesLabel.drawLeftElided(p, padding.left(), tshift, paintw, width());
	}
}

int HistoryPoll::paintAnswer(
		Painter &p,
		const Answer &answer,
		int left,
		int top,
		int width,
		int outerWidth,
		TextSelection selection,
		TimeMs ms) const {
	const auto result = countAnswerHeight(answer, width);
	const auto bottom = top + result;
	const auto outbg = _parent->hasOutLayout();
	const auto selected = (selection == FullSelection);
	const auto &regular = selected ? (outbg ? st::msgOutDateFgSelected : st::msgInDateFgSelected) : (outbg ? st::msgOutDateFg : st::msgInDateFg);

	const auto aleft = left + st::historyPollAnswerPadding.left();
	const auto awidth = width
		- st::historyPollAnswerPadding.left()
		- st::historyPollAnswerPadding.right();

	top += st::historyPollAnswerPadding.top();

	if (_voted || _closed) {
		p.setFont(st::historyPollPercentFont);
		p.setPen(outbg ? st::webPageDescriptionOutFg : st::webPageDescriptionInFg);
		const auto left = aleft
			- answer.votesPercentWidth
			- st::historyPollPercentSkip;
		p.drawTextLeft(left, top + st::historyPollPercentTop, outerWidth, answer.votesPercent, answer.votesPercentWidth);
	} else {
		PainterHighQualityEnabler hq(p);
		const auto &st = st::defaultRadio;
		auto pen = regular->p;
		pen.setWidth(st.thickness);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(QRectF(left, top, st.diameter, st.diameter).marginsRemoved(QMarginsF(st.thickness / 2., st.thickness / 2., st.thickness / 2., st.thickness / 2.)));
	}

	top += st::defaultCheckbox.textPosition.y();
	p.setPen(outbg ? st::webPageDescriptionOutFg : st::webPageDescriptionInFg);
	answer.text.drawLeft(p, aleft, top, awidth, outerWidth);

	if (_voted || _closed) {
		auto &semibold = selected ? (outbg ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (outbg ? st::msgOutServiceFg : st::msgInServiceFg);
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(semibold);
		const auto size = anim::interpolate(st::historyPollFillingMin, awidth, answer.filling);
		const auto radius = st::historyPollFillingRadius;
		const auto top = bottom - st::historyPollFillingBottom - st::historyPollFillingHeight;
		p.drawRoundedRect(aleft, top, size, st::historyPollFillingHeight, radius, radius);
	} else {
		p.setOpacity(0.5);
		p.fillRect(
			aleft,
			bottom - st::lineWidth,
			outerWidth - aleft,
			st::lineWidth,
			regular);
		p.setOpacity(1.);
	}
	return result;
}

TextState HistoryPoll::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);
	if (_voted || _closed) {
		return result;
	}

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
			result.link = answer.handler;
			return result;
		}
		tshift += height;
	}
	return result;
}

HistoryPoll::~HistoryPoll() {
	Auth().data().unregisterPollView(_poll, _parent);
}
