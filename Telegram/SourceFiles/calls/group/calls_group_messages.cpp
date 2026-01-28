/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_messages.h"

#include "apiwrap.h"
#include "api/api_blocked_peers.h"
#include "api/api_chat_participants.h"
#include "api/api_text_entities.h"
#include "base/random.h"
#include "base/unixtime.h"
#include "calls/group/ui/calls_group_stars_coloring.h"
#include "calls/group/calls_group_call.h"
#include "calls/group/calls_group_message_encryption.h"
#include "data/data_channel.h"
#include "data/data_group_call.h"
#include "data/data_message_reactions.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "mtproto/sender.h"
#include "ui/text/text_utilities.h"
#include "ui/ui_utility.h"

namespace Calls::Group {
namespace {

constexpr auto kMaxShownVideoStreamMessages = 100;
constexpr auto kStarsStatsShortPollDelay = 30 * crl::time(1000);

[[nodiscard]] StarsTop ParseStarsTop(
		not_null<Data::Session*> owner,
		const MTPphone_GroupCallStars &stars) {
	const auto &data = stars.data();
	const auto &list = data.vtop_donors().v;
	auto result = StarsTop{ .total = int(data.vtotal_stars().v) };
	result.topDonors.reserve(list.size());
	for (const auto &entry : list) {
		const auto &fields = entry.data();
		result.topDonors.push_back({
			.peer = (fields.vpeer_id()
				? owner->peer(peerFromMTP(*fields.vpeer_id())).get()
				: nullptr),
			.stars = int(fields.vstars().v),
			.my = fields.is_my(),
		});
	}
	return result;
}

[[nodiscard]] TimeId PinFinishDate(
		not_null<PeerData*> peer,
		TimeId date,
		int stars) {
	if (!date || !stars) {
		return 0;
	}
	const auto &colorings = peer->session().appConfig().groupCallColorings();
	return date + Ui::StarsColoringForCount(colorings, stars).secondsPin;
}

[[nodiscard]] TimeId PinFinishDate(const Message &message) {
	return PinFinishDate(message.peer, message.date, message.stars);
}

} // namespace

Messages::Messages(not_null<GroupCall*> call, not_null<MTP::Sender*> api)
: _call(call)
, _session(&call->peer()->session())
, _api(api)
, _destroyTimer([=] { checkDestroying(); })
, _ttl(_session->appConfig().groupCallMessageTTL())
, _starsStatsTimer([=] { requestStarsStats(); }) {
	Ui::PostponeCall(_call, [=] {
		_call->real(
		) | rpl::on_next([=](not_null<Data::GroupCall*> call) {
			_real = call;
			if (ready()) {
				sendPending();
			} else {
				Unexpected("Not ready call.");
			}
		}, _lifetime);

		requestStarsStats();
	});
}

Messages::~Messages() {
	if (_paid.sending > 0) {
		finishPaidSending({
			.count = int(_paid.sending),
			.valid = true,
			.shownPeer = _paid.sendingShownPeer,
		}, false);
	}
}

void Messages::requestStarsStats() {
	if (!_call->videoStream()) {
		return;
	}
	_starsStatsTimer.cancel();
	_starsTopRequestId = _api->request(MTPphone_GetGroupCallStars(
		_call->inputCall()
	)).done([=](const MTPphone_GroupCallStars &result) {
		const auto &data = result.data();

		const auto owner = &_session->data();
		owner->processUsers(data.vusers());
		owner->processChats(data.vchats());

		_paid.top = ParseStarsTop(owner, result);
		_paidChanges.fire({});

		_starsStatsTimer.callOnce(kStarsStatsShortPollDelay);
	}).fail([=](const MTP::Error &error) {
		[[maybe_unused]] const auto &type = error.type();
		_starsStatsTimer.callOnce(kStarsStatsShortPollDelay);
	}).send();

}

bool Messages::ready() const {
	return _real && (!_call->conference() || _call->e2eEncryptDecrypt());
}

void Messages::send(TextWithTags text, int stars) {
	if (text.empty() && !stars) {
		return;
	} else if (!ready()) {
		_pending.push_back({ std::move(text), stars });
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

	const auto localId = _call->peer()->owner().nextLocalMessageId();
	const auto randomId = base::RandomValue<uint64>();
	_sendingIdByRandomId.emplace(randomId, localId);

	const auto from = _call->messagesFrom();
	const auto creator = _real->creator();
	const auto skip = skipMessage(prepared, stars);
	if (skip) {
		_skippedIds.emplace(localId);
	} else {
		_messages.push_back({
			.id = localId,
			.peer = from,
			.text = std::move(prepared),
			.stars = stars,
			.admin = (from == _call->peer()) || (creator && from->isSelf()),
			.mine = true,
		});
	}
	if (!_call->conference()) {
		using Flag = MTPphone_SendGroupCallMessage::Flag;
		_api->request(MTPphone_SendGroupCallMessage(
			MTP_flags(Flag::f_send_as
				| (stars ? Flag::f_allow_paid_stars : Flag())),
			_call->inputCall(),
			MTP_long(randomId),
			serialized,
			MTP_long(stars),
			from->input()
		)).done([=](
				const MTPUpdates &result,
				const MTP::Response &response) {
			_session->api().applyUpdates(result, randomId);
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

	addStars(from, stars, true);
	if (!skip) {
		checkDestroying(true);
	}
}

void Messages::setApplyingInitial(bool value) {
	_applyingInitial = value;
}

void Messages::received(const MTPDupdateGroupCallMessage &data) {
	if (!ready()) {
		return;
	}
	const auto &fields = data.vmessage().data();
	received(
		fields.vid().v,
		fields.vfrom_id(),
		fields.vmessage(),
		fields.vdate().v,
		fields.vpaid_message_stars().value_or_empty(),
		fields.is_from_admin());
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
	const auto realId = ++_conferenceIdAutoIncrement;
	const auto randomId = deserialized->randomId;
	if (!_conferenceIdByRandomId.emplace(randomId, realId).second) {
		// Already received.
		return;
	}
	received(
		realId,
		fromId,
		deserialized->message,
		base::unixtime::now(), // date
		0, // stars
		false,
		true); // checkCustomEmoji
}

void Messages::deleted(const MTPDupdateDeleteGroupCallMessages &data) {
	const auto was = _messages.size();
	for (const auto &id : data.vmessages().v) {
		const auto i = ranges::find(_messages, id.v, &Message::id);
		if (i != end(_messages)) {
			_messages.erase(i);
		}
	}
	if (_messages.size() < was) {
		pushChanges();
	}
}

void Messages::sent(const MTPDupdateMessageID &data) {
	sent(data.vrandom_id().v, data.vid().v);
}

void Messages::sent(uint64 randomId, const MTP::Response &response) {
	const auto realId = ++_conferenceIdAutoIncrement;
	_conferenceIdByRandomId.emplace(randomId, realId);
	sent(randomId, realId);

	const auto i = ranges::find(_messages, realId, &Message::id);
	if (i != end(_messages) && !i->date) {
		i->date = Api::UnixtimeFromMsgId(response.outerMsgId);
		i->pinFinishDate = PinFinishDate(*i);
		checkDestroying(true);
	}
}

void Messages::sent(uint64 randomId, MsgId realId) {
	const auto i = _sendingIdByRandomId.find(randomId);
	if (i == end(_sendingIdByRandomId)) {
		return;
	}
	const auto localId = i->second;
	_sendingIdByRandomId.erase(i);

	const auto j = ranges::find(_messages, localId, &Message::id);
	if (j == end(_messages)) {
		_skippedIds.emplace(realId);
		return;
	}
	j->id = realId;
	crl::on_main(this, [=] {
		const auto i = ranges::find(_messages, realId, &Message::id);
		if (i != end(_messages) && !i->date) {
			i->date = base::unixtime::now();
			i->pinFinishDate = PinFinishDate(*i);
			checkDestroying(true);
		}
	});
	_idUpdates.fire({ .localId = localId, .realId = realId });
}

void Messages::received(
		MsgId id,
		const MTPPeer &from,
		const MTPTextWithEntities &message,
		TimeId date,
		int stars,
		bool fromAdmin,
		bool checkCustomEmoji) {
	const auto peer = _call->peer();
	const auto i = ranges::find(_messages, id, &Message::id);
	if (i != end(_messages)) {
		const auto fromId = peerFromMTP(from);
		const auto me1 = peer->session().userPeerId();
		const auto me2 = _call->messagesFrom()->id;
		if (((fromId == me1) || (fromId == me2)) && !i->date) {
			i->date = date;
			i->pinFinishDate = PinFinishDate(*i);
			checkDestroying(true);
		}
		return;
	} else if (_skippedIds.contains(id)) {
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
	const auto author = peer->owner().peer(peerFromMTP(from));

	auto text = Ui::Text::Filtered(
		Api::ParseTextWithEntities(&author->session(), message),
		allowedEntityTypes);
	const auto mine = author->isSelf()
		|| (author->isChannel() && author->asChannel()->amCreator());
	const auto skip = skipMessage(text, stars);
	if (skip) {
		_skippedIds.emplace(id);
	} else {
		// Should check by sendAsPeers() list instead, but it may not be
		// loaded here yet.
		_messages.push_back({
			.id = id,
			.date = date,
			.pinFinishDate = PinFinishDate(author, date, stars),
			.peer = author,
			.text = std::move(text),
			.stars = stars,
			.admin = fromAdmin,
			.mine = mine,
		});
		ranges::sort(_messages, ranges::less(), &Message::id);
	}
	if (!_applyingInitial) {
		addStars(author, stars, mine);
	}
	if (!skip) {
		checkDestroying(true);
	}
}

bool Messages::skipMessage(const TextWithEntities &text, int stars) const {
	const auto real = _call->lookupReal();
	return text.empty()
		&& real
		&& (stars < real->messagesMinPrice());
}

void Messages::checkDestroying(bool afterChanges) {
	auto next = TimeId();
	const auto now = base::unixtime::now();
	const auto initial = int(_messages.size());
	if (_call->videoStream()) {
		if (initial > kMaxShownVideoStreamMessages) {
			const auto remove = initial - kMaxShownVideoStreamMessages;
			auto i = begin(_messages);
			for (auto k = 0; k != remove; ++k) {
				if (i->date && i->pinFinishDate <= now) {
					i = _messages.erase(i);
				} else if (!next || next > i->pinFinishDate - now) {
					next = i->pinFinishDate - now;
					++i;
				} else {
					++i;
				}
			}
		}
	} else for (auto i = begin(_messages); i != end(_messages);) {
		const auto date = i->date;
		//const auto ttl = i->stars
		//	? (Ui::StarsColoringForCount(i->stars).minutesPin * 60)
		//	: _ttl;
		const auto ttl = _ttl;
		if (!date) {
			if (i->id < 0) {
				++i;
			} else {
				i = _messages.erase(i);
			}
		} else if (date + ttl <= now) {
			i = _messages.erase(i);
		} else if (!next || next > date + ttl - now) {
			next = date + ttl - now;
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

rpl::producer<MessageIdUpdate> Messages::idUpdates() const {
	return _idUpdates.events();
}

void Messages::sendPending() {
	Expects(_real != nullptr);

	for (auto &pending : base::take(_pending)) {
		send(std::move(pending.text), pending.stars);
	}
	if (_paidSendingPending) {
		reactionsPaidSend();
	}
}

void Messages::pushChanges() {
	if (_changesScheduled) {
		return;
	}
	_changesScheduled = true;
	Ui::PostponeCall(this, [=] {
		_changesScheduled = false;
		_changes.fire_copy(_messages);
	});
}

void Messages::failed(uint64 randomId, const MTP::Response &response) {
	const auto i = _sendingIdByRandomId.find(randomId);
	if (i == end(_sendingIdByRandomId)) {
		return;
	}
	const auto localId = i->second;
	_sendingIdByRandomId.erase(i);

	const auto j = ranges::find(_messages, localId, &Message::id);
	if (j != end(_messages) && !j->date) {
		j->date = Api::UnixtimeFromMsgId(response.outerMsgId);
		j->stars = 0;
		j->failed = true;
		checkDestroying(true);
	}
}

int Messages::reactionsPaidScheduled() const {
	return _paid.scheduled;
}

PeerId Messages::reactionsLocalShownPeer() const {
	const auto minePaidShownPeer = [&] {
		for (const auto &entry : _paid.top.topDonors) {
			if (entry.my) {
				return entry.peer ? entry.peer->id : PeerId();
			}
		}
		return _call->messagesFrom()->id;
		//const auto api = &_session->api();
		//return api->globalPrivacy().paidReactionShownPeerCurrent();
	};
	return _paid.scheduledFlag
		? _paid.scheduledShownPeer
		: _paid.sendingFlag
		? _paid.sendingShownPeer
		: minePaidShownPeer();
}

void Messages::reactionsPaidAdd(int count) {
	Expects(count >= 0);

	_paid.scheduled += count;
	_paid.scheduledFlag = 1;
	_paid.scheduledShownPeer = _call->messagesFrom()->id;
	if (count > 0) {
		_session->credits().lock(CreditsAmount(count));
	}
	_call->peer()->owner().reactions().schedulePaid(_call);
	_paidChanges.fire({});
}

void Messages::reactionsPaidScheduledCancel() {
	if (!_paid.scheduledFlag) {
		return;
	}
	if (const auto amount = int(_paid.scheduled)) {
		_session->credits().unlock(
			CreditsAmount(amount));
	}
	_paid.scheduled = 0;
	_paid.scheduledFlag = 0;
	_paid.scheduledShownPeer = 0;
	_paidChanges.fire({});
}

Data::PaidReactionSend Messages::startPaidReactionSending() {
	_paidSendingPending = false;
	if (!_paid.scheduledFlag || !_paid.scheduled) {
		return {};
	} else if (_paid.sendingFlag || !ready()) {
		_paidSendingPending = true;
		return {};
	}
	_paid.sending = _paid.scheduled;
	_paid.sendingFlag = _paid.scheduledFlag;
	_paid.sendingShownPeer = _paid.scheduledShownPeer;
	_paid.scheduled = 0;
	_paid.scheduledFlag = 0;
	_paid.scheduledShownPeer = 0;
	return {
		.count = int(_paid.sending),
		.valid = true,
		.shownPeer = _paid.sendingShownPeer,
	};
}

void Messages::finishPaidSending(
		Data::PaidReactionSend send,
		bool success) {
	Expects(send.count == _paid.sending);
	Expects(send.valid == (_paid.sendingFlag == 1));
	Expects(send.shownPeer == _paid.sendingShownPeer);

	_paid.sending = 0;
	_paid.sendingFlag = 0;
	_paid.sendingShownPeer = 0;
	if (const auto amount = send.count) {
		if (success) {
			const auto from = _session->data().peer(*send.shownPeer);
			_session->credits().withdrawLocked(CreditsAmount(amount));

			auto &donors = _paid.top.topDonors;
			const auto i = ranges::find(donors, true, &StarsDonor::my);
			if (i != end(donors)) {
				i->peer = from;
				i->stars += amount;
			} else {
				donors.push_back({
					.peer = from,
					.stars = amount,
					.my = true,
				});
			}
		} else {
			_session->credits().unlock(CreditsAmount(amount));
			_paidChanges.fire({});
		}
	}
	if (_paidSendingPending) {
		reactionsPaidSend();
	}
}

void Messages::reactionsPaidSend() {
	const auto send = startPaidReactionSending();
	if (!send.valid || !send.count) {
		return;
	}

	const auto localId = _call->peer()->owner().nextLocalMessageId();
	const auto randomId = base::RandomValue<uint64>();
	_sendingIdByRandomId.emplace(randomId, localId);

	const auto from = _session->data().peer(*send.shownPeer);
	const auto stars = int(send.count);
	const auto skip = skipMessage({}, stars);
	if (skip) {
		_skippedIds.emplace(localId);
	} else {
		_messages.push_back({
			.id = localId,
			.peer = from,
			.stars = stars,
			.admin = (from == _call->peer()),
			.mine = true,
		});
	}
	using Flag = MTPphone_SendGroupCallMessage::Flag;
	_api->request(MTPphone_SendGroupCallMessage(
		MTP_flags(Flag::f_send_as | Flag::f_allow_paid_stars),
		_call->inputCall(),
		MTP_long(randomId),
		MTP_textWithEntities(MTP_string(), MTP_vector<MTPMessageEntity>()),
		MTP_long(stars),
		from->input()
	)).done([=](
			const MTPUpdates &result,
			const MTP::Response &response) {
		finishPaidSending(send, true);
		_session->api().applyUpdates(result, randomId);
	}).fail([=](const MTP::Error &, const MTP::Response &response) {
		finishPaidSending(send, false);
		failed(randomId, response);
	}).send();

	addStars(from, stars, true);
	if (!skip) {
		checkDestroying(true);
	}
}

void Messages::undoScheduledPaidOnDestroy() {
	_call->peer()->owner().reactions().undoScheduledPaid(_call);
}

Messages::PaidLocalState Messages::starsLocalState() const {
	const auto &donors = _paid.top.topDonors;
	const auto i = ranges::find(donors, true, &StarsDonor::my);
	const auto local = int(_paid.scheduled);
	const auto my = (i != end(donors) ? i->stars : 0) + local;
	const auto total = _paid.top.total + local;
	return { .total = total, .my = my };
}

void Messages::deleteConfirmed(MessageDeleteRequest request) {
	const auto eraseFrom = [&](auto iterator) {
		if (iterator != end(_messages)) {
			_messages.erase(iterator, end(_messages));
			pushChanges();
		}
	};
	const auto peer = _call->peer();
	if (const auto from = request.deleteAllFrom) {
		using Flag = MTPphone_DeleteGroupCallParticipantMessages::Flag;
		_api->request(MTPphone_DeleteGroupCallParticipantMessages(
			MTP_flags(request.reportSpam ? Flag::f_report_spam : Flag()),
			_call->inputCall(),
			from->input()
		)).send();
		eraseFrom(ranges::remove(_messages, not_null(from), &Message::peer));
	} else {
		using Flag = MTPphone_DeleteGroupCallMessages::Flag;
		_api->request(MTPphone_DeleteGroupCallMessages(
			MTP_flags(request.reportSpam ? Flag::f_report_spam : Flag()),
			_call->inputCall(),
			MTP_vector<MTPint>(1, MTP_int(request.id.bare))
		)).send();
		eraseFrom(ranges::remove(_messages, request.id, &Message::id));
	}
	if (const auto ban = request.ban) {
		if (const auto channel = peer->asChannel()) {
			ban->session().api().chatParticipants().kick(
				channel,
				ban,
				ChatRestrictionsInfo());
		} else {
			ban->session().api().blockedPeers().block(ban);
		}
	}
}

void Messages::addStars(not_null<PeerData*> from, int stars, bool mine) {
	if (stars <= 0) {
		return;
	}
	_paid.top.total += stars;
	const auto i = ranges::find(
		_paid.top.topDonors,
		from.get(),
		&StarsDonor::peer);
	if (i != end(_paid.top.topDonors)) {
		i->stars += stars;
	} else {
		_paid.top.topDonors.push_back({
			.peer = from,
			.stars = stars,
			.my = mine,
		});
	}
	ranges::stable_sort(
		_paid.top.topDonors,
		ranges::greater(),
		&StarsDonor::stars);
	_paidChanges.fire({ .peer = from, .stars = stars });
}

} // namespace Calls::Group
