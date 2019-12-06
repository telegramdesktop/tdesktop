/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/mtproto_auth_key.h"
#include "mtproto/mtp_instance.h"
#include "base/weak_ptr.h"

namespace Main {

class Session;
class Settings;
class AppConfig;

class Account final : public base::has_weak_ptr {
public:
	explicit Account(const QString &dataName);
	~Account();

	Account(const Account &other) = delete;
	Account &operator=(const Account &other) = delete;

	void createSession(const MTPUser &user);
	void createSession(
		UserId id,
		QByteArray serialized,
		int streamVersion,
		Settings &&settings);
	void destroySession();

	void logOut();
	void forcedLogOut();

	[[nodiscard]] AppConfig &appConfig() {
		return *_appConfig;
	}

	[[nodiscard]] bool sessionExists() const;
	[[nodiscard]] Session &session();
	[[nodiscard]] const Session &session() const;
	[[nodiscard]] rpl::producer<Session*> sessionValue() const;
	[[nodiscard]] rpl::producer<Session*> sessionChanges() const;

	[[nodiscard]] MTP::Instance *mtp() {
		return _mtp.get();
	}
	[[nodiscard]] rpl::producer<MTP::Instance*> mtpValue() const;
	[[nodiscard]] rpl::producer<MTP::Instance*> mtpChanges() const;

	// Set from legacy storage.
	void setMtpMainDcId(MTP::DcId mainDcId);
	void setMtpKey(MTP::DcId dcId, const MTP::AuthKey::Data &keyData);
	void setSessionUserId(UserId userId);
	void setSessionFromStorage(
		std::unique_ptr<Settings> data,
		QByteArray &&selfSerialized,
		int32 selfStreamVersion);
	[[nodiscard]] Settings *getSessionSettings();
	[[nodiscard]] rpl::producer<> mtpNewSessionCreated() const;
	[[nodiscard]] rpl::producer<MTPUpdates> mtpUpdates() const;

	// Serialization.
	[[nodiscard]] QByteArray serializeMtpAuthorization() const;
	void setMtpAuthorization(const QByteArray &serialized);

	void startMtp();
	void suggestMainDcId(MTP::DcId mainDcId);
	void destroyStaleAuthorizationKeys();
	void configUpdated();
	[[nodiscard]] rpl::producer<> configUpdates() const;
	void clearMtp();

private:
	void createSession(
		const MTPUser &user,
		QByteArray serialized,
		int streamVersion,
		Settings &&settings);
	void watchProxyChanges();
	void watchSessionChanges();
	bool checkForUpdates(const mtpPrime *from, const mtpPrime *end);
	bool checkForNewSession(const mtpPrime *from, const mtpPrime *end);

	void destroyMtpKeys(MTP::AuthKeysList &&keys);
	void resetAuthorizationKeys();

	void loggedOut();

	std::unique_ptr<MTP::Instance> _mtp;
	rpl::variable<MTP::Instance*> _mtpValue;
	std::unique_ptr<MTP::Instance> _mtpForKeysDestroy;
	rpl::event_stream<MTPUpdates> _mtpUpdates;
	rpl::event_stream<> _mtpNewSessionCreated;
	rpl::event_stream<> _configUpdates;

	std::unique_ptr<AppConfig> _appConfig;

	std::unique_ptr<Session> _session;
	rpl::variable<Session*> _sessionValue;

	UserId _sessionUserId = 0;
	QByteArray _sessionUserSerialized;
	int32 _sessionUserStreamVersion = 0;
	std::unique_ptr<Settings> _storedSettings;
	MTP::Instance::Config _mtpConfig;
	MTP::AuthKeysList _mtpKeysToDestroy;
	bool _loggingOut = false;

	rpl::lifetime _lifetime;

};

} // namespace Main
