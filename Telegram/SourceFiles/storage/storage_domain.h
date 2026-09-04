/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace MTP {
class Config;
class AuthKey;
using AuthKeyPtr = std::shared_ptr<AuthKey>;
} // namespace MTP

namespace Main {
class Account;
class Domain;
} // namespace Main

namespace Storage {

struct PasscodeWrap;
struct KeyData;

enum class StartResult : uchar {
	Success,
	IncorrectPasscode,
	IncorrectPasscodeLegacy,
};

enum class SetPasscodeResult : uchar {
	Success,
	NeedsVerification,
	Failed,
};

// Proof that the passcode currently protecting the local key was typed and
// accepted. Only Domain::verifyPasscode() can mint one, and a token is
// honoured only while the nonce it carries is still the one Domain holds.
// A token is single use: setPasscode() zeroes that nonce on every exit,
// whether it accepted the token or refused it, and setAppLockEnabled() zeroes
// it as well, so a copy of the token left behind in a settings navigation
// step cannot authorize a second change later on. Both spend it before they
// fire the change notification, so a subscriber that calls back into the
// domain while that notification runs cannot reuse the token either. A default-constructed token
// carries a zero nonce, which never matches an outstanding verification and
// is therefore accepted only where there is no passcode to prove in the first
// place.
class PasscodeVerification final {
public:
	PasscodeVerification() = default;

private:
	friend class Domain;

	explicit PasscodeVerification(quint64 nonce);

	quint64 _nonce = 0;

};

class Domain final {
public:
	Domain(not_null<Main::Domain*> owner, const QString &dataName);
	~Domain();

	[[nodiscard]] StartResult start(const QByteArray &passcode);
	void startAdded(
		not_null<Main::Account*> account,
		std::unique_ptr<MTP::Config> config);
	void writeAccounts();
	void startFromScratch();

	[[nodiscard]] bool checkPasscode(const QByteArray &passcode) const;
	[[nodiscard]] std::optional<PasscodeVerification> verifyPasscode(
		const QByteArray &passcode);
	[[nodiscard]] SetPasscodeResult setPasscode(
		const QByteArray &passcode,
		PasscodeVerification verification);
	[[nodiscard]] SetPasscodeResult setAppLockEnabled(bool enabled);
	void clearPasscodeAfterReset();

	[[nodiscard]] int oldVersion() const;
	void clearOldVersion();

	[[nodiscard]] rpl::producer<> localPasscodeChanged() const;
	[[nodiscard]] bool hasPasscode() const;
	[[nodiscard]] bool appLockEnabled() const;
	[[nodiscard]] bool hasLocalPasscode() const;

private:
	enum class StartModernResult {
		Success,
		IncorrectPasscode,
		Failed,
		Empty,
	};

	[[nodiscard]] StartModernResult startModern(const QByteArray &passcode);
	void startWithSingleAccount(
		const QByteArray &passcode,
		std::unique_ptr<Main::Account> account);
	void generateLocalKey();
	void installOpenWrap(KeyData &data) const;
	void dropOpenWrap(KeyData &data) const;
	void installLegacyWrap(KeyData &data, const QByteArray &passcode) const;
	[[nodiscard]] MTP::AuthKeyPtr installPasscodeWrap(
		KeyData &data,
		const QByteArray &passcode,
		quint32 generation) const;
	void migrateFromLegacy(const QByteArray &passcode);
	[[nodiscard]] QByteArray prepareAccountsInfo() const;
	bool writeKeyData(const KeyData &data, bool sync) const;
	[[nodiscard]] bool writeKeyDataChecked(const KeyData &data) const;
	[[nodiscard]] bool wrapOnDiskOpensLocalKey(
		const PasscodeWrap &staged,
		const MTP::AuthKeyPtr &wrapKey) const;

	const not_null<Main::Domain*> _owner;
	const QString _dataName;

	MTP::AuthKeyPtr _localKey;
	std::unique_ptr<KeyData> _keyData;
	quint64 _verificationNonce = 0;
	int _oldVersion = 0;
	bool _keyDataDirty = false;

	rpl::event_stream<> _passcodeKeyChanged;

};

} // namespace Storage
