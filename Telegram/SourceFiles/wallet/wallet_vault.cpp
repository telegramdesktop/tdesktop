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
#include "main/main_domain.h"
#include "storage/details/storage_file_utilities.h"
#include "storage/serialize_common.h"
#include "storage/storage_account.h"
#include "storage/storage_domain.h"
#include "wallet/wallet_custody.h"

#include <openssl/kdf.h>

namespace Wallet {
namespace {

const auto kVaultHeaderKey = u"vault/header"_q;
constexpr auto kVaultHeaderMagic = quint32(0x57564C54);
constexpr auto kVaultRecordMagic = quint32(0x57565243);
constexpr auto kVaultFormatVersion = quint32(1);
constexpr auto kVaultRecordFormatVersion = quint32(2);
constexpr auto kVaultRecordLegacyFormatVersion = quint32(1);
constexpr auto kVaultAeadAesGcm = quint32(1);
constexpr auto kMaxVaultWraps = quint32(2);
constexpr auto kMaxVaultRecordEntries = quint32(2);
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
constexpr char kVaultRecordLabelV2[] = "tdesktop-wallet-vault/record/v2";

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

struct VaultRecordEntry {
	quint32 generation = 0;
	QByteArray nonce;
	QByteArray sealed;
};

struct VaultRecordShape {
	quint32 formatVersion = 0;
	std::vector<VaultRecordEntry> entries;
};

[[nodiscard]] QByteArray RecordAssociatedData(
		const QString &storageKey,
		quint32 generation) {
	const auto key = storageKey.toUtf8();
	auto stream = Serialize::ByteArrayWriter();
	stream.underlying().writeRawData(
		kVaultRecordLabelV2,
		int(sizeof(kVaultRecordLabelV2) - 1));
	stream.underlying().writeRawData(key.constData(), int(key.size()));
	stream << generation;
	return std::move(stream).result();
}

[[nodiscard]] std::optional<VaultRecordShape> ParseVaultRecord(
		const QByteArray &serialized) {
	auto stream = Serialize::ByteArrayReader(serialized);
	auto result = VaultRecordShape();
	auto magic = quint32();
	auto entryCount = quint32(1);
	stream >> magic >> result.formatVersion;
	if (!stream.ok() || magic != kVaultRecordMagic) {
		return std::nullopt;
	}
	const auto tagged = (result.formatVersion == kVaultRecordFormatVersion);
	if (tagged) {
		stream >> entryCount;
	} else if (result.formatVersion != kVaultRecordLegacyFormatVersion) {
		return std::nullopt;
	}
	if (!stream.ok() || !entryCount || entryCount > kMaxVaultRecordEntries) {
		return std::nullopt;
	}
	result.entries.reserve(entryCount);
	for (auto i = quint32(); i != entryCount; ++i) {
		auto entry = VaultRecordEntry();
		if (tagged) {
			stream >> entry.generation;
		}
		stream >> entry.nonce >> entry.sealed;
		const auto duplicate = ranges::contains(
			result.entries,
			entry.generation,
			&VaultRecordEntry::generation);
		if (!stream.ok()
			|| entry.nonce.size() != kVaultNonceSize
			|| duplicate) {
			return std::nullopt;
		}
		result.entries.push_back(std::move(entry));
	}
	return result;
}

[[nodiscard]] QByteArray SerializeVaultRecord(
		const std::vector<VaultRecordEntry> &entries) {
	auto stream = Serialize::ByteArrayWriter();
	stream
		<< kVaultRecordMagic
		<< kVaultRecordFormatVersion
		<< quint32(entries.size());
	for (const auto &entry : entries) {
		stream << entry.generation << entry.nonce << entry.sealed;
	}
	return std::move(stream).result();
}

[[nodiscard]] SecureBytes SerializeRecordPlaintext(
		bool requireUserPresence,
		bytes::const_span secret) {
	auto plain = Serialize::ByteArrayWriter(
		int(2 * sizeof(quint32)) + int(secret.size()));
	plain
		<< quint32(requireUserPresence ? kVaultRequireUserPresenceFlag : 0)
		<< Serialize::bytes(secret);
	auto serialized = std::move(plain).result();
	return TakeSecure(serialized);
}

[[nodiscard]] std::optional<VaultRecordEntry> SealRecordEntry(
		const SecureBytes &key,
		quint32 generation,
		const QString &storageKey,
		const SecureBytes &plaintext) {
	auto result = VaultRecordEntry{
		.generation = generation,
		.nonce = RandomBytes(kVaultNonceSize),
	};
	const auto aad = RecordAssociatedData(storageKey, generation);
	result.sealed = AesGcmSeal(
		key.span(),
		bytes::make_span(result.nonce),
		plaintext.span(),
		bytes::make_span(aad));
	if (result.sealed.isEmpty()) {
		return std::nullopt;
	}
	return result;
}

[[nodiscard]] bool IsDefinedVaultKind(quint32 kind) {
	switch (VaultKind(kind)) {
	case VaultKind::Passcode:
	case VaultKind::Open:
	case VaultKind::TouchId:
	case VaultKind::WindowsHello:
		return true;
	}
	return false;
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
	} else if (kind == quint32(VaultKind::TouchId)
		|| kind == quint32(VaultKind::WindowsHello)) {
		// Only the payload's presence is checked here: its shape belongs to
		// the provider, so a malformed one reads as Read and the provider
		// answers Corrupt, which states the vault unavailable and deletes
		// nothing.
		if (!kdfEmpty) {
			LOG(("Wallet Error: a hardware vault wrap %1 carries KDF "
				"parameters.").arg(index));
			return false;
		} else if (wrap.openSecret.isEmpty()) {
			LOG(("Wallet Error: a hardware vault wrap %1 carries no "
				"provider payload.").arg(index));
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
		if (!IsDefinedVaultKind(kind)) {
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

[[nodiscard]] std::optional<std::vector<QString>> CustodyRecordKeys(
		Storage::Account &local) {
	auto store = ReadCustodyStore(local);
	if (!store) {
		return std::nullopt;
	}
	auto result = std::vector<QString>();
	result.reserve(store->records.size() + 1);
	ForEachCustodySecretRef(*store, [&](const QString &secretRef) {
		result.push_back(VaultSecretStorageKey(secretRef));
		return false;
	});
	ranges::sort(result);
	result.erase(ranges::unique(result), end(result));
	return result;
}

struct OpenedRecord {
	QString key;
	VaultSecretRecord record;
};

[[nodiscard]] bool SameRecord(
		const std::optional<VaultSecretRecord> &opened,
		const VaultSecretRecord &expected) {
	return opened
		&& (opened->requireUserPresence == expected.requireUserPresence)
		&& (bytes::compare(opened->bytes.span(), expected.bytes.span()) == 0);
}

// A custody-named record that fails its tag under the caller's key is the
// one observable sign that the key is not this vault's: re-keying around it
// would commit a header whose only wrap opens the new key while the records
// stay sealed under a key no wrap opens, and every one of them would be lost
// for good. Refusing costs a blocked protection change instead.
[[nodiscard]] std::optional<std::vector<OpenedRecord>> OpenCustodyRecords(
		Storage::Account &local,
		const std::vector<QString> &keys,
		const SecureBytes &vaultKey,
		int &skipped) {
	using State = Storage::WalletEngineValue::State;
	auto result = std::vector<OpenedRecord>();
	result.reserve(keys.size());
	auto unreadable = 0;
	auto unopenable = 0;
	for (const auto &key : keys) {
		auto value = local.readWalletEngineValue(key);
		const auto cleanse = gsl::finally([&] {
			Cleanse(value.bytes);
		});
		if (value.state == State::Absent) {
			++skipped;
		} else if (value.state == State::Broken) {
			++unreadable;
		} else if (!IsVaultRecord(value.bytes)) {
			++skipped;
		} else if (auto opened = OpenVaultRecord(
				vaultKey,
				key,
				value.bytes)) {
			result.push_back({ .key = key, .record = std::move(*opened) });
		} else {
			++unopenable;
		}
	}
	if (unreadable || unopenable) {
		LOG(("Wallet Error: refused a vault wrap transition: %1 of %2 "
			"custody-named record(s) do not open under the vault key and %3 "
			"are unreadable."
			).arg(unopenable).arg(int(keys.size())).arg(unreadable));
		return std::nullopt;
	}
	return result;
}

[[nodiscard]] VaultTransitionResult WriteDualRecords(
		Storage::Account &local,
		const std::vector<OpenedRecord> &opened,
		const SecureBytes &oldKey,
		quint32 committed,
		const SecureBytes &newKey,
		quint32 staged,
		int &written) {
	using Result = VaultTransitionResult;
	using State = Storage::WalletEngineValue::State;
	for (const auto &[key, record] : opened) {
		const auto plaintext = SerializeRecordPlaintext(
			record.requireUserPresence,
			record.bytes.span());
		const auto oldEntry = SealRecordEntry(
			oldKey,
			committed,
			key,
			plaintext);
		const auto newEntry = SealRecordEntry(newKey, staged, key, plaintext);
		if (!oldEntry || !newEntry) {
			LOG(("Wallet Error: could not seal a custody-named record for the "
				"vault wrap transition to generation %1.").arg(staged));
			return Result::WriteFailed;
		} else if (!local.writeWalletEngineValue(
				key,
				SerializeVaultRecord({ *oldEntry, *newEntry }))) {
			return Result::WriteFailed;
		}
		auto value = local.readWalletEngineValue(key);
		const auto cleanse = gsl::finally([&] {
			Cleanse(value.bytes);
		});
		const auto shape = ParseVaultRecord(value.bytes);
		const auto verified = (value.state == State::Read)
			&& shape
			&& (shape->entries.size() == 2)
			&& (shape->entries.front().generation == committed)
			&& (shape->entries.back().generation == staged)
			&& SameRecord(OpenVaultRecord(oldKey, key, value.bytes), record)
			&& SameRecord(OpenVaultRecord(newKey, key, value.bytes), record);
		if (!verified) {
			LOG(("Wallet Error: a re-sealed custody-named record does not "
				"read back under both vault keys at generations %1 and %2."
				).arg(committed).arg(staged));
			return Result::VerifyFailed;
		}
		++written;
	}
	return Result::Done;
}

// The one rule the strip and the commit half's record check both apply: a
// value that is not a current-format vault record is neither stripped nor
// counted, so a record the strip leaves untouched is never one the commit
// refuses over, and a value the strip cannot parse never blocks a
// transition. Reading it in one place keeps the two answers equal by
// construction. The bytes are cleansed before the shape is returned; the
// shape carries entry generations, nonces and ciphertext only.
[[nodiscard]] std::optional<VaultRecordShape> ReadStrippableRecord(
		Storage::Account &local,
		const QString &key) {
	auto value = local.readWalletEngineValue(key);
	const auto cleanse = gsl::finally([&] {
		Cleanse(value.bytes);
	});
	auto shape = ParseVaultRecord(value.bytes);
	if (!shape || shape->formatVersion != kVaultRecordFormatVersion) {
		return std::nullopt;
	}
	return shape;
}

[[nodiscard]] bool StripRecords(
		Storage::Account &local,
		const std::vector<QString> &keys,
		quint32 committed,
		int &stripped,
		int &stranded) {
	auto result = true;
	for (const auto &key : keys) {
		auto shape = ReadStrippableRecord(local, key);
		if (!shape) {
			continue;
		}
		auto &entries = shape->entries;
		const auto outside = ranges::remove_if(entries, [&](
				const VaultRecordEntry &entry) {
			return (entry.generation != committed);
		});
		if (outside == end(entries)) {
			continue;
		} else if (outside == begin(entries)) {
			++stranded;
			continue;
		}
		entries.erase(outside, end(entries));
		if (local.writeWalletEngineValue(
				key,
				SerializeVaultRecord(entries))) {
			++stripped;
		} else {
			result = false;
		}
	}
	return result;
}

// The records the strip would strand once committed pointed at the staged
// generation: the strip leaves such a record untouched and the settle then
// refuses the header that would drop the wrap it needs, which keeps the
// record but leaves the vault dirty and openable by nothing the product
// reads. Counting them before committed moves is what keeps write B to the
// rule the header states - committed moves only after every record carries
// an entry at the new generation. It does not retire the settle's own
// refusal, which stays the standing backstop for both paths that reach it:
// the commit half's fail-open while the custody store does not read, and a
// dirty header this build did not write, which a reconciling read can hand
// the settle at any time.
[[nodiscard]] int CountRecordsWithoutEntry(
		Storage::Account &local,
		const std::vector<QString> &keys,
		quint32 generation) {
	auto result = 0;
	for (const auto &key : keys) {
		const auto shape = ReadStrippableRecord(local, key);
		if (!shape) {
			continue;
		} else if (!ranges::contains(
				shape->entries,
				generation,
				&VaultRecordEntry::generation)) {
			++result;
		}
	}
	return result;
}

[[nodiscard]] bool SettleToCommitted(
		Storage::Account &local,
		const VaultHeader &committedOnly) {
	const auto committed = committedOnly.committed;
	const auto keys = CustodyRecordKeys(local);
	auto stripped = 0;
	auto stranded = 0;
	if (!keys) {
		LOG(("Wallet Error: the custody store does not read, leaving the "
			"vault header dirty and the record entries outside the committed "
			"generation %1 in place until it does.").arg(committed));
		return false;
	} else if (!StripRecords(local, *keys, committed, stripped, stranded)) {
		LOG(("Wallet Error: could not strip every custody-named vault record "
			"to the committed generation %1, stripped %2."
			).arg(committed).arg(stripped));
		return false;
	} else if (stranded) {
		LOG(("Wallet Error: %1 custody-named vault record(s) carry no entry "
			"at the committed generation %2, so the wrap(s) outside it stay "
			"in the header and it stays dirty until a repair."
			).arg(stranded).arg(committed));
		return false;
	} else if (stripped) {
		LOG(("Wallet Info: stripped %1 custody-named vault record(s) to the "
			"committed generation %2.").arg(stripped).arg(committed));
	}
	return WriteVaultHeader(local, committedOnly);
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

const VaultWrap *VaultHeader::committedWrap() const {
	const auto i = ranges::find(wraps, committed, &VaultWrap::generation);
	return (i != end(wraps)) ? &*i : nullptr;
}

VaultGrant::VaultGrant(std::shared_ptr<VaultRuntime> runtime, quint32 epoch)
: _runtime(std::move(runtime))
, _epoch(epoch) {
}

VaultGrant::VaultGrant(VaultGrant &&other) noexcept
: _runtime(base::take(other._runtime))
, _epoch(other._epoch) {
}

VaultGrant &VaultGrant::operator=(VaultGrant &&other) noexcept {
	if (this != &other) {
		if (const auto runtime = base::take(_runtime)) {
			runtime->release(_epoch);
		}
		_runtime = base::take(other._runtime);
		_epoch = other._epoch;
	}
	return *this;
}

VaultGrant::~VaultGrant() {
	if (const auto runtime = base::take(_runtime)) {
		runtime->release(_epoch);
	}
}

bool VaultGrant::valid() const {
	return _runtime && (_runtime->clearEpoch() == _epoch);
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

	// A local passcode change retires an armed creation policy just as it
	// retires an unlocked key: a wrap prepared under the old passcode would
	// seal the account's first vault under a passcode it no longer has, and
	// the store creating that vault may already hold the policy on the engine
	// worker. The bumped clear epoch is what both store branches check, so a
	// term taken before the change is refused instead of written. This fires
	// from inside the passcode writer, while vault headers can be dirty, so
	// nothing here may read one: clear() touches no storage.
	Core::App().domain().local().localPasscodeChanged(
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

bool VaultRuntime::unlockOpen(Storage::Account &local) {
	auto key = OpenCommittedWrap(reading(local), VaultKind::Open, {});
	if (!key) {
		return false;
	}
	auto lock = std::lock_guard(_mutex);
	_key = std::move(*key);
	return true;
}

quint32 VaultRuntime::clearEpoch() const {
	auto lock = std::lock_guard(_mutex);
	return _clearEpoch;
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
	return VaultGrant(shared_from_this(), _clearEpoch);
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
		_grants = 0;
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

void VaultRuntime::release(quint32 epoch) {
	auto lock = std::lock_guard(_mutex);
	if (epoch != _clearEpoch) {
		return;
	}

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
		if (!SettleToCommitted(local, result.header)) {
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
		quint32 generation,
		bool requireUserPresence,
		bytes::const_span secret) {
	if (vaultKey.size() != kVaultKeySize) {
		return {};
	}
	const auto plaintext = SerializeRecordPlaintext(
		requireUserPresence,
		secret);
	const auto entry = SealRecordEntry(
		vaultKey,
		generation,
		storageKey,
		plaintext);
	if (!entry) {
		return {};
	}
	return SerializeVaultRecord({ *entry });
}

std::optional<VaultSecretRecord> OpenVaultRecord(
		const SecureBytes &vaultKey,
		const QString &storageKey,
		const QByteArray &serialized) {
	if (vaultKey.size() != kVaultKeySize) {
		return std::nullopt;
	}
	const auto shape = ParseVaultRecord(serialized);
	if (!shape) {
		return std::nullopt;
	}
	const auto tagged = (shape->formatVersion == kVaultRecordFormatVersion);
	auto plaintext = std::optional<SecureBytes>();
	for (const auto &entry : shape->entries) {
		const auto aad = tagged
			? RecordAssociatedData(storageKey, entry.generation)
			: RecordAssociatedData(storageKey);
		plaintext = AesGcmOpen(
			vaultKey.span(),
			bytes::make_span(entry.nonce),
			bytes::make_span(entry.sealed),
			bytes::make_span(aad));
		if (plaintext) {
			break;
		}
	}
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
	const auto keys = CustodyRecordKeys(local);
	if (!keys) {
		LOG(("Wallet Error: refused a vault wrap transition: the custody "
			"store does not read."));
		return Result::Refused;
	}
	auto skipped = 0;
	const auto opened = OpenCustodyRecords(local, *keys, vaultKey, skipped);
	if (!opened) {
		return Result::Refused;
	}
	const auto generation = header.committed + 1;
	auto fresh = SecureBytes(kVaultKeySize);
	bytes::set_random(fresh.span());
	next.wrap.generation = generation;
	next.wrap.blob = WrapVaultKey(fresh, next.wrap, next.wrapKey);
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
		&& WrapOpensVaultKey(*written, next.wrapKey, fresh);
	if (!verified) {
		LOG(("Wallet Error: the staged vault wrap at generation %1 does not "
			"read back, dropping it.").arg(generation));
		if (!SettleToCommitted(local, header)) {
			LOG(("Wallet Error: could not drop the staged vault wrap."));
		}
		return Result::VerifyFailed;
	}
	auto resealed = 0;
	const auto dual = WriteDualRecords(
		local,
		*opened,
		vaultKey,
		header.committed,
		fresh,
		generation,
		resealed);
	if (dual != Result::Done) {
		LOG(("Wallet Error: re-sealing the custody-named records for the "
			"vault wrap at generation %1 stopped after %2 record(s), dropping "
			"it.").arg(generation).arg(resealed));
		if (!SettleToCommitted(local, header)) {
			LOG(("Wallet Error: could not drop the staged vault wrap."));
		}
		return dual;
	}
	header = std::move(staged);
	LOG(("Wallet Info: staged vault wrap generation %1, re-sealed %2 "
		"record(s), skipped %3.").arg(generation).arg(resealed).arg(skipped));
	return Result::Done;
}

bool CommitStagedVaultWrap(
		Storage::Account &local,
		VaultHeader &header,
		VaultRuntime *runtime) {
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
	// A reconciling read between the two halves rolls the stage back on
	// disk: every custody-named record is stripped to the committed
	// generation and the committed wrap is written alone. Committing the
	// header the caller still holds would then move committed to a
	// generation no record carries an entry at and drop the only wrap that
	// opens them, so the disk is read once here and a stage it no longer
	// carries is refused: nothing is written and the previous wrap stays
	// committed.
	const auto parsed = ReadRawVaultHeader(local);
	const auto reading = DropStagedWraps(parsed);
	const auto onDisk = ranges::find(
		parsed.header.wraps,
		generation,
		&VaultWrap::generation);
	if (reading.state != VaultReading::State::Read
		|| reading.header.committed != header.committed
		|| onDisk == end(parsed.header.wraps)
		|| !SameWrap(*onDisk, *staged)) {
		LOG(("Wallet Error: refused to commit the staged vault wrap at "
			"generation %1: the disk no longer carries it beside the committed "
			"generation %2.").arg(generation).arg(header.committed));
		return false;
	}
	// The disk check above proves the header alone. A reconciling read
	// between the halves whose strip writes landed but whose header write
	// failed leaves that header intact while the records it stripped carry
	// an entry at the committed generation only. Committing then moves
	// committed past their single entry: the settle below refuses to drop
	// the wrap they still need, so nothing is lost, but the vault is left
	// dirty and the product's committed-wrap reading opens none of them. So
	// the records are asked the same question the strip will ask, before
	// committed moves, and a transition that would strand one fails instead
	// with the old wrap still committed and every record still opening under
	// it. An unreadable custody store answers nothing and is let through:
	// the settle refuses to write any header while the store does not read,
	// so no wrap is dropped either way, and refusing here instead would fail
	// a transition the next read completes.
	const auto keys = CustodyRecordKeys(local);
	const auto missing = keys
		? CountRecordsWithoutEntry(local, *keys, generation)
		: 0;
	if (missing) {
		LOG(("Wallet Error: refused to commit the staged vault wrap at "
			"generation %1: %2 custody-named record(s) carry no entry at it, "
			"leaving the committed generation %3 in place."
			).arg(generation).arg(missing).arg(header.committed));
		return false;
	}
	auto committing = header;
	committing.committed = generation;
	if (!WriteVaultHeader(local, committing)) {
		return false;
	}
	if (runtime) {
		runtime->clear();
	}
	auto settled = VaultHeader{ .committed = generation };
	settled.wraps.push_back(*staged);
	if (!SettleToCommitted(local, settled)) {
		LOG(("Wallet Error: the vault wrap transition to generation %1 "
			"committed but its cleanup did not finish; the header stays "
			"dirty and a later read retries it, with the refusal above "
			"naming why.").arg(generation));
	}
	header = std::move(settled);
	return true;
}

VaultTransitionResult TransitionVaultWrap(
		Storage::Account &local,
		VaultHeader &header,
		const SecureBytes &vaultKey,
		VaultPreparedWrap next,
		VaultRuntime *runtime) {
	const auto staged = StageVaultWrap(
		local,
		header,
		vaultKey,
		std::move(next));
	if (staged != VaultTransitionResult::Done) {
		return staged;
	}
	return CommitStagedVaultWrap(local, header, runtime)
		? VaultTransitionResult::Done
		: VaultTransitionResult::WriteFailed;
}

int DropPreVaultCustody(Storage::Account &local, CustodyStore &store) {
	auto dropped = 0;
	ForEachCustodySecretRef(store, [&](const QString &secretRef) {
		if (!DropPlainSecret(local, secretRef)) {
			return false;
		}
		++dropped;
		return true;
	});
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
