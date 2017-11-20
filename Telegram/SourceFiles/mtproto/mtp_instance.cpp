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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "mtproto/mtp_instance.h"

#include "mtproto/dc_options.h"
#include "mtproto/dcenter.h"
#include "mtproto/config_loader.h"
#include "mtproto/connection.h"
#include "mtproto/sender.h"
#include "mtproto/rsa_public_key.h"
#include "storage/localstorage.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "messenger.h"
#include "lang/lang_instance.h"
#include "lang/lang_cloud_manager.h"
#include "base/timer.h"

namespace MTP {

class Instance::Private : private Sender {
public:
	Private(not_null<Instance*> instance, not_null<DcOptions*> options, Instance::Mode mode);

	void start(Config &&config);

	void suggestMainDcId(DcId mainDcId);
	void setMainDcId(DcId mainDcId);
	DcId mainDcId() const;

	void setKeyForWrite(DcId dcId, const AuthKeyPtr &key);
	AuthKeysList getKeysForWrite() const;
	void addKeysForDestroy(AuthKeysList &&keys);

	not_null<DcOptions*> dcOptions();

	void requestConfig();
	void requestCDNConfig();

	void restart();
	void restart(ShiftedDcId shiftedDcId);
	int32 dcstate(ShiftedDcId shiftedDcId = 0);
	QString dctransport(ShiftedDcId shiftedDcId = 0);
	void ping();
	void cancel(mtpRequestId requestId);
	int32 state(mtpRequestId requestId); // < 0 means waiting for such count of ms
	void killSession(ShiftedDcId shiftedDcId);
	void killSession(std::unique_ptr<internal::Session> session);
	void stopSession(ShiftedDcId shiftedDcId);
	void reInitConnection(DcId dcId);
	void logout(RPCDoneHandlerPtr onDone, RPCFailHandlerPtr onFail);

	std::shared_ptr<internal::Dcenter> getDcById(ShiftedDcId shiftedDcId);
	void unpaused();

	void queueQuittingConnection(std::unique_ptr<internal::Connection> connection);
	void connectionFinished(internal::Connection *connection);

	void registerRequest(mtpRequestId requestId, int32 dcWithShift);
	void unregisterRequest(mtpRequestId requestId);
	mtpRequestId storeRequest(mtpRequest &request, const RPCResponseHandler &parser);
	mtpRequest getRequest(mtpRequestId requestId);
	void clearCallbacks(mtpRequestId requestId, int32 errorCode = RPCError::NoError); // 0 - do not toggle onError callback
	void clearCallbacksDelayed(const RPCCallbackClears &requestIds);
	void performDelayedClear();
	void execCallback(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end);
	bool hasCallbacks(mtpRequestId requestId);
	void globalCallback(const mtpPrime *from, const mtpPrime *end);

	void onStateChange(ShiftedDcId dcWithShift, int32 state);
	void onSessionReset(ShiftedDcId dcWithShift);

	// return true if need to clean request data
	bool rpcErrorOccured(mtpRequestId requestId, const RPCFailHandlerPtr &onFail, const RPCError &err);
	inline bool rpcErrorOccured(mtpRequestId requestId, const RPCResponseHandler &handler, const RPCError &err) {
		return rpcErrorOccured(requestId, handler.onFail, err);
	}

	void setUpdatesHandler(RPCDoneHandlerPtr onDone);
	void setGlobalFailHandler(RPCFailHandlerPtr onFail);
	void setStateChangedHandler(base::lambda<void(ShiftedDcId shiftedDcId, int32 state)> handler);
	void setSessionResetHandler(base::lambda<void(ShiftedDcId shiftedDcId)> handler);
	void clearGlobalHandlers();

	internal::Session *getSession(ShiftedDcId shiftedDcId);

	bool isNormal() const {
		return (_mode == Instance::Mode::Normal);
	}
	bool isKeysDestroyer() const {
		return (_mode == Instance::Mode::KeysDestroyer);
	}
	bool isSpecialConfigRequester() const {
		return (_mode == Instance::Mode::SpecialConfigRequester);
	}

	void scheduleKeyDestroy(ShiftedDcId shiftedDcId);
	void performKeyDestroy(ShiftedDcId shiftedDcId);
	void completedKeyDestroy(ShiftedDcId shiftedDcId);

	void clearKilledSessions();
	void prepareToDestroy();

private:
	bool hasAuthorization();
	void importDone(const MTPauth_Authorization &result, mtpRequestId requestId);
	bool importFail(const RPCError &error, mtpRequestId requestId);
	void exportDone(const MTPauth_ExportedAuthorization &result, mtpRequestId requestId);
	bool exportFail(const RPCError &error, mtpRequestId requestId);
	bool onErrorDefault(mtpRequestId requestId, const RPCError &error);

	bool logoutGuestDone(mtpRequestId requestId);

	void configLoadDone(const MTPConfig &result);
	bool configLoadFail(const RPCError &error);

	void cdnConfigLoadDone(const MTPCdnConfig &result);
	bool cdnConfigLoadFail(const RPCError &error);

	void checkDelayedRequests();

	not_null<Instance*> _instance;
	not_null<DcOptions*> _dcOptions;
	Instance::Mode _mode = Instance::Mode::Normal;

	DcId _mainDcId = Config::kDefaultMainDc;
	bool _mainDcIdForced = false;
	std::map<DcId, std::shared_ptr<internal::Dcenter>> _dcenters;

	internal::Session *_mainSession = nullptr;
	std::map<ShiftedDcId, std::unique_ptr<internal::Session>> _sessions;
	std::vector<std::unique_ptr<internal::Session>> _killedSessions; // delayed delete

	base::set_of_unique_ptr<internal::Connection> _quittingConnections;

	std::unique_ptr<internal::ConfigLoader> _configLoader;
	mtpRequestId _cdnConfigLoadRequestId = 0;

	std::map<DcId, AuthKeyPtr> _keysForWrite;
	mutable QReadWriteLock _keysForWriteLock;

	std::map<ShiftedDcId, mtpRequestId> _logoutGuestRequestIds;

	// holds dcWithShift for request to this dc or -dc for request to main dc
	std::map<mtpRequestId, ShiftedDcId> _requestsByDc;
	QMutex _requestByDcLock;

	// holds target dcWithShift for auth export request
	std::map<mtpRequestId, ShiftedDcId> _authExportRequests;

	std::map<mtpRequestId, RPCResponseHandler> _parserMap;
	QMutex _parserMapLock;

	std::map<mtpRequestId, mtpRequest> _requestMap;
	QReadWriteLock _requestMapLock;

	std::deque<std::pair<mtpRequestId, TimeMs>> _delayedRequests;

	std::map<mtpRequestId, int> _requestsDelays;

	std::set<mtpRequestId> _badGuestDcRequests;

	std::map<DcId, std::vector<mtpRequestId>> _authWaiters;

	QMutex _toClearLock;
	RPCCallbackClears _toClear;

	RPCResponseHandler _globalHandler;
	base::lambda<void(ShiftedDcId shiftedDcId, int32 state)> _stateChangedHandler;
	base::lambda<void(ShiftedDcId shiftedDcId)> _sessionResetHandler;

	base::Timer _checkDelayedTimer;

	// Debug flag to find out how we end up crashing.
	bool MustNotCreateSessions = false;

};

Instance::Private::Private(not_null<Instance*> instance, not_null<DcOptions*> options, Instance::Mode mode) : Sender()
, _instance(instance)
, _dcOptions(options)
, _mode(mode) {
}

void Instance::Private::start(Config &&config) {
	if (isKeysDestroyer()) {
		_instance->connect(_instance, SIGNAL(keyDestroyed(qint32)), _instance, SLOT(onKeyDestroyed(qint32)), Qt::QueuedConnection);
	} else if (isNormal()) {
		unixtimeInit();
	}

	for (auto &key : config.keys) {
		auto dcId = key->dcId();
		auto shiftedDcId = dcId;
		if (isKeysDestroyer()) {
			shiftedDcId = MTP::destroyKeyNextDcId(shiftedDcId);

			// There could be several keys for one dc if we're destroying them.
			// Place them all in separate shiftedDcId so that they won't conflict.
			while (_keysForWrite.find(shiftedDcId) != _keysForWrite.cend()) {
				shiftedDcId = MTP::destroyKeyNextDcId(shiftedDcId);
			}
		}
		_keysForWrite[shiftedDcId] = key;

		auto dc = std::make_shared<internal::Dcenter>(_instance, dcId, std::move(key));
		_dcenters.emplace(shiftedDcId, std::move(dc));
	}

	if (config.mainDcId != Config::kNotSetMainDc) {
		_mainDcId = config.mainDcId;
		_mainDcIdForced = true;
	}

	if (isKeysDestroyer()) {
		for (auto &dc : _dcenters) {
			Assert(!MustNotCreateSessions);
			auto shiftedDcId = dc.first;
			auto session = std::make_unique<internal::Session>(_instance, shiftedDcId);
			auto it = _sessions.emplace(shiftedDcId, std::move(session)).first;
			it->second->start();
		}
	} else if (_mainDcId != Config::kNoneMainDc) {
		Assert(!MustNotCreateSessions);
		auto main = std::make_unique<internal::Session>(_instance, _mainDcId);
		_mainSession = main.get();
		_sessions.emplace(_mainDcId, std::move(main));
		_mainSession->start();
	}

	_checkDelayedTimer.setCallback([this] { checkDelayedRequests(); });

	Assert((_mainDcId == Config::kNoneMainDc) == isKeysDestroyer());
	if (!isKeysDestroyer()) {
		requestConfig();
	}
}

void Instance::Private::suggestMainDcId(DcId mainDcId) {
	if (_mainDcIdForced) return;
	setMainDcId(mainDcId);
}

void Instance::Private::setMainDcId(DcId mainDcId) {
	if (!_mainSession) {
		LOG(("MTP Error: attempting to change mainDcId in an MTP instance without main session."));
		return;
	}

	_mainDcIdForced = true;
	auto oldMainDcId = _mainSession->getDcWithShift();
	_mainDcId = mainDcId;
	if (oldMainDcId != _mainDcId) {
		killSession(oldMainDcId);
	}
	Local::writeMtpData();
}

DcId Instance::Private::mainDcId() const {
	Expects(_mainDcId != Config::kNoneMainDc);
	return _mainDcId;
}

void Instance::Private::requestConfig() {
	if (_configLoader) {
		return;
	}
	_configLoader = std::make_unique<internal::ConfigLoader>(_instance, rpcDone([this](const MTPConfig &result) {
		configLoadDone(result);
	}), rpcFail([this](const RPCError &error) {
		return configLoadFail(error);
	}));
	_configLoader->load();
}

void Instance::Private::requestCDNConfig() {
	if (_cdnConfigLoadRequestId || _mainDcId == Config::kNoneMainDc) {
		return;
	}
	_cdnConfigLoadRequestId = request(MTPhelp_GetCdnConfig()).done([this](const MTPCdnConfig &result) {
		_cdnConfigLoadRequestId = 0;

		Expects(result.type() == mtpc_cdnConfig);
		dcOptions()->setCDNConfig(result.c_cdnConfig());

		Local::writeSettings();

		emit _instance->cdnConfigLoaded();
	}).send();
}

void Instance::Private::restart() {
	for (auto &session : _sessions) {
		session.second->restart();
	}
}

void Instance::Private::restart(ShiftedDcId shiftedDcId) {
	auto dcId = bareDcId(shiftedDcId);
	for (auto &session : _sessions) {
		if (bareDcId(session.second->getDcWithShift()) == dcId) {
			session.second->restart();
		}
	}
}

int32 Instance::Private::dcstate(ShiftedDcId shiftedDcId) {
	if (!shiftedDcId) {
		Assert(_mainSession != nullptr);
		return _mainSession->getState();
	}

	if (!bareDcId(shiftedDcId)) {
		Assert(_mainSession != nullptr);
		shiftedDcId += bareDcId(_mainSession->getDcWithShift());
	}

	auto it = _sessions.find(shiftedDcId);
	if (it != _sessions.cend()) {
		return it->second->getState();
	}

	return DisconnectedState;
}

QString Instance::Private::dctransport(ShiftedDcId shiftedDcId) {
	if (!shiftedDcId) {
		Assert(_mainSession != nullptr);
		return _mainSession->transport();
	}
	if (!bareDcId(shiftedDcId)) {
		Assert(_mainSession != nullptr);
		shiftedDcId += bareDcId(_mainSession->getDcWithShift());
	}

	auto it = _sessions.find(shiftedDcId);
	if (it != _sessions.cend()) {
		return it->second->transport();
	}

	return QString();
}

void Instance::Private::ping() {
	if (auto session = getSession(0)) {
		session->ping();
	}
}

void Instance::Private::cancel(mtpRequestId requestId) {
	if (!requestId) return;

	mtpMsgId msgId = 0;
	_requestsDelays.erase(requestId);
	{
		QWriteLocker locker(&_requestMapLock);
		auto it = _requestMap.find(requestId);
		if (it != _requestMap.end()) {
			msgId = *(mtpMsgId*)(it->second->constData() + 4);
			_requestMap.erase(it);
		}
	}
	{
		QMutexLocker locker(&_requestByDcLock);
		auto it = _requestsByDc.find(requestId);
		if (it != _requestsByDc.end()) {
			if (auto session = getSession(qAbs(it->second))) {
				session->cancel(requestId, msgId);
			}
			_requestsByDc.erase(it);
		}
	}
	clearCallbacks(requestId);
}

int32 Instance::Private::state(mtpRequestId requestId) { // < 0 means waiting for such count of ms
	if (requestId > 0) {
		QMutexLocker locker(&_requestByDcLock);
		auto i = _requestsByDc.find(requestId);
		if (i != _requestsByDc.end()) {
			if (auto session = getSession(qAbs(i->second))) {
				return session->requestState(requestId);
			}
			return MTP::RequestConnecting;
		}
		return MTP::RequestSent;
	}
	if (auto session = getSession(-requestId)) {
		return session->requestState(0);
	}
	return MTP::RequestConnecting;
}

void Instance::Private::killSession(ShiftedDcId shiftedDcId) {
	auto checkIfMainAndKill = [this](ShiftedDcId shiftedDcId) {
		auto it = _sessions.find(shiftedDcId);
		if (it != _sessions.cend()) {
			_killedSessions.push_back(std::move(it->second));
			_sessions.erase(it);
			_killedSessions.back()->kill();
			return (_killedSessions.back().get() == _mainSession);
		}
		return false;
	};
	if (checkIfMainAndKill(shiftedDcId)) {
		checkIfMainAndKill(_mainDcId);

		Assert(!MustNotCreateSessions);
		auto main = std::make_unique<internal::Session>(_instance, _mainDcId);
		_mainSession = main.get();
		_sessions.emplace(_mainDcId, std::move(main));
		_mainSession->start();
	}
	InvokeQueued(_instance, [this] {
		clearKilledSessions();
	});
}

void Instance::Private::clearKilledSessions() {
	_killedSessions.clear();
}

void Instance::Private::stopSession(ShiftedDcId shiftedDcId) {
	auto it = _sessions.find(shiftedDcId);
	if (it != _sessions.end()) {
		if (it->second.get() != _mainSession) { // don't stop main session
			it->second->stop();
		}
	}
}

void Instance::Private::reInitConnection(DcId dcId) {
	killSession(dcId);
	getSession(dcId)->notifyLayerInited(false);
}

void Instance::Private::logout(RPCDoneHandlerPtr onDone, RPCFailHandlerPtr onFail) {
	_instance->send(MTPauth_LogOut(), onDone, onFail);

	auto dcIds = std::vector<DcId>();
	{
		QReadLocker lock(&_keysForWriteLock);
		dcIds.reserve(_keysForWrite.size());
		for (auto &key : _keysForWrite) {
			dcIds.push_back(key.first);
		}
	}
	for (auto dcId : dcIds) {
		if (dcId != mainDcId() && dcOptions()->dcType(dcId) != DcType::Cdn) {
			auto shiftedDcId = MTP::logoutDcId(dcId);
			auto requestId = _instance->send(MTPauth_LogOut(), rpcDone([this](mtpRequestId requestId) {
				logoutGuestDone(requestId);
			}), rpcFail([this](mtpRequestId requestId) {
				return logoutGuestDone(requestId);
			}), shiftedDcId);
			_logoutGuestRequestIds.emplace(shiftedDcId, requestId);
		}
	}
}

bool Instance::Private::logoutGuestDone(mtpRequestId requestId) {
	for (auto i = _logoutGuestRequestIds.begin(), e = _logoutGuestRequestIds.end(); i != e; ++i) {
		if (i->second == requestId) {
			killSession(i->first);
			_logoutGuestRequestIds.erase(i);
			return true;
		}
	}
	return false;
}

std::shared_ptr<internal::Dcenter> Instance::Private::getDcById(ShiftedDcId shiftedDcId) {
	auto it = _dcenters.find(shiftedDcId);
	if (it == _dcenters.cend()) {
		auto dcId = bareDcId(shiftedDcId);
		if (isTemporaryDcId(dcId)) {
			if (auto realDcId = getRealIdFromTemporaryDcId(dcId)) {
				dcId = realDcId;
			}
		}
		it = _dcenters.find(dcId);
		if (it == _dcenters.cend()) {
			auto result = std::make_shared<internal::Dcenter>(_instance, dcId, AuthKeyPtr());
			return _dcenters.emplace(dcId, std::move(result)).first->second;
		}
	}
	return it->second;
}

void Instance::Private::setKeyForWrite(DcId dcId, const AuthKeyPtr &key) {
	if (isTemporaryDcId(dcId)) {
		return;
	}

	QWriteLocker lock(&_keysForWriteLock);
	if (key) {
		_keysForWrite[dcId] = key;
	} else {
		_keysForWrite.erase(dcId);
	}
}

AuthKeysList Instance::Private::getKeysForWrite() const {
	auto result = AuthKeysList();

	QReadLocker lock(&_keysForWriteLock);
	result.reserve(_keysForWrite.size());
	for (auto &key : _keysForWrite) {
		result.push_back(key.second);
	}
	return result;
}

void Instance::Private::addKeysForDestroy(AuthKeysList &&keys) {
	Expects(isKeysDestroyer());

	for (auto &key : keys) {
		auto dcId = key->dcId();
		auto shiftedDcId = MTP::destroyKeyNextDcId(dcId);

		{
			QWriteLocker lock(&_keysForWriteLock);
			// There could be several keys for one dc if we're destroying them.
			// Place them all in separate shiftedDcId so that they won't conflict.
			while (_keysForWrite.find(shiftedDcId) != _keysForWrite.cend()) {
				shiftedDcId = MTP::destroyKeyNextDcId(shiftedDcId);
			}
			_keysForWrite[shiftedDcId] = key;
		}

		auto dc = std::make_shared<internal::Dcenter>(_instance, dcId, std::move(key));
		_dcenters.emplace(shiftedDcId, std::move(dc));

		Assert(!MustNotCreateSessions);
		auto session = std::make_unique<internal::Session>(_instance, shiftedDcId);
		auto it = _sessions.emplace(shiftedDcId, std::move(session)).first;
		it->second->start();
	}
}

not_null<DcOptions*> Instance::Private::dcOptions() {
	return _dcOptions;
}

void Instance::Private::unpaused() {
	for (auto &session : _sessions) {
		session.second->unpaused();
	}
}

void Instance::Private::queueQuittingConnection(std::unique_ptr<internal::Connection> connection) {
	_quittingConnections.insert(std::move(connection));
}

void Instance::Private::connectionFinished(internal::Connection *connection) {
	auto it = _quittingConnections.find(connection);
	if (it != _quittingConnections.end()) {
		_quittingConnections.erase(it);
	}
}

void Instance::Private::configLoadDone(const MTPConfig &result) {
	Expects(result.type() == mtpc_config);

	_configLoader.reset();

	auto &data = result.c_config();
	DEBUG_LOG(("MTP Info: got config, chat_size_max: %1, date: %2, test_mode: %3, this_dc: %4, dc_options.length: %5").arg(data.vchat_size_max.v).arg(data.vdate.v).arg(mtpIsTrue(data.vtest_mode)).arg(data.vthis_dc.v).arg(data.vdc_options.v.size()));
	if (data.vdc_options.v.empty()) {
		LOG(("MTP Error: config with empty dc_options received!"));
	} else {
		_dcOptions->setFromList(data.vdc_options);
	}

	Global::SetChatSizeMax(data.vchat_size_max.v);
	Global::SetMegagroupSizeMax(data.vmegagroup_size_max.v);
	Global::SetForwardedCountMax(data.vforwarded_count_max.v);
	Global::SetOnlineUpdatePeriod(data.vonline_update_period_ms.v);
	Global::SetOfflineBlurTimeout(data.voffline_blur_timeout_ms.v);
	Global::SetOfflineIdleTimeout(data.voffline_idle_timeout_ms.v);
	Global::SetOnlineCloudTimeout(data.vonline_cloud_timeout_ms.v);
	Global::SetNotifyCloudDelay(data.vnotify_cloud_delay_ms.v);
	Global::SetNotifyDefaultDelay(data.vnotify_default_delay_ms.v);
	Global::SetChatBigSize(data.vchat_big_size.v);
	Global::SetPushChatPeriod(data.vpush_chat_period_ms.v);
	Global::SetPushChatLimit(data.vpush_chat_limit.v);
	Global::SetSavedGifsLimit(data.vsaved_gifs_limit.v);
	Global::SetEditTimeLimit(data.vedit_time_limit.v);
	Global::SetStickersRecentLimit(data.vstickers_recent_limit.v);
	Global::SetStickersFavedLimit(data.vstickers_faved_limit.v);
	Global::SetPinnedDialogsCountMax(data.vpinned_dialogs_count_max.v);
	Messenger::Instance().setInternalLinkDomain(qs(data.vme_url_prefix));
	Global::SetChannelsReadMediaPeriod(data.vchannels_read_media_period.v);
	Global::SetCallReceiveTimeoutMs(data.vcall_receive_timeout_ms.v);
	Global::SetCallRingTimeoutMs(data.vcall_ring_timeout_ms.v);
	Global::SetCallConnectTimeoutMs(data.vcall_connect_timeout_ms.v);
	Global::SetCallPacketTimeoutMs(data.vcall_packet_timeout_ms.v);
	if (Global::PhoneCallsEnabled() != data.is_phonecalls_enabled()) {
		Global::SetPhoneCallsEnabled(data.is_phonecalls_enabled());
		Global::RefPhoneCallsEnabledChanged().notify();
	}
	Lang::CurrentCloudManager().setSuggestedLanguage(data.has_suggested_lang_code() ? qs(data.vsuggested_lang_code) : QString());

	Local::writeSettings();

	emit _instance->configLoaded();
}

bool Instance::Private::configLoadFail(const RPCError &error) {
	if (isDefaultHandledError(error)) return false;

	//	loadingConfig = false;
	LOG(("MTP Error: failed to get config!"));
	return false;
}

void Instance::Private::checkDelayedRequests() {
	auto now = getms(true);
	while (!_delayedRequests.empty() && now >= _delayedRequests.front().second) {
		auto requestId = _delayedRequests.front().first;
		_delayedRequests.pop_front();

		auto dcWithShift = ShiftedDcId(0);
		{
			QMutexLocker locker(&_requestByDcLock);
			auto it = _requestsByDc.find(requestId);
			if (it != _requestsByDc.cend()) {
				dcWithShift = it->second;
			} else {
				LOG(("MTP Error: could not find request dc for delayed resend, requestId %1").arg(requestId));
				continue;
			}
		}

		auto request = mtpRequest();
		{
			QReadLocker locker(&_requestMapLock);
			auto it = _requestMap.find(requestId);
			if (it == _requestMap.cend()) {
				DEBUG_LOG(("MTP Error: could not find request %1").arg(requestId));
				continue;
			}
			request = it->second;
		}
		if (auto session = getSession(qAbs(dcWithShift))) {
			session->sendPrepared(request);
		}
	}

	if (!_delayedRequests.empty()) {
		_checkDelayedTimer.callOnce(_delayedRequests.front().second - now);
	}
}

void Instance::Private::registerRequest(mtpRequestId requestId, int32 dcWithShift) {
	{
		QMutexLocker locker(&_requestByDcLock);
		_requestsByDc.emplace(requestId, dcWithShift);
	}
	performDelayedClear(); // need to do it somewhere...
}

void Instance::Private::unregisterRequest(mtpRequestId requestId) {
	_requestsDelays.erase(requestId);

	{
		QWriteLocker locker(&_requestMapLock);
		_requestMap.erase(requestId);
	}

	QMutexLocker locker(&_requestByDcLock);
	_requestsByDc.erase(requestId);
}

mtpRequestId Instance::Private::storeRequest(mtpRequest &request, const RPCResponseHandler &parser) {
	mtpRequestId res = reqid();
	request->requestId = res;
	if (parser.onDone || parser.onFail) {
		QMutexLocker locker(&_parserMapLock);
		_parserMap.emplace(res, parser);
	}
	{
		QWriteLocker locker(&_requestMapLock);
		_requestMap.emplace(res, request);
	}
	return res;
}

mtpRequest Instance::Private::getRequest(mtpRequestId requestId) {
	auto result = mtpRequest();
	{
		QReadLocker locker(&_requestMapLock);
		auto it = _requestMap.find(requestId);
		if (it != _requestMap.cend()) {
			result = it->second;
		}
	}
	return result;
}


void Instance::Private::clearCallbacks(mtpRequestId requestId, int32 errorCode) {
	RPCResponseHandler h;
	bool found = false;
	{
		QMutexLocker locker(&_parserMapLock);
		auto it = _parserMap.find(requestId);
		if (it != _parserMap.end()) {
			h = it->second;
			found = true;

			_parserMap.erase(it);
		}
	}
	if (errorCode && found) {
		rpcErrorOccured(requestId, h, internal::rpcClientError("CLEAR_CALLBACK", QString("did not handle request %1, error code %2").arg(requestId).arg(errorCode)));
	}
}

void Instance::Private::clearCallbacksDelayed(const RPCCallbackClears &requestIds) {
	uint32 idsCount = requestIds.size();
	if (!idsCount) return;

	if (cDebug()) {
		QString idsStr = QString("%1").arg(requestIds[0].requestId);
		for (uint32 i = 1; i < idsCount; ++i) {
			idsStr += QString(", %1").arg(requestIds[i].requestId);
		}
		DEBUG_LOG(("RPC Info: clear callbacks delayed, msgIds: %1").arg(idsStr));
	}

	QMutexLocker lock(&_toClearLock);
	uint32 toClearNow = _toClear.size();
	if (toClearNow) {
		_toClear.resize(toClearNow + idsCount);
		memcpy(_toClear.data() + toClearNow, requestIds.constData(), idsCount * sizeof(RPCCallbackClear));
	} else {
		_toClear = requestIds;
	}
}

void Instance::Private::performDelayedClear() {
	QMutexLocker lock(&_toClearLock);
	if (!_toClear.isEmpty()) {
		for (auto &clearRequest : _toClear) {
			if (cDebug()) {
				QMutexLocker locker(&_parserMapLock);
				if (_parserMap.find(clearRequest.requestId) != _parserMap.end()) {
					DEBUG_LOG(("RPC Info: clearing delayed callback %1, error code %2").arg(clearRequest.requestId).arg(clearRequest.errorCode));
				}
			}
			clearCallbacks(clearRequest.requestId, clearRequest.errorCode);
			unregisterRequest(clearRequest.requestId);
		}
		_toClear.clear();
	}
}

void Instance::Private::execCallback(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) {
	RPCResponseHandler h;
	{
		QMutexLocker locker(&_parserMapLock);
		auto it = _parserMap.find(requestId);
		if (it != _parserMap.cend()) {
			h = it->second;
			_parserMap.erase(it);

			DEBUG_LOG(("RPC Info: found parser for request %1, trying to parse response...").arg(requestId));
		}
	}
	if (h.onDone || h.onFail) {
		try {
			if (from >= end) throw mtpErrorInsufficient();

			if (*from == mtpc_rpc_error) {
				auto mtpError = MTPRpcError();
				mtpError.read(from, end);
				auto error = RPCError(mtpError);
				DEBUG_LOG(("RPC Info: error received, code %1, type %2, description: %3").arg(error.code()).arg(error.type()).arg(error.description()));
				if (!rpcErrorOccured(requestId, h, error)) {
					QMutexLocker locker(&_parserMapLock);
					_parserMap.emplace(requestId, h);
					return;
				}
			} else {
				if (h.onDone) {
					(*h.onDone)(requestId, from, end);
				}
			}
		} catch (Exception &e) {
			if (!rpcErrorOccured(requestId, h, internal::rpcClientError("RESPONSE_PARSE_FAILED", QString("exception text: ") + e.what()))) {
				QMutexLocker locker(&_parserMapLock);
				_parserMap.emplace(requestId, h);
				return;
			}
		}
	} else {
		DEBUG_LOG(("RPC Info: parser not found for %1").arg(requestId));
	}
	unregisterRequest(requestId);
}

bool Instance::Private::hasCallbacks(mtpRequestId requestId) {
	QMutexLocker locker(&_parserMapLock);
	auto it = _parserMap.find(requestId);
	return (it != _parserMap.cend());
}

void Instance::Private::globalCallback(const mtpPrime *from, const mtpPrime *end) {
	if (_globalHandler.onDone) {
		(*_globalHandler.onDone)(0, from, end); // some updates were received
	}
}

void Instance::Private::onStateChange(int32 dcWithShift, int32 state) {
	if (_stateChangedHandler) {
		_stateChangedHandler(dcWithShift, state);
	}
}

void Instance::Private::onSessionReset(int32 dcWithShift) {
	if (_sessionResetHandler) {
		_sessionResetHandler(dcWithShift);
	}
}

bool Instance::Private::rpcErrorOccured(mtpRequestId requestId, const RPCFailHandlerPtr &onFail, const RPCError &err) { // return true if need to clean request data
	if (isDefaultHandledError(err)) {
		if (onFail && (*onFail)(requestId, err)) return true;
	}

	if (onErrorDefault(requestId, err)) {
		return false;
	}
	LOG(("RPC Error: request %1 got fail with code %2, error %3%4").arg(requestId).arg(err.code()).arg(err.type()).arg(err.description().isEmpty() ? QString() : QString(": %1").arg(err.description())));
	onFail && (*onFail)(requestId, err);
	return true;
}

bool Instance::Private::hasAuthorization() {
	return AuthSession::Exists();
}

void Instance::Private::importDone(const MTPauth_Authorization &result, mtpRequestId requestId) {
	QMutexLocker locker1(&_requestByDcLock);

	auto it = _requestsByDc.find(requestId);
	if (it == _requestsByDc.end()) {
		LOG(("MTP Error: auth import request not found in requestsByDC, requestId: %1").arg(requestId));
		RPCError error(internal::rpcClientError("AUTH_IMPORT_FAIL", QString("did not find import request in requestsByDC, request %1").arg(requestId)));
		if (_globalHandler.onFail && hasAuthorization()) {
			(*_globalHandler.onFail)(requestId, error); // auth failed in main dc
		}
		return;
	}
	auto newdc = bareDcId(it->second);

	DEBUG_LOG(("MTP Info: auth import to dc %1 succeeded").arg(newdc));

	auto &waiters = _authWaiters[newdc];
	if (waiters.size()) {
		QReadLocker locker(&_requestMapLock);
		for (auto waitedRequestId : waiters) {
			auto it = _requestMap.find(waitedRequestId);
			if (it == _requestMap.cend()) {
				LOG(("MTP Error: could not find request %1 for resending").arg(waitedRequestId));
				continue;
			}
			auto dcWithShift = ShiftedDcId(newdc);
			{
				auto k = _requestsByDc.find(waitedRequestId);
				if (k == _requestsByDc.cend()) {
					LOG(("MTP Error: could not find request %1 by dc for resending").arg(waitedRequestId));
					continue;
				}
				if (k->second < 0) {
					_instance->setMainDcId(newdc);
					k->second = -newdc;
				} else {
					dcWithShift = shiftDcId(newdc, getDcIdShift(k->second));
					k->second = dcWithShift;
				}
				DEBUG_LOG(("MTP Info: resending request %1 to dc %2 after import auth").arg(waitedRequestId).arg(k->second));
			}
			if (auto session = getSession(dcWithShift)) {
				session->sendPrepared(it->second);
			}
		}
		waiters.clear();
	}
}

bool Instance::Private::importFail(const RPCError &error, mtpRequestId requestId) {
	if (isDefaultHandledError(error)) return false;

	if (_globalHandler.onFail && hasAuthorization()) {
		(*_globalHandler.onFail)(requestId, error); // auth import failed
	}
	return true;
}

void Instance::Private::exportDone(const MTPauth_ExportedAuthorization &result, mtpRequestId requestId) {
	auto it = _authExportRequests.find(requestId);
	if (it == _authExportRequests.cend()) {
		LOG(("MTP Error: auth export request target dcWithShift not found, requestId: %1").arg(requestId));
		RPCError error(internal::rpcClientError("AUTH_IMPORT_FAIL", QString("did not find target dcWithShift, request %1").arg(requestId)));
		if (_globalHandler.onFail && hasAuthorization()) {
			(*_globalHandler.onFail)(requestId, error); // auth failed in main dc
		}
		return;
	}

	auto &data = result.c_auth_exportedAuthorization();
	_instance->send(MTPauth_ImportAuthorization(data.vid, data.vbytes), rpcDone([this](const MTPauth_Authorization &result, mtpRequestId requestId) {
		importDone(result, requestId);
	}), rpcFail([this](const RPCError &error, mtpRequestId requestId) {
		return importFail(error, requestId);
	}), it->second);
	_authExportRequests.erase(requestId);
}

bool Instance::Private::exportFail(const RPCError &error, mtpRequestId requestId) {
	if (isDefaultHandledError(error)) return false;

	auto it = _authExportRequests.find(requestId);
	if (it != _authExportRequests.cend()) {
		_authWaiters[bareDcId(it->second)].clear();
	}
	if (_globalHandler.onFail && hasAuthorization()) {
		(*_globalHandler.onFail)(requestId, error); // auth failed in main dc
	}
	return true;
}

bool Instance::Private::onErrorDefault(mtpRequestId requestId, const RPCError &error) {
	auto &err(error.type());
	auto code = error.code();
	if (!isFloodError(error) && err != qstr("AUTH_KEY_UNREGISTERED")) {
		int breakpoint = 0;
	}
	auto badGuestDc = (code == 400) && (err == qsl("FILE_ID_INVALID"));
	QRegularExpressionMatch m;
	if ((m = QRegularExpression("^(FILE|PHONE|NETWORK|USER)_MIGRATE_(\\d+)$").match(err)).hasMatch()) {
		if (!requestId) return false;

		ShiftedDcId dcWithShift = 0, newdcWithShift = m.captured(2).toInt();
		{
			QMutexLocker locker(&_requestByDcLock);
			auto it = _requestsByDc.find(requestId);
			if (it == _requestsByDc.end()) {
				LOG(("MTP Error: could not find request %1 for migrating to %2").arg(requestId).arg(newdcWithShift));
			} else {
				dcWithShift = it->second;
			}
		}
		if (!dcWithShift || !newdcWithShift) return false;

		DEBUG_LOG(("MTP Info: changing request %1 from dcWithShift%2 to dc%3").arg(requestId).arg(dcWithShift).arg(newdcWithShift));
		if (dcWithShift < 0) { // newdc shift = 0
			if (false && hasAuthorization() && _authExportRequests.find(requestId) == _authExportRequests.cend()) {
				//
				// migrate not supported at this moment
				// this was not tested even once
				//
				//DEBUG_LOG(("MTP Info: importing auth to dc %1").arg(newdcWithShift));
				//auto &waiters(_authWaiters[newdcWithShift]);
				//if (waiters.empty()) {
				//	auto exportRequestId = _instance->send(MTPauth_ExportAuthorization(MTP_int(newdcWithShift)), rpcDone([this](const MTPauth_ExportedAuthorization &result, mtpRequestId requestId) {
				//		exportDone(result, requestId);
				//	}), rpcFail([this](const RPCError &error, mtpRequestId requestId) {
				//		return exportFail(error, requestId);
				//	}));
				//	_authExportRequests.emplace(exportRequestId, newdcWithShift);
				//}
				//waiters.push_back(requestId);
				//return true;
			} else {
				_instance->setMainDcId(newdcWithShift);
			}
		} else {
			newdcWithShift = shiftDcId(newdcWithShift, getDcIdShift(dcWithShift));
		}

		auto request = mtpRequest();
		{
			QReadLocker locker(&_requestMapLock);
			auto it = _requestMap.find(requestId);
			if (it == _requestMap.cend()) {
				LOG(("MTP Error: could not find request %1").arg(requestId));
				return false;
			}
			request = it->second;
		}
		if (auto session = getSession(newdcWithShift)) {
			registerRequest(requestId, (dcWithShift < 0) ? -newdcWithShift : newdcWithShift);
			session->sendPrepared(request);
		}
		return true;
	} else if (code < 0 || code >= 500 || (m = QRegularExpression("^FLOOD_WAIT_(\\d+)$").match(err)).hasMatch()) {
		if (!requestId) return false;

		int32 secs = 1;
		if (code < 0 || code >= 500) {
			auto it = _requestsDelays.find(requestId);
			if (it != _requestsDelays.cend()) {
				secs = (it->second > 60) ? it->second : (it->second *= 2);
			} else {
				_requestsDelays.emplace(requestId, secs);
			}
		} else {
			secs = m.captured(1).toInt();
//			if (secs >= 60) return false;
		}
		auto sendAt = getms(true) + secs * 1000 + 10;
		auto it = _delayedRequests.begin(), e = _delayedRequests.end();
		for (; it != e; ++it) {
			if (it->first == requestId) return true;
			if (it->second > sendAt) break;
		}
		_delayedRequests.insert(it, std::make_pair(requestId, sendAt));

		checkDelayedRequests();

		return true;
	} else if (code == 401 || (badGuestDc && _badGuestDcRequests.find(requestId) == _badGuestDcRequests.cend())) {
		auto dcWithShift = ShiftedDcId(0);
		{
			QMutexLocker locker(&_requestByDcLock);
			auto it = _requestsByDc.find(requestId);
			if (it != _requestsByDc.end()) {
				dcWithShift = it->second;
			} else {
				LOG(("MTP Error: unauthorized request without dc info, requestId %1").arg(requestId));
			}
		}
		auto newdc = bareDcId(qAbs(dcWithShift));
		if (!newdc || newdc == mainDcId() || !hasAuthorization()) {
			if (!badGuestDc && _globalHandler.onFail) {
				(*_globalHandler.onFail)(requestId, error); // auth failed in main dc
			}
			return false;
		}

		DEBUG_LOG(("MTP Info: importing auth to dcWithShift %1").arg(dcWithShift));
		auto &waiters(_authWaiters[newdc]);
		if (!waiters.size()) {
			auto exportRequestId = _instance->send(MTPauth_ExportAuthorization(MTP_int(newdc)), rpcDone([this](const MTPauth_ExportedAuthorization &result, mtpRequestId requestId) {
				exportDone(result, requestId);
			}), rpcFail([this](const RPCError &error, mtpRequestId requestId) {
				return exportFail(error, requestId);
			}));
			_authExportRequests.emplace(exportRequestId, abs(dcWithShift));
		}
		waiters.push_back(requestId);
		if (badGuestDc) _badGuestDcRequests.insert(requestId);
		return true;
	} else if (err == qstr("CONNECTION_NOT_INITED") || err == qstr("CONNECTION_LAYER_INVALID")) {
		mtpRequest request;
		{
			QReadLocker locker(&_requestMapLock);
			auto it = _requestMap.find(requestId);
			if (it == _requestMap.cend()) {
				LOG(("MTP Error: could not find request %1").arg(requestId));
				return false;
			}
			request = it->second;
		}
		auto dcWithShift = ShiftedDcId(0);
		{
			QMutexLocker locker(&_requestByDcLock);
			auto it = _requestsByDc.find(requestId);
			if (it == _requestsByDc.end()) {
				LOG(("MTP Error: could not find request %1 for resending with init connection").arg(requestId));
			} else {
				dcWithShift = it->second;
			}
		}
		if (!dcWithShift) return false;

		if (auto session = getSession(qAbs(dcWithShift))) {
			request->needsLayer = true;
			session->sendPrepared(request);
		}
		return true;
	} else if (err == qstr("CONNECTION_LANG_CODE_INVALID")) {
		Lang::CurrentCloudManager().resetToDefault();
	} else if (err == qstr("MSG_WAIT_FAILED")) {
		mtpRequest request;
		{
			QReadLocker locker(&_requestMapLock);
			auto it = _requestMap.find(requestId);
			if (it == _requestMap.cend()) {
				LOG(("MTP Error: could not find request %1").arg(requestId));
				return false;
			}
			request = it->second;
		}
		if (!request->after) {
			LOG(("MTP Error: wait failed for not dependent request %1").arg(requestId));
			return false;
		}
		auto dcWithShift = ShiftedDcId(0);
		{
			QMutexLocker locker(&_requestByDcLock);
			auto it = _requestsByDc.find(requestId);
			auto afterIt = _requestsByDc.find(request->after->requestId);
			if (it == _requestsByDc.end()) {
				LOG(("MTP Error: could not find request %1 by dc").arg(requestId));
			} else if (afterIt == _requestsByDc.end()) {
				LOG(("MTP Error: could not find dependent request %1 by dc").arg(request->after->requestId));
			} else {
				dcWithShift = it->second;
				if (it->second != afterIt->second) {
					request->after = mtpRequest();
				}
			}
		}
		if (!dcWithShift) return false;

		if (!request->after) {
			if (auto session = getSession(qAbs(dcWithShift))) {
				request->needsLayer = true;
				session->sendPrepared(request);
			}
		} else {
			auto newdc = bareDcId(qAbs(dcWithShift));
			auto &waiters(_authWaiters[newdc]);
			if (base::contains(waiters, request->after->requestId)) {
				if (!base::contains(waiters, requestId)) {
					waiters.push_back(requestId);
				}
				if (_badGuestDcRequests.find(request->after->requestId) != _badGuestDcRequests.cend()) {
					if (_badGuestDcRequests.find(requestId) == _badGuestDcRequests.cend()) {
						_badGuestDcRequests.insert(requestId);
					}
				}
			} else {
				auto i = _delayedRequests.begin(), e = _delayedRequests.end();
				for (; i != e; ++i) {
					if (i->first == requestId) return true;
					if (i->first == request->after->requestId) break;
				}
				if (i != e) {
					_delayedRequests.insert(i, std::make_pair(requestId, i->second));
				}

				checkDelayedRequests();
			}
		}
		return true;
	}
	if (badGuestDc) _badGuestDcRequests.erase(requestId);
	return false;
}

internal::Session *Instance::Private::getSession(ShiftedDcId shiftedDcId) {
	if (!shiftedDcId) {
		Assert(_mainSession != nullptr);
		return _mainSession;
	}
	if (!bareDcId(shiftedDcId)) {
		Assert(_mainSession != nullptr);
		shiftedDcId += bareDcId(_mainSession->getDcWithShift());
	}

	auto it = _sessions.find(shiftedDcId);
	if (it == _sessions.cend()) {
		Assert(!MustNotCreateSessions);
		it = _sessions.emplace(shiftedDcId, std::make_unique<internal::Session>(_instance, shiftedDcId)).first;
		it->second->start();
	}
	return it->second.get();
}

void Instance::Private::scheduleKeyDestroy(ShiftedDcId shiftedDcId) {
	Expects(isKeysDestroyer());

	_instance->send(MTPauth_LogOut(), rpcDone([this, shiftedDcId](const MTPBool &result) {
		performKeyDestroy(shiftedDcId);
	}), rpcFail([this, shiftedDcId](const RPCError &error) {
		if (isDefaultHandledError(error)) return false;
		performKeyDestroy(shiftedDcId);
		return true;
	}), shiftedDcId);
}

void Instance::Private::performKeyDestroy(ShiftedDcId shiftedDcId) {
	Expects(isKeysDestroyer());

	_instance->send(MTPDestroy_auth_key(), rpcDone([this, shiftedDcId](const MTPDestroyAuthKeyRes &result) {
		switch (result.type()) {
		case mtpc_destroy_auth_key_ok: LOG(("MTP Info: key %1 destroyed.").arg(shiftedDcId)); break;
		case mtpc_destroy_auth_key_fail: {
			LOG(("MTP Error: key %1 destruction fail, leave it for now.").arg(shiftedDcId));
			killSession(shiftedDcId);
		} break;
		case mtpc_destroy_auth_key_none: LOG(("MTP Info: key %1 already destroyed.").arg(shiftedDcId)); break;
		}
		emit _instance->keyDestroyed(shiftedDcId);
	}), rpcFail([this, shiftedDcId](const RPCError &error) {
		LOG(("MTP Error: key %1 destruction resulted in error: %2").arg(shiftedDcId).arg(error.type()));
		emit _instance->keyDestroyed(shiftedDcId);
		return true;
	}), shiftedDcId);
}

void Instance::Private::completedKeyDestroy(ShiftedDcId shiftedDcId) {
	Expects(isKeysDestroyer());

	_dcenters.erase(shiftedDcId);
	{
		QWriteLocker lock(&_keysForWriteLock);
		_keysForWrite.erase(shiftedDcId);
	}
	killSession(shiftedDcId);
	if (_dcenters.empty()) {
		emit _instance->allKeysDestroyed();
	}
}

void Instance::Private::setUpdatesHandler(RPCDoneHandlerPtr onDone) {
	_globalHandler.onDone = onDone;
}

void Instance::Private::setGlobalFailHandler(RPCFailHandlerPtr onFail) {
	_globalHandler.onFail = onFail;
}

void Instance::Private::setStateChangedHandler(base::lambda<void(ShiftedDcId shiftedDcId, int32 state)> handler) {
	_stateChangedHandler = std::move(handler);
}

void Instance::Private::setSessionResetHandler(base::lambda<void(ShiftedDcId shiftedDcId)> handler) {
	_sessionResetHandler = std::move(handler);
}

void Instance::Private::clearGlobalHandlers() {
	setUpdatesHandler(RPCDoneHandlerPtr());
	setGlobalFailHandler(RPCFailHandlerPtr());
	setStateChangedHandler(base::lambda<void(ShiftedDcId,int32)>());
	setSessionResetHandler(base::lambda<void(ShiftedDcId)>());
}

void Instance::Private::prepareToDestroy() {
	// It accesses Instance in destructor, so it should be destroyed first.
	_configLoader.reset();

	requestCancellingDiscard();

	for (auto &session : base::take(_sessions)) {
		session.second->kill();
	}
	_mainSession = nullptr;

	MustNotCreateSessions = true;
}

Instance::Instance(not_null<DcOptions*> options, Mode mode, Config &&config) : QObject()
, _private(std::make_unique<Private>(this, options, mode)) {
	_private->start(std::move(config));
}

void Instance::suggestMainDcId(DcId mainDcId) {
	_private->suggestMainDcId(mainDcId);
}

void Instance::setMainDcId(DcId mainDcId) {
	_private->setMainDcId(mainDcId);
}

DcId Instance::mainDcId() const {
	return _private->mainDcId();
}

QString Instance::systemLangCode() const {
	return Lang::Current().systemLangCode();
}

QString Instance::cloudLangCode() const {
	return Lang::Current().cloudLangCode();
}

void Instance::requestConfig() {
	_private->requestConfig();
}

void Instance::requestCDNConfig() {
	_private->requestCDNConfig();
}

void Instance::connectionFinished(internal::Connection *connection) {
	_private->connectionFinished(connection);
}

void Instance::restart() {
	_private->restart();
}

void Instance::restart(ShiftedDcId shiftedDcId) {
	_private->restart(shiftedDcId);
}

int32 Instance::dcstate(ShiftedDcId shiftedDcId) {
	return _private->dcstate(shiftedDcId);
}

QString Instance::dctransport(ShiftedDcId shiftedDcId) {
	return _private->dctransport(shiftedDcId);
}

void Instance::ping() {
	_private->ping();
}

void Instance::cancel(mtpRequestId requestId) {
	_private->cancel(requestId);
}

int32 Instance::state(mtpRequestId requestId) { // < 0 means waiting for such count of ms
	return _private->state(requestId);
}

void Instance::killSession(ShiftedDcId shiftedDcId) {
	_private->killSession(shiftedDcId);
}

void Instance::stopSession(ShiftedDcId shiftedDcId) {
	_private->stopSession(shiftedDcId);
}

void Instance::reInitConnection(DcId dcId) {
	_private->reInitConnection(dcId);
}

void Instance::logout(RPCDoneHandlerPtr onDone, RPCFailHandlerPtr onFail) {
	_private->logout(onDone, onFail);
}

std::shared_ptr<internal::Dcenter> Instance::getDcById(ShiftedDcId shiftedDcId) {
	return _private->getDcById(shiftedDcId);
}

void Instance::setKeyForWrite(DcId dcId, const AuthKeyPtr &key) {
	_private->setKeyForWrite(dcId, key);
}

AuthKeysList Instance::getKeysForWrite() const {
	return _private->getKeysForWrite();
}

void Instance::addKeysForDestroy(AuthKeysList &&keys) {
	_private->addKeysForDestroy(std::move(keys));
}

not_null<DcOptions*> Instance::dcOptions() {
	return _private->dcOptions();
}

void Instance::unpaused() {
	_private->unpaused();
}

void Instance::queueQuittingConnection(std::unique_ptr<internal::Connection> connection) {
	_private->queueQuittingConnection(std::move(connection));
}

void Instance::setUpdatesHandler(RPCDoneHandlerPtr onDone) {
	_private->setUpdatesHandler(onDone);
}

void Instance::setGlobalFailHandler(RPCFailHandlerPtr onFail) {
	_private->setGlobalFailHandler(onFail);
}

void Instance::setStateChangedHandler(base::lambda<void(ShiftedDcId shiftedDcId, int32 state)> handler) {
	_private->setStateChangedHandler(std::move(handler));
}

void Instance::setSessionResetHandler(base::lambda<void(ShiftedDcId shiftedDcId)> handler) {
	_private->setSessionResetHandler(std::move(handler));
}

void Instance::clearGlobalHandlers() {
	_private->clearGlobalHandlers();
}

void Instance::onStateChange(ShiftedDcId dcWithShift, int32 state) {
	_private->onStateChange(dcWithShift, state);
}

void Instance::onSessionReset(ShiftedDcId dcWithShift) {
	_private->onSessionReset(dcWithShift);
}

void Instance::registerRequest(mtpRequestId requestId, ShiftedDcId dcWithShift) {
	_private->registerRequest(requestId, dcWithShift);
}

mtpRequestId Instance::storeRequest(mtpRequest &request, const RPCResponseHandler &parser) {
	return _private->storeRequest(request, parser);
}

mtpRequest Instance::getRequest(mtpRequestId requestId) {
	return _private->getRequest(requestId);
}

void Instance::clearCallbacksDelayed(const RPCCallbackClears &requestIds) {
	_private->clearCallbacksDelayed(requestIds);
}

void Instance::execCallback(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end) {
	_private->execCallback(requestId, from, end);
}

bool Instance::hasCallbacks(mtpRequestId requestId) {
	return _private->hasCallbacks(requestId);
}

void Instance::globalCallback(const mtpPrime *from, const mtpPrime *end) {
	_private->globalCallback(from, end);
}

bool Instance::rpcErrorOccured(mtpRequestId requestId, const RPCFailHandlerPtr &onFail, const RPCError &err) {
	return _private->rpcErrorOccured(requestId, onFail, err);
}

internal::Session *Instance::getSession(ShiftedDcId shiftedDcId) {
	return _private->getSession(shiftedDcId);
}

bool Instance::isKeysDestroyer() const {
	return _private->isKeysDestroyer();
}

void Instance::scheduleKeyDestroy(ShiftedDcId shiftedDcId) {
	_private->scheduleKeyDestroy(shiftedDcId);
}

void Instance::onKeyDestroyed(qint32 shiftedDcId) {
	_private->completedKeyDestroy(shiftedDcId);
}

Instance::~Instance() {
	_private->prepareToDestroy();
}

} // namespace MTP
