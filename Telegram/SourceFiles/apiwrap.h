/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_common.h"
#include "base/timer.h"
#include "mtproto/sender.h"
#include "data/stickers/data_stickers_set.h"
#include "data/data_messages.h"

class TaskQueue;
struct MessageGroupId;
struct SendingAlbum;
enum class SendMediaType;
struct FileLoadTo;
struct ChatRestrictionsInfo;

namespace Main {
class Session;
} // namespace Main

namespace Data {
struct UpdatedFileReferences;
class WallPaper;
struct ResolvedForwardDraft;
enum class DefaultNotify;
enum class StickersType : uchar;
class Forum;
class ForumTopic;
class Thread;
class Story;
class SavedMessages;
} // namespace Data

namespace InlineBots {
class Result;
} // namespace InlineBots

namespace Storage {
enum class SharedMediaType : signed char;
class DownloadMtprotoTask;
class Account;
} // namespace Storage

namespace Dialogs {
class Key;
} // namespace Dialogs

namespace Ui {
struct PreparedList;
} // namespace Ui

namespace Api {

struct SearchResult;

class Updates;
class Authorizations;
class AttachedStickers;
class BlockedPeers;
class CloudPassword;
class SelfDestruct;
class SensitiveContent;
class GlobalPrivacy;
class UserPrivacy;
class InviteLinks;
class ViewsManager;
class ConfirmPhone;
class PeerPhoto;
class PeerColors;
class Polls;
class ChatParticipants;
class UnreadThings;
class Ringtones;
class Transcribes;
class Premium;
class Usernames;
class Websites;

namespace details {

inline QString ToString(const QString &value) {
	return value;
}

inline QString ToString(int32 value) {
	return QString::number(value);
}

inline QString ToString(uint64 value) {
	return QString::number(value);
}

template <uchar Shift>
inline QString ToString(ChatIdType<Shift> value) {
	return QString::number(value.bare);
}

inline QString ToString(PeerId value) {
	return QString::number(value.value);
}

} // namespace details

template <
	typename ...Types,
	typename = std::enable_if_t<(sizeof...(Types) > 0)>>
QString RequestKey(Types &&...values) {
	const auto strings = { details::ToString(values)... };
	if (strings.size() == 1) {
		return *strings.begin();
	}

	auto result = QString();
	result.reserve(
		ranges::accumulate(strings, 0, ranges::plus(), &QString::size));
	for (const auto &string : strings) {
		result.append(string);
	}
	return result;
}

} // namespace Api

class ApiWrap final : public MTP::Sender {
public:
	using SendAction = Api::SendAction;
	using MessageToSend = Api::MessageToSend;

	explicit ApiWrap(not_null<Main::Session*> session);
	~ApiWrap();

	[[nodiscard]] Main::Session &session() const;
	[[nodiscard]] Storage::Account &local() const;
	[[nodiscard]] Api::Updates &updates() const;

	void applyUpdates(
		const MTPUpdates &updates,
		uint64 sentMessageRandomId = 0) const;
	int applyAffectedHistory(
		PeerData *peer, // May be nullptr, like for deletePhoneCallHistory.
		const MTPmessages_AffectedHistory &result) const;

	void registerModifyRequest(const QString &key, mtpRequestId requestId);
	void clearModifyRequest(const QString &key);

	void saveCurrentDraftToCloud();

	void savePinnedOrder(Data::Folder *folder);
	void savePinnedOrder(not_null<Data::Forum*> forum);
	void savePinnedOrder(not_null<Data::SavedMessages*> saved);
	void toggleHistoryArchived(
		not_null<History*> history,
		bool archived,
		Fn<void()> callback);

	void requestMessageData(PeerData *peer, MsgId msgId, Fn<void()> done);
	QString exportDirectMessageLink(
		not_null<HistoryItem*> item,
		bool inRepliesContext);
	QString exportDirectStoryLink(not_null<Data::Story*> item);

	void requestContacts();
	void requestDialogs(Data::Folder *folder = nullptr);
	void requestPinnedDialogs(Data::Folder *folder = nullptr);
	void requestMoreBlockedByDateDialogs();
	void requestMoreDialogsIfNeeded();
	rpl::producer<bool> dialogsLoadMayBlockByDate() const;
	rpl::producer<bool> dialogsLoadBlockedByDate() const;

	void requestWallPaper(
		const QString &slug,
		Fn<void(const Data::WallPaper &)> done,
		Fn<void()> fail);

	void requestFullPeer(not_null<PeerData*> peer);
	void requestPeerSettings(not_null<PeerData*> peer);

	using UpdatedFileReferences = Data::UpdatedFileReferences;
	using FileReferencesHandler = FnMut<void(const UpdatedFileReferences&)>;
	void refreshFileReference(
		Data::FileOrigin origin,
		FileReferencesHandler &&handler);
	void refreshFileReference(
		Data::FileOrigin origin,
		not_null<Storage::DownloadMtprotoTask*> task,
		int requestId,
		const QByteArray &current);

	void requestChangelog(
		const QString &sinceVersion,
		Fn<void(const MTPUpdates &result)> callback);
	void refreshTopPromotion();
	void requestDeepLinkInfo(
		const QString &path,
		Fn<void(TextWithEntities message, bool updateRequired)> callback);
	void requestTermsUpdate();
	void acceptTerms(bytes::const_span termsId);

	void checkChatInvite(
		const QString &hash,
		FnMut<void(const MTPChatInvite &)> done,
		Fn<void(const MTP::Error &)> fail);
	void checkFilterInvite(
		const QString &slug,
		FnMut<void(const MTPchatlists_ChatlistInvite &)> done,
		Fn<void(const MTP::Error &)> fail);

	void processFullPeer(
		not_null<PeerData*> peer,
		const MTPmessages_ChatFull &result);

	void migrateChat(
		not_null<ChatData*> chat,
		FnMut<void(not_null<ChannelData*>)> done,
		Fn<void(const QString &)> fail = nullptr);

	void markContentsRead(
		const base::flat_set<not_null<HistoryItem*>> &items);
	void markContentsRead(not_null<HistoryItem*> item);

	void deleteAllFromParticipant(
		not_null<ChannelData*> channel,
		not_null<PeerData*> from);

	void requestWebPageDelayed(not_null<WebPageData*> page);
	void clearWebPageRequest(not_null<WebPageData*> page);
	void clearWebPageRequests();

	void scheduleStickerSetRequest(uint64 setId, uint64 access);
	void requestStickerSets();
	void saveStickerSets(
		const Data::StickersSetsOrder &localOrder,
		const Data::StickersSetsOrder &localRemoved,
		Data::StickersType type);
	void updateStickers();
	void updateSavedGifs();
	void updateMasks();
	void updateCustomEmoji();
	void requestRecentStickersForce(bool attached = false);
	void setGroupStickerSet(
		not_null<ChannelData*> megagroup,
		const StickerSetIdentifier &set);
	void setGroupEmojiSet(
		not_null<ChannelData*> megagroup,
		const StickerSetIdentifier &set);
	[[nodiscard]] std::vector<not_null<DocumentData*>> *stickersByEmoji(
		const QString &key);

	void joinChannel(not_null<ChannelData*> channel);
	void leaveChannel(not_null<ChannelData*> channel);

	void requestNotifySettings(const MTPInputNotifyPeer &peer);
	void updateNotifySettingsDelayed(not_null<const Data::Thread*> thread);
	void updateNotifySettingsDelayed(not_null<const PeerData*> peer);
	void updateNotifySettingsDelayed(Data::DefaultNotify type);
	void saveDraftToCloudDelayed(not_null<Data::Thread*> thread);

	void clearHistory(not_null<PeerData*> peer, bool revoke);
	void deleteConversation(not_null<PeerData*> peer, bool revoke);

	bool isQuitPrevent();

	void resolveJumpToDate(
		Dialogs::Key chat,
		const QDate &date,
		Fn<void(not_null<PeerData*>, MsgId)> callback);

	using SliceType = Data::LoadDirection;
	void requestSharedMedia(
		not_null<PeerData*> peer,
		MsgId topicRootId,
		Storage::SharedMediaType type,
		MsgId messageId,
		SliceType slice);

	void readFeaturedSetDelayed(uint64 setId);

	rpl::producer<SendAction> sendActions() const {
		return _sendActions.events();
	}
	void sendAction(const SendAction &action);
	void finishForwarding(const SendAction &action);
	void forwardMessages(
		Data::ResolvedForwardDraft &&draft,
		const SendAction &action,
		FnMut<void()> &&successCallback = nullptr);
	void shareContact(
		const QString &phone,
		const QString &firstName,
		const QString &lastName,
		const SendAction &action,
		Fn<void(bool)> done = nullptr);
	void shareContact(
		not_null<UserData*> user,
		const SendAction &action,
		Fn<void(bool)> done = nullptr);
	void applyAffectedMessages(
		not_null<PeerData*> peer,
		const MTPmessages_AffectedMessages &result);

	void sendVoiceMessage(
		QByteArray result,
		VoiceWaveform waveform,
		crl::time duration,
		const SendAction &action);
	void sendFiles(
		Ui::PreparedList &&list,
		SendMediaType type,
		TextWithTags &&caption,
		std::shared_ptr<SendingAlbum> album,
		const SendAction &action);
	void sendFile(
		const QByteArray &fileContent,
		SendMediaType type,
		const SendAction &action);

	void editMedia(
		Ui::PreparedList &&list,
		SendMediaType type,
		TextWithTags &&caption,
		const SendAction &action);

	void sendUploadedPhoto(
		FullMsgId localId,
		Api::RemoteFileInfo info,
		Api::SendOptions options);
	void sendUploadedDocument(
		FullMsgId localId,
		Api::RemoteFileInfo file,
		Api::SendOptions options);

	void cancelLocalItem(not_null<HistoryItem*> item);

	void sendShortcutMessages(
		not_null<PeerData*> peer,
		BusinessShortcutId id);
	void sendMessage(MessageToSend &&message);
	void sendBotStart(
		not_null<UserData*> bot,
		PeerData *chat = nullptr,
		const QString &startTokenForChat = QString());
	void sendInlineResult(
		not_null<UserData*> bot,
		not_null<InlineBots::Result*> data,
		const SendAction &action,
		std::optional<MsgId> localMessageId);
	void sendMessageFail(
		const MTP::Error &error,
		not_null<PeerData*> peer,
		uint64 randomId = 0,
		FullMsgId itemId = FullMsgId());
	void sendMessageFail(
		const QString &error,
		not_null<PeerData*> peer,
		uint64 randomId = 0,
		FullMsgId itemId = FullMsgId());

	void reloadContactSignupSilent();
	rpl::producer<bool> contactSignupSilent() const;
	std::optional<bool> contactSignupSilentCurrent() const;
	void saveContactSignupSilent(bool silent);

	[[nodiscard]] auto botCommonGroups(not_null<UserData*> bot) const
		-> std::optional<std::vector<not_null<PeerData*>>>;
	void requestBotCommonGroups(not_null<UserData*> bot, Fn<void()> done);

	void saveSelfBio(const QString &text);

	void registerStatsRequest(MTP::DcId dcId, mtpRequestId id);
	void unregisterStatsRequest(MTP::DcId dcId, mtpRequestId id);

	[[nodiscard]] Api::Authorizations &authorizations();
	[[nodiscard]] Api::AttachedStickers &attachedStickers();
	[[nodiscard]] Api::BlockedPeers &blockedPeers();
	[[nodiscard]] Api::CloudPassword &cloudPassword();
	[[nodiscard]] Api::SelfDestruct &selfDestruct();
	[[nodiscard]] Api::SensitiveContent &sensitiveContent();
	[[nodiscard]] Api::GlobalPrivacy &globalPrivacy();
	[[nodiscard]] Api::UserPrivacy &userPrivacy();
	[[nodiscard]] Api::InviteLinks &inviteLinks();
	[[nodiscard]] Api::ViewsManager &views();
	[[nodiscard]] Api::ConfirmPhone &confirmPhone();
	[[nodiscard]] Api::PeerPhoto &peerPhoto();
	[[nodiscard]] Api::Polls &polls();
	[[nodiscard]] Api::ChatParticipants &chatParticipants();
	[[nodiscard]] Api::UnreadThings &unreadThings();
	[[nodiscard]] Api::Ringtones &ringtones();
	[[nodiscard]] Api::Transcribes &transcribes();
	[[nodiscard]] Api::Premium &premium();
	[[nodiscard]] Api::Usernames &usernames();
	[[nodiscard]] Api::Websites &websites();
	[[nodiscard]] Api::PeerColors &peerColors();

	void updatePrivacyLastSeens();

	static constexpr auto kJoinErrorDuration = 5 * crl::time(1000);

private:
	struct MessageDataRequest {
		using Callbacks = std::vector<Fn<void()>>;

		mtpRequestId requestId = 0;
		Callbacks callbacks;
	};
	using MessageDataRequests = base::flat_map<MsgId, MessageDataRequest>;
	using SharedMediaType = Storage::SharedMediaType;

	struct StickersByEmoji {
		std::vector<not_null<DocumentData*>> list;
		uint64 hash = 0;
		crl::time received = 0;
	};

	struct DialogsLoadState {
		TimeId offsetDate = 0;
		MsgId offsetId = 0;
		PeerData *offsetPeer = nullptr;
		mtpRequestId requestId = 0;
		bool listReceived = false;

		mtpRequestId pinnedRequestId = 0;
		bool pinnedReceived = false;
	};

	void setupSupportMode();
	void refreshDialogsLoadBlocked();
	void updateDialogsOffset(
		Data::Folder *folder,
		const QVector<MTPDialog> &dialogs,
		const QVector<MTPMessage> &messages);
	void requestMoreDialogs(Data::Folder *folder);
	DialogsLoadState *dialogsLoadState(Data::Folder *folder);
	void dialogsLoadFinish(Data::Folder *folder);

	void checkQuitPreventFinished();

	void saveDraftsToCloud();

	void resolveMessageDatas();
	void finalizeMessageDataRequest(
		ChannelData *channel,
		mtpRequestId requestId);

	[[nodiscard]] QVector<MTPInputMessage> collectMessageIds(
		const MessageDataRequests &requests);
	[[nodiscard]] MessageDataRequests *messageDataRequests(
		ChannelData *channel,
		bool onlyExisting = false);

	void gotChatFull(
		not_null<PeerData*> peer,
		const MTPmessages_ChatFull &result);
	void gotUserFull(
		not_null<UserData*> user,
		const MTPusers_UserFull &result);
	void resolveWebPages();
	void gotWebPages(
		ChannelData *channel,
		const MTPmessages_Messages &result,
		mtpRequestId req);
	void gotStickerSet(uint64 setId, const MTPmessages_StickerSet &result);

	void requestStickers(TimeId now);
	void requestMasks(TimeId now);
	void requestCustomEmoji(TimeId now);
	void requestRecentStickers(TimeId now, bool attached = false);
	void requestRecentStickersWithHash(uint64 hash, bool attached = false);
	void requestFavedStickers(TimeId now);
	void requestFeaturedStickers(TimeId now);
	void requestFeaturedEmoji(TimeId now);
	void requestSavedGifs(TimeId now);
	void readFeaturedSets();

	void resolveJumpToHistoryDate(
		not_null<PeerData*> peer,
		MsgId topicRootId,
		const QDate &date,
		Fn<void(not_null<PeerData*>, MsgId)> callback);
	template <typename Callback>
	void requestMessageAfterDate(
		not_null<PeerData*> peer,
		MsgId topicRootId,
		const QDate &date,
		Callback &&callback);

	void sharedMediaDone(
		not_null<PeerData*> peer,
		MsgId topicRootId,
		SharedMediaType type,
		Api::SearchResult &&parsed);

	void sendSharedContact(
		const QString &phone,
		const QString &firstName,
		const QString &lastName,
		UserId userId,
		const SendAction &action,
		Fn<void(bool)> done);

	void deleteHistory(
		not_null<PeerData*> peer,
		bool justClear,
		bool revoke);
	void applyAffectedMessages(const MTPmessages_AffectedMessages &result) const;

	void deleteAllFromParticipantSend(
		not_null<ChannelData*> channel,
		not_null<PeerData*> from);

	void uploadAlbumMedia(
		not_null<HistoryItem*> item,
		const MessageGroupId &groupId,
		const MTPInputMedia &media);
	void sendAlbumWithUploaded(
		not_null<HistoryItem*> item,
		const MessageGroupId &groupId,
		const MTPInputMedia &media);
	void sendAlbumWithCancelled(
		not_null<HistoryItem*> item,
		const MessageGroupId &groupId);
	void sendAlbumIfReady(not_null<SendingAlbum*> album);
	void sendMedia(
		not_null<HistoryItem*> item,
		const MTPInputMedia &media,
		Api::SendOptions options,
		Fn<void(bool)> done = nullptr);
	void sendMediaWithRandomId(
		not_null<HistoryItem*> item,
		const MTPInputMedia &media,
		Api::SendOptions options,
		uint64 randomId,
		Fn<void(bool)> done = nullptr);

	void getTopPromotionDelayed(TimeId now, TimeId next);
	void topPromotionDone(const MTPhelp_PromoData &proxy);

	void sendNotifySettingsUpdates();

	template <typename Request>
	void requestFileReference(
		Data::FileOrigin origin,
		FileReferencesHandler &&handler,
		Request &&data);

	void migrateDone(
		not_null<PeerData*> peer,
		not_null<ChannelData*> channel);
	void migrateFail(not_null<PeerData*> peer, const QString &error);

	void checkStatsSessions();

	const not_null<Main::Session*> _session;

	base::flat_map<QString, int> _modifyRequests;

	MessageDataRequests _messageDataRequests;
	base::flat_map<
		not_null<ChannelData*>,
		MessageDataRequests> _channelMessageDataRequests;
	SingleQueuedInvokation _messageDataResolveDelayed;

	using PeerRequests = base::flat_map<PeerData*, mtpRequestId>;
	PeerRequests _fullPeerRequests;
	base::flat_set<not_null<PeerData*>> _requestedPeerSettings;

	base::flat_map<
		not_null<History*>,
		std::pair<mtpRequestId,Fn<void()>>> _historyArchivedRequests;

	base::flat_map<not_null<WebPageData*>, mtpRequestId> _webPagesPending;
	base::Timer _webPagesTimer;

	struct StickerSetRequest {
		uint64 accessHash = 0;
		mtpRequestId id = 0;
	};
	base::flat_map<uint64, StickerSetRequest> _stickerSetRequests;

	base::flat_map<
		not_null<ChannelData*>,
		mtpRequestId> _channelAmInRequests;

	struct NotifySettingsKey {
		PeerId peerId = 0;
		MsgId topicRootId = 0;

		friend inline constexpr auto operator<=>(
			NotifySettingsKey,
			NotifySettingsKey) = default;
	};
	base::flat_map<NotifySettingsKey, mtpRequestId> _notifySettingRequests;

	base::flat_map<
		base::weak_ptr<Data::Thread>,
		mtpRequestId> _draftsSaveRequestIds;
	base::Timer _draftsSaveTimer;

	base::flat_set<mtpRequestId> _stickerSetDisenableRequests;
	base::flat_set<mtpRequestId> _maskSetDisenableRequests;
	base::flat_set<mtpRequestId> _customEmojiSetDisenableRequests;
	mtpRequestId _masksReorderRequestId = 0;
	mtpRequestId _customEmojiReorderRequestId = 0;
	mtpRequestId _stickersReorderRequestId = 0;
	mtpRequestId _stickersClearRecentRequestId = 0;
	mtpRequestId _stickersClearRecentAttachedRequestId = 0;

	mtpRequestId _stickersUpdateRequest = 0;
	mtpRequestId _masksUpdateRequest = 0;
	mtpRequestId _customEmojiUpdateRequest = 0;
	mtpRequestId _recentStickersUpdateRequest = 0;
	mtpRequestId _recentAttachedStickersUpdateRequest = 0;
	mtpRequestId _favedStickersUpdateRequest = 0;
	mtpRequestId _featuredStickersUpdateRequest = 0;
	mtpRequestId _featuredEmojiUpdateRequest = 0;
	mtpRequestId _savedGifsUpdateRequest = 0;

	base::Timer _featuredSetsReadTimer;
	base::flat_set<uint64> _featuredSetsRead;

	base::flat_map<QString, StickersByEmoji> _stickersByEmoji;

	mtpRequestId _contactsRequestId = 0;
	mtpRequestId _contactsStatusesRequestId = 0;

	struct SharedMediaRequest {
		not_null<PeerData*> peer;
		MsgId topicRootId = 0;
		SharedMediaType mediaType = {};
		MsgId aroundId = 0;
		SliceType sliceType = {};

		friend inline auto operator<=>(
			const SharedMediaRequest&,
			const SharedMediaRequest&) = default;
	};
	base::flat_set<SharedMediaRequest> _sharedMediaRequests;

	std::unique_ptr<DialogsLoadState> _dialogsLoadState;
	TimeId _dialogsLoadTill = 0;
	rpl::variable<bool> _dialogsLoadMayBlockByDate = false;
	rpl::variable<bool> _dialogsLoadBlockedByDate = false;

	base::flat_map<
		not_null<Data::Folder*>,
		DialogsLoadState> _foldersLoadState;

	rpl::event_stream<SendAction> _sendActions;

	std::unique_ptr<TaskQueue> _fileLoader;
	base::flat_map<uint64, std::shared_ptr<SendingAlbum>> _sendingAlbums;

	mtpRequestId _topPromotionRequestId = 0;
	std::pair<QString, uint32> _topPromotionKey;
	TimeId _topPromotionNextRequestTime = TimeId(0);
	base::Timer _topPromotionTimer;

	base::flat_set<not_null<const Data::ForumTopic*>> _updateNotifyTopics;
	base::flat_set<not_null<const PeerData*>> _updateNotifyPeers;
	base::flat_set<Data::DefaultNotify> _updateNotifyDefaults;
	base::Timer _updateNotifyTimer;
	rpl::lifetime _updateNotifyQueueLifetime;

	std::map<
		Data::FileOrigin,
		std::vector<FileReferencesHandler>> _fileReferenceHandlers;

	mtpRequestId _deepLinkInfoRequestId = 0;

	crl::time _termsUpdateSendAt = 0;
	mtpRequestId _termsUpdateRequestId = 0;

	mtpRequestId _checkInviteRequestId = 0;
	mtpRequestId _checkFilterInviteRequestId = 0;

	struct MigrateCallbacks {
		FnMut<void(not_null<ChannelData*>)> done;
		Fn<void(const QString&)> fail;
	};
	base::flat_map<
		not_null<PeerData*>,
		std::vector<MigrateCallbacks>> _migrateCallbacks;

	struct {
		mtpRequestId requestId = 0;
		QString requestedText;
	} _bio;

	base::flat_map<MTP::DcId, base::flat_set<mtpRequestId>> _statsRequests;
	base::Timer _statsSessionKillTimer;

	const std::unique_ptr<Api::Authorizations> _authorizations;
	const std::unique_ptr<Api::AttachedStickers> _attachedStickers;
	const std::unique_ptr<Api::BlockedPeers> _blockedPeers;
	const std::unique_ptr<Api::CloudPassword> _cloudPassword;
	const std::unique_ptr<Api::SelfDestruct> _selfDestruct;
	const std::unique_ptr<Api::SensitiveContent> _sensitiveContent;
	const std::unique_ptr<Api::GlobalPrivacy> _globalPrivacy;
	const std::unique_ptr<Api::UserPrivacy> _userPrivacy;
	const std::unique_ptr<Api::InviteLinks> _inviteLinks;
	const std::unique_ptr<Api::ViewsManager> _views;
	const std::unique_ptr<Api::ConfirmPhone> _confirmPhone;
	const std::unique_ptr<Api::PeerPhoto> _peerPhoto;
	const std::unique_ptr<Api::Polls> _polls;
	const std::unique_ptr<Api::ChatParticipants> _chatParticipants;
	const std::unique_ptr<Api::UnreadThings> _unreadThings;
	const std::unique_ptr<Api::Ringtones> _ringtones;
	const std::unique_ptr<Api::Transcribes> _transcribes;
	const std::unique_ptr<Api::Premium> _premium;
	const std::unique_ptr<Api::Usernames> _usernames;
	const std::unique_ptr<Api::Websites> _websites;
	const std::unique_ptr<Api::PeerColors> _peerColors;

	mtpRequestId _wallPaperRequestId = 0;
	QString _wallPaperSlug;
	Fn<void(const Data::WallPaper &)> _wallPaperDone;
	Fn<void()> _wallPaperFail;

	mtpRequestId _contactSignupSilentRequestId = 0;
	std::optional<bool> _contactSignupSilent;
	rpl::event_stream<bool> _contactSignupSilentChanges;

	base::flat_map<
		not_null<UserData*>,
		std::vector<not_null<PeerData*>>> _botCommonGroups;
	base::flat_map<not_null<UserData*>, Fn<void()>> _botCommonGroupsRequests;

	base::flat_map<FullMsgId, QString> _unlikelyMessageLinks;
	base::flat_map<FullStoryId, QString> _unlikelyStoryLinks;

};
