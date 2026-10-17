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
struct WalletEngineValue;

// The memory-hard step of a passcode check or change, cut out of Domain as a
// value so that it can run on a worker. It holds copies only - the wrap the
// typed bytes have to open (the live passcode wrap, the legacy blob, or a
// fresh wrap for a change; null when there is none), a deep copy of the typed
// bytes and the derived key - and no pointer into the domain, so it may be
// moved to any thread. run() derives the key for that wrap wherever it is
// called. The typed bytes are cleansed by run() the moment a passcode wrap's
// key exists; on every other leg the destructor cleanses them, because the
// legacy legs still read them on the main thread while the job lives (the
// migration installs its first passcode wrap from them). The domain accepts
// the key back only for a wrap with the same { kdf, salt } this job was made
// for, comparing instead of re-deriving, and never re-derives from bytes that
// were already cleansed - such a mismatch is answered as a wrong passcode.
// checkPasscode() takes one of these jobs and nothing else, so a caller
// holding typed bytes builds it with prepareOpen() and runs it through
// DeriveOnWorker() instead of paying the derivation on the main thread;
// the byte-taking verifyPasscode() and setPasscode() forms remain for the
// legs that still derive where they are called.
class PasscodeDerivation final {
public:
	PasscodeDerivation(PasscodeDerivation &&other) noexcept;
	PasscodeDerivation &operator=(PasscodeDerivation &&other) noexcept;
	~PasscodeDerivation();

	void run();
	[[nodiscard]] bool empty() const;

private:
	friend class Domain;

	PasscodeDerivation(
		std::unique_ptr<PasscodeWrap> wrap,
		const QByteArray &passcode);

	[[nodiscard]] MTP::AuthKeyPtr keyFor(const PasscodeWrap &wrap);
	void cleanse();

	std::unique_ptr<PasscodeWrap> _wrap;
	QByteArray _passcode;
	MTP::AuthKeyPtr _key;
	bool _cleansed = false;

};

// Runs job.run() on a worker and hands the job back to done on the main
// thread. Both callables are moved, never copied, across the two hops, and
// done is built by the caller with crl::guard on the main thread, so the
// guard object is created there and the worker only carries it along.
template <typename Job, typename Done>
void DeriveOnWorker(Job job, Done done) {
	crl::async([job = std::move(job), done = std::move(done)]() mutable {
		job.run();
		crl::on_main([job = std::move(job), done = std::move(done)]() mutable {
			done(std::move(job));
		});
	});
}

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
// setPasscode() reads it for every change or removal over an existing
// passcode, and setAppLockEnabled() reads it when it turns the lock off,
// because installing an open wrap weakens the data at rest exactly as a
// removal does. A token is single use: both zero that nonce on every exit,
// whichever way they move and whether they accepted the token or refused it,
// so a copy of the token left behind in a settings navigation step cannot
// authorize a second change later on. Both spend it before they fire the
// change notification, so a subscriber that calls back into the domain while
// that notification runs cannot reuse the token either. A default-constructed
// token carries a zero nonce, which never matches an outstanding verification
// and is therefore accepted only where there is no passcode to prove in the
// first place.
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

	[[nodiscard]] StartResult start(PasscodeDerivation derived);
	void startAdded(
		not_null<Main::Account*> account,
		std::unique_ptr<MTP::Config> config);
	void writeAccounts();
	void startFromScratch();

	[[nodiscard]] WalletEngineValue readWalletKeyring() const;
	[[nodiscard]] bool writeWalletKeyring(const QByteArray &bytes) const;
	[[nodiscard]] bool removeWalletKeyring();

	[[nodiscard]] PasscodeDerivation prepareOpen(
		const QByteArray &passcode) const;
	[[nodiscard]] PasscodeDerivation prepareNewWrap(
		const QByteArray &passcode) const;
	[[nodiscard]] bool checkPasscode(PasscodeDerivation derived) const;
	[[nodiscard]] std::optional<PasscodeVerification> verifyPasscode(
		const QByteArray &passcode);
	[[nodiscard]] std::optional<PasscodeVerification> verifyPasscode(
		PasscodeDerivation derived);
	[[nodiscard]] SetPasscodeResult setPasscode(
		const QByteArray &passcode,
		PasscodeVerification verification);
	[[nodiscard]] SetPasscodeResult setPasscode(
		PasscodeDerivation derived,
		PasscodeVerification verification);
	// Creates a first passcode in the wallet-only role, keeping the verified
	// open wrap so the launch lock stays off. It asks for no proof because it
	// is first-create only and weakens nothing - the local key already opens
	// without a passcode - and it refuses everything else at the write
	// boundary, rechecked here after the derivation instead of wherever the
	// caller started it: an existing passcode wrap answers NeedsVerification,
	// while retained legacy state and an open wrap that is missing or
	// unverified answer Failed. Otherwise it is the same staged, proved and
	// committed sequence setPasscode() runs to create or change a passcode,
	// one logical operation rather than one physical write, ending in the
	// same nonce reset and synchronous localPasscodeChanged().
	[[nodiscard]] SetPasscodeResult createPasscodeWithoutAppLock(
		PasscodeDerivation derived);
	[[nodiscard]] SetPasscodeResult setAppLockEnabled(
		bool enabled,
		PasscodeVerification verification);
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

	[[nodiscard]] StartModernResult startModern(PasscodeDerivation &derived);
	void startWithSingleAccount(
		const QByteArray &passcode,
		std::unique_ptr<Main::Account> account);
	void generateLocalKey();
	void installOpenWrap(KeyData &data) const;
	void dropOpenWrap(KeyData &data) const;
	void installLegacyWrap(KeyData &data, const QByteArray &passcode) const;
	[[nodiscard]] MTP::AuthKeyPtr installPasscodeWrap(
		KeyData &data,
		PasscodeDerivation &derived,
		quint32 generation) const;
	void migrateFromLegacy(const QByteArray &passcode);
	[[nodiscard]] QByteArray prepareAccountsInfo() const;
	bool writeKeyData(const KeyData &data, bool sync) const;
	[[nodiscard]] bool writeKeyDataChecked(const KeyData &data) const;
	[[nodiscard]] bool wrapOnDiskOpensLocalKey(
		const PasscodeWrap &staged,
		const MTP::AuthKeyPtr &wrapKey) const;
	[[nodiscard]] std::unique_ptr<PasscodeWrap> wrapToOpen() const;
	[[nodiscard]] bool accepts(PasscodeVerification verification) const;
	[[nodiscard]] SetPasscodeResult changePasscode(
		PasscodeDerivation *derived,
		PasscodeVerification verification);
	[[nodiscard]] SetPasscodeResult installPasscode(
		PasscodeDerivation &derived,
		bool keepOpenWrap);

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
