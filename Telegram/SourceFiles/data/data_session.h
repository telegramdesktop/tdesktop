/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "chat_helpers/stickers.h"
#include "dialogs/dialogs_key.h"
#include "data/data_groups.h"
#include "base/timer.h"

class HistoryItem;
class BoxContent;

namespace HistoryView {
struct Group;
class Element;
} // namespace HistoryView

class AuthSession;

namespace Media {
namespace Clip {
class Reader;
} // namespace Clip
} // namespace Media

namespace Export {
class ControllerWrap;
namespace View {
class PanelController;
} // namespace View
} // namespace Export

namespace Passport {
struct SavedCredentials;
} // namespace Passport

namespace Data {

class Feed;
enum class FeedUpdateFlag;
struct FeedUpdate;

class Session final {
public:
	using ViewElement = HistoryView::Element;

	explicit Session(not_null<AuthSession*> session);
	~Session();

	AuthSession &session() const {
		return *_session;
	}

	void startExport(PeerData *peer = nullptr);
	void startExport(const MTPInputPeer &singlePeer);
	void suggestStartExport(TimeId availableAt);
	void clearExportSuggestion();
	rpl::producer<Export::View::PanelController*> currentExportView() const;
	bool exportInProgress() const;
	void stopExportWithConfirmation(FnMut<void()> callback);
	void stopExport();

	const Passport::SavedCredentials *passportCredentials() const;
	void rememberPassportCredentials(
		Passport::SavedCredentials data,
		TimeMs rememberFor);
	void forgetPassportCredentials();

	[[nodiscard]] base::Variable<bool> &contactsLoaded() {
		return _contactsLoaded;
	}
	[[nodiscard]] base::Variable<bool> &allChatsLoaded() {
		return _allChatsLoaded;
	}
	[[nodiscard]] base::Observable<void> &moreChatsLoaded() {
		return _moreChatsLoaded;
	}

	struct ItemVisibilityQuery {
		not_null<HistoryItem*> item;
		not_null<bool*> isVisible;
	};
	[[nodiscard]] base::Observable<ItemVisibilityQuery> &queryItemVisibility() {
		return _queryItemVisibility;
	}
	struct IdChange {
		not_null<HistoryItem*> item;
		MsgId oldId = 0;
	};
	void notifyItemIdChange(IdChange event);
	[[nodiscard]] rpl::producer<IdChange> itemIdChanged() const;
	void notifyItemLayoutChange(not_null<const HistoryItem*> item);
	[[nodiscard]] rpl::producer<not_null<const HistoryItem*>> itemLayoutChanged() const;
	void notifyViewLayoutChange(not_null<const ViewElement*> view);
	[[nodiscard]] rpl::producer<not_null<const ViewElement*>> viewLayoutChanged() const;
	void requestItemRepaint(not_null<const HistoryItem*> item);
	[[nodiscard]] rpl::producer<not_null<const HistoryItem*>> itemRepaintRequest() const;
	void requestViewRepaint(not_null<const ViewElement*> view);
	[[nodiscard]] rpl::producer<not_null<const ViewElement*>> viewRepaintRequest() const;
	void requestItemResize(not_null<const HistoryItem*> item);
	[[nodiscard]] rpl::producer<not_null<const HistoryItem*>> itemResizeRequest() const;
	void requestViewResize(not_null<ViewElement*> view);
	[[nodiscard]] rpl::producer<not_null<ViewElement*>> viewResizeRequest() const;
	void requestItemViewRefresh(not_null<HistoryItem*> item);
	[[nodiscard]] rpl::producer<not_null<HistoryItem*>> itemViewRefreshRequest() const;
	void requestItemTextRefresh(not_null<HistoryItem*> item);
	void requestAnimationPlayInline(not_null<HistoryItem*> item);
	[[nodiscard]] rpl::producer<not_null<HistoryItem*>> animationPlayInlineRequest() const;
	void notifyHistoryUnloaded(not_null<const History*> history);
	[[nodiscard]] rpl::producer<not_null<const History*>> historyUnloaded() const;

	void notifyItemRemoved(not_null<const HistoryItem*> item);
	[[nodiscard]] rpl::producer<not_null<const HistoryItem*>> itemRemoved() const;
	void notifyViewRemoved(not_null<const ViewElement*> view);
	[[nodiscard]] rpl::producer<not_null<const ViewElement*>> viewRemoved() const;
	void notifyHistoryCleared(not_null<const History*> history);
	[[nodiscard]] rpl::producer<not_null<const History*>> historyCleared() const;
	void notifyHistoryChangeDelayed(not_null<History*> history);
	[[nodiscard]] rpl::producer<not_null<History*>> historyChanged() const;
	void sendHistoryChangeNotifications();

	using MegagroupParticipant = std::tuple<
		not_null<ChannelData*>,
		not_null<UserData*>>;
	void removeMegagroupParticipant(
		not_null<ChannelData*> channel,
		not_null<UserData*> user);
	[[nodiscard]] rpl::producer<MegagroupParticipant> megagroupParticipantRemoved() const;
	[[nodiscard]] rpl::producer<not_null<UserData*>> megagroupParticipantRemoved(
		not_null<ChannelData*> channel) const;
	void addNewMegagroupParticipant(
		not_null<ChannelData*> channel,
		not_null<UserData*> user);
	[[nodiscard]] rpl::producer<MegagroupParticipant> megagroupParticipantAdded() const;
	[[nodiscard]] rpl::producer<not_null<UserData*>> megagroupParticipantAdded(
		not_null<ChannelData*> channel) const;

	void notifyFeedUpdated(not_null<Feed*> feed, FeedUpdateFlag update);
	[[nodiscard]] rpl::producer<FeedUpdate> feedUpdated() const;

	void notifyStickersUpdated();
	[[nodiscard]] rpl::producer<> stickersUpdated() const;
	void notifySavedGifsUpdated();
	[[nodiscard]] rpl::producer<> savedGifsUpdated() const;

	bool stickersUpdateNeeded(TimeMs now) const {
		return stickersUpdateNeeded(_lastStickersUpdate, now);
	}
	void setLastStickersUpdate(TimeMs update) {
		_lastStickersUpdate = update;
	}
	bool recentStickersUpdateNeeded(TimeMs now) const {
		return stickersUpdateNeeded(_lastRecentStickersUpdate, now);
	}
	void setLastRecentStickersUpdate(TimeMs update) {
		_lastRecentStickersUpdate = update;
	}
	bool favedStickersUpdateNeeded(TimeMs now) const {
		return stickersUpdateNeeded(_lastFavedStickersUpdate, now);
	}
	void setLastFavedStickersUpdate(TimeMs update) {
		_lastFavedStickersUpdate = update;
	}
	bool featuredStickersUpdateNeeded(TimeMs now) const {
		return stickersUpdateNeeded(_lastFeaturedStickersUpdate, now);
	}
	void setLastFeaturedStickersUpdate(TimeMs update) {
		_lastFeaturedStickersUpdate = update;
	}
	bool savedGifsUpdateNeeded(TimeMs now) const {
		return stickersUpdateNeeded(_lastSavedGifsUpdate, now);
	}
	void setLastSavedGifsUpdate(TimeMs update) {
		_lastSavedGifsUpdate = update;
	}
	int featuredStickerSetsUnreadCount() const {
		return _featuredStickerSetsUnreadCount.current();
	}
	void setFeaturedStickerSetsUnreadCount(int count) {
		_featuredStickerSetsUnreadCount = count;
	}
	[[nodiscard]] rpl::producer<int> featuredStickerSetsUnreadCountValue() const {
		return _featuredStickerSetsUnreadCount.value();
	}
	const Stickers::Sets &stickerSets() const {
		return _stickerSets;
	}
	Stickers::Sets &stickerSetsRef() {
		return _stickerSets;
	}
	const Stickers::Order &stickerSetsOrder() const {
		return _stickerSetsOrder;
	}
	Stickers::Order &stickerSetsOrderRef() {
		return _stickerSetsOrder;
	}
	const Stickers::Order &featuredStickerSetsOrder() const {
		return _featuredStickerSetsOrder;
	}
	Stickers::Order &featuredStickerSetsOrderRef() {
		return _featuredStickerSetsOrder;
	}
	const Stickers::Order &archivedStickerSetsOrder() const {
		return _archivedStickerSetsOrder;
	}
	Stickers::Order &archivedStickerSetsOrderRef() {
		return _archivedStickerSetsOrder;
	}
	const Stickers::SavedGifs &savedGifs() const {
		return _savedGifs;
	}
	Stickers::SavedGifs &savedGifsRef() {
		return _savedGifs;
	}

	HistoryItemsList idsToItems(const MessageIdsList &ids) const;
	MessageIdsList itemsToIds(const HistoryItemsList &items) const;
	MessageIdsList itemOrItsGroup(not_null<HistoryItem*> item) const;

	int pinnedDialogsCount() const;
	const std::deque<Dialogs::Key> &pinnedDialogsOrder() const;
	void setPinnedDialog(const Dialogs::Key &key, bool pinned);
	void applyPinnedDialogs(const QVector<MTPDialog> &list);
	void applyPinnedDialogs(const QVector<MTPDialogPeer> &list);
	void reorderTwoPinnedDialogs(
		const Dialogs::Key &key1,
		const Dialogs::Key &key2);

	void photoLoadSettingsChanged();
	void voiceLoadSettingsChanged();
	void animationLoadSettingsChanged();

	void notifyPhotoLayoutChanged(not_null<const PhotoData*> photo);
	void notifyDocumentLayoutChanged(
		not_null<const DocumentData*> document);
	void requestDocumentViewRepaint(not_null<const DocumentData*> document);
	void markMediaRead(not_null<const DocumentData*> document);

	not_null<PhotoData*> photo(PhotoId id);
	not_null<PhotoData*> photo(const MTPPhoto &data);
	not_null<PhotoData*> photo(const MTPDphoto &data);
	not_null<PhotoData*> photo(
		const MTPPhoto &data,
		const PreparedPhotoThumbs &thumbs);
	not_null<PhotoData*> photo(
		PhotoId id,
		const uint64 &access,
		TimeId date,
		const ImagePtr &thumb,
		const ImagePtr &medium,
		const ImagePtr &full);
	void photoConvert(
		not_null<PhotoData*> original,
		const MTPPhoto &data);
	PhotoData *photoFromWeb(const MTPWebDocument &data, ImagePtr thumb);

	not_null<DocumentData*> document(DocumentId id);
	not_null<DocumentData*> document(const MTPDocument &data);
	not_null<DocumentData*> document(const MTPDdocument &data);
	not_null<DocumentData*> document(
		const MTPdocument &data,
		const QPixmap &thumb);
	not_null<DocumentData*> document(
		DocumentId id,
		const uint64 &access,
		int32 version,
		TimeId date,
		const QVector<MTPDocumentAttribute> &attributes,
		const QString &mime,
		const ImagePtr &thumb,
		int32 dc,
		int32 size,
		const StorageImageLocation &thumbLocation);
	void documentConvert(
		not_null<DocumentData*> original,
		const MTPDocument &data);
	DocumentData *documentFromWeb(
		const MTPWebDocument &data,
		ImagePtr thumb);

	not_null<WebPageData*> webpage(WebPageId id);
	not_null<WebPageData*> webpage(const MTPWebPage &data);
	not_null<WebPageData*> webpage(const MTPDwebPage &data);
	not_null<WebPageData*> webpage(const MTPDwebPagePending &data);
	not_null<WebPageData*> webpage(
		WebPageId id,
		const QString &siteName,
		const TextWithEntities &content);
	not_null<WebPageData*> webpage(
		WebPageId id,
		const QString &type,
		const QString &url,
		const QString &displayUrl,
		const QString &siteName,
		const QString &title,
		const TextWithEntities &description,
		PhotoData *photo,
		DocumentData *document,
		int duration,
		const QString &author,
		TimeId pendingTill);

	not_null<GameData*> game(GameId id);
	not_null<GameData*> game(const MTPDgame &data);
	not_null<GameData*> game(
		GameId id,
		const uint64 &accessHash,
		const QString &shortName,
		const QString &title,
		const QString &description,
		PhotoData *photo,
		DocumentData *document);
	void gameConvert(
		not_null<GameData*> original,
		const MTPGame &data);

	void registerPhotoItem(
		not_null<const PhotoData*> photo,
		not_null<HistoryItem*> item);
	void unregisterPhotoItem(
		not_null<const PhotoData*> photo,
		not_null<HistoryItem*> item);
	void registerDocumentItem(
		not_null<const DocumentData*> document,
		not_null<HistoryItem*> item);
	void unregisterDocumentItem(
		not_null<const DocumentData*> document,
		not_null<HistoryItem*> item);
	void registerWebPageView(
		not_null<const WebPageData*> page,
		not_null<ViewElement*> view);
	void unregisterWebPageView(
		not_null<const WebPageData*> page,
		not_null<ViewElement*> view);
	void registerWebPageItem(
		not_null<const WebPageData*> page,
		not_null<HistoryItem*> item);
	void unregisterWebPageItem(
		not_null<const WebPageData*> page,
		not_null<HistoryItem*> item);
	void registerGameView(
		not_null<const GameData*> game,
		not_null<ViewElement*> view);
	void unregisterGameView(
		not_null<const GameData*> game,
		not_null<ViewElement*> view);
	void registerContactView(
		UserId contactId,
		not_null<ViewElement*> view);
	void unregisterContactView(
		UserId contactId,
		not_null<ViewElement*> view);
	void registerContactItem(
		UserId contactId,
		not_null<HistoryItem*> item);
	void unregisterContactItem(
		UserId contactId,
		not_null<HistoryItem*> item);
	void registerAutoplayAnimation(
		not_null<::Media::Clip::Reader*> reader,
		not_null<ViewElement*> view);
	void unregisterAutoplayAnimation(
		not_null<::Media::Clip::Reader*> reader);

	HistoryItem *findWebPageItem(not_null<WebPageData*> page) const;
	QString findContactPhone(not_null<UserData*> contact) const;
	QString findContactPhone(UserId contactId) const;

	void notifyWebPageUpdateDelayed(not_null<WebPageData*> page);
	void notifyGameUpdateDelayed(not_null<GameData*> game);
	void sendWebPageGameNotifications();

	void stopAutoplayAnimations();

	void registerItemView(not_null<ViewElement*> view);
	void unregisterItemView(not_null<ViewElement*> view);

	not_null<Feed*> feed(FeedId id);
	Feed *feedLoaded(FeedId id);
	void setDefaultFeedId(FeedId id);
	FeedId defaultFeedId() const;
	rpl::producer<FeedId> defaultFeedIdValue() const;

	void requestNotifySettings(not_null<PeerData*> peer);
	void applyNotifySetting(
		const MTPNotifyPeer &notifyPeer,
		const MTPPeerNotifySettings &settings);
	void updateNotifySettings(
		not_null<PeerData*> peer,
		base::optional<int> muteForSeconds,
		base::optional<bool> silentPosts = base::none);
	bool notifyIsMuted(
		not_null<const PeerData*> peer,
		TimeMs *changesIn = nullptr) const;
	bool notifySilentPosts(not_null<const PeerData*> peer) const;
	bool notifyMuteUnknown(not_null<const PeerData*> peer) const;
	bool notifySilentPostsUnknown(not_null<const PeerData*> peer) const;
	bool notifySettingsUnknown(not_null<const PeerData*> peer) const;
	rpl::producer<> defaultUserNotifyUpdates() const;
	rpl::producer<> defaultChatNotifyUpdates() const;
	rpl::producer<> defaultNotifyUpdates(
		not_null<const PeerData*> peer) const;

	void forgetMedia();

	void setMimeForwardIds(MessageIdsList &&list);
	MessageIdsList takeMimeForwardIds();

	void setProxyPromoted(PeerData *promoted);
	PeerData *proxyPromoted() const;

	Groups &groups() {
		return _groups;
	}
	const Groups &groups() const {
		return _groups;
	}

private:
	void suggestStartExport();

	void setupContactViewsViewer();
	void setupChannelLeavingViewer();
	void photoApplyFields(
		not_null<PhotoData*> photo,
		const MTPPhoto &data);
	void photoApplyFields(
		not_null<PhotoData*> photo,
		const MTPDphoto &data);
	void photoApplyFields(
		not_null<PhotoData*> photo,
		const uint64 &access,
		TimeId date,
		const ImagePtr &thumb,
		const ImagePtr &medium,
		const ImagePtr &full);

	void documentApplyFields(
		not_null<DocumentData*> document,
		const MTPDocument &data);
	void documentApplyFields(
		not_null<DocumentData*> document,
		const MTPDdocument &data);
	void documentApplyFields(
		not_null<DocumentData*> document,
		const uint64 &access,
		int32 version,
		TimeId date,
		const QVector<MTPDocumentAttribute> &attributes,
		const QString &mime,
		const ImagePtr &thumb,
		int32 dc,
		int32 size,
		const StorageImageLocation &thumbLocation);
	DocumentData *documentFromWeb(
		const MTPDwebDocument &data,
		ImagePtr thumb);
	DocumentData *documentFromWeb(
		const MTPDwebDocumentNoProxy &data,
		ImagePtr thumb);

	void webpageApplyFields(
		not_null<WebPageData*> page,
		const MTPDwebPage &data);
	void webpageApplyFields(
		not_null<WebPageData*> page,
		const QString &type,
		const QString &url,
		const QString &displayUrl,
		const QString &siteName,
		const QString &title,
		const TextWithEntities &description,
		PhotoData *photo,
		DocumentData *document,
		int duration,
		const QString &author,
		TimeId pendingTill);

	void gameApplyFields(
		not_null<GameData*> game,
		const MTPDgame &data);
	void gameApplyFields(
		not_null<GameData*> game,
		const uint64 &accessHash,
		const QString &shortName,
		const QString &title,
		const QString &description,
		PhotoData *photo,
		DocumentData *document);

	bool stickersUpdateNeeded(TimeMs lastUpdate, TimeMs now) const {
		constexpr auto kStickersUpdateTimeout = TimeMs(3600'000);
		return (lastUpdate == 0)
			|| (now >= lastUpdate + kStickersUpdateTimeout);
	}
	void userIsContactUpdated(not_null<UserData*> user);

	void clearPinnedDialogs();
	void setIsPinned(const Dialogs::Key &key, bool pinned);

	NotifySettings &defaultNotifySettings(not_null<const PeerData*> peer);
	const NotifySettings &defaultNotifySettings(
		not_null<const PeerData*> peer) const;
	void unmuteByFinished();
	void unmuteByFinishedDelayed(TimeMs delay);
	void updateNotifySettingsLocal(not_null<PeerData*> peer);
	void sendNotifySettingsUpdates();

	template <typename Method>
	void enumerateItemViews(
		not_null<const HistoryItem*> item,
		Method method);

	not_null<AuthSession*> _session;

	std::unique_ptr<Export::ControllerWrap> _export;
	std::unique_ptr<Export::View::PanelController> _exportPanel;
	rpl::event_stream<Export::View::PanelController*> _exportViewChanges;
	TimeId _exportAvailableAt = 0;
	QPointer<BoxContent> _exportSuggestion;

	base::Variable<bool> _contactsLoaded = { false };
	base::Variable<bool> _allChatsLoaded = { false };
	base::Observable<void> _moreChatsLoaded;
	base::Observable<ItemVisibilityQuery> _queryItemVisibility;
	rpl::event_stream<IdChange> _itemIdChanges;
	rpl::event_stream<not_null<const HistoryItem*>> _itemLayoutChanges;
	rpl::event_stream<not_null<const ViewElement*>> _viewLayoutChanges;
	rpl::event_stream<not_null<const HistoryItem*>> _itemRepaintRequest;
	rpl::event_stream<not_null<const ViewElement*>> _viewRepaintRequest;
	rpl::event_stream<not_null<const HistoryItem*>> _itemResizeRequest;
	rpl::event_stream<not_null<ViewElement*>> _viewResizeRequest;
	rpl::event_stream<not_null<HistoryItem*>> _itemViewRefreshRequest;
	rpl::event_stream<not_null<HistoryItem*>> _itemTextRefreshRequest;
	rpl::event_stream<not_null<HistoryItem*>> _animationPlayInlineRequest;
	rpl::event_stream<not_null<const HistoryItem*>> _itemRemoved;
	rpl::event_stream<not_null<const ViewElement*>> _viewRemoved;
	rpl::event_stream<not_null<const History*>> _historyUnloaded;
	rpl::event_stream<not_null<const History*>> _historyCleared;
	base::flat_set<not_null<History*>> _historiesChanged;
	rpl::event_stream<not_null<History*>> _historyChanged;
	rpl::event_stream<MegagroupParticipant> _megagroupParticipantRemoved;
	rpl::event_stream<MegagroupParticipant> _megagroupParticipantAdded;
	rpl::event_stream<FeedUpdate> _feedUpdates;

	rpl::event_stream<> _stickersUpdated;
	rpl::event_stream<> _savedGifsUpdated;
	TimeMs _lastStickersUpdate = 0;
	TimeMs _lastRecentStickersUpdate = 0;
	TimeMs _lastFavedStickersUpdate = 0;
	TimeMs _lastFeaturedStickersUpdate = 0;
	TimeMs _lastSavedGifsUpdate = 0;
	rpl::variable<int> _featuredStickerSetsUnreadCount = 0;
	Stickers::Sets _stickerSets;
	Stickers::Order _stickerSetsOrder;
	Stickers::Order _featuredStickerSetsOrder;
	Stickers::Order _archivedStickerSetsOrder;
	Stickers::SavedGifs _savedGifs;

	std::unordered_map<
		PhotoId,
		std::unique_ptr<PhotoData>> _photos;
	std::map<
		not_null<const PhotoData*>,
		base::flat_set<not_null<HistoryItem*>>> _photoItems;
	std::unordered_map<
		DocumentId,
		std::unique_ptr<DocumentData>> _documents;
	std::map<
		not_null<const DocumentData*>,
		base::flat_set<not_null<HistoryItem*>>> _documentItems;
	std::unordered_map<
		WebPageId,
		std::unique_ptr<WebPageData>> _webpages;
	std::map<
		not_null<const WebPageData*>,
		base::flat_set<not_null<HistoryItem*>>> _webpageItems;
	std::map<
		not_null<const WebPageData*>,
		base::flat_set<not_null<ViewElement*>>> _webpageViews;
	std::unordered_map<
		GameId,
		std::unique_ptr<GameData>> _games;
	std::map<
		not_null<const GameData*>,
		base::flat_set<not_null<ViewElement*>>> _gameViews;
	std::map<
		UserId,
		base::flat_set<not_null<HistoryItem*>>> _contactItems;
	std::map<
		UserId,
		base::flat_set<not_null<ViewElement*>>> _contactViews;
	base::flat_map<
		not_null<::Media::Clip::Reader*>,
		not_null<ViewElement*>> _autoplayAnimations;

	base::flat_set<not_null<WebPageData*>> _webpagesUpdated;
	base::flat_set<not_null<GameData*>> _gamesUpdated;

	std::deque<Dialogs::Key> _pinnedDialogs;
	base::flat_map<FeedId, std::unique_ptr<Feed>> _feeds;
	rpl::variable<FeedId> _defaultFeedId = FeedId();
	Groups _groups;
	std::map<
		not_null<const HistoryItem*>,
		std::vector<not_null<ViewElement*>>> _views;

	PeerData *_proxyPromoted = nullptr;

	NotifySettings _defaultUserNotifySettings;
	NotifySettings _defaultChatNotifySettings;
	rpl::event_stream<> _defaultUserNotifyUpdates;
	rpl::event_stream<> _defaultChatNotifyUpdates;
	std::unordered_set<not_null<const PeerData*>> _mutedPeers;
	base::Timer _unmuteByFinishedTimer;

	MessageIdsList _mimeForwardIds;

	using CredentialsWithGeneration = std::pair<
		const Passport::SavedCredentials,
		int>;
	std::unique_ptr<CredentialsWithGeneration> _passportCredentials;

	rpl::lifetime _lifetime;

};

} // namespace Data
