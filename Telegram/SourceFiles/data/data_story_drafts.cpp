/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_story_drafts.h"

#include "base/unixtime.h"
#include "main/main_session.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>

namespace Data {
namespace {

constexpr auto kPhotoQuality = 95;
constexpr auto kThumbnailQuality = 87;

template <typename Write>
[[nodiscard]] QString ReplaceFile(const QString &path, Write &&write) {
	const auto temp = path + u".tmp"_q;
	QFile::remove(temp);
	if (!write(temp)) {
		QFile::remove(temp);
		return QString();
	}
	QFile::remove(path);
	return QFile::rename(temp, path) ? path : QString();
}

[[nodiscard]] QString ThumbnailPath(const QString &folder, uint64 id) {
	return folder + QString::number(id) + u"_thumb.jpg"_q;
}

[[nodiscard]] QJsonObject SerializeCaption(const TextWithTags &caption) {
	auto tags = QJsonArray();
	for (const auto &tag : caption.tags) {
		tags.append(QJsonObject{
			{ u"offset"_q, tag.offset },
			{ u"length"_q, tag.length },
			{ u"id"_q, tag.id },
		});
	}
	return QJsonObject{
		{ u"text"_q, caption.text },
		{ u"tags"_q, tags },
	};
}

[[nodiscard]] TextWithTags DeserializeCaption(const QJsonObject &object) {
	auto result = TextWithTags();
	result.text = object[u"text"_q].toString();
	for (const auto &value : object[u"tags"_q].toArray()) {
		const auto tag = value.toObject();
		result.tags.push_back({
			.offset = tag[u"offset"_q].toInt(),
			.length = tag[u"length"_q].toInt(),
			.id = tag[u"id"_q].toString(),
		});
	}
	return result;
}

[[nodiscard]] QJsonObject Serialize(const StoryDraft &draft) {
	const auto &edits = draft.edits;
	const auto &rect = edits.canvasMediaRect;
	const auto &video = draft.videoPlacement;
	return QJsonObject{
		{ u"id"_q, QString::number(draft.id) },
		{ u"date"_q, qint64(draft.date) },
		{ u"peer"_q, QString::number(draft.peer.value) },
		{ u"media"_q, QFileInfo(draft.mediaPath).fileName() },
		{ u"width"_q, draft.dimensions.width() },
		{ u"height"_q, draft.dimensions.height() },
		{ u"duration"_q, qint64(draft.duration) },
		{ u"video"_q, draft.video },
		{ u"caption"_q, SerializeCaption(draft.caption) },
		{ u"period"_q, draft.period },
		{ u"edits"_q, QJsonObject{
			{ u"angle"_q, edits.angle },
			{ u"flipped"_q, edits.flipped },
			{ u"from"_q, qint64(edits.from) },
			{ u"till"_q, qint64(edits.till) },
			{ u"cover"_q, qint64(edits.cover) },
			{ u"canvas"_q, QJsonArray{
				rect.x(),
				rect.y(),
				rect.width(),
				rect.height(),
			} },
		} },
		{ u"still"_q, QFileInfo(draft.stillPath).fileName() },
		{ u"vrect"_q, QJsonArray{
			video.rect.x(),
			video.rect.y(),
			video.rect.width(),
			video.rect.height(),
		} },
		{ u"vscale"_q, video.scale },
		{ u"vrot"_q, video.rotation },
		{ u"vol"_q, video.volume },
	};
}

[[nodiscard]] std::optional<StoryDraft> Deserialize(
		const QJsonObject &object,
		const QString &folder) {
	auto result = StoryDraft();
	result.id = object[u"id"_q].toString().toULongLong();
	if (!result.id) {
		return std::nullopt;
	}
	result.date = TimeId(object[u"date"_q].toInteger());
	result.peer = PeerId(object[u"peer"_q].toString().toULongLong());
	result.mediaPath = folder + object[u"media"_q].toString();
	result.dimensions = QSize(
		object[u"width"_q].toInt(),
		object[u"height"_q].toInt());
	result.duration = object[u"duration"_q].toInteger();
	result.video = object[u"video"_q].toBool();
	result.caption = DeserializeCaption(object[u"caption"_q].toObject());
	result.period = object[u"period"_q].toInt();

	const auto edits = object[u"edits"_q].toObject();
	result.edits.angle = edits[u"angle"_q].toInt();
	result.edits.flipped = edits[u"flipped"_q].toBool();
	result.edits.from = edits[u"from"_q].toInteger();
	result.edits.till = edits[u"till"_q].toInteger();
	result.edits.cover = edits[u"cover"_q].toInteger();
	const auto canvas = edits[u"canvas"_q].toArray();
	if (canvas.size() == 4) {
		result.edits.canvasMediaRect = QRect(
			canvas[0].toInt(),
			canvas[1].toInt(),
			canvas[2].toInt(),
			canvas[3].toInt());
	}
	result.stillPath = folder + object[u"still"_q].toString();
	if (!QFile::exists(result.stillPath)) {
		result.stillPath = QString();
	}
	auto &video = result.videoPlacement;
	const auto vrect = object[u"vrect"_q].toArray();
	if (vrect.size() == 4) {
		video.rect = QRectF(
			vrect[0].toDouble(),
			vrect[1].toDouble(),
			vrect[2].toDouble(),
			vrect[3].toDouble());
	}
	video.scale = object[u"vscale"_q].toDouble(1.);
	video.rotation = object[u"vrot"_q].toDouble();
	video.volume = object[u"vol"_q].toDouble(1.);
	if (!QFile::exists(result.mediaPath)) {
		return std::nullopt;
	}
	result.thumbnail = QImage(ThumbnailPath(folder, result.id));
	return result;
}

} // namespace

StoryDrafts::StoryDrafts(not_null<Main::Session*> session)
: _session(session) {
}

const std::vector<StoryDraft> &StoryDrafts::list() {
	load();
	return _list;
}

int StoryDrafts::count() {
	return int(list().size());
}

const StoryDraft *StoryDrafts::find(uint64 id) {
	const auto i = ranges::find(list(), id, &StoryDraft::id);
	return (i != end(_list)) ? &*i : nullptr;
}

rpl::producer<> StoryDrafts::changes() const {
	return _changes.events();
}

uint64 StoryDrafts::save(StoryDraft draft, const StoryDraftMedia &media) {
	load();
	QDir().mkpath(folder());

	const auto existing = draft.id
		? ranges::find(_list, draft.id, &StoryDraft::id)
		: end(_list);
	const auto replacing = (existing != end(_list));
	if (!draft.id || !replacing) {
		draft.id = uint64(QDateTime::currentMSecsSinceEpoch());
		while (ranges::contains(_list, draft.id, &StoryDraft::id)) {
			++draft.id;
		}
		draft.date = base::unixtime::now();
	} else {
		draft.date = existing->date;
	}

	const auto hasMedia = !media.photo.isNull()
		|| !media.videoPath.isEmpty()
		|| !media.videoContent.isEmpty();
	if (hasMedia) {
		const auto path = storeMedia(draft.id, media);
		if (path.isEmpty()) {
			return 0;
		}
		if (replacing && (existing->mediaPath != path)) {
			QFile::remove(existing->mediaPath);
		}
		draft.mediaPath = path;
	} else if (replacing) {
		draft.mediaPath = existing->mediaPath;
	} else {
		return 0;
	}

	const auto isVideo = !media.videoPath.isEmpty()
		|| !media.videoContent.isEmpty();
	auto stillPath = QString();
	if (isVideo && !media.photo.isNull()) {
		stillPath = storeStill(draft.id, media.photo);
		if (stillPath.isEmpty()) {
			return 0;
		}
	}
	if (replacing && (existing->stillPath != stillPath)) {
		QFile::remove(existing->stillPath);
	}
	draft.stillPath = stillPath;

	const auto thumbnail = ThumbnailPath(folder(), draft.id);
	if (!draft.thumbnail.isNull()) {
		draft.thumbnail.save(thumbnail, "JPG", kThumbnailQuality);
	} else if (replacing) {
		draft.thumbnail = existing->thumbnail;
	}

	if (replacing) {
		*existing = std::move(draft);
	} else {
		_list.insert(begin(_list), std::move(draft));
	}
	const auto id = replacing ? existing->id : _list.front().id;
	write();
	_changes.fire({});
	return id;
}

void StoryDrafts::remove(uint64 id) {
	load();
	const auto i = ranges::find(_list, id, &StoryDraft::id);
	if (i == end(_list)) {
		return;
	}
	QFile::remove(i->mediaPath);
	QFile::remove(i->stillPath);
	QFile::remove(ThumbnailPath(folder(), id));
	_list.erase(i);
	write();
	_changes.fire({});
}

void StoryDrafts::load() {
	if (_loaded) {
		return;
	}
	_loaded = true;
	auto file = QFile(folder() + u"index.json"_q);
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto document = QJsonDocument::fromJson(file.readAll());
	const auto drafts = document.object()[u"drafts"_q].toArray();
	for (const auto &value : drafts) {
		if (auto draft = Deserialize(value.toObject(), folder())) {
			_list.push_back(std::move(*draft));
		}
	}
}

void StoryDrafts::write() {
	auto drafts = QJsonArray();
	for (const auto &draft : _list) {
		drafts.append(Serialize(draft));
	}
	auto file = QSaveFile(folder() + u"index.json"_q);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(QJsonObject{
			{ u"drafts"_q, drafts },
		}).toJson(QJsonDocument::Compact));
		file.commit();
	}
}

QString StoryDrafts::folder() const {
	return cWorkingDir()
		+ u"tdata/story_drafts/"_q
		+ QString::number(_session->userId().bare)
		+ '/';
}

QString StoryDrafts::storeMedia(
		uint64 id,
		const StoryDraftMedia &media) const {
	const auto base = folder() + QString::number(id);
	const auto isVideo = !media.videoPath.isEmpty()
		|| !media.videoContent.isEmpty();
	if (!isVideo) {
		const auto alpha = media.photo.hasAlphaChannel();
		const auto path = base + (alpha ? u".png"_q : u".jpg"_q);
		return ReplaceFile(path, [&](const QString &temp) {
			return alpha
				? media.photo.save(temp, "PNG")
				: media.photo.save(temp, "JPG", kPhotoQuality);
		});
	}
	const auto suffix = QFileInfo(media.videoPath).suffix();
	const auto path = base + '.' + (suffix.isEmpty() ? u"mp4"_q : suffix);
	if (media.videoPath == path) {
		return path;
	}
	return ReplaceFile(path, [&](const QString &temp) {
		if (!media.videoPath.isEmpty()) {
			return QFile::copy(media.videoPath, temp);
		}
		auto file = QFile(temp);
		return file.open(QIODevice::WriteOnly)
			&& (file.write(media.videoContent) == media.videoContent.size());
	});
}

QString StoryDrafts::storeStill(uint64 id, const QImage &still) const {
	const auto alpha = still.hasAlphaChannel();
	const auto path = folder() + QString::number(id)
		+ u"_still."_q
		+ (alpha ? u"png"_q : u"jpg"_q);
	return ReplaceFile(path, [&](const QString &temp) {
		return alpha
			? still.save(temp, "PNG")
			: still.save(temp, "JPG", kPhotoQuality);
	});
}

} // namespace Data
