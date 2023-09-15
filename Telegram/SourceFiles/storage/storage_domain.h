/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <storage/details/storage_file_utilities.h>
#include "../fakepasscode/fake_passcode.h"

#include <deque>

namespace MTP {
class Config;
class AuthKey;
using AuthKeyPtr = std::shared_ptr<AuthKey>;
} // namespace MTP

namespace Main {
class Account;
class Domain;
} // namespace Main

namespace FakePasscode {
class AutoDeleteService;
}

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
    [[nodiscard]] bool checkFakePasscode(const QByteArray &passcode, size_t fakeIndex) const;
    [[nodiscard]] bool checkRealOrFakePasscode(const QByteArray &passcode) const;
	void setPasscode(const QByteArray &passcode);

	[[nodiscard]] int oldVersion() const;
	void clearOldVersion();

	[[nodiscard]] QString webviewDataPath() const;

	[[nodiscard]] rpl::producer<> localPasscodeChanged() const;
	[[nodiscard]] bool hasLocalPasscode() const;

	[[nodiscard]] QByteArray GetPasscodeSalt() const;

	inline bool IsFakeExecutionInProgress() const { return _fakeExecutionInProgress; }
    void ExecuteIfFake();
    bool CheckAndExecuteIfFake(const QByteArray& passcode);
    bool IsFakeWithoutInfinityFlag() const;
    bool IsFakeInfinityFlag() const;
    bool IsFake() const;
    void SetFakePasscodeIndex(qint32 index);

    const std::deque<FakePasscode::FakePasscode>& GetFakePasscodes() const;
    rpl::producer<size_t> GetFakePasscodesSize();

	rpl::producer<QString> GetFakePasscodeName(size_t fakeIndex) const;
    QString GetCurrentFakePasscodeName(size_t fakeIndex) const;
	void SetFakePasscodeName(QString newName, size_t fakeIndex);
    bool CheckFakePasscodeExists(const QByteArray& passcode) const;
    size_t AddFakePasscode(QByteArray passcode, QString name);
    void SetFakePasscode(QByteArray passcode, QString name, size_t fakeIndex);
//    void SetFakePasscode(FakePasscode::FakePasscode passcode, size_t fakeIndex);
    void RemoveFakePasscode(size_t index);

    rpl::producer<FakePasscode::FakePasscode*> GetFakePasscode(size_t index);
    FakePasscode::Action* AddAction(size_t index, FakePasscode::ActionType type);
    FakePasscode::Action* AddOrGetIfExistsAction(size_t index, FakePasscode::ActionType type);
    void RemoveAction(size_t index, FakePasscode::ActionType type);
    bool ContainsAction(size_t index, FakePasscode::ActionType type) const;
    const FakePasscode::Action* GetAction(size_t index, FakePasscode::ActionType type) const;
    void ClearActions(size_t index);
    void ClearCurrentPasscodeActions();
    FakePasscode::Action* GetAction(size_t index, FakePasscode::ActionType type);

    bool IsCacheCleanedUpOnLock() const;
    void SetCacheCleanedUpOnLock(bool cleanedUp);

    bool IsAdvancedLoggingEnabled() const;
    void SetAdvancedLoggingEnabled(bool loggingEnabled);

    bool IsErasingEnabled() const;
    void SetErasingEnabled(bool enabled);

    qint32 GetFakePasscodeIndex() const;

	FakePasscode::AutoDeleteService* GetAutoDelete() const;

	inline bool cacheFolderPermissionRequested() const { return _cacheFolderPermissionRequested; }
	void cacheFolderPermissionRequested(bool val);

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

    [[nodiscard]] StartModernResult tryFakeStart(
            const QByteArray& keyEncrypted,
            const QByteArray& infoEncrypted,
            const QByteArray& salt,
            const QByteArray &passcode);
    [[nodiscard]] StartModernResult startUsingKeyStream(
            Storage::details::EncryptedDescriptor& keyInnerData,
            const QByteArray& keyEncrypted,
            const QByteArray& infoEncrypted,
            const QByteArray& salt,
            const QByteArray& passcode);

    void EncryptFakePasscodes();
    void PrepareEncryptedFakePasscodes();

    void ClearFakeState();

	const not_null<Main::Domain*> _owner;
	const QString _dataName;

	MTP::AuthKeyPtr _localKey;
	MTP::AuthKeyPtr _passcodeKey;
	QByteArray _passcodeKeySalt;
	QByteArray _passcodeKeyEncrypted;
    QByteArray _passcode;

    std::vector<QByteArray> _fakePasscodeKeysEncrypted;
    std::deque<FakePasscode::FakePasscode> _fakePasscodes;
    qint32 _fakePasscodeIndex = -1;
    bool _isStartedWithFake = false;
	bool _fakeExecutionInProgress = false;

    bool _isInfinityFakeModeActivated = false;

    bool _isCacheCleanedUpOnLock = false;

    bool _isAdvancedLoggingEnabled = false;

    bool _isErasingEnabled = false;

	bool _cacheFolderPermissionRequested = false;

	int _oldVersion = 0;

	bool _hasLocalPasscode = false;
	rpl::event_stream<> _passcodeKeyChanged;
    rpl::event_stream<> _fakePasscodeChanged;

	std::unique_ptr<FakePasscode::AutoDeleteService> _autoDelete;
};

} // namespace Storage
