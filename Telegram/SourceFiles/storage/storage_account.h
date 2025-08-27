/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/flags.h"
#include "storage/cache/storage_cache_database.h"
#include "data/stickers/data_stickers_set.h"
#include "data/data_drafts.h"
#include "webview/webview_common.h"

class History;

namespace Core {
class FileLocation;
} // namespace Core

namespace Export {
struct Settings;
} // namespace Export

namespace Main {
class Account;
class SessionSettings;
} // namespace Main

namespace Data {
class WallPaper;
} // namespace Data

namespace MTP {
class Config;
class AuthKey;
using AuthKeyPtr = std::shared_ptr<AuthKey>;
} // namespace MTP

namespace Storage {
namespace details {
struct ReadSettingsContext;
struct FileReadDescriptor;
} // namespace details

class EncryptionKey;

using FileKey = quint64;

enum class StartResult : uchar;

struct MessageDraft {
	FullReplyTo reply;
	SuggestPostOptions suggest;
	TextWithTags textWithTags;
	Data::WebPageDraft webpage;
};

struct MessageDraftSource {
	Fn<MessageDraft()> draft;
	Fn<MessageCursor()> cursor;
};

class Account final {
public:
	Account(not_null<Main::Account*> owner, const QString &dataName);
	~Account();

	[[nodiscard]] StartResult legacyStart(const QByteArray &passcode);
	[[nodiscard]] std::unique_ptr<MTP::Config> start(
		MTP::AuthKeyPtr localKey);
	void startAdded(MTP::AuthKeyPtr localKey);
	[[nodiscard]] int oldMapVersion() const {
		return _oldMapVersion;
	}

	[[nodiscard]] QString tempDirectory() const;
	[[nodiscard]] QString supportModePath() const;

	[[nodiscard]] MTP::AuthKeyPtr peekLegacyLocalKey() const {
		return _localKey;
	}

	void writeSessionSettings();
	void writeMtpData();
	void writeMtpConfig();

	void registerDraftSource(
		not_null<History*> history,
		Data::DraftKey key,
		MessageDraftSource source);
	void unregisterDraftSource(
		not_null<History*> history,
		Data::DraftKey key);
	void writeDrafts(not_null<History*> history);
	void readDraftsWithCursors(not_null<History*> history);
	void writeDraftCursors(not_null<History*> history);
	[[nodiscard]] bool hasDraftCursors(PeerId peerId);
	[[nodiscard]] bool hasDraft(PeerId peerId);

	void writeFileLocation(
		MediaKey location,
		const Core::FileLocation &local);
	[[nodiscard]] Core::FileLocation readFileLocation(MediaKey location);
	void removeFileLocation(MediaKey location);

	void updateDownloads(Fn<std::optional<QByteArray>()> downloadsSerialize);
	[[nodiscard]] QByteArray downloadsSerialized() const;

	[[nodiscard]] EncryptionKey cacheKey() const;
	[[nodiscard]] QString cachePath() const;
	[[nodiscard]] Cache::Database::Settings cacheSettings() const;
	void updateCacheSettings(
		Cache::Database::SettingsUpdate &update,
		Cache::Database::SettingsUpdate &updateBig);

	[[nodiscard]] EncryptionKey cacheBigFileKey() const;
	[[nodiscard]] QString cacheBigFilePath() const;
	[[nodiscard]] Cache::Database::Settings cacheBigFileSettings() const;

	void writeInstalledStickers();
	void writeFeaturedStickers();
	void writeRecentStickers();
	void writeFavedStickers();
	void writeArchivedStickers();
	void writeArchivedMasks();
	void readInstalledStickers();
	void readFeaturedStickers();
	void readRecentStickers();
	void readFavedStickers();
	void readArchivedStickers();
	void readArchivedMasks();
	void writeSavedGifs();
	void readSavedGifs();
	void writeInstalledMasks();
	void writeRecentMasks();
	void readInstalledMasks();
	void readRecentMasks();
	void writeInstalledCustomEmoji();
	void writeFeaturedCustomEmoji();
	void readInstalledCustomEmoji();
	void readFeaturedCustomEmoji();

	void writeRecentHashtagsAndBots();
	void readRecentHashtagsAndBots();
	void saveRecentSentHashtags(const QString &text);
	void saveRecentSearchHashtags(const QString &text);

	void writeExportSettings(const Export::Settings &settings);
	[[nodiscard]] Export::Settings readExportSettings();

	void setMediaLastPlaybackPosition(DocumentId id, crl::time time);
	[[nodiscard]] crl::time mediaLastPlaybackPosition(DocumentId id) const;

	void writeSearchSuggestionsDelayed();
	void writeSearchSuggestionsIfNeeded();
	void writeSearchSuggestions();
	void readSearchSuggestions();

	void writeSelf();

	// Read self is special, it can't get session from account, because
	// it is not really there yet - it is still being constructed.
	void readSelf(
		not_null<Main::Session*> session,
		const QByteArray& serialized,
		int32 streamVersion);

	void markPeerTrustedOpenGame(PeerId peerId);
	[[nodiscard]] bool isPeerTrustedOpenGame(PeerId peerId);
	void markPeerTrustedPayment(PeerId peerId);
	[[nodiscard]] bool isPeerTrustedPayment(PeerId peerId);
	void markPeerTrustedOpenWebView(PeerId peerId);
	[[nodiscard]] bool isPeerTrustedOpenWebView(PeerId peerId);
	void markPeerTrustedPayForMessage(PeerId peerId, int starsPerMessage);
	[[nodiscard]] bool isPeerTrustedPayForMessage(
		PeerId peerId,
		int starsPerMessage);
	[[nodiscard]] bool peerTrustedPayForMessageRead() const;
	[[nodiscard]] bool hasPeerTrustedPayForMessageEntry(PeerId peerId) const;
	void clearPeerTrustedPayForMessage(PeerId peerId);

	void enforceModernStorageIdBots();
	[[nodiscard]] Webview::StorageId resolveStorageIdBots();
	[[nodiscard]] Webview::StorageId resolveStorageIdOther();

	[[nodiscard]] QImage readRoundPlaceholder();
	void writeRoundPlaceholder(const QImage &placeholder);

	[[nodiscard]] QByteArray readInlineBotsDownloads();
	void writeInlineBotsDownloads(const QByteArray &bytes);

	void writeBotStorage(PeerId botId, const QByteArray &serialized);
	[[nodiscard]] QByteArray readBotStorage(PeerId botId);

	[[nodiscard]] bool encrypt(
		const void *src,
		void *dst,
		uint32 len,
		const void *key128) const;
	[[nodiscard]] bool decrypt(
		const void *src,
		void *dst,
		uint32 len,
		const void *key128) const;

	void reset();

private:
	enum class ReadMapResult {
		Success,
		IncorrectPasscode,
		Failed,
	};
	enum class PeerTrustFlag : uchar {
		NoOpenGame        = (1 << 0),
		Payment           = (1 << 1),
		OpenWebView       = (1 << 2),
	};
	friend inline constexpr bool is_flag_type(PeerTrustFlag) { return true; };

	[[nodiscard]] base::flat_set<QString> collectGoodNames() const;
	[[nodiscard]] auto prepareReadSettingsContext() const
		-> details::ReadSettingsContext;

	ReadMapResult readMapWith(
		MTP::AuthKeyPtr localKey,
		const QByteArray &legacyPasscode = QByteArray());
	void clearLegacyFiles();
	void writeMapDelayed();
	void writeMapQueued();
	void writeMap();

	void readLocations();
	void writeLocations();
	void writeLocationsQueued();
	void writeLocationsDelayed();

	std::unique_ptr<Main::SessionSettings> readSessionSettings();
	void writeSessionSettings(Main::SessionSettings *stored);

	std::unique_ptr<MTP::Config> readMtpConfig();
	void readMtpData();
	std::unique_ptr<Main::SessionSettings> applyReadContext(
		details::ReadSettingsContext &&context);

	void readDraftCursors(PeerId peerId, Data::HistoryDrafts &map);
	void readDraftCursorsLegacy(
		PeerId peerId,
		details::FileReadDescriptor &draft,
		quint64 draftPeerSerialized,
		Data::HistoryDrafts &map);
	void clearDraftCursors(PeerId peerId);
	void readDraftsWithCursorsLegacy(
		not_null<History*> history,
		details::FileReadDescriptor &draft,
		quint64 draftPeerSerialized);

	void writeStickerSet(
		QDataStream &stream,
		const Data::StickersSet &set);
	template <typename CheckSet>
	void writeStickerSets(
		FileKey &stickersKey,
		CheckSet checkSet,
		const Data::StickersSetsOrder &order);
	void readStickerSets(
		FileKey &stickersKey,
		Data::StickersSetsOrder *outOrder = nullptr,
		Data::StickersSetFlags readingFlags = 0);
	void importOldRecentStickers();

	void readTrustedPeers();
	void writeTrustedPeers();

	void readMediaLastPlaybackPositions();
	void writeMediaLastPlaybackPositions();

	std::optional<RecentHashtagPack> saveRecentHashtags(
		Fn<RecentHashtagPack()> getPack,
		const QString &text);

	const not_null<Main::Account*> _owner;
	const QString _dataName;
	const FileKey _dataNameKey = 0;
	const QString _basePath;
	const QString _tempPath;
	const QString _databasePath;

	MTP::AuthKeyPtr _localKey;

	base::flat_map<PeerId, FileKey> _draftsMap;
	base::flat_map<PeerId, FileKey> _draftCursorsMap;
	base::flat_map<PeerId, bool> _draftsNotReadMap;
	base::flat_map<
		not_null<History*>,
		base::flat_map<Data::DraftKey, MessageDraftSource>> _draftSources;
	base::flat_map<PeerId, FileKey> _botStoragesMap;
	base::flat_map<PeerId, bool> _botStoragesNotReadMap;

	QMultiMap<MediaKey, Core::FileLocation> _fileLocations;
	QMap<QString, QPair<MediaKey, Core::FileLocation>> _fileLocationPairs;
	QMap<MediaKey, MediaKey> _fileLocationAliases;

	QByteArray _downloadsSerialized;
	Fn<std::optional<QByteArray>()> _downloadsSerialize;

	FileKey _locationsKey = 0;
	FileKey _trustedPeersKey = 0;
	FileKey _installedStickersKey = 0;
	FileKey _featuredStickersKey = 0;
	FileKey _recentStickersKey = 0;
	FileKey _favedStickersKey = 0;
	FileKey _archivedStickersKey = 0;
	FileKey _archivedMasksKey = 0;
	FileKey _savedGifsKey = 0;
	FileKey _recentStickersKeyOld = 0;
	FileKey _legacyBackgroundKeyDay = 0;
	FileKey _legacyBackgroundKeyNight = 0;
	FileKey _settingsKey = 0;
	FileKey _recentHashtagsAndBotsKey = 0;
	FileKey _exportSettingsKey = 0;
	FileKey _installedMasksKey = 0;
	FileKey _recentMasksKey = 0;
	FileKey _installedCustomEmojiKey = 0;
	FileKey _featuredCustomEmojiKey = 0;
	FileKey _archivedCustomEmojiKey = 0;
	FileKey _searchSuggestionsKey = 0;
	FileKey _roundPlaceholderKey = 0;
	FileKey _inlineBotsDownloadsKey = 0;
	FileKey _mediaLastPlaybackPositionsKey = 0;

	qint64 _cacheTotalSizeLimit = 0;
	qint64 _cacheBigFileTotalSizeLimit = 0;
	qint32 _cacheTotalTimeLimit = 0;
	qint32 _cacheBigFileTotalTimeLimit = 0;

	base::flat_map<PeerId, base::flags<PeerTrustFlag>> _trustedPeers;
	base::flat_map<PeerId, int> _trustedPayPerMessage;
	bool _trustedPeersRead = false;
	bool _readingUserSettings = false;
	bool _recentHashtagsAndBotsWereRead = false;
	bool _searchSuggestionsRead = false;
	bool _inlineBotsDownloadsRead = false;
	bool _mediaLastPlaybackPositionsRead = false;

	std::vector<std::pair<DocumentId, crl::time>> _mediaLastPlaybackPosition;

	Webview::StorageId _webviewStorageIdBots;
	Webview::StorageId _webviewStorageIdOther;

	int _oldMapVersion = 0;

	base::Timer _writeMapTimer;
	base::Timer _writeLocationsTimer;
	base::Timer _writeSearchSuggestionsTimer;
	bool _mapChanged = false;
	bool _locationsChanged = false;

	QImage _roundPlaceholder;

};

[[nodiscard]] Webview::StorageId TonSiteStorageId();

} // namespace Storage
