/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "storage/cache/storage_cache_database.h"
#include "data/stickers/data_stickers_set.h"

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
} // namespace details

class EncryptionKey;

using FileKey = quint64;

enum class StartResult : uchar;

struct MessageDraft {
	MsgId msgId = 0;
	TextWithTags textWithTags;
	bool previewCancelled = false;
};

class Account final {
public:
	Account(not_null<Main::Account*> owner, const QString &dataName);
	~Account();

	[[nodiscard]] StartResult legacyStart(const QByteArray &passcode);
	[[nodiscartd]] std::unique_ptr<MTP::Config> start(
		MTP::AuthKeyPtr localKey);
	void startAdded(MTP::AuthKeyPtr localKey);
	[[nodiscard]] int oldMapVersion() const {
		return _oldMapVersion;
	}

	[[nodiscard]] QString tempDirectory() const;

	[[nodiscard]] MTP::AuthKeyPtr peekLegacyLocalKey() const {
		return _localKey;
	}

	void writeSessionSettings();
	void writeMtpData();
	void writeMtpConfig();

	void writeDrafts(not_null<History*> history);
	void writeDrafts(
		const PeerId &peer,
		const MessageDraft &localDraft,
		const MessageDraft &editDraft);
	void readDraftsWithCursors(not_null<History*> history);
	void writeDraftCursors(
		const PeerId &peer,
		const MessageCursor &localCursor,
		const MessageCursor &editCursor);
	[[nodiscard]] bool hasDraftCursors(const PeerId &peer);
	[[nodiscard]] bool hasDraft(const PeerId &peer);

	void writeFileLocation(MediaKey location, const Core::FileLocation &local);
	[[nodiscard]] Core::FileLocation readFileLocation(MediaKey location);
	void removeFileLocation(MediaKey location);

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
	void readInstalledStickers();
	void readFeaturedStickers();
	void readRecentStickers();
	void readFavedStickers();
	void readArchivedStickers();
	void writeSavedGifs();
	void readSavedGifs();

	void writeRecentHashtagsAndBots();
	void readRecentHashtagsAndBots();
	void saveRecentSentHashtags(const QString &text);
	void saveRecentSearchHashtags(const QString &text);

	void writeExportSettings(const Export::Settings &settings);
	[[nodiscard]] Export::Settings readExportSettings();

	void writeSelf();

	// Read self is special, it can't get session from account, because
	// it is not really there yet - it is still being constructed.
	void readSelf(
		not_null<Main::Session*> session,
		const QByteArray& serialized,
		int32 streamVersion);

	void markBotTrusted(not_null<UserData*> bot);
	[[nodiscard]] bool isBotTrusted(not_null<UserData*> bot);

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

	void readDraftCursors(
		const PeerId &peer,
		MessageCursor &localCursor,
		MessageCursor &editCursor);
	void clearDraftCursors(const PeerId &peer);

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
		MTPDstickerSet::Flags readingFlags = 0);
	void importOldRecentStickers();

	void readTrustedBots();
	void writeTrustedBots();

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

	QMultiMap<MediaKey, Core::FileLocation> _fileLocations;
	QMap<QString, QPair<MediaKey, Core::FileLocation>> _fileLocationPairs;
	QMap<MediaKey, MediaKey> _fileLocationAliases;

	FileKey _locationsKey = 0;
	FileKey _trustedBotsKey = 0;
	FileKey _installedStickersKey = 0;
	FileKey _featuredStickersKey = 0;
	FileKey _recentStickersKey = 0;
	FileKey _favedStickersKey = 0;
	FileKey _archivedStickersKey = 0;
	FileKey _savedGifsKey = 0;
	FileKey _recentStickersKeyOld = 0;
	FileKey _legacyBackgroundKeyDay = 0;
	FileKey _legacyBackgroundKeyNight = 0;
	FileKey _settingsKey = 0;
	FileKey _recentHashtagsAndBotsKey = 0;
	FileKey _exportSettingsKey = 0;

	qint64 _cacheTotalSizeLimit = 0;
	qint64 _cacheBigFileTotalSizeLimit = 0;
	qint32 _cacheTotalTimeLimit = 0;
	qint32 _cacheBigFileTotalTimeLimit = 0;

	base::flat_set<uint64> _trustedBots;
	bool _trustedBotsRead = false;
	bool _readingUserSettings = false;
	bool _recentHashtagsAndBotsWereRead = false;

	int _oldMapVersion = 0;

	base::Timer _writeMapTimer;
	base::Timer _writeLocationsTimer;
	bool _mapChanged = false;
	bool _locationsChanged = false;

};

} // namespace Storage
