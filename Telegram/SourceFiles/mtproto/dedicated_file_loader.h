/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace MTP {

class Instance;

class WeakInstance : private QObject, private base::Subscriber {
public:
	WeakInstance(QPointer<Instance> instance);

	template <typename T>
	void send(
		const T &request,
		Fn<void(const typename T::ResponseType &result)> done,
		Fn<void(const RPCError &error)> fail,
		ShiftedDcId dcId = 0);

	bool valid() const;
	QPointer<Instance> instance() const;

	~WeakInstance();

private:
	void die();
	bool removeRequest(mtpRequestId requestId);
	void reportUnavailable(Fn<void(const RPCError &error)> callback);

	QPointer<Instance> _instance;
	std::map<mtpRequestId, Fn<void(const RPCError &)>> _requests;

};

class AbstractDedicatedLoader : public base::has_weak_ptr {
public:
	AbstractDedicatedLoader(const QString &filepath, int chunkSize);

	static constexpr auto kChunkSize = 128 * 1024;
	static constexpr auto kMaxFileSize = 256 * 1024 * 1024;

	struct Progress {
		int64 already;
		int64 size;

		inline bool operator<(const Progress &other) const {
			return (already < other.already)
				|| (already == other.already && size < other.size);
		}
		inline bool operator==(const Progress &other) const {
			return (already == other.already) && (size == other.size);
		}
	};

	void start();
	void wipeFolder();
	void wipeOutput();

	int alreadySize() const;
	int totalSize() const;

	rpl::producer<Progress> progress() const;
	rpl::producer<QString> ready() const;
	rpl::producer<> failed() const;

	rpl::lifetime &lifetime();

	virtual ~AbstractDedicatedLoader() = default;

protected:
	void threadSafeFailed();

	// Single threaded.
	void writeChunk(bytes::const_span data, int totalSize);

private:
	virtual void startLoading() = 0;

	bool validateOutput();
	void threadSafeProgress(Progress progress);
	void threadSafeReady();

	QString _filepath;
	int _chunkSize = 0;

	QFile _output;
	int _alreadySize = 0;
	int _totalSize = 0;
	mutable QMutex _sizesMutex;
	rpl::event_stream<Progress> _progress;
	rpl::event_stream<QString> _ready;
	rpl::event_stream<> _failed;

	rpl::lifetime _lifetime;

};

class DedicatedLoader : public AbstractDedicatedLoader {
public:
	struct Location {
		QString username;
		int32 postId = 0;
	};
	struct File {
		QString name;
		int32 size = 0;
		DcId dcId = 0;
		MTPInputFileLocation location;
	};

	DedicatedLoader(
		QPointer<Instance> instance,
		const QString &folder,
		const File &file);

private:
	struct Request {
		int offset = 0;
		QByteArray bytes;
	};
	void startLoading() override;
	void sendRequest();
	void gotPart(int offset, const MTPupload_File &result);
	Fn<void(const RPCError &)> failHandler();

	static constexpr auto kRequestsCount = 2;
	static constexpr auto kNextRequestDelay = TimeMs(20);

	std::deque<Request> _requests;
	int32 _size = 0;
	int _offset = 0;
	DcId _dcId = 0;
	MTPInputFileLocation _location;
	WeakInstance _mtp;

};

void ResolveChannel(
	not_null<MTP::WeakInstance*> mtp,
	const QString &username,
	Fn<void(const MTPInputChannel &channel)> done,
	Fn<void()> fail);

std::optional<MTPMessage> GetMessagesElement(
	const MTPmessages_Messages &list);

void StartDedicatedLoader(
	not_null<MTP::WeakInstance*> mtp,
	const DedicatedLoader::Location &location,
	const QString &folder,
	Fn<void(std::unique_ptr<DedicatedLoader>)> ready);

template <typename T>
void WeakInstance::send(
		const T &request,
		Fn<void(const typename T::ResponseType &result)> done,
		Fn<void(const RPCError &error)> fail,
		MTP::ShiftedDcId dcId) {
	using Response = typename T::ResponseType;
	if (!valid()) {
		reportUnavailable(fail);
		return;
	}
	const auto onDone = crl::guard((QObject*)this, [=](
			const Response &result,
			mtpRequestId requestId) {
		if (removeRequest(requestId)) {
			done(result);
		}
	});
	const auto onFail = crl::guard((QObject*)this, [=](
			const RPCError &error,
			mtpRequestId requestId) {
		if (MTP::isDefaultHandledError(error)) {
			return false;
		}
		if (removeRequest(requestId)) {
			fail(error);
		}
		return true;
	});
	const auto requestId = _instance->send(
		request,
		rpcDone(onDone),
		rpcFail(onFail),
		dcId);
	_requests.emplace(requestId, fail);
}

} // namespace MTP
