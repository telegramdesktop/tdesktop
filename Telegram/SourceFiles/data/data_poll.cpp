/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_poll.h"

#include "apiwrap.h"
#include "auth_session.h"

namespace {

constexpr auto kShortPollTimeout = 30 * TimeMs(1000);

const PollAnswer *AnswerByOption(
		const std::vector<PollAnswer> &list,
		const QByteArray &option) {
	const auto i = ranges::find(
		list,
		option,
		[](const PollAnswer &a) { return a.option; });
	return (i != end(list)) ? &*i : nullptr;
}

PollAnswer *AnswerByOption(
		std::vector<PollAnswer> &list,
		const QByteArray &option) {
	return const_cast<PollAnswer*>(AnswerByOption(
		std::as_const(list),
		option));
}

} // namespace

PollData::PollData(PollId id) : id(id) {
}

bool PollData::applyChanges(const MTPDpoll &poll) {
	Expects(poll.vid.v == id);

	const auto newQuestion = qs(poll.vquestion);
	const auto newClosed = poll.is_closed();
	auto newAnswers = ranges::view::all(
		poll.vanswers.v
	) | ranges::view::transform([](const MTPPollAnswer &data) {
		return data.match([](const MTPDpollAnswer &answer) {
			auto result = PollAnswer();
			result.option = answer.voption.v;
			result.text = qs(answer.vtext);
			return result;
		});
	}) | ranges::to_vector;

	const auto changed1 = (question != newQuestion)
		|| (closed != newClosed);
	const auto changed2 = (answers != newAnswers);
	if (!changed1 && !changed2) {
		return false;
	}
	if (changed1) {
		question = newQuestion;
		closed = newClosed;
	}
	if (changed2) {
		std::swap(answers, newAnswers);
		for (const auto &old : newAnswers) {
			if (const auto current = answerByOption(old.option)) {
				current->votes = old.votes;
				current->chosen = old.chosen;
			}
		}
	}
	++version;
	return true;
}

bool PollData::applyResults(const MTPPollResults &results) {
	return results.match([&](const MTPDpollResults &results) {
		const auto newTotalVoters = results.has_total_voters()
			? results.vtotal_voters.v
			: totalVoters;
		auto changed = (newTotalVoters != totalVoters);
		if (results.has_results()) {
			for (const auto &result : results.vresults.v) {
				if (applyResultToAnswers(result, results.is_min())) {
					changed = true;
				}
			}
		}
		totalVoters = newTotalVoters;
		lastResultsUpdate = getms();
		return changed;
	});
}

void PollData::checkResultsReload(not_null<HistoryItem*> item, TimeMs now) {
	if (lastResultsUpdate && lastResultsUpdate + kShortPollTimeout > now) {
		return;
	} else if (closed) {
		return;
	}
	lastResultsUpdate = now;
	Auth().api().reloadPollResults(item);
}

PollAnswer *PollData::answerByOption(const QByteArray &option) {
	return AnswerByOption(answers, option);
}

const PollAnswer *PollData::answerByOption(const QByteArray &option) const {
	return AnswerByOption(answers, option);
}

bool PollData::applyResultToAnswers(
		const MTPPollAnswerVoters &result,
		bool isMinResults) {
	return result.match([&](const MTPDpollAnswerVoters &voters) {
		const auto &option = voters.voption.v;
		const auto answer = answerByOption(option);
		if (!answer) {
			return false;
		}
		auto changed = (answer->votes != voters.vvoters.v);
		if (changed) {
			answer->votes = voters.vvoters.v;
		}
		if (!isMinResults) {
			if (answer->chosen != voters.is_chosen()) {
				answer->chosen = voters.is_chosen();
				changed = true;
			}
		} else if (const auto existing = answerByOption(option)) {
			answer->chosen = existing->chosen;
		}
		return changed;
	});
}

bool PollData::voted() const {
	return ranges::find(answers, true, &PollAnswer::chosen) != end(answers);
}

MTPPoll PollDataToMTP(not_null<const PollData*> poll) {
	const auto convert = [](const PollAnswer &answer) {
		return MTP_pollAnswer(
			MTP_string(answer.text),
			MTP_bytes(answer.option));
	};
	auto answers = QVector<MTPPollAnswer>();
	answers.reserve(poll->answers.size());
	ranges::transform(
		poll->answers,
		ranges::back_inserter(answers),
		convert);
	return MTP_poll(
		MTP_long(poll->id),
		MTP_flags(MTPDpoll::Flag::f_closed),
		MTP_string(poll->question),
		MTP_vector<MTPPollAnswer>(answers));
}
