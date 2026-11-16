// This file is part of mokaGram, https://github.com/ecronxy/mokagram
// (c) 2026 @ecronx (https://t.me/ecronx). GPLv3 with the attribution term
// of MOKAGRAM_NOTICE.md (section 7(b)): keep this notice when copying.
// mokagram-id: 8becca5f
#include "mokagram/music/mokagram_music_library.h"
#include "mokagram/lang/mokagram_lang.h"

#include "base/random.h"
#include "rpl/event_stream.h"
#include <crl/crl_async.h>

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QStandardPaths>
#include <QtCore/QUuid>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <set>

namespace Mokagram::Music {
namespace {

constexpr auto kHistoryLimit = 1000;
constexpr auto kShownLimit = 500;
constexpr qint64 kEarlySkipMs = 30 * 1000;
constexpr qint64 kFinishedCooldown = 30 * 86400;
constexpr qint64 kPartialCooldown = 14 * 86400;
constexpr qint64 kSkippedCooldown = 365 * 86400;
constexpr qint64 kShownCooldown = 5 * 86400;
constexpr auto kMinDurationMs = 75 * 1000;
constexpr auto kMaxDurationMs = 11 * 60 * 1000;
constexpr double kHalfLifeDays = 60.;

struct Liked {
	Track track;
	qint64 at = 0;
	bool remote = false; // пришёл из подключённого аккаунта SoundCloud
};

struct Listen {
	QString id;
	QString artistId;
	QString genre;
	QStringList tags;
	int durationMs = 0;
	int listenedMs = 0;
	bool finished = false;
	qint64 at = 0;
	QString key;  // нормализованные название и автор (для дедупликации перезаливов)
};

struct Blocked {
	QString artistId;
	QString name;
};

struct Shown {
	QString id;
	QString key;
	qint64 at = 0;
};

struct Library {
	std::vector<Liked> liked;      // новые сверху
	std::vector<QString> remoteLikes; // всё, что сейчас лайкнуто в аккаунте SoundCloud
	std::vector<Listen> history;   // старые сверху
	std::vector<Liked> boosted;    // «больше такого», новые сверху
	std::vector<Blocked> blocked;  // авторы, которых не предлагать
	std::vector<Shown> shown;      // что уже попадало в подборку, старые сверху
	std::vector<RecentItem> recent;
	std::vector<Playlist> playlists;
	rpl::event_stream<> playlistsChanged;
	bool loaded = false;
	rpl::event_stream<> likesChanged;
	rpl::event_stream<> recentChanged;
};

[[nodiscard]] Library &Lib();

[[nodiscard]] QString FilePath() {
	return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
		+ u"/mokagram_music/library.json"_q;
}

[[nodiscard]] QJsonObject ToJson(const Track &t) {
	auto o = QJsonObject();
	o.insert("id", t.id);
	o.insert("title", t.title);
	o.insert("artist", t.artist);
	o.insert("art", t.artworkUrl);
	o.insert("link", t.permalink);
	o.insert("dur", t.durationMs);
	o.insert("aid", t.artistId);
	o.insert("artistArt", t.artistArtworkUrl);
	o.insert("genre", t.genre);
	o.insert("tags", QJsonArray::fromStringList(t.tags));
	o.insert("plays", double(t.playbackCount));
	o.insert("likes", double(t.likesCount));
	o.insert("created", double(t.createdAt));
	return o;
}

[[nodiscard]] Track FromJson(const QJsonObject &o) {
	auto t = Track();
	t.id = o.value("id").toString();
	// id из файла может быть мусором: оставляем только числа и telegram-треки.
	const auto numeric = t.id.toULongLong();
	if (!t.id.startsWith(u"telegram:"_q)
		&& (numeric == 0 || QString::number(numeric) != t.id)) {
		t.id = QString();
	}
	t.title = o.value("title").toString();
	t.artist = o.value("artist").toString();
	t.artworkUrl = o.value("art").toString();
	t.permalink = o.value("link").toString();
	t.durationMs = o.value("dur").toInt();
	t.artistId = o.value("aid").toString();
	t.artistArtworkUrl = o.value("artistArt").toString();
	t.genre = o.value("genre").toString();
	for (const auto &tag : o.value("tags").toArray()) {
		t.tags.push_back(tag.toString());
	}
	t.playbackCount = qint64(o.value("plays").toDouble());
	t.likesCount = qint64(o.value("likes").toDouble());
	t.createdAt = qint64(o.value("created").toDouble());
	return t;
}

std::mutex SaveMutex;
std::uint64_t SaveWritten = 0;
std::atomic<std::uint64_t> SaveGeneration = 0;

void Save() {
	auto &lib = Lib();
	auto liked = QJsonArray();
	for (const auto &item : lib.liked) {
		auto o = ToJson(item.track);
		o.insert("at", double(item.at));
		if (item.remote) {
			o.insert("rem", true);
		}
		liked.push_back(o);
	}
	auto history = QJsonArray();
	for (const auto &h : lib.history) {
		auto o = QJsonObject();
		o.insert("id", h.id);
		o.insert("aid", h.artistId);
		o.insert("genre", h.genre);
		o.insert("tags", QJsonArray::fromStringList(h.tags));
		o.insert("dur", h.durationMs);
		o.insert("l", h.listenedMs);
		o.insert("fin", h.finished);
		o.insert("at", double(h.at));
		o.insert("k", h.key);
		history.push_back(o);
	}
	auto boosted = QJsonArray();
	for (const auto &item : lib.boosted) {
		auto o = ToJson(item.track);
		o.insert("at", double(item.at));
		boosted.push_back(o);
	}
	auto blocked = QJsonArray();
	for (const auto &b : lib.blocked) {
		auto o = QJsonObject();
		o.insert("aid", b.artistId);
		o.insert("name", b.name);
		blocked.push_back(o);
	}
	auto shown = QJsonArray();
	for (const auto &sh : lib.shown) {
		auto o = QJsonObject();
		o.insert("id", sh.id);
		o.insert("k", sh.key);
		o.insert("at", double(sh.at));
		shown.push_back(o);
	}
	auto root = QJsonObject();
	root.insert("liked", liked);
	root.insert("remoteLikes", QJsonArray::fromStringList(
		QStringList(lib.remoteLikes.begin(), lib.remoteLikes.end())));
	root.insert("history", history);
	root.insert("boosted", boosted);
	root.insert("blocked", blocked);
	root.insert("shown", shown);
	auto recent = QJsonArray();
	for (const auto &item : lib.recent) {
		auto o = QJsonObject();
		o.insert("album", item.isAlbum);
		if (item.isAlbum) {
			auto album = QJsonObject();
			album.insert("id", item.album.id);
			album.insert("title", item.album.title);
			album.insert("artist", item.album.artist);
			album.insert("art", item.album.artworkUrl);
			album.insert("count", item.album.trackCount);
			o.insert("data", album);
		} else {
			o.insert("data", ToJson(item.track));
		}
		recent.push_back(o);
	}
	root.insert("recent", recent);
	auto playlists = QJsonArray();
	for (const auto &playlist : lib.playlists) {
		auto tracks = QJsonArray();
		for (const auto &track : playlist.tracks) tracks.push_back(ToJson(track));
		auto item = QJsonObject();
		item.insert("id", playlist.id);
		item.insert("name", playlist.name);
		if (playlist.remote) {
			item.insert("remote", true);
			item.insert("source", playlist.sourceId);
		}
		item.insert("tracks", tracks);
		playlists.push_back(item);
	}
	root.insert("playlists", playlists);
	const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
	const auto generation = ++SaveGeneration;
	crl::async([bytes = std::move(bytes), path = FilePath(), generation] {
		auto lock = std::lock_guard(SaveMutex);
		if (generation < SaveWritten) {
			return;
		}
		SaveWritten = generation;
		QDir().mkpath(QFileInfo(path).absolutePath());
		auto file = QSaveFile(path);
		if (file.open(QIODevice::WriteOnly)) {
			file.write(bytes);
			file.commit();
		}
	});
}

Library &Lib() {
	static auto library = Library();
	if (!library.loaded) {
		library.loaded = true;
		auto file = QFile(FilePath());
		if (file.open(QIODevice::ReadOnly)) {
			const auto root = QJsonDocument::fromJson(file.readAll()).object();
			for (const auto &v : root.value("playlists").toArray()) {
				const auto o = v.toObject();
				auto playlist = Playlist();
				playlist.id = o.value("id").toString();
				playlist.name = o.value("name").toString();
				playlist.remote = o.value("remote").toBool();
				playlist.sourceId = o.value("source").toString();
				if (playlist.id.isEmpty() || playlist.name.trimmed().isEmpty()) continue;
				for (const auto &value : o.value("tracks").toArray()) {
					auto track = FromJson(value.toObject());
					if (!track.id.isEmpty() && !IsTelegramId(track.id)) playlist.tracks.push_back(std::move(track));
				}
				library.playlists.push_back(std::move(playlist));
			}
			for (const auto &v : root.value("recent").toArray()) {
				const auto o = v.toObject();
				const auto data = o.value("data").toObject();
				auto item = RecentItem();
				item.isAlbum = o.value("album").toBool();
				if (item.isAlbum) {
					item.album.id = data.value("id").toString();
					item.album.title = data.value("title").toString();
					item.album.artist = data.value("artist").toString();
					item.album.artworkUrl = data.value("art").toString();
					item.album.trackCount = data.value("count").toInt();
					if (item.album.id.isEmpty()) continue;
				} else {
					item.track = FromJson(data);
					if (item.track.id.isEmpty()) continue;
				}
				if (library.recent.size() < 30) {
					library.recent.push_back(std::move(item));
				}
			}
			for (const auto &v : root.value("remoteLikes").toArray()) {
				const auto id = v.toString();
				if (!id.isEmpty()) {
					library.remoteLikes.push_back(id);
				}
			}
			for (const auto &v : root.value("liked").toArray()) {
				const auto o = v.toObject();
				auto track = FromJson(o);
				if (track.id.isEmpty()) continue;
				library.liked.push_back({
					std::move(track),
					qint64(o.value("at").toDouble()),
					o.value("rem").toBool() });
			}
			for (const auto &v : root.value("history").toArray()) {
				const auto o = v.toObject();
				auto h = Listen();
				h.id = o.value("id").toString();
				h.artistId = o.value("aid").toString();
				h.genre = o.value("genre").toString();
				for (const auto &tag : o.value("tags").toArray()) {
					h.tags.push_back(tag.toString());
				}
				h.durationMs = o.value("dur").toInt();
				h.listenedMs = o.value("l").toInt();
				h.finished = o.value("fin").toBool();
				h.at = qint64(o.value("at").toDouble());
				h.key = o.value("k").toString();
				library.history.push_back(std::move(h));
			}
			for (const auto &v : root.value("boosted").toArray()) {
				const auto o = v.toObject();
				auto track = FromJson(o);
				if (track.id.isEmpty()) continue;
				library.boosted.push_back({ std::move(track), qint64(o.value("at").toDouble()) });
			}
			for (const auto &v : root.value("blocked").toArray()) {
				const auto o = v.toObject();
				library.blocked.push_back({ o.value("aid").toString(), o.value("name").toString() });
			}
			for (const auto &v : root.value("shown").toArray()) {
				const auto o = v.toObject();
				library.shown.push_back({
					o.value("id").toString(),
					o.value("k").toString(),
					qint64(o.value("at").toDouble()) });
			}
		}
	}
	return library;
}

// ---- нормализация для дедупликации ----

[[nodiscard]] QString NormalizePart(const QString &source) {
	static const auto brackets = QRegularExpression(
		u"[\\(\\[\\{][^\\)\\]\\}]*[\\)\\]\\}]"_q);
	static const auto noise = QRegularExpression(
		u"\\b(feat|ft|featuring|prod|remix|edit|mix|official|remaster|remastered|slowed|sped)\\b.*$"_q,
		QRegularExpression::CaseInsensitiveOption
			| QRegularExpression::UseUnicodePropertiesOption);
	auto text = source;
	text.remove(brackets);
	auto cleaned = text;
	cleaned.remove(noise);
	if (cleaned.trimmed().isEmpty()) {
		cleaned = text.isEmpty() ? source : text;
	}
	auto result = QString();
	for (const auto ch : cleaned.toLower()) {
		if (ch.isLetterOrNumber()) {
			result.append(ch);
		}
	}
	return result;
}

[[nodiscard]] QString NormKey(const QString &title, const QString &artist) {
	return NormalizePart(title) + '|' + NormalizePart(artist);
}

[[nodiscard]] QString NormKey(const Track &t) {
	return NormKey(t.title, t.artist);
}

// Ключи для сравнения: по id, по названию с автором и по длинному названию
// (перезалив того же трека другим аккаунтом).
[[nodiscard]] std::vector<QString> KeysFor(const QString &id, const QString &key) {
	auto result = std::vector<QString>();
	if (!id.isEmpty()) {
		result.push_back(u"i:"_q + id);
	}
	if (key.size() > 1) {
		result.push_back(key);
		const auto title = key.left(key.indexOf('|'));
		if (title.size() >= 10) {
			result.push_back(u"t:"_q + title);
		}
	}
	return result;
}

// Источник трека: «Автор - Название» часто заливают чужие аккаунты, поэтому
// сравниваем по имени из названия, а не только по аккаунту.
[[nodiscard]] QString Origin(const Track &t) {
	const auto dash = t.title.indexOf(u" - "_q);
	if (dash > 1) {
		const auto name = NormalizePart(t.title.left(dash));
		if (name.size() >= 3) {
			return name;
		}
	}
	return NormalizePart(t.artist);
}

[[nodiscard]] std::vector<QString> KeysFor(const Track &t) {
	return KeysFor(t.id, NormKey(t));
}

// ---- профиль вкуса ----

struct Profile {
	std::map<QString, double> track;
	std::map<QString, double> artist;
	std::map<QString, double> genre;
	std::map<QString, double> tag;
	// Кулдауны по ключам (id, название+автор): дослушан, прослушан частично, пропущен.
	std::map<QString, qint64> finishedKeys;
	std::map<QString, qint64> partialKeys;
	std::map<QString, qint64> skippedKeys;
	// Контекст сессии: последние дослушанные треки (новые весят больше).
	std::map<QString, double> sessionGenre;
	std::map<QString, double> sessionTag;
	std::vector<QString> recentFinishedIds;  // новые первыми
	bool likesLong = false;
	// Доля кириллицы среди лайкнутых и «больше такого»: язык вкуса.
	// Меньше 0, пока явных сигналов мало.
	double cyrillicShare = -1.;
};

[[nodiscard]] bool HasCyrillic(const Track &t) {
	const auto has = [](const QString &text) {
		for (const auto ch : text) {
			const auto code = ch.unicode();
			if ((code >= 0x0400 && code <= 0x04FF)) {
				return true;
			}
		}
		return false;
	};
	if (has(t.title) || has(t.artist)) {
		return true;
	}
	for (const auto &tag : t.tags) {
		if (has(tag)) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] double Decay(qint64 at, qint64 now) {
	const auto days = std::max<qint64>(now - at, 0) / 86400.;
	return std::pow(0.5, days / kHalfLifeDays);
}

void AddSignalEx(
		Profile &p,
		const QString &trackId,
		const QString &artistId,
		const QString &genre,
		const QStringList &tags,
		double wTrack,
		double wArtist,
		double wGenre,
		double wTag) {
	if (!trackId.isEmpty()) {
		p.track[trackId] += wTrack;
	}
	if (!artistId.isEmpty()) {
		p.artist[artistId] += wArtist;
	}
	if (!genre.isEmpty()) {
		p.genre[genre] += wGenre;
	}
	const auto norm = tags.isEmpty() ? 1. : 1. / std::sqrt(double(tags.size()));
	for (const auto &tag : tags) {
		p.tag[tag] += wTag * norm;
	}
}

void AddSignal(
		Profile &p,
		const QString &trackId,
		const QString &artistId,
		const QString &genre,
		const QStringList &tags,
		double weight) {
	AddSignalEx(p, trackId, artistId, genre, tags, weight, weight, weight, weight);
}

void Remember(
		std::map<QString, qint64> &map,
		const QString &id,
		const QString &key,
		qint64 at) {
	for (const auto &k : KeysFor(id, key)) {
		auto &value = map[k];
		value = std::max(value, at);
	}
}

[[nodiscard]] Profile BuildProfile(const Track &current) {
	auto p = Profile();
	const auto now = QDateTime::currentSecsSinceEpoch();
	// Тег, который встречается только у одного автора, это его ник, а не звучание:
	// по нему подборка превращается в «всё от этого автора».
	auto tagArtists = std::map<QString, std::set<QString>>();
	const auto collectTags = [&](const QString &artistId, const QStringList &tags) {
		for (const auto &tag : tags) {
			tagArtists[tag].insert(artistId);
		}
	};
	for (const auto &item : Lib().liked) {
		collectTags(item.track.artistId, item.track.tags);
	}
	for (const auto &item : Lib().boosted) {
		collectTags(item.track.artistId, item.track.tags);
	}
	for (const auto &h : Lib().history) {
		collectTags(h.artistId, h.tags);
	}
	collectTags(current.artistId, current.tags);
	const auto clean = [&](const QStringList &tags) {
		auto result = QStringList();
		for (const auto &tag : tags) {
			if (tagArtists[tag].size() >= 2) {
				result.push_back(tag);
			}
		}
		return result;
	};
	for (const auto &item : Lib().liked) {
		const auto &t = item.track;
		AddSignal(p, t.id, t.artistId, t.genre, clean(t.tags), 3. * Decay(item.at, now));
		p.likesLong = p.likesLong || (t.durationMs > kMaxDurationMs);
	}
	// «Больше такого»: сильнее лайка по тегам и жанру.
	for (const auto &item : Lib().boosted) {
		const auto &t = item.track;
		const auto d = Decay(item.at, now);
		AddSignalEx(p, t.id, t.artistId, t.genre, clean(t.tags), 4. * d, 2. * d, 2. * d, 4. * d);
	}
	{
		auto cyrillic = 0;
		auto total = 0;
		const auto count = [&](const Track &t) {
			++total;
			cyrillic += HasCyrillic(t) ? 1 : 0;
		};
		for (const auto &item : Lib().liked) {
			count(item.track);
		}
		for (const auto &item : Lib().boosted) {
			count(item.track);
		}
		if (total >= 5) {
			p.cyrillicShare = cyrillic / double(total);
		}
	}
	auto recent = std::vector<const Listen*>();
	for (const auto &h : Lib().history) {
		const auto ratio = (h.durationMs > 0)
			? std::min(1., h.listenedMs / double(h.durationMs))
			: 0.;
		const auto d = Decay(h.at, now);
		const auto done = h.finished || ratio >= 0.8;
		if (done) {
			Remember(p.finishedKeys, h.id, h.key, h.at);
			AddSignal(p, h.id, h.artistId, h.genre, clean(h.tags), d);
			recent.push_back(&h);
		} else if (h.listenedMs < kEarlySkipMs) {
			// Пропуск в первые секунды: сильный минус треку, слабее автору и тегам.
			Remember(p.skippedKeys, h.id, h.key, h.at);
			AddSignalEx(p, h.id, h.artistId, h.genre, clean(h.tags), -2. * d, -1. * d, -0.4 * d, -0.6 * d);
		} else if (ratio < 0.25) {
			Remember(p.skippedKeys, h.id, h.key, h.at);
			AddSignal(p, h.id, h.artistId, h.genre, clean(h.tags), -0.8 * d);
		} else if (ratio >= 0.35) {
			Remember(p.partialKeys, h.id, h.key, h.at);
			AddSignal(p, h.id, h.artistId, h.genre, clean(h.tags), 0.25 * d);
		} else {
			Remember(p.partialKeys, h.id, h.key, h.at);
		}
	}
	// Сессия: последние 10 дослушанных и играющий сейчас трек.
	auto addSession = [&](const QString &genre, const QStringList &tags, double w) {
		if (!genre.isEmpty()) {
			p.sessionGenre[genre] += w;
		}
		const auto norm = tags.isEmpty() ? 1. : 1. / std::sqrt(double(tags.size()));
		for (const auto &tag : tags) {
			p.sessionTag[tag] += w * norm;
		}
	};
	if (!current.id.isEmpty()) {
		addSession(current.genre, clean(current.tags), 1.4);
	}
	auto weight = 1.;
	for (auto i = int(recent.size()) - 1, n = 0; i >= 0 && n < 10; --i, ++n) {
		const auto &h = *recent[i];
		addSession(h.genre, clean(h.tags), weight);
		if (n < 3) {
			p.recentFinishedIds.push_back(h.id);
		}
		weight = std::max(0.3, weight - 0.07);
	}
	return p;
}

[[nodiscard]] double Get(const std::map<QString, double> &m, const QString &key) {
	const auto i = m.find(key);
	return (i != m.end()) ? i->second : 0.;
}

[[nodiscard]] std::vector<QString> TopKeys(
		const std::map<QString, double> &m,
		int count,
		double minimum = 0.) {
	auto items = std::vector<std::pair<QString, double>>(m.begin(), m.end());
	std::sort(items.begin(), items.end(), [](const auto &a, const auto &b) {
		return a.second > b.second;
	});
	auto result = std::vector<QString>();
	for (const auto &[key, value] : items) {
		if (value <= minimum || int(result.size()) >= count) {
			break;
		}
		result.push_back(key);
	}
	return result;
}

[[nodiscard]] double Random01() {
	return base::RandomIndex(10000) / 10000.;
}

// Насколько трек подходит к тому, что играет в этой сессии: 0..1.
[[nodiscard]] double SessionMatch(const Profile &p, const Track &t) {
	auto maxGenre = 0., maxTag = 0.;
	for (const auto &[k, v] : p.sessionGenre) {
		maxGenre = std::max(maxGenre, v);
	}
	for (const auto &[k, v] : p.sessionTag) {
		maxTag = std::max(maxTag, v);
	}
	const auto genre = (maxGenre > 0.)
		? Get(p.sessionGenre, t.genre) / maxGenre
		: 0.;
	auto tag = 0.;
	if (maxTag > 0.) {
		for (const auto &name : t.tags) {
			tag += Get(p.sessionTag, name) / maxTag;
		}
		tag = std::min(1., tag / 2.);
	}
	return 0.45 * std::min(1., genre) + 0.55 * tag;
}

// Плавность перехода между двумя треками: жанр, теги, длительность. 0..1.
[[nodiscard]] double Smoothness(const Track &a, const Track &b) {
	if (a.id.isEmpty()) {
		return 0.;
	}
	auto result = 0.;
	if (!a.genre.isEmpty() && a.genre == b.genre) {
		result += 0.4;
	}
	if (!a.tags.isEmpty() && !b.tags.isEmpty()) {
		auto common = 0;
		for (const auto &tag : b.tags) {
			common += a.tags.contains(tag) ? 1 : 0;
		}
		const auto all = a.tags.size() + b.tags.size() - common;
		result += 0.4 * (all > 0 ? common / double(all) : 0.);
	}
	if (a.durationMs > 0 && b.durationMs > 0) {
		const auto diff = std::abs(a.durationMs - b.durationMs);
		result += 0.2 * (1. - std::min(1., diff / double(std::max(a.durationMs, b.durationMs))));
	}
	return result;
}

[[nodiscard]] bool IsBlockedArtist(const Track &t) {
	const auto name = NormalizePart(t.artist);
	const auto origin = Origin(t);
	for (const auto &b : Lib().blocked) {
		if (!b.artistId.isEmpty() && b.artistId == t.artistId) {
			return true;
		}
		const auto blocked = NormalizePart(b.name);
		if (!blocked.isEmpty() && (blocked == name || blocked == origin)) {
			return true;
		}
	}
	return false;
}

void MarkShown(const std::vector<Track> &tracks) {
	auto &lib = Lib();
	const auto now = QDateTime::currentSecsSinceEpoch();
	for (const auto &t : tracks) {
		lib.shown.push_back({ t.id, NormKey(t), now });
	}
	if (int(lib.shown.size()) > kShownLimit) {
		lib.shown.erase(
			lib.shown.begin(),
			lib.shown.begin() + (lib.shown.size() - kShownLimit));
	}
	Save();
}

struct Candidate {
	Track track;
	double seedHits = 0.;
	bool exploration = false;
};

struct Collector {
	std::map<QString, Candidate> candidates;
	int pending = 0;
	Fn<void()> finish;
};

void Gather(
		const std::shared_ptr<Collector> &collector,
		std::vector<Track> tracks,
		double seedWeight,
		bool exploration) {
	for (auto &track : tracks) {
		auto &c = collector->candidates[track.id];
		if (c.track.id.isEmpty()) {
			c.track = std::move(track);
			c.exploration = exploration;
		}
		c.seedHits += seedWeight;
	}
	if (--collector->pending == 0) {
		collector->finish();
	}
}

// Типографские кавычки, ё и лишние пробелы: запрос не должен зависеть от того,
// как трек назвали в SoundCloud.
[[nodiscard]] QString CleanQuery(const QString &source) {
	auto text = source;
	text.replace(u'«', u'"').replace(u'»', u'"');
	text.replace(u'“', u'"').replace(u'”', u'"');
	text.replace(u'`', u'"').replace(u'\'', u' ');
	text.replace(u'ё', u'е').replace(u'Ё', u'Е');
	return text.simplified();
}

// Ключ автора для разнообразия подборки: аккаунт, иначе ник из названия.
[[nodiscard]] QString OriginKey(const Track &t) {
	return t.artistId.isEmpty()
		? u"o:"_q + Origin(t)
		: u"a:"_q + t.artistId;
}

// Название без служебных слов: «Bo Chax (prod. by X)» для поиска почти бесполезно,
// а «бо чакс» и «bo chax» находят одно и то же.
[[nodiscard]] QString SearchTitle(const QString &title, const QString &artist) {
	static const auto noise = QRegularExpression(
		u"\\([^\\)]*\\)|\\[[^\\]]*\\]|\\{[^\\}]*\\}"_q);
	auto text = CleanQuery(title);
	auto titleKey = NormalizePart(text);
	if (!artist.isEmpty()) {
		// «Автор - Название» или «Автор — Название»: для поиска по звучанию нужна
		// только часть после тире, иначе ищется сам автор, а это уже «Ещё от».
		static const auto dashPattern = QRegularExpression(u"\\s[\\-–—]\\s"_q);
		const auto artistKey = NormalizePart(artist);
		auto match = dashPattern.match(text);
		while (match.hasMatch()) {
			if (NormalizePart(text.left(match.capturedStart())) == artistKey) {
				text = text.mid(match.capturedEnd()).trimmed();
				break;
			}
			match = dashPattern.match(text, match.capturedEnd());
		}
		titleKey = NormalizePart(text);
	}
	text = text.remove(noise).simplified();
	if (NormalizePart(text).size() >= 3) {
		return text;
	}
	// Всё название было в скобках («(Intro)»): оставляем как есть.
	return titleKey.isEmpty() ? title : text.isEmpty() ? title : text;
}

// Слова названия и тегов без служебного шума: по ним трек понимается
// по-настоящему («хорроркор», «бэнгер»), а не только по имени автора.
[[nodiscard]] QStringList TasteWords(const Track &t) {
	auto words = QStringList();
	static const auto service = QRegularExpression(
		u"\\b(prod|remix|edit|mix|instrumental|original|official|audio|video|clip|lyrics|"
		u"slowed|reverb|speed|sped|nightcore|extended|version|feat|ft|by|the|and|now|out)\\b"_q,
		QRegularExpression::CaseInsensitiveOption
			| QRegularExpression::UseUnicodePropertiesOption);
	const auto add = [&](const QString &text) {
		const auto cleaned = CleanQuery(text).remove(service).simplified();
		for (const auto &word : cleaned.split(
				QRegularExpression(u"[^\\p{L}\\p{N}#]+"_q),
				Qt::SkipEmptyParts)) {
			const auto lower = word.toLower();
			if (lower.size() >= 3 && lower.size() <= 20
				&& !words.contains(lower)) {
				words.push_back(lower);
			}
		}
	};
	const auto addTag = [&](const QString &tag) {
		const auto cleaned = CleanQuery(tag).remove(service).simplified();
		// Тег целиком нужен как точное совпадение звучания, а не только словами.
		if (cleaned.size() >= 3 && cleaned.size() <= 24 && !cleaned.contains(u' ')
			&& !words.contains(cleaned.toLower())) {
			words.push_back(cleaned.toLower());
		}
		for (const auto &part : cleaned.split(u' ', Qt::SkipEmptyParts)) {
			if (part.size() >= 3 && !words.contains(part.toLower())) {
				words.push_back(part.toLower());
			}
		}
	};
	addTag(t.genre);
	for (const auto &tag : t.tags) {
		addTag(tag);
	}
	const auto hasPrefix = [](const QString &text, const QString &key) {
		return text.size() >= 4 && key.size() >= 4 && text.startsWith(key);
	};
	const auto artistKey = NormalizePart(t.artist);
	const auto addTitleWord = [&](const QString &word) {
		const auto lower = word.toLower();
		const auto key = NormalizePart(lower);
		// Слово автора в названии («Pharaoh - ...») это не звучание.
		if (lower.size() < 3 || lower.size() > 20 || words.contains(lower)
			|| (key.size() >= 4 && (key == artistKey
				|| hasPrefix(key, artistKey)
				|| hasPrefix(artistKey, key)))) {
			return;
		}
		words.push_back(lower);
	};
	const auto title = SearchTitle(t.title, t.artist);
	for (const auto &word : title.split(u' ', Qt::SkipEmptyParts)) {
		addTitleWord(word);
	}
	// Автор не тег, но его ник в названии — не звучание: такое слово отсекается.
	return words;
}

// Сила совпадения слова с треком, 0..1: жанр весит больше тегов, теги больше
// названия. Типографика и словоформы не мешают: сравниваем ключи.
[[nodiscard]] double WordMatch(const QString &word, const Track &t) {
	const auto key = NormalizePart(word);
	if (key.size() < 3) {
		return 0.;
	}
	const auto close = [&](const QString &text) {
		const auto other = NormalizePart(text);
		if (other.size() < 3) {
			return false;
		}
		if (other == key) {
			return true;
		}
		const auto shorter = std::min(other.size(), key.size());
		return shorter >= 4
			&& (other.startsWith(key) || key.startsWith(other));
	};
	if (close(t.genre)) {
		return 1.;
	}
	for (const auto &tag : t.tags) {
		if (close(tag)) {
			return 0.9;
		}
	}
	if (close(t.title)) {
		return 0.7;
	}
	return 0.;
}

struct SimilarContext {
	Track seed;
	std::vector<Track> avoid;
	int count = 0;
	QStringList tasteWords;
};

void BuildSimilarImpl(SimilarContext context, Fn<void(std::vector<Track>, QString)> done) {
	auto seed = std::move(context.seed);
	seed.title = CleanQuery(seed.title);
	seed.artist = CleanQuery(seed.artist);
	if (seed.title.trimmed().isEmpty()) {
		done({}, QString::fromUtf8(Mokagram::Lang::Tr(
			"У трека нет названия: похожее подобрать не из чего.")));
		return;
	}
	const auto ctx = std::make_shared<SimilarContext>(std::move(context));
	ctx->seed = seed;
	ctx->tasteWords = TasteWords(seed);
	const auto now = QDateTime::currentSecsSinceEpoch();
	const auto profile = std::make_shared<Profile>(BuildProfile(seed));
	const auto seedOrigin = OriginKey(seed);

	// Один и тот же трек под разными id: сам трек, уже показанное рядом, лайки.
	auto excluded = std::set<QString>();
	for (const auto &k : KeysFor(seed)) {
		excluded.insert(k);
	}
	for (const auto &t : ctx->avoid) {
		for (const auto &k : KeysFor(t)) {
			excluded.insert(k);
		}
	}
	for (const auto &l : Lib().liked) {
		for (const auto &k : KeysFor(l.track)) {
			excluded.insert(k);
		}
	}
	for (const auto &b : Lib().boosted) {
		for (const auto &k : KeysFor(b.track)) {
			excluded.insert(k);
		}
	}

	const auto collector = std::make_shared<Collector>();
	collector->finish = [=] {
		auto scored = std::vector<std::pair<Track, double>>();
		for (const auto &[id, c] : collector->candidates) {
			const auto &t = c.track;
			const auto keys = KeysFor(t);
			auto skip = false;
			for (const auto &k : keys) {
				skip = skip || excluded.contains(k);
			}
			// Мусор: превью и очень длинные миксы (если автор не слушает такое).
			if (t.durationMs > 0
				&& (t.durationMs < kMinDurationMs
					|| (t.durationMs > kMaxDurationMs && !profile->likesLong))) {
				skip = true;
			}
			if (skip || IsBlockedArtist(t)) {
				continue;
			}
			// Семантика: слова жанра и настроения из названия и тегов сида.
			auto semantic = 0.;
			for (const auto &word : ctx->tasteWords) {
				semantic += WordMatch(word, t);
			}
			semantic = std::min(1., semantic / 1.5);
			// Сходство по SoundCloud (похожие следом за сидом) и по словам.
			auto value = 1.4 * std::min(1., c.seedHits / 1.5);
			value += 1.5 * semantic;
			if (!t.genre.isEmpty() && !seed.genre.isEmpty() && t.genre == seed.genre) {
				value += 0.5;
			}
			// Вкус слушателя не перебивает звучание, но помогает выбрать из равных.
			auto tagAffinity = 0.;
			for (const auto &tag : t.tags) {
				tagAffinity += std::tanh(Get(profile->tag, tag));
			}
			value += 0.5 * std::min(1., tagAffinity / 2.);
			if (!t.artistId.isEmpty()) {
				value += 0.3 * std::tanh(Get(profile->artist, t.artistId));
			}
			value += 0.25 * std::clamp(std::log10(t.playbackCount + 1.) / 7., 0., 1.);
			if (t.playbackCount > 0) {
				value += 0.2 * std::min(1., (t.likesCount / double(t.playbackCount)) * 25.);
			}
			if (t.createdAt > 0 && (now - t.createdAt) < 365 * 86400) {
				value += 0.15;
			}
			// Автор сида не повторяется: для него есть отдельный ряд «Ещё от».
			if (OriginKey(t) == seedOrigin) {
				value -= 6.;
			}
			value += (Random01() - 0.5) * 0.2;
			scored.push_back(std::make_pair(t, value));
		}
		std::sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) {
			return a.second > b.second;
		});
		const auto wanted = std::max(ctx->count, 6);
		auto result = std::vector<Track>();
		auto origins = std::map<QString, int>();
		auto genres = std::map<QString, int>();
		const auto perOrigin = std::max(1, wanted / 4);
		for (const auto &[track, value] : scored) {
			// Слабое совпадение не тянем ради количества: лучше показать меньше.
			if (int(result.size()) >= wanted || value < 0.35) {
				break;
			}
			const auto origin = OriginKey(track);
			if (origins[origin] >= perOrigin
				|| (!track.genre.isEmpty()
					&& genres[track.genre] > (int(result.size()) / 2) + 1)) {
				continue;
			}
			++origins[origin];
			++genres[track.genre];
			result.push_back(track);
		}
		if (collector->candidates.empty()) {
			done({}, QString::fromUtf8(Mokagram::Lang::Tr(
				"Не нашлось похожего по звучанию. Попробуйте позже.")));
			return;
		}
		MarkShown(result);
		done(std::move(result), QString());
	};

	// Запросы: похожие по SoundCloud, жанр и теги, настроение из слов названия,
	// для треков без тегов — название в кавычках и «автор название».
	auto queries = QStringList();
	const auto addQuery = [&](QString query) {
		const auto pieces = CleanQuery(query).split(u' ', Qt::SkipEmptyParts);
		if (int(pieces.size()) > 5) {
			return;
		}
		query = pieces.join(u' ');
		if (query.size() >= 3 && !query.contains(u'"')
			&& !queries.contains(query, Qt::CaseInsensitive)) {
			queries.push_back(std::move(query));
		}
	};
	addQuery(seed.genre);
	for (const auto &tag : seed.tags) {
		addQuery(tag);
	}
	for (const auto &word : ctx->tasteWords) {
		addQuery(u'"' + word + u'"');
	}
	addQuery(seed.artist + u' ' + SearchTitle(seed.title, seed.artist));
	if (seed.tags.isEmpty() && seed.genre.isEmpty()) {
		const auto title = SearchTitle(seed.title, seed.artist);
		addQuery(u'"' + title + u'"');
		const auto pieces = title.split(u' ', Qt::SkipEmptyParts);
		if (pieces.size() > 2) {
			addQuery(pieces.front() + u' ' + pieces.back());
		}
	}
	if (queries.empty() && seed.id.isEmpty()) {
		done({}, QString::fromUtf8(Mokagram::Lang::Tr(
			"Нет жанра и тегов: поищу похожее позже.")));
		return;
	}
	// Пустой ответ тоже закрывает свою долю в collector; счётчик до запуска (кеш синхронный).
	const auto relatedJobs = int(seed.id.isEmpty() ? 0 : 1);
	const auto searchJobs = std::min(int(queries.size()), 6);
	collector->pending = relatedJobs + searchJobs;
	if (!seed.id.isEmpty()) {
		Related(seed.id, 40, [=](std::vector<Track> list) {
			Gather(collector, std::move(list), 1.5, false);
		});
	}
	for (auto i = 0; i != searchJobs; ++i) {
		SearchLimited(queries[i], 20, [=](std::vector<Track> list) {
			Gather(collector, std::move(list), 0.6, false);
		});
	}
}

void BuildImpl(
		StreamContext context,
		int count,
		bool stream,
		Fn<void(std::vector<Track>, QString)> done) {
	if (IsTelegramId(context.current.id)) {
		context.current = Track();
	}
	if (!HasTaste() && context.current.id.isEmpty()) {
		done({}, QString::fromUtf8(Mokagram::Lang::Tr("Пока нечего анализировать. Поставьте сердечко нескольким трекам и послушайте музыку: подборка подстроится под вас.")));
		return;
	}
	const auto ctx = std::make_shared<StreamContext>(std::move(context));
	const auto profile = std::make_shared<Profile>(BuildProfile(ctx->current));
	const auto now = QDateTime::currentSecsSinceEpoch();

	// Семена: лучшие треки по вкусу, часть выбирается случайно из следующих.
	auto ranked = std::vector<std::pair<QString, double>>(
		profile->track.begin(),
		profile->track.end());
	std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) {
		return a.second > b.second;
	});
	auto seeds = std::vector<std::pair<QString, double>>();
	const auto maxSeed = (!ranked.empty() && ranked[0].second > 1.)
		? ranked[0].second
		: 1.;
	auto seedIds = std::set<QString>();
	const auto addSeed = [&](const QString &id, double weight) {
		if (id.isEmpty() || IsTelegramId(id) || !seedIds.insert(id).second) {
			return;
		}
		seeds.push_back({ id, weight });
	};
	// Что играет сейчас и последние дослушанные: главный сигнал для потока.
	addSeed(ctx->current.id, 1.);
	for (const auto &id : profile->recentFinishedIds) {
		addSeed(id, 0.8);
	}
	if (!ranked.empty() && ranked[0].second > 0.) {
		addSeed(ranked[0].first, std::clamp(ranked[0].second / maxSeed, 0.3, 1.));
	}
	auto pool = std::vector<std::pair<QString, double>>();
	for (auto i = 1; i < std::min<int>(ranked.size(), 30); ++i) {
		if (ranked[i].second > 0.) {
			pool.push_back(ranked[i]);
		}
	}
	const auto randomSeeds = stream ? 4 : 8;
	for (auto picked = 0; picked != randomSeeds && !pool.empty(); ++picked) {
		const auto at = base::RandomIndex(int(pool.size()));
		addSeed(pool[at].first, std::clamp(pool[at].second / maxSeed, 0.3, 1.));
		pool.erase(pool.begin() + at);
	}
	const auto topGenres = TopKeys(profile->genre, 2, 0.5);
	const auto topTags = TopKeys(profile->tag, 2, 0.5);
	const auto sessionTags = TopKeys(profile->sessionTag, 1, 0.);

	const auto collector = std::make_shared<Collector>();
	collector->finish = [=] {
		const auto &lib = Lib();
		// Жёсткие исключения: уже играет или стоит в очереди, лайкнуто.
		auto excluded = std::set<QString>();
		auto avoidArtists = std::set<QString>();
		const auto exclude = [&](const Track &t, bool artist) {
			for (const auto &k : KeysFor(t)) {
				excluded.insert(k);
			}
			if (artist) {
				if (!t.artistId.isEmpty()) {
					avoidArtists.insert(t.artistId);
				}
				avoidArtists.insert(Origin(t));
			}
		};
		for (const auto &l : lib.liked) {
			for (const auto &k : KeysFor(l.track)) {
				excluded.insert(k);
			}
		}
		for (const auto &b : lib.boosted) {
			for (const auto &k : KeysFor(b.track)) {
				excluded.insert(k);
			}
		}
		if (!ctx->current.id.isEmpty()) {
			exclude(ctx->current, true);
		}
		for (const auto &t : ctx->avoid) {
			exclude(t, ctx->avoidPreviousArtists);
		}
		auto shownAt = std::map<QString, qint64>();
		for (const auto &sh : lib.shown) {
			Remember(shownAt, sh.id, sh.key, sh.at);
		}
		const auto within = [&](const std::map<QString, qint64> &map, const std::vector<QString> &keys, qint64 span) {
			for (const auto &k : keys) {
				const auto i = map.find(k);
				if (i != map.end() && (now - i->second) < span) {
					return true;
				}
			}
			return false;
		};
		struct Scored {
			Track track;
			double score = 0.;
			bool known = false;
		};
		auto scored = std::vector<Scored>();
		for (const auto &[id, c] : collector->candidates) {
			const auto &t = c.track;
			const auto keys = KeysFor(t);
			auto skip = false;
			for (const auto &k : keys) {
				skip = skip || excluded.contains(k);
			}
			// Мусор: превью и очень длинные миксы (если автор не слушает такое сам).
			if (t.durationMs > 0
				&& (t.durationMs < kMinDurationMs
					|| (t.durationMs > kMaxDurationMs && !profile->likesLong))) {
				skip = true;
			}
			if (skip
				|| IsBlockedArtist(t)
				|| (!t.artistId.isEmpty() && avoidArtists.contains(t.artistId))
				|| avoidArtists.contains(Origin(t))
				|| within(profile->finishedKeys, keys, kFinishedCooldown)
				|| within(profile->partialKeys, keys, kPartialCooldown)
				|| within(profile->skippedKeys, keys, kSkippedCooldown)) {
				continue;
			}
			auto score = 1.6 * std::min(1., c.seedHits / 1.5);
			const auto artistAffinity = Get(profile->artist, t.artistId);
			score += 0.3 * std::tanh(artistAffinity);
			score += 0.9 * std::tanh(Get(profile->genre, t.genre));
			auto tagAffinity = 0.;
			for (const auto &tag : t.tags) {
				tagAffinity += std::tanh(Get(profile->tag, tag));
			}
			score += 0.9 * std::min(1., tagAffinity / 2.);
			score += 1.1 * SessionMatch(*profile, t);
			score += 0.25 * std::clamp(std::log10(t.playbackCount + 1.) / 7., 0., 1.);
			if (t.playbackCount > 0) {
				score += 0.2 * std::min(1., (t.likesCount / double(t.playbackCount)) * 25.);
			}
			if (t.createdAt > 0 && (now - t.createdAt) < 365 * 86400) {
				score += 0.15;
			}
			if (artistAffinity < -0.8) {
				score -= 2.;
			}
			// Язык вкуса: русский слушатель не хочет латиницу и наоборот.
			if (profile->cyrillicShare >= 0.) {
				const auto cyr = HasCyrillic(t);
				if (profile->cyrillicShare >= 0.6) {
					score += cyr ? 0.6 : -2.5 * profile->cyrillicShare;
				} else if (profile->cyrillicShare <= 0.2) {
					score += cyr ? -2.5 * (1. - profile->cyrillicShare) : 0.3;
				}
			}
			if (within(shownAt, keys, kShownCooldown)) {
				score -= 2.; // уже предлагали недавно
			}
			scored.push_back({ t, score, artistAffinity > 0.3 });
		}
		if (scored.empty()) {
			done({}, QString::fromUtf8(Mokagram::Lang::Tr("Не нашлось подходящих треков. Попробуйте позже.")));
			return;
		}
		// Жадный отбор: штрафы за повтор исполнителя и жанра, плавный переход от
		// предыдущего трека, знакомых авторов около 20% (остальное новое по вайбу).
		const auto knownMax = std::max(1, int(std::round(count * 0.2)));
		auto result = std::vector<Track>();
		auto artistCount = std::map<QString, int>();
		auto genreCount = std::map<QString, int>();
		auto titles = excluded;
		auto unknown = 0;
		while (int(result.size()) < count && !scored.empty()) {
			auto best = -1;
			auto bestValue = -1e9;
			const auto previous = result.empty() ? ctx->current : result.back();
			for (auto i = 0; i != int(scored.size()); ++i) {
				const auto &c = scored[i];
				auto value = c.score;
				const auto origin = Origin(c.track);
				value -= 0.55 * artistCount[origin];
				if (artistCount[origin] >= 2) {
					value -= 3.;
				}
				if (!result.empty()
					&& genreCount[c.track.genre] > int(result.size()) / 2 + 1) {
					value -= 0.3;
				}
				if (!c.known) {
					value += 0.5;
				} else if (int(result.size()) - unknown >= knownMax) {
					value -= 0.8;
				}
				// Тот же автор не чаще раза в 5 треков.
				for (auto back = 0; back != 4 && back != int(result.size()); ++back) {
					if (Origin(result[result.size() - 1 - back]) == origin) {
						value -= 3.;
						break;
					}
				}
				value += 0.7 * Smoothness(previous, c.track);
				value += (Random01() - 0.5) * 0.3;
				if (value > bestValue) {
					bestValue = value;
					best = i;
				}
			}
			auto picked = scored[best];
			scored.erase(scored.begin() + best);
			auto repeated = false;
			for (const auto &k : KeysFor(picked.track)) {
				repeated = repeated || titles.contains(k);
			}
			if (repeated) {
				continue;
			}
			for (const auto &k : KeysFor(picked.track)) {
				titles.insert(k);
			}
			++artistCount[Origin(picked.track)];
			++genreCount[picked.track.genre];
			unknown += picked.known ? 0 : 1;
			result.push_back(std::move(picked.track));
		}
		// Немного знакомого: пара любимых треков внутри подборки (не в потоке).
		if (!stream && lib.liked.size() >= 5 && result.size() > 6) {
			for (const auto position : { 8 }) {
				if (position < int(result.size())) {
					const auto &pick = lib.liked[base::RandomIndex(int(lib.liked.size()))];
					const auto exists = std::any_of(result.begin(), result.end(), [&](const Track &t) {
						return t.id == pick.track.id;
					});
					if (!exists) {
						result.insert(result.begin() + position, pick.track);
					}
				}
			}
		}
		MarkShown(result);
		done(std::move(result), QString());
	};

	// Запросы кандидатов: related по семенам, поиск по жанрам и тегам.
	// Треки любимых исполнителей не запрашиваем: подборка про звучание, а не про авторов.
	auto jobs = std::vector<Fn<void()>>();
	for (const auto &[id, weight] : seeds) {
		const auto w = weight;
		jobs.push_back([=] {
			Related(id, 25, [=](std::vector<Track> list) {
				Gather(collector, std::move(list), w, false);
			});
		});
	}
	for (const auto &genre : topGenres) {
		jobs.push_back([=] {
			SearchLimited(genre, 20, [=](std::vector<Track> list) {
				Gather(collector, std::move(list), 0.2, true);
			});
		});
	}
	for (const auto &tag : topTags) {
		jobs.push_back([=] {
			SearchLimited(tag, 20, [=](std::vector<Track> list) {
				Gather(collector, std::move(list), 0.2, true);
			});
		});
	}
	// Тег текущей сессии: поток следует за настроением, а не только за долгим профилем.
	for (const auto &tag : sessionTags) {
		if (std::find(topTags.begin(), topTags.end(), tag) != topTags.end()) {
			continue;
		}
		jobs.push_back([=] {
			SearchLimited(tag, 20, [=](std::vector<Track> list) {
				Gather(collector, std::move(list), 0.4, true);
			});
		});
	}
	if (jobs.empty()) {
		done({}, QString::fromUtf8(Mokagram::Lang::Tr("Мало данных о вкусе: послушайте и полайкайте ещё несколько треков.")));
		return;
	}
	collector->pending = int(jobs.size());
	for (const auto &job : jobs) {
		job();
	}
}

} // namespace

bool IsTelegramId(const QString &trackId) {
	return trackId.startsWith(u"telegram:"_q);
}

bool IsLiked(const QString &trackId) {
	for (const auto &item : Lib().liked) {
		if (item.track.id == trackId) {
			return true;
		}
	}
	return false;
}

void SetLiked(const Track &track, bool liked) {
	auto &lib = Lib();
	const auto i = std::find_if(lib.liked.begin(), lib.liked.end(), [&](const Liked &l) {
		return l.track.id == track.id;
	});
	// Лайк из аккаунта снимаем и в аккаунте, но только если он ещё считается
	// лайкнутым там: так повторная синхронизация не вернёт его обратно.
	const auto inAccount = (i != lib.liked.end() && i->remote)
		|| (std::find(
			lib.remoteLikes.begin(),
			lib.remoteLikes.end(),
			track.id) != lib.remoteLikes.end());
	if (liked && i == lib.liked.end()) {
		auto copy = track;
		copy.transcodingUrl.clear();
		copy.authorization.clear();
		const auto remote = inAccount;
		lib.liked.insert(lib.liked.begin(), {
			std::move(copy),
			QDateTime::currentSecsSinceEpoch(),
			remote });
	} else if (!liked && i != lib.liked.end()) {
		lib.liked.erase(i);
		if (inAccount) {
			std::erase(lib.remoteLikes, track.id);
		}
	} else {
		return;
	}
	if (Music::Account::IsLoggedIn()) {
		// Постановка лайка в аккаунт идёт фоном: сердечко отвечает мгновенно.
		if (!IsTelegramId(track.id)) {
			Music::Account::SetTrackLiked(track.id, liked);
		} else if (!track.title.trimmed().isEmpty()) {
			const auto userId = Music::Account::User().id;
			Search(track.title, 0, [=](SearchResult result) {
				if (!result.error.isEmpty()
					|| !Music::Account::IsLoggedIn()
					|| Music::Account::User().id != userId
					|| IsLiked(track.id) != liked) {
					return;
				}
				// Только точное название. Среди тёзок выбираем по исполнителю.
				const auto title = track.title.trimmed();
				auto matches = std::vector<Track>();
				for (const auto &candidate : result.tracks) {
					if (candidate.title.trimmed().compare(title, Qt::CaseInsensitive) == 0
						&& (track.artist.trimmed().isEmpty()
							|| candidate.artist.trimmed().compare(
								track.artist.trimmed(), Qt::CaseInsensitive) == 0)
						&& candidate.id.toULongLong()) {
						matches.push_back(candidate);
					}
				}
				if (matches.size() == 1) {
					Music::Account::SetTrackLiked(matches.front().id, liked);
				}
			});
		}
	}
	Save();
	lib.likesChanged.fire({});
}

std::vector<Track> LikedTracks() {
	auto result = std::vector<Track>();
	for (const auto &item : Lib().liked) {
		result.push_back(item.track);
	}
	return result;
}

rpl::producer<> LikesChanged() {
	return Lib().likesChanged.events();
}

void NotifyLikesChanged() {
	auto &lib = Lib();
	lib.likesChanged.fire({});
}

int MergeRemoteLikes(const std::vector<Track> &remote, bool complete) {
	auto &lib = Lib();
	auto remoteIds = std::vector<QString>();
	remoteIds.reserve(remote.size());
	for (const auto &track : remote) {
		if (!track.id.isEmpty()) {
			remoteIds.push_back(track.id);
		}
	}
	// До последней страницы список аккаунта неполный: сохраняем предыдущий индекс.
	if (complete) {
		lib.remoteLikes = remoteIds;
	}
	auto remoteSet = std::set<QString>(remoteIds.begin(), remoteIds.end());
	auto imported = 0;
	auto fresh = std::vector<Liked>();
	for (const auto &track : remote) {
		if (track.id.isEmpty() || IsTelegramId(track.id)) {
			continue;
		}
		const auto i = std::find_if(lib.liked.begin(), lib.liked.end(), [&](const Liked &l) {
			return l.track.id == track.id;
		});
		if (i == lib.liked.end()) {
			auto copy = track;
			copy.transcodingUrl.clear();
			copy.authorization.clear();
			// Собираем в порядке аккаунта (новые лайки первыми), в начало
			// списка вставим блоком ниже: по одному в begin порядок
			// переворачивался и свежие лайки уезжали вниз.
			fresh.push_back({
				std::move(copy),
				QDateTime::currentSecsSinceEpoch(),
				true });
			++imported;
		} else if (!i->remote) {
			// Свежие метаданные из аккаунта, позиция в списке остаётся.
			auto fresh = *i;
			fresh.track = track;
			fresh.track.transcodingUrl.clear();
			fresh.track.authorization.clear();
			fresh.remote = true;
			*i = std::move(fresh);
		}
	}
	if (!fresh.empty()) {
		lib.liked.insert(lib.liked.begin(), fresh.begin(), fresh.end());
	}
	// Лайк сняли в аккаунте — убираем его и из локального списка.
	auto removed = 0;
	if (complete) {
		for (auto i = lib.liked.begin(); i != lib.liked.end();) {
			if (i->remote && !remoteSet.contains(i->track.id)) {
				i = lib.liked.erase(i);
				++removed;
			} else {
				++i;
			}
		}
	}
	// Порядок лайков из аккаунта: раскладываем их по тем же местам списка
	// в порядке remoteIds (чинит и уже перевёрнутый список).
	auto reordered = false;
	if (complete) {
		auto slots = std::vector<int>();
		auto byId = std::map<QString, Liked>();
		for (auto i = 0; i != int(lib.liked.size()); ++i) {
			const auto &id = lib.liked[i].track.id;
			if (lib.liked[i].remote && remoteSet.contains(id)) {
				slots.push_back(i);
				byId.emplace(id, lib.liked[i]);
			}
		}
		auto k = size_t(0);
		for (const auto &id : remoteIds) {
			const auto j = byId.find(id);
			if (j == byId.end() || k >= slots.size()) {
				continue;
			}
			if (lib.liked[slots[k]].track.id != id) {
				reordered = true;
			}
			lib.liked[slots[k++]] = j->second;
		}
	}
	if (imported || removed || reordered) {
		lib.likesChanged.fire({});
	}
	Save();
	return imported;
}

void ClearRemoteLikes() {
	auto &lib = Lib();
	auto removed = false;
	for (auto i = lib.liked.begin(); i != lib.liked.end();) {
		if (i->remote) {
			i = lib.liked.erase(i);
			removed = true;
		} else {
			++i;
		}
	}
	lib.remoteLikes.clear();
	if (removed) {
		lib.likesChanged.fire({});
	}
	Save();
}

std::vector<Playlist> Playlists() {
	return Lib().playlists;
}

QString CreatePlaylist(const QString &name) {
	const auto trimmed = name.trimmed();
	if (trimmed.isEmpty()) return {};
	auto &lib = Lib();
	const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	lib.playlists.insert(lib.playlists.begin(), Playlist{ id, trimmed, {} });
	Save();
	lib.playlistsChanged.fire({});
	return id;
}

void RenamePlaylist(const QString &id, const QString &name) {
	const auto trimmed = name.trimmed();
	if (trimmed.isEmpty()) return;
	auto &lib = Lib();
	for (auto &playlist : lib.playlists) {
		if (playlist.id == id && playlist.name != trimmed) {
			playlist.name = trimmed;
			Save();
			lib.playlistsChanged.fire({});
			return;
		}
	}
}

void DeletePlaylist(const QString &id) {
	auto &lib = Lib();
	const auto old = lib.playlists.size();
	std::erase_if(lib.playlists, [&](const Playlist &p) { return p.id == id; });
	if (old != lib.playlists.size()) { Save(); lib.playlistsChanged.fire({}); }
}

void AddPlaylistTracks(const QString &id, const std::vector<Track> &tracks) {
	auto &lib = Lib();
	for (auto &playlist : lib.playlists) {
		if (playlist.id != id) continue;
		auto changed = false;
		for (const auto &track : tracks) {
			if (track.id.isEmpty() || IsTelegramId(track.id) || track.unavailable) continue;
			if (std::any_of(playlist.tracks.begin(), playlist.tracks.end(), [&](const Track &t) { return t.id == track.id; })) continue;
			auto copy = track;
			copy.transcodingUrl.clear();
			copy.authorization.clear();
			playlist.tracks.push_back(std::move(copy));
			changed = true;
		}
		if (changed) { Save(); lib.playlistsChanged.fire({}); }
		return;
	}
}

void AddPlaylistTrack(const QString &id, const Track &track) {
	AddPlaylistTracks(id, { track });
}

void RemovePlaylistTrack(const QString &id, const QString &trackId) {
	auto &lib = Lib();
	for (auto &playlist : lib.playlists) {
		if (playlist.id != id) continue;
		const auto old = playlist.tracks.size();
		std::erase_if(playlist.tracks, [&](const Track &t) { return t.id == trackId; });
		if (old != playlist.tracks.size()) { Save(); lib.playlistsChanged.fire({}); }
		return;
	}
}

void MergeRemotePlaylists(
		const std::vector<RemotePlaylist> &remote,
		bool complete) {
	auto &lib = Lib();
	auto changed = false;
	auto remoteIds = std::set<QString>();
	const auto sameTracks = [](const std::vector<Track> &a, const std::vector<Track> &b) {
		if (a.size() != b.size()) return false;
		for (auto i = a.begin(), j = b.begin(); i != a.end(); ++i, ++j) {
			if (i->id != j->id
				|| i->title != j->title
				|| i->artist != j->artist
				|| i->artworkUrl != j->artworkUrl
				|| i->unavailable != j->unavailable) {
				return false;
			}
		}
		return true;
	};
	for (const auto &incoming : remote) {
		if (incoming.id.isEmpty() || incoming.name.trimmed().isEmpty()) continue;
		remoteIds.insert(incoming.id);
		auto tracks = incoming.tracks;
		for (auto &track : tracks) {
			track.transcodingUrl.clear();
			track.authorization.clear();
		}
		auto found = std::find_if(lib.playlists.begin(), lib.playlists.end(),
			[&](const Playlist &playlist) {
				return playlist.remote && playlist.sourceId == incoming.id;
			});
		if (found == lib.playlists.end()) {
			Playlist playlist;
			playlist.id = u"soundcloud:"_q + incoming.id;
			playlist.name = incoming.name.trimmed();
			playlist.tracks = std::move(tracks);
			playlist.remote = true;
			playlist.sourceId = incoming.id;
			lib.playlists.push_back(std::move(playlist));
			changed = true;
		} else if (found->name != incoming.name.trimmed()
			|| !sameTracks(found->tracks, tracks)) {
			found->name = incoming.name.trimmed();
			found->tracks = std::move(tracks);
			changed = true;
		}
	}
	if (complete) {
		for (auto i = lib.playlists.begin(); i != lib.playlists.end();) {
			if (i->remote && !remoteIds.contains(i->sourceId)) {
				i = lib.playlists.erase(i);
				changed = true;
			} else {
				++i;
			}
		}
	}
	if (changed) {
		Save();
		lib.playlistsChanged.fire({});
	}
}

void ClearRemotePlaylists() {
	auto &lib = Lib();
	const auto old = lib.playlists.size();
	std::erase_if(lib.playlists, [](const Playlist &playlist) {
		return playlist.remote;
	});
	if (old != lib.playlists.size()) {
		Save();
		lib.playlistsChanged.fire({});
	}
}

rpl::producer<> PlaylistsChanged() { return Lib().playlistsChanged.events(); }

std::vector<RecentItem> RecentItems() {
	return Lib().recent;
}

void RecordRecentTrack(const Track &track) {
	if (track.id.isEmpty() || IsTelegramId(track.id)) {
		return;
	}
	auto &lib = Lib();
	std::erase_if(lib.recent, [&](const RecentItem &item) {
		return !item.isAlbum && item.track.id == track.id;
	});
	auto item = RecentItem();
	item.track = track;
	item.track.transcodingUrl.clear();
	item.track.authorization.clear();
	lib.recent.insert(lib.recent.begin(), std::move(item));
	if (lib.recent.size() > 30) lib.recent.resize(30);
	Save();
	lib.recentChanged.fire({});
}

void RecordRecentAlbum(const Album &album) {
	if (album.id.isEmpty()) {
		return;
	}
	auto &lib = Lib();
	std::erase_if(lib.recent, [&](const RecentItem &item) {
		return item.isAlbum && item.album.id == album.id;
	});
	auto item = RecentItem();
	item.album = album;
	item.isAlbum = true;
	lib.recent.insert(lib.recent.begin(), std::move(item));
	if (lib.recent.size() > 30) lib.recent.resize(30);
	Save();
	lib.recentChanged.fire({});
}

rpl::producer<> RecentChanged() {
	return Lib().recentChanged.events();
}

void RecordListen(const Track &track, int listenedMs, bool finished) {
	if (track.id.isEmpty() || track.durationMs <= 0) {
		return;
	}
	auto &lib = Lib();
	auto h = Listen();
	h.id = track.id;
	h.artistId = track.artistId;
	h.genre = track.genre;
	h.tags = track.tags;
	h.durationMs = track.durationMs;
	h.listenedMs = std::clamp(listenedMs, 0, track.durationMs);
	h.finished = finished;
	h.at = QDateTime::currentSecsSinceEpoch();
	h.key = NormKey(track);
	lib.history.push_back(std::move(h));
	if (int(lib.history.size()) > kHistoryLimit) {
		lib.history.erase(
			lib.history.begin(),
			lib.history.begin() + (lib.history.size() - kHistoryLimit));
	}
	Save();
}

void BoostTrack(const Track &track) {
	if (track.id.isEmpty()) {
		return;
	}
	auto &lib = Lib();
	auto copy = track;
	copy.transcodingUrl.clear();
	copy.authorization.clear();
	lib.boosted.insert(
		lib.boosted.begin(),
		{ std::move(copy), QDateTime::currentSecsSinceEpoch() });
	if (lib.boosted.size() > 300) {
		lib.boosted.resize(300);
	}
	Save();
}

void BlockArtist(const Track &track) {
	if (track.artistId.isEmpty() && track.artist.trimmed().isEmpty()) {
		return;
	}
	auto &lib = Lib();
	if (!IsBlockedArtist(track)) {
		lib.blocked.push_back({ track.artistId, track.artist });
		Save();
	}
}

void UnblockArtist(const QString &artistId) {
	auto &lib = Lib();
	const auto old = lib.blocked.size();
	lib.blocked.erase(
		std::remove_if(lib.blocked.begin(), lib.blocked.end(), [&](const Blocked &b) {
			return b.artistId == artistId;
		}),
		lib.blocked.end());
	if (lib.blocked.size() != old) {
		Save();
	}
}

bool IsArtistBlocked(const Track &track) {
	return IsBlockedArtist(track);
}

std::vector<QString> TrackIdentityKeys(const Track &track) {
	return KeysFor(track);
}

bool HasTaste() {
	return !Lib().liked.empty() || !Lib().history.empty();
}

void BuildForYou(
		int count,
		Fn<void(std::vector<Track>, QString)> done) {
	if (!Music::Account::IsLoggedIn()) {
		BuildImpl({}, count, false, std::move(done));
		return;
	}
	// Лайки аккаунта — главный сигнал вкуса: подтягиваем их до сборки подборки,
	// а уже собранную отдаём, даже если синхронизация не удалась.
	auto once = std::make_shared<bool>(false);
	const auto finish = [once, done = std::move(done)](
			std::vector<Track> tracks,
			QString note) {
		if (*once) {
			return;
		}
		*once = true;
		done(std::move(tracks), std::move(note));
	};
	Account::SyncLikes([once, count, finish = Fn<void(std::vector<Track>, QString)>(finish)](int, int, QString) {
		BuildImpl({}, count, false, finish);
	});
}

void BuildStream(
		StreamContext context,
		int count,
		Fn<void(std::vector<Track>, QString)> done) {
	BuildImpl(std::move(context), count, true, std::move(done));
}

void BuildSimilar(
		Track seed,
		const std::vector<Track> &avoid,
		int count,
		Fn<void(std::vector<Track>, QString)> done) {
	if (IsTelegramId(seed.id)) {
		seed.artistId.clear();
	}
	BuildSimilarImpl({ std::move(seed), avoid, count }, std::move(done));
}

} // namespace Mokagram::Music
