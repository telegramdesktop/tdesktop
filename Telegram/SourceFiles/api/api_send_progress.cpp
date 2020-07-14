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
	MTPsendMessageAction action;
	switch (type) {
	case Type::Typing: action = MTP_sendMessageTypingAction(); break;
	case Type::RecordVideo: action = MTP_sendMessageRecordVideoAction(); break;
	case Type::UploadVideo: action = MTP_sendMessageUploadVideoAction(MTP_int(progress)); break;
	case Type::RecordVoice: action = MTP_sendMessageRecordAudioAction(); break;
	case Type::UploadVoice: action = MTP_sendMessageUploadAudioAction(MTP_int(progress)); break;
	case Type::RecordRound: action = MTP_sendMessageRecordRoundAction(); break;
	case Type::UploadRound: action = MTP_sendMessageUploadRoundAction(MTP_int(progress)); break;
	case Type::UploadPhoto: action = MTP_sendMessageUploadPhotoAction(MTP_int(progress)); break;
	case Type::UploadFile: action = MTP_sendMessageUploadDocumentAction(MTP_int(progress)); break;
	case Type::ChooseLocation: action = MTP_sendMessageGeoLocationAction(); break;
	case Type::ChooseContact: action = MTP_sendMessageChooseContactAction(); break;
	case Type::PlayGame: action = MTP_sendMessageGamePlayAction(); break;
	}
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
