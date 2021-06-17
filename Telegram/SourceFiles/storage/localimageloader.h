/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/openssl_help.h"
#include "base/variant.h"
#include "api/api_common.h"
#include "ui/chat/attach/attach_prepare.h"

constexpr auto kFileSizeLimit = 2000 * 1024 * 1024; // Load files up to 2000MB

enum class SendMediaType {
	Photo,
	Audio,
	File,
	ThemeFile,
	Secure,
};

struct SendMediaPrepare {
	SendMediaPrepare(
		const QString &file,
		const PeerId &peer,
		SendMediaType type,
		MsgId replyTo) : id(openssl::RandomValue<PhotoId>()),
		file(file),
		peer(peer),
		type(type),
		replyTo(replyTo) {
	}
	SendMediaPrepare(
		const QImage &img,
		const PeerId &peer,
		SendMediaType type,
		MsgId replyTo) : id(openssl::RandomValue<PhotoId>()),
		img(img),
		peer(peer),
		type(type),
		replyTo(replyTo) {
	}
	SendMediaPrepare(
		const QByteArray &data,
		const PeerId &peer,
		SendMediaType type,
		MsgId replyTo) : id(openssl::RandomValue<PhotoId>()),
		data(data),
		peer(peer),
		type(type),
		replyTo(replyTo) {
	}
	SendMediaPrepare(
		const QByteArray &data,
		int duration,
		const PeerId &peer,
		SendMediaType type,
		MsgId replyTo) : id(openssl::RandomValue<PhotoId>()),
		data(data),
		peer(peer),
		type(type),
		duration(duration),
		replyTo(replyTo) {
	}
	PhotoId id;
	QString file;
	QImage img;
	QByteArray data;
	PeerId peer;
	SendMediaType type;
	int duration = 0;
	MsgId replyTo;

};
using SendMediaPrepareList = QList<SendMediaPrepare>;

using UploadFileParts =  QMap<int, QByteArray>;
struct SendMediaReady {
	SendMediaReady() = default; // temp
	SendMediaReady(
		SendMediaType type,
		const QString &file,
		const QString &filename,
		int32 filesize,
		const QByteArray &data,
		const uint64 &id,
		const uint64 &thumbId,
		const QString &thumbExt,
		const PeerId &peer,
		const MTPPhoto &photo,
		const PreparedPhotoThumbs &photoThumbs,
		const MTPDocument &document,
		const QByteArray &jpeg,
		MsgId replyTo);

	MsgId replyTo;
	SendMediaType type;
	QString file, filename;
	int32 filesize;
	QByteArray data;
	QString thumbExt;
	uint64 id, thumbId; // id always file-id of media, thumbId is file-id of thumb ( == id for photos)
	PeerId peer;

	MTPPhoto photo;
	MTPDocument document;
	PreparedPhotoThumbs photoThumbs;
	UploadFileParts parts;
	QByteArray jpeg_md5;

	QString caption;

};

SendMediaReady PreparePeerPhoto(MTP::DcId dcId, PeerId peerId, QImage &&image);

using TaskId = void*; // no interface, just id

class Task {
public:
	virtual void process() = 0; // is executed in a separate thread
	virtual void finish() = 0; // is executed in the same as TaskQueue thread
	virtual ~Task() = default;

	TaskId id() const {
		return static_cast<TaskId>(const_cast<Task*>(this));
	}

};

class TaskQueueWorker;
class TaskQueue : public QObject {
	Q_OBJECT

public:
	explicit TaskQueue(crl::time stopTimeoutMs = 0); // <= 0 - never stop worker

	TaskId addTask(std::unique_ptr<Task> &&task);
	void addTasks(std::vector<std::unique_ptr<Task>> &&tasks);
	void cancelTask(TaskId id); // this task finish() won't be called

	~TaskQueue();

Q_SIGNALS:
	void taskAdded();

public Q_SLOTS:
	void onTaskProcessed();
	void stop();

private:
	friend class TaskQueueWorker;

	void wakeThread();

	std::deque<std::unique_ptr<Task>> _tasksToProcess;
	std::deque<std::unique_ptr<Task>> _tasksToFinish;
	TaskId _taskInProcessId = TaskId();
	QMutex _tasksToProcessMutex, _tasksToFinishMutex;
	QThread *_thread = nullptr;
	TaskQueueWorker *_worker = nullptr;
	QTimer *_stopTimer = nullptr;

};

class TaskQueueWorker : public QObject {
	Q_OBJECT

public:
	TaskQueueWorker(TaskQueue *queue) : _queue(queue) {
	}

Q_SIGNALS:
	void taskProcessed();

public Q_SLOTS:
	void onTaskAdded();

private:
	TaskQueue *_queue;
	bool _inTaskAdded = false;

};

struct SendingAlbum {
	struct Item {
		explicit Item(TaskId taskId) : taskId(taskId) {
		}

		TaskId taskId;
		uint64 randomId = 0;
		FullMsgId msgId;
		std::optional<MTPInputSingleMedia> media;
	};

	SendingAlbum();

	void fillMedia(
		not_null<HistoryItem*> item,
		const MTPInputMedia &media,
		uint64 randomId);
	void refreshMediaCaption(not_null<HistoryItem*> item);
	void removeItem(not_null<HistoryItem*> item);

	uint64 groupId = 0;
	std::vector<Item> items;
	Api::SendOptions options;

};

struct FileLoadTo {
	FileLoadTo(
		const PeerId &peer,
		Api::SendOptions options,
		MsgId replyTo,
		MsgId replaceMediaOf)
	: peer(peer)
	, options(options)
	, replyTo(replyTo)
	, replaceMediaOf(replaceMediaOf) {
	}
	PeerId peer;
	Api::SendOptions options;
	MsgId replyTo;
	MsgId replaceMediaOf;
};

struct FileLoadResult {
	FileLoadResult(
		TaskId taskId,
		uint64 id,
		const FileLoadTo &to,
		const TextWithTags &caption,
		std::shared_ptr<SendingAlbum> album);

	TaskId taskId;
	uint64 id;
	FileLoadTo to;
	std::shared_ptr<SendingAlbum> album;
	SendMediaType type = SendMediaType::File;
	QString filepath;
	QByteArray content;

	QString filename;
	QString filemime;
	int32 filesize = 0;
	UploadFileParts fileparts;
	QByteArray filemd5;
	int32 partssize;

	uint64 thumbId = 0; // id is always file-id of media, thumbId is file-id of thumb ( == id for photos)
	QString thumbname;
	UploadFileParts thumbparts;
	QByteArray thumbbytes;
	QByteArray thumbmd5;
	QImage thumb;

	QImage goodThumbnail;
	QByteArray goodThumbnailBytes;

	MTPPhoto photo;
	MTPDocument document;

	PreparedPhotoThumbs photoThumbs;
	TextWithTags caption;

	void setFileData(const QByteArray &filedata);
	void setThumbData(const QByteArray &thumbdata);

};

class FileLoadTask final : public Task {
public:
	static std::unique_ptr<Ui::PreparedFileInformation> ReadMediaInformation(
		const QString &filepath,
		const QByteArray &content,
		const QString &filemime);
	static bool FillImageInformation(
		QImage &&image,
		bool animated,
		std::unique_ptr<Ui::PreparedFileInformation> &result);

	FileLoadTask(
		not_null<Main::Session*> session,
		const QString &filepath,
		const QByteArray &content,
		std::unique_ptr<Ui::PreparedFileInformation> information,
		SendMediaType type,
		const FileLoadTo &to,
		const TextWithTags &caption,
		std::shared_ptr<SendingAlbum> album = nullptr);
	FileLoadTask(
		not_null<Main::Session*> session,
		const QByteArray &voice,
		int32 duration,
		const VoiceWaveform &waveform,
		const FileLoadTo &to,
		const TextWithTags &caption);
	~FileLoadTask();

	uint64 fileid() const {
		return _id;
	}

	struct Args {
		bool generateGoodThumbnail = true;
	};
	void process(Args &&args);

	void process() override {
		process({});
	}
	void finish() override;

	FileLoadResult *peekResult() const;

private:
	static bool CheckForSong(
		const QString &filepath,
		const QByteArray &content,
		std::unique_ptr<Ui::PreparedFileInformation> &result);
	static bool CheckForVideo(
		const QString &filepath,
		const QByteArray &content,
		std::unique_ptr<Ui::PreparedFileInformation> &result);
	static bool CheckForImage(
		const QString &filepath,
		const QByteArray &content,
		std::unique_ptr<Ui::PreparedFileInformation> &result);

	template <typename Mimes, typename Extensions>
	static bool CheckMimeOrExtensions(const QString &filepath, const QString &filemime, Mimes &mimes, Extensions &extensions);

	std::unique_ptr<Ui::PreparedFileInformation> readMediaInformation(const QString &filemime) const;
	void removeFromAlbum();

	uint64 _id = 0;
	base::weak_ptr<Main::Session> _session;
	MTP::DcId _dcId = 0;
	FileLoadTo _to;
	const std::shared_ptr<SendingAlbum> _album;
	QString _filepath;
	QByteArray _content;
	std::unique_ptr<Ui::PreparedFileInformation> _information;
	int32 _duration = 0;
	VoiceWaveform _waveform;
	SendMediaType _type;
	TextWithTags _caption;

	std::shared_ptr<FileLoadResult> _result;

};
