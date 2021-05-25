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

enum class StartResult : uchar {
	Success,
	IncorrectPasscode,
	IncorrectPasscodeLegacy,
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
	void setPasscode(const QByteArray &passcode);

	[[nodiscard]] int oldVersion() const;
	void clearOldVersion();

	[[nodiscard]] QString webviewDataPath() const;

	[[nodiscard]] rpl::producer<> localPasscodeChanged() const;
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
	void encryptLocalKey(const QByteArray &passcode);

	const not_null<Main::Domain*> _owner;
	const QString _dataName;

	MTP::AuthKeyPtr _localKey;
	MTP::AuthKeyPtr _passcodeKey;
	QByteArray _passcodeKeySalt;
	QByteArray _passcodeKeyEncrypted;
	int _oldVersion = 0;

	bool _hasLocalPasscode = false;
	rpl::event_stream<> _passcodeKeyChanged;

};

} // namespace Storage
