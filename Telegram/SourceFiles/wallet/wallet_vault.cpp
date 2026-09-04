/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_vault.h"

#include "base/openssl_help.h"
#include "base/random.h"
#include "core/application.h"
#include "storage/details/storage_file_utilities.h"
#include "storage/serialize_common.h"
#include "storage/storage_account.h"
#include "wallet/wallet_custody.h"

#include <openssl/kdf.h>

namespace Wallet {
namespace {

const auto kVaultHeaderKey = u"vault/header"_q;
constexpr auto kVaultHeaderMagic = quint32(0x57564C54);
constexpr auto kVaultRecordMagic = quint32(0x57565243);
constexpr auto kVaultFormatVersion = quint32(1);
constexpr auto kVaultAeadAesGcm = quint32(1);
constexpr auto kMaxVaultWraps = quint32(2);
constexpr auto kVaultSaltMinSize = 8;
constexpr auto kVaultBlobSize = kVaultNonceSize
	+ kVaultKeySize
	+ kVaultTagSize;
constexpr auto kVaultRequireUserPresenceFlag = quint32(1U << 0);

constexpr char kVaultKeyLabel[] = "tdesktop-wallet-vault/key/v1";
constexpr char kVaultPasscodeWrapLabel[]
	= "tdesktop-wallet-vault/passcode-wrap/v1";
constexpr char kVaultOpenWrapLabel[] = "tdesktop-wallet-vault/open-wrap/v1";
constexpr char kVaultRecordLabel[] = "tdesktop-wallet-vault/record/v1";

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

[[nodiscard]] SecureBytes HkdfSha256(
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

// The wrap's kind and generation travel as associated data so a blob moved
// between two wraps of one header, or kept from a retired generation, fails
// its tag instead of opening the vault key under the wrong wrap.
[[nodiscard]] QByteArray WrapAssociatedData(const VaultWrap &wrap) {
	auto stream = Serialize::ByteArrayWriter();
	stream.underlying().writeRawData(
		kVaultKeyLabel,
		int(sizeof(kVaultKeyLabel) - 1));
	stream << quint32(wrap.kind) << wrap.generation;
	return std::move(stream).result();
}

[[nodiscard]] QByteArray RecordAssociatedData(const QString &storageKey) {
	return QByteArray(kVaultRecordLabel) + storageKey.toUtf8();
}

[[nodiscard]] bool WrapIsWellFormed(const VaultWrap &wrap, quint32 index) {
	const auto kind = quint32(wrap.kind);
	const auto &kdf = wrap.kdf;
	const auto kdfEmpty = !kdf.kind
		&& !kdf.memory
		&& !kdf.time
		&& !kdf.parallel;
	if (!kind) {
		LOG(("Wallet Error: bad vault wrap %1 kind: 0.").arg(index));
		return false;
	} else if (wrap.salt.size() < kVaultSaltMinSize) {
		LOG(("Wallet Error: bad vault wrap %1 salt size: %2."
			).arg(index).arg(wrap.salt.size()));
		return false;
	} else if (wrap.blob.size() != kVaultBlobSize) {
		LOG(("Wallet Error: bad vault wrap %1 blob size: %2."
			).arg(index).arg(wrap.blob.size()));
		return false;
	} else if (kind == quint32(VaultKind::Passcode)) {
		if (!kdf.valid()) {
			LOG(("Wallet Error: bad vault wrap %1 KDF family: %2."
				).arg(index).arg(kdf.kind));
			return false;
		} else if (!wrap.openSecret.isEmpty()) {
			LOG(("Wallet Error: a passcode vault wrap %1 carries an open "
				"secret.").arg(index));
			return false;
		}
	} else if (kind == quint32(VaultKind::Open)) {
		if (!kdfEmpty) {
			LOG(("Wallet Error: an open vault wrap %1 carries KDF "
				"parameters.").arg(index));
			return false;
		} else if (wrap.openSecret.size() != kVaultOpenSecretSize) {
			LOG(("Wallet Error: bad vault wrap %1 open secret size: %2."
				).arg(index).arg(wrap.openSecret.size()));
			return false;
		}
	}
	return true;
}

void WriteWrap(Serialize::ByteArrayWriter &stream, const VaultWrap &wrap) {
	stream
		<< quint32(wrap.kind)
		<< wrap.generation
		<< wrap.kdf.kind
		<< wrap.kdf.memory
		<< wrap.kdf.time
		<< wrap.kdf.parallel
		<< wrap.salt
		<< wrap.openSecret
		<< wrap.blob;
}

// The raw parse keeps every wrap the header carries. ReadVaultHeader drops
// the ones outside the committed generation; the transition primitive needs
// the staged wrap above it when it reads its first write back, so the two
// stay separate.
[[nodiscard]] VaultReading ParseVaultHeader(
		const Storage::WalletEngineValue &value) {
	using State = VaultReading::State;
	using StorageState = Storage::WalletEngineValue::State;
	if (value.state == StorageState::Absent) {
		return { .state = State::Absent };
	} else if (value.state == StorageState::Broken) {
		LOG(("Wallet Error: the vault header value is unreadable."));
		return { .state = State::Broken };
	}
	auto stream = Serialize::ByteArrayReader(value.bytes);
	auto magic = quint32();
	auto formatVersion = quint32();
	auto aead = quint32();
	auto committed = quint32();
	auto wrapCount = quint32();
	stream >> magic >> formatVersion >> aead >> committed >> wrapCount;
	if (!stream.ok()) {
		LOG(("Wallet Error: the vault header is truncated."));
		return { .state = State::Broken };
	} else if (magic != kVaultHeaderMagic) {
		LOG(("Wallet Error: bad vault header magic."));
		return { .state = State::Broken };
	} else if (formatVersion > kVaultFormatVersion) {
		LOG(("Wallet Error: too new vault header format: %1."
			).arg(formatVersion));
		return { .state = State::Broken };
	} else if (aead != kVaultAeadAesGcm) {
		LOG(("Wallet Error: unknown vault header AEAD: %1.").arg(aead));
		return { .state = State::Broken };
	} else if (wrapCount > kMaxVaultWraps) {
		LOG(("Wallet Error: bad vault header wrap count: %1."
			).arg(wrapCount));
		return { .state = State::Broken };
	}
	auto result = VaultReading{ .state = State::Read };
	result.header.committed = committed;
	result.header.wraps.reserve(wrapCount);
	for (auto i = quint32(); i != wrapCount; ++i) {
		auto wrap = VaultWrap();
		auto kind = quint32();
		stream
			>> kind
			>> wrap.generation
			>> wrap.kdf.kind
			>> wrap.kdf.memory
			>> wrap.kdf.time
			>> wrap.kdf.parallel
			>> wrap.salt
			>> wrap.openSecret
			>> wrap.blob;
		wrap.kind = VaultKind(kind);
		if (!stream.ok()) {
			LOG(("Wallet Error: the vault header wrap %1 is truncated."
				).arg(i));
			return { .state = State::Broken };
		} else if (!WrapIsWellFormed(wrap, i)) {
			return { .state = State::Broken };
		}
		for (const auto &already : result.header.wraps) {
			if (already.generation == wrap.generation) {
				LOG(("Wallet Error: duplicate vault wrap generation: %1."
					).arg(wrap.generation));
				return { .state = State::Broken };
			}
		}
		if (kind >= kFirstReservedVaultKind) {
			result.state = State::Unsupported;
		}
		result.header.wraps.push_back(std::move(wrap));
	}
	return result;
}

[[nodiscard]] VaultReading ReadRawVaultHeader(Storage::Account &local) {
	return ParseVaultHeader(local.readWalletEngineValue(kVaultHeaderKey));
}

[[nodiscard]] VaultReading DropStagedWraps(VaultReading parsed) {
	using State = VaultReading::State;
	if (parsed.state != State::Read) {
		return parsed;
	}
	auto &wraps = parsed.header.wraps;
	const auto committed = parsed.header.committed;
	const auto staged = ranges::remove_if(wraps, [&](const VaultWrap &wrap) {
		return (wrap.generation != committed);
	});
	parsed.dirty = (staged != end(wraps));
	wraps.erase(staged, end(wraps));
	if (wraps.size() != 1) {
		LOG(("Wallet Error: no vault wrap at the committed generation %1."
			).arg(committed));
		return { .state = State::Broken };
	}
	return parsed;
}

[[nodiscard]] bool SameWrap(const VaultWrap &a, const VaultWrap &b) {
	return (a.kind == b.kind)
		&& (a.generation == b.generation)
		&& (a.kdf.kind == b.kdf.kind)
		&& (a.kdf.memory == b.kdf.memory)
		&& (a.kdf.time == b.kdf.time)
		&& (a.kdf.parallel == b.kdf.parallel)
		&& (a.salt == b.salt)
		&& (a.openSecret == b.openSecret)
		&& (a.blob == b.blob);
}

[[nodiscard]] bool WrapOpensVaultKey(
		const VaultWrap &wrap,
		const SecureBytes &wrapKey,
		const SecureBytes &vaultKey) {
	const auto unwrapped = UnwrapVaultKey(wrap, wrapKey);
	return unwrapped
		&& (bytes::compare(unwrapped->span(), vaultKey.span()) == 0);
}

[[nodiscard]] std::optional<SecureBytes> OpenCommittedWrap(
		const VaultReading &reading,
		VaultKind kind,
		const QByteArray &passcode) {
	if (reading.state != VaultReading::State::Read) {
		return std::nullopt;
	}
	const auto wrap = reading.header.committedWrap();
	if (!wrap || wrap->kind != kind) {
		return std::nullopt;
	}
	const auto wrapKey = DeriveVaultWrapKey(*wrap, passcode);
	if (!wrapKey) {
		return std::nullopt;
	}
	return UnwrapVaultKey(*wrap, *wrapKey);
}

[[nodiscard]] bool DropPlainSecret(
		Storage::Account &local,
		const QString &secretRef) {
	using State = Storage::WalletEngineValue::State;
	const auto key = VaultSecretStorageKey(secretRef);
	auto value = local.readWalletEngineValue(key);
	const auto plain = (value.state == State::Read)
		&& !IsVaultRecord(value.bytes);
	Cleanse(value.bytes);
	return plain && local.removeWalletEngineValue(key);
}

} // namespace

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

const VaultWrap *VaultHeader::committedWrap() const {
	const auto i = ranges::find(wraps, committed, &VaultWrap::generation);
	return (i != end(wraps)) ? &*i : nullptr;
}

VaultGrant::VaultGrant(std::shared_ptr<VaultRuntime> runtime)
: _runtime(std::move(runtime)) {
}

VaultGrant::VaultGrant(VaultGrant &&other) noexcept
: _runtime(base::take(other._runtime)) {
}

VaultGrant &VaultGrant::operator=(VaultGrant &&other) noexcept {
	if (this != &other) {
		if (const auto runtime = base::take(_runtime)) {
			runtime->release();
		}
		_runtime = base::take(other._runtime);
	}
	return *this;
}

VaultGrant::~VaultGrant() {
	if (const auto runtime = base::take(_runtime)) {
		runtime->release();
	}
}

bool VaultGrant::valid() const {
	return (_runtime != nullptr);
}

VaultRuntime::VaultRuntime()
: _retention([=] { clear(); }) {
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
}

VaultRuntime::~VaultRuntime() {
	clear();
}

VaultReading VaultRuntime::reading(Storage::Account &local) {
	return ReconcileVaultHeader(local);
}

bool VaultRuntime::unlockWithPasscode(
		Storage::Account &local,
		const QByteArray &passcode) {
	auto key = OpenCommittedWrap(
		reading(local),
		VaultKind::Passcode,
		passcode);
	if (!key) {
		return false;
	}
	auto lock = std::lock_guard(_mutex);
	_key = std::move(*key);
	return true;
}

bool VaultRuntime::unlockOpen(Storage::Account &local) {
	auto key = OpenCommittedWrap(reading(local), VaultKind::Open, {});
	if (!key) {
		return false;
	}
	auto lock = std::lock_guard(_mutex);
	_key = std::move(*key);
	return true;
}

void VaultRuntime::arm(VaultPreparedWrap policy) {
	auto lock = std::lock_guard(_mutex);
	_policy = std::move(policy);
}

VaultGrant VaultRuntime::grant() {
	auto lock = std::lock_guard(_mutex);
	if (!_key && !_policy) {
		return VaultGrant();
	}
	++_grants;
	return VaultGrant(shared_from_this());
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

bool VaultRuntime::retained() const {
	auto lock = std::lock_guard(_mutex);
	return _key && (_retainUntil > crl::now());
}

bool VaultRuntime::unlocked() const {
	auto lock = std::lock_guard(_mutex);
	return _key.has_value();
}

void VaultRuntime::clear() {
	{
		auto lock = std::lock_guard(_mutex);
		_key.reset();
		_policy.reset();
		_retainUntil = 0;
		++_clearEpoch;
	}
	_retention.cancel();
}

std::optional<SecureBytes> VaultRuntime::keyForRead() {
	auto lock = std::lock_guard(_mutex);
	if (!_key || (_grants <= 0 && _retainUntil <= crl::now())) {
		return std::nullopt;
	}
	return _key->copy();
}

VaultRuntime::StoreAuthority VaultRuntime::authorityForStore() {
	auto lock = std::lock_guard(_mutex);
	if (_grants <= 0) {
		return {};
	} else if (_key) {
		return { .key = _key->copy(), .epoch = _clearEpoch };
	}
	return { .policy = base::take(_policy), .epoch = _clearEpoch };
}

void VaultRuntime::adoptCreated(SecureBytes key, quint32 epoch) {
	auto lock = std::lock_guard(_mutex);
	if (epoch != _clearEpoch
		|| (_grants <= 0 && _retainUntil <= crl::now())) {
		return;
	}
	_key = std::move(key);
}

void VaultRuntime::release() {
	auto lock = std::lock_guard(_mutex);

	Expects(_grants > 0);

	if (--_grants > 0) {
		return;
	}
	_policy.reset();
	if (_retainUntil <= crl::now()) {
		_key.reset();
	}
}

QString VaultSecretStorageKey(const QString &secretRef) {
	return u"secret/"_q + secretRef;
}

VaultReading ReadVaultHeader(Storage::Account &local) {
	return DropStagedWraps(ReadRawVaultHeader(local));
}

VaultReading ReconcileVaultHeader(Storage::Account &local) {
	auto result = ReadVaultHeader(local);
	if (result.state == VaultReading::State::Read && result.dirty) {
		LOG(("Wallet Warning: dropping the staged vault wrap(s) outside the "
			"committed generation %1.").arg(result.header.committed));
		if (!WriteVaultHeader(local, result.header)) {
			LOG(("Wallet Error: could not rewrite the reconciled vault "
				"header."));
		}
	}
	return result;
}

bool WriteVaultHeader(Storage::Account &local, const VaultHeader &header) {
	const auto &wraps = header.wraps;
	if (wraps.empty() || wraps.size() > kMaxVaultWraps) {
		LOG(("Wallet Error: refused to write a vault header with %1 wrap(s)."
			).arg(int(wraps.size())));
		return false;
	} else if (wraps.size() == kMaxVaultWraps
		&& wraps.front().generation == wraps.back().generation) {
		LOG(("Wallet Error: refused to write a vault header with a "
			"duplicate wrap generation %1.").arg(wraps.front().generation));
		return false;
	} else if (!header.committedWrap()) {
		LOG(("Wallet Error: refused to write a vault header with no wrap at "
			"the committed generation %1.").arg(header.committed));
		return false;
	}
	auto stream = Serialize::ByteArrayWriter();
	stream
		<< kVaultHeaderMagic
		<< kVaultFormatVersion
		<< kVaultAeadAesGcm
		<< header.committed
		<< quint32(wraps.size());
	for (const auto &wrap : wraps) {
		WriteWrap(stream, wrap);
	}
	return local.writeWalletEngineValue(
		kVaultHeaderKey,
		std::move(stream).result());
}

bool RemoveVaultHeader(Storage::Account &local) {
	return local.removeWalletEngineValue(kVaultHeaderKey);
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
		if (wrap.openSecret.size() != kVaultOpenSecretSize) {
			return std::nullopt;
		}
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
		|| wrapKey.size() != kVaultKeySize) {
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
	if (wrap.blob.size() != kVaultBlobSize
		|| wrapKey.size() != kVaultKeySize) {
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

bool IsVaultRecord(const QByteArray &serialized) {
	auto stream = Serialize::ByteArrayReader(serialized);
	auto magic = quint32();
	stream >> magic;
	return stream.ok() && (magic == kVaultRecordMagic);
}

QByteArray SealVaultRecord(
		const SecureBytes &vaultKey,
		const QString &storageKey,
		bool requireUserPresence,
		bytes::const_span secret) {
	if (vaultKey.size() != kVaultKeySize) {
		return {};
	}
	auto plain = Serialize::ByteArrayWriter(
		int(2 * sizeof(quint32)) + int(secret.size()));
	plain
		<< quint32(requireUserPresence ? kVaultRequireUserPresenceFlag : 0)
		<< Serialize::bytes(secret);
	auto serialized = std::move(plain).result();
	const auto plaintext = TakeSecure(serialized);
	const auto nonce = RandomBytes(kVaultNonceSize);
	const auto aad = RecordAssociatedData(storageKey);
	const auto sealed = AesGcmSeal(
		vaultKey.span(),
		bytes::make_span(nonce),
		plaintext.span(),
		bytes::make_span(aad));
	if (sealed.isEmpty()) {
		return {};
	}
	auto stream = Serialize::ByteArrayWriter();
	stream << kVaultRecordMagic << kVaultFormatVersion << nonce << sealed;
	return std::move(stream).result();
}

std::optional<VaultSecretRecord> OpenVaultRecord(
		const SecureBytes &vaultKey,
		const QString &storageKey,
		const QByteArray &serialized) {
	if (vaultKey.size() != kVaultKeySize) {
		return std::nullopt;
	}
	auto stream = Serialize::ByteArrayReader(serialized);
	auto magic = quint32();
	auto formatVersion = quint32();
	auto nonce = QByteArray();
	auto sealed = QByteArray();
	stream >> magic >> formatVersion >> nonce >> sealed;
	if (!stream.ok()
		|| magic != kVaultRecordMagic
		|| formatVersion > kVaultFormatVersion
		|| nonce.size() != kVaultNonceSize) {
		return std::nullopt;
	}
	const auto aad = RecordAssociatedData(storageKey);
	const auto plaintext = AesGcmOpen(
		vaultKey.span(),
		bytes::make_span(nonce),
		bytes::make_span(sealed),
		bytes::make_span(aad));
	if (!plaintext) {
		return std::nullopt;
	}
	auto flags = quint32();
	auto secret = QByteArray();
	auto inner = Serialize::ByteArrayReader(QByteArray::fromRawData(
		reinterpret_cast<const char*>(plaintext->span().data()),
		plaintext->size()));
	inner >> flags >> secret;
	if (!inner.ok()) {
		return std::nullopt;
	}
	return VaultSecretRecord{
		.requireUserPresence = ((flags & kVaultRequireUserPresenceFlag)
			== kVaultRequireUserPresenceFlag),
		.bytes = TakeSecure(secret),
	};
}

VaultTransitionResult StageVaultWrap(
		Storage::Account &local,
		VaultHeader &header,
		const SecureBytes &vaultKey,
		VaultPreparedWrap next) {
	using Result = VaultTransitionResult;
	if (header.wraps.size() != 1 || !header.committedWrap()) {
		LOG(("Wallet Error: refused a vault wrap transition from a header "
			"with %1 wrap(s) at the committed generation %2."
			).arg(int(header.wraps.size())).arg(header.committed));
		return Result::Refused;
	} else if (next.wrapKey.empty()) {
		LOG(("Wallet Error: refused a vault wrap transition without a wrap "
			"key, kind: %1.").arg(quint32(next.wrap.kind)));
		return Result::Refused;
	}
	const auto generation = header.committed + 1;
	next.wrap.generation = generation;
	next.wrap.blob = WrapVaultKey(vaultKey, next.wrap, next.wrapKey);
	if (next.wrap.blob.isEmpty()) {
		LOG(("Wallet Error: could not wrap the vault key for the transition "
			"to generation %1, kind: %2."
			).arg(generation).arg(quint32(next.wrap.kind)));
		return Result::Refused;
	}
	auto staged = header;
	staged.wraps.push_back(next.wrap);
	if (!WriteVaultHeader(local, staged)) {
		return Result::WriteFailed;
	}
	const auto parsed = ReadRawVaultHeader(local);
	const auto reading = DropStagedWraps(parsed);
	const auto written = ranges::find(
		parsed.header.wraps,
		generation,
		&VaultWrap::generation);
	const auto verified = (reading.state == VaultReading::State::Read)
		&& reading.dirty
		&& (reading.header.committed == header.committed)
		&& (written != end(parsed.header.wraps))
		&& SameWrap(*written, next.wrap)
		&& WrapOpensVaultKey(*written, next.wrapKey, vaultKey);
	if (!verified) {
		LOG(("Wallet Error: the staged vault wrap at generation %1 does not "
			"read back, dropping it.").arg(generation));
		if (!WriteVaultHeader(local, header)) {
			LOG(("Wallet Error: could not drop the staged vault wrap."));
		}
		return Result::VerifyFailed;
	}
	header = std::move(staged);
	return Result::Done;
}

bool CommitStagedVaultWrap(Storage::Account &local, VaultHeader &header) {
	const auto generation = header.committed + 1;
	const auto staged = ranges::find(
		header.wraps,
		generation,
		&VaultWrap::generation);
	if (header.wraps.size() != 2
		|| staged == end(header.wraps)
		|| !header.committedWrap()) {
		LOG(("Wallet Error: refused to commit a staged vault wrap from a "
			"header with %1 wrap(s) at the committed generation %2."
			).arg(int(header.wraps.size())).arg(header.committed));
		return false;
	}
	auto updated = VaultHeader{ .committed = generation };
	updated.wraps.push_back(*staged);
	if (!WriteVaultHeader(local, updated)) {
		return false;
	}
	header = std::move(updated);
	return true;
}

VaultTransitionResult TransitionVaultWrap(
		Storage::Account &local,
		VaultHeader &header,
		const SecureBytes &vaultKey,
		VaultPreparedWrap next) {
	const auto staged = StageVaultWrap(
		local,
		header,
		vaultKey,
		std::move(next));
	if (staged != VaultTransitionResult::Done) {
		return staged;
	}
	return CommitStagedVaultWrap(local, header)
		? VaultTransitionResult::Done
		: VaultTransitionResult::WriteFailed;
}

int DropPreVaultCustody(Storage::Account &local, CustodyStore &store) {
	auto &records = store.records;
	const auto plain = ranges::remove_if(records, [&](
			const CustodyRecord &record) {
		return DropPlainSecret(local, record.secretRef);
	});
	auto dropped = int(end(records) - plain);
	records.erase(plain, end(records));
	if (store.pendingRotation
		&& DropPlainSecret(local, store.pendingRotation->secretRef)) {
		store.pendingRotation = std::nullopt;
		++dropped;
	}
	if (!dropped) {
		return 0;
	}
	LOG(("Wallet Warning: dropped %1 pre-vault custody record(s) as "
		"development state.").arg(dropped));
	if (!WriteCustodyStore(local, store)) {
		LOG(("Wallet Error: could not write the custody store after "
			"dropping the pre-vault records."));
	}
	return dropped;
}

} // namespace Wallet
