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
#include "stdafx.h"

#include "mtproto/dcenter.h"

#include "mtproto/facade.h"
#include "localstorage.h"

namespace MTP {
namespace internal {

namespace {
	DcenterMap gDCs;
	bool configLoadedOnce = false;
	bool mainDCChanged = false;
	int32 _mainDC = 2;
	int32 userId = 0;

	typedef QMap<int32, AuthKeyPtr> _KeysMapForWrite;
	_KeysMapForWrite _keysMapForWrite;
	QMutex _keysMapForWriteMutex;
} // namespace

int32 authed() {
	return userId;
}

void authed(int32 uid) {
	if (userId != uid) {
		userId = uid;
	}
}

DcenterMap &DCMap() {
	return gDCs;
}

bool configNeeded() {
	return !configLoadedOnce;
}

int32 mainDC() {
	return _mainDC;
}

namespace {
	QMap<int32, mtpRequestId> logoutGuestMap; // dcWithShift to logout request id
	bool logoutDone(mtpRequestId req) {
		for (QMap<int32, mtpRequestId>::iterator i = logoutGuestMap.begin(); i != logoutGuestMap.end(); ++i) {
			if (i.value() == req) {
				MTP::killSession(i.key());
				logoutGuestMap.erase(i);
				return true;
			}
		}
		return false;
	}
}

void logoutOtherDCs() {
	QList<int32> dcs;
	{
		QMutexLocker lock(&_keysMapForWriteMutex);
		dcs = _keysMapForWrite.keys();
	}
	for (int32 i = 0, cnt = dcs.size(); i != cnt; ++i) {
		if (dcs[i] != MTP::maindc()) {
			logoutGuestMap.insert(MTP::lgtDcId(dcs[i]), MTP::send(MTPauth_LogOut(), rpcDone(&logoutDone), rpcFail(&logoutDone), MTP::lgtDcId(dcs[i])));
		}
	}
}

void setDC(int32 dc, bool firstOnly) {
	if (!dc || (firstOnly && mainDCChanged)) return;
	mainDCChanged = true;
	if (dc != _mainDC) {
		_mainDC = dc;
	}
}

Dcenter::Dcenter(int32 id, const AuthKeyPtr &key) : _id(id), _key(key), _connectionInited(false) {
	connect(this, SIGNAL(authKeyCreated()), this, SLOT(authKeyWrite()), Qt::QueuedConnection);

	QMutexLocker lock(&_keysMapForWriteMutex);
	if (_key) {
		_keysMapForWrite[_id] = _key;
	} else {
		_keysMapForWrite.remove(_id);
	}
}

void Dcenter::authKeyWrite() {
	DEBUG_LOG(("AuthKey Info: MTProtoDC::authKeyWrite() slot, dc %1").arg(_id));
	if (_key) {
		Local::writeMtpData();
	}
}

void Dcenter::setKey(const AuthKeyPtr &key) {
	DEBUG_LOG(("AuthKey Info: MTProtoDC::setKey(%1), emitting authKeyCreated, dc %2").arg(key ? key->keyId() : 0).arg(_id));
	_key = key;
	_connectionInited = false;
	emit authKeyCreated();

	QMutexLocker lock(&_keysMapForWriteMutex);
	if (_key) {
		_keysMapForWrite[_id] = _key;
	} else {
		_keysMapForWrite.remove(_id);
	}
}

QReadWriteLock *Dcenter::keyMutex() const {
	return &keyLock;
}

const AuthKeyPtr &Dcenter::getKey() const {
	return _key;
}

void Dcenter::destroyKey() {
	setKey(AuthKeyPtr());

	QMutexLocker lock(&_keysMapForWriteMutex);
	_keysMapForWrite.remove(_id);
}

namespace {

ConfigLoader *_configLoader = nullptr;
auto loadingConfig = false;

void configLoaded(const MTPConfig &result) {
	loadingConfig = false;

	auto &data = result.c_config();

	DEBUG_LOG(("MTP Info: got config, chat_size_max: %1, date: %2, test_mode: %3, this_dc: %4, dc_options.length: %5").arg(data.vchat_size_max.v).arg(data.vdate.v).arg(mtpIsTrue(data.vtest_mode)).arg(data.vthis_dc.v).arg(data.vdc_options.c_vector().v.size()));

	updateDcOptions(data.vdc_options.c_vector().v);

	Global::SetChatSizeMax(data.vchat_size_max.v);
	Global::SetMegagroupSizeMax(data.vmegagroup_size_max.v);
	Global::SetForwardedCountMax(data.vforwarded_count_max.v);
	Global::SetOnlineUpdatePeriod(data.vonline_update_period_ms.v);
	Global::SetOfflineBlurTimeout(data.voffline_blur_timeout_ms.v);
	Global::SetOfflineIdleTimeout(data.voffline_idle_timeout_ms.v);
	Global::SetOnlineCloudTimeout(data.vonline_cloud_timeout_ms.v);
	Global::SetNotifyCloudDelay(data.vnotify_cloud_delay_ms.v);
	Global::SetNotifyDefaultDelay(data.vnotify_default_delay_ms.v);
	Global::SetChatBigSize(data.vchat_big_size.v); // ?
	Global::SetPushChatPeriod(data.vpush_chat_period_ms.v); // ?
	Global::SetPushChatLimit(data.vpush_chat_limit.v); // ?
	Global::SetSavedGifsLimit(data.vsaved_gifs_limit.v);
	Global::SetEditTimeLimit(data.vedit_time_limit.v); // ?
	Global::SetStickersRecentLimit(data.vstickers_recent_limit.v);
	Global::SetPinnedDialogsCountMax(data.vpinned_dialogs_count_max.v);

	configLoadedOnce = true;
	Local::writeSettings();

	configLoader()->done();
}

bool configFailed(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	loadingConfig = false;
	LOG(("MTP Error: failed to get config!"));
	return false;
}

};

void updateDcOptions(const QVector<MTPDcOption> &options) {
	QSet<int32> already, restart;
	{
		MTP::DcOptions opts;
		{
			QReadLocker lock(dcOptionsMutex());
			opts = Global::DcOptions();
		}
		for (QVector<MTPDcOption>::const_iterator i = options.cbegin(), e = options.cend(); i != e; ++i) {
			const auto &optData(i->c_dcOption());
			int32 id = optData.vid.v, idWithShift = MTP::shiftDcId(id, optData.vflags.v);
			if (already.constFind(idWithShift) == already.cend()) {
				already.insert(idWithShift);
				auto a = opts.constFind(idWithShift);
				if (a != opts.cend()) {
					if (a.value().ip != optData.vip_address.c_string().v || a.value().port != optData.vport.v) {
						restart.insert(id);
					}
				}
				opts.insert(idWithShift, MTP::DcOption(id, optData.vflags.v, optData.vip_address.c_string().v, optData.vport.v));
			}
		}
		{
			QWriteLocker lock(dcOptionsMutex());
			Global::SetDcOptions(opts);
		}
	}
	for (QSet<int32>::const_iterator i = restart.cbegin(), e = restart.cend(); i != e; ++i) {
		MTP::restart(*i);
	}
}

namespace {
	QReadWriteLock _dcOptionsMutex;
}

QReadWriteLock *dcOptionsMutex() {
	return &_dcOptionsMutex;
}

ConfigLoader::ConfigLoader() : _enumCurrent(0), _enumRequest(0) {
	connect(&_enumDCTimer, SIGNAL(timeout()), this, SLOT(enumDC()));
}

void ConfigLoader::load() {
	if (loadingConfig) return;
	loadingConfig = true;

	MTP::send(MTPhelp_GetConfig(), rpcDone(configLoaded), rpcFail(configFailed));

	_enumDCTimer.start(MTPEnumDCTimeout);
}

void ConfigLoader::done() {
	_enumDCTimer.stop();
	if (_enumRequest) {
		MTP::cancel(_enumRequest);
		_enumRequest = 0;
	}
	if (_enumCurrent) {
		MTP::killSession(MTP::cfgDcId(_enumCurrent));
		_enumCurrent = 0;
	}
	emit loaded();
}

void ConfigLoader::enumDC() {
	if (!loadingConfig) return;

	if (_enumRequest) MTP::cancel(_enumRequest);

	if (!_enumCurrent) {
		_enumCurrent = _mainDC;
	} else {
		MTP::killSession(MTP::cfgDcId(_enumCurrent));
	}
	OrderedSet<int32> dcs;
	{
		QReadLocker lock(dcOptionsMutex());
		const auto &options(Global::DcOptions());
		for (auto i = options.cbegin(), e = options.cend(); i != e; ++i) {
			dcs.insert(MTP::bareDcId(i.key()));
		}
	}
	auto i = dcs.constFind(_enumCurrent);
	if (i == dcs.cend() || (++i) == dcs.cend()) {
		_enumCurrent = *dcs.cbegin();
	} else {
		_enumCurrent = *i;
	}
	_enumRequest = MTP::send(MTPhelp_GetConfig(), rpcDone(configLoaded), rpcFail(configFailed), MTP::cfgDcId(_enumCurrent));

	_enumDCTimer.start(MTPEnumDCTimeout);
}

ConfigLoader *configLoader() {
	if (!_configLoader) _configLoader = new ConfigLoader();
	return _configLoader;
}

void destroyConfigLoader() {
	delete _configLoader;
	_configLoader = nullptr;
}

AuthKeysMap getAuthKeys() {
	AuthKeysMap result;
	QMutexLocker lock(&_keysMapForWriteMutex);
	for_const (const AuthKeyPtr &key, _keysMapForWrite) {
		result.push_back(key);
	}
	return result;
}

void setAuthKey(int32 dcId, AuthKeyPtr key) {
	DcenterPtr dc(new Dcenter(dcId, key));
	gDCs.insert(dcId, dc);
}

} // namespace internal
} // namespace MTP
