// This file is part of mokaGram, https://github.com/ecronxy/mokagram
// (c) 2026 @ecronx (https://t.me/ecronx). GPLv3 with the attribution term
// of MOKAGRAM_NOTICE.md (section 7(b)): keep this notice when copying.
// mokagram-id: c7df01d5
// Mokagram: источник музыки SoundCloud через неофициальный api-v2.
// client_id берётся из публичных JS-файлов сайта, кэшируется и обновляется при 401/403.
#pragma once

#include "base/basic_types.h"
#include "rpl/producer.h"

#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtGui/QImage>
#include <variant>
#include <vector>

namespace Mokagram::Music {

struct Track {
	QString id;
	QString title;
	QString artist;
	QString artworkUrl;      // 100x100, для крупной заменить на t500x500
	QString permalink;
	QString transcodingUrl;  // выбранный вариант потока (progressive mp3 или hls)
	QString authorization;   // track_authorization
	QString extension;       // "mp3" или "m4a"
	int durationMs = 0;
	bool hls = false;
	bool unavailable = false;
	// Для рекомендаций.
	QString artistId;
	QString artistArtworkUrl;
	QString genre;
	QStringList tags;
	qint64 playbackCount = 0;
	qint64 likesCount = 0;
	qint64 createdAt = 0;    // unix time
	QString uploaderName;
};

[[nodiscard]] QStringList TrackArtistNames(const Track &track);

struct SearchResult {
	std::vector<Track> tracks;
	QString error;
	int nextOffset = 0;      // 0: больше нет
};

// Все колбэки вызываются в главном потоке. Запросы можно звать только из главного потока.
void Search(
	const QString &query,
	int offset,
	Fn<void(SearchResult)> done);

// Достаёт свежие ссылки на поток по id (нужно для сохранённых лайков: ссылки живут недолго).
void Resolve(
	const Track &track,
	Fn<void(Track, const QString &error)> done);

// Похожие на трек.
void Related(
	const QString &trackId,
	int limit,
	Fn<void(std::vector<Track>)> done);

struct RelatedPage {
	std::vector<Track> tracks;
	QString next;
	QString error;
};

void LoadRelatedPage(
	const QString &trackId,
	const QString &cursor,
	Fn<void(RelatedPage)> done);

// Последние треки исполнителя.
void UserTracks(
	const QString &userId,
	int limit,
	Fn<void(std::vector<Track>)> done);

struct ArtistInfo {
	QString id;
	QString name;
	QString avatarUrl;
	QString bannerUrl;
};

// Публичный профиль исполнителя SoundCloud, включая аватар и обложку страницы.
void LoadArtistInfo(
	const QString &userId,
	Fn<void(ArtistInfo)> done);

void ResolveTrackArtist(
	const Track &track,
	const QString &name,
	Fn<void(ArtistInfo)> done);

enum class ReleaseKind {
	Unknown,
	Album,
	Single,
	Ep,
	Compilation,
};

struct Album {
	QString id;
	QString title;
	QString artist;
	QString artworkUrl;
	int trackCount = 0;
	QString artistId;
	ReleaseKind releaseKind = ReleaseKind::Unknown;
	int releaseYear = 0;
};

enum class SearchCategory { All, Tracks, Playlists, Albums, Artists };

using CatalogItem = std::variant<Track, Album, ArtistInfo>;

// Разнородные результаты в порядке релевантности SoundCloud.
void SearchCatalog(
	const QString &query,
	SearchCategory category,
	Fn<void(std::vector<CatalogItem>, QString error)> done);

// Альбомы по запросу.
void SearchAlbums(
	const QString &query,
	int limit,
	Fn<void(std::vector<Album>, QString error)> done);

// Подборки из раздела discovery SoundCloud, если они доступны аккаунту.
void LoadDiscoverPlaylists(
	Fn<void(std::vector<Album>, QString error)> done);

// Публичный чарт новых популярных треков SoundCloud.
void LoadTrending(
	int limit,
	Fn<void(std::vector<Track>, QString error)> done);

// Альбомы, в которые входит трек.
void TrackAlbums(
	const QString &trackId,
	Fn<void(std::vector<Album>)> done);

// Треки альбома по порядку: недостающие в ответе подгружаются отдельно по id.
void AlbumTracks(
	const QString &albumId,
	Fn<void(std::vector<Track>, QString error)> done);

// Поиск с произвольным лимитом (для подбора кандидатов).
void SearchLimited(
	const QString &query,
	int limit,
	Fn<void(std::vector<Track>)> done);

// Скачивает трек (HLS склеивается) в кэш и отдаёт путь к файлу. Уже скачанный отдаётся сразу.
void Download(
	const Track &track,
	Fn<void(const QString &path, const QString &error)> done);

// Картинка по ссылке (обложки). Результат кэшируется в памяти.
void LoadImage(const QString &url, Fn<void(QImage)> done);

// Обложка трека SoundCloud по id документа клиента (пока не загружена, пустая).
[[nodiscard]] QImage CoverForDocument(uint64 documentId);
void SetCover(uint64 documentId, QImage image);
[[nodiscard]] rpl::producer<> CoversChanged();
[[nodiscard]] uint64 DocumentIdForTrack(const QString &trackId);

// Папка кэша скачанных треков.
[[nodiscard]] QString CacheDir();

struct TrackComment {
	QString id;
	QString author;
	QString avatarUrl;
	QString body;
	qint64 timestampMs = -1;
};

struct TrackCommentsPage {
	std::vector<TrackComment> comments;
	QString next;
	QString error;
};

void LoadTrackComments(
	const QString &trackId,
	const QString &cursor,
	Fn<void(TrackCommentsPage)> done);

namespace Account {

// Аккаунт SoundCloud. Вход идёт в браузере через обычный OAuth сайта: после входа
// токен остаётся в файле на диске (как client_id), чтобы лайки синхронизировались
// при запуске. В API-запросах передаётся в заголовке Authorization.
struct Info {
	QString id;
	QString username;
	QString avatarUrl;
};

[[nodiscard]] bool IsLoggedIn();
[[nodiscard]] Info User();

void Login(const QString &accessToken, Fn<void(QString)> done);
void Logout();
[[nodiscard]] rpl::producer<> Changes();
[[nodiscard]] rpl::producer<QString> LikeWriteErrors();

// Лайки из аккаунта: страница по nextHref (пусто — с начала).
struct LikesPage {
	std::vector<Track> tracks;
	QString next;
	QString error;
};

void LoadLikes(const QString &nextHref, Fn<void(LikesPage)> done);

// Двусторонняя синхронизация: новые лайки из аккаунта в локальные, локальные
// лайки не из аккаунта отправляются туда же. Дальше поднимает LikesChanged.
void SyncLikes(
	Fn<void(int remote, int pushed, QString error)> done = nullptr,
	bool refreshed = false);

// Импортирует собственные sets, лайкнутые плейлисты и треки в «Плейлисты».
void SyncPlaylists(
	Fn<void(int playlists, int tracks, QString error)> done = nullptr);

// Ставит лайк в аккаунте при входе. Для локального лайка это фоновая операция.
void SetTrackLiked(
	const QString &trackId,
	bool liked,
	bool refreshed = false);

// Локальные лайки, пришедшие из подключённого аккаунта: их не отправляем обратно,
// а снятый в аккаунте лайк убирается и локально. Возвращает число новых треков.
[[nodiscard]] int MergeRemoteLikes(const std::vector<Track> &remote);
void ClearRemoteLikes();

} // namespace Account

} // namespace Mokagram::Music
