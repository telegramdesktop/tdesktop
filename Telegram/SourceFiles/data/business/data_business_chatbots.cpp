/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/business/data_business_chatbots.h"

#include "apiwrap.h"
#include "data/business/data_business_common.h"
#include "data/business/data_business_info.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"

namespace Data {

Chatbots::Chatbots(not_null<Session*> owner)
: _owner(owner) {
}

Chatbots::~Chatbots() = default;

void Chatbots::preload() {
	if (_loaded || _requestId) {
		return;
	}
	_requestId = _owner->session().api().request(
		MTPaccount_GetConnectedBots()
	).done([=](const MTPaccount_ConnectedBots &result) {
		_requestId = 0;
		_loaded = true;

		const auto &data = result.data();
		_owner->processUsers(data.vusers());
		const auto &list = data.vconnected_bots().v;
		if (!list.isEmpty()) {
			const auto &bot = list.front().data();
			const auto botId = bot.vbot_id().v;
			_settings = ChatbotsSettings{
				.bot = _owner->session().data().user(botId),
				.recipients = FromMTP(_owner, bot.vrecipients()),
				.repliesAllowed = bot.is_can_reply(),
			};
		} else {
			_settings.force_assign(ChatbotsSettings());
		}
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
		LOG(("API Error: Could not get connected bots %1 (%2)"
			).arg(error.code()
			).arg(error.type()));
	}).send();
}

bool Chatbots::loaded() const {
	return _loaded;
}

const ChatbotsSettings &Chatbots::current() const {
	return _settings.current();
}

rpl::producer<ChatbotsSettings> Chatbots::changes() const {
	return _settings.changes();
}

rpl::producer<ChatbotsSettings> Chatbots::value() const {
	return _settings.value();
}

void Chatbots::save(
		ChatbotsSettings settings,
		Fn<void()> done,
		Fn<void(QString)> fail) {
	const auto was = _settings.current();
	if (was == settings) {
		return;
	} else if (was.bot || settings.bot) {
		using Flag = MTPaccount_UpdateConnectedBot::Flag;
		const auto api = &_owner->session().api();
		api->request(MTPaccount_UpdateConnectedBot(
			MTP_flags(!settings.bot
				? Flag::f_deleted
				: settings.repliesAllowed
				? Flag::f_can_reply
				: Flag()),
			(settings.bot ? settings.bot : was.bot)->inputUser,
			ForBotsToMTP(settings.recipients)
		)).done([=](const MTPUpdates &result) {
			api->applyUpdates(result);
			if (done) {
				done();
			}
		}).fail([=](const MTP::Error &error) {
			_settings = was;
			if (fail) {
				fail(error.type());
			}
		}).send();
	}
	_settings = settings;
}

void Chatbots::togglePaused(not_null<PeerData*> peer, bool paused) {
	const auto type = paused
		? SentRequestType::Pause
		: SentRequestType::Unpause;
	const auto api = &_owner->session().api();
	const auto i = _sentRequests.find(peer);
	if (i != end(_sentRequests)) {
		const auto already = i->second.type;
		if (already == SentRequestType::Remove || already == type) {
			return;
		}
		api->request(i->second.requestId).cancel();
		_sentRequests.erase(i);
	}
	const auto id = api->request(MTPaccount_ToggleConnectedBotPaused(
		peer->input,
		MTP_bool(paused)
	)).done([=] {
		if (_sentRequests[peer].type != type) {
			return;
		} else if (const auto settings = peer->barSettings()) {
			peer->setBarSettings(paused
				? (*settings | PeerBarSetting::BusinessBotPaused)
				: (*settings & ~PeerBarSetting::BusinessBotPaused));
		} else {
			api->requestPeerSettings(peer);
		}
		_sentRequests.remove(peer);
	}).fail([=] {
		if (_sentRequests[peer].type != type) {
			return;
		}
		api->requestPeerSettings(peer);
		_sentRequests.remove(peer);
	}).send();
	_sentRequests[peer] = SentRequest{ type, id };
}

void Chatbots::removeFrom(not_null<PeerData*> peer) {
	const auto type = SentRequestType::Remove;
	const auto api = &_owner->session().api();
	const auto i = _sentRequests.find(peer);
	if (i != end(_sentRequests)) {
		const auto already = i->second.type;
		if (already == type) {
			return;
		}
		api->request(i->second.requestId).cancel();
		_sentRequests.erase(i);
	}
	const auto id = api->request(MTPaccount_DisablePeerConnectedBot(
		peer->input
	)).done([=] {
		if (_sentRequests[peer].type != type) {
			return;
		} else if (const auto settings = peer->barSettings()) {
			peer->clearBusinessBot();
		} else {
			api->requestPeerSettings(peer);
		}
		_sentRequests.remove(peer);
		reload();
	}).fail([=] {
		api->requestPeerSettings(peer);
		_sentRequests.remove(peer);
	}).send();
	_sentRequests[peer] = SentRequest{ type, id };
}

void Chatbots::reload() {
	_loaded = false;
	_owner->session().api().request(base::take(_requestId)).cancel();
	preload();
}

} // namespace Data
