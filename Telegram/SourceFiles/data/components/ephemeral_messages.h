/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

class History;
class HistoryItem;
class PeerData;
class UserData;

namespace Api {
struct MessageToSend;
} // namespace Api

namespace Main {
class Session;
} // namespace Main

namespace Data {

class EphemeralMessages final {
public:
	explicit EphemeralMessages(not_null<Main::Session*> session);
	EphemeralMessages(const EphemeralMessages &other) = delete;
	EphemeralMessages &operator=(const EphemeralMessages &other) = delete;
	~EphemeralMessages();

	void apply(const MTPDupdateNewEphemeralMessage &update);
	void apply(const MTPDupdateEditEphemeralMessage &update);
	void apply(const MTPDupdateDeleteEphemeralMessages &update);

	[[nodiscard]] HistoryItem *lookupItem(
		not_null<PeerData*> peer,
		int32 ephemeralId) const;
	[[nodiscard]] int32 lookupId(not_null<const HistoryItem*> item) const;
	[[nodiscard]] UserData *replyReceiver(
		not_null<const HistoryItem*> item) const;

	[[nodiscard]] bool wouldSend(const Api::MessageToSend &message) const;
	[[nodiscard]] bool isEphemeralBotReply(FullMsgId replyToId) const;
	[[nodiscard]] bool trySend(const Api::MessageToSend &message);
	void send(
		not_null<History*> history,
		not_null<UserData*> bot,
		TextWithEntities text,
		int32 replyToEphemeralId = 0,
		MsgId topicRootId = 0);
	[[nodiscard]] bool sendMedia(
		not_null<HistoryItem*> item,
		const MTPInputMedia &media);
	[[nodiscard]] bool sendSimpleMedia(
		not_null<History*> history,
		FullReplyTo replyTo,
		const MTPInputMedia &media);
	void noteCallbackTopic(
		not_null<History*> history,
		PeerId botId,
		MsgId topicRootId);
	void deleteMessage(not_null<HistoryItem*> item);

private:
	struct Entry {
		int32 ephemeralId = 0;
		UserId receiverId;
		not_null<HistoryItem*> item;
	};
	using List = std::vector<Entry>;

	void applyOrDefer(const MTPEphemeralMessage &message);
	HistoryItem *applyNew(const MTPDephemeralMessage &data);
	[[nodiscard]] UserData *findCommandBot(
		not_null<PeerData*> peer,
		const QString &text) const;
	[[nodiscard]] FullMsgId realReplyId(
		const Api::MessageToSend &message) const;
	void request(
		not_null<History*> history,
		not_null<UserData*> bot,
		TextWithEntities text,
		const MTPInputMedia &media,
		bool hasMedia,
		int32 replyToEphemeralId,
		MsgId topicRootId,
		FullMsgId destroyOnResult = {});
	[[nodiscard]] bool replyTargetMissing(
		const MTPDephemeralMessage &data) const;
	void drainPending(bool force = false);
	[[nodiscard]] const Entry *findByItem(
		not_null<const HistoryItem*> item) const;
	[[nodiscard]] MsgId takeCallbackTopic(
		not_null<History*> history,
		PeerId botId);
	[[nodiscard]] UserData *botForSending(const Entry &entry) const;
	void itemRemoved(not_null<const HistoryItem*> item);
	void pruneOld();

	const not_null<Main::Session*> _session;

	base::Timer _pruneTimer;
	base::Timer _pendingTimer;
	base::flat_map<not_null<History*>, List> _data;
	std::vector<MTPEphemeralMessage> _pending;
	FullMsgId _convertLocalMediaTarget;
	base::flat_map<
		not_null<History*>,
		base::flat_map<PeerId, MsgId>> _callbackTopicHints;

	rpl::lifetime _lifetime;

};

} // namespace Data
