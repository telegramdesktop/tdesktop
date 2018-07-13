/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/localimageloader.h"

#include "data/data_document.h"
#include "core/file_utilities.h"
#include "core/mime_type.h"
#include "media/media_audio.h"
#include "boxes/send_files_box.h"
#include "media/media_clip_reader.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "lang/lang_keys.h"
#include "boxes/confirm_box.h"
#include "storage/file_download.h"
#include "storage/storage_media_prepare.h"

using Storage::ValidateThumbDimensions;

TaskQueue::TaskQueue(TimeMs stopTimeoutMs) {
	if (stopTimeoutMs > 0) {
		_stopTimer = new QTimer(this);
		connect(_stopTimer, SIGNAL(timeout()), this, SLOT(stop()));
		_stopTimer->setSingleShot(true);
		_stopTimer->setInterval(int(stopTimeoutMs));
	}
}

TaskId TaskQueue::addTask(std::unique_ptr<Task> &&task) {
	const auto result = task->id();
	{
		QMutexLocker lock(&_tasksToProcessMutex);
		_tasksToProcess.push_back(std::move(task));
	}

	wakeThread();

	return result;
}

void TaskQueue::addTasks(std::vector<std::unique_ptr<Task>> &&tasks) {
	{
		QMutexLocker lock(&_tasksToProcessMutex);
		for (auto &task : tasks) {
			_tasksToProcess.push_back(std::move(task));
		}
	}

	wakeThread();
}

void TaskQueue::wakeThread() {
	if (!_thread) {
		_thread = new QThread();

		_worker = new TaskQueueWorker(this);
		_worker->moveToThread(_thread);

		connect(this, SIGNAL(taskAdded()), _worker, SLOT(onTaskAdded()));
		connect(_worker, SIGNAL(taskProcessed()), this, SLOT(onTaskProcessed()));

		_thread->start();
	}
	if (_stopTimer) _stopTimer->stop();
	emit taskAdded();
}

void TaskQueue::cancelTask(TaskId id) {
	const auto removeFrom = [&](std::deque<std::unique_ptr<Task>> &queue) {
		const auto proj = [](const std::unique_ptr<Task> &task) {
			return task->id();
		};
		auto i = ranges::find(queue, id, proj);
		if (i != queue.end()) {
			queue.erase(i);
		}
	};
	{
		QMutexLocker lock(&_tasksToProcessMutex);
		removeFrom(_tasksToProcess);
		if (_taskInProcessId == id) {
			_taskInProcessId = TaskId();
		}
	}
	QMutexLocker lock(&_tasksToFinishMutex);
	removeFrom(_tasksToFinish);
}

void TaskQueue::onTaskProcessed() {
	do {
		auto task = std::unique_ptr<Task>();
		{
			QMutexLocker lock(&_tasksToFinishMutex);
			if (_tasksToFinish.empty()) break;
			task = std::move(_tasksToFinish.front());
			_tasksToFinish.pop_front();
		}
		task->finish();
	} while (true);

	if (_stopTimer) {
		QMutexLocker lock(&_tasksToProcessMutex);
		if (_tasksToProcess.empty() && !_taskInProcessId) {
			_stopTimer->start();
		}
	}
}

void TaskQueue::stop() {
	if (_thread) {
		_thread->requestInterruption();
		_thread->quit();
		DEBUG_LOG(("Waiting for taskThread to finish"));
		_thread->wait();
		delete _worker;
		delete _thread;
		_worker = 0;
		_thread = 0;
	}
	_tasksToProcess.clear();
	_tasksToFinish.clear();
	_taskInProcessId = TaskId();
}

TaskQueue::~TaskQueue() {
	stop();
	delete _stopTimer;
}

void TaskQueueWorker::onTaskAdded() {
	if (_inTaskAdded) return;
	_inTaskAdded = true;

	bool someTasksLeft = false;
	do {
		auto task = std::unique_ptr<Task>();
		{
			QMutexLocker lock(&_queue->_tasksToProcessMutex);
			if (!_queue->_tasksToProcess.empty()) {
				task = std::move(_queue->_tasksToProcess.front());
				_queue->_tasksToProcess.pop_front();
				_queue->_taskInProcessId = task->id();
			}
		}

		if (task) {
			task->process();
			bool emitTaskProcessed = false;
			{
				QMutexLocker lockToProcess(&_queue->_tasksToProcessMutex);
				if (_queue->_taskInProcessId == task->id()) {
					_queue->_taskInProcessId = TaskId();
					someTasksLeft = !_queue->_tasksToProcess.empty();

					QMutexLocker lockToFinish(&_queue->_tasksToFinishMutex);
					emitTaskProcessed = _queue->_tasksToFinish.empty();
					_queue->_tasksToFinish.push_back(std::move(task));
				}
			}
			if (emitTaskProcessed) {
				emit taskProcessed();
			}
		}
		QCoreApplication::processEvents();
	} while (someTasksLeft && !thread()->isInterruptionRequested());

	_inTaskAdded = false;
}

SendingAlbum::SendingAlbum() : groupId(rand_value<uint64>()) {
}

FileLoadResult::FileLoadResult(
	TaskId taskId,
	uint64 id,
	const FileLoadTo &to,
	const TextWithTags &caption,
	std::shared_ptr<SendingAlbum> album)
: taskId(taskId)
, id(id)
, to(to)
, album(std::move(album))
, caption(caption) {
}

FileLoadTask::FileLoadTask(
	const QString &filepath,
	const QByteArray &content,
	std::unique_ptr<FileMediaInformation> information,
	SendMediaType type,
	const FileLoadTo &to,
	const TextWithTags &caption,
	std::shared_ptr<SendingAlbum> album)
: _id(rand_value<uint64>())
, _to(to)
, _album(std::move(album))
, _filepath(filepath)
, _content(content)
, _information(std::move(information))
, _type(type)
, _caption(caption) {
}

FileLoadTask::FileLoadTask(
	const QByteArray &voice,
	int32 duration,
	const VoiceWaveform &waveform,
	const FileLoadTo &to,
	const TextWithTags &caption)
: _id(rand_value<uint64>())
, _to(to)
, _content(voice)
, _duration(duration)
, _waveform(waveform)
, _type(SendMediaType::Audio)
, _caption(caption) {
}

std::unique_ptr<FileMediaInformation> FileLoadTask::ReadMediaInformation(
		const QString &filepath,
		const QByteArray &content,
		const QString &filemime) {
	auto result = std::make_unique<FileMediaInformation>();
	result->filemime = filemime;

	if (CheckForSong(filepath, content, result)) {
		return result;
	} else if (CheckForVideo(filepath, content, result)) {
		return result;
	} else if (CheckForImage(filepath, content, result)) {
		return result;
	}
	return result;
}

template <typename Mimes, typename Extensions>
bool FileLoadTask::CheckMimeOrExtensions(
		const QString &filepath,
		const QString &filemime,
		Mimes &mimes,
		Extensions &extensions) {
	if (std::find(std::begin(mimes), std::end(mimes), filemime) != std::end(mimes)) {
		return true;
	}
	if (std::find_if(std::begin(extensions), std::end(extensions), [&filepath](auto &extension) {
		return filepath.endsWith(extension, Qt::CaseInsensitive);
	}) != std::end(extensions)) {
		return true;
	}
	return false;
}

bool FileLoadTask::CheckForSong(
		const QString &filepath,
		const QByteArray &content,
		std::unique_ptr<FileMediaInformation> &result) {
	static const auto mimes = {
		qstr("audio/mp3"),
		qstr("audio/m4a"),
		qstr("audio/aac"),
		qstr("audio/ogg"),
		qstr("audio/flac"),
	};
	static const auto extensions = {
		qstr(".mp3"),
		qstr(".m4a"),
		qstr(".aac"),
		qstr(".ogg"),
		qstr(".flac"),
	};
	if (!CheckMimeOrExtensions(filepath, result->filemime, mimes, extensions)) {
		return false;
	}

	auto media = Media::Player::PrepareForSending(filepath, content);
	if (media.duration < 0) {
		return false;
	}
	if (!ValidateThumbDimensions(media.cover.width(), media.cover.height())) {
		media.cover = QImage();
	}
	result->media = std::move(media);
	return true;
}

bool FileLoadTask::CheckForVideo(
		const QString &filepath,
		const QByteArray &content,
		std::unique_ptr<FileMediaInformation> &result) {
	static const auto mimes = {
		qstr("video/mp4"),
		qstr("video/quicktime"),
	};
	static const auto extensions = {
		qstr(".mp4"),
		qstr(".mov"),
	};
	if (!CheckMimeOrExtensions(filepath, result->filemime, mimes, extensions)) {
		return false;
	}

	auto media = Media::Clip::PrepareForSending(filepath, content);
	if (media.duration < 0) {
		return false;
	}

	auto coverWidth = media.thumbnail.width();
	auto coverHeight = media.thumbnail.height();
	if (!ValidateThumbDimensions(coverWidth, coverHeight)) {
		return false;
	}

	if (filepath.endsWith(qstr(".mp4"), Qt::CaseInsensitive)) {
		result->filemime = qstr("video/mp4");
	}
	result->media = std::move(media);
	return true;
}

bool FileLoadTask::CheckForImage(
		const QString &filepath,
		const QByteArray &content,
		std::unique_ptr<FileMediaInformation> &result) {
	auto animated = false;
	auto image = ([&filepath, &content, &animated] {
		if (!content.isEmpty()) {
			return App::readImage(content, nullptr, false, &animated);
		} else if (!filepath.isEmpty()) {
			return App::readImage(filepath, nullptr, false, &animated);
		}
		return QImage();
	})();
	return FillImageInformation(std::move(image), animated, result);
}

bool FileLoadTask::FillImageInformation(
		QImage &&image,
		bool animated,
		std::unique_ptr<FileMediaInformation> &result) {
	Expects(result != nullptr);

	if (image.isNull()) {
		return false;
	}
	auto media = FileMediaInformation::Image();
	media.data = std::move(image);
	media.animated = animated;
	result->media = media;
	return true;
}

void FileLoadTask::process() {
	const auto stickerMime = qsl("image/webp");

	_result = std::make_shared<FileLoadResult>(
		id(),
		_id,
		_to,
		_caption,
		_album);

	QString filename, filemime;
	qint64 filesize = 0;
	QByteArray filedata;

	uint64 thumbId = 0;
	auto thumbname = qsl("thumb.jpg");
	QByteArray thumbdata;

	auto isAnimation = false;
	auto isSong = false;
	auto isVideo = false;
	auto isVoice = (_type == SendMediaType::Audio);

	auto fullimage = QImage();
	auto info = _filepath.isEmpty() ? QFileInfo() : QFileInfo(_filepath);
	if (info.exists()) {
		if (info.isDir()) {
			_result->filesize = -1;
			return;
		}

		// Voice sending is supported only from memory for now.
		// Because for voice we force mime type and don't read MediaInformation.
		// For a real file we always read mime type and read MediaInformation.
		Assert(!isVoice);

		filesize = info.size();
		filename = info.fileName();
		if (!_information) {
			_information = readMediaInformation(Core::MimeTypeForFile(info).name());
		}
		filemime = _information->filemime;
		if (auto image = base::get_if<FileMediaInformation::Image>(
				&_information->media)) {
			fullimage = base::take(image->data);
			if (auto opaque = (filemime != stickerMime)) {
				fullimage = Images::prepareOpaque(std::move(fullimage));
			}
			isAnimation = image->animated;
		}
	} else if (!_content.isEmpty()) {
		filesize = _content.size();
		if (isVoice) {
			filename = filedialogDefaultName(qsl("audio"), qsl(".ogg"), QString(), true);
			filemime = "audio/ogg";
		} else {
			if (_information) {
				if (auto image = base::get_if<FileMediaInformation::Image>(
						&_information->media)) {
					fullimage = base::take(image->data);
				}
			}
			const auto mimeType = Core::MimeTypeForData(_content);
			filemime = mimeType.name();
			if (filemime != stickerMime) {
				fullimage = Images::prepareOpaque(std::move(fullimage));
			}
			if (filemime == "image/jpeg") {
				filename = filedialogDefaultName(qsl("photo"), qsl(".jpg"), QString(), true);
			} else if (filemime == "image/png") {
				filename = filedialogDefaultName(qsl("image"), qsl(".png"), QString(), true);
			} else {
				QString ext;
				QStringList patterns = mimeType.globPatterns();
				if (!patterns.isEmpty()) {
					ext = patterns.front().replace('*', QString());
				}
				filename = filedialogDefaultName(qsl("file"), ext, QString(), true);
			}
		}
	} else {
		if (_information) {
			if (auto image = base::get_if<FileMediaInformation::Image>(
					&_information->media)) {
				fullimage = base::take(image->data);
			}
		}
		if (!fullimage.isNull() && fullimage.width() > 0) {
			if (_type == SendMediaType::Photo) {
				if (ValidateThumbDimensions(fullimage.width(), fullimage.height())) {
					filesize = -1; // Fill later.
					filemime = Core::MimeTypeForName("image/jpeg").name();
					filename = filedialogDefaultName(qsl("image"), qsl(".jpg"), QString(), true);
				} else {
					_type = SendMediaType::File;
				}
			}
			if (_type == SendMediaType::File) {
				filemime = Core::MimeTypeForName("image/png").name();
				filename = filedialogDefaultName(qsl("image"), qsl(".png"), QString(), true);
				{
					QBuffer buffer(&_content);
					fullimage.save(&buffer, "PNG");
				}
				filesize = _content.size();
			}
			fullimage = Images::prepareOpaque(std::move(fullimage));
		}
	}
	_result->filesize = (int32)qMin(filesize, qint64(INT_MAX));

	if (!filesize || filesize > App::kFileSizeLimit) {
		return;
	}

	PreparedPhotoThumbs photoThumbs;
	QVector<MTPPhotoSize> photoSizes;
	QPixmap thumb;

	QVector<MTPDocumentAttribute> attributes(1, MTP_documentAttributeFilename(MTP_string(filename)));

	auto thumbSize = MTP_photoSizeEmpty(MTP_string(""));
	auto photo = MTP_photoEmpty(MTP_long(0));
	auto document = MTP_documentEmpty(MTP_long(0));

	if (!isVoice) {
		if (!_information) {
			_information = readMediaInformation(filemime);
			filemime = _information->filemime;
		}
		if (auto song = base::get_if<FileMediaInformation::Song>(
				&_information->media)) {
			isSong = true;
			auto flags = MTPDdocumentAttributeAudio::Flag::f_title | MTPDdocumentAttributeAudio::Flag::f_performer;
			attributes.push_back(MTP_documentAttributeAudio(MTP_flags(flags), MTP_int(song->duration), MTP_string(song->title), MTP_string(song->performer), MTPstring()));
			if (!song->cover.isNull()) { // cover to thumb
				auto coverWidth = song->cover.width();
				auto coverHeight = song->cover.height();
				auto full = (coverWidth > 90 || coverHeight > 90) ? App::pixmapFromImageInPlace(song->cover.scaled(90, 90, Qt::KeepAspectRatio, Qt::SmoothTransformation)) : App::pixmapFromImageInPlace(std::move(song->cover));
				{
					auto thumbFormat = QByteArray("JPG");
					auto thumbQuality = 87;

					QBuffer buffer(&thumbdata);
					full.save(&buffer, thumbFormat, thumbQuality);
				}

				thumb = full;
				thumbSize = MTP_photoSize(MTP_string(""), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(full.width()), MTP_int(full.height()), MTP_int(0));

				thumbId = rand_value<uint64>();
			}
		} else if (auto video = base::get_if<FileMediaInformation::Video>(
				&_information->media)) {
			isVideo = true;
			auto coverWidth = video->thumbnail.width();
			auto coverHeight = video->thumbnail.height();
			if (video->isGifv && !_album) {
				attributes.push_back(MTP_documentAttributeAnimated());
			}
			auto flags = MTPDdocumentAttributeVideo::Flags(0);
			attributes.push_back(MTP_documentAttributeVideo(MTP_flags(flags), MTP_int(video->duration), MTP_int(coverWidth), MTP_int(coverHeight)));

			auto cover = (coverWidth > 90 || coverHeight > 90)
				? video->thumbnail.scaled(90, 90, Qt::KeepAspectRatio, Qt::SmoothTransformation)
				: std::move(video->thumbnail);
			{
				auto thumbFormat = QByteArray("JPG");
				auto thumbQuality = 87;

				QBuffer buffer(&thumbdata);
				cover.save(&buffer, thumbFormat, thumbQuality);
			}

			thumb = App::pixmapFromImageInPlace(std::move(cover));
			thumbSize = MTP_photoSize(MTP_string(""), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(thumb.width()), MTP_int(thumb.height()), MTP_int(0));

			thumbId = rand_value<uint64>();
		}
	}

	if (!fullimage.isNull() && fullimage.width() > 0 && !isSong && !isVideo && !isVoice) {
		auto w = fullimage.width(), h = fullimage.height();
		attributes.push_back(MTP_documentAttributeImageSize(MTP_int(w), MTP_int(h)));

		if (ValidateThumbDimensions(w, h)) {
			if (isAnimation) {
				attributes.push_back(MTP_documentAttributeAnimated());
			} else if (_type != SendMediaType::File) {
				auto thumb = (w > 100 || h > 100) ? App::pixmapFromImageInPlace(fullimage.scaled(100, 100, Qt::KeepAspectRatio, Qt::SmoothTransformation)) : QPixmap::fromImage(fullimage);
				photoThumbs.insert('s', thumb);
				photoSizes.push_back(MTP_photoSize(MTP_string("s"), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(thumb.width()), MTP_int(thumb.height()), MTP_int(0)));

				auto medium = (w > 320 || h > 320) ? App::pixmapFromImageInPlace(fullimage.scaled(320, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation)) : QPixmap::fromImage(fullimage);
				photoThumbs.insert('m', medium);
				photoSizes.push_back(MTP_photoSize(MTP_string("m"), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(medium.width()), MTP_int(medium.height()), MTP_int(0)));

				auto full = (w > 1280 || h > 1280) ? App::pixmapFromImageInPlace(fullimage.scaled(1280, 1280, Qt::KeepAspectRatio, Qt::SmoothTransformation)) : QPixmap::fromImage(fullimage);
				photoThumbs.insert('y', full);
				photoSizes.push_back(MTP_photoSize(MTP_string("y"), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(full.width()), MTP_int(full.height()), MTP_int(0)));

				{
					QBuffer buffer(&filedata);
					full.save(&buffer, "JPG", 87);
				}

				photo = MTP_photo(
					MTP_flags(0),
					MTP_long(_id),
					MTP_long(0),
					MTP_bytes(QByteArray()),
					MTP_int(unixtime()),
					MTP_vector<MTPPhotoSize>(photoSizes));

				if (filesize < 0) {
					filesize = _result->filesize = filedata.size();
				}
			}

			QByteArray thumbFormat = "JPG";
			auto thumbQuality = 87;
			if (!isAnimation
				&& filemime == stickerMime
				&& w > 0
				&& h > 0
				&& w <= StickerMaxSize
				&& h <= StickerMaxSize
				&& filesize < Storage::kMaxStickerInMemory) {
				attributes.push_back(MTP_documentAttributeSticker(MTP_flags(0), MTP_string(""), MTP_inputStickerSetEmpty(), MTPMaskCoords()));
				thumbFormat = "webp";
				thumbname = qsl("thumb.webp");
			}

			QPixmap full = (w > 90 || h > 90) ? App::pixmapFromImageInPlace(fullimage.scaled(90, 90, Qt::KeepAspectRatio, Qt::SmoothTransformation)) : QPixmap::fromImage(fullimage, Qt::ColorOnly);

			{
				QBuffer buffer(&thumbdata);
				full.save(&buffer, thumbFormat, thumbQuality);
			}

			thumb = full;
			thumbSize = MTP_photoSize(MTP_string(""), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(full.width()), MTP_int(full.height()), MTP_int(0));

			thumbId = rand_value<uint64>();
		}
	}

	if (_type == SendMediaType::Photo && photo.type() == mtpc_photoEmpty) {
		_type = SendMediaType::File;
	}

	if (isVoice) {
		auto flags = MTPDdocumentAttributeAudio::Flag::f_voice | MTPDdocumentAttributeAudio::Flag::f_waveform;
		attributes[0] = MTP_documentAttributeAudio(MTP_flags(flags), MTP_int(_duration), MTPstring(), MTPstring(), MTP_bytes(documentWaveformEncode5bit(_waveform)));
		attributes.resize(1);
		document = MTP_document(
			MTP_long(_id),
			MTP_long(0),
			MTP_bytes(QByteArray()),
			MTP_int(unixtime()),
			MTP_string(filemime),
			MTP_int(filesize),
			thumbSize,
			MTP_int(MTP::maindc()),
			MTP_vector<MTPDocumentAttribute>(attributes));
	} else if (_type != SendMediaType::Photo) {
		document = MTP_document(
			MTP_long(_id),
			MTP_long(0),
			MTP_bytes(QByteArray()),
			MTP_int(unixtime()),
			MTP_string(filemime),
			MTP_int(filesize),
			thumbSize,
			MTP_int(MTP::maindc()),
			MTP_vector<MTPDocumentAttribute>(attributes));
		_type = SendMediaType::File;
	}

	_result->type = _type;
	_result->filepath = _filepath;
	_result->content = _content;

	_result->filename = filename;
	_result->filemime = filemime;
	_result->setFileData(filedata);

	_result->thumbId = thumbId;
	_result->thumbname = thumbname;
	_result->setThumbData(thumbdata);
	_result->thumb = thumb;

	_result->photo = photo;
	_result->document = document;
	_result->photoThumbs = photoThumbs;
}

void FileLoadTask::finish() {
	if (!_result || !_result->filesize || _result->filesize < 0) {
		Ui::show(
			Box<InformBox>(lng_send_image_empty(lt_name, _filepath)),
			LayerOption::KeepOther);
		removeFromAlbum();
	} else if (_result->filesize > App::kFileSizeLimit) {
		Ui::show(
			Box<InformBox>(
				lng_send_image_too_large(lt_name, _filepath)),
			LayerOption::KeepOther);
		removeFromAlbum();
	} else if (App::main()) {
		App::main()->onSendFileConfirm(_result);
	}
}

void FileLoadTask::removeFromAlbum() {
	if (!_album) {
		return;
	}
	const auto proj = [](const SendingAlbum::Item &item) {
		return item.taskId;
	};
	const auto it = ranges::find(_album->items, id(), proj);
	Assert(it != _album->items.end());

	_album->items.erase(it);
}
