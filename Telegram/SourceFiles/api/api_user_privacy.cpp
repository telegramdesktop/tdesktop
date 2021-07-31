/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_user_privacy.h"

#include "apiwrap.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"

namespace Api {
namespace {

constexpr auto kMaxRules = 3; // Allow users, disallow users, Option.

using TLInputRules = MTPVector<MTPInputPrivacyRule>;
using TLRules = MTPVector<MTPPrivacyRule>;

TLInputRules RulesToTL(const UserPrivacy::Rule &rule) {
	const auto collectInputUsers = [](const auto &peers) {
		auto result = QVector<MTPInputUser>();
		result.reserve(peers.size());
		for (const auto peer : peers) {
			if (const auto user = peer->asUser()) {
				result.push_back(user->inputUser);
			}
		}
		return result;
	};
	const auto collectInputChats = [](const auto &peers) {
		auto result = QVector<MTPint>(); // #TODO ids
		result.reserve(peers.size());
		for (const auto peer : peers) {
			if (!peer->isUser()) {
				result.push_back(peerToBareMTPInt(peer->id));
			}
		}
		return result;
	};

	auto result = QVector<MTPInputPrivacyRule>();
	result.reserve(kMaxRules);
	if (!rule.ignoreAlways) {
		const auto users = collectInputUsers(rule.always);
		const auto chats = collectInputChats(rule.always);
		if (!users.empty()) {
			result.push_back(
				MTP_inputPrivacyValueAllowUsers(
					MTP_vector<MTPInputUser>(users)));
		}
		if (!chats.empty()) {
			result.push_back(
				MTP_inputPrivacyValueAllowChatParticipants(
					MTP_vector<MTPint>(chats)));
		}
	}
	if (!rule.ignoreNever) {
		const auto users = collectInputUsers(rule.never);
		const auto chats = collectInputChats(rule.never);
		if (!users.empty()) {
			result.push_back(
				MTP_inputPrivacyValueDisallowUsers(
					MTP_vector<MTPInputUser>(users)));
		}
		if (!chats.empty()) {
			result.push_back(
				MTP_inputPrivacyValueDisallowChatParticipants(
					MTP_vector<MTPint>(chats)));
		}
	}
	result.push_back([&] {
		using Option = UserPrivacy::Option;
		switch (rule.option) {
		case Option::Everyone: return MTP_inputPrivacyValueAllowAll();
		case Option::Contacts: return MTP_inputPrivacyValueAllowContacts();
		case Option::Nobody: return MTP_inputPrivacyValueDisallowAll();
		}
		Unexpected("Option value in Api::UserPrivacy::RulesToTL.");
	}());


	return MTP_vector<MTPInputPrivacyRule>(std::move(result));
}

UserPrivacy::Rule TLToRules(const TLRules &rules, Data::Session &owner) {
	// This is simplified version of privacy rules interpretation.
	// But it should be fine for all the apps
	// that use the same subset of features.
	using Option = UserPrivacy::Option;
	auto result = UserPrivacy::Rule();
	auto optionSet = false;
	const auto setOption = [&](Option option) {
		if (optionSet) return;
		optionSet = true;
		result.option = option;
	};
	auto &always = result.always;
	auto &never = result.never;
	const auto feed = [&](const MTPPrivacyRule &rule) {
		rule.match([&](const MTPDprivacyValueAllowAll &) {
			setOption(Option::Everyone);
		}, [&](const MTPDprivacyValueAllowContacts &) {
			setOption(Option::Contacts);
		}, [&](const MTPDprivacyValueAllowUsers &data) {
			const auto &users = data.vusers().v;
			always.reserve(always.size() + users.size());
			for (const auto userId : users) {
				const auto user = owner.user(UserId(userId.v));
				if (!base::contains(never, user)
					&& !base::contains(always, user)) {
					always.emplace_back(user);
				}
			}
		}, [&](const MTPDprivacyValueAllowChatParticipants &data) {
			const auto &chats = data.vchats().v;
			always.reserve(always.size() + chats.size());
			for (const auto &chatId : chats) {
				const auto chat = owner.chatLoaded(chatId);
				const auto peer = chat
					? static_cast<PeerData*>(chat)
					: owner.channelLoaded(chatId);
				if (peer
					&& !base::contains(never, peer)
					&& !base::contains(always, peer)) {
					always.emplace_back(peer);
				}
			}
		}, [&](const MTPDprivacyValueDisallowContacts &) {
			// Not supported
		}, [&](const MTPDprivacyValueDisallowAll &) {
			setOption(Option::Nobody);
		}, [&](const MTPDprivacyValueDisallowUsers &data) {
			const auto &users = data.vusers().v;
			never.reserve(never.size() + users.size());
			for (const auto userId : users) {
				const auto user = owner.user(UserId(userId.v));
				if (!base::contains(always, user)
					&& !base::contains(never, user)) {
					never.emplace_back(user);
				}
			}
		}, [&](const MTPDprivacyValueDisallowChatParticipants &data) {
			const auto &chats = data.vchats().v;
			never.reserve(never.size() + chats.size());
			for (const auto &chatId : chats) {
				const auto chat = owner.chatLoaded(chatId);
				const auto peer = chat
					? static_cast<PeerData*>(chat)
					: owner.channelLoaded(chatId);
				if (peer
					&& !base::contains(always, peer)
					&& !base::contains(never, peer)) {
					never.emplace_back(peer);
				}
			}
		});
	};
	for (const auto &rule : rules.v) {
		feed(rule);
	}
	feed(MTP_privacyValueDisallowAll()); // Disallow by default.
	return result;
}

MTPInputPrivacyKey KeyToTL(UserPrivacy::Key key) {
	using Key = UserPrivacy::Key;
	switch (key) {
	case Key::Calls: return MTP_inputPrivacyKeyPhoneCall();
	case Key::Invites: return MTP_inputPrivacyKeyChatInvite();
	case Key::PhoneNumber: return MTP_inputPrivacyKeyPhoneNumber();
	case Key::AddedByPhone:
		return MTP_inputPrivacyKeyAddedByPhone();
	case Key::LastSeen:
		return MTP_inputPrivacyKeyStatusTimestamp();
	case Key::CallsPeer2Peer:
		return MTP_inputPrivacyKeyPhoneP2P();
	case Key::Forwards:
		return MTP_inputPrivacyKeyForwards();
	case Key::ProfilePhoto:
		return MTP_inputPrivacyKeyProfilePhoto();
	}
	Unexpected("Key in Api::UserPrivacy::KetToTL.");
}

std::optional<UserPrivacy::Key> TLToKey(mtpTypeId type) {
	using Key = UserPrivacy::Key;
	switch (type) {
	case mtpc_privacyKeyPhoneNumber:
	case mtpc_inputPrivacyKeyPhoneNumber: return Key::PhoneNumber;
	case mtpc_privacyKeyAddedByPhone:
	case mtpc_inputPrivacyKeyAddedByPhone: return Key::AddedByPhone;
	case mtpc_privacyKeyStatusTimestamp:
	case mtpc_inputPrivacyKeyStatusTimestamp: return Key::LastSeen;
	case mtpc_privacyKeyChatInvite:
	case mtpc_inputPrivacyKeyChatInvite: return Key::Invites;
	case mtpc_privacyKeyPhoneCall:
	case mtpc_inputPrivacyKeyPhoneCall: return Key::Calls;
	case mtpc_privacyKeyPhoneP2P:
	case mtpc_inputPrivacyKeyPhoneP2P: return Key::CallsPeer2Peer;
	case mtpc_privacyKeyForwards:
	case mtpc_inputPrivacyKeyForwards: return Key::Forwards;
	case mtpc_privacyKeyProfilePhoto:
	case mtpc_inputPrivacyKeyProfilePhoto: return Key::ProfilePhoto;
	}
	return std::nullopt;
}

} // namespace

UserPrivacy::UserPrivacy(not_null<ApiWrap*> api)
: _session(&api->session())
, _api(&api->instance()) {
}

void UserPrivacy::save(
		Key key,
		const UserPrivacy::Rule &rule) {
	const auto tlKey = KeyToTL(key);
	const auto keyTypeId = tlKey.type();
	const auto it = _privacySaveRequests.find(keyTypeId);
	if (it != _privacySaveRequests.cend()) {
		_api.request(it->second).cancel();
		_privacySaveRequests.erase(it);
	}

	const auto requestId = _api.request(MTPaccount_SetPrivacy(
		tlKey,
		RulesToTL(rule)
	)).done([=](const MTPaccount_PrivacyRules &result) {
		result.match([&](const MTPDaccount_privacyRules &data) {
			_session->data().processUsers(data.vusers());
			_session->data().processChats(data.vchats());
			_privacySaveRequests.remove(keyTypeId);
			apply(keyTypeId, data.vrules(), true);
		});
	}).fail([=](const MTP::Error &error) {
		_privacySaveRequests.remove(keyTypeId);
	}).send();

	_privacySaveRequests.emplace(keyTypeId, requestId);
}

void UserPrivacy::apply(
		mtpTypeId type,
		const TLRules &rules,
		bool allLoaded) {
	if (const auto key = TLToKey(type)) {
		if (!allLoaded) {
			reload(*key);
			return;
		}
		pushPrivacy(*key, rules);
		if ((*key) == Key::LastSeen) {
			_session->api().updatePrivacyLastSeens();
		}
	}
}

void UserPrivacy::reload(Key key) {
	if (_privacyRequestIds.contains(key)) {
		return;
	}
	const auto requestId = _api.request(MTPaccount_GetPrivacy(
		KeyToTL(key)
	)).done([=](const MTPaccount_PrivacyRules &result) {
		_privacyRequestIds.erase(key);
		result.match([&](const MTPDaccount_privacyRules &data) {
			_session->data().processUsers(data.vusers());
			_session->data().processChats(data.vchats());
			pushPrivacy(key, data.vrules());
		});
	}).fail([=](const MTP::Error &error) {
		_privacyRequestIds.erase(key);
	}).send();
	_privacyRequestIds.emplace(key, requestId);
}

void UserPrivacy::pushPrivacy(Key key, const TLRules &rules) {
	const auto &saved = (_privacyValues[key] =
		TLToRules(rules, _session->data()));
	const auto i = _privacyChanges.find(key);
	if (i != end(_privacyChanges)) {
		i->second.fire_copy(saved);
	}
}

auto UserPrivacy::value(Key key) -> rpl::producer<UserPrivacy::Rule> {
	if (const auto i = _privacyValues.find(key); i != end(_privacyValues)) {
		return _privacyChanges[key].events_starting_with_copy(i->second);
	} else {
		return _privacyChanges[key].events();
	}
}

} // namespace Api
