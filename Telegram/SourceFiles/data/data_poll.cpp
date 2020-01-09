/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_poll.h"

#include "apiwrap.h"
#include "main/main_session.h"

namespace {

constexpr auto kShortPollTimeout = 30 * crl::time(1000);

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
	Expects(poll.vid().v == id);

	const auto newQuestion = qs(poll.vquestion());
	const auto newFlags = (poll.is_closed() ? Flag::Closed : Flag(0))
		| (poll.is_public_voters() ? Flag::PublicVotes : Flag(0))
		| (poll.is_multiple_choice() ? Flag::MultiChoice : Flag(0))
		| (poll.is_quiz() ? Flag::Quiz : Flag(0));
	auto newAnswers = ranges::view::all(
		poll.vanswers().v
	) | ranges::view::transform([](const MTPPollAnswer &data) {
		return data.match([](const MTPDpollAnswer &answer) {
			auto result = PollAnswer();
			result.option = answer.voption().v;
			result.text = qs(answer.vtext());
			return result;
		});
	}) | ranges::view::take(
		kMaxOptions
	) | ranges::to_vector;

	const auto changed1 = (question != newQuestion)
		|| (_flags != newFlags);
	const auto changed2 = (answers != newAnswers);
	if (!changed1 && !changed2) {
		return false;
	}
	if (changed1) {
		question = newQuestion;
		_flags = newFlags;
	}
	if (changed2) {
		std::swap(answers, newAnswers);
		for (const auto &old : newAnswers) {
			if (const auto current = answerByOption(old.option)) {
				current->votes = old.votes;
				current->chosen = old.chosen;
				current->correct = old.correct;
			}
		}
	}
	++version;
	return true;
}

bool PollData::applyResults(const MTPPollResults &results) {
	return results.match([&](const MTPDpollResults &results) {
		lastResultsUpdate = crl::now();

		const auto newTotalVoters =
			results.vtotal_voters().value_or(totalVoters);
		auto changed = (newTotalVoters != totalVoters);
		if (const auto list = results.vresults()) {
			for (const auto &result : list->v) {
				if (applyResultToAnswers(result, results.is_min())) {
					changed = true;
				}
			}
		}
		if (!changed) {
			return false;
		}
		totalVoters = newTotalVoters;
		++version;
		return changed;
	});
}

void PollData::checkResultsReload(not_null<HistoryItem*> item, crl::time now) {
	if (lastResultsUpdate && lastResultsUpdate + kShortPollTimeout > now) {
		return;
	} else if (closed()) {
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
		const auto &option = voters.voption().v;
		const auto answer = answerByOption(option);
		if (!answer) {
			return false;
		}
		auto changed = (answer->votes != voters.vvoters().v);
		if (changed) {
			answer->votes = voters.vvoters().v;
		}
		if (!isMinResults) {
			if (answer->chosen != voters.is_chosen()) {
				answer->chosen = voters.is_chosen();
				changed = true;
			}
			if (answer->correct != voters.is_correct()) {
				answer->correct = voters.is_correct();
				changed = true;
			}
		} else if (const auto existing = answerByOption(option)) {
			answer->chosen = existing->chosen;
			answer->correct = existing->correct;
		}
		return changed;
	});
}

PollData::Flags PollData::flags() const {
	return _flags;
}

bool PollData::voted() const {
	return ranges::find(answers, true, &PollAnswer::chosen) != end(answers);
}

bool PollData::closed() const {
	return (_flags & Flag::Closed);
}

bool PollData::publicVotes() const {
	return (_flags & Flag::PublicVotes);
}

bool PollData::multiChoice() const {
	return (_flags & Flag::MultiChoice);
}

bool PollData::quiz() const {
	return (_flags & Flag::Quiz);
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
