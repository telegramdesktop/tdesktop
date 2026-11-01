/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_story_edit_sources.h"

#include "base/unixtime.h"
#include "data/data_document.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "data/data_stories.h"
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

constexpr auto kPendingLifeSeconds = 3600;
constexpr auto kMaxIndexSize = 16 * 1024 * 1024;

[[nodiscard]] QJsonObject SerializeSend(const StorySourceSend &send) {
	const auto peers = [](const std::vector<PeerId> &list) {
		auto result = QJsonArray();
		for (const auto &peer : list) {
			result.append(QString::number(peer.value));
		}
		return result;
	};
	auto albums = QJsonArray();
	for (const auto &album : send.albums) {
		albums.append(album);
	}
	return QJsonObject{
		{ u"option"_q, send.option },
		{ u"excludedEveryone"_q, peers(send.excludedEveryone) },
		{ u"excludedContacts"_q, peers(send.excludedContacts) },
		{ u"selected"_q, peers(send.selected) },
		{ u"pinned"_q, send.pinned },
		{ u"forwards"_q, send.allowForwards },
		{ u"albums"_q, albums },
		{ u"postTo"_q, QString::number(send.postTo.value) },
	};
}

[[nodiscard]] StorySourceSend DeserializeSend(const QJsonObject &object) {
	const auto peers = [](const QJsonArray &array) {
		auto result = std::vector<PeerId>();
		for (const auto &value : array) {
			result.push_back(PeerId(value.toString().toULongLong()));
		}
		return result;
	};
	auto albums = std::vector<int>();
	for (const auto &value : object[u"albums"_q].toArray()) {
		albums.push_back(value.toInt());
	}
	return {
		.option = object[u"option"_q].toInt(),
		.excludedEveryone = peers(object[u"excludedEveryone"_q].toArray()),
		.excludedContacts = peers(object[u"excludedContacts"_q].toArray()),
		.selected = peers(object[u"selected"_q].toArray()),
		.pinned = object[u"pinned"_q].toBool(true),
		.allowForwards = object[u"forwards"_q].toBool(true),
		.albums = std::move(albums),
		.postTo = PeerId(object[u"postTo"_q].toString().toULongLong()),
	};
}

[[nodiscard]] QJsonObject Serialize(const StorySource &source) {
	return {
		{ u"pending"_q, QString::number(source.pendingId) },
		{ u"peer"_q, QString::number(source.peer.value) },
		{ u"story"_q, qint64(source.storyId) },
		{ u"expires"_q, qint64(source.expiresAt) },
		{ u"photo"_q, qint64(source.photoId) },
		{ u"document"_q, qint64(source.documentId) },
		{ u"fingerprint"_q, QString::number(source.fingerprint) },
		{ u"send"_q, SerializeSend(source.send) },
		{ u"draft"_q, SerializeStoryDraftJson(source.draft) },
	};
}

[[nodiscard]] std::optional<StorySource> Deserialize(
		const QJsonObject &object,
		const QString &folder) {
	auto result = StorySource();
	result.pendingId = object[u"pending"_q].toString().toULongLong();
	result.peer = PeerId(object[u"peer"_q].toString().toULongLong());
	result.storyId = StoryId(object[u"story"_q].toInteger());
	result.expiresAt = TimeId(object[u"expires"_q].toInteger());
	result.photoId = PhotoId(object[u"photo"_q].toInteger());
	result.documentId = DocumentId(object[u"document"_q].toInteger());
	result.fingerprint = object[u"fingerprint"_q].toString().toULongLong();
	result.send = DeserializeSend(object[u"send"_q].toObject());
	auto draft = ParseStoryDraftJson(object[u"draft"_q].toObject(), folder);
	if (!draft || !draft->id) {
		return std::nullopt;
	}
	result.draft = std::move(*draft);
	if (!QFile::exists(result.draft.mediaPath)) {
		return std::nullopt;
	}
	return result;
}

} // namespace

StoryEditSources::StoryEditSources(not_null<Main::Session*> session)
: _session(session) {
}

void StoryEditSources::savePending(
		uint64 pendingId,
		const StorySourceSave &save,
		const StorySourceSend &send) {
	load();
	removeExpired();
	QDir().mkpath(folder());

	auto source = StorySource{
		.pendingId = pendingId,
		.peer = send.postTo ? send.postTo : save.draft.peer,
		.fingerprint = save.fingerprint,
		.draft = save.draft,
		.send = send,
	};
	source.draft.id = pendingId;
	source.draft.date = base::unixtime::now();

	const auto hasMedia = !save.media.photo.isNull()
		|| !save.media.videoPath.isEmpty()
		|| !save.media.videoContent.isEmpty();
	if (!hasMedia) {
		return;
	}
	const auto &media = save.media;
	const auto base = folder() + QString::number(pendingId);
	const auto isVideo = !media.videoPath.isEmpty()
		|| !media.videoContent.isEmpty();
	const auto mediaPath = isVideo
		? base + '.' + (QFileInfo(media.videoPath).suffix().isEmpty()
			? u"mp4"_q
			: QFileInfo(media.videoPath).suffix())
		: base + (media.photo.hasAlphaChannel() ? u".png"_q : u".jpg"_q);
	if (!isVideo) {
		const auto saved = media.photo.hasAlphaChannel()
			? media.photo.save(mediaPath, "PNG")
			: media.photo.save(mediaPath, "JPG", 95);
		if (!saved) {
			return;
		}
	} else if (media.videoPath != mediaPath) {
		QFile::remove(mediaPath);
		if (!media.videoPath.isEmpty()) {
			if (!QFile::copy(media.videoPath, mediaPath)) {
				return;
			}
		} else {
			auto file = QFile(mediaPath);
			if (!file.open(QIODevice::WriteOnly)
				|| (file.write(media.videoContent)
					!= media.videoContent.size())) {
				return;
			}
		}
	}
	source.draft.mediaPath = mediaPath;

	if (isVideo && !media.photo.isNull()) {
		const auto alpha = media.photo.hasAlphaChannel();
		const auto stillPath = base + u"_still."_q
			+ (alpha ? u"png"_q : u"jpg"_q);
		const auto saved = alpha
			? media.photo.save(stillPath, "PNG")
			: media.photo.save(stillPath, "JPG", 95);
		if (saved) {
			source.draft.stillPath = stillPath;
		}
	}

	auto sceneNames = QStringList();
	for (auto i = 0, count = int(media.sceneFiles.size()); i != count; ++i) {
		const auto path = storeFile(
			pendingId,
			i,
			media.sceneFiles[i],
			mediaPath);
		if (path.isEmpty()) {
			continue;
		}
		sceneNames.push_back(path);
	}
	if (sceneNames.size() != media.sceneFiles.size()) {
		while (sceneNames.size() != media.sceneFiles.size()) {
			sceneNames.push_back(QString());
		}
	}
	source.draft.sceneFiles = std::move(sceneNames);

	const auto audioPath = (!media.audioPath.isEmpty()
		|| !media.audioContent.isEmpty())
		? storeFile(
			pendingId,
			-1,
			{ .path = media.audioPath,
				.content = media.audioContent,
				.suffix = QFileInfo(media.audioPath).suffix() },
			QString())
		: QString();
	if (!audioPath.isEmpty()) {
		source.draft.audioPath = audioPath;
		if (!media.audioCover.isNull()) {
			media.audioCover.save(
				StoryDraftAudioCoverPath(folder(), pendingId),
				"PNG");
		}
	}

	_list.push_back(std::move(source));
	write();
}

void StoryEditSources::finalize(
		uint64 pendingId,
		StoryId storyId,
		TimeId expiresAt) {
	load();
	const auto i = ranges::find(_list, pendingId, &StorySource::pendingId);
	if (i == end(_list)) {
		return;
	}
	i->storyId = storyId;
	i->expiresAt = expiresAt;
	const auto peer = i->peer;
	_list.erase(ranges::remove_if(_list, [&](const StorySource &source) {
		if (source.pendingId == pendingId
			|| source.peer != peer
			|| source.storyId != storyId) {
			return false;
		}
		removeFiles(source);
		return true;
	}), end(_list));
	const auto target = ranges::find(
		_list,
		pendingId,
		&StorySource::pendingId);
	const auto lookup = _session->data().stories().lookup({
		peer,
		storyId });
	if (lookup) {
		target->photoId = (*lookup)->photo()
			? (*lookup)->photo()->id
			: PhotoId(0);
		target->documentId = (*lookup)->document()
			? (*lookup)->document()->id
			: DocumentId(0);
	}
	target->draft.id = pendingId;
	write();
}

void StoryEditSources::removePending(uint64 pendingId) {
	load();
	const auto i = ranges::find(_list, pendingId, &StorySource::pendingId);
	if (i == end(_list)) {
		return;
	}
	removeFiles(*i);
	_list.erase(i);
	write();
}

void StoryEditSources::updateSendState(
		PeerId peer,
		StoryId storyId,
		const StorySourceSend &send) {
	load();
	const auto i = ranges::find_if(_list, [&](const StorySource &source) {
		return (source.peer == peer) && (source.storyId == storyId);
	});
	if (i == end(_list)) {
		return;
	}
	i->send = send;
	write();
}

StorySource StoryEditSources::find(
		PeerId peer,
		StoryId storyId,
		PhotoId photoId,
		DocumentId documentId) {
	load();
	removeExpired();
	auto i = ranges::find_if(_list, [&](const StorySource &source) {
		return (source.peer == peer) && (source.storyId == storyId);
	});
	if (i == end(_list)) {
		return {};
	}
	if ((i->photoId && i->photoId != photoId)
		|| (i->documentId && i->documentId != documentId)) {
		return {};
	}
	return *i;
}

uint64 StoryEditSources::fingerprint(PeerId peer, StoryId storyId) {
	load();
	const auto i = ranges::find_if(_list, [&](const StorySource &source) {
		return (source.peer == peer) && (source.storyId == storyId);
	});
	return (i != end(_list)) ? i->fingerprint : 0;
}

void StoryEditSources::removeExpired() {
	if (!_loaded) {
		return;
	}
	const auto now = base::unixtime::now();
	auto changed = false;
	for (auto i = _list.begin(); i != _list.end();) {
		const auto pending = (i->storyId == 0);
		const auto expired = pending
			? (i->draft.date + kPendingLifeSeconds < now)
			: (i->expiresAt && (i->expiresAt < now));
		if (expired || (!pending && !i->expiresAt)) {
			removeFiles(*i);
			i = _list.erase(i);
			changed = true;
		} else {
			++i;
		}
	}
	if (changed) {
		write();
	}
}

void StoryEditSources::clear() {
	load();
	for (const auto &source : _list) {
		removeFiles(source);
	}
	_list.clear();
	QFile::remove(folder() + u"index.json"_q);
}

void StoryEditSources::load() {
	if (_loaded) {
		return;
	}
	_loaded = true;
	auto file = QFile(folder() + u"index.json"_q);
	if (!file.open(QIODevice::ReadOnly) || (file.size() > kMaxIndexSize)) {
		return;
	}
	const auto document = QJsonDocument::fromJson(file.readAll());
	for (const auto &value : document.object()[u"sources"_q].toArray()) {
		if (auto source = Deserialize(value.toObject(), folder())) {
			_list.push_back(std::move(*source));
		}
	}
}

void StoryEditSources::write() {
	auto sources = QJsonArray();
	for (const auto &source : _list) {
		sources.append(Serialize(source));
	}
	auto file = QSaveFile(folder() + u"index.json"_q);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(QJsonObject{
			{ u"sources"_q, sources },
		}).toJson(QJsonDocument::Compact));
		file.commit();
	}
}

void StoryEditSources::removeFiles(const StorySource &source) {
	QFile::remove(source.draft.mediaPath);
	QFile::remove(source.draft.stillPath);
	QFile::remove(source.draft.audioPath);
	for (const auto &file : source.draft.sceneFiles) {
		QFile::remove(file);
	}
	QFile::remove(StoryDraftAudioCoverPath(folder(), source.draft.id));
	QFile::remove(StoryDraftThumbnailPath(folder(), source.draft.id));
}

QString StoryEditSources::folder() const {
	return cWorkingDir()
		+ u"tdata/story_sources/"_q
		+ QString::number(_session->userId().bare)
		+ '/';
}

QString StoryEditSources::storeFile(
		uint64 pendingId,
		int index,
		const StoryDraftSceneFile &file,
		const QString &mediaPath) const {
	if (!file.path.isEmpty() && (file.path == mediaPath)) {
		return mediaPath;
	}
	const auto path = folder() + QString::number(pendingId)
		+ (index >= 0 ? u"_scene%1."_q.arg(index) : u"_audio."_q)
		+ (file.suffix.isEmpty() ? u"png"_q : file.suffix);
	if (!file.image.isNull()) {
		return file.image.save(path, "PNG") ? path : QString();
	}
	if (file.path == path) {
		return path;
	}
	QFile::remove(path);
	if (!file.path.isEmpty()) {
		return QFile::copy(file.path, path) ? path : QString();
	}
	auto out = QFile(path);
	if (!out.open(QIODevice::WriteOnly)
		|| (out.write(file.content) != file.content.size())) {
		return QString();
	}
	return path;
}

} // namespace Data
