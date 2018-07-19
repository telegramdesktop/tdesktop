/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/basic_types.h"
#include "storage/file_download.h"
#include "auth_session.h"

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
FileLocation readFileLocation(MediaKey location, bool check = true);

void writeImage(const StorageKey &location, const ImagePtr &img);
void writeImage(const StorageKey &location, const StorageImageSaved &jpeg, bool overwrite = true);
TaskId startImageLoad(const StorageKey &location, mtpFileLoader *loader);
bool willImageLoad(const StorageKey &location);
int32 hasImages();
qint64 storageImagesSize();

void writeStickerImage(const StorageKey &location, const QByteArray &data, bool overwrite = true);
TaskId startStickerImageLoad(const StorageKey &location, mtpFileLoader *loader);
bool willStickerImageLoad(const StorageKey &location);
bool copyStickerImage(const StorageKey &oldLocation, const StorageKey &newLocation);
int32 hasStickers();
qint64 storageStickersSize();

void writeAudio(const StorageKey &location, const QByteArray &data, bool overwrite = true);
TaskId startAudioLoad(const StorageKey &location, mtpFileLoader *loader);
bool willAudioLoad(const StorageKey &location);
bool copyAudio(const StorageKey &oldLocation, const StorageKey &newLocation);
int32 hasAudios();
qint64 storageAudiosSize();

void writeWebFile(const QString &url, const QByteArray &data, bool overwrite = true);
TaskId startWebFileLoad(const QString &url, webFileLoader *loader);
bool willWebFileLoad(const QString &url);
int32 hasWebFiles();
qint64 storageWebFilesSize();

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

void writeBackground(int32 id, const QImage &img);
bool readBackground();

void writeTheme(const Window::Theme::Saved &saved);
void clearTheme();
bool copyThemeColorsToPalette(const QString &file);
Window::Theme::Saved readThemeAfterSwitch();

void writeLangPack();

void writeRecentHashtagsAndBots();
void readRecentHashtagsAndBots();
void saveRecentSentHashtags(const QString &text);
void saveRecentSearchHashtags(const QString &text);

void WriteExportSettings(const Export::Settings &settings);
Export::Settings ReadExportSettings();

void addSavedPeer(PeerData *peer, const QDateTime &position);
void removeSavedPeer(PeerData *peer);
void readSavedPeers();

void writeReportSpamStatuses();

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
