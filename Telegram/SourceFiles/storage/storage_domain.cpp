/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include <fakepasscode/clear_proxies.h>
#include <fakepasscode/fake_passcode.h>
#include "storage/storage_domain.h"

#include "storage/details/storage_file_utilities.h"
#include "storage/serialize_common.h"
#include "mtproto/mtproto_config.h"
#include "main/main_domain.h"
#include "main/main_account.h"
#include "base/random.h"

namespace Storage {
namespace {

using namespace details;

[[nodiscard]] QString BaseGlobalPath() {
	return cWorkingDir() + qsl("tdata/");
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
			writeAccounts();
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

	EncryptedDescriptor passKeyData(MTP::AuthKey::kSize);
	_localKey->write(passKeyData.stream);
	_passcodeKeyEncrypted = PrepareEncrypted(passKeyData, _passcodeKey);
	_hasLocalPasscode = !passcode.isEmpty();
}

Domain::StartModernResult Domain::startModern(
		const QByteArray &passcode) {
	const auto name = ComputeKeyName(_dataName);
    LOG((QString("Try to startModern with passcode ") + passcode));
    LOG(("Try to read from " + name));
	FileReadDescriptor keyData;
	if (!ReadFile(keyData, name, BaseGlobalPath())) {
		return StartModernResult::Empty;
	}
	LOG(("App Info: reading accounts info..."));

	QByteArray salt, keyEncrypted, infoEncrypted, fakePasscodeCountArray;
	keyData.stream >> salt >> keyEncrypted >> infoEncrypted >> fakePasscodeCountArray;
    LOG(("Fake bytearray size: " + QString::number(fakePasscodeCountArray.size())));
    std::vector<QByteArray> infoFakeEncrypted;
    const auto convertFromByteArray = [](QByteArray& sizeArray) -> qint32 {
        return sizeArray.toInt();
    };
    qint32 fakePasscodeCount = convertFromByteArray(fakePasscodeCountArray);

    LOG(("Fake " + QString::number(fakePasscodeCount)));

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
    _passcodeKeyEncrypted = keyEncrypted;
    _passcodeKeySalt = salt;

	EncryptedDescriptor keyInnerData, info;
	if (!DecryptLocal(keyInnerData, keyEncrypted, _passcodeKey)) {
		LOG(("App Info: could not decrypt pass-protected key from info file, "
			"maybe bad or fake password..."));
        return tryFakeStart(keyEncrypted, infoEncrypted, salt, passcode);
	}

	return startUsingKeyStream(keyInnerData, infoEncrypted, salt, passcode);
}

void Domain::writeAccounts() {
	Expects(!_owner->accounts().empty());

	const auto path = BaseGlobalPath();
	if (!QDir().exists(path)) {
		QDir().mkpath(path);
	}

    LOG(("Write to " + ComputeKeyName(_dataName)));
	FileWriteDescriptor key(ComputeKeyName(_dataName), path);
	key.writeData(_passcodeKeySalt);
	key.writeData(_passcodeKeyEncrypted);
	const auto convertToByteArray = [](qint32 size) {
	    return QByteArray::number(size);
	};

	const auto &list = _owner->accounts();

    qint32 serializedSize = 0;
    std::vector<QByteArray> serializedActions;
    std::vector<QByteArray> fakePasscodes;
    std::vector<QByteArray> fakeNames;

    for (const auto& fakePasscode : _fakePasscodes) {
        QByteArray curSerializedActions = fakePasscode.SerializeActions();
        auto fakePass = fakePasscode.GetPasscode();
        QByteArray name = fakePasscode.GetName().toUtf8();
        serializedSize += curSerializedActions.size() + fakePass.size() + name.size();
        serializedActions.push_back(std::move(curSerializedActions));
        fakePasscodes.push_back(std::move(fakePass));
        fakeNames.push_back(std::move(name));
    }

	auto keySize = sizeof(qint32) + sizeof(qint32) * list.size() + serializedSize;

	EncryptedDescriptor keyData(keySize);
	keyData.stream << qint32(list.size());
	for (const auto &[index, account] : list) {
		keyData.stream << qint32(index);
	}

    for (qint32 i = 0; i < serializedActions.size(); ++i) {
        keyData.stream << serializedActions[i];
        keyData.stream << fakePasscodes[i];
        keyData.stream << fakeNames[i];
    }

	keyData.stream << qint32(_owner->activeForStorage());
	key.writeEncrypted(keyData, _localKey);

    LOG(("Write passcodes: " + QString::number(_fakePasscodeKeysEncrypted.size())));
    LOG(("Write passcode size bytearray: " +
            QString::number(convertToByteArray(qint32(_fakePasscodeKeysEncrypted.size())).size())));
    key.writeData(convertToByteArray(qint32(_fakePasscodeKeysEncrypted.size())));
    for (const auto& fakePasscodeEncrypted : _fakePasscodeKeysEncrypted) {
        LOG(("Write encrypted of size: " + QString::number(fakePasscodeEncrypted.size())));
        key.writeData(fakePasscodeEncrypted);
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
    const auto checkKey = CreateLocalKey(passcode, _fakePasscodes[fakeIndex].getSalt());
    return checkKey->equals(_fakePasscodes[fakeIndex].GetEncryptedPasscode());
}

void Domain::setPasscode(const QByteArray &passcode) {
    LOG(("Passcode set!"));
	Expects(!_passcodeKeySalt.isEmpty());
	Expects(_localKey != nullptr);

	encryptLocalKey(passcode);
//    _fakePasscodes.clear();
//    FakePasscode::FakePasscode fake{};
//    fake.AddAction(std::make_shared<FakePasscode::ClearProxies>());
//    fake.SetSalt(_passcodeKeySalt);
//    fake.SetPasscode(QByteArray("1234"));
//    fake.setRealPasscode(passcode);
//    _fakePasscodes.emplace_back(fake);
    for (auto& fakePasscode : _fakePasscodes) {
        fakePasscode.SetSalt(_passcodeKeySalt);
        fakePasscode.setRealPasscode(passcode);
    }

    EncryptFakePasscodes();
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
    bool isFakeStarted = false;
    QByteArray sourcePasscode;
    for (qint32 i = 0; i < qint32(_fakePasscodeKeysEncrypted.size()); ++i) {
        if (salt.size() != LocalEncryptSaltSize) {
            LOG(("App Error: bad salt in info file, size: %1").arg(salt.size()));
            return StartModernResult::Failed;
        }
        _passcodeKey = CreateLocalKey(passcode, salt);
        LOG(("Try passcode " + QString::number(i) + ". Fake size: " + QString::number(_fakePasscodeKeysEncrypted[i].size())));

        EncryptedDescriptor keyInnerData;
        if (!DecryptLocal(keyInnerData, _fakePasscodeKeysEncrypted[i], _passcodeKey)) {
            LOG(("App Info: could not decrypt pass-protected key from info file, "
                 "maybe bad or fake password..."));
            continue;
        }
        isFakeStarted = true;
        keyInnerData.stream >> sourcePasscode;
        _fakePasscodes[i].SetSalt(salt);
    }

    if (isFakeStarted) {
        EncryptedDescriptor realKeyInnerData;
        DecryptLocal(realKeyInnerData, keyEncrypted, CreateLocalKey(sourcePasscode, salt));
        return startUsingKeyStream(realKeyInnerData, infoEncrypted, salt, sourcePasscode);
    } else {
        return StartModernResult::IncorrectPasscode;
    }
}

Domain::StartModernResult Domain::startUsingKeyStream(EncryptedDescriptor& keyInnerData,
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

    _hasLocalPasscode = !passcode.isEmpty();

    if (!DecryptLocal(info, infoEncrypted, _localKey)) {
        LOG(("App Error: could not decrypt info."));
        return StartModernResult::Failed;
    }
    LOG(("App Info: reading encrypted info..."));
    auto count = qint32();
    info.stream >> count;
    if (count <= 0 || count > Main::Domain::kMaxAccounts) {
        LOG(("App Error: bad accounts count: %1").arg(count));
        return StartModernResult::Failed;
    }
    auto tried = base::flat_set<int>();
    auto sessions = base::flat_set<uint64>();
    auto active = 0;
    for (auto i = 0; i != count; ++i) {
        auto index = qint32();
        info.stream >> index;
        if (index >= 0
            && index < Main::Domain::kMaxAccounts
            && tried.emplace(index).second) {
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
        }
    }
    if (sessions.empty()) {
        LOG(("App Error: no accounts read."));
        return StartModernResult::Failed;
    }

    for (qint32 i = 0; i < _fakePasscodes.size(); ++i) {
        QByteArray serializedActions, pass;
        QByteArray serializedName;
        info.stream >> serializedActions >> pass >> serializedName;
        QString name = QString::fromUtf8(serializedName);
        _fakePasscodes[i].SetSalt(salt);
        _fakePasscodes[i].SetPasscode(pass);
        _fakePasscodes[i].SetName(name);
        _fakePasscodes[i].DeSerializeActions(serializedActions);
    }

    if (!info.stream.atEnd()) {
        info.stream >> active;
    }
    _owner->activateFromStorage(active);

    Ensures(!sessions.empty());

    return StartModernResult::Success;
}

void Domain::ExecuteFake(qint32 index) {
    _fakePasscodes[index].Execute();
    _fakePasscodeIndex = index;
}

const std::vector<FakePasscode::FakePasscode> &Domain::GetFakePasscodes() const {
    return _fakePasscodes;
}

std::vector<FakePasscode::FakePasscode> &Domain::GetFakePasscodesMutable() {
    return _fakePasscodes;
}

void Domain::EncryptFakePasscodes() {
//    auto passcodeKey = CreateLocalKey(passcode, _passcodeKeySalt);
    _fakePasscodeKeysEncrypted.resize(_fakePasscodes.size());
    LOG(("Encrypt fake passcodes of size: " + QString::number(_fakePasscodes.size())));
    for (size_t i = 0; i < _fakePasscodes.size(); ++i) {
        const auto& fakePasscode = _fakePasscodes[i];
        EncryptedDescriptor passKeyData(fakePasscode.getRealPasscode().size() + MTP::AuthKey::kSize);
        passKeyData.stream << fakePasscode.getRealPasscode();
        fakePasscode.GetEncryptedPasscode()->write(passKeyData.stream);
        LOG(("Encrypt fake passcodes of size: " + fakePasscode.getRealPasscode()));
        _fakePasscodeKeysEncrypted[i] = PrepareEncrypted(passKeyData, fakePasscode.GetEncryptedPasscode());
    }
}

void Domain::AddFakePasscode(QByteArray passcode, QString name) {
    FakePasscode::FakePasscode fakePasscode;
    fakePasscode.SetSalt(_passcodeKeySalt);
    fakePasscode.SetPasscode(passcode);
    fakePasscode.setRealPasscode(_passcode);
    fakePasscode.SetName(std::move(name));
    _fakePasscodes.push_back(std::move(fakePasscode));
    EncryptFakePasscodes();
    writeAccounts();
}

void Domain::SetFakePasscode(QByteArray passcode, size_t fakeIndex) {
    _fakePasscodes[fakeIndex].SetSalt(_passcodeKeySalt);
    _fakePasscodes[fakeIndex].SetPasscode(passcode);
    _fakePasscodes[fakeIndex].setRealPasscode(_passcode);
    EncryptFakePasscodes();
    writeAccounts();
}

void Domain::SetFakePasscode(QString name, size_t fakeIndex) {
    _fakePasscodes[fakeIndex].SetSalt(_passcodeKeySalt);
    _fakePasscodes[fakeIndex].setRealPasscode(_passcode);
    _fakePasscodes[fakeIndex].SetName(std::move(name));
    EncryptFakePasscodes();
    writeAccounts();
}

void Domain::SetFakePasscode(QByteArray passcode, QString name, size_t fakeIndex) {
    _fakePasscodes[fakeIndex].SetSalt(_passcodeKeySalt);
    _fakePasscodes[fakeIndex].SetPasscode(passcode);
    _fakePasscodes[fakeIndex].setRealPasscode(_passcode);
    _fakePasscodes[fakeIndex].SetName(std::move(name));
    EncryptFakePasscodes();
    writeAccounts();
}

void Domain::RemoveFakePasscode(const FakePasscode::FakePasscode& passcode) {
    auto pos = std::find(_fakePasscodes.begin(), _fakePasscodes.end(), passcode);
    if (pos == _fakePasscodes.end()) {
        LOG(("Cannot remove passcode, because it's not exists"));
        return;
    }
    size_t index = pos - _fakePasscodes.begin();

    _fakePasscodes.erase(pos);
    _fakePasscodeKeysEncrypted.erase(_fakePasscodeKeysEncrypted.begin() + index);

    EncryptFakePasscodes();
    writeAccounts();
}

QString Domain::GetFakePasscodeName(size_t fakeIndex) const {
    if (fakeIndex >= _fakePasscodes.size()) {
        return "";
    }
    return _fakePasscodes[fakeIndex].GetName();
}

//void Domain::AddFakePasscode(FakePasscode::FakePasscode passcode) {
//    _fakePasscodes.push_back(std::move(passcode));
//    EncryptFakePasscodes();
//    writeAccounts();
//}

} // namespace Storage
