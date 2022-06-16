/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/localimageloader.h"

#include "api/api_text_entities.h"
#include "api/api_sending.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "core/file_utilities.h"
#include "core/mime_type.h"
#include "base/unixtime.h"
#include "base/random.h"
#include "editor/scene/scene_item_sticker.h"
#include "editor/scene/scene.h"
#include "media/audio/media_audio.h"
#include "media/clip/media_clip_reader.h"
#include "mtproto/facade.h"
#include "lottie/lottie_animation.h"
#include "history/history.h"
#include "history/history_item.h"
#include "boxes/send_files_box.h"
#include "boxes/premium_limits_box.h"
#include "ui/boxes/confirm_box.h"
#include "ui/image/image_prepare.h"
#include "lang/lang_keys.h"
#include "storage/file_download.h"
#include "storage/storage_media_prepare.h"
#include "window/themes/window_theme_preview.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "main/main_session.h"

#include <QtCore/QBuffer>
#include <QtGui/QImageWriter>
#include <QtGui/QColorSpace>

namespace {

constexpr auto kThumbnailQuality = 87;
constexpr auto kThumbnailSize = 320;
constexpr auto kPhotoUploadPartSize = 32 * 1024;
constexpr auto kRecompressAfterBpp = 4;

using Ui::ValidateThumbDimensions;

struct PreparedFileThumbnail {
	uint64 id = 0;
	QString name;
	QImage image;
	QByteArray bytes;
	MTPPhotoSize mtpSize = MTP_photoSizeEmpty(MTP_string());
};

[[nodiscard]] PreparedFileThumbnail PrepareFileThumbnail(QImage &&original) {
	const auto width = original.width();
	const auto height = original.height();
	if (!ValidateThumbDimensions(width, height)) {
		return {};
	}
	auto result = PreparedFileThumbnail();
	result.id = base::RandomValue<uint64>();
	const auto scaled = (width > kThumbnailSize || height > kThumbnailSize);
	const auto scaledWidth = [&] {
		return (width > height)
			? kThumbnailSize
			: int(base::SafeRound(kThumbnailSize * width / float64(height)));
	};
	const auto scaledHeight = [&] {
		return (width > height)
			? int(base::SafeRound(kThumbnailSize * height / float64(width)))
			: kThumbnailSize;
	};
	result.image = scaled
		? original.scaled(
			scaledWidth(),
			scaledHeight(),
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation)
		: std::move(original);
	result.mtpSize = MTP_photoSize(
		MTP_string(),
		MTP_int(result.image.width()),
		MTP_int(result.image.height()),
		MTP_int(0));
	return result;
}

[[nodiscard]] bool FileThumbnailUploadRequired(
		const QString &filemime,
		int64 filesize) {
	constexpr auto kThumbnailUploadBySize = 5 * int64(1024 * 1024);
	const auto kThumbnailKnownMimes = {
		"image/jpeg",
		"image/gif",
		"image/png",
		"image/webp",
		"video/mp4",
	};
	return (filesize > kThumbnailUploadBySize)
		|| (ranges::find(kThumbnailKnownMimes, filemime.toLower())
			== end(kThumbnailKnownMimes));
}

[[nodiscard]] PreparedFileThumbnail FinalizeFileThumbnail(
		PreparedFileThumbnail &&prepared,
		const QString &filemime,
		int64 filesize,
		bool isSticker) {
	prepared.name = isSticker ? qsl("thumb.webp") : qsl("thumb.jpg");
	if (FileThumbnailUploadRequired(filemime, filesize)) {
		const auto format = isSticker ? "WEBP" : "JPG";
		auto buffer = QBuffer(&prepared.bytes);
		prepared.image.save(&buffer, format, kThumbnailQuality);
	}
	return std::move(prepared);
}

[[nodiscard]] auto FindAlbumItem(
		std::vector<SendingAlbum::Item> &items,
		not_null<HistoryItem*> item) {
	const auto result = ranges::find(
		items,
		item->fullId(),
		&SendingAlbum::Item::msgId);

	Ensures(result != end(items));
	return result;
}

[[nodiscard]] MTPInputSingleMedia PrepareAlbumItemMedia(
		not_null<HistoryItem*> item,
		const MTPInputMedia &media,
		uint64 randomId) {
	auto caption = item->originalText();
	TextUtilities::Trim(caption);
	auto sentEntities = Api::EntitiesToMTP(
		&item->history()->session(),
		caption.entities,
		Api::ConvertOption::SkipLocal);
	const auto flags = !sentEntities.v.isEmpty()
		? MTPDinputSingleMedia::Flag::f_entities
		: MTPDinputSingleMedia::Flag(0);

	return MTP_inputSingleMedia(
		MTP_flags(flags),
		media,
		MTP_long(randomId),
		MTP_string(caption.text),
		sentEntities);
}

[[nodiscard]] std::vector<not_null<DocumentData*>> ExtractStickersFromScene(
		not_null<const Ui::PreparedFileInformation::Image*> info) {
	const auto allItems = info->modifications.paint->items();

	return ranges::views::all(
		allItems
	) | ranges::views::filter([](const Editor::Scene::ItemPtr &i) {
		return i->isVisible() && (i->type() == Editor::ItemSticker::Type);
	}) | ranges::views::transform([](const Editor::Scene::ItemPtr &i) {
		return static_cast<Editor::ItemSticker*>(i.get())->sticker();
	}) | ranges::to_vector;
}

[[nodiscard]] QByteArray ComputePhotoJpegBytes(
		QImage &full,
		const QByteArray &bytes,
		const QByteArray &format) {
	if (!bytes.isEmpty()
		&& (bytes.size()
			<= full.width() * full.height() * kRecompressAfterBpp / 8)
		&& (format == u"jpeg"_q)
		&& Images::IsProgressiveJpeg(bytes)) {
		return bytes;
	}

	// We have an example of dark .png image that when being sent without
	// removing its color space is displayed fine on tdesktop, but with
	// a light gray background on mobile apps.
	full.setColorSpace(QColorSpace());
	auto result = QByteArray();
	QBuffer buffer(&result);
	QImageWriter writer(&buffer, "JPEG");
	writer.setQuality(87);
	writer.setProgressiveScanWrite(true);
	writer.write(full);
	buffer.close();

	return result;
}

} // namespace

SendMediaPrepare::SendMediaPrepare(
	const QString &file,
	const PeerId &peer,
	SendMediaType type,
	MsgId replyTo)
: id(base::RandomValue<PhotoId>())
, file(file)
, peer(peer)
, type(type)
, replyTo(replyTo) {
}

SendMediaPrepare::SendMediaPrepare(
	const QImage &img,
	const PeerId &peer,
	SendMediaType type,
	MsgId replyTo)
: id(base::RandomValue<PhotoId>())
, img(img)
, peer(peer)
, type(type)
, replyTo(replyTo) {
}

SendMediaPrepare::SendMediaPrepare(
	const QByteArray &data,
	const PeerId &peer,
	SendMediaType type,
	MsgId replyTo)
: id(base::RandomValue<PhotoId>())
, data(data)
, peer(peer)
, type(type)
, replyTo(replyTo) {
}

SendMediaPrepare::SendMediaPrepare(
	const QByteArray &data,
	int duration,
	const PeerId &peer,
	SendMediaType type,
	MsgId replyTo)
: id(base::RandomValue<PhotoId>())
, data(data)
, peer(peer)
, type(type)
, duration(duration)
, replyTo(replyTo) {
}

SendMediaReady::SendMediaReady(
	SendMediaType type,
	const QString &file,
	const QString &filename,
	int64 filesize,
	const QByteArray &data,
	const uint64 &id,
	const uint64 &thumbId,
	const QString &thumbExt,
	const PeerId &peer,
	const MTPPhoto &photo,
	const PreparedPhotoThumbs &photoThumbs,
	const MTPDocument &document,
	const QByteArray &jpeg,
	MsgId replyTo)
: replyTo(replyTo)
, type(type)
, file(file)
, filename(filename)
, filesize(filesize)
, data(data)
, thumbExt(thumbExt)
, id(id)
, thumbId(thumbId)
, peer(peer)
, photo(photo)
, document(document)
, photoThumbs(photoThumbs) {
	if (!jpeg.isEmpty()) {
		int32 size = jpeg.size();
		for (int32 i = 0, part = 0; i < size; i += kPhotoUploadPartSize, ++part) {
			parts.insert(part, jpeg.mid(i, kPhotoUploadPartSize));
		}
		jpeg_md5.resize(32);
		hashMd5Hex(jpeg.constData(), jpeg.size(), jpeg_md5.data());
	}
}

TaskQueue::TaskQueue(crl::time stopTimeoutMs) {
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
	taskAdded();
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
		delete base::take(_worker);
		delete base::take(_thread);
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
				taskProcessed();
			}
		}
		QCoreApplication::processEvents();
	} while (someTasksLeft && !thread()->isInterruptionRequested());

	_inTaskAdded = false;
}

SendingAlbum::SendingAlbum() : groupId(base::RandomValue<uint64>()) {
}

void SendingAlbum::fillMedia(
		not_null<HistoryItem*> item,
		const MTPInputMedia &media,
		uint64 randomId) {
	const auto i = FindAlbumItem(items, item);
	Assert(!i->media);

	i->randomId = randomId;
	i->media = PrepareAlbumItemMedia(item, media, randomId);
}

void SendingAlbum::refreshMediaCaption(not_null<HistoryItem*> item) {
	const auto i = FindAlbumItem(items, item);
	if (!i->media) {
		return;
	}
	i->media = i->media->match([&](const MTPDinputSingleMedia &data) {
		return PrepareAlbumItemMedia(
			item,
			data.vmedia(),
			data.vrandom_id().v);
	});
}

void SendingAlbum::removeItem(not_null<HistoryItem*> item) {
	const auto localId = item->fullId();
	const auto i = ranges::find(items, localId, &Item::msgId);
	const auto moveCaption = (items.size() > 1) && (i == begin(items));
	Assert(i != end(items));
	items.erase(i);
	if (moveCaption) {
		const auto caption = item->originalText();
		const auto firstId = items.front().msgId;
		if (const auto first = item->history()->owner().message(firstId)) {
			// We don't need to finishEdition() here, because the whole
			// album will be rebuilt after one item was removed from it.
			first->setText(caption);
			refreshMediaCaption(first);
		}
	}
}

SendingAlbum::Item::Item(TaskId taskId)
: taskId(taskId) {
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

void FileLoadResult::setFileData(const QByteArray &filedata) {
	if (filedata.isEmpty()) {
		partssize = 0;
	} else {
		partssize = filedata.size();
		for (int32 i = 0, part = 0; i < partssize; i += kPhotoUploadPartSize, ++part) {
			fileparts.insert(part, filedata.mid(i, kPhotoUploadPartSize));
		}
		filemd5.resize(32);
		hashMd5Hex(filedata.constData(), filedata.size(), filemd5.data());
	}
}

void FileLoadResult::setThumbData(const QByteArray &thumbdata) {
	if (!thumbdata.isEmpty()) {
		thumbbytes = thumbdata;
		int32 size = thumbdata.size();
		for (int32 i = 0, part = 0; i < size; i += kPhotoUploadPartSize, ++part) {
			thumbparts.insert(part, thumbdata.mid(i, kPhotoUploadPartSize));
		}
		thumbmd5.resize(32);
		hashMd5Hex(thumbdata.constData(), thumbdata.size(), thumbmd5.data());
	}
}


FileLoadTask::FileLoadTask(
	not_null<Main::Session*> session,
	const QString &filepath,
	const QByteArray &content,
	std::unique_ptr<Ui::PreparedFileInformation> information,
	SendMediaType type,
	const FileLoadTo &to,
	const TextWithTags &caption,
	std::shared_ptr<SendingAlbum> album)
: _id(base::RandomValue<uint64>())
, _session(session)
, _dcId(session->mainDcId())
, _to(to)
, _album(std::move(album))
, _filepath(filepath)
, _content(content)
, _information(std::move(information))
, _type(type)
, _caption(caption) {
	Expects(to.options.scheduled
		|| !to.replaceMediaOf
		|| IsServerMsgId(to.replaceMediaOf));
}

FileLoadTask::FileLoadTask(
	not_null<Main::Session*> session,
	const QByteArray &voice,
	int32 duration,
	const VoiceWaveform &waveform,
	const FileLoadTo &to,
	const TextWithTags &caption)
: _id(base::RandomValue<uint64>())
, _session(session)
, _dcId(session->mainDcId())
, _to(to)
, _content(voice)
, _duration(duration)
, _waveform(waveform)
, _type(SendMediaType::Audio)
, _caption(caption) {
}

FileLoadTask::~FileLoadTask() = default;

auto FileLoadTask::ReadMediaInformation(
	const QString &filepath,
	const QByteArray &content,
	const QString &filemime)
-> std::unique_ptr<Ui::PreparedFileInformation> {
	auto result = std::make_unique<Ui::PreparedFileInformation>();
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
		std::unique_ptr<Ui::PreparedFileInformation> &result) {
	static const auto mimes = {
		qstr("audio/mp3"),
		qstr("audio/m4a"),
		qstr("audio/aac"),
		qstr("audio/ogg"),
		qstr("audio/flac"),
		qstr("audio/opus"),
	};
	static const auto extensions = {
		qstr(".mp3"),
		qstr(".m4a"),
		qstr(".aac"),
		qstr(".ogg"),
		qstr(".flac"),
		qstr(".opus"),
		qstr(".oga"),
	};
	if (!filepath.isEmpty()
		&& !CheckMimeOrExtensions(
			filepath,
			result->filemime,
			mimes,
			extensions)) {
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
		std::unique_ptr<Ui::PreparedFileInformation> &result) {
	static const auto mimes = {
		qstr("video/mp4"),
		qstr("video/quicktime"),
	};
	static const auto extensions = {
		qstr(".mp4"),
		qstr(".mov"),
		qstr(".webm"),
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
		std::unique_ptr<Ui::PreparedFileInformation> &result) {
	auto read = [&] {
		if (filepath.endsWith(qstr(".tgs"), Qt::CaseInsensitive)) {
			auto image = Lottie::ReadThumbnail(
				Lottie::ReadContent(content, filepath));
			const auto success = !image.isNull();
			if (success) {
				result->filemime = qstr("application/x-tgsticker");
			}
			return Images::ReadResult{
				.image = std::move(image),
				.animated = success,
			};
		}
		return Images::Read({
			.path = filepath,
			.content = content,
			.returnContent = true,
		});
	}();
	return FillImageInformation(
		std::move(read.image),
		read.animated,
		result,
		std::move(read.content),
		std::move(read.format));
}

bool FileLoadTask::FillImageInformation(
		QImage &&image,
		bool animated,
		std::unique_ptr<Ui::PreparedFileInformation> &result,
		QByteArray content,
		QByteArray format) {
	Expects(result != nullptr);

	if (image.isNull()) {
		return false;
	}
	auto media = Ui::PreparedFileInformation::Image();
	media.data = std::move(image);
	media.bytes = std::move(content);
	media.format = std::move(format);
	media.animated = animated;
	result->media = media;
	return true;
}

void FileLoadTask::process(Args &&args) {
	_result = std::make_shared<FileLoadResult>(
		id(),
		_id,
		_to,
		_caption,
		_album);

	QString filename, filemime;
	qint64 filesize = 0;
	QByteArray filedata;

	auto isAnimation = false;
	auto isSong = false;
	auto isVideo = false;
	auto isVoice = (_type == SendMediaType::Audio);
	auto isSticker = false;

	auto fullimage = QImage();
	auto fullimagebytes = QByteArray();
	auto fullimageformat = QByteArray();
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
		if (auto image = std::get_if<Ui::PreparedFileInformation::Image>(
				&_information->media)) {
			fullimage = base::take(image->data);
			fullimagebytes = base::take(image->bytes);
			fullimageformat = base::take(image->format);
			if (!Core::IsMimeSticker(filemime)
				&& fullimageformat != u"jpeg"_q) {
				fullimage = Images::Opaque(std::move(fullimage));
				fullimagebytes = fullimageformat = QByteArray();
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
				if (auto image = std::get_if<Ui::PreparedFileInformation::Image>(
						&_information->media)) {
					fullimage = base::take(image->data);
					fullimagebytes = base::take(image->bytes);
					fullimageformat = base::take(image->format);
				}
			}
			const auto mimeType = Core::MimeTypeForData(_content);
			filemime = mimeType.name();
			if (!Core::IsMimeSticker(filemime)
				&& fullimageformat != u"jpeg"_q) {
				fullimage = Images::Opaque(std::move(fullimage));
				fullimagebytes = fullimageformat = QByteArray();
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
			if (auto image = std::get_if<Ui::PreparedFileInformation::Image>(
					&_information->media)) {
				fullimage = base::take(image->data);
				fullimagebytes = base::take(image->bytes);
				fullimageformat = base::take(image->format);
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
			fullimage = Images::Opaque(std::move(fullimage));
			fullimagebytes = fullimageformat = QByteArray();
		}
	}
	_result->filesize = qMin(filesize, qint64(UINT_MAX));

	if (!filesize || filesize > kFileSizePremiumLimit) {
		return;
	}

	PreparedPhotoThumbs photoThumbs;
	QVector<MTPPhotoSize> photoSizes;
	QImage goodThumbnail;
	QByteArray goodThumbnailBytes;

	QVector<MTPDocumentAttribute> attributes(1, MTP_documentAttributeFilename(MTP_string(filename)));

	auto thumbnail = PreparedFileThumbnail();

	auto photo = MTP_photoEmpty(MTP_long(0));
	auto document = MTP_documentEmpty(MTP_long(0));

	if (!isVoice) {
		if (!_information) {
			_information = readMediaInformation(filemime);
			filemime = _information->filemime;
		}
		if (auto song = std::get_if<Ui::PreparedFileInformation::Song>(
				&_information->media)) {
			isSong = true;
			auto flags = MTPDdocumentAttributeAudio::Flag::f_title | MTPDdocumentAttributeAudio::Flag::f_performer;
			attributes.push_back(MTP_documentAttributeAudio(MTP_flags(flags), MTP_int(song->duration), MTP_string(song->title), MTP_string(song->performer), MTPstring()));
			thumbnail = PrepareFileThumbnail(std::move(song->cover));
		} else if (auto video = std::get_if<Ui::PreparedFileInformation::Video>(
				&_information->media)) {
			isVideo = true;
			auto coverWidth = video->thumbnail.width();
			auto coverHeight = video->thumbnail.height();
			if (video->isGifv && !_album) {
				attributes.push_back(MTP_documentAttributeAnimated());
			}
			auto flags = MTPDdocumentAttributeVideo::Flags(0);
			if (video->supportsStreaming) {
				flags |= MTPDdocumentAttributeVideo::Flag::f_supports_streaming;
			}
			attributes.push_back(MTP_documentAttributeVideo(MTP_flags(flags), MTP_int(video->duration), MTP_int(coverWidth), MTP_int(coverHeight)));

			if (args.generateGoodThumbnail) {
				goodThumbnail = video->thumbnail;
				{
					QBuffer buffer(&goodThumbnailBytes);
					goodThumbnail.save(&buffer, "JPG", kThumbnailQuality);
				}
			}
			thumbnail = PrepareFileThumbnail(std::move(video->thumbnail));
		} else if (filemime == qstr("application/x-tdesktop-theme")
			|| filemime == qstr("application/x-tgtheme-tdesktop")) {
			goodThumbnail = Window::Theme::GeneratePreview(_content, _filepath);
			if (!goodThumbnail.isNull()) {
				QBuffer buffer(&goodThumbnailBytes);
				goodThumbnail.save(&buffer, "JPG", kThumbnailQuality);

				thumbnail = PrepareFileThumbnail(base::duplicate(goodThumbnail));
			}
		}
	}

	if (!fullimage.isNull() && fullimage.width() > 0 && !isSong && !isVideo && !isVoice) {
		auto w = fullimage.width(), h = fullimage.height();
		attributes.push_back(MTP_documentAttributeImageSize(MTP_int(w), MTP_int(h)));

		if (ValidateThumbDimensions(w, h)) {
			isSticker = Core::IsMimeSticker(filemime)
				&& (filesize < Storage::kMaxStickerBytesSize)
				&& (Core::IsMimeStickerAnimated(filemime)
					|| GoodStickerDimensions(w, h));
			if (isSticker) {
				attributes.push_back(MTP_documentAttributeSticker(
					MTP_flags(0),
					MTP_string(),
					MTP_inputStickerSetEmpty(),
					MTPMaskCoords()));
				if (isAnimation && args.generateGoodThumbnail) {
					goodThumbnail = fullimage;
					{
						QBuffer buffer(&goodThumbnailBytes);
						goodThumbnail.save(&buffer, "WEBP", kThumbnailQuality);
					}
				}
			} else if (isAnimation) {
				attributes.push_back(MTP_documentAttributeAnimated());
			} else if (filemime.startsWith(u"image/"_q)
				&& _type != SendMediaType::File) {
				auto medium = (w > 320 || h > 320) ? fullimage.scaled(320, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation) : fullimage;

				const auto downscaled = (w > 1280 || h > 1280);
				auto full = downscaled ? fullimage.scaled(1280, 1280, Qt::KeepAspectRatio, Qt::SmoothTransformation) : fullimage;
				if (downscaled) {
					fullimagebytes = fullimageformat = QByteArray();
				}
				filedata = ComputePhotoJpegBytes(full, fullimagebytes, fullimageformat);

				photoThumbs.emplace('m', PreparedPhotoThumb{ .image = medium });
				photoSizes.push_back(MTP_photoSize(MTP_string("m"), MTP_int(medium.width()), MTP_int(medium.height()), MTP_int(0)));

				photoThumbs.emplace('y', PreparedPhotoThumb{
					.image = full,
					.bytes = filedata
				});
				photoSizes.push_back(MTP_photoSize(MTP_string("y"), MTP_int(full.width()), MTP_int(full.height()), MTP_int(0)));

				photo = MTP_photo(
					MTP_flags(0),
					MTP_long(_id),
					MTP_long(0),
					MTP_bytes(),
					MTP_int(base::unixtime::now()),
					MTP_vector<MTPPhotoSize>(photoSizes),
					MTPVector<MTPVideoSize>(),
					MTP_int(_dcId));

				if (filesize < 0) {
					filesize = _result->filesize = filedata.size();
				}
			}
			thumbnail = PrepareFileThumbnail(std::move(fullimage));
		}
	}
	thumbnail = FinalizeFileThumbnail(
		std::move(thumbnail),
		filemime,
		filesize,
		isSticker);

	if (_type == SendMediaType::Photo && photo.type() == mtpc_photoEmpty) {
		_type = SendMediaType::File;
	}

	if (isVoice) {
		auto flags = MTPDdocumentAttributeAudio::Flag::f_voice | MTPDdocumentAttributeAudio::Flag::f_waveform;
		attributes[0] = MTP_documentAttributeAudio(MTP_flags(flags), MTP_int(_duration), MTPstring(), MTPstring(), MTP_bytes(documentWaveformEncode5bit(_waveform)));
		attributes.resize(1);
		document = MTP_document(
			MTP_flags(0),
			MTP_long(_id),
			MTP_long(0),
			MTP_bytes(),
			MTP_int(base::unixtime::now()),
			MTP_string(filemime),
			MTP_long(filesize),
			MTP_vector<MTPPhotoSize>(1, thumbnail.mtpSize),
			MTPVector<MTPVideoSize>(),
			MTP_int(_dcId),
			MTP_vector<MTPDocumentAttribute>(attributes));
	} else if (_type != SendMediaType::Photo) {
		document = MTP_document(
			MTP_flags(0),
			MTP_long(_id),
			MTP_long(0),
			MTP_bytes(),
			MTP_int(base::unixtime::now()),
			MTP_string(filemime),
			MTP_long(filesize),
			MTP_vector<MTPPhotoSize>(1, thumbnail.mtpSize),
			MTPVector<MTPVideoSize>(),
			MTP_int(_dcId),
			MTP_vector<MTPDocumentAttribute>(attributes));
		_type = SendMediaType::File;
	}

	if (_information) {
		if (auto image = std::get_if<Ui::PreparedFileInformation::Image>(
				&_information->media)) {
			if (image->modifications.paint) {
				const auto documents = ExtractStickersFromScene(image);
				_result->attachedStickers = documents
					| ranges::view::transform(&DocumentData::mtpInput)
					| ranges::to_vector;
			}
		}
	}

	_result->type = _type;
	_result->filepath = _filepath;
	_result->content = _content;

	_result->filename = filename;
	_result->filemime = filemime;
	_result->setFileData(filedata);

	_result->thumbId = thumbnail.id;
	_result->thumbname = thumbnail.name;
	_result->setThumbData(thumbnail.bytes);
	_result->thumb = std::move(thumbnail.image);

	_result->goodThumbnail = std::move(goodThumbnail);
	_result->goodThumbnailBytes = std::move(goodThumbnailBytes);

	_result->photo = photo;
	_result->document = document;
	_result->photoThumbs = photoThumbs;
}

void FileLoadTask::finish() {
	const auto session = _session.get();
	if (!session) {
		return;
	}
	const auto premium = session->user()->isPremium();
	if (!_result || !_result->filesize || _result->filesize < 0) {
		Ui::show(
			Ui::MakeInformBox(
				tr::lng_send_image_empty(tr::now, lt_name, _filepath)),
			Ui::LayerOption::KeepOther);
		removeFromAlbum();
	} else if (_result->filesize > kFileSizePremiumLimit
		|| (_result->filesize > kFileSizeLimit && !premium)) {
		Ui::show(
			Box(FileSizeLimitBox, session, _result->filesize),
			Ui::LayerOption::KeepOther);
		removeFromAlbum();
	} else {
		Api::SendConfirmedFile(session, _result);
	}
}

FileLoadResult *FileLoadTask::peekResult() const {
	return _result.get();
}

std::unique_ptr<Ui::PreparedFileInformation> FileLoadTask::readMediaInformation(
		const QString &filemime) const {
	return ReadMediaInformation(_filepath, _content, filemime);
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
