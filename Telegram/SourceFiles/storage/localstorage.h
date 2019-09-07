/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "storage/file_download.h"
#include "storage/cache/storage_cache_database.h"
#include "storage/localimageloader.h"
#include "main/main_session.h"

namespace Data {
class WallPaper;
} // namespace Data

namespace Lang {
struct Language;
} // namespace Lang

namespace Storage {
class EncryptionKey;
} // namespace Storage

namespace Window {
namespace Theme {
struct Saved;
} // namespace Theme
} // namespace Window

namespace Export {
struct Settings;
} // namespace Export

namespace Local {

void start();
void finish();

void readSettings();
void writeSettings();
void writeUserSettings();
void writeMtpData();

void writeAutoupdatePrefix(const QString &prefix);
QString readAutoupdatePrefix();

void reset();

bool checkPasscode(const QByteArray &passcode);
void setPasscode(const QByteArray &passcode);

enum ClearManagerTask {
	ClearManagerAll = 0xFFFF,
	ClearManagerDownloads = 0x01,
	ClearManagerStorage = 0x02,
};

struct ClearManagerData;
class ClearManager : public QObject {
	Q_OBJECT

public:
	ClearManager();
	bool addTask(int task);
	bool hasTask(ClearManagerTask task);
	void start();
	void stop();

signals:
	void succeed(int task, void *manager);
	void failed(int task, void *manager);

private slots:
	void onStart();

private:
	~ClearManager();

	ClearManagerData *data;

};

enum ReadMapState {
	ReadMapFailed = 0,
	ReadMapDone = 1,
	ReadMapPassNeeded = 2,
};
ReadMapState readMap(const QByteArray &pass);
int32 oldMapVersion();

int32 oldSettingsVersion();

struct MessageDraft {
	MessageDraft(MsgId msgId = 0, TextWithTags textWithTags = TextWithTags(), bool previewCancelled = false)
		: msgId(msgId)
		, textWithTags(textWithTags)
		, previewCancelled(previewCancelled) {
	}
	MsgId msgId;
	TextWithTags textWithTags;
	bool previewCancelled;
};
void writeDrafts(const PeerId &peer, const MessageDraft &localDraft, const MessageDraft &editDraft);
void readDraftsWithCursors(History *h);
void writeDraftCursors(const PeerId &peer, const MessageCursor &localCursor, const MessageCursor &editCursor);
bool hasDraftCursors(const PeerId &peer);
bool hasDraft(const PeerId &peer);

void writeFileLocation(MediaKey location, const FileLocation &local);
FileLocation readFileLocation(MediaKey location);
void removeFileLocation(MediaKey location);

Storage::EncryptionKey cacheKey();
QString cachePath();
Storage::Cache::Database::Settings cacheSettings();
void updateCacheSettings(
	Storage::Cache::Database::SettingsUpdate &update,
	Storage::Cache::Database::SettingsUpdate &updateBig);

Storage::EncryptionKey cacheBigFileKey();
QString cacheBigFilePath();
Storage::Cache::Database::Settings cacheBigFileSettings();

void countVoiceWaveform(DocumentData *document);

void cancelTask(TaskId id);

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
int32 countStickersHash(bool checkOutdatedInfo = false);
int32 countRecentStickersHash();
int32 countFavedStickersHash();
int32 countFeaturedStickersHash();

void writeSavedGifs();
void readSavedGifs();
int32 countSavedGifsHash();

void writeBackground(const Data::WallPaper &paper, const QImage &image);
bool readBackground();

void writeTheme(const Window::Theme::Saved &saved);
void clearTheme();
bool copyThemeColorsToPalette(const QString &destination);
Window::Theme::Saved readThemeAfterSwitch();

void writeLangPack();
void pushRecentLanguage(const Lang::Language &language);
std::vector<Lang::Language> readRecentLanguages();
void saveRecentLanguages(const std::vector<Lang::Language> &list);
void removeRecentLanguage(const QString &id);

void writeRecentHashtagsAndBots();
void readRecentHashtagsAndBots();
void saveRecentSentHashtags(const QString &text);
void saveRecentSearchHashtags(const QString &text);

void WriteExportSettings(const Export::Settings &settings);
Export::Settings ReadExportSettings();

void writeSelf();
void readSelf(const QByteArray &serialized, int32 streamVersion);

void makeBotTrusted(UserData *bot);
bool isBotTrusted(UserData *bot);

bool encrypt(const void *src, void *dst, uint32 len, const void *key128);
bool decrypt(const void *src, void *dst, uint32 len, const void *key128);

namespace internal {

class Manager : public QObject {
	Q_OBJECT

public:
	Manager();

	void writeMap(bool fast);
	void writingMap();
	void writeLocations(bool fast);
	void writingLocations();
	void finish();

public slots:
	void mapWriteTimeout();
	void locationsWriteTimeout();

private:
	QTimer _mapWriteTimer;
	QTimer _locationsWriteTimer;

};

} // namespace internal
} // namespace Local
