/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_messages.h"

#include "apiwrap.h"
#include "api/api_text_entities.h"
#include "base/random.h"
#include "base/unixtime.h"
#include "calls/group/calls_group_call.h"
#include "calls/group/calls_group_message_encryption.h"
#include "data/data_group_call.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "mtproto/sender.h"
#include "ui/text/text_utilities.h"
#include "ui/ui_utility.h"

namespace Calls::Group {

Messages::Messages(not_null<GroupCall*> call, not_null<MTP::Sender*> api)
: _call(call)
, _api(api)
, _destroyTimer([=] { checkDestroying(); })
, _ttl(_call->peer()->session().appConfig().groupCallMessageTTL()) {
	Ui::PostponeCall(_call, [=] {
		_call->real(
		) | rpl::start_with_next([=](not_null<Data::GroupCall*> call) {
			_real = call;
			if (ready()) {
				sendPending();
			} else {
				Unexpected("Not ready call.");
			}
		}, _lifetime);
	});
}

bool Messages::ready() const {
	return _real && (!_call->conference() || _call->e2eEncryptDecrypt());
}

void Messages::send(TextWithTags text) {
	if (!ready()) {
		_pending.push_back(std::move(text));
		return;
	}

	auto prepared = TextWithEntities{
		text.text,
		TextUtilities::ConvertTextTagsToEntities(text.tags)
	};
	auto serialized = MTPTextWithEntities(MTP_textWithEntities(
		MTP_string(prepared.text),
		Api::EntitiesToMTP(
			&_real->session(),
			prepared.entities,
			Api::ConvertOption::SkipLocal)));

	const auto randomId = base::RandomValue<uint64>();
	const auto from = _call->joinAs();
	_messages.push_back({
		.randomId = randomId,
		.peer = from,
		.text = std::move(prepared),
	});

	if (!_call->conference()) {
		_api->request(MTPphone_SendGroupCallMessage(
			_call->inputCall(),
			MTP_long(randomId),
			serialized
		)).done([=](const MTPBool &, const MTP::Response &response) {
			sent(randomId, response);
		}).fail([=](const MTP::Error &, const MTP::Response &response) {
			failed(randomId, response);
		}).send();
	} else {
		const auto bytes = SerializeMessage({ randomId, serialized });
		auto v = std::vector<std::uint8_t>(bytes.size());
		bytes::copy(bytes::make_span(v), bytes::make_span(bytes));

		const auto userId = peerToUser(from->id).bare;
		const auto encrypt = _call->e2eEncryptDecrypt();
		const auto encrypted = encrypt(v, int64_t(userId), true, 0);

		_api->request(MTPphone_SendGroupCallEncryptedMessage(
			_call->inputCall(),
			MTP_bytes(bytes::make_span(encrypted))
		)).done([=](const MTPBool &, const MTP::Response &response) {
			sent(randomId, response);
		}).fail([=](const MTP::Error &, const MTP::Response &response) {
			failed(randomId, response);
		}).send();
	}
	checkDestroying(true);
}

void Messages::received(const MTPDupdateGroupCallMessage &data) {
	if (!ready()) {
		return;
	}
	received(data.vrandom_id().v, data.vfrom_id(), data.vmessage());
	pushChanges();
}

void Messages::received(const MTPDupdateGroupCallEncryptedMessage &data) {
	if (!ready()) {
		return;
	}
	const auto fromId = data.vfrom_id();
	const auto &bytes = data.vencrypted_message().v;
	auto v = std::vector<std::uint8_t>(bytes.size());
	bytes::copy(bytes::make_span(v), bytes::make_span(bytes));

	const auto userId = peerToUser(peerFromMTP(fromId)).bare;
	const auto decrypt = _call->e2eEncryptDecrypt();
	const auto decrypted = decrypt(v, int64_t(userId), false, 0);

	const auto deserialized = DeserializeMessage(QByteArray::fromRawData(
		reinterpret_cast<const char*>(decrypted.data()),
		decrypted.size()));
	if (!deserialized) {
		LOG(("API Error: Can't parse decrypted message"));
		return;
	}
	received(deserialized->randomId, fromId, deserialized->message, true);
	pushChanges();
}

void Messages::received(
		uint64 randomId,
		const MTPPeer &from,
		const MTPTextWithEntities &message,
		bool checkCustomEmoji) {
	const auto peer = _call->peer();
	const auto i = ranges::find(_messages, randomId, &Message::randomId);
	if (i != end(_messages)) {
		if (peerFromMTP(from) == peer->session().userPeerId() && !i->date) {
			i->date = base::unixtime::now();
			checkDestroying(true);
		}
		return;
	}
	auto allowedEntityTypes = std::vector<EntityType>{
		EntityType::Code,
		EntityType::Bold,
		EntityType::Semibold,
		EntityType::Spoiler,
		EntityType::StrikeOut,
		EntityType::Underline,
		EntityType::Italic,
		EntityType::CustomEmoji,
	};
	if (checkCustomEmoji && !peer->isSelf() && !peer->isPremium()) {
		allowedEntityTypes.pop_back();
	}
	_messages.push_back({
		.randomId = randomId,
		.date = base::unixtime::now(),
		.peer = peer->owner().peer(peerFromMTP(from)),
		.text = Ui::Text::Filtered(
			Api::ParseTextWithEntities(&peer->session(), message),
			allowedEntityTypes),
	});
	checkDestroying(true);
}

void Messages::checkDestroying(bool afterChanges) {
	auto next = TimeId();
	const auto now = base::unixtime::now();
	const auto destroyTime = now - _ttl;
	const auto initial = _messages.size();
	for (auto i = begin(_messages); i != end(_messages);) {
		const auto date = i->date;
		if (!date) {
			++i;
		} else if (date <= destroyTime) {
			i = _messages.erase(i);
		} else if (!next) {
			next = date + _ttl - now;
			++i;
		} else {
			++i;
		}
	}
	if (!next) {
		_destroyTimer.cancel();
	} else {
		const auto delay = next * crl::time(1000);
		if (!_destroyTimer.isActive()
			|| (_destroyTimer.remainingTime() > delay)) {
			_destroyTimer.callOnce(delay);
		}
	}
	if (afterChanges || (_messages.size() < initial)) {
		pushChanges();
	}
}

rpl::producer<std::vector<Message>> Messages::listValue() const {
	return _changes.events_starting_with_copy(_messages);
}

void Messages::sendPending() {
	Expects(_real != nullptr);

	for (auto &pending : base::take(_pending)) {
		send(std::move(pending));
	}
}

void Messages::pushChanges() {
	_changes.fire_copy(_messages);
}

void Messages::sent(uint64 randomId, const MTP::Response &response) {
	const auto i = ranges::find(_messages, randomId, &Message::randomId);
	if (i != end(_messages) && !i->date) {
		i->date = Api::UnixtimeFromMsgId(response.outerMsgId);
		checkDestroying(true);
	}
}

void Messages::failed(uint64 randomId, const MTP::Response &response) {
	const auto i = ranges::find(_messages, randomId, &Message::randomId);
	if (i != end(_messages) && !i->date) {
		i->date = Api::UnixtimeFromMsgId(response.outerMsgId);
		i->failed = true;
		checkDestroying(true);
	}
}

} // namespace Calls::Group
