/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_location.h"

namespace Data {
class Session;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

class PeerData;
class PhotoData;
class DocumentData;
struct WebPageData;

struct PollMedia {
	PhotoData *photo = nullptr;
	DocumentData *document = nullptr;
	std::optional<Data::LocationPoint> geo;
	WebPageData *webpage = nullptr;
	QString url;

	explicit operator bool() const {
		return photo || document || geo || !url.isEmpty();
	}
};

inline bool operator==(const PollMedia &a, const PollMedia &b) {
	return (a.photo == b.photo)
		&& (a.document == b.document)
		&& (a.geo == b.geo)
		&& (a.webpage == b.webpage)
		&& (a.url == b.url);
}

inline bool operator!=(const PollMedia &a, const PollMedia &b) {
	return !(a == b);
}

struct PollAnswer {
	TextWithEntities text;
	QByteArray option;
	PollMedia media;
	std::vector<not_null<PeerData*>> recentVoters;
	PeerData *addedBy = nullptr;
	TimeId addedDate = 0;
	int votes = 0;
	bool chosen = false;
	bool correct = false;
};

inline bool operator==(const PollAnswer &a, const PollAnswer &b) {
	return (a.text == b.text)
		&& (a.option == b.option)
		&& (a.media == b.media);
}

inline bool operator!=(const PollAnswer &a, const PollAnswer &b) {
	return !(a == b);
}

struct PollData {
	PollData(not_null<Data::Session*> owner, PollId id);

	[[nodiscard]] Data::Session &owner() const;
	[[nodiscard]] Main::Session &session() const;

	enum class Flag {
		Closed                = 0x001,
		PublicVotes           = 0x002,
		MultiChoice           = 0x004,
		Quiz                  = 0x008,
		ShuffleAnswers        = 0x010,
		RevotingDisabled      = 0x020,
		OpenAnswers           = 0x040,
		HideResultsUntilClose = 0x080,
		Creator               = 0x100,
		SubscribersOnly       = 0x200,
		CanViewStats          = 0x400,
	};
	friend inline constexpr bool is_flag_type(Flag) { return true; };
	using Flags = base::flags<Flag>;
	enum class VoteRestriction {
		None,
		SubscribersOnly,
		SubscribersJoinedTooRecently,
		Countries,
	};

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
	[[nodiscard]] bool shuffleAnswers() const;
	[[nodiscard]] bool revotingDisabled() const;
	[[nodiscard]] bool openAnswers() const;
	[[nodiscard]] bool hideResultsUntilClose() const;
	[[nodiscard]] bool creator() const;
	[[nodiscard]] bool subscribersOnly() const;
	[[nodiscard]] bool canViewStats() const;
	void setVoteRestriction(VoteRestriction restriction);
	[[nodiscard]] VoteRestriction voteRestriction() const;
	[[nodiscard]] crl::time voteRestrictionUpdated() const;

	[[nodiscard]] QString debugString() const;

	PollId id = 0;
	TextWithEntities question;
	std::vector<PollAnswer> answers;
	std::vector<not_null<PeerData*>> recentVoters;
	std::vector<QByteArray> sendingVotes;
	TextWithEntities solution;
	PollMedia attachedMedia;
	PollMedia solutionMedia;
	std::vector<QString> countries;
	TimeId closePeriod = 0;
	TimeId closeDate = 0;
	int totalVoters = 0;
	int version = 0;
	uint64 hash = 0;

	static constexpr auto kMaxOptions = 32;

private:
	bool applyResultToAnswers(
		const MTPPollAnswerVoters &result,
		bool isMinResults);

	const not_null<Data::Session*> _owner;
	Flags _flags = Flags();
	VoteRestriction _voteRestriction = VoteRestriction::None;
	crl::time _voteRestrictionUpdated = 0;
	crl::time _lastResultsUpdate = 0; // < 0 means force reload.

};

inline constexpr auto kDefaultPollCreateFlags = PollData::Flag::PublicVotes
	| PollData::Flag::MultiChoice
	| PollData::Flag::OpenAnswers
	| PollData::Flag::ShuffleAnswers;

[[nodiscard]] QString JoinPollCountries(
	const std::vector<QString> &countriesIso2);
[[nodiscard]] TextWithEntities PollCountriesRestrictionText(
	const std::vector<QString> &countries);
[[nodiscard]] TextWithEntities PollVoteRestrictionText(
	PollData::VoteRestriction restriction,
	not_null<PeerData*> peer,
	not_null<const PollData*> poll);

[[nodiscard]] QByteArray PollOptionFromLink(const QString &value);
[[nodiscard]] QString PollOptionToLink(const QByteArray &option);

[[nodiscard]] MTPPoll PollDataToMTP(
	not_null<const PollData*> poll,
	bool close = false);
[[nodiscard]] MTPInputMedia PollDataToInputMedia(
	not_null<const PollData*> poll,
	bool close = false);
[[nodiscard]] MTPInputMedia PollMediaToMTP(const PollMedia &media);
[[nodiscard]] PollMedia PollMediaFromMTP(
	not_null<Data::Session*> owner,
	const MTPMessageMedia &media);
[[nodiscard]] PollMedia PollMediaFromInputMTP(
	not_null<Data::Session*> owner,
	const MTPInputMedia &media);
