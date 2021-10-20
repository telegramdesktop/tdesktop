/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {
class Session;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

struct PollAnswer {
	QString text;
	QByteArray option;
	int votes = 0;
	bool chosen = false;
	bool correct = false;
};

inline bool operator==(const PollAnswer &a, const PollAnswer &b) {
	return (a.text == b.text)
		&& (a.option == b.option);
}

inline bool operator!=(const PollAnswer &a, const PollAnswer &b) {
	return !(a == b);
}

struct PollData {
	PollData(not_null<Data::Session*> owner, PollId id);

	[[nodiscard]] Data::Session &owner() const;
	[[nodiscard]] Main::Session &session() const;

	enum class Flag {
		Closed      = 0x01,
		PublicVotes = 0x02,
		MultiChoice = 0x04,
		Quiz        = 0x08,
	};
	friend inline constexpr bool is_flag_type(Flag) { return true; };
	using Flags = base::flags<Flag>;

	bool closeByTimer();
	bool applyChanges(const MTPDpoll &poll);
	bool applyResults(const MTPPollResults &results);
	[[nodiscard]] bool checkResultsReload(crl::time now);

	[[nodiscard]] PollAnswer *answerByOption(const QByteArray &option);
	[[nodiscard]] const PollAnswer *answerByOption(
		const QByteArray &option) const;

	void setFlags(Flags flags);
	[[nodiscard]] Flags flags() const;
	[[nodiscard]] bool voted() const;
	[[nodiscard]] bool closed() const;
	[[nodiscard]] bool publicVotes() const;
	[[nodiscard]] bool multiChoice() const;
	[[nodiscard]] bool quiz() const;

	PollId id = 0;
	QString question;
	std::vector<PollAnswer> answers;
	std::vector<not_null<UserData*>> recentVoters;
	std::vector<QByteArray> sendingVotes;
	TextWithEntities solution;
	TimeId closePeriod = 0;
	TimeId closeDate = 0;
	int totalVoters = 0;
	int version = 0;

	static constexpr auto kMaxOptions = 10;

private:
	bool applyResultToAnswers(
		const MTPPollAnswerVoters &result,
		bool isMinResults);

	const not_null<Data::Session*> _owner;
	Flags _flags = Flags();
	crl::time _lastResultsUpdate = 0; // < 0 means force reload.

};

[[nodiscard]] MTPPoll PollDataToMTP(
	not_null<const PollData*> poll,
	bool close = false);
[[nodiscard]] MTPInputMedia PollDataToInputMedia(
	not_null<const PollData*> poll,
	bool close = false);
