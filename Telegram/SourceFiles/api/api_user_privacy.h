/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"

class ApiWrap;

namespace Main {
class Session;
} // namespace Main

namespace Api {

class UserPrivacy final {
public:
	enum class Key {
		PhoneNumber,
		AddedByPhone,
		LastSeen,
		Calls,
		Invites,
		CallsPeer2Peer,
		Forwards,
		ProfilePhoto,
		Voices,
		About,
		Birthday,
		GiftsAutoSave,
	};
	enum class Option {
		Everyone,
		Contacts,
		CloseFriends,
		Nobody,
	};
	struct Exceptions {
		std::vector<not_null<PeerData*>> peers;
		bool premiums = false;
		bool miniapps = false;
	};
	struct Rule {
		Option option = Option::Everyone;
		Exceptions always;
		Exceptions never;
		bool ignoreAlways = false;
		bool ignoreNever = false;
	};

	explicit UserPrivacy(not_null<ApiWrap*> api);

	void save(
		Key key,
		const UserPrivacy::Rule &rule);
	void apply(
		mtpTypeId type,
		const MTPVector<MTPPrivacyRule> &rules,
		bool allLoaded);

	void reload(Key key);
	rpl::producer<Rule> value(Key key);

private:
	const not_null<Main::Session*> _session;

	void pushPrivacy(Key key, const MTPVector<MTPPrivacyRule> &rules);

	base::flat_map<mtpTypeId, mtpRequestId> _privacySaveRequests;

	base::flat_map<Key, mtpRequestId> _privacyRequestIds;
	base::flat_map<Key, Rule> _privacyValues;
	std::map<Key, rpl::event_stream<Rule>> _privacyChanges;

	MTP::Sender _api;

};

} // namespace Api
