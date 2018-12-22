/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

struct PollAnswer {
	QString text;
	QByteArray option;
	int votes = 0;
	bool chosen = false;
};

inline bool operator==(const PollAnswer &a, const PollAnswer &b) {
	return (a.text == b.text)
		&& (a.option == b.option);
}

inline bool operator!=(const PollAnswer &a, const PollAnswer &b) {
	return !(a == b);
}

struct PollData {
	explicit PollData(PollId id);

	bool applyChanges(const MTPDpoll &poll);
	bool applyResults(const MTPPollResults &results);

	PollAnswer *answerByOption(const QByteArray &option);
	const PollAnswer *answerByOption(const QByteArray &option) const;

	bool voted() const;

	PollId id = 0;
	QString question;
	std::vector<PollAnswer> answers;
	int totalVoters = 0;
	bool closed = false;
	QByteArray sendingVote;

	int version = 0;

private:
	bool applyResultToAnswers(
		const MTPPollAnswerVoters &result,
		bool isMinResults);

};

MTPPoll PollDataToMTP(not_null<const PollData*> poll);
