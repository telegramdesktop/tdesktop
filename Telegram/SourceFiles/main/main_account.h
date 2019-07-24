/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/auth_key.h"
#include "base/weak_ptr.h"

class AuthSession;
class AuthSessionSettings;

namespace Main {

class Account final : public base::has_weak_ptr {
public:
	explicit Account(const QString &dataName);
	~Account();

	Account(const Account &other) = delete;
	Account &operator=(const Account &other) = delete;

	void createSession(const MTPUser &user);
	void destroySession();

	void logOut();
	void forcedLogOut();

	[[nodiscard]] bool sessionExists() const;
	[[nodiscard]] AuthSession &session();
	[[nodiscard]] const AuthSession &session() const;
	[[nodiscard]] rpl::producer<AuthSession*> sessionValue() const;
	[[nodiscard]] rpl::producer<AuthSession*> sessionChanges() const;

	[[nodiscard]] MTP::Instance *mtp() {
		return _mtp.get();
	}
	[[nodiscard]] rpl::producer<MTP::Instance*> mtpValue() const;
	[[nodiscard]] rpl::producer<MTP::Instance*> mtpChanges() const;

	// Set from legacy storage.
	void setMtpMainDcId(MTP::DcId mainDcId);
	void setMtpKey(MTP::DcId dcId, const MTP::AuthKey::Data &keyData);
	void setAuthSessionUserId(UserId userId);
	void setAuthSessionFromStorage(
		std::unique_ptr<AuthSessionSettings> data,
		QByteArray &&selfSerialized,
		int32 selfStreamVersion);
	[[nodiscard]] AuthSessionSettings *getAuthSessionSettings();

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
	void watchProxyChanges();
	void watchSessionChanges();

	void destroyMtpKeys(MTP::AuthKeysList &&keys);
	void allKeysDestroyed();
	void resetAuthorizationKeys();

	void loggedOut();

	std::unique_ptr<MTP::Instance> _mtp;
	rpl::variable<MTP::Instance*> _mtpValue;
	std::unique_ptr<MTP::Instance> _mtpForKeysDestroy;
	rpl::event_stream<> _configUpdates;

	std::unique_ptr<AuthSession> _session;
	rpl::variable<AuthSession*> _sessionValue;

	UserId _authSessionUserId = 0;
	QByteArray _authSessionUserSerialized;
	int32 _authSessionUserStreamVersion = 0;
	std::unique_ptr<AuthSessionSettings> _storedAuthSession;
	MTP::Instance::Config _mtpConfig;
	MTP::AuthKeysList _mtpKeysToDestroy;

	rpl::lifetime _lifetime;

};

} // namespace Main
