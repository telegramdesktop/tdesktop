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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

enum PrepareMediaType {
	PrepareAuto,
	PreparePhoto,
	PrepareAudio,
	PrepareVideo,
	PrepareDocument,
};

struct ToPrepareMedia {
	ToPrepareMedia(const QString &file, const PeerId &peer, PrepareMediaType t, bool broadcast, bool ctrlShiftEnter, MsgId replyTo) : id(MTP::nonce<PhotoId>()), file(file), peer(peer), type(t), duration(0), ctrlShiftEnter(ctrlShiftEnter), replyTo(replyTo) {
	}
	ToPrepareMedia(const QImage &img, const PeerId &peer, PrepareMediaType t, bool broadcast, bool ctrlShiftEnter, MsgId replyTo) : id(MTP::nonce<PhotoId>()), img(img), peer(peer), type(t), duration(0), ctrlShiftEnter(ctrlShiftEnter), replyTo(replyTo) {
	}
	ToPrepareMedia(const QByteArray &data, const PeerId &peer, PrepareMediaType t, bool broadcast, bool ctrlShiftEnter, MsgId replyTo) : id(MTP::nonce<PhotoId>()), data(data), peer(peer), type(t), duration(0), ctrlShiftEnter(ctrlShiftEnter), replyTo(replyTo) {
	}
	ToPrepareMedia(const QByteArray &data, int32 duration, const PeerId &peer, PrepareMediaType t, bool broadcast, bool ctrlShiftEnter, MsgId replyTo) : id(MTP::nonce<PhotoId>()), data(data), peer(peer), type(t), duration(duration), ctrlShiftEnter(ctrlShiftEnter), replyTo(replyTo) {
	}
	PhotoId id;
	QString file;
	QImage img;
	QByteArray data;
	PeerId peer;
	PrepareMediaType type;
	int32 duration;
	bool broadcast;
	bool ctrlShiftEnter;
	MsgId replyTo;
};
typedef QList<ToPrepareMedia> ToPrepareMedias;

typedef QMap<int32, QByteArray> UploadFileParts;
struct ReadyLocalMedia {
	ReadyLocalMedia(PrepareMediaType type, const QString &file, const QString &filename, int32 filesize, const QByteArray &data, const uint64 &id, const uint64 &thumbId, const QString &thumbExt, const PeerId &peer, const MTPPhoto &photo, const MTPAudio &audio, const PreparedPhotoThumbs &photoThumbs, const MTPDocument &document, const QByteArray &jpeg, bool broadcast, bool ctrlShiftEnter, MsgId replyTo) :
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
	PrepareMediaType type;
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
	UploadFileParts parts;
	QByteArray jpeg_md5;

	bool broadcast;
	bool ctrlShiftEnter;
	QString caption;

	ReadyLocalMedia() : type(PrepareAuto) { // temp
	}
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
typedef QList<TaskPtr> TasksList;

class TaskQueueWorker;
class TaskQueue : public QObject {
	Q_OBJECT

public:

	TaskQueue(QObject *parent, int32 stopTimeoutMs = 0); // <= 0 - never stop worker

	TaskId addTask(TaskPtr task);
	void addTasks(const TasksList &tasks);
	void cancelTask(TaskId id); // this task finish() won't be called
	
	TaskId addTask(Task *task) {
		return addTask(TaskPtr(task));
	}

	~TaskQueue();

signals:

	void taskAdded();

public slots:

	void onTaskProcessed();
	void stop();

private:

	friend class TaskQueueWorker;

	void wakeThread();

	TasksList _tasksToProcess, _tasksToFinish;
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

struct FileLoadTo {
	FileLoadTo(const PeerId &peer, bool broadcast, MsgId replyTo) : peer(peer), broadcast(broadcast), replyTo(replyTo) {
	}
	PeerId peer;
	bool broadcast;
	MsgId replyTo;
};

struct FileLoadResult {
	FileLoadResult(const uint64 &id, const FileLoadTo &to, const QString &originalText) : id(id)
		, to(to)
		, type(PrepareAuto)
		, filesize(0)
		, thumbId(0)
		, originalText(originalText) {
	}

	uint64 id;
	FileLoadTo to;
	PrepareMediaType type;
	QString filepath;
	QByteArray content;

	QString filename;
	QString filemime;
	int32 filesize;
	UploadFileParts fileparts;
	QByteArray filemd5;
	int32 partssize;

	uint64 thumbId; // id is always file-id of media, thumbId is file-id of thumb ( == id for photos)
	QString thumbname;
	UploadFileParts thumbparts;
	QByteArray thumbmd5;
	QPixmap thumb;

	MTPPhoto photo;
	MTPAudio audio;
	MTPDocument document;

	PreparedPhotoThumbs photoThumbs;
	QString caption;

	QString originalText; // when pasted had an image mime save text mime here to insert if image send was cancelled

	void setFileData(const QByteArray &filedata) {
		if (filedata.isEmpty()) {
			partssize = 0;
		} else {
			partssize = filedata.size();
			for (int32 i = 0, part = 0; i < partssize; i += UploadPartSize, ++part) {
				fileparts.insert(part, filedata.mid(i, UploadPartSize));
			}
			filemd5.resize(32);
			hashMd5Hex(filedata.constData(), filedata.size(), filemd5.data());
		}
	}
	void setThumbData(const QByteArray &thumbdata) {
		if (!thumbdata.isEmpty()) {
			int32 size = thumbdata.size();
			for (int32 i = 0, part = 0; i < size; i += UploadPartSize, ++part) {
				thumbparts.insert(part, thumbdata.mid(i, UploadPartSize));
			}
			thumbmd5.resize(32);
			hashMd5Hex(thumbdata.constData(), thumbdata.size(), thumbmd5.data());
		}
	}
};
typedef QSharedPointer<FileLoadResult> FileLoadResultPtr;

enum FileLoadForceConfirmType {
	FileLoadNoForceConfirm,
	FileLoadNeverConfirm,
	FileLoadAlwaysConfirm,
};

class FileLoadTask : public Task {
public:

	FileLoadTask(const QString &filepath, PrepareMediaType type, const FileLoadTo &to, FileLoadForceConfirmType confirm = FileLoadNoForceConfirm);
	FileLoadTask(const QByteArray &content, PrepareMediaType type, const FileLoadTo &to);
	FileLoadTask(const QImage &image, PrepareMediaType type, const FileLoadTo &to, FileLoadForceConfirmType confirm = FileLoadNoForceConfirm, const QString &originalText = QString());
	FileLoadTask(const QByteArray &audio, int32 duration, const FileLoadTo &to);

	uint64 fileid() const {
		return _id;
	}

	void process();
	void finish();

protected:

	uint64 _id;
	FileLoadTo _to;
	QString _filepath;
	QImage _image;
	QByteArray _content;
	int32 _duration;
	PrepareMediaType _type;
	FileLoadForceConfirmType _confirm;
	QString _originalText;

	FileLoadResultPtr _result;

};
