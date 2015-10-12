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
#pragma once

enum ToPrepareMediaType {
	ToPrepareAuto,
	ToPreparePhoto,
	ToPrepareAudio,
	ToPrepareVideo,
	ToPrepareDocument,
};

struct ToPrepareMedia {
	ToPrepareMedia(const QString &file, const PeerId &peer, ToPrepareMediaType t, bool broadcast, bool ctrlShiftEnter, MsgId replyTo) : id(MTP::nonce<PhotoId>()), file(file), peer(peer), type(t), duration(0), ctrlShiftEnter(ctrlShiftEnter), replyTo(replyTo) {
	}
	ToPrepareMedia(const QImage &img, const PeerId &peer, ToPrepareMediaType t, bool broadcast, bool ctrlShiftEnter, MsgId replyTo) : id(MTP::nonce<PhotoId>()), img(img), peer(peer), type(t), duration(0), ctrlShiftEnter(ctrlShiftEnter), replyTo(replyTo) {
	}
	ToPrepareMedia(const QByteArray &data, const PeerId &peer, ToPrepareMediaType t, bool broadcast, bool ctrlShiftEnter, MsgId replyTo) : id(MTP::nonce<PhotoId>()), data(data), peer(peer), type(t), duration(0), ctrlShiftEnter(ctrlShiftEnter), replyTo(replyTo) {
	}
	ToPrepareMedia(const QByteArray &data, int32 duration, const PeerId &peer, ToPrepareMediaType t, bool broadcast, bool ctrlShiftEnter, MsgId replyTo) : id(MTP::nonce<PhotoId>()), data(data), peer(peer), type(t), duration(duration), ctrlShiftEnter(ctrlShiftEnter), replyTo(replyTo) {
	}
	PhotoId id;
	QString file;
	QImage img;
	QByteArray data;
	PeerId peer;
	ToPrepareMediaType type;
	int32 duration;
	bool broadcast;
	bool ctrlShiftEnter;
	MsgId replyTo;
};
typedef QList<ToPrepareMedia> ToPrepareMedias;

typedef QMap<int32, QByteArray> LocalFileParts;
struct ReadyLocalMedia {
	ReadyLocalMedia(ToPrepareMediaType type, const QString &file, const QString &filename, int32 filesize, const QByteArray &data, const uint64 &id, const uint64 &thumbId, const QString &thumbExt, const PeerId &peer, const MTPPhoto &photo, const MTPAudio &audio, const PreparedPhotoThumbs &photoThumbs, const MTPDocument &document, const QByteArray &jpeg, bool broadcast, bool ctrlShiftEnter, MsgId replyTo) :
		replyTo(replyTo), type(type), file(file), filename(filename), filesize(filesize), data(data), thumbExt(thumbExt), id(id), thumbId(thumbId), peer(peer), photo(photo), document(document), audio(audio), photoThumbs(photoThumbs), broadcast(broadcast), ctrlShiftEnter(ctrlShiftEnter) {
		if (!jpeg.isEmpty()) {
			int32 size = jpeg.size();
			for (int32 i = 0, part = 0; i < size; i += UploadPartSize, ++part) {
				parts.insert(part, jpeg.mid(i, UploadPartSize));
			}
			jpeg_md5.resize(32);
			hashMd5Hex(jpeg.constData(), jpeg.size(), jpeg_md5.data());
		}
	}
	MsgId replyTo;
	ToPrepareMediaType type;
	QString file, filename;
	int32 filesize;
	QByteArray data;
	QString thumbExt;
	uint64 id, thumbId; // id always file-id of media, thumbId is file-id of thumb ( == id for photos)
	PeerId peer;

	MTPPhoto photo;
	MTPDocument document;
	MTPAudio audio;
	PreparedPhotoThumbs photoThumbs;
	LocalFileParts parts;
	QByteArray jpeg_md5;

	bool broadcast;
	bool ctrlShiftEnter;
	QString caption;
};
typedef QList<ReadyLocalMedia> ReadyLocalMedias;

class LocalImageLoader;
class LocalImageLoaderPrivate : public QObject {
	Q_OBJECT

public:

	LocalImageLoaderPrivate(LocalImageLoader *loader, QThread *thread);
	~LocalImageLoaderPrivate();

public slots:

	void prepareImages();

signals:

	void imageReady();
	void imageFailed(quint64 id);

private:

	LocalImageLoader *loader;

};

class LocalImageLoader : public QObject {
	Q_OBJECT

public:

	LocalImageLoader(QObject *parent);
	void append(const QStringList &files, const PeerId &peer, bool broadcast, MsgId replyTo, ToPrepareMediaType t);
	PhotoId append(const QByteArray &img, const PeerId &peer, bool broadcast, MsgId replyTo, ToPrepareMediaType t);
	AudioId append(const QByteArray &audio, int32 duration, const PeerId &peer, bool broadcast, MsgId replyTo, ToPrepareMediaType t);
	PhotoId append(const QImage &img, const PeerId &peer, bool broadcast, MsgId replyTo, ToPrepareMediaType t, bool ctrlShiftEnter = false);
	PhotoId append(const QString &file, const PeerId &peer, bool broadcast, MsgId replyTo, ToPrepareMediaType t);

	QMutex *readyMutex();
	ReadyLocalMedias &readyList();

	QMutex *toPrepareMutex();
	ToPrepareMedias &toPrepareMedias();

	~LocalImageLoader();

public slots:

	void onImageReady();
	void onImageFailed(quint64 id);

signals:

	void imageReady();
	void imageFailed(quint64 id);
	void needToPrepare();

private:

	ReadyLocalMedias ready;
	ToPrepareMedias toPrepare;
	QMutex readyLock, toPrepareLock;
	QThread *thread;
	LocalImageLoaderPrivate *priv;

};

class Task {
public:

	virtual void process() = 0; // is executed in a separate thread
	virtual void finish() = 0; // is executed in the same as TaskQueue thread
	virtual ~Task() {
	}

	TaskId id() const {
		return TaskId(this);
	}

};
typedef QSharedPointer<Task> TaskPtr;

class TaskQueueWorker;
class TaskQueue : public QObject {
	Q_OBJECT

public:

	TaskQueue(QObject *parent, int32 stopTimeoutMs = 0); // <= 0 - never stop worker

	TaskId addTask(TaskPtr task);
	void cancelTask(TaskId id); // this task finish() won't be called
	
	template <typename DerivedTask>
	TaskId addTask(DerivedTask *task) {
		return addTask(TaskPtr(task));
	}

	~TaskQueue();

signals:

	void taskAdded();

public slots:

	void onTaskProcessed();
	void stop();

private:

	typedef QList<TaskPtr> Tasks;
	friend class TaskQueueWorker;

	Tasks _tasksToProcess, _tasksToFinish;
	QMutex _tasksToProcessMutex, _tasksToFinishMutex;
	QThread *_thread;
	TaskQueueWorker *_worker;
	QTimer *_stopTimer;

};

class TaskQueueWorker : public QObject {
	Q_OBJECT

public:

	TaskQueueWorker(TaskQueue *queue) : _queue(queue), _inTaskAdded(false) {
	}

signals:

	void taskProcessed();

public slots:

	void onTaskAdded();

private:
	TaskQueue *_queue;
	bool _inTaskAdded;

};
