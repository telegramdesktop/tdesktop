/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/media/history_media_poll.h"

#include "lang/lang_keys.h"
#include "layout.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "calls/calls_instance.h"
#include "ui/text_options.h"
#include "data/data_media_types.h"
#include "data/data_poll.h"
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
		+ st::msgDateTextStyle.font->height
		+ st::historyPollAnswersSkip
		+ answersHeight
		+ st::msgPadding.bottom()
		+ st::msgDateTextStyle.font->height
		+ st::msgPadding.bottom();
	if (!isBubbleTop()) {
		minHeight -= st::msgFileTopMinus;
	}
	return { maxWidth, minHeight };
}

QSize HistoryPoll::countCurrentSize(int newWidth) {
	const auto paddings = st::msgPadding.left() + st::msgPadding.right();

	accumulate_min(newWidth, maxWidth());
	const auto innerWidth = newWidth
		- st::msgPadding.left()
		- st::msgPadding.right();

	const auto answerWidth = innerWidth
		- st::historyPollAnswerPadding.left()
		- st::historyPollAnswerPadding.right();
	const auto answersHeight = ranges::accumulate(ranges::view::all(
		_answers
	) | ranges::view::transform([&](const Answer &answer) {
		return st::historyPollAnswerPadding.top()
			+ 2 * st::defaultCheckbox.textPosition.y()
			+ answer.text.countHeight(answerWidth)
			+ st::historyPollAnswerPadding.bottom()
			+ st::lineWidth;
	}), 0);

	auto newHeight = st::historyPollQuestionTop
		+ _question.countHeight(innerWidth)
		+ st::historyPollSubtitleSkip
		+ st::msgDateTextStyle.font->height
		+ st::historyPollAnswersSkip
		+ answersHeight
		+ st::msgPadding.bottom()
		+ st::msgDateTextStyle.font->height
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

	_voted = ranges::find(
		_poll->answers,
		true,
		[](const PollAnswer &answer) { return answer.chosen; }
	) != end(_poll->answers);
}

void HistoryPoll::updateVotes() const {
	updateTotalVotes();
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

void HistoryPoll::draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	auto paintx = 0, painty = 0, paintw = width(), painth = height();

	updateVotes();

	auto outbg = _parent->hasOutLayout();
	bool selected = (selection == FullSelection);

	auto &semibold = selected ? (outbg ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (outbg ? st::msgOutServiceFg : st::msgInServiceFg);
	auto &regular = selected ? (outbg ? st::msgOutDateFgSelected : st::msgInDateFgSelected) : (outbg ? st::msgOutDateFg : st::msgInDateFg);

	auto padding = st::msgPadding;
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
	tshift += st::msgDateTextStyle.font->height + st::historyPollAnswersSkip;

	const auto aleft = padding.left() + st::historyPollAnswerPadding.left();
	const auto awidth = paintw - st::historyPollAnswerPadding.left() - st::historyPollAnswerPadding.right();
	for (const auto &answer : _answers) {
		tshift += st::historyPollAnswerPadding.top();

		{
			PainterHighQualityEnabler hq(p);
			const auto &st = st::defaultRadio;
			auto pen = regular->p;
			pen.setWidth(st.thickness);
			p.setPen(pen);
			p.setBrush(Qt::NoBrush);
			p.drawEllipse(QRectF(padding.left(), tshift, st.diameter, st.diameter).marginsRemoved(QMarginsF(st.thickness / 2., st.thickness / 2., st.thickness / 2., st.thickness / 2.)));
		}

		tshift += st::defaultCheckbox.textPosition.y();
		p.setPen(outbg ? st::webPageDescriptionOutFg : st::webPageDescriptionInFg);
		answer.text.drawLeft(p, aleft, tshift, awidth, width());
		tshift += answer.text.countHeight(awidth)
			+ st::defaultCheckbox.textPosition.y()
			+ st::historyPollAnswerPadding.bottom();

		p.setOpacity(0.5);
		p.fillRect(aleft, tshift, width() - aleft, st::lineWidth, regular);
		tshift += st::lineWidth;
		p.setOpacity(1.);
	}
	if (!_totalVotesLabel.isEmpty()) {
		tshift += st::msgPadding.bottom();
		p.setPen(regular);
		_totalVotesLabel.drawLeftElided(p, padding.left(), tshift, paintw, width());
	}
}

TextState HistoryPoll::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);
	if (QRect(0, 0, width(), height()).contains(point)) {
//		result.link = _link;
		return result;
	}
	return result;
}
