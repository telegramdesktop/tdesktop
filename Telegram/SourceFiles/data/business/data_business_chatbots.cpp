/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/business/data_business_chatbots.h"

#include "api/api_authorizations.h"
#include "apiwrap.h"
#include "boxes/peers/edit_peer_permissions_box.h"
#include "data/business/data_business_common.h"
#include "data/business/data_business_info.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"

namespace Data {

Chatbots::Chatbots(not_null<Session*> owner)
: _owner(owner) {
}

Chatbots::~Chatbots() = default;

void Chatbots::preload() {
	if (_loaded || _requestId || _saving) {
		return;
	}
	const auto authorizations = &_owner->session().api().authorizations();
	const auto snapshot = authorizations->unreviewed();
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
			_settings.force_assign(ChatbotsSettings{
				.bot = _owner->session().data().user(botId),
				.recipients = FromMTP(_owner, bot.vrecipients()),
				.permissions = FromMTP(bot.vrights()),
			});
		} else {
			_settings.force_assign(ChatbotsSettings());
		}
		authorizations->reconcileBots(result, snapshot);
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
		_loadFailed.fire({});
		LOG(("API Error: Could not get connected bots %1 (%2)"
			).arg(error.code()
			).arg(error.type()));
	}).send();
}

rpl::producer<> Chatbots::loadFailed() const {
	return _loadFailed.events();
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
	if (_saving) {
		_pendingSaves.push_back({
			std::move(settings),
			std::move(done),
			std::move(fail),
		});
		return;
	}
	if (was == settings) {
		if (done) {
			done();
		}
		saveNext();
		return;
	} else if (was.bot || settings.bot) {
		using Flag = MTPaccount_UpdateConnectedBot::Flag;
		const auto api = &_owner->session().api();
		api->request(base::take(_requestId)).cancel();
		_saving = true;
		api->request(MTPaccount_UpdateConnectedBot(
			MTP_flags(!settings.bot ? Flag::f_deleted : Flag::f_rights),
			ToMTP(settings.permissions),
			(settings.bot ? settings.bot : was.bot)->inputUser(),
			ForBotsToMTP(settings.recipients)
		)).done([=](const MTPUpdates &result) {
			api->applyUpdates(result);
			_saving = false;
			if (done) {
				done();
			}
			saveNext();
		}).fail([=](const MTP::Error &error) {
			_saving = false;
			_settings = was;
			if (fail) {
				fail(error.type());
			}
			saveNext();
		}).send();
	}
	_settings = settings;
}

void Chatbots::saveNext() {
	if (_saving) {
		return;
	} else if (_pendingSaves.empty()) {
		reload();
		return;
	}
	auto pending = std::move(_pendingSaves.front());
	_pendingSaves.erase(begin(_pendingSaves));
	save(
		std::move(pending.settings),
		std::move(pending.done),
		std::move(pending.fail));
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
		peer->input(),
		MTP_bool(paused)
	)).done([=] {
		if (_sentRequests[peer].type != type) {
			return;
		} else if (const auto settings = peer->barSettings()) {
			peer->setBarSettings(paused
				? ((*settings | PeerBarSetting::BusinessBotPaused)
					& ~PeerBarSetting::BusinessBotCanReply)
				: ((*settings & ~PeerBarSetting::BusinessBotPaused)
					| PeerBarSetting::BusinessBotCanReply));
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
		peer->input()
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
	if (_saving) {
		return;
	}
	_loaded = false;
	_owner->session().api().request(base::take(_requestId)).cancel();
	preload();
}

EditFlagsDescriptor<ChatbotsPermissions> ChatbotsPermissionsLabels() {
	using Flag = ChatbotsPermission;

	using PermissionLabel = EditFlagsLabel<ChatbotsPermissions>;
	auto messages = std::vector<PermissionLabel>{
		{ Flag::ViewMessages, tr::lng_chatbots_read(tr::now) },
		{ Flag::ReplyToMessages, tr::lng_chatbots_reply(tr::now) },
		{ Flag::MarkAsRead, tr::lng_chatbots_mark_as_read(tr::now) },
		{ Flag::DeleteSent, tr::lng_chatbots_delete_sent(tr::now) },
		{ Flag::DeleteReceived, tr::lng_chatbots_delete_received(tr::now) },
	};
	auto manage = std::vector<PermissionLabel>{
		{ Flag::EditName, tr::lng_chatbots_edit_name(tr::now) },
		{ Flag::EditBio, tr::lng_chatbots_edit_bio(tr::now) },
		{ Flag::EditUserpic, tr::lng_chatbots_edit_userpic(tr::now) },
		{ Flag::EditUsername, tr::lng_chatbots_edit_username(tr::now) },
	};
	auto gifts = std::vector<PermissionLabel>{
		{ Flag::ViewGifts, tr::lng_chatbots_view_gifts(tr::now) },
		{ Flag::SellGifts, tr::lng_chatbots_sell_gifts(tr::now) },
		{ Flag::GiftSettings, tr::lng_chatbots_gift_settings(tr::now) },
		{ Flag::TransferGifts, tr::lng_chatbots_transfer_gifts(tr::now) },
		{ Flag::TransferStars, tr::lng_chatbots_transfer_stars(tr::now) },
	};
	auto stories = std::vector<PermissionLabel>{
		{ Flag::ManageStories, tr::lng_chatbots_manage_stories(tr::now) },
	};
	return { .labels = {
		{ tr::lng_chatbots_manage_messages(), std::move(messages) },
		{ tr::lng_chatbots_manage_profile(), std::move(manage) },
		{ tr::lng_chatbots_manage_gifts(), std::move(gifts) },
		{ std::nullopt, std::move(stories) },
	}, .st = nullptr };
}

} // namespace Data
