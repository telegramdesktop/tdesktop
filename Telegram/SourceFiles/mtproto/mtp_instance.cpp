/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/mtp_instance.h"

#include "mtproto/details/mtproto_dcenter.h"
#include "mtproto/details/mtproto_rsa_public_key.h"
#include "mtproto/special_config_request.h"
#include "mtproto/session.h"
#include "mtproto/mtproto_config.h"
#include "mtproto/mtproto_dc_options.h"
#include "mtproto/config_loader.h"
#include "mtproto/sender.h"
#include "storage/localstorage.h"
#include "calls/calls_instance.h"
#include "main/main_account.h" // Account::configUpdated.
#include "apiwrap.h"
#include "core/application.h"
#include "lang/lang_instance.h"
#include "lang/lang_cloud_manager.h"
#include "base/unixtime.h"
#include "base/call_delayed.h"
#include "base/timer.h"
#include "base/network_reachability.h"
#include "facades.h" // Proxies list.

namespace MTP {
namespace {

constexpr auto kConfigBecomesOldIn = 2 * 60 * crl::time(1000);
constexpr auto kConfigBecomesOldForBlockedIn = 8 * crl::time(1000);
constexpr auto kCheckKeyEach = 60 * crl::time(1000);

using namespace details;

std::atomic<int> GlobalAtomicRequestId = 0;

} // namespace

namespace details {

int GetNextRequestId() {
	const auto result = ++GlobalAtomicRequestId;
	if (result == std::numeric_limits<int>::max() / 2) {
		GlobalAtomicRequestId = 0;
	}
	return result;
}

} // namespace details

class Instance::Private : private Sender {
public:
	Private(
		not_null<Instance*> instance,
		Instance::Mode mode,
		Fields &&fields);

	void start();

	[[nodiscard]] Config &config() const;
	[[nodiscard]] const ConfigFields &configValues() const;
	[[nodiscard]] DcOptions &dcOptions() const;
	[[nodiscard]] Environment environment() const;
	[[nodiscard]] bool isTestMode() const;

	void resolveProxyDomain(const QString &host);
	void setGoodProxyDomain(const QString &host, const QString &ip);
	void suggestMainDcId(DcId mainDcId);
	void setMainDcId(DcId mainDcId);
	[[nodiscard]] DcId mainDcId() const;

	[[nodiscard]] rpl::producer<> writeKeysRequests() const;

	void dcPersistentKeyChanged(DcId dcId, const AuthKeyPtr &persistentKey);
	void dcTemporaryKeyChanged(DcId dcId);
	[[nodiscard]] rpl::producer<DcId> dcTemporaryKeyChanged() const;
	[[nodiscard]] AuthKeysList getKeysForWrite() const;
	void addKeysForDestroy(AuthKeysList &&keys);
	[[nodiscard]] rpl::producer<> allKeysDestroyed() const;

	// Thread safe.
	[[nodiscard]] QString deviceModel() const;
	[[nodiscard]] QString systemVersion() const;

	// Main thread.
	void requestConfig();
	void requestConfigIfOld();
	void requestCDNConfig();
	void setUserPhone(const QString &phone);
	void badConfigurationError();
	void syncHttpUnixtime();

	void restartedByTimeout(ShiftedDcId shiftedDcId);
	[[nodiscard]] rpl::producer<ShiftedDcId> restartsByTimeout() const;

	void restart();
	void restart(ShiftedDcId shiftedDcId);
	[[nodiscard]] int32 dcstate(ShiftedDcId shiftedDcId = 0);
	[[nodiscard]] QString dctransport(ShiftedDcId shiftedDcId = 0);
	void ping();
	void cancel(mtpRequestId requestId);
	[[nodiscard]] int32 state(mtpRequestId requestId); // < 0 means waiting for such count of ms
	void killSession(ShiftedDcId shiftedDcId);
	void stopSession(ShiftedDcId shiftedDcId);
	void reInitConnection(DcId dcId);
	void logout(Fn<void()> done);

	not_null<Dcenter*> getDcById(ShiftedDcId shiftedDcId);
	Dcenter *findDc(ShiftedDcId shiftedDcId);
	not_null<Dcenter*> addDc(
		ShiftedDcId shiftedDcId,
		AuthKeyPtr &&key = nullptr);
	void removeDc(ShiftedDcId shiftedDcId);

	void sendRequest(
		mtpRequestId requestId,
		SerializedRequest &&request,
		ResponseHandler &&callbacks,
		ShiftedDcId shiftedDcId,
		crl::time msCanWait,
		bool needsLayer,
		mtpRequestId afterRequestId);
	void registerRequest(mtpRequestId requestId, ShiftedDcId shiftedDcId);
	void unregisterRequest(mtpRequestId requestId);
	void storeRequest(
		mtpRequestId requestId,
		const SerializedRequest &request,
		ResponseHandler &&callbacks);
	SerializedRequest getRequest(mtpRequestId requestId);
	[[nodiscard]] bool hasCallback(mtpRequestId requestId) const;
	void processCallback(const Response &response);
	void processUpdate(const Response &message);

	void onStateChange(ShiftedDcId shiftedDcId, int32 state);
	void onSessionReset(ShiftedDcId shiftedDcId);

	// return true if need to clean request data
	bool rpcErrorOccured(
		const Response &response,
		const FailHandler &onFail,
		const Error &error);
	inline bool rpcErrorOccured(
			const Response &response,
			const ResponseHandler &handler,
			const Error &error) {
		return rpcErrorOccured(response, handler.fail, error);
	}

	void setUpdatesHandler(Fn<void(const Response&)> handler);
	void setGlobalFailHandler(
		Fn<void(const Error&, const Response&)> handler);
	void setStateChangedHandler(Fn<void(ShiftedDcId shiftedDcId, int32 state)> handler);
	void setSessionResetHandler(Fn<void(ShiftedDcId shiftedDcId)> handler);
	void clearGlobalHandlers();

	[[nodiscard]] not_null<Session*> getSession(ShiftedDcId shiftedDcId);

	bool isNormal() const {
		return (_mode == Instance::Mode::Normal);
	}
	bool isKeysDestroyer() const {
		return (_mode == Instance::Mode::KeysDestroyer);
	}

	void scheduleKeyDestroy(ShiftedDcId shiftedDcId);
	void keyWasPossiblyDestroyed(ShiftedDcId shiftedDcId);
	void performKeyDestroy(ShiftedDcId shiftedDcId);
	void completedKeyDestroy(ShiftedDcId shiftedDcId);
	void keyDestroyedOnServer(ShiftedDcId shiftedDcId, uint64 keyId);

	void prepareToDestroy();

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void importDone(
		const MTPauth_Authorization &result,
		const Response &response);
	bool importFail(const Error &error, const Response &response);
	void exportDone(
		const MTPauth_ExportedAuthorization &result,
		const Response &response);
	bool exportFail(const Error &error, const Response &response);
	bool onErrorDefault(const Error &error, const Response &response);

	void unpaused();

	Session *findSession(ShiftedDcId shiftedDcId);
	not_null<Session*> startSession(ShiftedDcId shiftedDcId);
	Session *removeSession(ShiftedDcId shiftedDcId);
	[[nodiscard]] not_null<QThread*> getThreadForDc(ShiftedDcId shiftedDcId);

	void applyDomainIps(
		const QString &host,
		const QStringList &ips,
		crl::time expireAt);

	void logoutGuestDcs();
	bool logoutGuestDone(mtpRequestId requestId);

	void requestConfigIfExpired();
	void configLoadDone(const MTPConfig &result);
	bool configLoadFail(const Error &error);

	std::optional<ShiftedDcId> queryRequestByDc(
		mtpRequestId requestId) const;
	std::optional<ShiftedDcId> changeRequestByDc(
		mtpRequestId requestId, DcId newdc);

	void checkDelayedRequests();

	const not_null<Instance*> _instance;
	const Instance::Mode _mode = Instance::Mode::Normal;
	const std::unique_ptr<Config> _config;
	const std::shared_ptr<base::NetworkReachability> _networkReachability;

	std::unique_ptr<QThread> _mainSessionThread;
	std::unique_ptr<QThread> _otherSessionsThread;
	std::vector<std::unique_ptr<QThread>> _fileSessionThreads;

	QString _deviceModel;
	QString _systemVersion;

	DcId _mainDcId = Fields::kDefaultMainDc;
	bool _mainDcIdForced = false;
	base::flat_map<DcId, std::unique_ptr<Dcenter>> _dcenters;
	std::vector<std::unique_ptr<Dcenter>> _dcentersToDestroy;
	rpl::event_stream<DcId> _dcTemporaryKeyChanged;

	Session *_mainSession = nullptr;
	base::flat_map<ShiftedDcId, std::unique_ptr<Session>> _sessions;
	std::vector<std::unique_ptr<Session>> _sessionsToDestroy;
	rpl::event_stream<ShiftedDcId> _restartsByTimeout;

	std::unique_ptr<ConfigLoader> _configLoader;
	std::unique_ptr<DomainResolver> _domainResolver;
	std::unique_ptr<SpecialConfigRequest> _httpUnixtimeLoader;
	QString _userPhone;
	mtpRequestId _cdnConfigLoadRequestId = 0;
	crl::time _lastConfigLoadedTime = 0;
	crl::time _configExpiresAt = 0;

	base::flat_map<DcId, AuthKeyPtr> _keysForWrite;
	base::flat_map<ShiftedDcId, mtpRequestId> _logoutGuestRequestIds;

	rpl::event_stream<> _writeKeysRequests;
	rpl::event_stream<> _allKeysDestroyed;

	// holds dcWithShift for request to this dc or -dc for request to main dc
	std::map<mtpRequestId, ShiftedDcId> _requestsByDc;
	mutable QMutex _requestByDcLock;

	// holds target dcWithShift for auth export request
	std::map<mtpRequestId, ShiftedDcId> _authExportRequests;

	std::map<mtpRequestId, ResponseHandler> _parserMap;
	mutable QMutex _parserMapLock;

	std::map<mtpRequestId, SerializedRequest> _requestMap;
	QReadWriteLock _requestMapLock;

	std::deque<std::pair<mtpRequestId, crl::time>> _delayedRequests;

	std::map<mtpRequestId, int> _requestsDelays;

	std::set<mtpRequestId> _badGuestDcRequests;

	std::map<DcId, std::vector<mtpRequestId>> _authWaiters;

	Fn<void(const Response&)> _updatesHandler;
	Fn<void(const Error&, const Response&)> _globalFailHandler;
	Fn<void(ShiftedDcId shiftedDcId, int32 state)> _stateChangedHandler;
	Fn<void(ShiftedDcId shiftedDcId)> _sessionResetHandler;

	base::Timer _checkDelayedTimer;

	rpl::lifetime _lifetime;

};

Instance::Fields::Fields() = default;

Instance::Fields::Fields(Fields &&other) = default;

auto Instance::Fields::operator=(Fields &&other) -> Fields & = default;

Instance::Fields::~Fields() = default;

Instance::Private::Private(
	not_null<Instance*> instance,
	Instance::Mode mode,
	Fields &&fields)
: Sender(instance)
, _instance(instance)
, _mode(mode)
, _config(std::move(fields.config))
, _networkReachability(base::NetworkReachability::Instance()) {
	Expects(_config != nullptr);

	const auto idealThreadPoolSize = QThread::idealThreadCount();
	_fileSessionThreads.resize(2 * std::max(idealThreadPoolSize / 2, 1));

	details::unpaused(
	) | rpl::start_with_next([=] {
		unpaused();
	}, _lifetime);

	_networkReachability->availableChanges(
	) | rpl::start_with_next([=](bool available) {
		restart();
	}, _lifetime);

	_deviceModel = std::move(fields.deviceModel);
	_systemVersion = std::move(fields.systemVersion);

	for (auto &key : fields.keys) {
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
		addDc(shiftedDcId, std::move(key));
	}

	if (fields.mainDcId != Fields::kNotSetMainDc) {
		_mainDcId = fields.mainDcId;
		_mainDcIdForced = true;
	}
}

void Instance::Private::start() {
	if (isKeysDestroyer()) {
		for (const auto &[shiftedDcId, dc] : _dcenters) {
			startSession(shiftedDcId);
		}
	} else if (_mainDcId != Fields::kNoneMainDc) {
		_mainSession = startSession(_mainDcId);
	}

	_checkDelayedTimer.setCallback([this] { checkDelayedRequests(); });

	Assert((_mainDcId == Fields::kNoneMainDc) == isKeysDestroyer());
	requestConfig();
}

void Instance::Private::resolveProxyDomain(const QString &host) {
	if (!_domainResolver) {
		_domainResolver = std::make_unique<DomainResolver>([=](
				const QString &host,
				const QStringList &ips,
				crl::time expireAt) {
			applyDomainIps(host, ips, expireAt);
		});
	}
	_domainResolver->resolve(host);
}

void Instance::Private::applyDomainIps(
		const QString &host,
		const QStringList &ips,
		crl::time expireAt) {
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
		for (const auto &ip : std::as_const(copy)) {
			proxy.resolvedIPs.push_back(ip);
		}
		return true;
	};
	for (auto &proxy : Global::RefProxiesList()) {
		applyToProxy(proxy);
	}
	if (applyToProxy(Global::RefSelectedProxy())
		&& (Global::ProxySettings() == ProxyData::Settings::Enabled)) {
		for (const auto &[shiftedDcId, session] : _sessions) {
			session->refreshOptions();
		}
	}
	_instance->proxyDomainResolved(host, ips, expireAt);
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
		Core::App().refreshGlobalProxy();
	}
}

void Instance::Private::suggestMainDcId(DcId mainDcId) {
	if (!_mainDcIdForced) {
		setMainDcId(mainDcId);
	}
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
	_writeKeysRequests.fire({});
}

DcId Instance::Private::mainDcId() const {
	Expects(_mainDcId != Fields::kNoneMainDc);

	return _mainDcId;
}

void Instance::Private::requestConfig() {
	if (_configLoader || isKeysDestroyer()) {
		return;
	}
	_configLoader = std::make_unique<ConfigLoader>(
		_instance,
		_userPhone,
		[=](const MTPConfig &result) { configLoadDone(result); },
		[=](const Error &error, const Response &) {
			return configLoadFail(error);
		});
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
		Core::App().badMtprotoConfigurationError();
	}
}

void Instance::Private::syncHttpUnixtime() {
	if (base::unixtime::http_valid() || _httpUnixtimeLoader) {
		return;
	}
	_httpUnixtimeLoader = std::make_unique<SpecialConfigRequest>([=] {
		InvokeQueued(_instance, [=] {
			_httpUnixtimeLoader = nullptr;
		});
	}, configValues().txtDomainString);
}

void Instance::Private::restartedByTimeout(ShiftedDcId shiftedDcId) {
	_restartsByTimeout.fire_copy(shiftedDcId);
}

rpl::producer<ShiftedDcId> Instance::Private::restartsByTimeout() const {
	return _restartsByTimeout.events();
}

void Instance::Private::requestConfigIfOld() {
	const auto timeout = _config->values().blockedMode
		? kConfigBecomesOldForBlockedIn
		: kConfigBecomesOldIn;
	if (crl::now() - _lastConfigLoadedTime >= timeout) {
		requestConfig();
	}
}

void Instance::Private::requestConfigIfExpired() {
	const auto requestIn = (_configExpiresAt - crl::now());
	if (requestIn > 0) {
		base::call_delayed(
			std::min(requestIn, 3600 * crl::time(1000)),
			_instance,
			[=] { requestConfigIfExpired(); });
	} else {
		requestConfig();
	}
}

void Instance::Private::requestCDNConfig() {
	if (_cdnConfigLoadRequestId || _mainDcId == Fields::kNoneMainDc) {
		return;
	}
	_cdnConfigLoadRequestId = request(
		MTPhelp_GetCdnConfig()
	).done([this](const MTPCdnConfig &result) {
		_cdnConfigLoadRequestId = 0;
		result.match([&](const MTPDcdnConfig &data) {
			dcOptions().setCDNConfig(data);
		});
		Local::writeSettings();
	}).send();
}

void Instance::Private::restart() {
	for (const auto &[shiftedDcId, session] : _sessions) {
		session->restart();
	}
}

void Instance::Private::restart(ShiftedDcId shiftedDcId) {
	const auto dcId = BareDcId(shiftedDcId);
	for (const auto &[shiftedDcId, session] : _sessions) {
		if (BareDcId(shiftedDcId) == dcId) {
			session->restart();
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

	if (const auto session = findSession(shiftedDcId)) {
		return session->getState();
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

	if (const auto session = findSession(shiftedDcId)) {
		return session->transport();
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

	QMutexLocker locker(&_parserMapLock);
	_parserMap.erase(requestId);
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
	const auto checkIfMainAndKill = [&](ShiftedDcId shiftedDcId) {
		if (const auto removed = removeSession(shiftedDcId)) {
			return (removed == _mainSession);
		}
		return false;
	};
	if (checkIfMainAndKill(shiftedDcId)) {
		checkIfMainAndKill(_mainDcId);
		_mainSession = startSession(_mainDcId);
	}
	InvokeQueued(_instance, [=] {
		_sessionsToDestroy.clear();
	});
}

void Instance::Private::stopSession(ShiftedDcId shiftedDcId) {
	if (const auto session = findSession(shiftedDcId)) {
		if (session != _mainSession) { // don't stop main session
			session->stop();
		}
	}
}

void Instance::Private::reInitConnection(DcId dcId) {
	for (const auto &[shiftedDcId, session] : _sessions) {
		if (BareDcId(shiftedDcId) == dcId) {
			session->reInitConnection();
		}
	}
}

void Instance::Private::logout(Fn<void()> done) {
	_instance->send(MTPauth_LogOut(), [=](Response) {
		done();
		return true;
	}, [=](const Error&, Response) {
		done();
		return true;
	});
	logoutGuestDcs();
}

void Instance::Private::logoutGuestDcs() {
	auto dcIds = std::vector<DcId>();
	dcIds.reserve(_keysForWrite.size());
	for (const auto &key : _keysForWrite) {
		dcIds.push_back(key.first);
	}
	for (const auto dcId : dcIds) {
		if (dcId == mainDcId() || dcOptions().dcType(dcId) == DcType::Cdn) {
			continue;
		}
		const auto shiftedDcId = MTP::logoutDcId(dcId);
		const auto requestId = _instance->send(MTPauth_LogOut(), [=](
				const Response &response) {
			logoutGuestDone(response.requestId);
			return true;
		}, [=](const Error &, const Response &response) {
			logoutGuestDone(response.requestId);
			return true;
		}, shiftedDcId);
		_logoutGuestRequestIds.emplace(shiftedDcId, requestId);
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

Dcenter *Instance::Private::findDc(ShiftedDcId shiftedDcId) {
	const auto i = _dcenters.find(shiftedDcId);
	return (i != _dcenters.end()) ? i->second.get() : nullptr;
}

not_null<Dcenter*> Instance::Private::addDc(
		ShiftedDcId shiftedDcId,
		AuthKeyPtr &&key) {
	const auto dcId = BareDcId(shiftedDcId);
	return _dcenters.emplace(
		shiftedDcId,
		std::make_unique<Dcenter>(dcId, std::move(key))
	).first->second.get();
}

void Instance::Private::removeDc(ShiftedDcId shiftedDcId) {
	const auto i = _dcenters.find(shiftedDcId);
	if (i != _dcenters.end()) {
		_dcentersToDestroy.push_back(std::move(i->second));
		_dcenters.erase(i);
	}
}

not_null<Dcenter*> Instance::Private::getDcById(
		ShiftedDcId shiftedDcId) {
	if (const auto result = findDc(shiftedDcId)) {
		return result;
	}
	const auto dcId = [&] {
		const auto result = BareDcId(shiftedDcId);
		if (isTemporaryDcId(result)) {
			if (const auto realDcId = getRealIdFromTemporaryDcId(result)) {
				return realDcId;
			}
		}
		return result;
	}();
	if (dcId != shiftedDcId) {
		if (const auto result = findDc(dcId)) {
			return result;
		}
	}
	return addDc(dcId);
}

void Instance::Private::dcPersistentKeyChanged(
		DcId dcId,
		const AuthKeyPtr &persistentKey) {
	dcTemporaryKeyChanged(dcId);

	if (isTemporaryDcId(dcId)) {
		return;
	}

	const auto i = _keysForWrite.find(dcId);
	if (i != _keysForWrite.end() && i->second == persistentKey) {
		return;
	} else if (i == _keysForWrite.end() && !persistentKey) {
		return;
	}
	if (!persistentKey) {
		_keysForWrite.erase(i);
	} else if (i != _keysForWrite.end()) {
		i->second = persistentKey;
	} else {
		_keysForWrite.emplace(dcId, persistentKey);
	}
	DEBUG_LOG(("AuthKey Info: writing auth keys, called by dc %1"
		).arg(dcId));
	_writeKeysRequests.fire({});
}

void Instance::Private::dcTemporaryKeyChanged(DcId dcId) {
	_dcTemporaryKeyChanged.fire_copy(dcId);
}

rpl::producer<DcId> Instance::Private::dcTemporaryKeyChanged() const {
	return _dcTemporaryKeyChanged.events();
}

AuthKeysList Instance::Private::getKeysForWrite() const {
	auto result = AuthKeysList();

	result.reserve(_keysForWrite.size());
	for (const auto &key : _keysForWrite) {
		result.push_back(key.second);
	}
	return result;
}

void Instance::Private::addKeysForDestroy(AuthKeysList &&keys) {
	Expects(isKeysDestroyer());

	for (auto &key : keys) {
		const auto dcId = key->dcId();
		auto shiftedDcId = MTP::destroyKeyNextDcId(dcId);

		// There could be several keys for one dc if we're destroying them.
		// Place them all in separate shiftedDcId so that they won't conflict.
		while (_keysForWrite.find(shiftedDcId) != _keysForWrite.cend()) {
			shiftedDcId = MTP::destroyKeyNextDcId(shiftedDcId);
		}
		_keysForWrite[shiftedDcId] = key;

		addDc(shiftedDcId, std::move(key));
		startSession(shiftedDcId);
	}
}

rpl::producer<> Instance::Private::allKeysDestroyed() const {
	return _allKeysDestroyed.events();
}

rpl::producer<> Instance::Private::writeKeysRequests() const {
	return _writeKeysRequests.events();
}

Config &Instance::Private::config() const {
	return *_config;
}

const ConfigFields &Instance::Private::configValues() const {
	return _config->values();
}

DcOptions &Instance::Private::dcOptions() const {
	return _config->dcOptions();
}

Environment Instance::Private::environment() const {
	return _config->environment();
}

bool Instance::Private::isTestMode() const {
	return _config->isTestMode();
}

QString Instance::Private::deviceModel() const {
	return _deviceModel;
}

QString Instance::Private::systemVersion() const {
	return _systemVersion;
}

void Instance::Private::unpaused() {
	for (const auto &[shiftedDcId, session] : _sessions) {
		session->unpaused();
	}
}

void Instance::Private::configLoadDone(const MTPConfig &result) {
	Expects(result.type() == mtpc_config);

	_configLoader.reset();
	_lastConfigLoadedTime = crl::now();

	const auto &data = result.c_config();
	_config->apply(data);

	const auto lang = qs(data.vsuggested_lang_code().value_or_empty());
	Lang::CurrentCloudManager().setSuggestedLanguage(lang);
	Lang::CurrentCloudManager().setCurrentVersions(
		data.vlang_pack_version().value_or_empty(),
		data.vbase_lang_pack_version().value_or_empty());
	if (const auto prefix = data.vautoupdate_url_prefix()) {
		Local::writeAutoupdatePrefix(qs(*prefix));
	}

	_configExpiresAt = crl::now()
		+ (data.vexpires().v - base::unixtime::now()) * crl::time(1000);
	requestConfigIfExpired();
}

bool Instance::Private::configLoadFail(const Error &error) {
	if (IsDefaultHandledError(error)) return false;

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
	auto now = crl::now();
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

		auto request = SerializedRequest();
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
		SerializedRequest &&request,
		ResponseHandler &&callbacks,
		ShiftedDcId shiftedDcId,
		crl::time msCanWait,
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
	request->lastSentTime = crl::now();
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
		const SerializedRequest &request,
		ResponseHandler &&callbacks) {
	if (callbacks.done || callbacks.fail) {
		QMutexLocker locker(&_parserMapLock);
		_parserMap.emplace(requestId, std::move(callbacks));
	}
	{
		QWriteLocker locker(&_requestMapLock);
		_requestMap.emplace(requestId, request);
	}
}

SerializedRequest Instance::Private::getRequest(mtpRequestId requestId) {
	auto result = SerializedRequest();
	{
		QReadLocker locker(&_requestMapLock);
		auto it = _requestMap.find(requestId);
		if (it != _requestMap.cend()) {
			result = it->second;
		}
	}
	return result;
}

bool Instance::Private::hasCallback(mtpRequestId requestId) const {
	QMutexLocker locker(&_parserMapLock);
	auto it = _parserMap.find(requestId);
	return (it != _parserMap.cend());
}

void Instance::Private::processCallback(const Response &response) {
	const auto requestId = response.requestId;
	ResponseHandler handler;
	{
		QMutexLocker locker(&_parserMapLock);
		auto it = _parserMap.find(requestId);
		if (it != _parserMap.cend()) {
			handler = std::move(it->second);
			_parserMap.erase(it);

			DEBUG_LOG(("RPC Info: found parser for request %1, trying to parse response...").arg(requestId));
		}
	}
	if (handler.done || handler.fail) {
		const auto handleError = [&](const Error &error) {
			DEBUG_LOG(("RPC Info: "
				"error received, code %1, type %2, description: %3").arg(
					QString::number(error.code()),
					error.type(),
					error.description()));
			if (rpcErrorOccured(response, handler, error)) {
				unregisterRequest(requestId);
			} else {
				QMutexLocker locker(&_parserMapLock);
				_parserMap.emplace(requestId, std::move(handler));
			}
		};

		auto from = response.reply.constData();
		if (response.reply.isEmpty()) {
			handleError(Error::Local(
				"RESPONSE_PARSE_FAILED",
				"Empty response."));
		} else if (*from == mtpc_rpc_error) {
			auto error = MTPRpcError();
			handleError(
				Error(error.read(from, from + response.reply.size())
					? error
					: Error::MTPLocal(
						"RESPONSE_PARSE_FAILED",
						"Error parse failed.")));
		} else {
			if (handler.done && !handler.done(response)) {
				handleError(Error::Local(
					"RESPONSE_PARSE_FAILED",
					"Response parse failed."));
			}
			unregisterRequest(requestId);
		}
	} else {
		DEBUG_LOG(("RPC Info: parser not found for %1").arg(requestId));
		unregisterRequest(requestId);
	}
}

void Instance::Private::processUpdate(const Response &message) {
	if (_updatesHandler) {
		_updatesHandler(message);
	}
}

void Instance::Private::onStateChange(ShiftedDcId dcWithShift, int32 state) {
	if (_stateChangedHandler) {
		_stateChangedHandler(dcWithShift, state);
	}
}

void Instance::Private::onSessionReset(ShiftedDcId dcWithShift) {
	if (_sessionResetHandler) {
		_sessionResetHandler(dcWithShift);
	}
}

bool Instance::Private::rpcErrorOccured(
		const Response &response,
		const FailHandler &onFail,
		const Error &error) { // return true if need to clean request data
	if (IsDefaultHandledError(error)) {
		if (onFail && onFail(error, response)) {
			return true;
		}
	}

	if (onErrorDefault(error, response)) {
		return false;
	}
	LOG(("RPC Error: request %1 got fail with code %2, error %3%4").arg(
		QString::number(response.requestId),
		QString::number(error.code()),
		error.type(),
		error.description().isEmpty()
			? QString()
			: QString(": %1").arg(error.description())));
	if (onFail) {
		onFail(error, response);
	}
	return true;
}

void Instance::Private::importDone(
		const MTPauth_Authorization &result,
		const Response &response) {
	const auto shiftedDcId = queryRequestByDc(response.requestId);
	if (!shiftedDcId) {
		LOG(("MTP Error: "
			"auth import request not found in requestsByDC, requestId: %1"
			).arg(response.requestId));
		//
		// Don't log out on export/import problems, perhaps this is a server side error.
		//
		//const auto error = Error::Local(
		//	"AUTH_IMPORT_FAIL",
		//	QString("did not find import request in requestsByDC, "
		//		"request %1").arg(requestId));
		//if (_globalFailHandler && hasAuthorization()) {
		//	_globalFailHandler(error, response); // auth failed in main dc
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

bool Instance::Private::importFail(
		const Error &error,
		const Response &response) {
	if (IsDefaultHandledError(error)) {
		return false;
	}

	//
	// Don't log out on export/import problems, perhaps this is a server side error.
	//
	//if (_globalFailHandler && hasAuthorization()) {
	//	_globalFailHandler(error, response); // auth import failed
	//}
	return true;
}

void Instance::Private::exportDone(
		const MTPauth_ExportedAuthorization &result,
		const Response &response) {
	auto it = _authExportRequests.find(response.requestId);
	if (it == _authExportRequests.cend()) {
		LOG(("MTP Error: "
			"auth export request target dcWithShift not found, requestId: %1"
			).arg(response.requestId));
		//
		// Don't log out on export/import problems, perhaps this is a server side error.
		//
		//const auto error = Error::Local(
		//	"AUTH_IMPORT_FAIL",
		//	QString("did not find target dcWithShift, request %1"
		//	).arg(requestId));
		//if (_globalFailHandler && hasAuthorization()) {
		//	_globalFailHandler(error, response); // auth failed in main dc
		//}
		return;
	}

	auto &data = result.c_auth_exportedAuthorization();
	_instance->send(MTPauth_ImportAuthorization(
		data.vid(),
		data.vbytes()
	), [this](const Response &response) {
		auto result = MTPauth_Authorization();
		auto from = response.reply.constData();
		if (!result.read(from, from + response.reply.size())) {
			return false;
		}
		importDone(result, response);
		return true;
	}, [this](const Error &error, const Response &response) {
		return importFail(error, response);
	}, it->second);
	_authExportRequests.erase(response.requestId);
}

bool Instance::Private::exportFail(
		const Error &error,
		const Response &response) {
	if (IsDefaultHandledError(error)) {
		return false;
	}

	auto it = _authExportRequests.find(response.requestId);
	if (it != _authExportRequests.cend()) {
		_authWaiters[BareDcId(it->second)].clear();
	}
	//
	// Don't log out on export/import problems, perhaps this is a server side error.
	//
	//if (_globalFailHandler && hasAuthorization()) {
	//	_globalFailHandler(error, response); // auth failed in main dc
	//}
	return true;
}

bool Instance::Private::onErrorDefault(
		const Error &error,
		const Response &response) {
	const auto requestId = response.requestId;
	const auto &type = error.type();
	const auto code = error.code();
	if (!IsFloodError(error) && type != qstr("AUTH_KEY_UNREGISTERED")) {
		int breakpoint = 0;
	}
	auto badGuestDc = (code == 400) && (type == qsl("FILE_ID_INVALID"));
	QRegularExpressionMatch m;
	if ((m = QRegularExpression("^(FILE|PHONE|NETWORK|USER)_MIGRATE_(\\d+)$").match(type)).hasMatch()) {
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
			if (false/* && hasAuthorization() && _authExportRequests.find(requestId) == _authExportRequests.cend()*/) {
				//
				// migrate not supported at this moment
				// this was not tested even once
				//
				//DEBUG_LOG(("MTP Info: importing auth to dc %1").arg(newdcWithShift));
				//auto &waiters(_authWaiters[newdcWithShift]);
				//if (waiters.empty()) {
				//	auto exportRequestId = _instance->send(MTPauth_ExportAuthorization(
				//		MTP_int(newdcWithShift)
				//	), [this](const Response &response) {
				//		auto result = MTPauth_ExportedAuthorization();
				//		auto from = response.reply.constData();
				//		if (!result.read(from, from + response.reply.size())) {
				//			return false;
				//		}
				//		exportDone(result, response);
				//		return true;
				//	}, [this](const Error &error, const Response &response) {
				//		return exportFail(error, response);
				//	});
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

		auto request = SerializedRequest();
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
	} else if (code < 0 || code >= 500 || (m = QRegularExpression("^FLOOD_WAIT_(\\d+)$").match(type)).hasMatch()) {
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
		auto sendAt = crl::now() + secs * 1000 + 10;
		auto it = _delayedRequests.begin(), e = _delayedRequests.end();
		for (; it != e; ++it) {
			if (it->first == requestId) return true;
			if (it->second > sendAt) break;
		}
		_delayedRequests.insert(it, std::make_pair(requestId, sendAt));

		checkDelayedRequests();

		return true;
	} else if ((code == 401 && type != qstr("AUTH_KEY_PERM_EMPTY"))
		|| (badGuestDc && _badGuestDcRequests.find(requestId) == _badGuestDcRequests.cend())) {
		auto dcWithShift = ShiftedDcId(0);
		if (const auto shiftedDcId = queryRequestByDc(requestId)) {
			dcWithShift = *shiftedDcId;
		} else {
			LOG(("MTP Error: unauthorized request without dc info, requestId %1").arg(requestId));
		}
		auto newdc = BareDcId(qAbs(dcWithShift));
		if (!newdc || newdc == mainDcId()) {
			if (!badGuestDc && _globalFailHandler) {
				_globalFailHandler(error, response); // auth failed in main dc
			}
			return false;
		}

		DEBUG_LOG(("MTP Info: importing auth to dcWithShift %1"
			).arg(dcWithShift));
		auto &waiters(_authWaiters[newdc]);
		if (!waiters.size()) {
			auto exportRequestId = _instance->send(MTPauth_ExportAuthorization(
				MTP_int(newdc)
			), [this](const Response &response) {
				auto result = MTPauth_ExportedAuthorization();
				auto from = response.reply.constData();
				if (!result.read(from, from + response.reply.size())) {
					return false;
				}
				exportDone(result, response);
				return true;
			}, [this](const Error &error, const Response &response) {
				return exportFail(error, response);
			});
			_authExportRequests.emplace(exportRequestId, abs(dcWithShift));
		}
		waiters.push_back(requestId);
		if (badGuestDc) _badGuestDcRequests.insert(requestId);
		return true;
	} else if (type == qstr("CONNECTION_NOT_INITED")
		|| type == qstr("CONNECTION_LAYER_INVALID")) {
		SerializedRequest request;
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
	} else if (type == qstr("CONNECTION_LANG_CODE_INVALID")) {
		Lang::CurrentCloudManager().resetToDefault();
	} else if (type == qstr("MSG_WAIT_FAILED")) {
		SerializedRequest request;
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
					request->after = SerializedRequest();
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

not_null<Session*> Instance::Private::getSession(
		ShiftedDcId shiftedDcId) {
	if (!shiftedDcId) {
		Assert(_mainSession != nullptr);
		return _mainSession;
	} else if (!BareDcId(shiftedDcId)) {
		Assert(_mainSession != nullptr);
		shiftedDcId += BareDcId(_mainSession->getDcWithShift());
	}

	if (const auto session = findSession(shiftedDcId)) {
		return session;
	}
	return startSession(shiftedDcId);
}

rpl::lifetime &Instance::Private::lifetime() {
	return _lifetime;
}

Session *Instance::Private::findSession(ShiftedDcId shiftedDcId) {
	const auto i = _sessions.find(shiftedDcId);
	return (i != _sessions.end()) ? i->second.get() : nullptr;
}

not_null<Session*> Instance::Private::startSession(ShiftedDcId shiftedDcId) {
	Expects(BareDcId(shiftedDcId) != 0);

	const auto dc = getDcById(shiftedDcId);
	const auto thread = getThreadForDc(shiftedDcId);
	const auto result = _sessions.emplace(
		shiftedDcId,
		std::make_unique<Session>(_instance, thread, shiftedDcId, dc)
	).first->second.get();
	if (isKeysDestroyer()) {
		scheduleKeyDestroy(shiftedDcId);
	}

	return result;
}

Session *Instance::Private::removeSession(ShiftedDcId shiftedDcId) {
	const auto i = _sessions.find(shiftedDcId);
	if (i == _sessions.cend()) {
		return nullptr;
	}
	i->second->kill();
	_sessionsToDestroy.push_back(std::move(i->second));
	_sessions.erase(i);
	return _sessionsToDestroy.back().get();
}


not_null<QThread*> Instance::Private::getThreadForDc(
		ShiftedDcId shiftedDcId) {
	static const auto EnsureStarted = [](
			std::unique_ptr<QThread> &thread,
			auto name) {
		if (!thread) {
			thread = std::make_unique<QThread>();
			thread->setObjectName(name());
			thread->start();
		}
		return thread.get();
	};
	static const auto FindOne = [](
			std::vector<std::unique_ptr<QThread>> &threads,
			const char *prefix,
			int index,
			bool shift) {
		Expects(!threads.empty());
		Expects(!(threads.size() % 2));

		const auto count = int(threads.size());
		index %= count;
		if (index >= count / 2) {
			index = (count - 1) - (index - count / 2);
		}
		if (shift) {
			index = (index + count / 2) % count;
		}
		return EnsureStarted(threads[index], [=] {
			return QString("MTP %1 Session (%2)").arg(prefix).arg(index);
		});
	};
	if (shiftedDcId == BareDcId(shiftedDcId)) {
		return EnsureStarted(_mainSessionThread, [] {
			return QString("MTP Main Session");
		});
	} else if (isDownloadDcId(shiftedDcId)) {
		const auto index = GetDcIdShift(shiftedDcId) - kBaseDownloadDcShift;
		const auto composed = index + BareDcId(shiftedDcId);
		return FindOne(_fileSessionThreads, "Download", composed, false);
	} else if (isUploadDcId(shiftedDcId)) {
		const auto index = GetDcIdShift(shiftedDcId) - kBaseUploadDcShift;
		const auto composed = index + BareDcId(shiftedDcId);
		return FindOne(_fileSessionThreads, "Upload", composed, true);
	}
	return EnsureStarted(_otherSessionsThread, [] {
		return QString("MTP Other Session");
	});
}

void Instance::Private::scheduleKeyDestroy(ShiftedDcId shiftedDcId) {
	Expects(isKeysDestroyer());

	if (dcOptions().dcType(shiftedDcId) == DcType::Cdn) {
		performKeyDestroy(shiftedDcId);
	} else {
		_instance->send(MTPauth_LogOut(), [=](const Response &) {
			performKeyDestroy(shiftedDcId);
			return true;
		}, [=](const Error &error, const Response &) {
			if (IsDefaultHandledError(error)) {
				return false;
			}
			performKeyDestroy(shiftedDcId);
			return true;
		}, shiftedDcId);
	}
}

void Instance::Private::keyWasPossiblyDestroyed(ShiftedDcId shiftedDcId) {
	Expects(isKeysDestroyer());

	InvokeQueued(_instance, [=] {
		LOG(("MTP Info: checkIfKeyWasDestroyed on destroying key %1, "
			"assuming it is destroyed.").arg(shiftedDcId));
		completedKeyDestroy(shiftedDcId);
	});
}

void Instance::Private::performKeyDestroy(ShiftedDcId shiftedDcId) {
	Expects(isKeysDestroyer());

	_instance->send(MTPDestroy_auth_key(), [=](const Response &response) {
		auto result = MTPDestroyAuthKeyRes();
		auto from = response.reply.constData();
		if (!result.read(from, from + response.reply.size())) {
			return false;
		}
		result.match([&](const MTPDdestroy_auth_key_ok &) {
			LOG(("MTP Info: key %1 destroyed.").arg(shiftedDcId));
		}, [&](const MTPDdestroy_auth_key_fail &) {
			LOG(("MTP Error: key %1 destruction fail, leave it for now."
				).arg(shiftedDcId));
			killSession(shiftedDcId);
		}, [&](const MTPDdestroy_auth_key_none &) {
			LOG(("MTP Info: key %1 already destroyed.").arg(shiftedDcId));
		});
		_instance->keyWasPossiblyDestroyed(shiftedDcId);
		return true;
	}, [=](const Error &error, const Response &response) {
		LOG(("MTP Error: key %1 destruction resulted in error: %2"
			).arg(shiftedDcId).arg(error.type()));
		_instance->keyWasPossiblyDestroyed(shiftedDcId);
		return true;
	}, shiftedDcId);
}

void Instance::Private::completedKeyDestroy(ShiftedDcId shiftedDcId) {
	Expects(isKeysDestroyer());

	removeDc(shiftedDcId);
	_keysForWrite.erase(shiftedDcId);
	killSession(shiftedDcId);
	if (_dcenters.empty()) {
		_allKeysDestroyed.fire({});
	}
}

void Instance::Private::keyDestroyedOnServer(
		ShiftedDcId shiftedDcId,
		uint64 keyId) {
	LOG(("Destroying key for dc: %1").arg(shiftedDcId));
	if (const auto dc = findDc(BareDcId(shiftedDcId))) {
		if (dc->destroyConfirmedForgottenKey(keyId)) {
			LOG(("Key destroyed!"));
			dcPersistentKeyChanged(BareDcId(shiftedDcId), nullptr);
		} else {
			LOG(("Key already is different."));
		}
	}
	restart(shiftedDcId);
}

void Instance::Private::setUpdatesHandler(
		Fn<void(const Response&)> handler) {
	_updatesHandler = std::move(handler);
}

void Instance::Private::setGlobalFailHandler(
		Fn<void(const Error&, const Response&)> handler) {
	_globalFailHandler = std::move(handler);
}

void Instance::Private::setStateChangedHandler(
		Fn<void(ShiftedDcId shiftedDcId, int32 state)> handler) {
	_stateChangedHandler = std::move(handler);
}

void Instance::Private::setSessionResetHandler(
		Fn<void(ShiftedDcId shiftedDcId)> handler) {
	_sessionResetHandler = std::move(handler);
}

void Instance::Private::clearGlobalHandlers() {
	setUpdatesHandler(nullptr);
	setGlobalFailHandler(nullptr);
	setStateChangedHandler(nullptr);
	setSessionResetHandler(nullptr);
}

void Instance::Private::prepareToDestroy() {
	// It accesses Instance in destructor, so it should be destroyed first.
	_configLoader.reset();

	requestCancellingDiscard();

	for (const auto &[shiftedDcId, session] : base::take(_sessions)) {
		session->kill();
	}
	_mainSession = nullptr;

	auto threads = std::vector<std::unique_ptr<QThread>>();
	threads.push_back(base::take(_mainSessionThread));
	threads.push_back(base::take(_otherSessionsThread));
	for (auto &thread : base::take(_fileSessionThreads)) {
		threads.push_back(std::move(thread));
	}
	for (const auto &thread : threads) {
		if (thread) {
			thread->quit();
		}
	}
	for (const auto &thread : threads) {
		if (thread) {
			thread->wait();
		}
	}
}

Instance::Instance(Mode mode, Fields &&fields)
: QObject()
, _private(std::make_unique<Private>(this, mode, std::move(fields))) {
	_private->start();
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
	return Lang::GetInstance().systemLangCode();
}

QString Instance::cloudLangCode() const {
	return Lang::GetInstance().cloudLangCode(Lang::Pack::Current);
}

QString Instance::langPackName() const {
	return Lang::GetInstance().langPackName();
}

rpl::producer<> Instance::writeKeysRequests() const {
	return _private->writeKeysRequests();
}

rpl::producer<> Instance::allKeysDestroyed() const {
	return _private->allKeysDestroyed();
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

void Instance::syncHttpUnixtime() {
	_private->syncHttpUnixtime();
}

void Instance::restartedByTimeout(ShiftedDcId shiftedDcId) {
	_private->restartedByTimeout(shiftedDcId);
}

rpl::producer<ShiftedDcId> Instance::restartsByTimeout() const {
	return _private->restartsByTimeout();
}

void Instance::requestConfigIfOld() {
	_private->requestConfigIfOld();
}

void Instance::requestCDNConfig() {
	_private->requestCDNConfig();
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

void Instance::logout(Fn<void()> done) {
	_private->logout(std::move(done));
}

void Instance::dcPersistentKeyChanged(
		DcId dcId,
		const AuthKeyPtr &persistentKey) {
	_private->dcPersistentKeyChanged(dcId, persistentKey);
}

void Instance::dcTemporaryKeyChanged(DcId dcId) {
	_private->dcTemporaryKeyChanged(dcId);
}

rpl::producer<DcId> Instance::dcTemporaryKeyChanged() const {
	return _private->dcTemporaryKeyChanged();
}

AuthKeysList Instance::getKeysForWrite() const {
	return _private->getKeysForWrite();
}

void Instance::addKeysForDestroy(AuthKeysList &&keys) {
	_private->addKeysForDestroy(std::move(keys));
}

Config &Instance::config() const {
	return _private->config();
}

const ConfigFields &Instance::configValues() const {
	return _private->configValues();
}

DcOptions &Instance::dcOptions() const {
	return _private->dcOptions();
}

Environment Instance::environment() const {
	return _private->environment();
}

bool Instance::isTestMode() const {
	return _private->isTestMode();
}

QString Instance::deviceModel() const {
	return _private->deviceModel();
}

QString Instance::systemVersion() const {
	return _private->systemVersion();
}

void Instance::setUpdatesHandler(Fn<void(const Response&)> handler) {
	_private->setUpdatesHandler(std::move(handler));
}

void Instance::setGlobalFailHandler(
		Fn<void(const Error&, const Response&)> handler) {
	_private->setGlobalFailHandler(std::move(handler));
}

void Instance::setStateChangedHandler(
		Fn<void(ShiftedDcId shiftedDcId, int32 state)> handler) {
	_private->setStateChangedHandler(std::move(handler));
}

void Instance::setSessionResetHandler(
		Fn<void(ShiftedDcId shiftedDcId)> handler) {
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

bool Instance::hasCallback(mtpRequestId requestId) const {
	return _private->hasCallback(requestId);
}

void Instance::processCallback(const Response &response) {
	_private->processCallback(response);
}

void Instance::processUpdate(const Response &message) {
	_private->processUpdate(message);
}

bool Instance::rpcErrorOccured(
		const Response &response,
		const FailHandler &onFail,
		const Error &error) {
	return _private->rpcErrorOccured(response, onFail, error);
}

bool Instance::isKeysDestroyer() const {
	return _private->isKeysDestroyer();
}

void Instance::keyWasPossiblyDestroyed(ShiftedDcId shiftedDcId) {
	_private->keyWasPossiblyDestroyed(shiftedDcId);
}

void Instance::keyDestroyedOnServer(ShiftedDcId shiftedDcId, uint64 keyId) {
	_private->keyDestroyedOnServer(shiftedDcId, keyId);
}

void Instance::sendRequest(
		mtpRequestId requestId,
		SerializedRequest &&request,
		ResponseHandler &&callbacks,
		ShiftedDcId shiftedDcId,
		crl::time msCanWait,
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

void Instance::sendAnything(ShiftedDcId shiftedDcId, crl::time msCanWait) {
	_private->getSession(shiftedDcId)->sendAnything(msCanWait);
}

rpl::lifetime &Instance::lifetime() {
	return _private->lifetime();
}

Instance::~Instance() {
	_private->prepareToDestroy();
}

} // namespace MTP
