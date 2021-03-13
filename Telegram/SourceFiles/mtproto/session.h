/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "mtproto/mtproto_response.h"
#include "mtproto/mtproto_proxy_data.h"
#include "mtproto/details/mtproto_serialized_request.h"

#include <QtCore/QTimer>

namespace MTP {

class Instance;
class AuthKey;
using AuthKeyPtr = std::shared_ptr<AuthKey>;
enum class DcType;

namespace details {

class Dcenter;
class SessionPrivate;

enum class TemporaryKeyType;
enum class CreatingKeyType;

struct SessionOptions {
	SessionOptions() = default;
	SessionOptions(
		const QString &systemLangCode,
		const QString &cloudLangCode,
		const QString &langPackName,
		const ProxyData &proxy,
		bool useIPv4,
		bool useIPv6,
		bool useHttp,
		bool useTcp);

	QString systemLangCode;
	QString cloudLangCode;
	QString langPackName;
	ProxyData proxy;
	bool useIPv4 = true;
	bool useIPv6 = true;
	bool useHttp = true;
	bool useTcp = true;

};

class Session;
class SessionData final {
public:
	explicit SessionData(not_null<Session*> creator) : _owner(creator) {
	}

	void notifyConnectionInited(const SessionOptions &options);
	void setOptions(SessionOptions options) {
		QWriteLocker locker(&_optionsLock);
		_options = options;
	}
	[[nodiscard]] SessionOptions options() const {
		QReadLocker locker(&_optionsLock);
		return _options;
	}

	not_null<QReadWriteLock*> toSendMutex() {
		return &_toSendLock;
	}
	not_null<QReadWriteLock*> haveSentMutex() {
		return &_haveSentLock;
	}
	not_null<QReadWriteLock*> haveReceivedMutex() {
		return &_haveReceivedLock;
	}

	base::flat_map<mtpRequestId, SerializedRequest> &toSendMap() {
		return _toSend;
	}
	base::flat_map<mtpMsgId, SerializedRequest> &haveSentMap() {
		return _haveSent;
	}
	std::vector<Response> &haveReceivedMessages() {
		return _receivedMessages;
	}

	// SessionPrivate -> Session interface.
	void queueTryToReceive();
	void queueNeedToResumeAndSend();
	void queueConnectionStateChange(int newState);
	void queueResetDone();
	void queueSendAnything(crl::time msCanWait = 0);

	[[nodiscard]] bool connectionInited() const;
	[[nodiscard]] AuthKeyPtr getPersistentKey() const;
	[[nodiscard]] AuthKeyPtr getTemporaryKey(TemporaryKeyType type) const;
	[[nodiscard]] CreatingKeyType acquireKeyCreation(DcType type);
	[[nodiscard]] bool releaseKeyCreationOnDone(
		const AuthKeyPtr &temporaryKey,
		const AuthKeyPtr &persistentKeyUsedForBind);
	[[nodiscard]] bool releaseCdnKeyCreationOnDone(
		const AuthKeyPtr &temporaryKey);
	void releaseKeyCreationOnFail();
	void destroyTemporaryKey(uint64 keyId);

	void detach();

private:
	template <typename Callback>
	void withSession(Callback &&callback);

	Session *_owner = nullptr;
	mutable QMutex _ownerMutex;

	SessionOptions _options;
	mutable QReadWriteLock _optionsLock;

	base::flat_map<mtpRequestId, SerializedRequest> _toSend; // map of request_id -> request, that is waiting to be sent
	QReadWriteLock _toSendLock;

	base::flat_map<mtpMsgId, SerializedRequest> _haveSent; // map of msg_id -> request, that was sent
	QReadWriteLock _haveSentLock;

	std::vector<Response> _receivedMessages; // list of responses / updates that should be processed in the main thread
	QReadWriteLock _haveReceivedLock;

};

class Session final : public QObject {
public:
	// Main thread.
	Session(
		not_null<Instance*> instance,
		not_null<QThread*> thread,
		ShiftedDcId shiftedDcId,
		not_null<Dcenter*> dc);
	~Session();

	void start();
	void reInitConnection();

	void restart();
	void refreshOptions();
	void stop();
	void kill();

	void unpaused();

	// Thread-safe.
	[[nodiscard]] ShiftedDcId getDcWithShift() const;
	[[nodiscard]] AuthKeyPtr getPersistentKey() const;
	[[nodiscard]] AuthKeyPtr getTemporaryKey(TemporaryKeyType type) const;
	[[nodiscard]] bool connectionInited() const;
	void sendPrepared(
		const SerializedRequest &request,
		crl::time msCanWait = 0);

	// SessionPrivate thread.
	[[nodiscard]] CreatingKeyType acquireKeyCreation(DcType type);
	[[nodiscard]] bool releaseKeyCreationOnDone(
		const AuthKeyPtr &temporaryKey,
		const AuthKeyPtr &persistentKeyUsedForBind);
	[[nodiscard]] bool releaseCdnKeyCreationOnDone(const AuthKeyPtr &temporaryKey);
	void releaseKeyCreationOnFail();
	void destroyTemporaryKey(uint64 keyId);

	void notifyDcConnectionInited();

	void ping();
	void cancel(mtpRequestId requestId, mtpMsgId msgId);
	int requestState(mtpRequestId requestId) const;
	int getState() const;
	QString transport() const;

	void tryToReceive();
	void needToResumeAndSend();
	void connectionStateChange(int newState);
	void resetDone();
	void sendAnything(crl::time msCanWait = 0);

private:
	void watchDcKeyChanges();
	void watchDcOptionsChanges();

	void killConnection();

	[[nodiscard]] bool releaseGenericKeyCreationOnDone(
		const AuthKeyPtr &temporaryKey,
		const AuthKeyPtr &persistentKeyUsedForBind);

	const not_null<Instance*> _instance;
	const ShiftedDcId _shiftedDcId = 0;
	const not_null<Dcenter*> _dc;
	const std::shared_ptr<SessionData> _data;
	const not_null<QThread*> _thread;

	SessionPrivate *_private = nullptr;

	bool _killed = false;
	bool _needToReceive = false;

	AuthKeyPtr _dcKeyForCheck;
	CreatingKeyType _myKeyCreation = CreatingKeyType();

	crl::time _msSendCall = 0;
	crl::time _msWait = 0;

	bool _ping = false;

	base::Timer _timeouter;
	base::Timer _sender;

	rpl::lifetime _lifetime;

};

} // namespace details
} // namespace MTP
