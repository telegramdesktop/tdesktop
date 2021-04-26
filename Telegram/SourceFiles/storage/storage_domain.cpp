/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/storage_domain.h"

#include "storage/details/storage_file_utilities.h"
#include "storage/serialize_common.h"
#include "mtproto/mtproto_config.h"
#include "main/main_domain.h"
#include "main/main_account.h"
#include "facades.h"

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

[[nodiscard]] QString ComputeInfoName(const QString &dataName) {
	// We dropped old test authorizations when migrated to multi auth.
	//return "info_" + dataName + (cTestMode() ? "[test]" : "");
	return "info_" + dataName;
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
	memset_rand(pass.data(), pass.size());
	memset_rand(salt.data(), salt.size());
	_localKey = CreateLocalKey(pass, salt);

	encryptLocalKey(QByteArray());
}

void Domain::encryptLocalKey(const QByteArray &passcode) {
	_passcodeKeySalt.resize(LocalEncryptSaltSize);
	memset_rand(_passcodeKeySalt.data(), _passcodeKeySalt.size());
	_passcodeKey = CreateLocalKey(passcode, _passcodeKeySalt);

	EncryptedDescriptor passKeyData(MTP::AuthKey::kSize);
	_localKey->write(passKeyData.stream);
	_passcodeKeyEncrypted = PrepareEncrypted(passKeyData, _passcodeKey);
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
	if (count <= 0 || count > Main::Domain::kMaxAccounts) {
		LOG(("App Error: bad accounts count: %1").arg(count));
		return StartModernResult::Failed;
	}

	_oldVersion = keyData.version;

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

	if (!info.stream.atEnd()) {
		info.stream >> active;
	}
	_owner->activateFromStorage(active);

	Ensures(!sessions.empty());
	return StartModernResult::Success;
}

void Domain::writeAccounts() {
	Expects(!_owner->accounts().empty());

	const auto path = BaseGlobalPath();
	if (!QDir().exists(path)) {
		QDir().mkpath(path);
	}

	FileWriteDescriptor key(ComputeKeyName(_dataName), path);
	key.writeData(_passcodeKeySalt);
	key.writeData(_passcodeKeyEncrypted);

	const auto &list = _owner->accounts();

	auto keySize = sizeof(qint32) + sizeof(qint32) * list.size();

	EncryptedDescriptor keyData(keySize);
	keyData.stream << qint32(list.size());
	for (const auto &[index, account] : list) {
		keyData.stream << qint32(index);
	}
	keyData.stream << qint32(_owner->activeForStorage());
	key.writeEncrypted(keyData, _localKey);
}

void Domain::startFromScratch() {
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

void Domain::setPasscode(const QByteArray &passcode) {
	Expects(!_passcodeKeySalt.isEmpty());
	Expects(_localKey != nullptr);

	encryptLocalKey(passcode);
	writeAccounts();

	Global::SetLocalPasscode(!passcode.isEmpty());
	Global::RefLocalPasscodeChanged().notify();
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

} // namespace Storage
