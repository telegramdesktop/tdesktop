/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "localimageloader.h"
#include "gui/filedialog.h"
#include "audio.h"

#include "boxes/photosendbox.h"
#include "mainwidget.h"
#include "window.h"
#include "lang.h"
#include "boxes/confirmbox.h"

TaskQueue::TaskQueue(QObject *parent, int32 stopTimeoutMs) : QObject(parent), _thread(0), _worker(0), _stopTimer(0) {
	if (stopTimeoutMs > 0) {
		_stopTimer = new QTimer(this);
		connect(_stopTimer, SIGNAL(timeout()), this, SLOT(stop()));
		_stopTimer->setSingleShot(true);
		_stopTimer->setInterval(stopTimeoutMs);
	}
}

TaskId TaskQueue::addTask(TaskPtr task) {
	{
		QMutexLocker lock(&_tasksToProcessMutex);
		_tasksToProcess.push_back(task);
	}

	wakeThread();

	return task->id();
}

void TaskQueue::addTasks(const TasksList &tasks) {
	{
		QMutexLocker lock(&_tasksToProcessMutex);
		_tasksToProcess.append(tasks);
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
	{
		QMutexLocker lock(&_tasksToProcessMutex);
		for (int32 i = 0, l = _tasksToProcess.size(); i != l; ++i) {
			if (_tasksToProcess.at(i)->id() == id) {
				_tasksToProcess.removeAt(i);
				return;
			}
		}
	}
	QMutexLocker lock(&_tasksToFinishMutex);
	for (int32 i = 0, l = _tasksToFinish.size(); i != l; ++i) {
		if (_tasksToFinish.at(i)->id() == id) {
			_tasksToFinish.removeAt(i);
			return;
		}
	}
}

void TaskQueue::onTaskProcessed() {
	do {
		TaskPtr task;
		{
			QMutexLocker lock(&_tasksToFinishMutex);
			if (_tasksToFinish.isEmpty()) break;
			task = _tasksToFinish.front();
			_tasksToFinish.pop_front();
		}
		task->finish();
	} while (true);

	if (_stopTimer) {
		QMutexLocker lock(&_tasksToProcessMutex);
		if (_tasksToProcess.isEmpty()) {
			_stopTimer->start();
		}
	}
}

void TaskQueue::stop() {
	if (_thread) {
		_thread->requestInterruption();
		_thread->quit();
		_thread->wait();
		delete _worker;
		delete _thread;
		_worker = 0;
		_thread = 0;
	}
	_tasksToProcess.clear();
	_tasksToFinish.clear();
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
		TaskPtr task;
		{
			QMutexLocker lock(&_queue->_tasksToProcessMutex);
			if (!_queue->_tasksToProcess.isEmpty()) {
				task = _queue->_tasksToProcess.front();
			}
		}

		if (task) {
			task->process();
			bool emitTaskProcessed = false;
			{
				QMutexLocker lockToProcess(&_queue->_tasksToProcessMutex);
				if (!_queue->_tasksToProcess.isEmpty() && _queue->_tasksToProcess.front() == task) {
					_queue->_tasksToProcess.pop_front();
					someTasksLeft = !_queue->_tasksToProcess.isEmpty();

					QMutexLocker lockToFinish(&_queue->_tasksToFinishMutex);
					emitTaskProcessed = _queue->_tasksToFinish.isEmpty();
					_queue->_tasksToFinish.push_back(task);
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

FileLoadTask::FileLoadTask(const QString &filepath, PrepareMediaType type, const FileLoadTo &to, FileLoadForceConfirmType confirm) : _id(MTP::nonce<uint64>())
, _to(to)
, _filepath(filepath)
, _duration(0)
, _type(type)
, _confirm(confirm)
, _result(0) {
}

FileLoadTask::FileLoadTask(const QByteArray &content, PrepareMediaType type, const FileLoadTo &to) : _id(MTP::nonce<uint64>())
, _to(to)
, _content(content)
, _duration(0)
, _type(type)
, _confirm(FileLoadNoForceConfirm)
, _result(0) {
}

FileLoadTask::FileLoadTask(const QImage &image, PrepareMediaType type, const FileLoadTo &to, FileLoadForceConfirmType confirm, const QString &originalText) : _id(MTP::nonce<uint64>())
, _to(to)
, _image(image)
, _duration(0)
, _type(type)
, _confirm(confirm)
, _originalText(originalText)
, _result(0) {
}

FileLoadTask::FileLoadTask(const QByteArray &audio, int32 duration, const FileLoadTo &to) : _id(MTP::nonce<uint64>())
, _to(to)
, _content(audio)
, _duration(duration)
, _type(PrepareAudio)
, _confirm(FileLoadNoForceConfirm)
, _result(0) {
}

void FileLoadTask::process() {
	const QString stickerMime = qsl("image/webp");

	_result = FileLoadResultPtr(new FileLoadResult(_id, _to, _originalText));

	QString filename, filemime;
	qint64 filesize = 0;
	QByteArray filedata;

	uint64 thumbId = 0;
	QString thumbname = "thumb.jpg";
	QByteArray thumbdata;

	bool animated = false;
	QImage fullimage = _image;

	if (!_filepath.isEmpty()) {
		QFileInfo info(_filepath);
		if (info.isDir()) {
			_result->filesize = -1;
			return;
		}
		filesize = info.size();
		filemime = mimeTypeForFile(info).name();
		filename = info.fileName();
		if (filesize <= MaxUploadPhotoSize && _type != PrepareAudio) {
			bool opaque = (filemime != stickerMime);
			fullimage = App::readImage(_filepath, 0, opaque, &animated);
		}
	} else if (!_content.isEmpty()) {
		filesize = _content.size();
		MimeType mimeType = mimeTypeForData(_content);
		filemime = mimeType.name();
		if (filesize <= MaxUploadPhotoSize && _type != PrepareAudio) {
			bool opaque = (filemime != stickerMime);
			fullimage = App::readImage(_content, 0, opaque, &animated);
		}
		if (filemime == "image/jpeg") {
			filename = filedialogDefaultName(qsl("image"), qsl(".jpg"), QString(), true);
		} else if (_type == PrepareAudio) {
			filename = filedialogDefaultName(qsl("audio"), qsl(".ogg"), QString(), true);
			filemime = "audio/ogg";
		} else {
			QString ext;
			QStringList patterns = mimeType.globPatterns();
			if (!patterns.isEmpty()) {
				ext = patterns.front().replace('*', QString());
			}
			filename = filedialogDefaultName(qsl("file"), ext, QString(), true);
		}
	} else if (!_image.isNull()) {
		_image = QImage();

		filemime = mimeTypeForName("image/png").name();
		filename = filedialogDefaultName(qsl("image"), qsl(".png"), QString(), true);
		{
			QBuffer buffer(&_content);
			fullimage.save(&buffer, "PNG");
		}
		filesize = _content.size();

		if (fullimage.hasAlphaChannel()) {
			QImage solid(fullimage.width(), fullimage.height(), QImage::Format_ARGB32_Premultiplied);
			solid.fill(st::white->c);
			{
				QPainter(&solid).drawImage(0, 0, fullimage);
			}
			fullimage = solid;
		}
	}
	_result->filesize = (int32)qMin(filesize, qint64(INT_MAX));

	if (!filesize || filesize > MaxUploadDocumentSize) {
		return;
	}

	PreparedPhotoThumbs photoThumbs;
	QVector<MTPPhotoSize> photoSizes;
	QPixmap thumb;

	QVector<MTPDocumentAttribute> attributes(1, MTP_documentAttributeFilename(MTP_string(filename)));

	MTPPhotoSize thumbSize(MTP_photoSizeEmpty(MTP_string("")));
	MTPPhoto photo(MTP_photoEmpty(MTP_long(0)));
	MTPDocument document(MTP_documentEmpty(MTP_long(0)));
	MTPAudio audio(MTP_audioEmpty(MTP_long(0)));

	bool song = false, gif = false;
	if (_type != PrepareAudio) {
		if (filemime == qstr("audio/mp3") || filemime == qstr("audio/m4a") || filemime == qstr("audio/aac") || filemime == qstr("audio/ogg") || filemime == qstr("audio/flac") ||
			filename.endsWith(qstr(".mp3"), Qt::CaseInsensitive) || filename.endsWith(qstr(".m4a"), Qt::CaseInsensitive) ||
			filename.endsWith(qstr(".aac"), Qt::CaseInsensitive) || filename.endsWith(qstr(".ogg"), Qt::CaseInsensitive) ||
			filename.endsWith(qstr(".flac"), Qt::CaseInsensitive)) {
			QImage cover;
			QByteArray coverBytes, coverFormat;
			MTPDocumentAttribute audioAttribute = audioReadSongAttributes(_filepath, _content, cover, coverBytes, coverFormat);
			if (audioAttribute.type() == mtpc_documentAttributeAudio) {
				attributes.push_back(audioAttribute);
				song = true;
				if (!cover.isNull()) { // cover to thumb
					int32 cw = cover.width(), ch = cover.height();
					if (cw < 20 * ch && ch < 20 * cw) {
						QPixmap full = (cw > 90 || ch > 90) ? QPixmap::fromImage(cover.scaled(90, 90, Qt::KeepAspectRatio, Qt::SmoothTransformation), Qt::ColorOnly) : QPixmap::fromImage(cover, Qt::ColorOnly);
						{
							QByteArray thumbFormat = "JPG";
							int32 thumbQuality = 87;

							QBuffer buffer(&thumbdata);
							full.save(&buffer, thumbFormat, thumbQuality);
						}

						thumb = full;
						thumbSize = MTP_photoSize(MTP_string(""), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(full.width()), MTP_int(full.height()), MTP_int(0));

						thumbId = MTP::nonce<uint64>();
					}
				}
			}
		}
		if (filemime == qstr("video/mp4") || filename.endsWith(qstr(".mp4"), Qt::CaseInsensitive) || animated) {
			QImage cover;
			MTPDocumentAttribute animatedAttribute = clipReadAnimatedAttributes(_filepath, _content, cover);
			if (animatedAttribute.type() == mtpc_documentAttributeVideo) {
				int32 cw = cover.width(), ch = cover.height();
				if (cw < 20 * ch && ch < 20 * cw) {
					attributes.push_back(MTP_documentAttributeAnimated());
					attributes.push_back(animatedAttribute);
					gif = true;

					QPixmap full = (cw > 90 || ch > 90) ? QPixmap::fromImage(cover.scaled(90, 90, Qt::KeepAspectRatio, Qt::SmoothTransformation), Qt::ColorOnly) : QPixmap::fromImage(cover, Qt::ColorOnly);
					{
						QByteArray thumbFormat = "JPG";
						int32 thumbQuality = 87;

						QBuffer buffer(&thumbdata);
						full.save(&buffer, thumbFormat, thumbQuality);
					}

					thumb = full;
					thumbSize = MTP_photoSize(MTP_string(""), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(full.width()), MTP_int(full.height()), MTP_int(0));

					thumbId = MTP::nonce<uint64>();

					if (filename.endsWith(qstr(".mp4"), Qt::CaseInsensitive)) {
						filemime = qstr("video/mp4");
					}
				}
			}
		}
	}

	if (!fullimage.isNull() && fullimage.width() > 0 && !song && !gif) {
		int32 w = fullimage.width(), h = fullimage.height();
		attributes.push_back(MTP_documentAttributeImageSize(MTP_int(w), MTP_int(h)));

		if (w < 20 * h && h < 20 * w) {
			if (animated) {
				attributes.push_back(MTP_documentAttributeAnimated());
			} else if (_type != PrepareDocument) {
				QPixmap thumb = (w > 100 || h > 100) ? QPixmap::fromImage(fullimage.scaled(100, 100, Qt::KeepAspectRatio, Qt::SmoothTransformation), Qt::ColorOnly) : QPixmap::fromImage(fullimage);
				photoThumbs.insert('s', thumb);
				photoSizes.push_back(MTP_photoSize(MTP_string("s"), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(thumb.width()), MTP_int(thumb.height()), MTP_int(0)));

				QPixmap medium = (w > 320 || h > 320) ? QPixmap::fromImage(fullimage.scaled(320, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation), Qt::ColorOnly) : QPixmap::fromImage(fullimage);
				photoThumbs.insert('m', medium);
				photoSizes.push_back(MTP_photoSize(MTP_string("m"), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(medium.width()), MTP_int(medium.height()), MTP_int(0)));

				QPixmap full = (w > 1280 || h > 1280) ? QPixmap::fromImage(fullimage.scaled(1280, 1280, Qt::KeepAspectRatio, Qt::SmoothTransformation), Qt::ColorOnly) : QPixmap::fromImage(fullimage);
				photoThumbs.insert('y', full);
				photoSizes.push_back(MTP_photoSize(MTP_string("y"), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(full.width()), MTP_int(full.height()), MTP_int(0)));

				{
					QBuffer buffer(&filedata);
					full.save(&buffer, "JPG", 77);
				}

				photo = MTP_photo(MTP_long(_id), MTP_long(0), MTP_int(unixtime()), MTP_vector<MTPPhotoSize>(photoSizes));
			}

			QByteArray thumbFormat = "JPG";
			int32 thumbQuality = 87;
			if (!animated && filemime == stickerMime && w > 0 && h > 0 && w <= StickerMaxSize && h <= StickerMaxSize && filesize < StickerInMemory) {
				attributes.push_back(MTP_documentAttributeSticker(MTP_string(""), MTP_inputStickerSetEmpty()));
				thumbFormat = "webp";
				thumbname = qsl("thumb.webp");
			}

			QPixmap full = (w > 90 || h > 90) ? QPixmap::fromImage(fullimage.scaled(90, 90, Qt::KeepAspectRatio, Qt::SmoothTransformation), Qt::ColorOnly) : QPixmap::fromImage(fullimage, Qt::ColorOnly);

			{
				QBuffer buffer(&thumbdata);
				full.save(&buffer, thumbFormat, thumbQuality);
			}

			thumb = full;
			thumbSize = MTP_photoSize(MTP_string(""), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(full.width()), MTP_int(full.height()), MTP_int(0));

			thumbId = MTP::nonce<uint64>();
		}
	}

	if (_type == PrepareAudio) {
		audio = MTP_audio(MTP_long(_id), MTP_long(0), MTP_int(unixtime()), MTP_int(_duration), MTP_string(filemime), MTP_int(filesize), MTP_int(MTP::maindc()));
	} else {
		document = MTP_document(MTP_long(_id), MTP_long(0), MTP_int(unixtime()), MTP_string(filemime), MTP_int(filesize), thumbSize, MTP_int(MTP::maindc()), MTP_vector<MTPDocumentAttribute>(attributes));
		if (photo.type() == mtpc_photoEmpty) {
			_type = PrepareDocument;
		}
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
	_result->audio = audio;
	_result->document = document;
	_result->photoThumbs = photoThumbs;
}

void FileLoadTask::finish() {
	if (!_result || !_result->filesize) {
		if (_result) App::main()->onSendFileCancel(_result);
		Ui::showLayer(new InformBox(lang(lng_send_image_empty)), KeepOtherLayers);
		return;
	}
	if (_result->filesize == -1) { // dir
		App::main()->onSendFileCancel(_result);
		Ui::showLayer(new InformBox(lng_send_folder(lt_name, QFileInfo(_filepath).dir().dirName())), KeepOtherLayers);
		return;
	}
	if (_result->filesize > MaxUploadDocumentSize) {
		App::main()->onSendFileCancel(_result);
		Ui::showLayer(new InformBox(lang(lng_send_image_too_large)), KeepOtherLayers);
		return;
	}
	if (App::main()) {
		bool confirm = (_confirm == FileLoadAlwaysConfirm) || (_result->photo.type() != mtpc_photoEmpty && _confirm != FileLoadNeverConfirm);
		if (confirm) {
			Ui::showLayer(new PhotoSendBox(_result), ShowAfterOtherLayers);
		} else {
			if (_result->type == PrepareAuto) {
				_result->type = (_result->photo.type() != mtpc_photoEmpty) ? PreparePhoto : PrepareDocument;
			}
			App::main()->onSendFileConfirm(_result, false);
		}
	}
}
