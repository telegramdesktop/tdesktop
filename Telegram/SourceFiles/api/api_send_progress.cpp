/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_send_progress.h"

#include "main/main_session.h"
#include "history/history.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "base/unixtime.h"
#include "data/data_peer_values.h"
#include "apiwrap.h"

namespace Api {
namespace {

constexpr auto kCancelTypingActionTimeout = crl::time(5000);
constexpr auto kSetMyActionForMs = 10 * crl::time(1000);
constexpr auto kSendTypingsToOfflineFor = TimeId(30);

} // namespace

SendProgressManager::SendProgressManager(not_null<Main::Session*> session)
: _session(session)
, _stopTypingTimer([=] { cancelTyping(base::take(_stopTypingHistory)); }) {
}

void SendProgressManager::cancel(
		not_null<History*> history,
		SendProgressType type) {
	cancel(history, 0, type);
}

void SendProgressManager::cancel(
		not_null<History*> history,
		MsgId topMsgId,
		SendProgressType type) {
	const auto i = _requests.find(Key{ history, topMsgId, type });
	if (i != _requests.end()) {
		_session->api().request(i->second).cancel();
		_requests.erase(i);
	}
}

void SendProgressManager::cancelTyping(not_null<History*> history) {
	_stopTypingTimer.cancel();
	cancel(history, SendProgressType::Typing);
}

void SendProgressManager::update(
		not_null<History*> history,
		SendProgressType type,
		int progress) {
	update(history, 0, type, progress);
}

void SendProgressManager::update(
		not_null<History*> history,
		MsgId topMsgId,
		SendProgressType type,
		int progress) {
	const auto peer = history->peer;
	if (peer->isSelf() || (peer->isChannel() && !peer->isMegagroup())) {
		return;
	}

	const auto doing = (progress >= 0);
	const auto key = Key{ history, topMsgId, type };
	if (updated(key, doing)) {
		cancel(history, topMsgId, type);
		if (doing) {
			send(key, progress);
		}
	}
}

bool SendProgressManager::updated(const Key &key, bool doing) {
	const auto now = crl::now();
	const auto i = _updated.find(key);
	if (doing) {
		if (i == end(_updated)) {
			_updated.emplace(key, now + kSetMyActionForMs);
		} else if (i->second > now + (kSetMyActionForMs / 2)) {
			return false;
		} else {
			i->second = now + kSetMyActionForMs;
		}
	} else {
		if (i == end(_updated)) {
			return false;
		} else if (i->second <= now) {
			return false;
		} else {
			_updated.erase(i);
		}
	}
	return true;
}

void SendProgressManager::send(const Key &key, int progress) {
	if (skipRequest(key)) {
		return;
	}
	using Type = SendProgressType;
	const auto action = [&]() -> MTPsendMessageAction {
		const auto p = MTP_int(progress);
		switch (key.type) {
		case Type::Typing: return MTP_sendMessageTypingAction();
		case Type::RecordVideo: return MTP_sendMessageRecordVideoAction();
		case Type::UploadVideo: return MTP_sendMessageUploadVideoAction(p);
		case Type::RecordVoice: return MTP_sendMessageRecordAudioAction();
		case Type::UploadVoice: return MTP_sendMessageUploadAudioAction(p);
		case Type::RecordRound: return MTP_sendMessageRecordRoundAction();
		case Type::UploadRound: return MTP_sendMessageUploadRoundAction(p);
		case Type::UploadPhoto: return MTP_sendMessageUploadPhotoAction(p);
		case Type::UploadFile: return MTP_sendMessageUploadDocumentAction(p);
		case Type::ChooseLocation: return MTP_sendMessageGeoLocationAction();
		case Type::ChooseContact: return MTP_sendMessageChooseContactAction();
		case Type::PlayGame: return MTP_sendMessageGamePlayAction();
		default: return MTP_sendMessageTypingAction();
		}
	}();
	const auto requestId = _session->api().request(MTPmessages_SetTyping(
		MTP_flags(key.topMsgId
			? MTPmessages_SetTyping::Flag::f_top_msg_id
			: MTPmessages_SetTyping::Flag(0)),
		key.history->peer->input,
		MTP_int(key.topMsgId),
		action
	)).done([=](const MTPBool &result, mtpRequestId requestId) {
		done(result, requestId);
	}).send();
	_requests.emplace(key, requestId);

	if (key.type == Type::Typing) {
		_stopTypingHistory = key.history;
		_stopTypingTimer.callOnce(kCancelTypingActionTimeout);
	}
}

bool SendProgressManager::skipRequest(const Key &key) const {
	const auto user = key.history->peer->asUser();
	if (!user) {
		return false;
	} else if (user->isSelf()) {
		return true;
	} else if (user->isBot() && !user->isSupport()) {
		return true;
	}
	const auto recently = base::unixtime::now() - kSendTypingsToOfflineFor;
	const auto online = user->onlineTill;
	if (online == -2) { // last seen recently
		return false;
	} else if (online < 0) {
		return (-online < recently);
	} else {
		return (online < recently);
	}
}

void SendProgressManager::done(
		const MTPBool &result,
		mtpRequestId requestId) {
	for (auto i = _requests.begin(), e = _requests.end(); i != e; ++i) {
		if (i->second == requestId) {
			_requests.erase(i);
			break;
		}
	}
}

} // namespace Api
