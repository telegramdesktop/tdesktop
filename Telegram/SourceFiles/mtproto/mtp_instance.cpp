/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/mtp_instance.h"

#include "mtproto/session.h"
#include "mtproto/dc_options.h"
#include "mtproto/dcenter.h"
#include "mtproto/config_loader.h"
#include "mtproto/special_config_request.h"
#include "mtproto/connection.h"
#include "mtproto/sender.h"
#include "mtproto/rsa_public_key.h"
#include "storage/localstorage.h"
#include "calls/calls_instance.h"
#include "auth_session.h"
#include "application.h"
#include "apiwrap.h"
#include "messenger.h"
#include "lang/lang_instance.h"
#include "lang/lang_cloud_manager.h"
#include "base/timer.h"

namespace MTP {
namespace {

constexpr auto kConfigBecomesOldIn = 2 * 60 * TimeMs(1000);
constexpr auto kConfigBecomesOldForBlockedIn = 8 * TimeMs(1000);

} // namespace

class Instance::Private : private Sender {
public:
	Private(not_null<Instance*> instance, not_null<DcOptions*> options, Instance::Mode mode);

	void start(Config &&config);

	void resolveProxyDomain(const QString &host);
	void setGoodProxyDomain(const QString &host, const QString &ip);
	void suggestMainDcId(DcId mainDcId);
	void setMainDcId(DcId mainDcId);
	DcId mainDcId() const;

	void setKeyForWrite(DcId dcId, const AuthKeyPtr &key);
	AuthKeysList getKeysForWrite() const;
	void addKeysForDestroy(AuthKeysList &&keys);

	not_null<DcOptions*> dcOptions();

	void requestConfig();
	void requestConfigIfOld();
	void requestCDNConfig();
	void setUserPhone(const QString &phone);
	void badConfigurationError();

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

	void queueQuittingConnection(
		std::unique_ptr<internal::Connection> &&connection);
	void connectionFinished(internal::Connection *connection);

	void sendRequest(
		mtpRequestId requestId,
		SecureRequest &&request,
		RPCResponseHandler &&callbacks,
		ShiftedDcId shiftedDcId,
		TimeMs msCanWait,
		bool needsLayer,
		mtpRequestId afterRequestId);
	void registerRequest(mtpRequestId requestId, ShiftedDcId shiftedDcId);
	void unregisterRequest(mtpRequestId requestId);
	void storeRequest(
		mtpRequestId requestId,
		const SecureRequest &request,
		RPCResponseHandler &&callbacks);
	SecureRequest getRequest(mtpRequestId requestId);
	void clearCallbacksDelayed(std::vector<RPCCallbackClear> &&ids);
	void execCallback(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end);
	bool hasCallbacks(mtpRequestId requestId);
	void globalCallback(const mtpPrime *from, const mtpPrime *end);

	void onStateChange(ShiftedDcId shiftedDcId, int32 state);
	void onSessionReset(ShiftedDcId shiftedDcId);

	// return true if need to clean request data
	bool rpcErrorOccured(mtpRequestId requestId, const RPCFailHandlerPtr &onFail, const RPCError &err);
	inline bool rpcErrorOccured(mtpRequestId requestId, const RPCResponseHandler &handler, const RPCError &err) {
		return rpcErrorOccured(requestId, handler.onFail, err);
	}

	void setUpdatesHandler(RPCDoneHandlerPtr onDone);
	void setGlobalFailHandler(RPCFailHandlerPtr onFail);
	void setStateChangedHandler(Fn<void(ShiftedDcId shiftedDcId, int32 state)> handler);
	void setSessionResetHandler(Fn<void(ShiftedDcId shiftedDcId)> handler);
	void clearGlobalHandlers();

	not_null<internal::Session*> getSession(ShiftedDcId shiftedDcId);

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

	void applyDomainIps(
		const QString &host,
		const QStringList &ips,
		TimeMs expireAt);

	void logoutGuestDcs();
	bool logoutGuestDone(mtpRequestId requestId);

	void requestConfigIfExpired();
	void configLoadDone(const MTPConfig &result);
	bool configLoadFail(const RPCError &error);

	std::optional<ShiftedDcId> queryRequestByDc(
		mtpRequestId requestId) const;
	std::optional<ShiftedDcId> changeRequestByDc(
		mtpRequestId requestId, DcId newdc);

	// RPCError::NoError means do not toggle onError callback.
	void clearCallbacks(
		mtpRequestId requestId,
		int32 errorCode = RPCError::NoError);
	void clearCallbacks(const std::vector<RPCCallbackClear> &ids);

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
	std::unique_ptr<DomainResolver> _domainResolver;
	QString _userPhone;
	mtpRequestId _cdnConfigLoadRequestId = 0;
	TimeMs _lastConfigLoadedTime = 0;
	TimeMs _configExpiresAt = 0;

	std::map<DcId, AuthKeyPtr> _keysForWrite;
	mutable QReadWriteLock _keysForWriteLock;

	std::map<ShiftedDcId, mtpRequestId> _logoutGuestRequestIds;

	// holds dcWithShift for request to this dc or -dc for request to main dc
	std::map<mtpRequestId, ShiftedDcId> _requestsByDc;
	mutable QMutex _requestByDcLock;

	// holds target dcWithShift for auth export request
	std::map<mtpRequestId, ShiftedDcId> _authExportRequests;

	std::map<mtpRequestId, RPCResponseHandler> _parserMap;
	QMutex _parserMapLock;

	std::map<mtpRequestId, SecureRequest> _requestMap;
	QReadWriteLock _requestMapLock;

	std::deque<std::pair<mtpRequestId, TimeMs>> _delayedRequests;

	std::map<mtpRequestId, int> _requestsDelays;

	std::set<mtpRequestId> _badGuestDcRequests;

	std::map<DcId, std::vector<mtpRequestId>> _authWaiters;

	RPCResponseHandler _globalHandler;
	Fn<void(ShiftedDcId shiftedDcId, int32 state)> _stateChangedHandler;
	Fn<void(ShiftedDcId shiftedDcId)> _sessionResetHandler;

	base::Timer _checkDelayedTimer;

	// Debug flag to find out how we end up crashing.
	bool MustNotCreateSessions = false;

};

Instance::Private::Private(
	not_null<Instance*> instance,
	not_null<DcOptions*> options,
	Instance::Mode mode)
: Sender()
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
	requestConfig();
}

void Instance::Private::resolveProxyDomain(const QString &host) {
	if (!_domainResolver) {
		_domainResolver = std::make_unique<DomainResolver>([=](
				const QString &host,
				const QStringList &ips,
				TimeMs expireAt) {
			applyDomainIps(host, ips, expireAt);
		});
	}
	_domainResolver->resolve(host);
}

void Instance::Private::applyDomainIps(
		const QString &host,
		const QStringList &ips,
		TimeMs expireAt) {
	const auto applyToProxy = [&](ProxyData &proxy) {
		if (!proxy.tryCustomResolve() || proxy.host != host) {
			return false;
		}
		proxy.resolvedExpireAt = expireAt;
		auto copy = ips;
		auto &current = proxy.resolvedIPs;
		const auto i = ranges::remove_if(current, [&](const QString &ip) {
			const auto index = copy.indexOf(ip);
			if (index < 0) {
				return true;
			}
			copy.removeAt(index);
			return false;
		});
		if (i == end(current) && copy.isEmpty()) {
			// Even if the proxy was changed already, we still want
			// to refreshOptions in all sessions across all instances.
			return true;
		}
		current.erase(i, end(current));
		for (const auto &ip : copy) {
			proxy.resolvedIPs.push_back(ip);
		}
		return true;
	};
	for (auto &proxy : Global::RefProxiesList()) {
		applyToProxy(proxy);
	}
	if (applyToProxy(Global::RefSelectedProxy())
		&& (Global::ProxySettings() == ProxyData::Settings::Enabled)) {
		for (auto &session : _sessions) {
			session.second->refreshOptions();
		}
	}
	emit _instance->proxyDomainResolved(host, ips, expireAt);
}

void Instance::Private::setGoodProxyDomain(
		const QString &host,
		const QString &ip) {
	const auto applyToProxy = [&](ProxyData &proxy) {
		if (!proxy.tryCustomResolve() || proxy.host != host) {
			return false;
		}
		auto &current = proxy.resolvedIPs;
		auto i = ranges::find(current, ip);
		if (i == end(current) || i == begin(current)) {
			return false;
		}
		while (i != begin(current)) {
			const auto j = i--;
			std::swap(*i, *j);
		}
		return true;
	};
	for (auto &proxy : Global::RefProxiesList()) {
		applyToProxy(proxy);
	}
	if (applyToProxy(Global::RefSelectedProxy())
		&& (Global::ProxySettings() == ProxyData::Settings::Enabled)) {
		Sandbox::refreshGlobalProxy();
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
	if (_configLoader || isKeysDestroyer()) {
		return;
	}
	_configLoader = std::make_unique<internal::ConfigLoader>(
		_instance,
		_userPhone,
		rpcDone([=](const MTPConfig &result) { configLoadDone(result); }),
		rpcFail([=](const RPCError &error) { return configLoadFail(error); }));
	_configLoader->load();
}

void Instance::Private::setUserPhone(const QString &phone) {
	if (_userPhone != phone) {
		_userPhone = phone;
		if (_configLoader) {
			_configLoader->setPhone(_userPhone);
		}
	}
}

void Instance::Private::badConfigurationError() {
	if (_mode == Mode::Normal) {
		Messenger::Instance().badMtprotoConfigurationError();
	}
}

void Instance::Private::requestConfigIfOld() {
	const auto timeout = Global::BlockedMode()
		? kConfigBecomesOldForBlockedIn
		: kConfigBecomesOldIn;
	if (getms(true) - _lastConfigLoadedTime >= timeout) {
		requestConfig();
	}
}

void Instance::Private::requestConfigIfExpired() {
	const auto requestIn = (_configExpiresAt - getms(true));
	if (requestIn > 0) {
		App::CallDelayed(
			std::min(requestIn, 3600 * TimeMs(1000)),
			_instance,
			[=] { requestConfigIfExpired(); });
	} else {
		requestConfig();
	}
}

void Instance::Private::requestCDNConfig() {
	if (_cdnConfigLoadRequestId || _mainDcId == Config::kNoneMainDc) {
		return;
	}
	_cdnConfigLoadRequestId = request(
		MTPhelp_GetCdnConfig()
	).done([this](const MTPCdnConfig &result) {
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
	auto dcId = BareDcId(shiftedDcId);
	for (auto &session : _sessions) {
		if (BareDcId(session.second->getDcWithShift()) == dcId) {
			session.second->restart();
		}
	}
}

int32 Instance::Private::dcstate(ShiftedDcId shiftedDcId) {
	if (!shiftedDcId) {
		Assert(_mainSession != nullptr);
		return _mainSession->getState();
	}

	if (!BareDcId(shiftedDcId)) {
		Assert(_mainSession != nullptr);
		shiftedDcId += BareDcId(_mainSession->getDcWithShift());
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
	if (!BareDcId(shiftedDcId)) {
		Assert(_mainSession != nullptr);
		shiftedDcId += BareDcId(_mainSession->getDcWithShift());
	}

	auto it = _sessions.find(shiftedDcId);
	if (it != _sessions.cend()) {
		return it->second->transport();
	}

	return QString();
}

void Instance::Private::ping() {
	getSession(0)->ping();
}

void Instance::Private::cancel(mtpRequestId requestId) {
	if (!requestId) return;

	DEBUG_LOG(("MTP Info: Cancel request %1.").arg(requestId));
	const auto shiftedDcId = queryRequestByDc(requestId);
	auto msgId = mtpMsgId(0);
	{
		QWriteLocker locker(&_requestMapLock);
		auto it = _requestMap.find(requestId);
		if (it != _requestMap.end()) {
			msgId = *(mtpMsgId*)(it->second->constData() + 4);
			_requestMap.erase(it);
		}
	}
	unregisterRequest(requestId);
	if (shiftedDcId) {
		const auto session = getSession(qAbs(*shiftedDcId));
		session->cancel(requestId, msgId);
	}
	clearCallbacks(requestId);
}

// result < 0 means waiting for such count of ms.
int32 Instance::Private::state(mtpRequestId requestId) {
	if (requestId > 0) {
		if (const auto shiftedDcId = queryRequestByDc(requestId)) {
			const auto session = getSession(qAbs(*shiftedDcId));
			return session->requestState(requestId);
		}
		return MTP::RequestSent;
	}
	const auto session = getSession(-requestId);
	return session->requestState(0);
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
	for (auto &session : _sessions) {
		if (BareDcId(session.second->getDcWithShift()) == dcId) {
			session.second->reInitConnection();
		}
	}
}

void Instance::Private::logout(
		RPCDoneHandlerPtr onDone,
		RPCFailHandlerPtr onFail) {
	_instance->send(MTPauth_LogOut(), std::move(onDone), std::move(onFail));
	logoutGuestDcs();
}

void Instance::Private::logoutGuestDcs() {
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
		auto dcId = BareDcId(shiftedDcId);
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

void Instance::Private::queueQuittingConnection(
		std::unique_ptr<internal::Connection> &&connection) {
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
	_lastConfigLoadedTime = getms(true);

	const auto &data = result.c_config();
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
	Global::SetPushChatPeriod(data.vpush_chat_period_ms.v);
	Global::SetPushChatLimit(data.vpush_chat_limit.v);
	Global::SetSavedGifsLimit(data.vsaved_gifs_limit.v);
	Global::SetEditTimeLimit(data.vedit_time_limit.v);
	Global::SetRevokeTimeLimit(data.vrevoke_time_limit.v);
	Global::SetRevokePrivateTimeLimit(data.vrevoke_pm_time_limit.v);
	Global::SetRevokePrivateInbox(data.is_revoke_pm_inbox());
	Global::SetStickersRecentLimit(data.vstickers_recent_limit.v);
	Global::SetStickersFavedLimit(data.vstickers_faved_limit.v);
	Global::SetPinnedDialogsCountMax(data.vpinned_dialogs_count_max.v);
	Messenger::Instance().setInternalLinkDomain(qs(data.vme_url_prefix));
	Global::SetChannelsReadMediaPeriod(data.vchannels_read_media_period.v);
	Global::SetWebFileDcId(data.vwebfile_dc_id.v);
	Global::SetTxtDomainString(qs(data.vdc_txt_domain_name));
	Global::SetCallReceiveTimeoutMs(data.vcall_receive_timeout_ms.v);
	Global::SetCallRingTimeoutMs(data.vcall_ring_timeout_ms.v);
	Global::SetCallConnectTimeoutMs(data.vcall_connect_timeout_ms.v);
	Global::SetCallPacketTimeoutMs(data.vcall_packet_timeout_ms.v);
	if (Global::PhoneCallsEnabled() != data.is_phonecalls_enabled()) {
		Global::SetPhoneCallsEnabled(data.is_phonecalls_enabled());
		Global::RefPhoneCallsEnabledChanged().notify();
	}
	Global::SetBlockedMode(data.is_blocked_mode());
	Global::SetCaptionLengthMax(data.vcaption_length_max.v);

	const auto lang = data.has_suggested_lang_code()
		? qs(data.vsuggested_lang_code)
		: QString();
	Lang::CurrentCloudManager().setSuggestedLanguage(lang);
	Lang::CurrentCloudManager().setCurrentVersions(
		(data.has_lang_pack_version()
			? data.vlang_pack_version.v
			: 0),
		(data.has_base_lang_pack_version()
			? data.vbase_lang_pack_version.v
			: 0));

	if (data.has_autoupdate_url_prefix()) {
		Local::writeAutoupdatePrefix(qs(data.vautoupdate_url_prefix));
	}
	Local::writeSettings();

	_configExpiresAt = getms(true)
		+ (data.vexpires.v - unixtime()) * TimeMs(1000);
	requestConfigIfExpired();

	emit _instance->configLoaded();
}

bool Instance::Private::configLoadFail(const RPCError &error) {
	if (isDefaultHandledError(error)) return false;

	//	loadingConfig = false;
	LOG(("MTP Error: failed to get config!"));
	return false;
}

std::optional<ShiftedDcId> Instance::Private::queryRequestByDc(
		mtpRequestId requestId) const {
	QMutexLocker locker(&_requestByDcLock);
	auto it = _requestsByDc.find(requestId);
	if (it != _requestsByDc.cend()) {
		return it->second;
	}
	return std::nullopt;
}

std::optional<ShiftedDcId> Instance::Private::changeRequestByDc(
		mtpRequestId requestId,
		DcId newdc) {
	QMutexLocker locker(&_requestByDcLock);
	auto it = _requestsByDc.find(requestId);
	if (it != _requestsByDc.cend()) {
		if (it->second < 0) {
			it->second = -newdc;
		} else {
			it->second = ShiftDcId(newdc, GetDcIdShift(it->second));
		}
		return it->second;
	}
	return std::nullopt;
}

void Instance::Private::checkDelayedRequests() {
	auto now = getms(true);
	while (!_delayedRequests.empty() && now >= _delayedRequests.front().second) {
		auto requestId = _delayedRequests.front().first;
		_delayedRequests.pop_front();

		auto dcWithShift = ShiftedDcId(0);
		if (const auto shiftedDcId = queryRequestByDc(requestId)) {
			dcWithShift = *shiftedDcId;
		} else {
			LOG(("MTP Error: could not find request dc for delayed resend, requestId %1").arg(requestId));
			continue;
		}

		auto request = SecureRequest();
		{
			QReadLocker locker(&_requestMapLock);
			auto it = _requestMap.find(requestId);
			if (it == _requestMap.cend()) {
				DEBUG_LOG(("MTP Error: could not find request %1").arg(requestId));
				continue;
			}
			request = it->second;
		}
		const auto session = getSession(qAbs(dcWithShift));
		session->sendPrepared(request);
	}

	if (!_delayedRequests.empty()) {
		_checkDelayedTimer.callOnce(_delayedRequests.front().second - now);
	}
}

void Instance::Private::sendRequest(
		mtpRequestId requestId,
		SecureRequest &&request,
		RPCResponseHandler &&callbacks,
		ShiftedDcId shiftedDcId,
		TimeMs msCanWait,
		bool needsLayer,
		mtpRequestId afterRequestId) {
	const auto session = getSession(shiftedDcId);

	request->requestId = requestId;
	storeRequest(requestId, request, std::move(callbacks));

	const auto toMainDc = (shiftedDcId == 0);
	const auto realShiftedDcId = session->getDcWithShift();
	const auto signedDcId = toMainDc ? -realShiftedDcId : realShiftedDcId;
	registerRequest(requestId, signedDcId);

	if (afterRequestId) {
		request->after = getRequest(afterRequestId);
	}
	request->msDate = getms(true); // > 0 - can send without container
	request->needsLayer = needsLayer;

	session->sendPrepared(request, msCanWait);
}

void Instance::Private::registerRequest(
		mtpRequestId requestId,
		ShiftedDcId shiftedDcId) {
	QMutexLocker locker(&_requestByDcLock);
	_requestsByDc[requestId] = shiftedDcId;
}

void Instance::Private::unregisterRequest(mtpRequestId requestId) {
	DEBUG_LOG(("MTP Info: unregistering request %1.").arg(requestId));

	_requestsDelays.erase(requestId);

	{
		QWriteLocker locker(&_requestMapLock);
		_requestMap.erase(requestId);
	}

	QMutexLocker locker(&_requestByDcLock);
	_requestsByDc.erase(requestId);
}

void Instance::Private::storeRequest(
		mtpRequestId requestId,
		const SecureRequest &request,
		RPCResponseHandler &&callbacks) {
	if (callbacks.onDone || callbacks.onFail) {
		QMutexLocker locker(&_parserMapLock);
		_parserMap.emplace(requestId, std::move(callbacks));
	}
	{
		QWriteLocker locker(&_requestMapLock);
		_requestMap.emplace(requestId, request);
	}
}

SecureRequest Instance::Private::getRequest(mtpRequestId requestId) {
	auto result = SecureRequest();
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
		LOG(("API Error: callbacks cleared without handling! "
			"Request: %1, error code: %2"
			).arg(requestId
			).arg(errorCode));
		rpcErrorOccured(requestId, h, internal::rpcClientError("CLEAR_CALLBACK", QString("did not handle request %1, error code %2").arg(requestId).arg(errorCode)));
	}
}

void Instance::Private::clearCallbacksDelayed(
		std::vector<RPCCallbackClear> &&ids) {
	if (ids.empty()) {
		return;
	}

	if (Logs::DebugEnabled()) {
		auto idsString = QStringList();
		idsString.reserve(ids.size());
		for (auto &value : ids) {
			idsString.push_back(QString::number(value.requestId));
		}
		DEBUG_LOG(("RPC Info: clear callbacks delayed, msgIds: %1"
			).arg(idsString.join(", ")));
	}

	crl::on_main(_instance, [this, list = std::move(ids)] {
		clearCallbacks(list);
	});
}

void Instance::Private::clearCallbacks(
		const std::vector<RPCCallbackClear> &ids) {
	Expects(!ids.empty());

	for (const auto &clearRequest : ids) {
		if (Logs::DebugEnabled()) {
			QMutexLocker locker(&_parserMapLock);
			const auto hasParsers = (_parserMap.find(clearRequest.requestId)
				!= _parserMap.end());
			DEBUG_LOG(("RPC Info: "
				"clearing delayed callback %1, error code %2, parsers: %3"
				).arg(clearRequest.requestId
				).arg(clearRequest.errorCode
				).arg(Logs::b(hasParsers)));
		}
		clearCallbacks(clearRequest.requestId, clearRequest.errorCode);
		unregisterRequest(clearRequest.requestId);
	}
}

void Instance::Private::execCallback(
		mtpRequestId requestId,
		const mtpPrime *from,
		const mtpPrime *end) {
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
		const auto handleError = [&](const MTPRpcError &error) {
			const auto wrapped = RPCError(error);
			DEBUG_LOG(("RPC Info: "
				"error received, code %1, type %2, description: %3"
				).arg(wrapped.code()
				).arg(wrapped.type()
				).arg(wrapped.description()));
			if (rpcErrorOccured(requestId, h, wrapped)) {
				unregisterRequest(requestId);
			} else {
				QMutexLocker locker(&_parserMapLock);
				_parserMap.emplace(requestId, h);
			}
		};

		try {
			if (from >= end) throw mtpErrorInsufficient();
			if (*from == mtpc_rpc_error) {
				auto error = MTPRpcError();
				error.read(from, end);
				handleError(error);
			} else {
				if (h.onDone) {
					(*h.onDone)(requestId, from, end);
				}
				unregisterRequest(requestId);
			}
		} catch (Exception &e) {
			handleError(internal::rpcClientError(
				"RESPONSE_PARSE_FAILED",
				QString("exception text: ") + e.what()));
		}
	} else {
		DEBUG_LOG(("RPC Info: parser not found for %1").arg(requestId));
		unregisterRequest(requestId);
	}
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
	const auto shiftedDcId = queryRequestByDc(requestId);
	if (!shiftedDcId) {
		LOG(("MTP Error: auth import request not found in requestsByDC, requestId: %1").arg(requestId));
		//
		// Don't log out on export/import problems, perhaps this is a server side error.
		//
		//RPCError error(internal::rpcClientError("AUTH_IMPORT_FAIL", QString("did not find import request in requestsByDC, request %1").arg(requestId)));
		//if (_globalHandler.onFail && hasAuthorization()) {
		//	(*_globalHandler.onFail)(requestId, error); // auth failed in main dc
		//}
		return;
	}
	auto newdc = BareDcId(*shiftedDcId);

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
			const auto shiftedDcId = changeRequestByDc(waitedRequestId, newdc);
			if (!shiftedDcId) {
				LOG(("MTP Error: could not find request %1 by dc for resending").arg(waitedRequestId));
				continue;
			} else if (*shiftedDcId < 0) {
				_instance->setMainDcId(newdc);
			}
			DEBUG_LOG(("MTP Info: resending request %1 to dc %2 after import auth").arg(waitedRequestId).arg(*shiftedDcId));
			const auto session = getSession(*shiftedDcId);
			session->sendPrepared(it->second);
		}
		waiters.clear();
	}
}

bool Instance::Private::importFail(const RPCError &error, mtpRequestId requestId) {
	if (isDefaultHandledError(error)) return false;

	//
	// Don't log out on export/import problems, perhaps this is a server side error.
	//
	//if (_globalHandler.onFail && hasAuthorization()) {
	//	(*_globalHandler.onFail)(requestId, error); // auth import failed
	//}
	return true;
}

void Instance::Private::exportDone(const MTPauth_ExportedAuthorization &result, mtpRequestId requestId) {
	auto it = _authExportRequests.find(requestId);
	if (it == _authExportRequests.cend()) {
		LOG(("MTP Error: auth export request target dcWithShift not found, requestId: %1").arg(requestId));
		//
		// Don't log out on export/import problems, perhaps this is a server side error.
		//
		//RPCError error(internal::rpcClientError("AUTH_IMPORT_FAIL", QString("did not find target dcWithShift, request %1").arg(requestId)));
		//if (_globalHandler.onFail && hasAuthorization()) {
		//	(*_globalHandler.onFail)(requestId, error); // auth failed in main dc
		//}
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
		_authWaiters[BareDcId(it->second)].clear();
	}
	//
	// Don't log out on export/import problems, perhaps this is a server side error.
	//
	//if (_globalHandler.onFail && hasAuthorization()) {
	//	(*_globalHandler.onFail)(requestId, error); // auth failed in main dc
	//}
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

		auto dcWithShift = ShiftedDcId(0);
		auto newdcWithShift = ShiftedDcId(m.captured(2).toInt());
		if (const auto shiftedDcId = queryRequestByDc(requestId)) {
			dcWithShift = *shiftedDcId;
		} else {
			LOG(("MTP Error: could not find request %1 for migrating to %2").arg(requestId).arg(newdcWithShift));
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
			newdcWithShift = ShiftDcId(newdcWithShift, GetDcIdShift(dcWithShift));
		}

		auto request = SecureRequest();
		{
			QReadLocker locker(&_requestMapLock);
			auto it = _requestMap.find(requestId);
			if (it == _requestMap.cend()) {
				LOG(("MTP Error: could not find request %1").arg(requestId));
				return false;
			}
			request = it->second;
		}
		const auto session = getSession(newdcWithShift);
		registerRequest(
			requestId,
			(dcWithShift < 0) ? -newdcWithShift : newdcWithShift);
		session->sendPrepared(request);
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
		if (const auto shiftedDcId = queryRequestByDc(requestId)) {
			dcWithShift = *shiftedDcId;
		} else {
			LOG(("MTP Error: unauthorized request without dc info, requestId %1").arg(requestId));
		}
		auto newdc = BareDcId(qAbs(dcWithShift));
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
		SecureRequest request;
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
		if (const auto shiftedDcId = queryRequestByDc(requestId)) {
			dcWithShift = *shiftedDcId;
		} else {
			LOG(("MTP Error: could not find request %1 for resending with init connection").arg(requestId));
		}
		if (!dcWithShift) return false;

		const auto session = getSession(qAbs(dcWithShift));
		request->needsLayer = true;
		session->sendPrepared(request);
		return true;
	} else if (err == qstr("CONNECTION_LANG_CODE_INVALID")) {
		Lang::CurrentCloudManager().resetToDefault();
	} else if (err == qstr("MSG_WAIT_FAILED")) {
		SecureRequest request;
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
		if (const auto shiftedDcId = queryRequestByDc(requestId)) {
			if (const auto afterDcId = queryRequestByDc(request->after->requestId)) {
				dcWithShift = *shiftedDcId;
				if (*shiftedDcId != *afterDcId) {
					request->after = SecureRequest();
				}
			} else {
				LOG(("MTP Error: could not find dependent request %1 by dc").arg(request->after->requestId));
			}
		} else {
			LOG(("MTP Error: could not find request %1 by dc").arg(requestId));
		}
		if (!dcWithShift) return false;

		if (!request->after) {
			const auto session = getSession(qAbs(dcWithShift));
			request->needsLayer = true;
			session->sendPrepared(request);
		} else {
			auto newdc = BareDcId(qAbs(dcWithShift));
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

not_null<internal::Session*> Instance::Private::getSession(
		ShiftedDcId shiftedDcId) {
	if (!shiftedDcId) {
		Assert(_mainSession != nullptr);
		return _mainSession;
	}
	if (!BareDcId(shiftedDcId)) {
		Assert(_mainSession != nullptr);
		shiftedDcId += BareDcId(_mainSession->getDcWithShift());
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

	if (dcOptions()->dcType(shiftedDcId) == DcType::Cdn) {
		performKeyDestroy(shiftedDcId);
	} else {
		_instance->send(MTPauth_LogOut(), rpcDone([=](const MTPBool &) {
			performKeyDestroy(shiftedDcId);
		}), rpcFail([=](const RPCError &error) {
			if (isDefaultHandledError(error)) return false;
			performKeyDestroy(shiftedDcId);
			return true;
		}), shiftedDcId);
	}
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

void Instance::Private::setStateChangedHandler(Fn<void(ShiftedDcId shiftedDcId, int32 state)> handler) {
	_stateChangedHandler = std::move(handler);
}

void Instance::Private::setSessionResetHandler(Fn<void(ShiftedDcId shiftedDcId)> handler) {
	_sessionResetHandler = std::move(handler);
}

void Instance::Private::clearGlobalHandlers() {
	setUpdatesHandler(RPCDoneHandlerPtr());
	setGlobalFailHandler(RPCFailHandlerPtr());
	setStateChangedHandler(Fn<void(ShiftedDcId,int32)>());
	setSessionResetHandler(Fn<void(ShiftedDcId)>());
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

Instance::Instance(not_null<DcOptions*> options, Mode mode, Config &&config)
: QObject()
, _private(std::make_unique<Private>(this, options, mode)) {
	_private->start(std::move(config));
}

void Instance::resolveProxyDomain(const QString &host) {
	_private->resolveProxyDomain(host);
}

void Instance::setGoodProxyDomain(const QString &host, const QString &ip) {
	_private->setGoodProxyDomain(host, ip);
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
	return Lang::Current().cloudLangCode(Lang::Pack::Current);
}

QString Instance::langPackName() const {
	return Lang::Current().langPackName();
}

void Instance::requestConfig() {
	_private->requestConfig();
}

void Instance::setUserPhone(const QString &phone) {
	_private->setUserPhone(phone);
}

void Instance::badConfigurationError() {
	_private->badConfigurationError();
}

void Instance::requestConfigIfOld() {
	_private->requestConfigIfOld();
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

void Instance::queueQuittingConnection(
		std::unique_ptr<internal::Connection> &&connection) {
	_private->queueQuittingConnection(std::move(connection));
}

void Instance::setUpdatesHandler(RPCDoneHandlerPtr onDone) {
	_private->setUpdatesHandler(onDone);
}

void Instance::setGlobalFailHandler(RPCFailHandlerPtr onFail) {
	_private->setGlobalFailHandler(onFail);
}

void Instance::setStateChangedHandler(Fn<void(ShiftedDcId shiftedDcId, int32 state)> handler) {
	_private->setStateChangedHandler(std::move(handler));
}

void Instance::setSessionResetHandler(Fn<void(ShiftedDcId shiftedDcId)> handler) {
	_private->setSessionResetHandler(std::move(handler));
}

void Instance::clearGlobalHandlers() {
	_private->clearGlobalHandlers();
}

void Instance::onStateChange(ShiftedDcId shiftedDcId, int32 state) {
	_private->onStateChange(shiftedDcId, state);
}

void Instance::onSessionReset(ShiftedDcId shiftedDcId) {
	_private->onSessionReset(shiftedDcId);
}

void Instance::clearCallbacksDelayed(std::vector<RPCCallbackClear> &&ids) {
	_private->clearCallbacksDelayed(std::move(ids));
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

bool Instance::isKeysDestroyer() const {
	return _private->isKeysDestroyer();
}

void Instance::scheduleKeyDestroy(ShiftedDcId shiftedDcId) {
	_private->scheduleKeyDestroy(shiftedDcId);
}

void Instance::onKeyDestroyed(qint32 shiftedDcId) {
	_private->completedKeyDestroy(shiftedDcId);
}
void Instance::sendRequest(
		mtpRequestId requestId,
		SecureRequest &&request,
		RPCResponseHandler &&callbacks,
		ShiftedDcId shiftedDcId,
		TimeMs msCanWait,
		bool needsLayer,
		mtpRequestId afterRequestId) {
	return _private->sendRequest(
		requestId,
		std::move(request),
		std::move(callbacks),
		shiftedDcId,
		msCanWait,
		needsLayer,
		afterRequestId);
}

void Instance::sendAnything(ShiftedDcId shiftedDcId, TimeMs msCanWait) {
	const auto session = _private->getSession(shiftedDcId);
	session->sendAnything(msCanWait);
}

Instance::~Instance() {
	_private->prepareToDestroy();
}

} // namespace MTP
