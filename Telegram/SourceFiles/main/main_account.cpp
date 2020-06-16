/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "main/main_account.h"

#include "core/application.h"
#include "core/launcher.h"
#include "core/shortcuts.h"
#include "storage/storage_account.h"
#include "storage/storage_accounts.h" // Storage::StartResult.
#include "storage/serialize_common.h"
#include "storage/localstorage.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_changes.h"
#include "window/window_controller.h"
#include "media/audio/media_audio.h"
#include "ui/image/image.h"
#include "mainwidget.h"
#include "api/api_updates.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "facades.h"

namespace Main {
namespace {

[[nodiscard]] QString ComposeDataString(const QString &dataName, int index) {
	auto result = dataName;
	result.replace('#', QString());
	if (index > 0) {
		result += '#' + QString::number(index + 1);
	}
	return result;
}

} // namespace

Account::Account(const QString &dataName, int index)
: _local(std::make_unique<Storage::Account>(
	this,
	ComposeDataString(dataName, index))) {
}

Account::~Account() = default;

[[nodiscard]] Storage::StartResult Account::legacyStart(
		const QByteArray &passcode) {
	Expects(!_appConfig);

	const auto result = _local->legacyStart(passcode);
	if (result == Storage::StartResult::Success) {
		finishStarting();
	}
	return result;
}

void Account::start(std::shared_ptr<MTP::AuthKey> localKey) {
	_local->start(std::move(localKey));
	finishStarting();
}

void Account::startAdded(std::shared_ptr<MTP::AuthKey> localKey) {
	_local->startAdded(std::move(localKey));
	finishStarting();
}

void Account::finishStarting() {
	_appConfig = std::make_unique<AppConfig>(this);
	watchProxyChanges();
	watchSessionChanges();
}

void Account::watchProxyChanges() {
	using ProxyChange = Core::Application::ProxyChange;

	Core::App().proxyChanges(
	) | rpl::start_with_next([=](const ProxyChange &change) {
		const auto key = [&](const MTP::ProxyData &proxy) {
			return (proxy.type == MTP::ProxyData::Type::Mtproto)
				? std::make_pair(proxy.host, proxy.port)
				: std::make_pair(QString(), uint32(0));
		};
		if (_mtp) {
			_mtp->restart();
			if (key(change.was) != key(change.now)) {
				_mtp->reInitConnection(_mtp->mainDcId());
			}
		}
		if (_mtpForKeysDestroy) {
			_mtpForKeysDestroy->restart();
		}
	}, _lifetime);
}

void Account::watchSessionChanges() {
	sessionChanges(
	) | rpl::start_with_next([=](Session *session) {
		if (!session && _mtp) {
			_mtp->setUserPhone(QString());
		}
	}, _lifetime);
}

UserId Account::willHaveUserId() const {
	return _sessionUserId;
}

void Account::createSession(const MTPUser &user) {
	createSession(user, QByteArray(), 0, Settings());
}

void Account::createSession(
		UserId id,
		QByteArray serialized,
		int streamVersion,
		Settings &&settings) {
	DEBUG_LOG(("sessionUserSerialized.size: %1").arg(serialized.size()));
	QDataStream peekStream(serialized);
	const auto phone = Serialize::peekUserPhone(streamVersion, peekStream);
	const auto flags = MTPDuser::Flag::f_self | (phone.isEmpty()
		? MTPDuser::Flag()
		: MTPDuser::Flag::f_phone);
	createSession(
		MTP_user(
			MTP_flags(flags),
			MTP_int(base::take(_sessionUserId)),
			MTPlong(), // access_hash
			MTPstring(), // first_name
			MTPstring(), // last_name
			MTPstring(), // username
			MTP_string(phone),
			MTPUserProfilePhoto(),
			MTPUserStatus(),
			MTPint(), // bot_info_version
			MTPVector<MTPRestrictionReason>(),
			MTPstring(), // bot_inline_placeholder
			MTPstring()), // lang_code
		serialized,
		streamVersion,
		std::move(settings));
}

void Account::createSession(
		const MTPUser &user,
		QByteArray serialized,
		int streamVersion,
		Settings &&settings) {
	Expects(_mtp != nullptr);
	Expects(_session == nullptr);
	Expects(_sessionValue.current() == nullptr);

	_session = std::make_unique<Session>(this, user, std::move(settings));
	if (!serialized.isEmpty()) {
		local().readSelf(_session.get(), serialized, streamVersion);
	}
	_sessionValue = _session.get();

	Ensures(_session != nullptr);
}

void Account::destroySession() {
	_storedSettings.reset();
	_sessionUserId = 0;
	_sessionUserSerialized = {};
	if (!sessionExists()) {
		return;
	}

	_sessionValue = nullptr;
	_session = nullptr;

	Notify::unreadCounterUpdated();
}

bool Account::sessionExists() const {
	return (_sessionValue.current() != nullptr);
}

Session &Account::session() const {
	Expects(sessionExists());

	return *_sessionValue.current();
}

rpl::producer<Session*> Account::sessionValue() const {
	return _sessionValue.value();
}

rpl::producer<Session*> Account::sessionChanges() const {
	return _sessionValue.changes();
}

rpl::producer<MTP::Instance*> Account::mtpValue() const {
	return _mtpValue.value();
}

rpl::producer<MTP::Instance*> Account::mtpChanges() const {
	return _mtpValue.changes();
}

rpl::producer<MTPUpdates> Account::mtpUpdates() const {
	return _mtpUpdates.events();
}

rpl::producer<> Account::mtpNewSessionCreated() const {
	return _mtpNewSessionCreated.events();
}

void Account::setLegacyMtpMainDcId(MTP::DcId mainDcId) {
	Expects(!_mtp);

	_mtpConfig.mainDcId = mainDcId;
}

void Account::setLegacyMtpKey(std::shared_ptr<MTP::AuthKey> key) {
	Expects(!_mtp);
	Expects(key != nullptr);

	_mtpConfig.keys.push_back(std::move(key));
}

QByteArray Account::serializeMtpAuthorization() const {
	const auto serialize = [&](
			MTP::DcId mainDcId,
			const MTP::AuthKeysList &keys,
			const MTP::AuthKeysList &keysToDestroy) {
		const auto keysSize = [](auto &list) {
			const auto keyDataSize = MTP::AuthKey::Data().size();
			return sizeof(qint32)
				+ list.size() * (sizeof(qint32) + keyDataSize);
		};
		const auto writeKeys = [](
				QDataStream &stream,
				const MTP::AuthKeysList &keys) {
			stream << qint32(keys.size());
			for (const auto &key : keys) {
				stream << qint32(key->dcId());
				key->write(stream);
			}
		};

		auto result = QByteArray();
		auto size = sizeof(qint32) + sizeof(qint32); // userId + mainDcId
		size += keysSize(keys) + keysSize(keysToDestroy);
		result.reserve(size);
		{
			QDataStream stream(&result, QIODevice::WriteOnly);
			stream.setVersion(QDataStream::Qt_5_1);

			const auto currentUserId = sessionExists()
				? session().userId()
				: 0;
			stream << qint32(currentUserId) << qint32(mainDcId);
			writeKeys(stream, keys);
			writeKeys(stream, keysToDestroy);

			DEBUG_LOG(("MTP Info: Keys written, userId: %1, dcId: %2").arg(currentUserId).arg(mainDcId));
		}
		return result;
	};
	if (_mtp) {
		const auto keys = _mtp->getKeysForWrite();
		const auto keysToDestroy = _mtpForKeysDestroy
			? _mtpForKeysDestroy->getKeysForWrite()
			: MTP::AuthKeysList();
		return serialize(_mtp->mainDcId(), keys, keysToDestroy);
	}
	const auto &keys = _mtpConfig.keys;
	const auto &keysToDestroy = _mtpKeysToDestroy;
	return serialize(_mtpConfig.mainDcId, keys, keysToDestroy);
}

void Account::setSessionUserId(UserId userId) {
	Expects(!sessionExists());

	_sessionUserId = userId;
}

void Account::setSessionFromStorage(
		std::unique_ptr<Settings> data,
		QByteArray &&selfSerialized,
		int32 selfStreamVersion) {
	Expects(!sessionExists());

	DEBUG_LOG(("sessionUserSerialized set: %1"
		).arg(selfSerialized.size()));

	_storedSettings = std::move(data);
	_sessionUserSerialized = std::move(selfSerialized);
	_sessionUserStreamVersion = selfStreamVersion;
}

Settings *Account::getSessionSettings() {
	if (_sessionUserId) {
		return _storedSettings ? _storedSettings.get() : nullptr;
	} else if (sessionExists()) {
		return &session().settings();
	}
	return nullptr;
}

void Account::setMtpAuthorization(const QByteArray &serialized) {
	Expects(!_mtp);

	QDataStream stream(serialized);
	stream.setVersion(QDataStream::Qt_5_1);

	auto userId = Serialize::read<qint32>(stream);
	auto mainDcId = Serialize::read<qint32>(stream);
	if (stream.status() != QDataStream::Ok) {
		LOG(("MTP Error: "
			"Could not read main fields from mtp authorization."));
		return;
	}

	setSessionUserId(userId);
	_mtpConfig.mainDcId = mainDcId;

	const auto readKeys = [&stream](auto &keys) {
		const auto count = Serialize::read<qint32>(stream);
		if (stream.status() != QDataStream::Ok) {
			LOG(("MTP Error: "
				"Could not read keys count from mtp authorization."));
			return;
		}
		keys.reserve(count);
		for (auto i = 0; i != count; ++i) {
			const auto dcId = Serialize::read<qint32>(stream);
			const auto keyData = Serialize::read<MTP::AuthKey::Data>(stream);
			if (stream.status() != QDataStream::Ok) {
				LOG(("MTP Error: "
					"Could not read key from mtp authorization."));
				return;
			}
			keys.push_back(std::make_shared<MTP::AuthKey>(MTP::AuthKey::Type::ReadFromFile, dcId, keyData));
		}
	};
	readKeys(_mtpConfig.keys);
	readKeys(_mtpKeysToDestroy);
	LOG(("MTP Info: "
		"read keys, current: %1, to destroy: %2"
		).arg(_mtpConfig.keys.size()
		).arg(_mtpKeysToDestroy.size()));
}

void Account::startMtp() {
	Expects(!_mtp);

	auto config = base::take(_mtpConfig);
	config.deviceModel = Core::App().launcher()->deviceModel();
	config.systemVersion = Core::App().launcher()->systemVersion();
	_mtp = std::make_unique<MTP::Instance>(
		Core::App().dcOptions(),
		MTP::Instance::Mode::Normal,
		std::move(config));
	_mtp->writeKeysRequests(
	) | rpl::start_with_next([=] {
		local().writeMtpData();
	}, _mtp->lifetime());
	_mtpConfig.mainDcId = _mtp->mainDcId();

	_mtp->setUpdatesHandler(::rpcDone([=](
			const mtpPrime *from,
			const mtpPrime *end) {
		return checkForUpdates(from, end)
			|| checkForNewSession(from, end);
	}));
	_mtp->setGlobalFailHandler(::rpcFail([=](const RPCError &error) {
		if (sessionExists()) {
			crl::on_main(&session(), [=] { logOut(); });
		}
		return true;
	}));
	_mtp->setStateChangedHandler([=](MTP::ShiftedDcId dc, int32 state) {
		if (dc == _mtp->mainDcId()) {
			Global::RefConnectionTypeChanged().notify();
		}
	});
	_mtp->setSessionResetHandler([=](MTP::ShiftedDcId shiftedDcId) {
		if (sessionExists()) {
			if (shiftedDcId == _mtp->mainDcId()) {
				session().updates().getDifference();
			}
		}
	});

	if (!_mtpKeysToDestroy.empty()) {
		destroyMtpKeys(base::take(_mtpKeysToDestroy));
	}

	if (_sessionUserId) {
		createSession(
			_sessionUserId,
			base::take(_sessionUserSerialized),
			base::take(_sessionUserStreamVersion),
			_storedSettings ? std::move(*_storedSettings) : Settings());
	}
	_storedSettings = nullptr;

	if (sessionExists()) {
		// Skip all pending self updates so that we won't local().writeSelf.
		session().changes().sendNotifications();
	}

	_mtpValue = _mtp.get();
}

bool Account::checkForUpdates(const mtpPrime *from, const mtpPrime *end) {
	auto updates = MTPUpdates();
	if (!updates.read(from, end)) {
		return false;
	}
	_mtpUpdates.fire(std::move(updates));
	return true;
}

bool Account::checkForNewSession(const mtpPrime *from, const mtpPrime *end) {
	auto newSession = MTPNewSession();
	if (!newSession.read(from, end)) {
		return false;
	}
	_mtpNewSessionCreated.fire({});
	return true;
}

void Account::logOut() {
	if (_loggingOut) {
		return;
	}
	_loggingOut = true;
	if (_mtp) {
		_mtp->logout([=] { loggedOut(); });
	} else {
		// We log out because we've forgotten passcode.
		loggedOut();
	}
}

bool Account::loggingOut() const {
	return _loggingOut;
}

void Account::forcedLogOut() {
	if (sessionExists()) {
		resetAuthorizationKeys();
		loggedOut();
	}
}

void Account::loggedOut() {
	_loggingOut = false;
	Media::Player::mixer()->stopAndClear();
	Global::SetVoiceMsgPlaybackDoubled(false);
	if (const auto window = Core::App().activeWindow()) {
		window->tempDirDelete(Local::ClearManagerAll);
	}
	destroySession();
	Core::App().unlockTerms();
	local().reset();
	cSetOtherOnline(0);
}

void Account::destroyMtpKeys(MTP::AuthKeysList &&keys) {
	if (keys.empty()) {
		return;
	}
	if (_mtpForKeysDestroy) {
		_mtpForKeysDestroy->addKeysForDestroy(std::move(keys));
		local().writeMtpData();
		return;
	}
	auto destroyConfig = MTP::Instance::Config();
	destroyConfig.mainDcId = MTP::Instance::Config::kNoneMainDc;
	destroyConfig.keys = std::move(keys);
	destroyConfig.deviceModel = Core::App().launcher()->deviceModel();
	destroyConfig.systemVersion = Core::App().launcher()->systemVersion();
	_mtpForKeysDestroy = std::make_unique<MTP::Instance>(
		Core::App().dcOptions(),
		MTP::Instance::Mode::KeysDestroyer,
		std::move(destroyConfig));
	_mtpForKeysDestroy->writeKeysRequests(
	) | rpl::start_with_next([=] {
		local().writeMtpData();
	}, _mtpForKeysDestroy->lifetime());
	_mtpForKeysDestroy->allKeysDestroyed(
	) | rpl::start_with_next([=] {
		LOG(("MTP Info: all keys scheduled for destroy are destroyed."));
		crl::on_main(this, [=] {
			_mtpForKeysDestroy = nullptr;
			local().writeMtpData();
		});
	}, _mtpForKeysDestroy->lifetime());
}

void Account::suggestMainDcId(MTP::DcId mainDcId) {
	Expects(_mtp != nullptr);

	_mtp->suggestMainDcId(mainDcId);
	if (_mtpConfig.mainDcId != MTP::Instance::Config::kNotSetMainDc) {
		_mtpConfig.mainDcId = mainDcId;
	}
}

void Account::destroyStaleAuthorizationKeys() {
	Expects(_mtp != nullptr);

	for (const auto &key : _mtp->getKeysForWrite()) {
		// Disable this for now.
		if (key->type() == MTP::AuthKey::Type::ReadFromFile) {
			_mtpKeysToDestroy = _mtp->getKeysForWrite();
			LOG(("MTP Info: destroying stale keys, count: %1"
				).arg(_mtpKeysToDestroy.size()));
			resetAuthorizationKeys();
			return;
		}
	}
}

void Account::configUpdated() {
	_configUpdates.fire({});
}

rpl::producer<> Account::configUpdates() const {
	return _configUpdates.events();
}

void Account::resetAuthorizationKeys() {
	_mtpValue = nullptr;
	_mtp = nullptr;
	startMtp();
	local().writeMtpData();
}

void Account::clearMtp() {
	_mtpValue = nullptr;
	_mtp = nullptr;
	_mtpForKeysDestroy = nullptr;
}

} // namespace Main
