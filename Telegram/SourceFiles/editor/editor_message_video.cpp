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

[[nodiscard]] QString TempVideoPath(not_null<DocumentData*> document) {
	auto temp = QTemporaryFile(
		Media::Encode::TempFileTemplate(VideoExtension(document)));
	if (!temp.open()) {
		return QString();
	}
	temp.setAutoRemove(false);
	return temp.fileName();
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
: _document(Find(item))
, _itemId(item->fullId()) {
}

MessageVideo::~MessageVideo() {
	_lifetime.destroy();
	_loader = nullptr;
	if (!_tempPath.isEmpty()) {
		QFile::remove(_tempPath);
	}
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
	const auto path = reader ? TempVideoPath(_document) : QString();
	if (path.isEmpty()) {
		fail();
		return;
	}
	_tempPath = path;
	_loader = _document->createStreamedDownloader(
		std::move(reader),
		origin,
		std::nullopt,
		path,
		LoadToFileOnly,
		LoadFromCloudOrLocal,
		false);
	_loader->updates(
	) | rpl::on_next_error_done([=] {
		_changes.fire({});
	}, [=](FileLoader::Error) {
		crl::on_main(base::make_weak(this), [=] { fail(); });
	}, [=] {
		readSource(_tempPath, QByteArray());
	}, _lifetime);
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

rpl::producer<> MessageVideo::changes() const {
	return _changes.events();
}

void MessageVideo::waitForLoader() {
	_waiting = true;
	const auto weak = base::make_weak(this);
	const auto lifetime = std::make_shared<rpl::lifetime>();
	_document->session().downloaderTaskFinished(
	) | rpl::on_next_done([=] {
		const auto strong = weak.get();
		if (strong && strong->_document->loading()) {
			return;
		}
		lifetime->destroy();
		if (strong) {
			strong->_waiting = false;
			strong->load();
		}
	}, [=] {
		lifetime->destroy();
	}, *lifetime);
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
	Storage::ReadPhotoEditorMediaAsync(path, content, done);
}

void MessageVideo::fail() {
	_failed = true;
	_loader = nullptr;
	_changes.fire({});
}

} // namespace Editor
