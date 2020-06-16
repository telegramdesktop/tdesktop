/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/storage_accounts.h"

#include "storage/details/storage_file_utilities.h"
#include "storage/serialize_common.h"
#include "main/main_accounts.h"
#include "main/main_account.h"
#include "facades.h"

namespace Storage {
namespace {

using namespace details;

constexpr auto kMaxAccounts = 3;

[[nodiscard]] QString BaseGlobalPath() {
	return cWorkingDir() + qsl("tdata/");
}

[[nodiscard]] QString ComputeKeyName(const QString &dataName) {
	return "key_" + dataName + (cTestMode() ? "[test]" : "");
}

[[nodiscard]] QString ComputeInfoName(const QString &dataName) {
	return "info_" + dataName + (cTestMode() ? "[test]" : "");
}

} // namespace

Accounts::Accounts(not_null<Main::Accounts*> owner, const QString &dataName)
: _owner(owner)
, _dataName(dataName) {
}

Accounts::~Accounts() = default;

StartResult Accounts::start(const QByteArray &passcode) {
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
	auto legacy = std::make_unique<Main::Account>(_dataName, 0);
	const auto result = legacy->legacyStart(passcode);
	if (result == StartResult::Success) {
		_oldVersion = legacy->local().oldMapVersion();
		startWithSingleAccount(passcode, std::move(legacy));
	}
	return result;
}

void Accounts::startAdded(not_null<Main::Account*> account) {
	Expects(_localKey != nullptr);

	account->startAdded(_localKey);
}

void Accounts::startWithSingleAccount(
		const QByteArray &passcode,
		std::unique_ptr<Main::Account> account) {
	Expects(account != nullptr);

	if (auto localKey = account->local().peekLegacyLocalKey()) {
		_localKey = std::move(localKey);
		encryptLocalKey(passcode);
	} else {
		generateLocalKey();
		account->start(_localKey);
	}
	_owner->accountAddedInStorage(0, std::move(account));
	writeAccounts();
}

void Accounts::generateLocalKey() {
	Expects(_localKey == nullptr);
	Expects(_passcodeKeySalt.isEmpty());
	Expects(_passcodeKeyEncrypted.isEmpty());

	auto pass = QByteArray(MTP::AuthKey::kSize, Qt::Uninitialized);
	auto salt = QByteArray(LocalEncryptSaltSize, Qt::Uninitialized);
	memset_rand(pass.data(), pass.size());
	memset_rand(salt.data(), salt.size());
	_localKey = CreateLocalKey(pass, salt);

	encryptLocalKey(QByteArray());
}

void Accounts::encryptLocalKey(const QByteArray &passcode) {
	_passcodeKeySalt.resize(LocalEncryptSaltSize);
	memset_rand(_passcodeKeySalt.data(), _passcodeKeySalt.size());
	_passcodeKey = CreateLocalKey(passcode, _passcodeKeySalt);

	EncryptedDescriptor passKeyData(MTP::AuthKey::kSize);
	_localKey->write(passKeyData.stream);
	_passcodeKeyEncrypted = PrepareEncrypted(passKeyData, _passcodeKey);
}

Accounts::StartModernResult Accounts::startModern(
		const QByteArray &passcode) {
	const auto name = ComputeKeyName(_dataName);

	FileReadDescriptor keyData;
	if (!ReadFile(keyData, name, BaseGlobalPath())) {
		return StartModernResult::Empty;
	}
	LOG(("App Info: reading accounts info..."));

	QByteArray salt, keyEncrypted, infoEncrypted;
	keyData.stream >> salt >> keyEncrypted >> infoEncrypted;
	if (!CheckStreamStatus(keyData.stream)) {
		return StartModernResult::Failed;
	}

	if (salt.size() != LocalEncryptSaltSize) {
		LOG(("App Error: bad salt in info file, size: %1").arg(salt.size()));
		return StartModernResult::Failed;
	}
	_passcodeKey = CreateLocalKey(passcode, salt);

	EncryptedDescriptor keyInnerData, info;
	if (!DecryptLocal(keyInnerData, keyEncrypted, _passcodeKey)) {
		LOG(("App Info: could not decrypt pass-protected key from info file, "
			"maybe bad password..."));
		return StartModernResult::IncorrectPasscode;
	}
	auto key = Serialize::read<MTP::AuthKey::Data>(keyInnerData.stream);
	if (keyInnerData.stream.status() != QDataStream::Ok
		|| !keyInnerData.stream.atEnd()) {
		LOG(("App Error: could not read pass-protected key from info file"));
		return StartModernResult::Failed;
	}
	_localKey = std::make_shared<MTP::AuthKey>(key);

	_passcodeKeyEncrypted = keyEncrypted;
	_passcodeKeySalt = salt;

	if (!DecryptLocal(info, infoEncrypted, _localKey)) {
		LOG(("App Error: could not decrypt info."));
		return StartModernResult::Failed;
	}
	LOG(("App Info: reading encrypted info..."));
	auto count = qint32();
	info.stream >> count;
	if (count <= 0 || count > kMaxAccounts) {
		LOG(("App Error: bad accounts count: %1").arg(count));
		return StartModernResult::Failed;
	}

	_oldVersion = keyData.version;

	auto tried = base::flat_set<int>();
	auto users = base::flat_set<UserId>();
	for (auto i = 0; i != count; ++i) {
		auto index = qint32();
		info.stream >> index;
		if (index >= 0
			&& index < kMaxAccounts
			&& tried.emplace(index).second) {
			auto account = std::make_unique<Main::Account>(_dataName, index);
			account->start(_localKey);
			const auto userId = account->willHaveUserId();
			if (!users.contains(userId)
				&& (userId != 0 || (users.empty() && i + 1 == count))) {
				_owner->accountAddedInStorage(index, std::move(account));
				users.emplace(userId);
			}
		}
	}

	Ensures(!users.empty());
	return StartModernResult::Success;
}

void Accounts::writeAccounts() {
	Expects(!_owner->list().empty());

	const auto path = BaseGlobalPath();
	if (!QDir().exists(path)) {
		QDir().mkpath(path);
	}

	FileWriteDescriptor key(ComputeKeyName(_dataName), path);
	key.writeData(_passcodeKeySalt);
	key.writeData(_passcodeKeyEncrypted);

	const auto &list = _owner->list();
	const auto active = _owner->activeIndex();

	auto keySize = sizeof(qint32) + sizeof(qint32) * list.size();

	EncryptedDescriptor keyData(keySize);
	keyData.stream << qint32(list.size());
	keyData.stream << qint32(active);
	for (const auto &[index, account] : list) {
		if (index != active) {
			keyData.stream << qint32(index);
		}
	}
	key.writeEncrypted(keyData, _localKey);
}

void Accounts::startFromScratch() {
	startWithSingleAccount(
		QByteArray(),
		std::make_unique<Main::Account>(_dataName, 0));
}

bool Accounts::checkPasscode(const QByteArray &passcode) const {
	Expects(!_passcodeKeySalt.isEmpty());
	Expects(_passcodeKey != nullptr);

	const auto checkKey = CreateLocalKey(passcode, _passcodeKeySalt);
	return checkKey->equals(_passcodeKey);
}

void Accounts::setPasscode(const QByteArray &passcode) {
	Expects(!_passcodeKeySalt.isEmpty());
	Expects(_localKey != nullptr);

	encryptLocalKey(passcode);
	writeAccounts();

	Global::SetLocalPasscode(!passcode.isEmpty());
	Global::RefLocalPasscodeChanged().notify();
}

int Accounts::oldVersion() const {
	return _oldVersion;
}

void Accounts::clearOldVersion() {
	_oldVersion = 0;
}

} // namespace Storage
