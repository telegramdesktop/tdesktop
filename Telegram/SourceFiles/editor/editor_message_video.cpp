/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_message_video.h"

#include "core/mime_type.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_groups.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "data/data_streaming.h"
#include "editor/video/video_clip.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "media/media_video_encode.h"
#include "storage/storage_media_prepare.h"
#include "storage/streamed_file_downloader.h"
#include "ui/image/image_prepare.h"

#include <QtCore/QTemporaryFile>

namespace Editor {
namespace {

constexpr auto kMaxExtensionLength = 5;

[[nodiscard]] QString VideoExtension(not_null<DocumentData*> document) {
	const auto extension = Core::FileExtension(document->filename());
	const auto valid = !extension.isEmpty()
		&& (extension.size() <= kMaxExtensionLength)
		&& ranges::all_of(extension, [](QChar ch) {
			return ch.isLetterOrNumber();
		});
	return valid ? extension : u"mp4"_q;
}

[[nodiscard]] std::shared_ptr<const QString> MakeTempVideo(
		not_null<DocumentData*> document) {
	const auto fileTemplate = Media::Encode::TempFileTemplate(
		VideoExtension(document));
	if (fileTemplate.isEmpty()) {
		return nullptr;
	}
	auto temp = QTemporaryFile(fileTemplate);
	if (!temp.open()) {
		return nullptr;
	}
	temp.setAutoRemove(false);
	return std::shared_ptr<const QString>(
		new QString(temp.fileName()),
		[](const QString *path) {
			QFile::remove(*path);
			delete path;
		});
}

} // namespace

DocumentData *MessageVideo::Find(not_null<HistoryItem*> item) {
	const auto media = item->media();
	const auto document = media ? media->document() : nullptr;
	if (!document
		|| media->webpage()
		|| item->history()->owner().groups().find(item)) {
		return nullptr;
	}
	const auto video = document->isVideoFile()
		|| document->isGifv()
		|| document->isVideoMessage();
	const auto fits = (document->size > 0)
		&& (document->size <= Images::kReadBytesLimit);
	return (video && fits) ? document : nullptr;
}

MessageVideo::MessageVideo(not_null<HistoryItem*> item)
: MessageVideo(Find(item), item->fullId(), Images::kReadBytesLimit) {
}

MessageVideo::MessageVideo(
	not_null<DocumentData*> document,
	FullMsgId itemId,
	int64 sizeLimit)
: _document(document)
, _itemId(itemId)
, _sizeLimit(sizeLimit) {
}

MessageVideo::~MessageVideo() {
	_lifetime.destroy();
	_loader = nullptr;
}

not_null<DocumentData*> MessageVideo::document() const {
	return _document;
}

void MessageVideo::load() {
	if (_source || _loader || _waiting || _reading || _failed) {
		return;
	}
	_media = _document->createMediaView();
	if (_media->loaded(true)) {
		const auto path = _document->filepath(true);
		readSource(path, path.isEmpty() ? _media->bytes() : QByteArray());
		return;
	} else if (_document->loading()) {
		waitForLoader();
		return;
	}
	const auto origin = Data::FileOrigin(_itemId);
	auto reader = _document->owner().streaming().sharedReader(
		_document,
		origin,
		true);
	_temporaryPath = reader ? MakeTempVideo(_document) : nullptr;
	if (!_temporaryPath) {
		fail();
		return;
	}
	_loader = _document->createStreamedDownloader(
		std::move(reader),
		origin,
		std::nullopt,
		*_temporaryPath,
		LoadToFileOnly,
		LoadFromCloudOrLocal,
		false);
	_loader->updates(
	) | rpl::on_next_error_done(
		[=] { _changes.fire({}); },
		[=](FileLoader::Error) {
			crl::on_main(base::make_weak(this), [=] { fail(); });
		},
		[=] { readSource(*_temporaryPath, QByteArray()); },
		_lifetime);
	_loader->start();
	_changes.fire({});
}

bool MessageVideo::loading() const {
	return (_loader != nullptr) || _waiting || _reading;
}

float64 MessageVideo::progress() const {
	return _loader
		? _loader->currentProgress()
		: _waiting
		? _document->progress()
		: _reading
		? 1.
		: 0.;
}

std::shared_ptr<VideoClipSource> MessageVideo::source() const {
	return _source;
}

std::shared_ptr<const QString> MessageVideo::temporaryPath() const {
	return _temporaryPath;
}

rpl::producer<> MessageVideo::changes() const {
	return _changes.events();
}

void MessageVideo::waitForLoader() {
	_waiting = true;
	_document->owner().documentLoadProgress(
	) | rpl::filter([=](not_null<DocumentData*> document) {
		return document == _document;
	}) | rpl::on_next([=] {
		if (_document->loading()) {
			_changes.fire({});
			return;
		}
		_lifetime.destroy();
		_waiting = false;
		load();
	}, _lifetime);
	_changes.fire({});
}

void MessageVideo::readSource(
		const QString &path,
		const QByteArray &content) {
	_reading = true;
	const auto done = crl::guard(this, [=](
			Storage::PhotoEditorMedia &&media) {
		_reading = false;
		_loader = nullptr;
		if (!media.video()) {
			fail();
			return;
		}
		_source = std::make_shared<VideoClipSource>(VideoClipSource{
			.path = std::move(media.videoPath),
			.content = std::move(media.videoContent),
			.thumbnail = std::move(media.image),
			.duration = media.videoDuration,
			.hasAudio = media.videoHasAudio,
		});
		_changes.fire({});
	});
	crl::async([=, sizeLimit = _sizeLimit, temporary = _temporaryPath] {
		auto media = Storage::ReadPhotoEditorMedia(
			temporary ? *temporary : path,
			content,
			sizeLimit);
		crl::on_main([=, media = std::move(media)]() mutable {
			done(std::move(media));
		});
	});
}

void MessageVideo::fail() {
	_failed = true;
	_loader = nullptr;
	_changes.fire({});
}

} // namespace Editor
