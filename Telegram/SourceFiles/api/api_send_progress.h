/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_common.h"
#include "base/timer.h"

class History;

namespace Main {
class Session;
} // namespace Main

namespace Api {

enum class SendProgressType {
	Typing,
	RecordVideo,
	UploadVideo,
	RecordVoice,
	UploadVoice,
	RecordRound,
	UploadRound,
	UploadPhoto,
	UploadFile,
	ChooseLocation,
	ChooseContact,
	PlayGame,
	Speaking,
};

struct SendProgress {
	SendProgress(
		SendProgressType type,
		crl::time until,
		int progress = 0)
	: type(type)
	, until(until)
	, progress(progress) {
	}
	SendProgressType type = SendProgressType::Typing;
	crl::time until = 0;
	int progress = 0;

};

class SendProgressManager final {
public:
	SendProgressManager(not_null<Main::Session*> session);

	void update(
		not_null<History*> history,
		SendProgressType type,
		int progress = 0);
	void update(
		not_null<History*> history,
		MsgId topMsgId,
		SendProgressType type,
		int progress = 0);
	void cancel(
		not_null<History*> history,
		MsgId topMsgId,
		SendProgressType type);
	void cancel(
		not_null<History*> history,
		SendProgressType type);
	void cancelTyping(not_null<History*> history);

private:
	struct Key {
		not_null<History*> history;
		MsgId topMsgId = 0;
		SendProgressType type = SendProgressType();

		inline bool operator<(const Key &other) const {
			return (history < other.history)
				|| (history == other.history && topMsgId < other.topMsgId)
				|| (history == other.history
					&& topMsgId == other.topMsgId
					&& type < other.type);
		}
	};

	bool updated(const Key &key, bool doing);

	void send(const Key &key, int progress);
	void done(const MTPBool &result, mtpRequestId requestId);

	[[nodiscard]] bool skipRequest(const Key &key) const;

	const not_null<Main::Session*> _session;
	base::flat_map<Key, mtpRequestId> _requests;
	base::flat_map<Key, crl::time> _updated;
	base::Timer _stopTypingTimer;
	History *_stopTypingHistory = nullptr;

};

} // namespace Api
