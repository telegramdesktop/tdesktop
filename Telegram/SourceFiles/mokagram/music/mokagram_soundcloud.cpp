// This file is part of mokaGram, https://github.com/ecronxy/mokagram
// (c) 2026 @ecronx (https://t.me/ecronx). GPLv3 with the attribution term
// of MOKAGRAM_NOTICE.md (section 7(b)): keep this notice when copying.
// mokagram-id: cfb896c6
#include "mokagram/music/mokagram_soundcloud.h"

#include "mokagram/lang/mokagram_lang.h"
#include "mokagram/music/mokagram_music_library.h"
#include "mokagram/music/mokagram_soundcloud_login.h"

#include "core/application.h"
#include "rpl/event_stream.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <map>
#include <memory>
#include <array>
#include <deque>
#include <set>
#include <algorithm>
#include <tuple>
#include <utility>

namespace Mokagram::Music {
namespace {

constexpr auto kApi = "https://api-v2.soundcloud.com";
constexpr auto kPublicApi = "https://api.soundcloud.com";
constexpr auto kHome = "https://soundcloud.com/";
constexpr auto kPageSize = 20;
constexpr auto kMaxAudioBytes = 200 * 1024 * 1024; // предел на размер трека
constexpr auto kMaxImageBytes = 32 * 1024 * 1024;  // предел на размер обложки
constexpr auto kMaxHlsSegments = 512;              // предел числа сегментов HLS
constexpr auto kConcurrentHlsSegments = 3;
// Кэш обложек в памяти: LRU по числу и по размеру QImage, без полного clear().
constexpr auto kImageCacheCount = 200;
constexpr auto kImageCacheBytes = qint64(48) * 1024 * 1024; // ~200 обложек t500x500
constexpr auto kCoverCacheCount = 200;
constexpr auto kCoverCacheBytes = qint64(48) * 1024 * 1024;
constexpr auto kArtistInfoCacheCount = 256;

// Дисковый кэш треков: держим не больше kAudioCacheBytes и не больше kAudioCacheFiles
// файлов, удаляя самые давно использованные. mtime файла обновляется при каждом
// обращении, поэтому он же служит ключом LRU.
constexpr auto kAudioCacheBytes = qint64(1024) * 1024 * 1024;
constexpr auto kAudioCacheFiles = 400;
constexpr auto kUserAgent = "Mozilla/5.0 (X11; Linux x86_64; rv:128.0) Gecko/20100101 Firefox/128.0";
constexpr auto kMaxLikesPages = 50;   // предел страниц лайков при синхронизации
constexpr auto kMaxPlaylistPages = 50;
constexpr auto kMaxSyncedPlaylists = 200;
constexpr auto kMaxPushedLikes = 200; // за один проход отправляем не больше
constexpr auto kConcurrentImages = 6;

QNetworkAccessManager &Net() {
	static auto manager = new QNetworkAccessManager();
	return *manager;
}

// Разрешаем только https на известных хостах: ответы API не должны уводить загрузку
// на file:// или чужой сервер. sndcdn.com и soundcloud.cloud — домены самого SoundCloud.
[[nodiscard]] bool SafeMediaUrl(const QUrl &url) {
	const auto host = url.host();
	return url.isValid()
		&& url.scheme() == u"https"_q
		&& url.userInfo().isEmpty()
		&& url.port(-1) == -1
		&& (host == u"api-v2.soundcloud.com"_q
			|| host == u"api.soundcloud.com"_q
			|| host == u"soundcloud.com"_q
			|| host.endsWith(u".sndcdn.com"_q)
			|| host.endsWith(u".soundcloud.cloud"_q));
}

// id трека — каноничное положительное число: всё остальное могло прийти из чужого ответа.
[[nodiscard]] bool ValidTrackId(const QString &id) {
	const auto number = id.toULongLong();
	return number != 0 && QString::number(number) == id;
}

[[nodiscard]] QString ResourceId(const QJsonObject &object, const QString &field) {
	auto raw = object.value(field).toString();
	if (raw.isEmpty() && object.value(field).isDouble()) {
		raw = QString::number(object.value(field).toVariant().toLongLong());
	}
	if (raw.isEmpty() && field == u"id"_q) {
		raw = object.value("urn").toString();
	}
	const auto match = QRegularExpression(u"(?:^|:)([0-9]+)$"_q).match(raw);
	if (!match.hasMatch()) {
		return QString();
	}
	const auto number = match.captured(1).toULongLong();
	return number ? QString::number(number) : QString();
}

[[nodiscard]] QString ClientIdFile() {
	return CacheDir() + u"/client_id.txt"_q;
}

QString &ClientId() {
	static auto id = [] {
		auto file = QFile(ClientIdFile());
		return file.open(QIODevice::ReadOnly)
			? QString::fromUtf8(file.readAll()).trimmed()
			: QString();
	}();
	return id;
}

[[nodiscard]] QString SessionFile() {
	return CacheDir() + u"/session.json"_q;
}

[[nodiscard]] bool ValidSessionToken(const QString &token) {
	// oauth_token SoundCloud — JWT из трёх частей через точку. Иногда бывает
	// старый вид "2-293746-185937845-AbCd". Токен уходит в Authorization, поэтому
	// пускаем только безопасные символы и ограничиваем длину.
	if (token.isEmpty() || token.size() > 4096) {
		return false;
	}
	static const auto pattern = QRegularExpression(
		u"^([A-Za-z0-9_-]{10,}\\.[A-Za-z0-9_-]{10,}\\.[A-Za-z0-9_-]{10,}"
		u"|[0-9][0-9]*-[0-9]+-[0-9]+-[A-Za-z0-9_-]{6,})$"_q);
	return pattern.match(token).hasMatch();
}

// Токен входа; живёт в файле рядом с client_id, как и он.
QString &AccountToken() {
	static auto token = [] {
		auto file = QFile(SessionFile());
		if (!file.open(QIODevice::ReadOnly)) {
			return QString();
		}
		const auto object = QJsonDocument::fromJson(file.readAll()).object();
		const auto result = object.value("token").toString();
		return ValidSessionToken(result) ? result : QString();
	}();
	return token;
}

void SaveSessionFile(const QString &token, const QJsonObject &user) {
	QDir().mkpath(CacheDir());
	auto file = QFile(SessionFile());
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return;
	}
	file.write(QJsonDocument(QJsonObject{
		{ "token", token },
		{ "user", user },
	}).toJson(QJsonDocument::Compact));
}

void RemoveSessionFile() {
	QFile::remove(SessionFile());
}

QNetworkReply *Get(
		const QUrl &url,
		qint64 limit = 0,
		const QString &accessToken = QString(),
		int timeoutMs = 20000) {
	auto request = QNetworkRequest(url);
	request.setRawHeader("User-Agent", kUserAgent);
	if (!accessToken.isEmpty()) {
		request.setRawHeader(
			"Authorization",
			QByteArray("OAuth ") + accessToken.toUtf8());
	}
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setTransferTimeout(timeoutMs);
	const auto reply = Net().get(request);
	if (limit > 0) {
		// Слишком большие ответы обрываем: readAll() не должен собирать гигабайты.
		reply->setReadBufferSize(limit + 1);
		QObject::connect(reply, &QNetworkReply::downloadProgress, reply,
			[=](qint64 received, qint64 total) {
				if (received > limit || total > limit) {
					reply->abort();
				}
			});
	}
	return reply;
}

// Ищет client_id в скриптах главной страницы (в последних скриптах он обычно есть).
void FetchClientId(Fn<void(QString)> done) {
	const auto home = Get(QUrl(QString::fromLatin1(kHome)));
	QObject::connect(home, &QNetworkReply::finished, [=] {
		const auto html = QString::fromUtf8(home->readAll());
		home->deleteLater();
		auto scripts = QStringList();
		auto match = QRegularExpression(
			uR"(https://a-v2\.sndcdn\.com/assets/[^"']+\.js)"_q
		).globalMatch(html);
		while (match.hasNext()) {
			scripts.push_back(match.next().captured());
		}
		if (scripts.isEmpty()) {
			done({});
			return;
		}
		std::reverse(scripts.begin(), scripts.end());
		const auto next = std::make_shared<Fn<void(int)>>();
		*next = [=](int index) {
			if (index >= scripts.size()) {
				done({});
				return;
			}
			// Ссылки на скрипты вытащены из HTML главной: проверяем перед загрузкой.
			if (!SafeMediaUrl(QUrl(scripts[index]))) {
				(*next)(index + 1);
				return;
			}
			const auto reply = Get(QUrl(scripts[index]));
			QObject::connect(reply, &QNetworkReply::finished, [=] {
				const auto text = QString::fromUtf8(reply->readAll());
				reply->deleteLater();
				const auto found = QRegularExpression(
					uR"(client_id["']?\s*[:=]\s*["']([A-Za-z0-9]{32})["'])"_q).match(text);
				if (found.hasMatch()) {
					done(found.captured(1));
				} else {
					(*next)(index + 1);
				}
			});
		};
		(*next)(0);
	});
}

void EnsureClientId(Fn<void(bool)> done, bool force = false) {
	if (!force && !ClientId().isEmpty()) {
		done(true);
		return;
	}
	FetchClientId([=](QString id) {
		if (id.isEmpty()) {
			done(false);
			return;
		}
		ClientId() = id;
		QDir().mkpath(CacheDir());
		auto file = QFile(ClientIdFile());
		if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			file.write(id.toUtf8());
		}
		done(true);
	});
}

// client_id нужен всегда: по нему API отдаёт список треков и поток.
// OAuth-токен аккаунта передаётся в заголовке Authorization.
void EnsureAnyClientId(Fn<void(bool)> done, bool force) {
	EnsureClientId(done, force);
}

[[nodiscard]] QString ApiErrorDetail(const QByteArray &data) {
	const auto root = QJsonDocument::fromJson(data).object();
	auto message = root.value("message").toString().trimmed();
	if (message.isEmpty()) {
		message = root.value("error_message").toString().trimmed();
	}
	if (message.isEmpty()) {
		const auto errors = root.value("errors").toArray();
		if (!errors.isEmpty()) {
			message = errors.first().toObject()
				.value("error_message").toString().trimmed();
		}
	}
	return message.left(200);
}

// GET с автоматическим обновлением client_id при 401/403 (один повтор).
void ApiGet(
		const QString &base,
		QUrlQuery query,
		Fn<void(QByteArray, QString)> done,
		bool retried = false,
		bool anonymous = false) {
	const auto officialApi = QUrl(base).host().toLower()
		== u"api.soundcloud.com"_q;
	const auto send = [=](bool ok) {
		LOG(("[moka-dbg] ApiGet send ok %1 %2").arg(ok ? 1 : 0).arg(base.left(80)));
		if (!ok) {
			done({}, QString::fromUtf8(Mokagram::Lang::Tr("не удалось получить client_id SoundCloud")));
			return;
		}
		auto url = QUrl(base);
		auto copy = QUrlQuery(url);
		for (const auto &item : query.queryItems()) {
			copy.removeAllQueryItems(item.first);
			copy.addQueryItem(item.first, item.second);
		}
		if (officialApi) {
			copy.removeAllQueryItems(u"client_id"_q);
		} else if (!ClientId().isEmpty()) {
			copy.removeAllQueryItems(u"client_id"_q);
			copy.addQueryItem(u"client_id"_q, ClientId());
		}
		url.setQuery(copy);
		// SoundCloud moved access tokens to the Authorization header.
		const auto reply = Get(url, 0, anonymous ? QString() : AccountToken());
		QObject::connect(reply, &QNetworkReply::finished, [=] {
			const auto status = reply->attribute(
				QNetworkRequest::HttpStatusCodeAttribute).toInt();
			const auto data = reply->readAll();
			const auto error = reply->error();
			reply->deleteLater();
			// Токен входа протух: поиск и стрим работают и без него, поэтому
			// сразу повторяем без токена, а client_id трогаем только если и так отказ.
			if ((status == 401 || status == 403)
				&& !anonymous
				&& !officialApi
				&& !AccountToken().isEmpty()) {
				ApiGet(base, query, done, retried, true);
				return;
			}
			if ((status == 401 || status == 403)
				&& !retried
				&& !officialApi) {
				EnsureAnyClientId([=](bool refreshed) {
					if (!refreshed) {
						done({}, QString::fromUtf8(Mokagram::Lang::Tr("client_id SoundCloud устарел")));
						return;
					}
					ApiGet(base, query, done, true);
				}, true);
				return;
			}
			if (status == 401) {
				auto message = QString::fromUtf8(Mokagram::Lang::Tr(
					"SoundCloud отклонил токен авторизации (HTTP 401)."));
				const auto detail = ApiErrorDetail(data);
				if (!detail.isEmpty()) message += u" "_q + detail;
				done({}, std::move(message));
				return;
			}
			if (status == 403) {
				auto message = QString::fromUtf8(Mokagram::Lang::Tr(
					"SoundCloud запретил запрос (HTTP 403)."));
				const auto detail = ApiErrorDetail(data);
				if (!detail.isEmpty()) message += u" "_q + detail;
				done({}, std::move(message));
				return;
			}
			if (status == 429) {
				done({}, QString::fromUtf8(Mokagram::Lang::Tr(
					"SoundCloud временно ограничил запросы (HTTP 429). Попробуй позже.")));
				return;
			}
			if (error != QNetworkReply::NoError) {
				done({}, QString::fromUtf8(Mokagram::Lang::Tr("сеть: %1 (код %2)")).arg(
					reply->errorString()).arg(status));
				return;
			}
			done(data, QString());
		});
	};
	if (officialApi) {
		send(true);
	} else {
		EnsureAnyClientId(send, false);
	}
}

[[nodiscard]] int AacBitrate(const QJsonObject &transcoding) {
	static const auto pattern = QRegularExpression(u"^aac_([1-9][0-9]*)k(?:_|$)"_q);
	const auto preset = transcoding.value("preset").toString();
	const auto bitrate = pattern.match(preset).captured(1).toInt();
	if (bitrate > 0) {
		return bitrate;
	}
	const auto quality = transcoding.value("quality").toString();
	if (quality == u"hq"_q) {
		return 256;
	} else if (quality == u"sq"_q) {
		return 160;
	}
	return 96;
}

struct TrackStream {
	QString url;
	QString extension;
	bool hls = false;
	std::tuple<int, bool, bool> rank;
};

[[nodiscard]] std::vector<TrackStream> ParseStreams(const QJsonObject &object) {
	auto result = std::vector<TrackStream>();
	for (const auto &value : object.value("media").toObject()
			.value("transcodings").toArray()) {
		const auto item = value.toObject();
		if (item.value("snipped").toBool()) {
			continue; // Превью-обрезка.
		}
		const auto format = item.value("format").toObject();
		const auto protocol = format.value("protocol").toString();
		const auto mime = format.value("mime_type").toString().section(';', 0, 0);
		const auto hls = (protocol == u"hls"_q);
		const auto aac = hls && (mime == u"audio/mp4"_q);
		const auto mp3 = (hls || protocol == u"progressive"_q)
			&& (mime == u"audio/mpeg"_q);
		if (!aac && !mp3) {
			continue;
		}
		const auto url = item.value("url").toString();
		if (SafeMediaUrl(QUrl(url))) {
			result.push_back({
				.url = url,
				.extension = aac ? u"m4a"_q : u"mp3"_q,
				.hls = hls,
				.rank = std::tuple(aac ? AacBitrate(item) : 128, aac, !hls),
			});
		}
	}
	std::stable_sort(result.begin(), result.end(), [](const TrackStream &a, const TrackStream &b) {
		return a.rank > b.rank;
	});
	return result;
}

[[nodiscard]] Track ParseTrack(const QJsonObject &object) {
	auto result = Track();
	result.id = ResourceId(object, u"id"_q);
	result.title = object.value("title").toString();
	const auto user = object.value("user").toObject();
	result.uploaderName = user.value("username").toString().trimmed();
	result.artist = object.value("publisher_metadata").toObject()
		.value("artist").toString().trimmed();
	if (result.artist.isEmpty()) {
		result.artist = object.value("metadata_artist").toString().trimmed();
	}
	if (result.artist.isEmpty()) {
		result.artist = result.uploaderName;
	}
	result.artworkUrl = object.value("artwork_url").toString();
	if (result.artworkUrl.isEmpty()) {
		result.artworkUrl = user.value("avatar_url").toString();
	}
	result.permalink = object.value("permalink_url").toString();
	result.durationMs = object.value("duration").toInt();
	result.authorization = object.value("track_authorization").toString();
	result.artistId = ResourceId(user, u"id"_q);
	result.artistArtworkUrl = user.value("avatar_url").toString();
	result.genre = object.value("genre").toString().trimmed().toLower();
	result.playbackCount = object.value("playback_count").toVariant().toLongLong();
	result.likesCount = object.value("likes_count").toVariant().toLongLong();
	result.createdAt = QDateTime::fromString(
		object.value("created_at").toString(),
		Qt::ISODate).toSecsSinceEpoch();
	{
		// tag_list: слова через пробел, составные теги в кавычках.
		const auto raw = object.value("tag_list").toString();
		auto match = QRegularExpression(u"\"([^\"]+)\"|(\\S+)"_q).globalMatch(raw);
		while (match.hasNext()) {
			const auto m = match.next();
			const auto tag = (m.captured(1).isEmpty() ? m.captured(2) : m.captured(1)).toLower();
			if (!tag.isEmpty() && result.tags.size() < 12) {
				result.tags.push_back(tag);
			}
		}
	}
	const auto streams = ParseStreams(object);
	if (!streams.empty()) {
		result.transcodingUrl = streams.front().url;
		result.extension = streams.front().extension;
		result.hls = streams.front().hls;
	}
	return result;
}

[[nodiscard]] QString ArtistBannerUrl(const QJsonObject &object) {
	for (const auto &key : { "cover_url", "banner_url", "header_url" }) {
		const auto url = object.value(key).toString();
		if (SafeMediaUrl(QUrl(url))) {
			return url;
		}
	}
	const auto raw = object.value("visuals");
	const auto visuals = raw.isObject()
		? raw.toObject().value("visuals").toArray()
		: raw.toArray();
	auto result = QString();
	auto bestArea = -1;
	for (const auto &entry : visuals) {
		const auto visual = entry.toObject();
		auto url = visual.value("visual_url").toString();
		if (url.isEmpty()) {
			url = visual.value("url").toString();
		}
		if (!SafeMediaUrl(QUrl(url))) {
			continue;
		}
		const auto area = visual.value("width").toInt()
			* visual.value("height").toInt();
		if (area > bestArea) {
			result = std::move(url);
			bestArea = area;
		}
	}
	return result;
}

[[nodiscard]] ArtistInfo ParseArtistInfo(
		const QJsonObject &object,
		const QString &requestedId) {
	if (object.isEmpty()) {
		return {};
	}
	auto result = ArtistInfo();
	result.id = ResourceId(object, u"id"_q);
	if (result.id.isEmpty()) {
		result.id = requestedId;
	}
	result.name = object.value("username").toString();
	result.avatarUrl = object.value("avatar_url").toString();
	if (!SafeMediaUrl(QUrl(result.avatarUrl))) {
		result.avatarUrl.clear();
	}
	result.bannerUrl = ArtistBannerUrl(object);
	return result;
}

[[nodiscard]] std::map<QString, ArtistInfo> &ArtistInfoCache() {
	static auto result = std::map<QString, ArtistInfo>();
	return result;
}
[[nodiscard]] std::deque<QString> &ArtistInfoOrder() {
	static auto result = std::deque<QString>();
	return result;
}
void TouchArtistInfo(const QString &id) {
	auto &order = ArtistInfoOrder();
	const auto i = std::find(order.begin(), order.end(), id);
	if (i != order.end()) {
		order.erase(i);
	}
	order.push_back(id);
}
void InsertArtistInfo(const QString &id, ArtistInfo info) {
	auto &cache = ArtistInfoCache();
	cache[id] = std::move(info);
	TouchArtistInfo(id);
	while (ArtistInfoOrder().size() > kArtistInfoCacheCount) {
		cache.erase(ArtistInfoOrder().front());
		ArtistInfoOrder().pop_front();
	}
}

[[nodiscard]] std::map<QString, std::vector<Fn<void(ArtistInfo)>>> &PendingArtistInfo() {
	static auto result = std::map<QString, std::vector<Fn<void(ArtistInfo)>>>();
	return result;
}

[[nodiscard]] QString CachePath(const Track &track) {
	// id попадает в путь к файлу: нечисловой id не пропускаем.
	if (!ValidTrackId(track.id)) {
		return QString();
	}
	// Разные варианты потока могут иметь одинаковое расширение.
	const auto stream = QUrl(track.transcodingUrl).toEncoded(
		QUrl::RemoveQuery | QUrl::RemoveFragment);
	const auto key = QCryptographicHash::hash(stream, QCryptographicHash::Sha256)
		.toHex()
		.left(16);
	return CacheDir() + '/' + track.id + '.'
		+ QString::fromLatin1(key) + '.' + track.extension;
}

[[nodiscard]] QString LegacyAudioPath(const QString &id) {
	const auto prefix = CacheDir() + '/' + id + '.';
	for (const auto &extension : { u"m4a"_q, u"mp3"_q }) {
		const auto file = QFileInfo(prefix + extension);
		if (file.isFile() && file.size() > 0) {
			return file.filePath();
		}
	}
	return {};
}

// Отмечаем file как недавно использованный: mtime служит ключом LRU.
void TouchCacheFile(const QString &path) {
	QFile file(path);
	if (file.open(QIODevice::ReadWrite)) {
		file.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
	}
}

// Держим кэш в пределах квоты, удаляя самые давно использованные треки.
// QDir::Time ставит новые файлы первыми, поэтому сверх квоты удаляются старые.
// protectPath (только что скачанный трек) всегда остаётся.
void TrimAudioCache(const QString &protectPath) {
	const auto dir = QDir(CacheDir());
	const auto files = dir.entryInfoList(
		{ u"*.m4a"_q, u"*.mp3"_q }, QDir::Files, QDir::Time);
	auto total = qint64(0);
	auto count = 0;
	for (const auto &file : files) {
		if (file.absoluteFilePath() == protectPath) {
			continue;
		}
		if (count < kAudioCacheFiles
			&& total + file.size() <= kAudioCacheBytes) {
			++count;
			total += file.size();
			continue;
		}
		QFile::remove(file.absoluteFilePath());
	}
}

void FinishFile(
		const QString &path,
		const std::vector<QByteArray> &parts,
		Fn<void(const QString &, const QString &)> done) {
	QDir().mkpath(CacheDir());
	auto file = QSaveFile(path);
	if (file.open(QIODevice::WriteOnly)) {
		const auto written = std::all_of(parts.begin(), parts.end(), [&](const QByteArray &part) {
			return file.write(part) == part.size();
		});
		if (written && file.commit()) {
			done(path, QString());
			return;
		}
	}
	done({}, QString::fromUtf8(Mokagram::Lang::Tr("не удалось записать файл")));
}

struct HlsDownload {
	QString path;
	QStringList urls;
	std::vector<QByteArray> parts;
	Fn<void(const QString &, const QString &)> done;
	qsizetype next = 0;
	qsizetype received = 0;
	qint64 bytes = 0;
};

void DownloadHlsSegment(const std::shared_ptr<HlsDownload> &job) {
	const auto index = job->next++;
	const auto reply = Get(QUrl(job->urls[index]), kMaxAudioBytes);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		if (!job->done) {
			return;
		}
		auto part = reply->readAll();
		job->bytes += part.size();
		if (reply->error() != QNetworkReply::NoError
			|| part.isEmpty()
			|| job->bytes > kMaxAudioBytes) {
			const auto done = std::exchange(job->done, {});
			done({}, QString::fromUtf8(Mokagram::Lang::Tr("сбой загрузки сегмента")));
			return;
		}
		job->parts[index] = std::move(part);
		if (++job->received == job->urls.size()) {
			FinishFile(job->path, job->parts, std::exchange(job->done, {}));
		} else if (job->next < job->urls.size()) {
			DownloadHlsSegment(job);
		}
	});
}

// Скачивает HLS: init-сегмент (EXT-X-MAP) и сегменты по порядку, склеивает в один файл.
void DownloadHls(
		const QString &playlistUrl,
		const QString &path,
		Fn<void(const QString &, const QString &)> done) {
	const auto reply = Get(QUrl(playlistUrl), kMaxAudioBytes);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		const auto text = QString::fromUtf8(reply->readAll());
		const auto base = reply->url();
		const auto failed = (reply->error() != QNetworkReply::NoError);
		reply->deleteLater();
		if (failed) {
			done({}, QString::fromUtf8(Mokagram::Lang::Tr("не загрузился плейлист")));
			return;
		}
		auto parts = QStringList();
		for (const auto &raw : text.split('\n')) {
			const auto line = raw.trimmed();
			if (line.startsWith(u"#EXT-X-MAP:"_q)) {
				const auto found = QRegularExpression(
					u"URI=\"([^\"]+)\""_q).match(line);
				if (found.hasMatch()) {
					parts.push_back(base.resolved(
						QUrl(found.captured(1))).toString());
				}
			} else if (!line.isEmpty() && !line.startsWith('#')) {
				parts.push_back(base.resolved(QUrl(line)).toString());
			}
		}
		if (parts.isEmpty()) {
			done({}, QString::fromUtf8(Mokagram::Lang::Tr("пустой плейлист")));
			return;
		}
		// Ссылки в плейлисте могли указать куда угодно: проверяем каждую.
		for (const auto &part : parts) {
			if (!SafeMediaUrl(QUrl(part))) {
				done({}, QString::fromUtf8(Mokagram::Lang::Tr("плейлист ведёт на чужой сервер")));
				return;
			}
		}
		if (parts.size() > kMaxHlsSegments) {
			done({}, QString::fromUtf8(Mokagram::Lang::Tr("слишком большой плейлист")));
			return;
		}
		const auto job = std::make_shared<HlsDownload>();
		job->path = path;
		job->parts.resize(parts.size());
		job->urls = std::move(parts);
		job->done = done;
		const auto concurrent = std::min<qsizetype>(
			kConcurrentHlsSegments,
			job->urls.size());
		for (auto i = qsizetype(0); i != concurrent; ++i) {
			DownloadHlsSegment(job);
		}
	});
}

} // namespace

namespace {

using ImageCallbacks = std::map<QString, std::vector<Fn<void(QImage)>>>;

[[nodiscard]] std::deque<QString> &QueuedImages() {
	static auto result = std::deque<QString>();
	return result;
}

int &ActiveImageRequests() {
	static auto result = 0;
	return result;
}

[[nodiscard]] std::map<QString, QImage> &ImageCache() {
	static auto result = std::map<QString, QImage>();
	return result;
}

// Порядок обращений к кэшу обложек: конец deque = недавно использованный.
[[nodiscard]] std::deque<QString> &ImageOrder() {
	static auto result = std::deque<QString>();
	return result;
}

qint64 &ImageCacheBytes() {
	static auto result = qint64(0);
	return result;
}

void TouchImage(const QString &url) {
	auto &order = ImageOrder();
	const auto i = std::find(order.begin(), order.end(), url);
	if (i != order.end()) {
		order.erase(i);
	}
	order.push_back(url);
}

void TrimImageCache() {
	auto &cache = ImageCache();
	auto &order = ImageOrder();
	while (!order.empty()
		&& (order.size() > kImageCacheCount
			|| ImageCacheBytes() > kImageCacheBytes)) {
		const auto url = std::move(order.front());
		order.pop_front();
		if (const auto i = cache.find(url); i != cache.end()) {
			ImageCacheBytes() -= i->second.sizeInBytes();
			cache.erase(i);
		}
	}
}

void InsertImage(const QString &url, QImage image) {
	auto &cache = ImageCache();
	if (const auto i = cache.find(url); i != cache.end()) {
		ImageCacheBytes() -= i->second.sizeInBytes();
		i->second = std::move(image);
	} else {
		cache.emplace(url, std::move(image));
	}
	ImageCacheBytes() += cache[url].sizeInBytes();
	TouchImage(url);
	TrimImageCache();
}

[[nodiscard]] ImageCallbacks &PendingImages() {
	static auto result = ImageCallbacks();
	return result;
}

void StartQueuedImageLoads();

void FinishImageRequest(const QString &url, QImage image) {
	if (!image.isNull()) {
		InsertImage(url, image);
	}
	ActiveImageRequests() = std::max(ActiveImageRequests() - 1, 0);
	auto callbacks = std::vector<Fn<void(QImage)>>();
	auto &pending = PendingImages();
	if (const auto i = pending.find(url); i != pending.end()) {
		callbacks = std::move(i->second);
		pending.erase(i);
	}
	StartQueuedImageLoads();
	for (const auto &callback : callbacks) {
		callback(image);
	}
}

void LoadImageAttempt(const QString &url, int attempt) {
	++ActiveImageRequests();
	const auto reply = Get(QUrl(url), kMaxImageBytes, QString(), 5000);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		const auto bytes = reply->readAll();
		auto image = QImage::fromData(bytes);
		reply->deleteLater();
		if (image.isNull() && attempt == 0) {
			// Освобождаем слот перед повтором: retry сам его займёт заново.
			// Иначе после нескольких неудач StartQueuedImageLoads встанет и
			// остальные обложки никогда не начнут грузиться.
			ActiveImageRequests() = std::max(ActiveImageRequests() - 1, 0);
			QTimer::singleShot(400, QCoreApplication::instance(), [=] {
				LoadImageAttempt(url, 1);
			});
			return;
		}
		FinishImageRequest(url, std::move(image));
	});
}

void StartQueuedImageLoads() {
	auto &queue = QueuedImages();
	while (ActiveImageRequests() < kConcurrentImages && !queue.empty()) {
		auto url = std::move(queue.front());
		queue.pop_front();
		if (PendingImages().find(url) == PendingImages().end()) {
			continue;
		}
		LoadImageAttempt(url, 0);
	}
}

} // namespace

void LoadImage(const QString &url, Fn<void(QImage)> done) {
	const auto &cache = ImageCache();
	if (const auto i = cache.find(url); i != cache.end()) {
		TouchImage(url);
		done(i->second);
		return;
	}
	if (url.isEmpty() || !SafeMediaUrl(QUrl(url))) {
		done(QImage());
		return;
	}
	auto &pending = PendingImages();
	if (const auto i = pending.find(url); i != pending.end()) {
		i->second.push_back(std::move(done));
		return;
	}
	pending[url].push_back(std::move(done));
	QueuedImages().push_back(url);
	StartQueuedImageLoads();
}

namespace {
std::map<uint64, QImage> &Covers() {
	static auto result = std::map<uint64, QImage>();
	return result;
}
std::deque<uint64> &CoverOrder() {
	static auto result = std::deque<uint64>();
	return result;
}
qint64 &CoversBytes() {
	static auto result = qint64(0);
	return result;
}
void TrimCovers() {
	auto &covers = Covers();
	auto &order = CoverOrder();
	while (!order.empty()
		&& (order.size() > kCoverCacheCount
			|| CoversBytes() > kCoverCacheBytes)) {
		const auto id = order.front();
		order.pop_front();
		if (const auto i = covers.find(id); i != covers.end()) {
			CoversBytes() -= i->second.sizeInBytes();
			covers.erase(i);
		}
	}
}
void InsertCover(uint64 documentId, QImage image) {
	auto &covers = Covers();
	if (const auto i = covers.find(documentId); i != covers.end()) {
		CoversBytes() -= i->second.sizeInBytes();
		i->second = std::move(image);
	} else {
		covers.emplace(documentId, std::move(image));
	}
	CoversBytes() += covers[documentId].sizeInBytes();
	auto &order = CoverOrder();
	const auto j = std::find(order.begin(), order.end(), documentId);
	if (j != order.end()) {
		order.erase(j);
	}
	order.push_back(documentId);
	TrimCovers();
}
} // namespace

QImage CoverForDocument(uint64 documentId) {
	const auto &covers = Covers();
	const auto i = covers.find(documentId);
	if (i == covers.end()) {
		return QImage();
	}
	auto &order = CoverOrder();
	const auto j = std::find(order.begin(), order.end(), documentId);
	if (j != order.end()) {
		order.erase(j);
		order.push_back(documentId);
	}
	return i->second;
}

namespace {
rpl::event_stream<> &CoversStream() {
	static auto result = rpl::event_stream<>();
	return result;
}
} // namespace

void SetCover(uint64 documentId, QImage image) {
	InsertCover(documentId, std::move(image));
	CoversStream().fire({});
}

rpl::producer<> CoversChanged() {
	return CoversStream().events();
}

uint64 DocumentIdForTrack(const QString &trackId) {
	const auto id = trackId.toULongLong();
	// Префикс занимает старшие биты: слишком длинный id переполнит сумму.
	if (id >= (1ULL << 56)) {
		return 0;
	}
	return 0x4D4F4B0000000000ULL + id;
}

namespace {

void CollectionRequest(
		const QString &path,
		QUrlQuery query,
		Fn<void(std::vector<Track>)> done) {
	ApiGet(
		QString::fromLatin1(kApi) + path,
		std::move(query),
		[=](QByteArray data, QString error) {
			auto result = std::vector<Track>();
			if (error.isEmpty()) {
				const auto root = QJsonDocument::fromJson(data).object();
				for (const auto &value : root.value("collection").toArray()) {
					auto track = ParseTrack(value.toObject());
					if (!track.transcodingUrl.isEmpty() && !track.id.isEmpty()) {
						result.push_back(std::move(track));
					}
				}
			}
			done(std::move(result));
		});
}

} // namespace

void Resolve(
		const Track &track,
		Fn<void(Track, const QString &error)> done) {
	// id попадает в URL запроса: нечисловой id мог бы подменить путь.
	if (!ValidTrackId(track.id)) {
		done(track, QString::fromUtf8(Mokagram::Lang::Tr("некорректный id трека")));
		return;
	}
	ApiGet(
		QString::fromLatin1(kApi) + u"/tracks/"_q + track.id,
		QUrlQuery(),
		[=](QByteArray data, QString error) {
			if (!error.isEmpty()) {
				done(track, LegacyAudioPath(track.id).isEmpty() ? error : QString());
				return;
			}
			auto fresh = ParseTrack(QJsonDocument::fromJson(data).object());
			if (fresh.transcodingUrl.isEmpty() && LegacyAudioPath(track.id).isEmpty()) {
				done(track, QString::fromUtf8(Mokagram::Lang::Tr("трек недоступен (закрыт или удалён)")));
				return;
			}
			done(std::move(fresh), QString());
		});
}

void Related(
		const QString &trackId,
		int limit,
		Fn<void(std::vector<Track>)> done) {
	auto q = QUrlQuery();
	q.addQueryItem(u"limit"_q, QString::number(limit));
	CollectionRequest(u"/tracks/"_q + trackId + u"/related"_q, q, std::move(done));
}

void LoadRelatedPage(
		const QString &trackId,
		const QString &cursor,
		Fn<void(RelatedPage)> done) {
	const auto endpoint = QString::fromLatin1(kApi)
		+ u"/tracks/"_q + trackId + u"/related"_q;
	const auto url = QUrl(cursor.isEmpty() ? endpoint : cursor);
	if (!trackId.toULongLong()
		|| url.scheme() != u"https"_q
		|| url.host() != QUrl(endpoint).host()
		|| url.path() != QUrl(endpoint).path()) {
		done({ {}, {}, u"Invalid radio source"_q });
		return;
	}
	auto query = QUrlQuery(url);
	query.removeAllQueryItems(u"client_id"_q);
	if (cursor.isEmpty()) {
		query.addQueryItem(u"limit"_q, u"50"_q);
		query.addQueryItem(u"linked_partitioning"_q, u"1"_q);
	}
	ApiGet(endpoint, std::move(query), [=](QByteArray data, QString error) {
		auto result = RelatedPage();
		result.error = error;
		if (error.isEmpty()) {
			const auto document = QJsonDocument::fromJson(data);
			const auto root = document.object();
			if (!document.isObject() || !root.value("collection").isArray()) {
				result.error = u"Invalid radio response"_q;
			} else {
				result.next = root.value("next_href").toString();
				for (const auto &value : root.value("collection").toArray()) {
					auto track = ParseTrack(value.toObject());
					if (track.id.toULongLong() && !track.transcodingUrl.isEmpty()) {
						result.tracks.push_back(std::move(track));
					}
				}
			}
		}
		done(std::move(result));
	});
}

void UserTracks(
		const QString &userId,
		int limit,
		Fn<void(std::vector<Track>)> done) {
	auto q = QUrlQuery();
	q.addQueryItem(u"limit"_q, QString::number(limit));
	CollectionRequest(u"/users/"_q + userId + u"/tracks"_q, q, std::move(done));
}

void LoadArtistInfo(
		const QString &userId,
		Fn<void(ArtistInfo)> done) {
	if (!ValidTrackId(userId)) {
		done({});
		return;
	}
	if (const auto i = ArtistInfoCache().find(userId);
		i != ArtistInfoCache().end()) {
		TouchArtistInfo(userId);
		done(i->second);
		return;
	}
	auto &pending = PendingArtistInfo();
	if (const auto i = pending.find(userId); i != pending.end()) {
		i->second.push_back(std::move(done));
		return;
	}
	pending[userId].push_back(std::move(done));
	ApiGet(
		QString::fromLatin1(kApi) + u"/users/"_q + userId,
		QUrlQuery(),
		[=](QByteArray data, QString error) {
			auto info = error.isEmpty()
				? ParseArtistInfo(QJsonDocument::fromJson(data).object(), userId)
				: ArtistInfo();
			if (!info.id.isEmpty()) {
				InsertArtistInfo(userId, info);
			}
			auto callbacks = std::move(PendingArtistInfo()[userId]);
			PendingArtistInfo().erase(userId);
			for (const auto &callback : callbacks) {
				callback(info);
			}
		});
}

QStringList TrackArtistNames(const Track &track) {
	const auto credit = track.artist.trimmed();
	if (credit.isEmpty()) {
		return {};
	}
	if (!ValidTrackId(track.id) || credit.compare(track.uploaderName, Qt::CaseInsensitive) == 0) {
		return { credit };
	}
	static const auto separator = QRegularExpression(
		u"\\s*,\\s*|\\s+(?:feat\\.?|ft\\.?)\\s+"_q,
		QRegularExpression::CaseInsensitiveOption);
	auto result = QStringList();
	auto seen = std::set<QString>();
	for (const auto &part : credit.split(separator, Qt::SkipEmptyParts)) {
		const auto name = part.trimmed();
		if (!name.isEmpty() && seen.insert(name.toCaseFolded()).second) {
			result.push_back(name);
		}
	}
	return result.empty() ? QStringList{ credit } : result;
}

void ResolveTrackArtist(
		const Track &track,
		const QString &name,
		Fn<void(ArtistInfo)> done) {
	const auto matches = [target = name.simplified()](const QString &candidate) {
		return candidate.simplified().compare(target, Qt::CaseInsensitive) == 0;
	};
	const auto findByName = [=] {
		auto query = QUrlQuery();
		query.addQueryItem(u"q"_q, name);
		query.addQueryItem(u"limit"_q, QString::number(kPageSize));
		query.addQueryItem(u"linked_partitioning"_q, u"1"_q);
		ApiGet(QString::fromLatin1(kApi) + u"/search/users"_q, std::move(query), [=](
				QByteArray data,
				QString error) {
			auto best = ArtistInfo();
			auto bestRank = std::pair(false, qint64(0));
			if (error.isEmpty()) {
				const auto collection = QJsonDocument::fromJson(data).object().value("collection").toArray();
				for (const auto &value : collection) {
					const auto object = value.toObject();
					if (!matches(object.value("username").toString())) {
						continue;
					}
					auto artist = ParseArtistInfo(object, QString());
					const auto rank = std::pair(
						object.value("verified").toBool(),
						object.value("followers_count").toVariant().toLongLong());
					if (!artist.id.isEmpty() && (best.id.isEmpty() || rank > bestRank)) {
						best = std::move(artist);
						bestRank = rank;
					}
				}
			}
			done(std::move(best));
		});
	};
	if (ValidTrackId(track.artistId) && track.uploaderName.isEmpty()) {
		LoadArtistInfo(track.artistId, [=](ArtistInfo info) {
			if (!info.id.isEmpty() && matches(info.name)) {
				done(std::move(info));
			} else {
				findByName();
			}
		});
	} else if (ValidTrackId(track.artistId) && matches(track.uploaderName)) {
		done({ track.artistId, track.uploaderName, track.artistArtworkUrl, {} });
	} else {
		findByName();
	}
}

void SearchLimited(
		const QString &query,
		int limit,
		Fn<void(std::vector<Track>)> done) {
	auto q = QUrlQuery();
	q.addQueryItem(u"q"_q, query);
	q.addQueryItem(u"limit"_q, QString::number(limit));
	CollectionRequest(u"/search/tracks"_q, q, std::move(done));
}

namespace {

[[nodiscard]] QString AlbumArtworkKey(const QString &url) {
	const auto parsed = QUrl(url);
	const auto path = parsed.path();
	static const auto variant = QRegularExpression(
		u"-(?:large|original|small|tiny|badge|crop|mini|t[0-9]+x[0-9]+)\\.[^.]+$"_q);
	const auto match = variant.match(path);
	return match.hasMatch()
		? path.left(match.capturedStart())
		: parsed.adjusted(QUrl::RemoveQuery | QUrl::RemoveFragment).toString();
}

[[nodiscard]] bool IsAlbumBackdrop(const QString &url, const QString &primaryKey) {
	return SafeMediaUrl(QUrl(url)) && AlbumArtworkKey(url) != primaryKey;
}

[[nodiscard]] Album ParseAlbum(const QJsonObject &object) {
	auto result = Album();
	result.id = QString::number(object.value("id").toVariant().toLongLong());
	result.title = object.value("title").toString();
	const auto user = object.value("user").toObject();
	result.artist = user.value("username").toString();
	result.artistId = QString::number(user.value("id").toVariant().toLongLong());
	const auto tracks = object.value("tracks").toArray();
	result.artworkUrl = object.value("artwork_url").toString();
	if (result.artworkUrl.isEmpty() && !tracks.isEmpty()) {
		result.artworkUrl = tracks.first().toObject().value("artwork_url").toString();
	}
	if (result.artworkUrl.isEmpty()) {
		result.artworkUrl = user.value("avatar_url").toString();
	}
	result.trackCount = object.value("track_count").toInt();
	const auto type = object.value("set_type").toString(
		object.value("playlist_type").toString()).trimmed().toLower();
	if (type == u"single"_q) {
		result.releaseKind = ReleaseKind::Single;
	} else if (type == u"ep"_q) {
		result.releaseKind = ReleaseKind::Ep;
	} else if (type == u"compilation"_q) {
		result.releaseKind = ReleaseKind::Compilation;
	} else if (type == u"album"_q || (type.isEmpty() && object.value("is_album").toBool())) {
		result.releaseKind = ReleaseKind::Album;
	}
	result.releaseYear = object.value("release_year").toInt();
	if (result.releaseYear <= 0) {
		const auto releaseDate = object.value("release_date").toString();
		result.releaseYear = releaseDate.left(4).toInt();
	}
	const auto primaryKey = AlbumArtworkKey(result.artworkUrl);
	auto complete = tracks.size() >= result.trackCount;
	for (const auto &value : tracks) {
		const auto track = value.toObject();
		const auto url = track.value("artwork_url").toString();
		if (IsAlbumBackdrop(url, primaryKey)) {
			result.backdropArtworkUrl = url;
			return result;
		}
		complete = complete && track.contains("artwork_url");
	}
	if (complete) {
		result.backdropArtworkUrl = QString();
	}
	return result;
}

void AlbumsRequest(
		const QString &path,
		QUrlQuery query,
		Fn<void(std::vector<Album>, QString)> done) {
	ApiGet(
		QString::fromLatin1(kApi) + path,
		std::move(query),
		[=](QByteArray data, QString error) {
			auto result = std::vector<Album>();
			if (error.isEmpty()) {
				const auto root = QJsonDocument::fromJson(data).object();
				for (const auto &value : root.value("collection").toArray()) {
					auto album = ParseAlbum(value.toObject());
					if (album.id != u"0"_q && album.trackCount > 0) {
						result.push_back(std::move(album));
					}
				}
			}
			done(std::move(result), error);
		});
}

// Подгружает треки по id пачками по 50 и дописывает в found.
void HydrateTracks(
		std::shared_ptr<std::map<QString, Track>> found,
		QStringList missing,
		Fn<void(QString)> done) {
	if (missing.isEmpty()) {
		done(QString());
		return;
	}
	const auto chunk = missing.mid(0, 50);
	missing = missing.mid(chunk.size());
	auto q = QUrlQuery();
	q.addQueryItem(u"ids"_q, chunk.join(','));
	ApiGet(
		QString::fromLatin1(kApi) + u"/tracks"_q,
		q,
		[=](QByteArray data, QString error) {
			if (!error.isEmpty()) {
				done(error);
				return;
			}
			auto parseError = QJsonParseError();
			const auto json = QJsonDocument::fromJson(data, &parseError);
			if (parseError.error != QJsonParseError::NoError || !json.isArray()) {
				done(QString::fromUtf8(Mokagram::Lang::Tr("Не удалось загрузить треки альбома. Попробуйте снова.")));
				return;
			}
			for (const auto &value : json.array()) {
				auto track = ParseTrack(value.toObject());
				if (track.id.toULongLong() && !track.title.isEmpty()) {
					track.unavailable = track.transcodingUrl.isEmpty();
					(*found)[track.id] = std::move(track);
				}
			}
			HydrateTracks(found, missing, done);
		});
}

} // namespace

void SearchCatalog(
		const QString &query,
		SearchCategory category,
		Fn<void(std::vector<CatalogItem>, QString)> done) {
	auto path = u"/search"_q;
	switch (category) {
	case SearchCategory::All: break;
	case SearchCategory::Tracks: path += u"/tracks"_q; break;
	case SearchCategory::Playlists: path += u"/playlists_without_albums"_q; break;
	case SearchCategory::Albums: path += u"/albums"_q; break;
	case SearchCategory::Artists: path += u"/users"_q; break;
	}
	auto q = QUrlQuery();
	q.addQueryItem(u"q"_q, query);
	q.addQueryItem(u"limit"_q, QString::number(kPageSize));
	q.addQueryItem(u"linked_partitioning"_q, u"1"_q);
	ApiGet(QString::fromLatin1(kApi) + path, std::move(q), [=](
			QByteArray data,
			QString error) {
		auto items = std::vector<CatalogItem>();
		if (error.isEmpty()) {
			const auto collection = QJsonDocument::fromJson(data).object().value("collection");
			if (!collection.isArray()) {
				error = QString::fromUtf8(Mokagram::Lang::Tr("Не удалось загрузить результаты поиска SoundCloud."));
			} else {
				auto seen = std::set<std::pair<QString, QString>>();
				for (const auto &value : collection.toArray()) {
					const auto object = value.toObject();
					const auto kind = object.value("kind").toString();
					const auto id = ResourceId(object, u"id"_q);
					if (id.isEmpty() || !seen.emplace(kind, id).second) {
						continue;
					}
					if (kind == u"track"_q) {
						auto track = ParseTrack(object);
						if (!track.title.isEmpty() && !track.transcodingUrl.isEmpty()) {
							items.push_back(std::move(track));
						}
					} else if (kind == u"playlist"_q || kind == u"album"_q) {
						auto album = ParseAlbum(object);
						album.id = id;
						if (kind == u"album"_q || category == SearchCategory::Albums) {
							if (album.releaseKind == ReleaseKind::Unknown) {
								album.releaseKind = ReleaseKind::Album;
							}
						}
						if (!album.title.isEmpty() && album.trackCount > 0) {
							items.push_back(std::move(album));
						}
					} else if (kind == u"user"_q) {
						auto artist = ParseArtistInfo(object, id);
						if (!artist.name.isEmpty()) {
							items.push_back(std::move(artist));
						}
					}
				}
			}
		}
		if (category != SearchCategory::All || !error.isEmpty()) {
			done(std::move(items), std::move(error));
			return;
		}
		struct SupplementalResults {
			std::vector<CatalogItem> items;
			std::array<std::vector<CatalogItem>, 2> sets;
			int pending = 0;
		};
		const auto hasSet = [&](bool playlist) {
			return std::ranges::any_of(items, [=](const CatalogItem &item) {
				const auto album = std::get_if<Album>(&item);
				return album && (album->releaseKind == ReleaseKind::Unknown) == playlist;
			});
		};
		const auto missingPlaylist = !hasSet(true);
		const auto missingAlbum = !hasSet(false);
		if (!missingPlaylist && !missingAlbum) {
			done(std::move(items), {});
			return;
		}
		// Общая выдача может полностью состоять из треков и профилей.
		// Добавляем верхние результаты из отсутствующих категорий.
		const auto state = std::make_shared<SupplementalResults>();
		state->items = std::move(items);
		state->pending = int(missingPlaylist) + int(missingAlbum);
		const auto request = [=](SearchCategory kind, int index) {
			SearchCatalog(query, kind, [=](std::vector<CatalogItem> sets, QString) {
				state->sets[index] = std::move(sets);
				if (--state->pending != 0) {
					return;
				}
				auto position = std::min(size_t(2), state->items.size());
				for (auto i = 0; i != 2; ++i) {
					for (auto &list : state->sets) {
						if (i < int(list.size())) {
							state->items.insert(
								state->items.begin() + position++,
								std::move(list[i]));
						}
					}
				}
				done(std::move(state->items), {});
			});
		};
		if (missingPlaylist) {
			request(SearchCategory::Playlists, 0);
		}
		if (missingAlbum) {
			request(SearchCategory::Albums, 1);
		}
	});
}

void SearchAlbums(
		const QString &query,
		int limit,
		Fn<void(std::vector<Album>, QString error)> done) {
	auto q = QUrlQuery();
	q.addQueryItem(u"q"_q, query);
	q.addQueryItem(u"limit"_q, QString::number(limit));
	AlbumsRequest(u"/search/albums"_q, q, std::move(done));
}

void LoadDiscoverPlaylists(Fn<void(std::vector<Album>, QString error)> done) {
	auto q = QUrlQuery();
	q.addQueryItem(u"limit"_q, u"50"_q);
	q.addQueryItem(u"linked_partitioning"_q, u"1"_q);
	AlbumsRequest(u"/playlists/discovery"_q, q, [=](
			std::vector<Album> discovery,
			QString discoveryError) {
		if (!Account::IsLoggedIn()) {
			done(std::move(discovery), std::move(discoveryError));
			return;
		}
		auto accountQuery = QUrlQuery();
		accountQuery.addQueryItem(u"show_tracks"_q, u"false"_q);
		accountQuery.addQueryItem(u"linked_partitioning"_q, u"true"_q);
		accountQuery.addQueryItem(u"limit"_q, u"50"_q);
		ApiGet(
			QString::fromLatin1(kPublicApi) + u"/me/playlists"_q,
			accountQuery,
			[=](QByteArray data, QString accountError) mutable {
				if (accountError.isEmpty()) {
					const auto document = QJsonDocument::fromJson(data);
					const auto collection = document.isArray()
						? document.array()
						: document.object().value("collection").toArray();
					for (const auto &value : collection) {
						auto album = ParseAlbum(value.toObject());
						if (album.id != u"0"_q
							&& album.trackCount > 0
							&& std::none_of(discovery.begin(), discovery.end(), [&](const Album &other) {
								return other.id == album.id;
							})) {
							discovery.push_back(std::move(album));
						}
					}
				}
				if (!discovery.empty()) {
					accountError.clear();
				} else if (!discoveryError.isEmpty()) {
					accountError = discoveryError;
				}
				done(std::move(discovery), std::move(accountError));
			});
	});
}

void LoadTrending(
		int limit,
		Fn<void(std::vector<Track>, QString error)> done) {
	auto q = QUrlQuery();
	q.addQueryItem(u"kind"_q, u"trending"_q);
	q.addQueryItem(u"genre"_q, u"soundcloud:genres:all-music"_q);
	q.addQueryItem(u"limit"_q, QString::number(std::clamp(limit, 1, 50)));
	ApiGet(
		QString::fromLatin1(kApi) + u"/charts"_q,
		q,
		[=](QByteArray data, QString error) {
			auto tracks = std::vector<Track>();
			if (error.isEmpty()) {
				const auto document = QJsonDocument::fromJson(data);
				if (!document.isObject()) {
					error = QString::fromUtf8(Mokagram::Lang::Tr(
						"SoundCloud вернул некорректный чарт."));
				} else {
					for (const auto &value : document.object()
							.value("collection").toArray()) {
						const auto item = value.toObject();
						const auto object = item.value("track").isObject()
							? item.value("track").toObject()
							: item;
						auto track = ParseTrack(object);
						if (!track.id.isEmpty() && !track.transcodingUrl.isEmpty()) {
							tracks.push_back(std::move(track));
						}
					}
				}
			}
			done(std::move(tracks), std::move(error));
		});
}

void TrackAlbums(
		const QString &trackId,
		Fn<void(std::vector<Album>)> done) {
	auto q = QUrlQuery();
	q.addQueryItem(u"limit"_q, u"10"_q);
	AlbumsRequest(
		u"/tracks/"_q + trackId + u"/albums"_q,
		q,
		[=](std::vector<Album> albums, QString) { done(std::move(albums)); });
}

void AlbumTracks(
		const QString &albumId,
		Fn<void(std::vector<Track>, QString error)> done) {
	ApiGet(
		QString::fromLatin1(kApi) + u"/playlists/"_q + albumId,
		QUrlQuery(),
		[=](QByteArray data, QString error) {
			if (!error.isEmpty()) {
				done({}, error);
				return;
			}
			auto parseError = QJsonParseError();
			const auto json = QJsonDocument::fromJson(data, &parseError);
			if (parseError.error != QJsonParseError::NoError
				|| !json.isObject()
				|| !json.object().value("tracks").isArray()) {
				done({}, QString::fromUtf8(Mokagram::Lang::Tr("Не удалось загрузить треки альбома. Попробуйте снова.")));
				return;
			}
			const auto root = json.object();
			auto order = QStringList();
			auto missing = QStringList();
			const auto found = std::make_shared<std::map<QString, Track>>();
			for (const auto &value : root.value("tracks").toArray()) {
				const auto object = value.toObject();
				const auto id = QString::number(
					object.value("id").toVariant().toLongLong());
				order.push_back(id);
				auto track = ParseTrack(object);
				if (id == u"0"_q) {
					continue;
				}
				if (track.transcodingUrl.isEmpty()) {
					missing.push_back(id);
				}
				if (!track.title.isEmpty()) {
					track.unavailable = track.transcodingUrl.isEmpty();
					(*found)[id] = std::move(track);
				}
			}
			HydrateTracks(found, missing, [=](QString error) {
				if (!error.isEmpty()) {
					done({}, error);
					return;
				}
				auto result = std::vector<Track>();
				for (const auto &id : order) {
					if (const auto i = found->find(id); i != found->end()) {
						result.push_back(i->second);
					}
				}
				done(std::move(result), QString());
			});
		});
}

void LoadAlbumBackdrop(const Album &album, Fn<void(QString)> done) {
	if (album.backdropArtworkUrl || !ValidTrackId(album.id)) {
		done(album.backdropArtworkUrl.value_or(QString()));
		return;
	}
	AlbumTracks(album.id, [=, primaryKey = AlbumArtworkKey(album.artworkUrl)](
			std::vector<Track> tracks,
			QString error) {
		if (error.isEmpty()) {
			for (const auto &track : tracks) {
				if (track.artworkUrl != track.artistArtworkUrl
					&& IsAlbumBackdrop(track.artworkUrl, primaryKey)) {
					done(track.artworkUrl);
					return;
				}
			}
		}
		done({});
	});
}

QString CacheDir() {
	return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
		+ u"/mokagram_music"_q;
}

void Search(
		const QString &query,
		int offset,
		Fn<void(SearchResult)> done) {
	auto q = QUrlQuery();
	q.addQueryItem(u"q"_q, query);
	q.addQueryItem(u"limit"_q, QString::number(kPageSize));
	q.addQueryItem(u"offset"_q, QString::number(offset));
	ApiGet(
		QString::fromLatin1(kApi) + u"/search/tracks"_q,
		q,
		[=](QByteArray data, QString error) {
			auto result = SearchResult();
			if (!error.isEmpty()) {
				result.error = error;
				done(std::move(result));
				return;
			}
			const auto root = QJsonDocument::fromJson(data).object();
			for (const auto &value : root.value("collection").toArray()) {
				auto track = ParseTrack(value.toObject());
				if (!track.transcodingUrl.isEmpty()) {
					result.tracks.push_back(std::move(track));
				}
			}
			if (!root.value("next_href").toString().isEmpty()) {
				result.nextOffset = offset + kPageSize;
			}
			done(std::move(result));
		});
}

namespace {

using DownloadDone = Fn<void(const QString &, const QString &)>;

void DownloadStream(const Track &track, DownloadDone done) {
	const auto path = CachePath(track);
	if (QFile::exists(path)) {
		TouchCacheFile(path);
		done(path, QString());
		return;
	}
	auto q = QUrlQuery();
	if (!track.authorization.isEmpty()) {
		q.addQueryItem(u"track_authorization"_q, track.authorization);
	}
	// Ссылка на транскодинг пришла из ответа API: проверяем перед загрузкой.
	if (!SafeMediaUrl(QUrl(track.transcodingUrl))) {
		done({}, QString::fromUtf8(Mokagram::Lang::Tr("сервер вернул недопустимую ссылку")));
		return;
	}
	ApiGet(track.transcodingUrl, q, [=](QByteArray data, QString error) {
		if (!error.isEmpty()) {
			done({}, error);
			return;
		}
		const auto url = QJsonDocument::fromJson(data).object()
			.value("url").toString();
		if (url.isEmpty()) {
			done({}, QString::fromUtf8(Mokagram::Lang::Tr("нет ссылки на поток")));
			return;
		}
		if (!SafeMediaUrl(QUrl(url))) {
			done({}, QString::fromUtf8(Mokagram::Lang::Tr("сервер вернул недопустимую ссылку")));
			return;
		}
		if (track.hls) {
			DownloadHls(url, path, done);
			return;
		}
		const auto reply = Get(QUrl(url), kMaxAudioBytes);
		QObject::connect(reply, &QNetworkReply::finished, [=] {
			const auto bytes = reply->readAll();
			const auto failed = (reply->error() != QNetworkReply::NoError);
			reply->deleteLater();
			if (failed || bytes.isEmpty()) {
				done({}, QString::fromUtf8(Mokagram::Lang::Tr("сбой загрузки трека")));
				return;
			}
			FinishFile(path, { bytes }, done);
		});
	});
}

void TryDownloadStreams(
		const Track &track,
		const std::shared_ptr<std::vector<TrackStream>> &streams,
		size_t index,
		const QString &firstError,
		DownloadDone done) {
	if (index == streams->size()) {
		const auto path = LegacyAudioPath(track.id);
		if (!path.isEmpty()) {
			TouchCacheFile(path);
			done(path, QString());
			return;
		}
		done({}, firstError);
		return;
	}
	const auto &stream = (*streams)[index];
	auto candidate = track;
	candidate.transcodingUrl = stream.url;
	candidate.extension = stream.extension;
	candidate.hls = stream.hls;
	DownloadStream(candidate, [=](const QString &path, const QString &error) {
		if (error.isEmpty()) {
			done(path, error);
		} else {
			TryDownloadStreams(track, streams, index + 1, firstError, done);
		}
	});
}

} // namespace

void Download(const Track &track, DownloadDone done) {
	const auto path = CachePath(track);
	if (path.isEmpty()) {
		done({}, QString::fromUtf8(Mokagram::Lang::Tr("некорректный id трека")));
		return;
	}
	const auto cached = track.transcodingUrl.isEmpty() ? LegacyAudioPath(track.id) : path;
	if (!cached.isEmpty() && QFile::exists(cached)) {
		TouchCacheFile(cached);
		done(cached, QString());
		return;
	}
	// Предзагрузка и воспроизведение делят одну загрузку, включая запасные потоки.
	static auto loading = std::map<QString, std::vector<DownloadDone>>();
	if (const auto i = loading.find(path); i != loading.end()) {
		i->second.push_back(std::move(done));
		return;
	}
	loading[path].push_back(std::move(done));
	const auto finish = [=](const QString &result, const QString &error) {
		if (!result.isEmpty() && error.isEmpty()) {
			TrimAudioCache(result);
		}
		auto waiters = std::move(loading.at(path));
		loading.erase(path);
		for (const auto &waiter : waiters) {
			waiter(result, error);
		}
	};
	DownloadStream(track, [=](const QString &result, const QString &firstError) {
		if (firstError.isEmpty()) {
			finish(result, firstError);
			return;
		}
		ApiGet(QString::fromLatin1(kApi) + u"/tracks/"_q + track.id,
			QUrlQuery(),
			[=](QByteArray data, QString error) {
				const auto fresh = QJsonDocument::fromJson(data).object();
				auto streams = error.isEmpty()
					? ParseStreams(fresh)
					: std::vector<TrackStream>();
				auto refreshed = track;
				refreshed.authorization = fresh.value("track_authorization").toString();
				if (refreshed.authorization == track.authorization) {
					std::erase_if(streams, [&](const TrackStream &stream) {
						return stream.url == track.transcodingUrl;
					});
				}
				TryDownloadStreams(refreshed,
					std::make_shared<std::vector<TrackStream>>(std::move(streams)),
					0,
					firstError,
					finish);
			});
	});
}

namespace {

TrackComment ParseTrackComment(const QJsonObject &object) {
	const auto user = object.value("user").toObject();
	auto result = TrackComment();
	result.id = ResourceId(object, u"id"_q);
	result.author = user.value("username").toString();
	result.avatarUrl = user.value("avatar_url").toString();
	if (!SafeMediaUrl(QUrl(result.avatarUrl))) {
		result.avatarUrl.clear();
	}
	result.body = object.value("body").toString();
	const auto timestamp = object.value("timestamp");
	if (timestamp.isDouble()) {
		result.timestampMs = timestamp.toVariant().toLongLong();
	}
	return result;
}

} // namespace

void LoadTrackComments(
		const QString &trackId,
		const QString &cursor,
		Fn<void(TrackCommentsPage)> done) {
	const auto path = u"/tracks/"_q + trackId + u"/comments"_q;
	auto url = cursor.isEmpty()
		? QUrl(QString::fromLatin1(kApi) + path) : QUrl(cursor);
	if (!ValidTrackId(trackId) || !SafeMediaUrl(url)
		|| (url.host() != u"api-v2.soundcloud.com"_q
			&& url.host() != u"api.soundcloud.com"_q)
		|| url.path() != path) {
		done({ .error = QString::fromUtf8(Mokagram::Lang::Tr("некорректная ссылка SoundCloud")) });
		return;
	}
	auto query = QUrlQuery();
	if (cursor.isEmpty()) {
		query.addQueryItem(u"limit"_q, u"200"_q);
		if (url.host() == u"api-v2.soundcloud.com"_q) {
			// API сайта требует явных параметров списка комментариев.
			query.addQueryItem(u"sort"_q, u"track-timestamp"_q);
			query.addQueryItem(u"offset"_q, u"0"_q);
			query.addQueryItem(u"threaded"_q, u"0"_q);
		} else {
			query.addQueryItem(u"linked_partitioning"_q, u"true"_q);
		}
	}
	LOG(("[moka-dbg] comments ApiGet %1").arg(url.toString()));
	ApiGet(url.toString(), query, [=](QByteArray data, QString error) {
		LOG(("[moka-dbg] comments reply %1 bytes, error '%2'").arg(data.size()).arg(error.left(200)));
		auto result = TrackCommentsPage();
		if (!error.isEmpty()) {
			// QNetworkReply включает URL в errorString, он не нужен в интерфейсе.
			result.error = QString::fromUtf8(Mokagram::Lang::Tr("Не удалось загрузить комментарии"));
			const auto status = QRegularExpression(
				u"(?:HTTP |код )(\\d{3})"_q).match(error);
			if (status.hasMatch()) {
				result.error += u" (HTTP %1)"_q.arg(status.captured(1));
			}
		}
		if (result.error.isEmpty()) {
			auto parseError = QJsonParseError();
			const auto document = QJsonDocument::fromJson(data, &parseError);
			if (parseError.error != QJsonParseError::NoError
				|| (!document.isObject() && !document.isArray())) {
				result.error = QString::fromUtf8(Mokagram::Lang::Tr("Не удалось загрузить комментарии"));
			} else {
				const auto root = document.object();
				const auto list = document.isArray()
					? document.array() : root.value("collection").toArray();
				for (const auto &value : list) {
					auto comment = ParseTrackComment(value.toObject());
					if (!comment.id.isEmpty() && !comment.body.isEmpty()) {
						result.comments.push_back(std::move(comment));
					}
				}
				const auto next = QUrl(root.value("next_href").toString());
				if (SafeMediaUrl(next) && next.host() == url.host()
					&& next.path() == path && next != url) {
					result.next = next.toString();
				}
			}
		}
		done(std::move(result));
	});
}


namespace Account {
namespace {

rpl::event_stream<> &ChangesStream() {
	static auto result = rpl::event_stream<>();
	return result;
}

rpl::event_stream<QString> &LikeWriteErrorsStream() {
	static auto result = rpl::event_stream<QString>();
	return result;
}

QJsonObject &UserObject() {
	static auto result = []() -> QJsonObject {
		auto file = QFile(SessionFile());
		if (!file.open(QIODevice::ReadOnly)) {
			return {};
		}
		return QJsonDocument::fromJson(file.readAll()).object()
			.value("user").toObject();
	}();
	return result;
}

[[nodiscard]] Info ParseUser(const QJsonObject &object) {
	if (object.isEmpty()) {
		return {};
	}
	auto result = Info();
	result.id = ResourceId(object, u"id"_q);
	result.username = object.value("username").toString();
	result.avatarUrl = object.value("avatar_url").toString();
	if (result.avatarUrl.isEmpty()) {
		result.avatarUrl = object.value("permalink_url").toString();
	}
	return result;
}

void FetchUser(Fn<void(QString)> done) {
	const auto finish = [=](QByteArray data, QString error) {
		if (!error.isEmpty() || data.isEmpty()) {
			done(error.isEmpty()
				? u"SoundCloud вернул пустой профиль."_q
				: error);
			return;
		}
		const auto profile = QJsonDocument::fromJson(data).object();
		if (profile.isEmpty()) {
			done(u"SoundCloud вернул некорректный профиль."_q);
			return;
		}
		UserObject() = profile;
		done(QString());
	};
	ApiGet(QString::fromLatin1(kPublicApi) + u"/me"_q, QUrlQuery(),
		[=](QByteArray data, QString error) {
			if (error.contains(u"HTTP 403"_q)) {
				// Старые токены сайта иногда доступны только через его api-v2.
				ApiGet(QString::fromLatin1(kApi) + u"/me"_q, QUrlQuery(), finish);
			} else {
				finish(std::move(data), std::move(error));
			}
		});
}

[[nodiscard]] bool ValidApiUrl(const QString &url) {
	const auto parsed = QUrl(url);
	const auto host = parsed.host().toLower();
	return parsed.isValid()
		&& parsed.scheme() == u"https"_q
		&& parsed.userInfo().isEmpty()
		&& parsed.port(-1) == -1
		&& (host == u"api-v2.soundcloud.com"_q
			|| host == u"api.soundcloud.com"_q);
}

[[nodiscard]] QString LikesUrl(const QString &next) {
	if (!next.isEmpty()) {
		return ValidApiUrl(next) ? next : QString();
	}
	return QString::fromLatin1(kPublicApi) + u"/me/likes/tracks"_q;
}

struct SyncState {
	std::vector<Track> remote;
	std::vector<QString> pushed;
	QString next;
	int pages = 0;
	int imported = 0;
	QString error;
	Fn<void(int, int, QString)> done;
};

struct PlaylistSyncEntry {
	RemotePlaylist playlist;
	QString resourceId;
	int trackCount = 0;
};

struct PlaylistCollectionPage {
	std::vector<PlaylistSyncEntry> playlists;
	QString next;
	QString error;
};

struct PlaylistSyncState {
	std::vector<PlaylistSyncEntry> playlists;
	std::vector<Track> likedTracks;
	std::set<QString> playlistIds;
	std::set<QString> likedTrackIds;
	QString userId;
	QString error;
	bool complete = true;
	bool likesComplete = true;
	bool refreshed = false;
	quint64 generation = 0;
};

struct PlaylistSyncControl {
	std::vector<Fn<void(int, int, QString)>> waiters;
	quint64 generation = 0;
	bool active = false;
};

PlaylistSyncControl &PlaylistSync() {
	static auto result = PlaylistSyncControl();
	return result;
}

void FinishPlaylistSync(quint64 generation, int playlists, int tracks, QString error) {
	auto &control = PlaylistSync();
	if (control.generation != generation) {
		return;
	}
	control.active = false;
	auto waiters = std::move(control.waiters);
	for (const auto &done : waiters) {
		if (done) done(playlists, tracks, error);
	}
}

void CancelPlaylistSync() {
	auto &control = PlaylistSync();
	if (!control.active) {
		return;
	}
	++control.generation;
	control.active = false;
	auto waiters = std::move(control.waiters);
	for (const auto &done : waiters) {
		if (done) done(0, 0, QString());
	}
}

void LoadPlaylistCollection(
		const QString &nextHref,
		bool liked,
		Fn<void(PlaylistCollectionPage)> done) {
	if (!nextHref.isEmpty() && !ValidApiUrl(nextHref)) {
		done({ .error = QString::fromUtf8(Mokagram::Lang::Tr(
			"некорректная ссылка SoundCloud")) });
		return;
	}
	const auto path = liked
		? u"/me/likes/playlists"_q
		: u"/me/playlists"_q;
	const auto url = nextHref.isEmpty()
		? QString::fromLatin1(kPublicApi) + path
		: nextHref;
	auto query = QUrlQuery();
	if (nextHref.isEmpty()) {
		query.addQueryItem(u"limit"_q, u"50"_q);
		query.addQueryItem(u"linked_partitioning"_q, u"true"_q);
	}
	const auto parse = [=](QByteArray data, QString error) {
		auto result = PlaylistCollectionPage();
		if (!error.isEmpty()) {
			result.error = error;
			done(std::move(result));
			return;
		}
		auto parseError = QJsonParseError();
		const auto json = QJsonDocument::fromJson(data, &parseError);
		if (parseError.error != QJsonParseError::NoError
			|| (!json.isObject() && !json.isArray())) {
			result.error = QString::fromUtf8(Mokagram::Lang::Tr(
				"SoundCloud вернул некорректный список плейлистов."));
			done(std::move(result));
			return;
		}
		const auto root = json.object();
		const auto values = json.isArray()
			? json.array()
			: root.value("collection").toArray();
		for (const auto &value : values) {
			auto object = value.toObject();
			if (object.contains(u"playlist"_q)) {
				object = object.value(u"playlist"_q).toObject();
			}
			const auto id = ResourceId(object, u"id"_q);
			const auto title = object.value(u"title"_q).toString().trimmed();
			if (id.isEmpty() || title.isEmpty()) {
				continue;
			}
			auto entry = PlaylistSyncEntry();
			entry.resourceId = id;
			entry.playlist.id = (liked ? u"liked-playlist:"_q : u"set:"_q) + id;
			entry.playlist.name = liked
				? QString::fromUtf8(Mokagram::Lang::Tr("Избранное: %1")).arg(title)
				: title;
			entry.trackCount = object.value(u"track_count"_q).toInt();
			for (const auto &trackValue : object.value(u"tracks"_q).toArray()) {
				auto track = ParseTrack(trackValue.toObject());
				if (!track.id.isEmpty() && !track.title.isEmpty()) {
					entry.playlist.tracks.push_back(std::move(track));
				}
			}
			entry.trackCount = std::max(
				entry.trackCount,
				int(entry.playlist.tracks.size()));
			result.playlists.push_back(std::move(entry));
		}
		const auto next = root.value(u"next_href"_q).toString();
		if (!next.isEmpty()) {
			if (ValidApiUrl(next)) {
				result.next = next;
			} else {
				result.error = QString::fromUtf8(Mokagram::Lang::Tr(
					"некорректная ссылка SoundCloud"));
			}
		}
		done(std::move(result));
	};
	ApiGet(url, query, [=](QByteArray data, QString error) {
		if (nextHref.isEmpty()
			&& error.contains(u"HTTP 403"_q)
			&& !User().id.isEmpty()) {
			const auto legacyPath = liked
				? u"/users/"_q + User().id + u"/likes/playlists"_q
				: u"/users/"_q + User().id + u"/playlists"_q;
			ApiGet(QString::fromLatin1(kApi) + legacyPath, QUrlQuery(), parse);
			return;
		}
		parse(std::move(data), std::move(error));
	});
}

void SetPlaylistSyncError(
		const std::shared_ptr<PlaylistSyncState> &state,
		const QString &error,
		bool likes = false) {
	if (state->error.isEmpty()
		|| (error.contains(u"HTTP 401"_q)
			&& !state->error.contains(u"HTTP 401"_q))) {
		state->error = error;
	}
	state->complete = false;
	if (likes) {
		state->likesComplete = false;
	}
}

void AppendPlaylistPage(
		const std::shared_ptr<PlaylistSyncState> &state,
		PlaylistCollectionPage page) {
	for (auto &entry : page.playlists) {
		if (state->playlistIds.contains(entry.resourceId)) {
			continue;
		}
		if (int(state->playlists.size()) >= kMaxSyncedPlaylists) {
			SetPlaylistSyncError(state, QString::fromUtf8(Mokagram::Lang::Tr(
				"Слишком много плейлистов для синхронизации.")));
			return;
		}
		state->playlistIds.insert(entry.resourceId);
		state->playlists.push_back(std::move(entry));
	}
}

void LoadPlaylistPages(
		const std::shared_ptr<PlaylistSyncState> &state,
		bool liked,
		Fn<void()> done) {
	const auto load = std::make_shared<Fn<void(QString, int)>>();
	*load = [=](QString next, int pages) {
		if (pages >= kMaxPlaylistPages
			|| int(state->playlists.size()) >= kMaxSyncedPlaylists) {
			if (!next.isEmpty()
				|| int(state->playlists.size()) >= kMaxSyncedPlaylists) {
				SetPlaylistSyncError(state, QString::fromUtf8(Mokagram::Lang::Tr(
					"Список плейлистов обрезан по лимиту.")));
			}
			done();
			return;
		}
		LoadPlaylistCollection(next, liked, [=](PlaylistCollectionPage page) {
			if (!page.error.isEmpty()) {
				SetPlaylistSyncError(state, page.error);
				done();
				return;
			}
			const auto nextHref = page.next;
			AppendPlaylistPage(state, std::move(page));
			if (!nextHref.isEmpty()) {
				(*load)(nextHref, pages + 1);
			} else {
				done();
			}
		});
	};
	(*load)(QString(), 0);
}

void LoadLikedTrackPages(
		const std::shared_ptr<PlaylistSyncState> &state,
		Fn<void()> done) {
	const auto load = std::make_shared<Fn<void(QString, int)>>();
	*load = [=](QString next, int pages) {
		if (pages >= kMaxLikesPages) {
			state->likesComplete = false;
			state->complete = false;
			state->error = state->error.isEmpty()
				? QString::fromUtf8(Mokagram::Lang::Tr(
					"Список лайков обрезан по лимиту."))
				: state->error;
			done();
			return;
		}
		LoadLikes(next, [=](LikesPage page) {
			if (!page.error.isEmpty()) {
				SetPlaylistSyncError(state, page.error, true);
				done();
				return;
			}
			for (auto &track : page.tracks) {
				if (!track.id.isEmpty()
					&& state->likedTrackIds.insert(track.id).second) {
					state->likedTracks.push_back(std::move(track));
				}
			}
			if (!page.next.isEmpty()) {
				(*load)(page.next, pages + 1);
			} else {
				done();
			}
		});
	};
	(*load)(QString(), 0);
}

void LoadPlaylistDetails(
		const std::shared_ptr<PlaylistSyncState> &state,
		Fn<void()> done) {
	const auto load = std::make_shared<Fn<void(int)>>();
	*load = [=](int index) {
		if (index >= int(state->playlists.size())) {
			done();
			return;
		}
		auto &entry = state->playlists[index];
		if (entry.trackCount <= int(entry.playlist.tracks.size())
			|| entry.resourceId.isEmpty()) {
			(*load)(index + 1);
			return;
		}
		AlbumTracks(entry.resourceId, [=](std::vector<Track> tracks, QString error) {
			if (!error.isEmpty()) {
				SetPlaylistSyncError(state, error);
				if (error.contains(u"HTTP 401"_q)) {
					done();
					return;
				}
			} else {
				state->playlists[index].playlist.tracks = std::move(tracks);
			}
			(*load)(index + 1);
		});
	};
	(*load)(0);
}

void BeginPlaylistSync(const std::shared_ptr<PlaylistSyncState> &state);

void FinishPlaylistSyncData(const std::shared_ptr<PlaylistSyncState> &state) {
	if (!IsLoggedIn() || User().id != state->userId) {
		FinishPlaylistSync(state->generation, 0, 0, QString());
		return;
	}
	if (!state->refreshed && state->error.contains(u"HTTP 401"_q)) {
		state->refreshed = true;
		RefreshSoundCloudToken([=](bool ok) {
			if (!ok || !IsLoggedIn() || User().id != state->userId) {
				FinishPlaylistSync(state->generation, 0, 0, state->error);
				return;
			}
			state->playlists.clear();
			state->likedTracks.clear();
			state->playlistIds.clear();
			state->likedTrackIds.clear();
			state->error.clear();
			state->complete = true;
			state->likesComplete = true;
			BeginPlaylistSync(state);
		});
		return;
	}

	auto remote = std::vector<RemotePlaylist>();
	remote.reserve(state->playlists.size() + (state->likesComplete ? 1 : 0));
	for (auto &entry : state->playlists) {
		remote.push_back(std::move(entry.playlist));
	}
	if (state->likesComplete) {
		auto likes = RemotePlaylist();
		likes.id = u"liked-tracks:"_q + state->userId;
		likes.name = QString::fromUtf8(Mokagram::Lang::Tr(
			"Понравившиеся треки SoundCloud"));
		likes.tracks = state->likedTracks;
		remote.push_back(std::move(likes));
	}
	const auto complete = state->complete && state->likesComplete;
	::Mokagram::Music::MergeRemotePlaylists(remote, complete);
	(void)::Mokagram::Music::MergeRemoteLikes(
		state->likedTracks,
		state->likesComplete);
	FinishPlaylistSync(
		state->generation,
		int(remote.size()),
		int(state->likedTracks.size()),
		state->error);
}

void BeginPlaylistSync(const std::shared_ptr<PlaylistSyncState> &state) {
	LoadPlaylistPages(state, false, [=] {
		if (state->error.contains(u"HTTP 401"_q)) {
			FinishPlaylistSyncData(state);
			return;
		}
		LoadPlaylistPages(state, true, [=] {
			if (state->error.contains(u"HTTP 401"_q)) {
				FinishPlaylistSyncData(state);
				return;
			}
			LoadLikedTrackPages(state, [=] {
				if (state->error.contains(u"HTTP 401"_q)) {
					FinishPlaylistSyncData(state);
					return;
				}
				LoadPlaylistDetails(state, [=] {
					FinishPlaylistSyncData(state);
				});
			});
		});
	});
}

} // namespace

bool IsLoggedIn() {
	return !AccountToken().isEmpty();
}

Info User() {
	return ParseUser(UserObject());
}

void Login(const QString &accessToken, Fn<void(QString)> done) {
	auto token = accessToken.trimmed();
	if (token.startsWith(u"TOKEN:"_q, Qt::CaseInsensitive)) {
		token = token.mid(6).trimmed();
	}
	if (!ValidSessionToken(token)) {
		done(u"Некорректный токен SoundCloud."_q);
		return;
	}
	const auto previousToken = AccountToken();
	const auto previousUser = UserObject();
	AccountToken() = token;
	UserObject() = QJsonObject();
	FetchUser([=](QString error) {
		if (!error.isEmpty()) {
			AccountToken() = previousToken;
			UserObject() = previousUser;
			if (previousToken.isEmpty()) {
				RemoveSessionFile();
			} else {
				SaveSessionFile(previousToken, previousUser);
			}
			ChangesStream().fire({});
			done(error);
			return;
		}
		SaveSessionFile(AccountToken(), UserObject());
		ChangesStream().fire({});
		done(QString());
	});
}

void Logout() {
	AccountToken().clear();
	UserObject() = QJsonObject();
	RemoveSessionFile();
	CancelPlaylistSync();
	ChangesStream().fire({});
	::Mokagram::Music::ClearRemoteLikes();
	::Mokagram::Music::ClearRemotePlaylists();
}

rpl::producer<> Changes() {
	return ChangesStream().events();
}

rpl::producer<QString> LikeWriteErrors() {
	return LikeWriteErrorsStream().events();
}

void LoadLikes(const QString &nextHref, Fn<void(LikesPage)> done) {
	if (!IsLoggedIn()) {
		done({ .error = QString::fromUtf8(Mokagram::Lang::Tr("нет аккаунта SoundCloud")) });
		return;
	}
	const auto url = LikesUrl(nextHref);
	if (url.isEmpty()) {
		done({ .error = QString::fromUtf8(Mokagram::Lang::Tr("некорректная ссылка SoundCloud")) });
		return;
	}
	auto query = QUrlQuery();
	if (nextHref.isEmpty()) {
		query.addQueryItem(u"limit"_q, u"200"_q);
		query.addQueryItem(u"linked_partitioning"_q, u"true"_q);
	}
	const auto parse = [=](QByteArray data, QString error) {
		auto result = LikesPage();
		if (!error.isEmpty()) {
			result.error = error;
			done(std::move(result));
			return;
		}
		auto parseError = QJsonParseError();
		const auto document = QJsonDocument::fromJson(data, &parseError);
		if (parseError.error != QJsonParseError::NoError
			|| !document.isObject()
			|| !document.object().value(u"collection"_q).isArray()) {
			result.error = QString::fromUtf8(Mokagram::Lang::Tr(
				"SoundCloud вернул некорректный список лайков."));
			done(std::move(result));
			return;
		}
		const auto root = document.object();
		for (const auto &value : root.value("collection").toArray()) {
			// Коллекция /likes — это лайки, а не треки: сам трек лежит в поле
			// "track". Плейлисты (kind like с "playlist") пропускаем.
			auto object = value.toObject();
			if (object.contains(u"track"_q)) {
				object = object.value(u"track"_q).toObject();
			}
			auto track = ParseTrack(object);
			if (!track.id.isEmpty() && !track.title.isEmpty()) {
				result.tracks.push_back(std::move(track));
			}
		}
		const auto next = root.value("next_href").toString();
		if (!next.isEmpty()) {
			if (ValidApiUrl(next)) {
				result.next = next;
			} else {
				result.error = QString::fromUtf8(Mokagram::Lang::Tr(
					"некорректная ссылка SoundCloud"));
			}
		}
		done(std::move(result));
	};
	ApiGet(url, query, [=](QByteArray data, QString error) {
		const auto info = User();
		if (error.contains(u"HTTP 403"_q)
			&& !info.id.isEmpty()
			&& QUrl(url).host() == u"api.soundcloud.com"_q) {
			// Для авторизации сайта пробуем его старый endpoint с найденным client_id.
			const auto legacy = QString::fromLatin1(kApi)
				+ u"/users/"_q + info.id + u"/likes"_q;
			ApiGet(legacy, QUrlQuery(), parse);
		} else {
			parse(std::move(data), std::move(error));
		}
	});
}

void SetTrackLiked(const QString &trackId, bool liked, bool refreshed) {
	if (!IsLoggedIn() || !ValidTrackId(trackId)) {
		return;
	}
	const auto failure = [=](
			int status,
			QNetworkReply::NetworkError networkError,
			QString networkMessage,
			QByteArray data) {
		if ((status == 401 || status == 403) && !refreshed) {
			// Сайт может отклонять устаревшую авторизацию и через 403.
			// Обновляем один раз, чтобы не зациклить запрещённый запрос.
			RefreshSoundCloudToken([=](bool ok) {
				if (ok) {
					SetTrackLiked(trackId, liked, true);
				} else {
					LikeWriteErrorsStream().fire(QString::fromUtf8(
						Mokagram::Lang::Tr("Вход в SoundCloud устарел: открой soundcloud.com в браузере и войди заново в mokaGram.")));
				}
			});
			return;
		}
		auto message = QString::fromUtf8(Mokagram::Lang::Tr(
			"SoundCloud не сохранил лайк"));
		if (status) {
			message += u" (HTTP "_q + QString::number(status) + u")"_q;
		} else if (networkError != QNetworkReply::NoError) {
			message += u": "_q + networkMessage;
		}
		const auto detail = ApiErrorDetail(data);
		if (!detail.isEmpty()) {
			message += u". "_q + detail;
		}
		LikeWriteErrorsStream().fire(std::move(message));
	};
	auto request = QNetworkRequest(QUrl(QString::fromLatin1(kPublicApi)
		+ u"/likes/tracks/soundcloud:tracks:"_q + trackId));
	request.setRawHeader("User-Agent", kUserAgent);
	request.setRawHeader(
		"Authorization",
		QByteArray("OAuth ") + AccountToken().toUtf8());
	// Заголовки запроса записи совпадают с запросами сайта.
	request.setRawHeader("Accept", "application/json, text/javascript, */*; q=0.01");
	request.setRawHeader("Accept-Language", "en-US,en;q=0.5");
	request.setRawHeader("Content-Type", "application/json");
	request.setRawHeader("Origin", "https://soundcloud.com");
	request.setRawHeader("Referer", "https://soundcloud.com/");
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setTransferTimeout(20000);
	const auto reply = liked
		? Net().post(request, QByteArray())
		: Net().deleteResource(request);
	QObject::connect(reply, &QNetworkReply::finished, reply, [=] {
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const auto networkError = reply->error();
		const auto networkMessage = reply->errorString();
		const auto data = reply->readAll();
		reply->deleteLater();
		LOG(("[moka-dbg] like %1 primary HTTP %2 err %3 body %4").arg(trackId).arg(status).arg(networkMessage.left(120)).arg(QString::fromUtf8(data.left(200))));
		if (status >= 200 && status < 300) {
			return;
		}
		const auto primaryStatus = status;
		const auto primaryNetworkError = networkError;
		const auto primaryNetworkMessage = networkMessage;
		const auto primaryData = data;
		const auto info = User();
		if (info.id.isEmpty()) {
			failure(primaryStatus, primaryNetworkError, primaryNetworkMessage, primaryData);
			return;
		}
		EnsureAnyClientId([=](bool ok) {
			if (!ok) {
				failure(primaryStatus, primaryNetworkError, primaryNetworkMessage, primaryData);
				return;
			}
			// api-v2 сайта: users/{id}/track_likes/{track}, PUT ставит лайк.
			auto url = QUrl(QString::fromLatin1(kApi)
				+ u"/users/"_q + info.id + u"/track_likes/"_q + trackId);
			auto query = QUrlQuery();
			query.addQueryItem(u"client_id"_q, ClientId());
			url.setQuery(query);
			auto legacyRequest = QNetworkRequest(url);
			legacyRequest.setRawHeader("User-Agent", kUserAgent);
			legacyRequest.setRawHeader(
				"Authorization",
				QByteArray("OAuth ") + AccountToken().toUtf8());
			legacyRequest.setRawHeader("Accept", "application/json, text/javascript, */*; q=0.01");
			legacyRequest.setRawHeader("Accept-Language", "en-US,en;q=0.5");
			legacyRequest.setRawHeader("Content-Type", "application/json");
			legacyRequest.setRawHeader("Origin", "https://soundcloud.com");
			legacyRequest.setRawHeader("Referer", "https://soundcloud.com/");
			legacyRequest.setTransferTimeout(20000);
			const auto legacyReply = liked
				? Net().put(legacyRequest, QByteArray())
				: Net().deleteResource(legacyRequest);
			QObject::connect(legacyReply, &QNetworkReply::finished, legacyReply, [=] {
				const auto legacyStatus = legacyReply->attribute(
					QNetworkRequest::HttpStatusCodeAttribute).toInt();
				const auto legacyError = legacyReply->error();
				const auto legacyMessage = legacyReply->errorString();
				const auto legacyData = legacyReply->readAll();
				legacyReply->deleteLater();
				LOG(("[moka-dbg] like %1 legacy HTTP %2 err %3 body %4").arg(trackId).arg(legacyStatus).arg(legacyMessage.left(120)).arg(QString::fromUtf8(legacyData.left(200))));
				if (legacyStatus >= 200 && legacyStatus < 300) {
					return;
				}
				failure(
					legacyStatus ? legacyStatus : primaryStatus,
					legacyError != QNetworkReply::NoError
						? legacyError
						: primaryNetworkError,
					legacyError != QNetworkReply::NoError
						? legacyMessage
						: primaryNetworkMessage,
					legacyData.isEmpty() ? primaryData : legacyData);
			});
		}, refreshed);
	});
}

void SyncLikes(
		Fn<void(int remote, int pushed, QString error)> done,
		bool refreshed) {
	auto state = std::make_shared<SyncState>();
	state->done = std::move(done);
	if (!IsLoggedIn()) {
		if (state->done) {
			state->done(0, 0, QString());
		}
		return;
	}
	const auto load = std::make_shared<Fn<void()>>();
	*load = [=] {
		// Лимит страниц: применяем собранное, не ждём бесконечности.
		++state->pages;
		LoadLikes(state->next, [=](LikesPage page) {
			if (!page.error.isEmpty()) {
				state->error = page.error;
				state->next.clear();
			} else {
				state->imported += ::Mokagram::Music::MergeRemoteLikes(
					page.tracks,
					false);
				for (auto &track : page.tracks) {
					state->remote.push_back(std::move(track));
				}
				state->next = page.next;
			}
			if (!state->next.isEmpty() && state->pages < kMaxLikesPages) {
				(*load)();
				return;
			}
			// 1) Лайки из аккаунта — в локальные.
			auto remoteIds = std::set<QString>();
			auto remoteTracks = std::vector<Track>();
			for (const auto &track : state->remote) {
				if (remoteIds.insert(track.id).second) {
					remoteTracks.push_back(track);
				}
			}
			if (!state->error.isEmpty()) {
				if (!refreshed && state->error.contains(u"HTTP 401"_q)) {
					// Токен входа живёт сутки: берём свежий из браузера.
					RefreshSoundCloudToken([=](bool ok) {
						if (ok) {
							SyncLikes(state->done, true);
						} else if (state->done) {
							state->done(0, 0, state->error);
						}
					});
					return;
				}
				if (state->done) {
					state->done(0, 0, state->error);
				}
				return;
			}
			const auto complete = state->next.isEmpty();
			const auto imported = state->imported
				+ ::Mokagram::Music::MergeRemoteLikes(remoteTracks, complete);
			NotifyLikesChanged();
			// 2) Локальные лайки, которых нет в аккаунте, — в аккаунт.
			auto pushed = 0;
			for (const auto &track : LikedTracks()) {
				if (!complete) {
					break;
				}
				if (pushed >= kMaxPushedLikes) {
					break;
				}
				if (track.id.isEmpty() || remoteIds.contains(track.id)) {
					continue;
				}
				LOG(("[moka-dbg] push like %1 %2").arg(track.id).arg(track.title.left(30)));
				SetTrackLiked(track.id, true);
				++pushed;
			}
			if (state->done) {
				state->done(imported, pushed, QString());
			}
		});
	};
	(*load)();
}

void SyncPlaylists(Fn<void(int playlists, int tracks, QString error)> done) {
	if (!IsLoggedIn()) {
		if (done) done(0, 0, QString());
		return;
	}
	auto &control = PlaylistSync();
	if (done) {
		control.waiters.push_back(std::move(done));
	}
	if (control.active) {
		return;
	}
	control.active = true;
	const auto state = std::make_shared<PlaylistSyncState>();
	state->generation = ++control.generation;
	state->userId = User().id;
	BeginPlaylistSync(state);
}

} // namespace Account

} // namespace Mokagram::Music
