/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_vault.h"

#include "base/flat_set.h"
#include "base/openssl_help.h"
#include "base/random.h"
#include "core/application.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "storage/details/storage_file_utilities.h"
#include "storage/serialize_common.h"
#include "storage/storage_account.h"
#include "storage/storage_domain.h"
#include "wallet/wallet_custody.h"
#include "wallet/wallet_key_protection.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QThread>

#include <openssl/kdf.h>

namespace Wallet {
namespace {

constexpr auto kDeviceKeyringMagic = quint32(0x574B5247);
constexpr auto kDeviceKeyringFormatVersion = quint32(1);
constexpr auto kVaultRecordMagic = quint32(0x574B5243);
constexpr auto kVaultRecordFormatVersion = quint32(1);
constexpr auto kVaultAeadAesGcm = quint32(1);
constexpr auto kVaultBlobSize = kVaultNonceSize
	+ kVaultKeySize
	+ kVaultTagSize;
constexpr auto kKeyringEntrySize = 3 * int(sizeof(quint32))
	+ int(sizeof(quint64))
	+ kVaultKeyIdSize
	+ kVaultNonceSize
	+ kVaultKeySize
	+ kVaultTagSize;
constexpr auto kVaultRecordPlaintextPrefixSize = 2 * int(sizeof(quint32));
constexpr auto kVaultRequireUserPresenceFlag = quint32(1U << 0);

constexpr char kVaultKeyLabel[] = "tdesktop-wallet-keyring/key/v1";
constexpr char kVaultPasscodeWrapLabel[]
	= "tdesktop-wallet-keyring/passcode-wrap/v1";
constexpr char kVaultOpenWrapLabel[] = "tdesktop-wallet-keyring/open-wrap/v1";
constexpr char kKeyringEntryLabel[] = "tdesktop-wallet-keyring/entry/v1";
constexpr char kVaultRecordLabel[] = "tdesktop-wallet-keyring/record/v1";

template <std::size_t N>
[[nodiscard]] bytes::const_span Label(const char (&label)[N]) {
	return bytes::make_span(label, N - 1);
}

[[nodiscard]] const unsigned char *Unsigned(bytes::const_span data) {
	return reinterpret_cast<const unsigned char*>(data.data());
}

[[nodiscard]] unsigned char *Unsigned(bytes::span data) {
	return reinterpret_cast<unsigned char*>(data.data());
}

[[nodiscard]] QByteArray RandomBytes(int size) {
	auto result = QByteArray(size, Qt::Uninitialized);
	base::RandomFill(result.data(), result.size());
	return result;
}

void Cleanse(QByteArray &data) {
	if (!data.isEmpty()) {
		OPENSSL_cleanse(data.data(), data.size());
	}
	data = QByteArray();
}

[[nodiscard]] SecureBytes TakeSecure(QByteArray &data) {
	auto result = SecureBytes(data);
	Cleanse(data);
	return result;
}

[[nodiscard]] QByteArray AesGcmSeal(
		bytes::const_span key32,
		bytes::const_span nonce12,
		bytes::const_span plaintext,
		bytes::const_span additionalData) {
	if (key32.size() != kVaultKeySize
		|| nonce12.size() != kVaultNonceSize) {
		return {};
	}
	const auto context = EVP_CIPHER_CTX_new();
	if (!context) {
		return {};
	}
	const auto guard = gsl::finally([&] {
		EVP_CIPHER_CTX_free(context);
	});
	auto result = QByteArray(
		int(plaintext.size()) + kVaultTagSize,
		Qt::Uninitialized);
	const auto out = reinterpret_cast<unsigned char*>(result.data());
	auto outLength = 0;
	auto fullLength = 0;
	auto ok = (EVP_EncryptInit_ex(
		context,
		EVP_aes_256_gcm(),
		nullptr,
		nullptr,
		nullptr) == 1)
		&& (EVP_CIPHER_CTX_ctrl(
			context,
			EVP_CTRL_GCM_SET_IVLEN,
			kVaultNonceSize,
			nullptr) == 1)
		&& (EVP_EncryptInit_ex(
			context,
			nullptr,
			nullptr,
			Unsigned(key32),
			Unsigned(nonce12)) == 1);
	if (ok && !additionalData.empty()) {
		ok = (EVP_EncryptUpdate(
			context,
			nullptr,
			&outLength,
			Unsigned(additionalData),
			int(additionalData.size())) == 1);
	}
	if (ok && !plaintext.empty()) {
		ok = (EVP_EncryptUpdate(
			context,
			out,
			&outLength,
			Unsigned(plaintext),
			int(plaintext.size())) == 1);
		fullLength = outLength;
	}
	ok = ok
		&& (EVP_EncryptFinal_ex(context, out + fullLength, &outLength) == 1)
		&& (EVP_CIPHER_CTX_ctrl(
			context,
			EVP_CTRL_GCM_GET_TAG,
			kVaultTagSize,
			out + plaintext.size()) == 1);
	return ok ? result : QByteArray();
}

[[nodiscard]] std::optional<SecureBytes> AesGcmOpen(
		bytes::const_span key32,
		bytes::const_span nonce12,
		bytes::const_span sealed,
		bytes::const_span additionalData) {
	if (key32.size() != kVaultKeySize
		|| nonce12.size() != kVaultNonceSize
		|| sealed.size() < kVaultTagSize) {
		return std::nullopt;
	}
	const auto plaintextLength = int(sealed.size()) - kVaultTagSize;
	const auto context = EVP_CIPHER_CTX_new();
	if (!context) {
		return std::nullopt;
	}
	const auto guard = gsl::finally([&] {
		EVP_CIPHER_CTX_free(context);
	});
	auto tag = bytes::array<kVaultTagSize>();
	bytes::copy(tag, sealed.subspan(plaintextLength));
	auto result = SecureBytes(plaintextLength);
	const auto out = Unsigned(result.span());
	auto outLength = 0;
	auto fullLength = 0;
	auto ok = (EVP_DecryptInit_ex(
		context,
		EVP_aes_256_gcm(),
		nullptr,
		nullptr,
		nullptr) == 1)
		&& (EVP_CIPHER_CTX_ctrl(
			context,
			EVP_CTRL_GCM_SET_IVLEN,
			kVaultNonceSize,
			nullptr) == 1)
		&& (EVP_DecryptInit_ex(
			context,
			nullptr,
			nullptr,
			Unsigned(key32),
			Unsigned(nonce12)) == 1);
	if (ok && !additionalData.empty()) {
		ok = (EVP_DecryptUpdate(
			context,
			nullptr,
			&outLength,
			Unsigned(additionalData),
			int(additionalData.size())) == 1);
	}
	if (ok && plaintextLength > 0) {
		ok = (EVP_DecryptUpdate(
			context,
			out,
			&outLength,
			Unsigned(sealed),
			plaintextLength) == 1);
		fullLength = outLength;
	}
	ok = ok
		&& (EVP_CIPHER_CTX_ctrl(
			context,
			EVP_CTRL_GCM_SET_TAG,
			kVaultTagSize,
			tag.data()) == 1)
		&& (EVP_DecryptFinal_ex(context, out + fullLength, &outLength) == 1);
	if (!ok) {
		return std::nullopt;
	}
	return result;
}

[[nodiscard]] QByteArray WrapAssociatedData(const VaultWrap &wrap) {
	auto stream = Serialize::ByteArrayWriter();
	stream.underlying().writeRawData(
		kVaultKeyLabel,
		int(sizeof(kVaultKeyLabel) - 1));
	stream << quint32(wrap.kind);
	return std::move(stream).result();
}

[[nodiscard]] QByteArray EntryAssociatedData(
		const QByteArray &keyId,
		uint64 accountId) {
	auto stream = Serialize::ByteArrayWriter();
	stream.underlying().writeRawData(
		kKeyringEntryLabel,
		int(sizeof(kKeyringEntryLabel) - 1));
	stream << keyId << quint64(accountId);
	return std::move(stream).result();
}

[[nodiscard]] QByteArray RecordAssociatedData(
		const QString &storageKey,
		const QByteArray &keyId) {
	auto stream = Serialize::ByteArrayWriter();
	stream.underlying().writeRawData(
		kVaultRecordLabel,
		int(sizeof(kVaultRecordLabel) - 1));
	stream << keyId << storageKey;
	return std::move(stream).result();
}

struct VaultRecordShape {
	QByteArray keyId;
	QByteArray nonce;
	QByteArray sealed;
};

[[nodiscard]] std::optional<QByteArray> ReadRecordKeyId(
		Serialize::ByteArrayReader &stream) {
	auto magic = quint32();
	auto formatVersion = quint32();
	auto keyId = QByteArray();
	stream >> magic >> formatVersion >> keyId;
	if (!stream.ok()
		|| magic != kVaultRecordMagic
		|| formatVersion != kVaultRecordFormatVersion
		|| keyId.size() != kVaultKeyIdSize) {
		return std::nullopt;
	}
	return keyId;
}

[[nodiscard]] std::optional<VaultRecordShape> ParseVaultRecord(
		const QByteArray &serialized) {
	auto stream = Serialize::ByteArrayReader(serialized);
	auto keyId = ReadRecordKeyId(stream);
	if (!keyId) {
		return std::nullopt;
	}
	auto result = VaultRecordShape{ .keyId = std::move(*keyId) };
	stream >> result.nonce >> result.sealed;
	if (!stream.ok()
		|| !stream.atEnd()
		|| result.nonce.size() != kVaultNonceSize
		|| result.sealed.size() < kVaultTagSize + kVaultRecordPlaintextPrefixSize) {
		return std::nullopt;
	}
	return result;
}

[[nodiscard]] SecureBytes SerializeRecordPlaintext(
		bool requireUserPresence,
		bytes::const_span secret) {
	auto plain = Serialize::ByteArrayWriter(
		kVaultRecordPlaintextPrefixSize + int(secret.size()));
	plain
		<< quint32(requireUserPresence ? kVaultRequireUserPresenceFlag : 0)
		<< Serialize::bytes(secret);
	auto serialized = std::move(plain).result();
	return TakeSecure(serialized);
}

[[nodiscard]] bool IsDefinedVaultKind(VaultKind kind) {
	switch (kind) {
	case VaultKind::Passcode:
	case VaultKind::Open:
	case VaultKind::TouchId:
	case VaultKind::WindowsHello:
		return true;
	}
	return false;
}

[[nodiscard]] bool WrapParametersAreWellFormed(const VaultWrap &wrap) {
	if (wrap.salt.size() != kVaultSaltSize) {
		return false;
	}
	const auto &kdf = wrap.kdf;
	const auto kdfEmpty = !kdf.kind
		&& !kdf.memory
		&& !kdf.time
		&& !kdf.parallel;
	switch (wrap.kind) {
	case VaultKind::Passcode:
		return kdf.valid()
			&& kdf.costWithinLimits()
			&& wrap.openSecret.isEmpty();
	case VaultKind::Open:
		return kdfEmpty && wrap.openSecret.size() == kVaultOpenSecretSize;
	case VaultKind::TouchId:
	case VaultKind::WindowsHello:
		// The payload belongs to the native provider: only it can validate
		// its credential or encrypted key blob. Keeping that check at the
		// provider boundary preserves its typed Corrupt result, which makes
		// the live keyring unavailable without deleting the user's records.
		return kdfEmpty && !wrap.openSecret.isEmpty();
	}
	return false;
}

[[nodiscard]] bool WrapIsWellFormed(const VaultWrap &wrap) {
	return WrapParametersAreWellFormed(wrap)
		&& wrap.blob.size() == kVaultBlobSize;
}

[[nodiscard]] bool EntryIsWellFormed(const DeviceKeyringEntry &entry) {
	return entry.keyId.size() == kVaultKeyIdSize
		&& entry.accountId != 0
		&& entry.nonce.size() == kVaultNonceSize
		&& entry.sealed.size() == kVaultKeySize + kVaultTagSize;
}

[[nodiscard]] bool EntriesAreWellFormed(
		const std::vector<DeviceKeyringEntry> &entries) {
	auto ids = base::flat_set<QByteArray>();
	for (const auto &entry : entries) {
		if (!EntryIsWellFormed(entry) || !ids.emplace(entry.keyId).second) {
			return false;
		}
	}
	return true;
}

void WriteWrap(Serialize::ByteArrayWriter &stream, const VaultWrap &wrap) {
	stream
		<< quint32(wrap.kind)
		<< wrap.kdf.kind
		<< wrap.kdf.memory
		<< wrap.kdf.time
		<< wrap.kdf.parallel
		<< wrap.salt
		<< wrap.openSecret
		<< wrap.blob;
}

[[nodiscard]] bool DropObsoleteSecret(
		Storage::Account &local,
		const QString &secretRef) {
	using State = Storage::WalletEngineValue::State;
	const auto key = VaultSecretStorageKey(secretRef);
	auto value = local.readWalletEngineValue(key);
	const auto obsolete = (value.state == State::Read)
		&& !IsVaultRecord(value.bytes);
	Cleanse(value.bytes);
	return obsolete && local.removeWalletEngineValue(key);
}

} // namespace

SecureBytes HkdfSha256(
		bytes::const_span ikm,
		bytes::const_span salt,
		bytes::const_span info,
		int size) {
	if (ikm.empty() || size <= 0) {
		return {};
	}
	const auto context = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
	if (!context) {
		return {};
	}
	const auto guard = gsl::finally([&] {
		EVP_PKEY_CTX_free(context);
	});
	auto result = SecureBytes(size);
	auto length = size_t(size);
	const auto ok = (EVP_PKEY_derive_init(context) > 0)
		&& (EVP_PKEY_CTX_set_hkdf_md(context, EVP_sha256()) > 0)
		&& (salt.empty()
			|| (EVP_PKEY_CTX_set1_hkdf_salt(
				context,
				Unsigned(salt),
				int(salt.size())) > 0))
		&& (EVP_PKEY_CTX_set1_hkdf_key(
			context,
			Unsigned(ikm),
			int(ikm.size())) > 0)
		&& (info.empty()
			|| (EVP_PKEY_CTX_add1_hkdf_info(
				context,
				Unsigned(info),
				int(info.size())) > 0))
		&& (EVP_PKEY_derive(context, Unsigned(result.span()), &length) > 0)
		&& (length == size_t(size));
	if (!ok) {
		return {};
	}
	return result;
}

SecureBytes::SecureBytes(int size)
: _bytes(std::max(size, 0), char(0)) {
}

SecureBytes::SecureBytes(bytes::const_span data)
: _bytes(reinterpret_cast<const char*>(data.data()), data.size()) {
}

SecureBytes::SecureBytes(const QByteArray &data)
: _bytes(data.constData(), data.size()) {
}

SecureBytes::SecureBytes(SecureBytes &&other) noexcept
: _bytes(base::take(other._bytes)) {
}

SecureBytes &SecureBytes::operator=(SecureBytes &&other) noexcept {
	if (this != &other) {
		clear();
		_bytes = base::take(other._bytes);
	}
	return *this;
}

SecureBytes::~SecureBytes() {
	clear();
}

SecureBytes SecureBytes::copy() const {
	return SecureBytes(_bytes);
}

bytes::const_span SecureBytes::span() const {
	return bytes::make_span(_bytes);
}

bytes::span SecureBytes::span() {
	return bytes::make_detached_span(_bytes);
}

int SecureBytes::size() const {
	return int(_bytes.size());
}

bool SecureBytes::empty() const {
	return _bytes.isEmpty();
}

void SecureBytes::clear() {
	Cleanse(_bytes);
}

VaultGrant::VaultGrant(std::shared_ptr<VaultRuntime> runtime, uint64 id)
: _runtime(std::move(runtime))
, _id(id) {
}

VaultGrant::VaultGrant(VaultGrant &&other) noexcept
: _runtime(base::take(other._runtime))
, _id(other._id) {
}

VaultGrant &VaultGrant::operator=(VaultGrant &&other) noexcept {
	if (this != &other) {
		if (const auto runtime = base::take(_runtime)) {
			runtime->release(_id);
		}
		_runtime = base::take(other._runtime);
		_id = other._id;
	}
	return *this;
}

VaultGrant::~VaultGrant() {
	if (const auto runtime = base::take(_runtime)) {
		runtime->release(_id);
	}
}

bool VaultGrant::valid() const {
	return _runtime && _runtime->grantValid(_id);
}

VaultRuntime::VaultRuntime(Main::Domain &domain)
: _domain(base::make_weak(&domain))
, _retention([=] { clear(); }) {
	Core::App().passcodeLockChanges(
	) | rpl::filter(rpl::mappers::_1) | rpl::on_next([=] {
		clear();
	}, _lifetime);

	Core::App().screenIsLockedValue(
	) | rpl::filter(rpl::mappers::_1) | rpl::on_next([=] {
		clear();
	}, _lifetime);

	Core::App().systemSleepEvents(
	) | rpl::on_next([=] {
		clear();
	}, _lifetime);

	// A prepared Passcode wrap must not survive a passcode mutation. This
	// signal can fire inside key_data's checked writer, so it only clears
	// in-memory authority. In particular it never reads or rewrites the
	// keyring, or reconciles a passcode during an armed install's gap.
	domain.local().localPasscodeChanged(
	) | rpl::on_next([=] {
		clear();
	}, _lifetime);
}

VaultRuntime::~VaultRuntime() {
	clear();
}

KeyringReading VaultRuntime::reading() const {
	const auto domain = _domain.get();
	return domain
		? ReadDeviceKeyring(domain->local())
		: KeyringReading{ .state = KeyringReading::State::Broken };
}

bool VaultRuntime::unlockOpen() {
	const auto epoch = clearEpoch();
	const auto value = reading();
	if (value.state != KeyringReading::State::Read
		|| value.keyring.wrap.kind != VaultKind::Open) {
		return false;
	}
	const auto wrapKey = DeriveVaultWrapKey(value.keyring.wrap, {});
	auto key = wrapKey
		? UnwrapVaultKey(value.keyring.wrap, *wrapKey)
		: std::nullopt;
	return key && unlockWith(std::move(*key), epoch);
}

void VaultRuntime::registerAccount(Main::Session &session) {
	auto membership = AccountRecords{
		.account = base::make_weak(&session.account()),
	};
	auto &local = session.local();
	for (const auto &storageKey : local.walletEngineStorageKeys(u"secret/"_q)) {
		const auto value = local.readWalletEngineValue(storageKey);
		if (value.state != Storage::WalletEngineValue::State::Read) {
			continue;
		}
		if (const auto keyId = ReadVaultRecordKeyId(value.bytes)) {
			membership.records.emplace(storageKey, *keyId);
		}
	}
	auto lock = std::lock_guard(_mutex);
	const auto accountId = session.uniqueId();
	Expects(!_accounts.contains(accountId));
	_accounts.emplace(accountId, std::move(membership));
}

void VaultRuntime::unregisterAccount(uint64 accountId) {
	auto policy = std::optional<ArmedPolicy>();
	{
		auto lock = std::lock_guard(_mutex);
		_accounts.remove(accountId);
		for (auto i = _grants.begin(); i != _grants.end();) {
			if (i->second == accountId) {
				i = _grants.erase(i);
			} else {
				++i;
			}
		}
		if (_policy && _policy->accountId == accountId) {
			policy = base::take(_policy);
		}
		if (_grants.empty() && _retainUntil <= crl::now()) {
			_key.reset();
		}
	}
	discard(std::move(policy));
	if (unusable() && !hasLiveEntries()) {
		setUnusable(false);
	}
}

void VaultRuntime::recordStored(
		uint64 accountId,
		const QString &storageKey,
		const QByteArray &keyId) {
	Expects(keyId.size() == kVaultKeyIdSize);
	auto lock = std::lock_guard(_mutex);
	const auto i = _accounts.find(accountId);
	if (i != end(_accounts)) {
		i->second.records[storageKey] = keyId;
	}
}

void VaultRuntime::recordRemoved(uint64 accountId, const QString &storageKey) {
	{
		auto lock = std::lock_guard(_mutex);
		const auto i = _accounts.find(accountId);
		if (i != end(_accounts)) {
			i->second.records.remove(storageKey);
		}
	}
	if (unusable() && !hasLiveEntries()) {
		setUnusable(false);
	}
}

bool VaultRuntime::hasLiveEntries() const {
	const auto value = reading();
	return value.state == KeyringReading::State::Read
		&& hasLiveEntries(value.keyring);
}

bool VaultRuntime::hasLiveEntries(const DeviceKeyring &keyring) const {
	auto lock = std::lock_guard(_mutex);
	return ranges::any_of(keyring.entries, [&](const auto &entry) {
		return entryLiveLocked(entry);
	});
}

DeviceKeyring VaultRuntime::liveKeyring(DeviceKeyring keyring) const {
	auto lock = std::lock_guard(_mutex);
	keyring.entries.erase(ranges::remove_if(
		keyring.entries,
		[&](const auto &entry) { return !entryLiveLocked(entry); }
	), end(keyring.entries));
	return keyring;
}

bool VaultRuntime::entryLiveLocked(const DeviceKeyringEntry &entry) const {
	const auto i = _accounts.find(entry.accountId);
	if (i == end(_accounts)) {
		return false;
	}
	const auto account = i->second.account.get();
	return account
		&& account->sessionExists()
		&& ranges::any_of(i->second.records, [&](const auto &record) {
			return record.second == entry.keyId;
		});
}

quint32 VaultRuntime::clearEpoch() const {
	auto lock = std::lock_guard(_mutex);
	return _clearEpoch;
}

bool VaultRuntime::current(uint64 accountId, quint32 epoch) const {
	auto lock = std::lock_guard(_mutex);
	return epoch == _clearEpoch && _accounts.contains(accountId) && !_unusable;
}

bool VaultRuntime::unlockWith(SecureBytes key, quint32 epoch) {
	if (key.size() != kVaultKeySize) {
		return false;
	}
	auto lock = std::lock_guard(_mutex);
	if (epoch != _clearEpoch) {
		return false;
	}
	_key = std::move(key);
	return true;
}

VaultGrant VaultRuntime::arm(uint64 accountId, VaultPreparedWrap policy) {
	auto abandoned = std::optional<ArmedPolicy>();
	auto id = uint64();
	{
		auto lock = std::lock_guard(_mutex);
		const auto i = _accounts.find(accountId);
		if (_policy || i == end(_accounts)
			|| policy.wrapKey.size() != kVaultKeySize) {
			abandoned = ArmedPolicy{
				.prepared = std::move(policy),
				.account = (i != end(_accounts))
					? i->second.account
					: base::weak_ptr<Main::Account>(),
			};
		} else {
			id = ++_nextGrantId;
			_grants.emplace(id, accountId);
			_policy = ArmedPolicy{
				.prepared = std::move(policy),
				.account = i->second.account,
				.accountId = accountId,
				.owner = id,
			};
		}
	}
	discard(std::move(abandoned));
	return id ? VaultGrant(shared_from_this(), id) : VaultGrant();
}

VaultGrant VaultRuntime::grant(uint64 accountId) {
	auto id = uint64();
	{
		auto lock = std::lock_guard(_mutex);
		if (!_key || _unusable || !_accounts.contains(accountId)) {
			return VaultGrant();
		}
		id = ++_nextGrantId;
		_grants.emplace(id, accountId);
	}
	crl::on_main([weak = weak_from_this()] {
		if (const auto runtime = weak.lock()) {
			runtime->_granted.fire({});
		}
	});
	return VaultGrant(shared_from_this(), id);
}

void VaultRuntime::setRetention(bool fifteenMinutes) {
	{
		auto lock = std::lock_guard(_mutex);
		_retainUntil = fifteenMinutes ? (crl::now() + kVaultRetention) : 0;
	}
	if (fifteenMinutes) {
		_retention.callOnce(kVaultRetention);
	} else {
		_retention.cancel();
	}
}

void VaultRuntime::endRetention() {
	{
		auto lock = std::lock_guard(_mutex);
		_retainUntil = 0;
		if (_grants.empty()) {
			_key.reset();
		}
	}
	_retention.cancel();
}

bool VaultRuntime::retained() const {
	auto lock = std::lock_guard(_mutex);
	return _key && !_unusable && (_retainUntil > crl::now());
}

bool VaultRuntime::unlocked() const {
	auto lock = std::lock_guard(_mutex);
	return _key.has_value() && !_unusable;
}

void VaultRuntime::clear() {
	auto policy = std::optional<ArmedPolicy>();
	{
		auto lock = std::lock_guard(_mutex);
		_key.reset();
		policy = base::take(_policy);
		_retainUntil = 0;
		++_clearEpoch;
		_grants.clear();
	}
	_retention.cancel();
	discard(std::move(policy));
}

bool VaultRuntime::unusable() const {
	auto lock = std::lock_guard(_mutex);
	return _unusable;
}

void VaultRuntime::setUnusable(bool unusable) {
	if (unusable && !hasLiveEntries()) {
		return;
	}
	{
		auto lock = std::lock_guard(_mutex);
		if (_unusable == unusable) {
			return;
		}
		_unusable = unusable;
	}
	if (unusable) {
		clear();
	}
	notifyProtectionChanged(true);
}

rpl::producer<> VaultRuntime::protectionChanges() const {
	return _protectionChanges.events();
}

rpl::producer<> VaultRuntime::granted() const {
	return _granted.events();
}

void VaultRuntime::notifyProtectionChanged(bool stillUnusable) {
	{
		auto lock = std::lock_guard(_mutex);
		if (!stillUnusable) {
			_unusable = false;
		}
	}
	crl::on_main([weak = weak_from_this()] {
		if (const auto runtime = weak.lock()) {
			runtime->_protectionChanges.fire({});
		}
	});
}

std::optional<SecureBytes> VaultRuntime::keyForRead(
		uint64 accountId,
		quint32 epoch) {
	auto lock = std::lock_guard(_mutex);
	if (epoch != _clearEpoch
		|| !_accounts.contains(accountId)
		|| _unusable
		|| !_key
		|| (!hasGrantLocked(accountId) && _retainUntil <= crl::now())) {
		return std::nullopt;
	}
	return _key->copy();
}

VaultRuntime::StoreAuthority VaultRuntime::authorityForStore(
		uint64 accountId,
		quint32 epoch) {
	auto lock = std::lock_guard(_mutex);
	if (epoch != _clearEpoch
		|| !_accounts.contains(accountId)
		|| !hasGrantLocked(accountId)) {
		return {};
	}
	if (_policy && _policy->accountId == accountId) {
		auto policy = base::take(_policy);
		return {
			.key = _key ? std::make_optional(_key->copy()) : std::nullopt,
			.policy = std::move(policy->prepared),
			.owner = policy->owner,
			.epoch = _clearEpoch,
		};
	} else if (_key && !_unusable) {
		return { .key = _key->copy(), .epoch = _clearEpoch };
	}
	return {};
}

std::optional<quint32> VaultRuntime::adoptCommitted(
		SecureBytes key,
		quint32 epoch,
		uint64 owner) {
	if (key.size() != kVaultKeySize) {
		return std::nullopt;
	}
	{
		auto lock = std::lock_guard(_mutex);
		const auto i = _grants.find(owner);
		if (epoch != _clearEpoch || i == end(_grants)) {
			return std::nullopt;
		}
		const auto accountId = i->second;
		_grants.clear();
		_grants.emplace(owner, accountId);
		_retainUntil = 0;
		_key = std::move(key);
		_unusable = false;
		epoch = ++_clearEpoch;
	}
	_retention.cancel();
	return epoch;
}

bool VaultRuntime::grantValid(uint64 id) const {
	auto lock = std::lock_guard(_mutex);
	return _grants.contains(id);
}

bool VaultRuntime::hasGrantLocked(uint64 accountId) const {
	return ranges::any_of(_grants, [&](const auto &grant) {
		return grant.second == accountId;
	});
}

void VaultRuntime::release(uint64 id) {
	auto policy = std::optional<ArmedPolicy>();
	{
		auto lock = std::lock_guard(_mutex);
		if (!_grants.remove(id)) {
			return;
		}
		if (_policy && _policy->owner == id) {
			policy = base::take(_policy);
		}
		if (_grants.empty() && _retainUntil <= crl::now()) {
			_key.reset();
		}
	}
	discard(std::move(policy));
}

void VaultRuntime::discard(std::optional<ArmedPolicy> policy) {
	if (!policy) {
		return;
	}
	auto retire = [
		domain = _domain,
		account = policy->account,
		wrap = std::move(policy->prepared.wrap)
	] {
		const auto provider = ProtectionProviderFor(wrap.kind);
		if (!provider) {
			return;
		}
		auto context = account.get();
		if (!context) {
			if (const auto current = domain.get()) {
				if (!current->accounts().empty()) {
					context = current->accounts().front().account.get();
				}
			}
		}
		if (context) {
			provider->remove(&context->local(), wrap, [](ProtectionError) {});
		}
	};
	policy.reset();
	if (QThread::currentThread() == QCoreApplication::instance()->thread()) {
		retire();
	} else {
		crl::on_main(std::move(retire));
	}
}

QString VaultSecretStorageKey(const QString &secretRef) {
	return u"secret/"_q + secretRef;
}

KeyringReading ParseDeviceKeyring(const QByteArray &serialized) {
	using State = KeyringReading::State;
	auto stream = Serialize::ByteArrayReader(serialized);
	auto magic = quint32();
	auto formatVersion = quint32();
	auto aead = quint32();
	stream >> magic >> formatVersion >> aead;
	if (!stream.ok() || magic != kDeviceKeyringMagic) {
		return { .state = State::Broken };
	} else if (formatVersion != kDeviceKeyringFormatVersion
		|| aead != kVaultAeadAesGcm) {
		return { .state = State::Unsupported };
	}
	auto result = KeyringReading{ .state = State::Read };
	auto &keyring = result.keyring;
	auto &wrap = keyring.wrap;
	auto kind = quint32();
	stream
		>> kind
		>> wrap.kdf.kind
		>> wrap.kdf.memory
		>> wrap.kdf.time
		>> wrap.kdf.parallel
		>> wrap.salt
		>> wrap.openSecret
		>> wrap.blob;
	wrap.kind = VaultKind(kind);
	if (!stream.ok()) {
		return { .state = State::Broken };
	} else if (!IsDefinedVaultKind(wrap.kind)) {
		return { .state = State::Unsupported };
	} else if (!WrapIsWellFormed(wrap)) {
		return { .state = State::Broken };
	}
	auto entryCount = quint32();
	stream >> entryCount;
	if (!stream.ok()
		|| qint64(entryCount) > stream.underlying().device()->bytesAvailable()
			/ kKeyringEntrySize) {
		return { .state = State::Broken };
	}
	keyring.entries.reserve(entryCount);
	auto ids = base::flat_set<QByteArray>();
	for (auto i = quint32(); i != entryCount; ++i) {
		auto entry = DeviceKeyringEntry();
		auto accountId = quint64();
		stream >> entry.keyId >> accountId >> entry.nonce >> entry.sealed;
		entry.accountId = accountId;
		if (!stream.ok()
			|| !EntryIsWellFormed(entry)
			|| !ids.emplace(entry.keyId).second) {
			return { .state = State::Broken };
		}
		keyring.entries.push_back(std::move(entry));
	}
	if (!stream.atEnd()) {
		return { .state = State::Broken };
	}
	return result;
}

QByteArray SerializeDeviceKeyring(const DeviceKeyring &keyring) {
	if (!WrapIsWellFormed(keyring.wrap)
		|| !EntriesAreWellFormed(keyring.entries)) {
		return {};
	}
	auto stream = Serialize::ByteArrayWriter();
	stream
		<< kDeviceKeyringMagic
		<< kDeviceKeyringFormatVersion
		<< kVaultAeadAesGcm;
	WriteWrap(stream, keyring.wrap);
	stream << quint32(keyring.entries.size());
	for (const auto &entry : keyring.entries) {
		stream
			<< entry.keyId
			<< quint64(entry.accountId)
			<< entry.nonce
			<< entry.sealed;
	}
	return std::move(stream).result();
}

KeyringReading ReadDeviceKeyring(Storage::Domain &local) {
	using State = KeyringReading::State;
	using StorageState = Storage::WalletEngineValue::State;
	const auto value = local.readWalletKeyring();
	if (value.state == StorageState::Absent) {
		return { .state = State::Absent };
	} else if (value.state == StorageState::Broken) {
		return { .state = State::Broken };
	}
	return ParseDeviceKeyring(value.bytes);
}

bool WriteDeviceKeyring(
		Storage::Domain &local,
		const DeviceKeyring &keyring) {
	const auto serialized = SerializeDeviceKeyring(keyring);
	return !serialized.isEmpty() && local.writeWalletKeyring(serialized);
}

std::optional<VaultPreparedWrap> PrepareVaultPasscodeWrap(
		const QByteArray &passcode) {
	auto wrap = VaultWrap{
		.kind = VaultKind::Passcode,
		.kdf = Storage::details::DefaultPasscodeKdf(),
		.salt = RandomBytes(kVaultSaltSize),
	};
	auto wrapKey = DeriveVaultWrapKey(wrap, passcode);
	if (!wrapKey) {
		return std::nullopt;
	}
	return VaultPreparedWrap{
		.wrap = std::move(wrap),
		.wrapKey = std::move(*wrapKey),
	};
}

std::optional<VaultPreparedWrap> PrepareVaultOpenWrap() {
	auto wrap = VaultWrap{
		.kind = VaultKind::Open,
		.salt = RandomBytes(kVaultSaltSize),
		.openSecret = RandomBytes(kVaultOpenSecretSize),
	};
	auto wrapKey = DeriveVaultWrapKey(wrap, QByteArray());
	if (!wrapKey) {
		return std::nullopt;
	}
	return VaultPreparedWrap{
		.wrap = std::move(wrap),
		.wrapKey = std::move(*wrapKey),
	};
}

std::optional<SecureBytes> DeriveVaultWrapKey(
		const VaultWrap &wrap,
		const QByteArray &passcode) {
	if (!WrapParametersAreWellFormed(wrap)) {
		return std::nullopt;
	}
	auto result = SecureBytes();
	if (wrap.kind == VaultKind::Passcode) {
		if (passcode.isEmpty()) {
			return std::nullopt;
		}
		const auto ikm = Storage::details::CreatePasscodeKey(
			passcode,
			wrap.salt,
			wrap.kdf);
		if (!ikm) {
			LOG(("Wallet Error: could not derive the vault passcode key, "
				"family: %1.").arg(wrap.kdf.kind));
			return std::nullopt;
		}
		result = HkdfSha256(
			ikm->data(),
			{},
			Label(kVaultPasscodeWrapLabel),
			kVaultKeySize);
	} else if (wrap.kind == VaultKind::Open) {
		result = HkdfSha256(
			bytes::make_span(wrap.openSecret),
			bytes::make_span(wrap.salt),
			Label(kVaultOpenWrapLabel),
			kVaultKeySize);
	} else {
		return std::nullopt;
	}
	if (result.empty()) {
		LOG(("Wallet Error: could not derive the vault wrap key, kind: %1."
			).arg(quint32(wrap.kind)));
		return std::nullopt;
	}
	return result;
}

QByteArray WrapVaultKey(
		const SecureBytes &vaultKey,
		const VaultWrap &wrap,
		const SecureBytes &wrapKey) {
	if (vaultKey.size() != kVaultKeySize
		|| wrapKey.size() != kVaultKeySize
		|| !WrapParametersAreWellFormed(wrap)) {
		return {};
	}
	const auto nonce = RandomBytes(kVaultNonceSize);
	const auto aad = WrapAssociatedData(wrap);
	const auto sealed = AesGcmSeal(
		wrapKey.span(),
		bytes::make_span(nonce),
		vaultKey.span(),
		bytes::make_span(aad));
	return sealed.isEmpty() ? QByteArray() : (nonce + sealed);
}

std::optional<SecureBytes> UnwrapVaultKey(
		const VaultWrap &wrap,
		const SecureBytes &wrapKey) {
	if (!WrapIsWellFormed(wrap) || wrapKey.size() != kVaultKeySize) {
		return std::nullopt;
	}
	const auto blob = bytes::make_span(wrap.blob);
	const auto aad = WrapAssociatedData(wrap);
	auto result = AesGcmOpen(
		wrapKey.span(),
		blob.subspan(0, kVaultNonceSize),
		blob.subspan(kVaultNonceSize),
		bytes::make_span(aad));
	if (!result || result->size() != kVaultKeySize) {
		return std::nullopt;
	}
	return result;
}

std::optional<DeviceKeyringEntry> SealDeviceKeyringEntry(
		const SecureBytes &deviceKey,
		const QByteArray &keyId,
		uint64 accountId,
		const SecureBytes &key) {
	if (deviceKey.size() != kVaultKeySize
		|| keyId.size() != kVaultKeyIdSize
		|| !accountId
		|| key.size() != kVaultKeySize) {
		return std::nullopt;
	}
	auto result = DeviceKeyringEntry{
		.keyId = keyId,
		.accountId = accountId,
		.nonce = RandomBytes(kVaultNonceSize),
	};
	const auto aad = EntryAssociatedData(keyId, accountId);
	result.sealed = AesGcmSeal(
		deviceKey.span(),
		bytes::make_span(result.nonce),
		key.span(),
		bytes::make_span(aad));
	if (result.sealed.isEmpty()) {
		return std::nullopt;
	}
	return result;
}

std::optional<SecureBytes> OpenDeviceKeyringEntry(
		const SecureBytes &deviceKey,
		const DeviceKeyringEntry &entry) {
	if (deviceKey.size() != kVaultKeySize || !EntryIsWellFormed(entry)) {
		return std::nullopt;
	}
	const auto aad = EntryAssociatedData(entry.keyId, entry.accountId);
	return AesGcmOpen(
		deviceKey.span(),
		bytes::make_span(entry.nonce),
		bytes::make_span(entry.sealed),
		bytes::make_span(aad));
}

// Every store writes the ring with a fresh keyId/K before its record, keeping
// the entry named by the old record until replacement succeeds. A crash between
// writes leaves that old record openable and the new entry merely orphaned.
// Rotating D re-seals retained K entries, never the records themselves. Old D
// cannot open the new entries, while an old ring snapshot can still open its
// old records. It cannot open a later store: even a store to the same ref mints
// a fresh keyId/K that the snapshot never held.
std::optional<DeviceKeyringRotation> RotateDeviceKeyring(
		const DeviceKeyring &current,
		const SecureBytes &deviceKey,
		const VaultPreparedWrap &next) {
	if (!EntriesAreWellFormed(current.entries)
		|| (!current.entries.empty() && deviceKey.size() != kVaultKeySize)) {
		return std::nullopt;
	}
	auto result = DeviceKeyringRotation{
		.keyring = DeviceKeyring{ .wrap = next.wrap },
		.key = SecureBytes(kVaultKeySize),
	};
	bytes::set_random(result.key.span());
	auto &keyring = result.keyring;
	keyring.wrap.blob = WrapVaultKey(result.key, keyring.wrap, next.wrapKey);
	if (keyring.wrap.blob.isEmpty()) {
		return std::nullopt;
	}
	keyring.entries.reserve(current.entries.size());
	for (const auto &entry : current.entries) {
		const auto key = OpenDeviceKeyringEntry(deviceKey, entry);
		if (!key) {
			return std::nullopt;
		}
		auto sealed = SealDeviceKeyringEntry(
			result.key,
			entry.keyId,
			entry.accountId,
			*key);
		if (!sealed) {
			return std::nullopt;
		}
		keyring.entries.push_back(std::move(*sealed));
	}
	return result;
}

bool IsVaultRecord(const QByteArray &serialized) {
	return ParseVaultRecord(serialized).has_value();
}

std::optional<QByteArray> ReadVaultRecordKeyId(const QByteArray &serialized) {
	auto stream = Serialize::ByteArrayReader(serialized);
	return ReadRecordKeyId(stream);
}

QByteArray SealVaultRecord(
		const SecureBytes &key,
		const QString &storageKey,
		const QByteArray &keyId,
		bool requireUserPresence,
		bytes::const_span secret) {
	if (key.size() != kVaultKeySize
		|| storageKey.isEmpty()
		|| keyId.size() != kVaultKeyIdSize) {
		return {};
	}
	const auto nonce = RandomBytes(kVaultNonceSize);
	const auto plaintext = SerializeRecordPlaintext(
		requireUserPresence,
		secret);
	const auto aad = RecordAssociatedData(storageKey, keyId);
	const auto sealed = AesGcmSeal(
		key.span(),
		bytes::make_span(nonce),
		plaintext.span(),
		bytes::make_span(aad));
	if (sealed.isEmpty()) {
		return {};
	}
	auto stream = Serialize::ByteArrayWriter();
	stream
		<< kVaultRecordMagic
		<< kVaultRecordFormatVersion
		<< keyId
		<< nonce
		<< sealed;
	return std::move(stream).result();
}

std::optional<VaultSecretRecord> OpenVaultRecord(
		const SecureBytes &key,
		const QString &storageKey,
		const QByteArray &keyId,
		const QByteArray &serialized) {
	if (key.size() != kVaultKeySize || storageKey.isEmpty()) {
		return std::nullopt;
	}
	const auto shape = ParseVaultRecord(serialized);
	if (!shape || shape->keyId != keyId) {
		return std::nullopt;
	}
	const auto aad = RecordAssociatedData(storageKey, keyId);
	auto plaintext = AesGcmOpen(
		key.span(),
		bytes::make_span(shape->nonce),
		bytes::make_span(shape->sealed),
		bytes::make_span(aad));
	if (!plaintext) {
		return std::nullopt;
	}
	auto flags = quint32();
	auto secret = QByteArray();
	const auto cleanse = gsl::finally([&] { Cleanse(secret); });
	auto inner = Serialize::ByteArrayReader(QByteArray::fromRawData(
		reinterpret_cast<const char*>(plaintext->span().data()),
		plaintext->size()));
	inner >> flags >> secret;
	if (!inner.ok()
		|| !inner.atEnd()
		|| (flags & ~kVaultRequireUserPresenceFlag)) {
		return std::nullopt;
	}
	return VaultSecretRecord{
		.requireUserPresence = ((flags & kVaultRequireUserPresenceFlag) != 0),
		.bytes = TakeSecure(secret),
	};
}

int DropPreVaultCustody(Storage::Account &local, CustodyStore &store) {
	local.removeWalletEngineValue(u"vault/header"_q);
	auto dropped = 0;
	ForEachCustodySecretRef(store, [&](const QString &secretRef) {
		if (!DropObsoleteSecret(local, secretRef)) {
			return false;
		}
		++dropped;
		return true;
	});
	if (!dropped) {
		return 0;
	}
	LOG(("Wallet Warning: dropped %1 obsolete custody record(s) as "
		"development state.").arg(dropped));
	if (!WriteCustodyStore(local, store)) {
		LOG(("Wallet Error: could not write the custody store after "
			"dropping the obsolete records."));
	}
	return dropped;
}

bool ResetVaultAndCustody(Main::Domain &domain) {
	auto &runtime = domain.walletKeyring();
	const auto reading = runtime.reading();
	auto context = static_cast<Storage::Account*>(nullptr);
	auto removed = 0;
	for (const auto &[index, account] : domain.accounts()) {
		const auto session = account->maybeSession();
		if (!session) {
			continue;
		}
		auto &local = session->local();
		context = &local;
		for (const auto &key : local.walletEngineStorageKeys(u"secret/"_q)) {
			if (local.removeWalletEngineValue(key)) {
				++removed;
			}
			runtime.recordRemoved(session->uniqueId(), key);
		}
		for (const auto &key : local.walletEngineStorageKeys(u"custody/"_q)) {
			local.removeWalletEngineValue(key);
		}
	}
	if (!domain.local().removeWalletKeyring()) {
		LOG(("Wallet Error: device reset removed %1 secrets but could not "
			"remove the keyring.").arg(removed));
		return false;
	}
	if (context && reading.state == KeyringReading::State::Read) {
		if (const auto provider = ProtectionProviderFor(reading.keyring.wrap.kind)) {
			provider->remove(context, reading.keyring.wrap, [](ProtectionError error) {
				if (error != ProtectionError::None) {
					LOG(("Wallet Warning: retired keyring credential removal "
						"failed with %1.").arg(int(error)));
				}
			});
		}
	}
	LOG(("Wallet Info: device reset removed %1 secrets and its keyring."
		).arg(removed));
	return true;
}

} // namespace Wallet
