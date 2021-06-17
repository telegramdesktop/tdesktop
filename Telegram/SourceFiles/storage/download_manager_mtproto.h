/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_file_origin.h"
#include "base/timer.h"
#include "base/weak_ptr.h"

class ApiWrap;

namespace MTP {
class Error;
} // namespace MTP

namespace Storage {

// Different part sizes are not supported for now :(
// Because we start downloading with some part size
// and then we get a CDN-redirect where we support only
// fixed part size download for hash checking.
constexpr auto kDownloadPartSize = 128 * 1024;

class DownloadMtprotoTask;

class DownloadManagerMtproto final : public base::has_weak_ptr {
public:
	using Task = DownloadMtprotoTask;

	explicit DownloadManagerMtproto(not_null<ApiWrap*> api);
	~DownloadManagerMtproto();

	[[nodiscard]] ApiWrap &api() const {
		return *_api;
	}

	void enqueue(not_null<Task*> task, int priority);
	void remove(not_null<Task*> task);

	void notifyTaskFinished() {
		_taskFinished.fire({});
	}
	[[nodiscard]] rpl::producer<> taskFinished() const {
		return _taskFinished.events();
	}

	int changeRequestedAmount(MTP::DcId dcId, int index, int delta);
	void requestSucceeded(
		MTP::DcId dcId,
		int index,
		int amountAtRequestStart,
		crl::time timeAtRequestStart);
	void checkSendNextAfterSuccess(MTP::DcId dcId);
	[[nodiscard]] int chooseSessionIndex(MTP::DcId dcId) const;

private:
	class Queue final {
	public:
		void enqueue(not_null<Task*> task, int priority);
		void remove(not_null<Task*> task);
		void resetGeneration();
		[[nodiscard]] bool empty() const;
		[[nodiscard]] Task *nextTask(bool onlyHighestPriority) const;
		void removeSession(int index);

	private:
		struct Enqueued {
			not_null<Task*> task;
			int priority = 0;
		};
		std::vector<Enqueued> _tasks;

	};
	struct DcSessionBalanceData {
		DcSessionBalanceData();

		int requested = 0;
		int successes = 0; // Since last timeout in this dc in any session.
		int maxWaitedAmount = 0;
	};
	struct DcBalanceData {
		DcBalanceData();

		std::vector<DcSessionBalanceData> sessions;
		crl::time lastSessionRemove = 0;
		int sessionRemoveIndex = 0;
		int sessionRemoveTimes = 0;
		int timeouts = 0; // Since all sessions had successes >= required.
		int totalRequested = 0;
	};

	void checkSendNext();
	void checkSendNext(MTP::DcId dcId, Queue &queue);
	bool trySendNextPart(MTP::DcId dcId, Queue &queue);

	void killSessionsSchedule(MTP::DcId dcId);
	void killSessionsCancel(MTP::DcId dcId);
	void killSessions();
	void killSessions(MTP::DcId dcId);

	void resetGeneration();
	void sessionTimedOut(MTP::DcId dcId, int index);
	void removeSession(MTP::DcId dcId);

	const not_null<ApiWrap*> _api;

	rpl::event_stream<> _taskFinished;

	base::flat_map<MTP::DcId, DcBalanceData> _balanceData;
	base::Timer _resetGenerationTimer;

	base::flat_map<MTP::DcId, crl::time> _killSessionsWhen;
	base::Timer _killSessionsTimer;

	base::flat_map<MTP::DcId, Queue> _queues;
	rpl::lifetime _lifetime;

};

class DownloadMtprotoTask : public base::has_weak_ptr {
public:
	struct Location {
		std::variant<
			StorageFileLocation,
			WebFileLocation,
			GeoPointLocation> data;
	};

	DownloadMtprotoTask(
		not_null<DownloadManagerMtproto*> owner,
		const StorageFileLocation &location,
		Data::FileOrigin origin);
	DownloadMtprotoTask(
		not_null<DownloadManagerMtproto*> owner,
		MTP::DcId dcId,
		const Location &location);
	virtual ~DownloadMtprotoTask();

	[[nodiscard]] MTP::DcId dcId() const;
	[[nodiscard]] Data::FileOrigin fileOrigin() const;
	[[nodiscard]] uint64 objectId() const;
	[[nodiscard]] const Location &location() const;

	[[nodiscard]] virtual bool readyToRequest() const = 0;
	void loadPart(int sessionIndex);
	void removeSession(int sessionIndex);

	void refreshFileReferenceFrom(
		const Data::UpdatedFileReferences &updates,
		int requestId,
		const QByteArray &current);

protected:
	[[nodiscard]] bool haveSentRequests() const;
	[[nodiscard]] bool haveSentRequestForOffset(int offset) const;
	void cancelAllRequests();
	void cancelRequestForOffset(int offset);

	void addToQueue(int priority = 0);
	void removeFromQueue();

	[[nodiscard]] ApiWrap &api() const {
		return _owner->api();
	}

private:
	struct RequestData {
		int offset = 0;
		mutable int sessionIndex = 0;
		int requestedInSession = 0;
		crl::time sent = 0;

		inline bool operator<(const RequestData &other) const {
			return offset < other.offset;
		}
	};
	struct CdnFileHash {
		CdnFileHash(int limit, QByteArray hash) : limit(limit), hash(hash) {
		}
		int limit = 0;
		QByteArray hash;
	};
	enum class CheckCdnHashResult {
		NoHash,
		Invalid,
		Good,
	};
	enum class FinishRequestReason {
		Success,
		Redirect,
		Cancel,
	};

	// Called only if readyToRequest() == true.
	[[nodiscard]] virtual int takeNextRequestOffset() = 0;
	virtual bool feedPart(int offset, const QByteArray &bytes) = 0;
	virtual bool setWebFileSizeHook(int size);
	virtual void cancelOnFail() = 0;

	void cancelRequest(mtpRequestId requestId);
	void makeRequest(const RequestData &requestData);
	void normalPartLoaded(
		const MTPupload_File &result,
		mtpRequestId requestId);
	void webPartLoaded(
		const MTPupload_WebFile &result,
		mtpRequestId requestId);
	void cdnPartLoaded(
		const MTPupload_CdnFile &result,
		mtpRequestId requestId);
	void reuploadDone(
		const MTPVector<MTPFileHash> &result,
		mtpRequestId requestId);
	void requestMoreCdnFileHashes();
	void getCdnFileHashesDone(
		const MTPVector<MTPFileHash> &result,
		mtpRequestId requestId);

	void partLoaded(int offset, const QByteArray &bytes);

	bool partFailed(const MTP::Error &error, mtpRequestId requestId);
	bool normalPartFailed(
		QByteArray fileReference,
		const MTP::Error &error,
		mtpRequestId requestId);
	bool cdnPartFailed(const MTP::Error &error, mtpRequestId requestId);

	[[nodiscard]] mtpRequestId sendRequest(const RequestData &requestData);
	void placeSentRequest(
		mtpRequestId requestId,
		const RequestData &requestData);
	[[nodiscard]] RequestData finishSentRequest(
		mtpRequestId requestId,
		FinishRequestReason reason);
	void switchToCDN(
		const RequestData &requestData,
		const MTPDupload_fileCdnRedirect &redirect);
	void addCdnHashes(const QVector<MTPFileHash> &hashes);
	void changeCDNParams(
		const RequestData &requestData,
		MTP::DcId dcId,
		const QByteArray &token,
		const QByteArray &encryptionKey,
		const QByteArray &encryptionIV,
		const QVector<MTPFileHash> &hashes);

	[[nodiscard]] CheckCdnHashResult checkCdnFileHash(
		int offset,
		bytes::const_span buffer);

	const not_null<DownloadManagerMtproto*> _owner;
	const MTP::DcId _dcId = 0;

	// _location can be changed with an updated file_reference.
	Location _location;
	const Data::FileOrigin _origin;

	base::flat_map<mtpRequestId, RequestData> _sentRequests;
	base::flat_map<int, mtpRequestId> _requestByOffset;

	MTP::DcId _cdnDcId = 0;
	QByteArray _cdnToken;
	QByteArray _cdnEncryptionKey;
	QByteArray _cdnEncryptionIV;
	base::flat_map<int, CdnFileHash> _cdnFileHashes;
	base::flat_map<RequestData, QByteArray> _cdnUncheckedParts;
	mtpRequestId _cdnHashesRequestId = 0;

};

} // namespace Storage
