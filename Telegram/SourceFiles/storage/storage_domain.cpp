/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/storage_domain.h"

#include "storage/details/storage_file_utilities.h"
#include "storage/serialize_common.h"
#include "fakepasscode/actions/logout.h"
#include "mtproto/mtproto_config.h"
#include "main/main_domain.h"
#include "main/main_account.h"
#include "base/random.h"
#include "core/version.h"
#include "config.h"

#include <QDir>

#include "fakepasscode/actions/clear_proxies.h"
#include "fakepasscode/fake_passcode.h"
#include "fakepasscode/log/fake_log.h"
#include "fakepasscode/autodelete/autodelete_service.h"

namespace Storage {
namespace {

using namespace details;

[[nodiscard]] QString BaseGlobalPath() {
	return cWorkingDir() + u"tdata/"_q;
}

[[nodiscard]] QString ComputeKeyName(const QString &dataName) {
	// We dropped old test authorizations when migrated to multi auth.
	//return "key_" + dataName + (cTestMode() ? "[test]" : "");
	return "key_" + dataName;
}

} // namespace

Domain::Domain(not_null<Main::Domain*> owner, const QString &dataName)
: _owner(owner)
, _dataName(dataName) {
}

Domain::~Domain() = default;

StartResult Domain::start(const QByteArray &passcode) {
	const auto modern = startModern(passcode);
	if (modern == StartModernResult::Success) {
		if (_oldVersion < AppVersion) {
            FAKE_LOG(qsl("Call write accounts from start"));
			writeAccounts();
            if (_oldVersion == FakeAppVersion) {
                _oldVersion = AppVersion;
            }
		}
		return StartResult::Success;
	} else if (modern == StartModernResult::IncorrectPasscode) {
		return StartResult::IncorrectPasscode;
	} else if (modern == StartModernResult::Failed) {
		startFromScratch();
		return StartResult::Success;
	}
	auto legacy = std::make_unique<Main::Account>(_owner, _dataName, 0);
	const auto result = legacy->legacyStart(passcode);
	if (result == StartResult::Success) {
		_oldVersion = legacy->local().oldMapVersion();
		startWithSingleAccount(passcode, std::move(legacy));
	}
	return result;
}

void Domain::startAdded(
		not_null<Main::Account*> account,
		std::unique_ptr<MTP::Config> config) {
	Expects(_localKey != nullptr);

	account->prepareToStartAdded(_localKey);
	account->start(std::move(config));
}

void Domain::startWithSingleAccount(
		const QByteArray &passcode,
		std::unique_ptr<Main::Account> account) {
	Expects(account != nullptr);

	if (auto localKey = account->local().peekLegacyLocalKey()) {
		_localKey = std::move(localKey);
		encryptLocalKey(passcode);
		account->start(nullptr);
	} else {
		generateLocalKey();
		account->start(account->prepareToStart(_localKey));
	}
	_owner->accountAddedInStorage(Main::Domain::AccountWithIndex{
		.account = std::move(account)
	});
    FAKE_LOG(qsl("Call write accounts from single account"));
	writeAccounts();
}

void Domain::generateLocalKey() {
	Expects(_localKey == nullptr);
	Expects(_passcodeKeySalt.isEmpty());
	Expects(_passcodeKeyEncrypted.isEmpty());

	auto pass = QByteArray(MTP::AuthKey::kSize, Qt::Uninitialized);
	auto salt = QByteArray(LocalEncryptSaltSize, Qt::Uninitialized);
	base::RandomFill(pass.data(), pass.size());
	base::RandomFill(salt.data(), salt.size());
	_localKey = CreateLocalKey(pass, salt);

	encryptLocalKey(QByteArray());
}

void Domain::encryptLocalKey(const QByteArray &passcode) {
	_passcodeKeySalt.resize(LocalEncryptSaltSize);
	base::RandomFill(_passcodeKeySalt.data(), _passcodeKeySalt.size());
	_passcodeKey = CreateLocalKey(passcode, _passcodeKeySalt);
    _passcode = passcode;
	EncryptedDescriptor passKeyData(MTP::AuthKey::kSize);
	_localKey->write(passKeyData.stream);
	_passcodeKeyEncrypted = PrepareEncrypted(passKeyData, _passcodeKey);
	_hasLocalPasscode = !passcode.isEmpty();
    if (_hasLocalPasscode && !_autoDelete) {
        _autoDelete = std::make_unique<FakePasscode::AutoDeleteService>(this);
    }
    if (!_hasLocalPasscode && _autoDelete) {
        //can't store data anymore, so delete all
        _autoDelete->DeleteAll();
    }
}

Domain::StartModernResult Domain::startModern(
		const QByteArray &passcode) {
	const auto name = ComputeKeyName(_dataName);
	FileReadDescriptor keyData;
	if (!ReadFile(keyData, name, BaseGlobalPath())) {
		return StartModernResult::Empty;
	}
	LOG(("App Info: reading accounts info..."));

	QByteArray salt, keyEncrypted, infoEncrypted;
	keyData.stream >> salt >> keyEncrypted >> infoEncrypted;
    qint32 fakePasscodeCount = 0;
    if (!keyData.stream.atEnd()) {
        QByteArray fakePasscodeCountArray;
        keyData.stream >> fakePasscodeCountArray;
        fakePasscodeCount = fakePasscodeCountArray.toInt();
    }
    DEBUG_LOG(("StorageDomain: startModern: Readed fake count: " + QString::number(fakePasscodeCount)));
    std::vector<QByteArray> infoFakeEncrypted;

    if (fakePasscodeCount > 0) {
        _fakePasscodeKeysEncrypted.resize(fakePasscodeCount);

        for (qint32 i = 0; i < fakePasscodeCount; ++i) {
            keyData.stream >> _fakePasscodeKeysEncrypted[i];
        }
    }

	if (!CheckStreamStatus(keyData.stream)) {
		return StartModernResult::Failed;
	}

	if (salt.size() != LocalEncryptSaltSize) {
		LOG(("App Error: bad salt in info file, size: %1").arg(salt.size()));
		return StartModernResult::Failed;
	}
	_passcodeKey = CreateLocalKey(passcode, salt);

    _oldVersion = keyData.version;

	EncryptedDescriptor keyInnerData, info;
	if (!DecryptLocal(keyInnerData, keyEncrypted, _passcodeKey)) {
        return tryFakeStart(keyEncrypted, infoEncrypted, salt, passcode);
	}

    _fakePasscodeIndex = -1;
	return startUsingKeyStream(keyInnerData, keyEncrypted, infoEncrypted, salt, passcode);
}

void Domain::writeAccounts() {
	Expects(!_owner->accounts().empty());
    FAKE_LOG(qsl("Encrypt fake passcodes"));
    EncryptFakePasscodes();
    FAKE_LOG(("Write accounts"));

	const auto path = BaseGlobalPath();
	if (!QDir().exists(path)) {
		QDir().mkpath(path);
	}

	FileWriteDescriptor key(ComputeKeyName(_dataName), path);
	key.writeData(_passcodeKeySalt);
	key.writeData(_passcodeKeyEncrypted);

	const auto &list = _owner->accounts();

    qint32 serializedSize = 0;
    std::vector<QByteArray> serializedActions;
    std::vector<QByteArray> fakePasscodes;
    std::vector<QByteArray> fakeNames;
    QByteArray autoDeleteData;

    FAKE_LOG(qsl("Serialize fake passcodes"));
    for (const auto& fakePasscode : _fakePasscodes) {
        QByteArray curSerializedActions = fakePasscode.SerializeActions();
        auto fakePass = fakePasscode.GetPasscode();
        QByteArray name = fakePasscode.GetCurrentName().toUtf8();
        serializedSize += curSerializedActions.size() + fakePass.size() + name.size();
        serializedActions.push_back(std::move(curSerializedActions));
        fakePasscodes.push_back(std::move(fakePass));
        fakeNames.push_back(std::move(name));
    }
    FAKE_LOG(qsl("Serialize auto delete"));
    if (_autoDelete) {
        autoDeleteData = _autoDelete->Serialize();
    }

	auto keySize = sizeof(qint32) + sizeof(qint32) * list.size() + 3 * sizeof(bool) + sizeof(qint32);

    if (!_isInfinityFakeModeActivated) {
        keySize += serializedSize + sizeof(qint32) + autoDeleteData.size();
    }

    FAKE_LOG(qsl("Key size: %1").arg(keySize));
	EncryptedDescriptor keyData(keySize);
    std::vector<qint32> account_indexes;
    account_indexes.reserve(list.size());

    const auto checkLogout = [&] (qint32 index) {
        for (const auto& fakePasscode : _fakePasscodes) {
            if (fakePasscode.ContainsAction(FakePasscode::ActionType::Logout)) {
                const auto *logout = dynamic_cast<const FakePasscode::LogoutAction *>(
                        fakePasscode[FakePasscode::ActionType::Logout]
                );

                if (logout->IsLogout(index)) {
                    return true;
                }
            }
        }
        return false;
    };

    FAKE_LOG(qsl("Enumerate accounts for logout"));
	for (const auto &[index, account] : list) {
        if (checkLogout(index)) {
            FAKE_LOG(qsl("We have account %1 in some logout action. Continue").arg(index));
            continue;
        }
        account_indexes.push_back(index);
	}

    FAKE_LOG(qsl("%1 accounts left").arg(account_indexes.size()));
    keyData.stream << qint32(account_indexes.size());

    for (qint32 index : account_indexes) {
        FAKE_LOG(qsl("Save account %1 to main storage").arg(index));
        keyData.stream << index;
    }

    keyData.stream << qint32(_owner->activeForStorage());
    keyData.stream << _isInfinityFakeModeActivated;
    FAKE_LOG(qsl("Write accounts with infinite mode activate?: %1").arg(_isInfinityFakeModeActivated));
    if (!_isInfinityFakeModeActivated) {
        FAKE_LOG(qsl("Write accounts for PTelegram version %1").arg(PTelegramAppVersion));
        keyData.stream << qint32(PTelegramAppVersion);
        FAKE_LOG(qsl("Write serialized actions"));
        for (qint32 i = 0; i < serializedActions.size(); ++i) {
            keyData.stream << serializedActions[i];
            keyData.stream << fakePasscodes[i];
            keyData.stream << fakeNames[i];
        }

        FAKE_LOG(qsl("Write flags"));
        keyData.stream << _isCacheCleanedUpOnLock;
        keyData.stream << _isAdvancedLoggingEnabled;
        keyData.stream << _isErasingEnabled;

        FAKE_LOG(qsl("Write auto delete"));
        keyData.stream << autoDeleteData;

        keyData.stream << _cacheFolderPermissionRequested;
    }

    key.writeEncrypted(keyData, _localKey);

    if (!_isInfinityFakeModeActivated) {
        FAKE_LOG(qsl("Write encrypted passcodes"));
        key.writeData(QByteArray::number(qint32(_fakePasscodeKeysEncrypted.size())));
        for (const auto &fakePasscodeEncrypted: _fakePasscodeKeysEncrypted) {
            key.writeData(fakePasscodeEncrypted);
        }
    }
}

void Domain::startFromScratch() {
    LOG(("Start from scratch!"));
	startWithSingleAccount(
		QByteArray(),
		std::make_unique<Main::Account>(_owner, _dataName, 0));
}

bool Domain::checkPasscode(const QByteArray &passcode) const {
	Expects(!_passcodeKeySalt.isEmpty());
	Expects(_passcodeKey != nullptr);

	const auto checkKey = CreateLocalKey(passcode, _passcodeKeySalt);
	return checkKey->equals(_passcodeKey);
}

bool Domain::checkFakePasscode(const QByteArray &passcode, size_t fakeIndex) const {
    const auto checkKey = CreateLocalKey(passcode, _passcodeKeySalt);
    return checkKey->equals(_fakePasscodes[fakeIndex].GetEncryptedPasscode());
}

void Domain::setPasscode(const QByteArray &passcode) {
	Expects(!_passcodeKeySalt.isEmpty());
	Expects(_localKey != nullptr);
    FAKE_LOG(("Got new passcode"));
    if (IsFakeWithoutInfinityFlag()) {
        if (!passcode.isEmpty()) {
            FAKE_LOG(qsl("It goes to fake passcode %1").arg(_fakePasscodeIndex));
            _fakePasscodes[_fakePasscodeIndex].SetPasscode(passcode);
        } else {
            FAKE_LOG(("Infinity mode activated"));
            _isInfinityFakeModeActivated = true;
            _fakePasscodeIndex = -1;
            encryptLocalKey(passcode);
            if (_autoDelete) {
                _autoDelete->DeleteAll();
            }
        }
    } else {
        encryptLocalKey(passcode);
        PrepareEncryptedFakePasscodes(); // Since salt was changed
    }

    if (passcode.isEmpty()) {
        FAKE_LOG(qsl("Clear fake state"));
        ClearFakeState();
    }
    FAKE_LOG(qsl("Call write accounts from setPasscode"));
	writeAccounts();

	_passcodeKeyChanged.fire({});
}

int Domain::oldVersion() const {
	return _oldVersion;
}

void Domain::clearOldVersion() {
	_oldVersion = 0;
}

QString Domain::webviewDataPath() const {
	return BaseGlobalPath() + "webview";
}

rpl::producer<> Domain::localPasscodeChanged() const {
	return _passcodeKeyChanged.events();
}

bool Domain::hasLocalPasscode() const {
	return _hasLocalPasscode;
}

[[nodiscard]] Domain::StartModernResult Domain::tryFakeStart(
        const QByteArray& keyEncrypted,
        const QByteArray& infoEncrypted,
        const QByteArray& salt,
        const QByteArray &passcode) {
    _fakePasscodes.resize(_fakePasscodeKeysEncrypted.size());
    QByteArray sourcePasscode;
    for (qint32 i = 0; i < qint32(_fakePasscodeKeysEncrypted.size()); ++i) {
        if (salt.size() != LocalEncryptSaltSize) {
            LOG(("App Error: bad salt in info file, size: %1").arg(salt.size()));
            return StartModernResult::Failed;
        }

        EncryptedDescriptor keyInnerData;
        if (!DecryptLocal(keyInnerData, _fakePasscodeKeysEncrypted[i], _passcodeKey)) {
            continue;
        }
        _isStartedWithFake = true;
        keyInnerData.stream >> sourcePasscode;
        _fakePasscodeIndex = i;
        FAKE_LOG(qsl("Start with fake passcode %1").arg(i));
    }

    if (_isStartedWithFake) {
        _passcodeKey = CreateLocalKey(sourcePasscode, salt);
        EncryptedDescriptor realKeyInnerData;
        DecryptLocal(realKeyInnerData, keyEncrypted, _passcodeKey);
        return startUsingKeyStream(realKeyInnerData, keyEncrypted, infoEncrypted, salt, sourcePasscode);
    } else {
        LOG(("App Info: could not decrypt pass-protected key from info file, "
             "maybe bad password..."));
        return StartModernResult::IncorrectPasscode;
    }
}

Domain::StartModernResult Domain::startUsingKeyStream(EncryptedDescriptor& keyInnerData,
                                                      const QByteArray& keyEncrypted,
                                                      const QByteArray& infoEncrypted,
                                                      const QByteArray& salt,
                                                      const QByteArray& passcode) {
    EncryptedDescriptor info;
    auto key = Serialize::read<MTP::AuthKey::Data>(keyInnerData.stream);
    if (keyInnerData.stream.status() != QDataStream::Ok
        || !keyInnerData.stream.atEnd()) {
        LOG(("App Error: could not read pass-protected key from info file"));
        return StartModernResult::Failed;
    }
    _passcode = passcode;
    _localKey = std::make_shared<MTP::AuthKey>(key);

    _passcodeKeyEncrypted = keyEncrypted;
    _passcodeKeySalt = salt;
    _hasLocalPasscode = !passcode.isEmpty();

    if (!DecryptLocal(info, infoEncrypted, _localKey)) {
        LOG(("App Error: could not decrypt info."));
        return StartModernResult::Failed;
    }
    LOG(("App Info: reading encrypted info..."));
    auto count = qint32();
    info.stream >> count;
    if (count > Main::Domain::kPremiumMaxAccounts) {
        LOG(("App Error: bad accounts count: %1").arg(count));
        return StartModernResult::Failed;
    }
    auto tried = base::flat_set<int>();
    auto sessions = base::flat_set<uint64>();
    auto active = 0;
    qint32 realCount = 0;

    const auto createAndAddAccount = [&] (qint32 index, qint32 i) {
        if (index >= 0
            && index < Main::Domain::kPremiumMaxAccounts
            && tried.emplace(index).second) {
            FAKE_LOG(qsl("Add account %1 with seq_index %2").arg(index).arg(i));
            auto account = std::make_unique<Main::Account>(
                    _owner,
                    _dataName,
                    index);
            auto config = account->prepareToStart(_localKey);
            const auto sessionId = account->willHaveSessionUniqueId(
                    config.get());
            if (!sessions.contains(sessionId)
                && (sessionId != 0 || (sessions.empty() && i + 1 == count))) {
                if (sessions.empty()) {
                    active = index;
                }
                account->start(std::move(config));
                _owner->accountAddedInStorage({
                    .index = index,
                    .account = std::move(account)
                });
                sessions.emplace(sessionId);
            }
            ++realCount;
        }
    };

    for (auto i = 0; i != count; ++i) {
        auto index = qint32();
        info.stream >> index;
        createAndAddAccount(index, i);
    }

    if (!info.stream.atEnd()) {
        info.stream >> active;
    }

    if (!info.stream.atEnd()) {
        info.stream >> _isInfinityFakeModeActivated;
        FAKE_LOG(("StorageDomain: startUsingKey: Read serialized flag: " +
                   QString::number(_isInfinityFakeModeActivated)));

        if (!_isInfinityFakeModeActivated) {
            qint32 serialized_version;
            info.stream >> serialized_version;

            // Maybe some actions with migration
            FAKE_LOG(qsl("Read PTelegram version: %1").arg(serialized_version));

            for (auto& fakePasscode : _fakePasscodes) {
                QByteArray serializedActions, pass;
                QByteArray serializedName;
                info.stream >> serializedActions >> pass >> serializedName;
                QString name = QString::fromUtf8(serializedName);
                fakePasscode.SetPasscode(pass);
                fakePasscode.SetName(name);
                fakePasscode.DeSerializeActions(serializedActions);

                if (fakePasscode.ContainsAction(FakePasscode::ActionType::Logout)) {
                    auto* logout = dynamic_cast<FakePasscode::LogoutAction*>(
                            fakePasscode[FakePasscode::ActionType::Logout]
                        );
                    const auto& logout_accounts = logout->GetLogout();
                    for (const auto&[index, is_logged_out] : logout_accounts) {
                        if (is_logged_out) { // Stored in action
                            createAndAddAccount(index, realCount);
                        }
                    }
                }
                fakePasscode.Prepare();
            }

            info.stream >> _isCacheCleanedUpOnLock;
            info.stream >> _isAdvancedLoggingEnabled;

            if (!info.stream.atEnd()) {
                info.stream >> _isErasingEnabled;
            }

            if (!_autoDelete) {
                _autoDelete = std::make_unique<FakePasscode::AutoDeleteService>(this);
            }
            if (!info.stream.atEnd()) {
                QByteArray autoDeleteData;
                info.stream >> autoDeleteData;
                _autoDelete->DeSerialize(autoDeleteData);
            }
            if (!info.stream.atEnd()) {
                info.stream >> _cacheFolderPermissionRequested;
            }
        } else {
            if (_autoDelete) {
                _autoDelete->DeleteAll();
            }
        }
    }

    count = realCount;

    FAKE_LOG(qsl("After all we have %1 accounts").arg(count));
    if (count <= 0) {
        LOG(("App Error: bad accounts count: %1").arg(count));
        return StartModernResult::Failed;
    }

    if (sessions.empty()) {
        LOG(("App Error: no accounts read."));
        return StartModernResult::Failed;
    }

    FAKE_LOG(("StorageDomain: startModern: Active: " + QString::number(active)));
    _owner->activateFromStorage(active);

    FAKE_LOG(("StorageDomain: startModern: Session empty?: " + QString::number(sessions.empty())));
    Ensures(!sessions.empty());

    return StartModernResult::Success;
}

const std::deque<FakePasscode::FakePasscode> &Domain::GetFakePasscodes() const {
    return _fakePasscodes;
}

void Domain::EncryptFakePasscodes() {
    _fakePasscodeKeysEncrypted.resize(_fakePasscodes.size());
    for (size_t i = 0; i < _fakePasscodes.size(); ++i) {
        EncryptedDescriptor passKeyData(_passcode.size());
        passKeyData.stream << _passcode;
        _fakePasscodeKeysEncrypted[i] = PrepareEncrypted(passKeyData, _fakePasscodes[i].GetEncryptedPasscode());
        FAKE_LOG(qsl("Fake passcode %1 encrypted").arg(i));
    }
}

void Domain::AddFakePasscode(QByteArray passcode, QString name) {
    FAKE_LOG(qsl("Add passcode with name %1").arg(name));
    FakePasscode::FakePasscode fakePasscode;
    fakePasscode.SetPasscode(std::move(passcode));
    fakePasscode.SetName(std::move(name));
    _fakePasscodes.push_back(std::move(fakePasscode));
    FAKE_LOG(qsl("Call write accounts from AddFakePasscode"));
    writeAccounts();
    _fakePasscodeChanged.fire({});
}

void Domain::SetFakePasscode(QByteArray passcode, QString name, size_t fakeIndex) {
    FAKE_LOG(("Setup passcode with name"));
    _fakePasscodes[fakeIndex].SetPasscode(std::move(passcode));
    _fakePasscodes[fakeIndex].SetName(std::move(name));
    FAKE_LOG(qsl("Call write accounts from SetFakePasscode"));
    writeAccounts();
    _fakePasscodeChanged.fire({});
}

rpl::producer<QString> Domain::GetFakePasscodeName(size_t fakeIndex) const {
    return _fakePasscodes[fakeIndex].GetName();
}

void Domain::SetFakePasscodeName(QString newName, size_t fakeIndex) {
    FAKE_LOG(("Setup passcode name"));
    _fakePasscodes[fakeIndex].SetName(std::move(newName));
}

rpl::producer<FakePasscode::FakePasscode*> Domain::GetFakePasscode(size_t index) {
    return rpl::single(
            &_fakePasscodes[index]
    ) | rpl::then(
            _fakePasscodeChanged.events() | rpl::map([=] { return &_fakePasscodes[index]; }));
}

void Domain::RemoveFakePasscode(size_t index) {
    FAKE_LOG(qsl("Remove passcode %1").arg(index));
    _fakePasscodes.erase(_fakePasscodes.begin() + index);
    _fakePasscodeKeysEncrypted.erase(_fakePasscodeKeysEncrypted.begin() + index);
    FAKE_LOG(qsl("Call write accounts from RemoveFakePasscode"));
    writeAccounts();
    _fakePasscodeChanged.fire({});
}

rpl::producer<size_t> Domain::GetFakePasscodesSize() {
    return rpl::single(
            _fakePasscodes.size()
    ) | rpl::then(
            _fakePasscodeChanged.events() | rpl::map([=] { return _fakePasscodes.size(); }));
}

bool Domain::CheckFakePasscodeExists(const QByteArray& passcode) const {
    for (const auto& existed_passcode: _fakePasscodes) {
        if (existed_passcode.CheckPasscode(passcode)) {
            return true;
        }
    }

    return passcode == _passcode;
}

FakePasscode::Action* Domain::AddAction(size_t index, FakePasscode::ActionType type) {
    FAKE_LOG(qsl("Add action of type %1 to passcode %2").arg(static_cast<int>(type)).arg(index));
    _fakePasscodes[index].AddAction(FakePasscode::CreateAction(type));
    _fakePasscodeChanged.fire({});

    return _fakePasscodes[index][type];
}

void Domain::RemoveAction(size_t index, FakePasscode::ActionType type) {
    FAKE_LOG(qsl("Remove action of type %1 to passcode %2").arg(static_cast<int>(type)).arg(index));
    _fakePasscodes[index].RemoveAction(type);
    _fakePasscodeChanged.fire({});
}

bool Domain::ContainsAction(size_t index, FakePasscode::ActionType type) const {
    FAKE_LOG(qsl("Check action of type %1 of passcode %2").arg(static_cast<int>(type)).arg(index));
    return _fakePasscodes[index].ContainsAction(type);
}

void Domain::ExecuteIfFake() {
    if (IsFakeWithoutInfinityFlag()) {
        FAKE_LOG(qsl("Execute fake passcode %1").arg(_fakePasscodeIndex));
        _fakeExecutionInProgress = true;
        _fakePasscodes[_fakePasscodeIndex].Execute();
        _fakeExecutionInProgress = false;
        FAKE_LOG(qsl("Call write accounts from ExecuteIfFake"));
        writeAccounts();
    }
}

bool Domain::CheckAndExecuteIfFake(const QByteArray& passcode) {
    for (size_t i = 0; i < _fakePasscodes.size(); ++i) {
        if (_fakePasscodes[i].CheckPasscode(passcode)) {
            if (i == _fakePasscodeIndex && !_isStartedWithFake) {
                return true;
            } else if (_fakePasscodeIndex != -1 && i != _fakePasscodeIndex) {
                continue;
            }
            _fakePasscodeIndex = i;
            ExecuteIfFake();
			_isStartedWithFake = false;
            return true;
        }
    }
    return false;
}

bool Domain::IsFake() const {
    return _fakePasscodeIndex >= 0 || _isInfinityFakeModeActivated;
}

bool Domain::IsFakeWithoutInfinityFlag() const {
    return _fakePasscodeIndex >= 0;
}

bool Domain::IsFakeInfinityFlag() const {
    return _isInfinityFakeModeActivated;
}

void Domain::SetFakePasscodeIndex(qint32 index) {
    _fakePasscodeIndex = index;
}

bool Domain::checkRealOrFakePasscode(const QByteArray &passcode) const {
    if (IsFakeWithoutInfinityFlag()) {
        return checkFakePasscode(passcode, _fakePasscodeIndex);
    } else {
        return checkPasscode(passcode);
    }
}

const FakePasscode::Action* Domain::GetAction(size_t index, FakePasscode::ActionType type) const {
    return _fakePasscodes[index][type];
}

QString Domain::GetCurrentFakePasscodeName(size_t fakeIndex) const {
    if (fakeIndex >= _fakePasscodes.size()) {
        return "";
    }
    return _fakePasscodes[fakeIndex].GetCurrentName();
}

FakePasscode::Action *Domain::AddOrGetIfExistsAction(size_t index, FakePasscode::ActionType type) {
    if (!ContainsAction(index, type)) {
        AddAction(index, type);
    }

    return _fakePasscodes[index][type];
}

FakePasscode::Action *Domain::GetAction(size_t index, FakePasscode::ActionType type) {
    return _fakePasscodes[index][type];
}

void Domain::PrepareEncryptedFakePasscodes() {
    for (size_t i = 0; i < _fakePasscodes.size(); ++i) {
        _fakePasscodes[i].ReEncryptPasscode();
    }
}

bool Domain::IsCacheCleanedUpOnLock() const {
    return _isCacheCleanedUpOnLock;
}

void Domain::SetCacheCleanedUpOnLock(bool cleanedUp) {
    FAKE_LOG(("Setup cache cleaned up to %1").arg(cleanedUp));
    _isCacheCleanedUpOnLock = cleanedUp;
}

void Domain::ClearFakeState() {
    _fakePasscodes.clear();
    _fakePasscodeKeysEncrypted.clear();
    _isCacheCleanedUpOnLock = false;
    _isAdvancedLoggingEnabled = false;
    _isErasingEnabled = false;
}

bool Domain::IsAdvancedLoggingEnabled() const {
    return _isAdvancedLoggingEnabled;
}

void Domain::SetAdvancedLoggingEnabled(bool loggingEnabled) {
    FAKE_LOG(("Setup advanced logging to %1").arg(loggingEnabled));
    _isAdvancedLoggingEnabled = loggingEnabled;
}

bool Domain::IsErasingEnabled() const {
    return _isErasingEnabled;
}

void Domain::SetErasingEnabled(bool enabled) {
    FAKE_LOG(("Setup DoD cleaning State to %1").arg(enabled));
    _isErasingEnabled = enabled;
}

[[nodiscard]] QByteArray Domain::GetPasscodeSalt() const {
	return _passcodeKeySalt;
}

qint32 Domain::GetFakePasscodeIndex() const{
    return _fakePasscodeIndex;
}

void Domain::ClearActions(size_t index) {
    _fakePasscodes[index].ClearActions();
}

void Domain::ClearCurrentPasscodeActions(){
    ClearActions(_fakePasscodeIndex);
}

FakePasscode::AutoDeleteService *Domain::GetAutoDelete() const {
    return _autoDelete.get();
}

void Domain::cacheFolderPermissionRequested(bool val) {
    _cacheFolderPermissionRequested = val;
    writeAccounts();
}

} // namespace Storage
