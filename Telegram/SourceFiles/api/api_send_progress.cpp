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
#include "apiwrap.h"

namespace Api {
namespace {

constexpr auto kCancelTypingActionTimeout = crl::time(5000);

} // namespace

SendProgressManager::SendProgressManager(not_null<Main::Session*> session)
: _session(session)
, _stopTypingTimer([=] { cancelTyping(base::take(_stopTypingHistory)); }) {
}

void SendProgressManager::cancel(
		not_null<History*> history,
		SendProgressType type) {
	const auto i = _requests.find({ history, type });
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
		int32 progress) {
	const auto peer = history->peer;
	if (peer->isSelf() || (peer->isChannel() && !peer->isMegagroup())) {
		return;
	}

	const auto doing = (progress >= 0);
	if (history->mySendActionUpdated(type, doing)) {
		cancel(history, type);
		if (doing) {
			send(history, type, progress);
		}
	}
}

void SendProgressManager::send(
		not_null<History*> history,
		SendProgressType type,
		int32 progress) {
	using Type = SendProgressType;
	const auto action = [&]() -> MTPsendMessageAction {
		const auto p = MTP_int(progress);
		switch (type) {
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
		history->peer->input,
		action
	)).done([=](const MTPBool &result, mtpRequestId requestId) {
		done(result, requestId);
	}).send();
	_requests.emplace(Key{ history, type }, requestId);

	if (type == Type::Typing) {
		_stopTypingHistory = history;
		_stopTypingTimer.callOnce(kCancelTypingActionTimeout);
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
