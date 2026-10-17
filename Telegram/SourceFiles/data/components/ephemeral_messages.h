/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "data/data_drafts.h"
#include "data/data_file_origin.h"
#include "history/history_item_edition.h"

class History;
class HistoryItem;
class PeerData;
class UserData;

namespace Api {
struct MessageToSend;
struct SendAction;
} // namespace Api

namespace Main {
class Session;
} // namespace Main

namespace Data {

[[nodiscard]] PeerId PeerIdFromEphemeral(const MTPDephemeralMessage &data);

class EphemeralMessages final {
public:
	explicit EphemeralMessages(not_null<Main::Session*> session);
	EphemeralMessages(const EphemeralMessages &other) = delete;
	EphemeralMessages &operator=(const EphemeralMessages &other) = delete;
	~EphemeralMessages();

	void clear();

	void apply(const MTPDupdateNewEphemeralMessage &update);
	void apply(const MTPDupdateEditEphemeralMessage &update);
	void apply(const MTPDupdateDeleteEphemeralMessages &update);

	[[nodiscard]] HistoryItem *lookupItem(
		not_null<PeerData*> peer,
		int32 ephemeralId) const;
	[[nodiscard]] int32 lookupId(not_null<const HistoryItem*> item) const;
	[[nodiscard]] UserId receiverId(not_null<const HistoryItem*> item) const;
	[[nodiscard]] UserData *replyReceiver(
		not_null<const HistoryItem*> item) const;
	[[nodiscard]] UserData *replyBot(
		not_null<const HistoryItem*> target) const;

	[[nodiscard]] bool wouldSend(const Api::MessageToSend &message) const;
	[[nodiscard]] bool hasEphemeralCommand(
		not_null<PeerData*> peer,
		const QString &text) const;
	[[nodiscard]] bool wouldSendMedia(
		not_null<PeerData*> peer,
		FullReplyTo replyTo,
		const QString &caption) const;
	[[nodiscard]] bool isEphemeralBotReply(FullMsgId replyToId) const;
	[[nodiscard]] bool trySend(const Api::MessageToSend &message);
	void send(
		not_null<History*> history,
		not_null<UserData*> bot,
		TextWithEntities text,
		int32 replyToEphemeralId = 0,
		MsgId topicRootId = 0,
		FullReplyTo realReply = {},
		Data::WebPageDraft webPage = {},
		bool invertCaption = false);
	bool sendMedia(
		not_null<HistoryItem*> item,
		const MTPInputMedia &media,
		Data::FileOrigin origin = {},
		Fn<MTPInputMedia()> rebuildMedia = nullptr);
	[[nodiscard]] bool sendRich(
		not_null<HistoryItem*> item,
		const MTPInputRichMessage &richMessage,
		const Api::SendAction &action);
	[[nodiscard]] bool sendSimpleMedia(
		not_null<History*> history,
		FullReplyTo replyTo,
		const MTPInputMedia &media);
	void noteCallbackTopic(
		not_null<History*> history,
		PeerId botId,
		MsgId topicRootId);
	[[nodiscard]] bool anchored(not_null<const HistoryItem*> item) const;
	[[nodiscard]] const Media *anchoredMedia(
		not_null<const HistoryItem*> item) const;
	void revertAnchored(not_null<HistoryItem*> item);
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
	HistoryItem *applyAnchored(
		not_null<History*> history,
		const MTPDephemeralMessage &data,
		MsgId anchorMsgId);
	void registerEntry(
		not_null<History*> history,
		int32 ephemeralId,
		UserId receiverId,
		not_null<HistoryItem*> item);
	void unregisterEntry(not_null<const HistoryItem*> item);
	void recountAttachToPrevious(not_null<HistoryItem*> item);
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
		FullReplyTo realReply = {},
		FullMsgId destroyOnResult = {},
		Data::FileOrigin origin = {},
		Fn<MTPInputMedia()> rebuildMedia = nullptr,
		bool invertMedia = false,
		std::optional<MTPInputRichMessage> richMessage = {},
		Fn<std::optional<MTPInputRichMessage>()> rebuildRich = nullptr);
	[[nodiscard]] bool replyTargetMissing(
		const MTPDephemeralMessage &data) const;
	[[nodiscard]] bool mentionsMe(
		not_null<History*> history,
		const MTPDephemeralMessage &data) const;
	void drainPending(bool force = false);
	[[nodiscard]] const Entry *findByItem(
		not_null<const HistoryItem*> item) const;
	[[nodiscard]] MsgId takeCallbackTopic(
		not_null<History*> history,
		PeerId botId);
	[[nodiscard]] UserData *botForSending(const Entry &entry) const;
	void reportDroppedReply() const;
	void itemRemoved(not_null<const HistoryItem*> item);
	void pruneOld();

	const not_null<Main::Session*> _session;

	base::Timer _pruneTimer;
	base::Timer _pendingTimer;
	base::flat_map<not_null<History*>, List> _data;
	base::flat_map<FullMsgId, HistoryMessageContent> _anchored;
	std::vector<MTPEphemeralMessage> _pending;
	FullMsgId _convertLocalTarget;
	base::flat_map<
		not_null<History*>,
		base::flat_map<PeerId, MsgId>> _callbackTopicHints;

	rpl::lifetime _lifetime;

};

} // namespace Data
