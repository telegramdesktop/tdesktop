/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/wallet_protection_mac.h"

#include "base/platform/mac/base_utilities_mac.h"
#include "base/random.h"
#include "base/system_unlock.h"
#include "lang/lang_keys.h"
#include "storage/serialize_common.h"
#include "wallet/wallet_key_protection.h"
#include "wallet/wallet_vault.h"

#include <Foundation/Foundation.h>
#include <LocalAuthentication/LocalAuthentication.h>
#include <Security/Security.h>

namespace Platform {
namespace {

using Wallet::ProtectionError;

constexpr auto kPayloadVersion = quint32(1);

// kSecAttrTokenOID, undeclared in the public SDK: the SEP-wrapped key blob
// CryptoKit persists as a Secure Enclave key's dataRepresentation. It must
// travel as this ATTRIBUTE - keyData is ignored for a token key, and the
// blob passed as keyData alone silently mints a new key (probe variants
// A/C/D against B/H/J); EnclaveRoundTrips() below detects that shape.
const auto kTokenOIDAttribute = CFSTR("toid");

const auto kAlgorithm
	= kSecKeyAlgorithmECIESEncryptionCofactorVariableIVX963SHA256AESGCM;

const auto kCryptoTokenKitDomain = CFSTR("CryptoTokenKit");
constexpr auto kCryptoTokenKitCorruptedData = CFIndex(-3);
constexpr auto kCryptoTokenKitCanceledByUser = CFIndex(-4);
constexpr auto kCryptoTokenKitAuthenticationFailed = CFIndex(-5);

struct Payload {
	QByteArray keyBlob;
	QByteArray sealedWrapKey;
};

struct UnwrapOperation {
	~UnwrapOperation();

	LAContext *context = nil;
	SecKeyRef key = nullptr;
	QByteArray sealedWrapKey;
	Wallet::VaultWrap wrap;
	Fn<void(Wallet::ProtectionUnwrapResult)> done;
};

[[nodiscard]] QByteArray SerializePayload(const Payload &payload) {
	auto stream = Serialize::ByteArrayWriter(int(sizeof(quint32))
		+ Serialize::bytearraySize(payload.keyBlob)
		+ Serialize::bytearraySize(payload.sealedWrapKey));
	stream << kPayloadVersion << payload.keyBlob << payload.sealedWrapKey;
	return std::move(stream).result();
}

[[nodiscard]] std::optional<Payload> ParsePayload(const QByteArray &data) {
	auto stream = Serialize::ByteArrayReader(data);
	auto version = quint32();
	auto result = Payload();
	stream >> version >> result.keyBlob >> result.sealedWrapKey;
	if (!stream.ok()
		|| !stream.atEnd()
		|| version != kPayloadVersion
		|| result.keyBlob.isEmpty()
		|| result.sealedWrapKey.isEmpty()) {
		return std::nullopt;
	}
	return result;
}

template <typename T>
[[nodiscard]] auto ReleaseLater(T &ref) {
	return gsl::finally([&] {
		if (ref) {
			CFRelease(ref);
		}
	});
}

void LogError(CFErrorRef error) {
	if (!error) {
		return;
	}
	const auto domain = CFErrorGetDomain(error);
	LOG(("Wallet Error: Touch ID protection failed, domain '%1', code %2."
		).arg(domain ? QString::fromCFString(domain) : QString()
		).arg(CFErrorGetCode(error)));
}

[[nodiscard]] QByteArray RandomBytes(int size) {
	auto result = QByteArray(size, Qt::Uninitialized);
	base::RandomFill(result.data(), result.size());
	return result;
}

[[nodiscard]] QByteArray TokenBlob(SecKeyRef key) {
	const auto attributes = SecKeyCopyAttributes(key);
	if (!attributes) {
		return QByteArray();
	}
	const auto releaseAttributes = ReleaseLater(attributes);
	const auto value = CFDictionaryGetValue(attributes, kTokenOIDAttribute);
	return (value && CFGetTypeID(value) == CFDataGetTypeID())
		? QByteArray::fromCFData((CFDataRef)value)
		: QByteArray();
}

[[nodiscard]] QByteArray PublicKeyBytes(SecKeyRef key) {
	const auto publicKey = SecKeyCopyPublicKey(key);
	if (!publicKey) {
		return QByteArray();
	}
	const auto releasePublicKey = ReleaseLater(publicKey);
	CFErrorRef error = nullptr;
	const auto releaseError = ReleaseLater(error);
	const auto data = SecKeyCopyExternalRepresentation(publicKey, &error);
	if (!data) {
		return QByteArray();
	}
	const auto releaseData = ReleaseLater(data);
	return QByteArray::fromCFData(data);
}

[[nodiscard]] SecKeyRef CreateEnclaveKey(CFErrorRef *error) {
	const auto access = SecAccessControlCreateWithFlags(
		kCFAllocatorDefault,
		kSecAttrAccessibleWhenUnlockedThisDeviceOnly,
		kSecAccessControlPrivateKeyUsage | kSecAccessControlUserPresence,
		error);
	if (!access) {
		return nullptr;
	}
	const auto releaseAccess = ReleaseLater(access);
	NSDictionary *attributes = @{
		(id)kSecAttrKeyType: (id)kSecAttrKeyTypeECSECPrimeRandom,
		(id)kSecAttrKeySizeInBits: @256,
		(id)kSecAttrTokenID: (id)kSecAttrTokenIDSecureEnclave,
		(id)kSecPrivateKeyAttrs: @{
			(id)kSecAttrIsPermanent: @NO,
			(id)kSecAttrAccessControl: (id)access,
		},
	};
	return SecKeyCreateRandomKey((CFDictionaryRef)attributes, error);
}

[[nodiscard]] SecKeyRef RestoreEnclaveKey(
		const QByteArray &blob,
		LAContext *context,
		CFErrorRef *error) {
	NSDictionary *attributes = @{
		(id)kSecAttrKeyType: (id)kSecAttrKeyTypeECSECPrimeRandom,
		(id)kSecAttrKeyClass: (id)kSecAttrKeyClassPrivate,
		(id)kSecAttrTokenID: (id)kSecAttrTokenIDSecureEnclave,
		(id)kTokenOIDAttribute: blob.toNSData(),
		(id)kSecAttrKeySizeInBits: @256,
		(id)kSecUseAuthenticationContext: context,
	};
	return SecKeyCreateWithData(
		(CFDataRef)[NSData data],
		(CFDictionaryRef)attributes,
		error);
}

[[nodiscard]] bool OwnerAuthenticationPossible() {
	return rpl::variable<base::SystemUnlockAvailability>(
		base::SystemUnlockStatus()).current().available;
}

[[nodiscard]] bool EnclaveRoundTrips() {
	CFErrorRef error = nullptr;
	const auto releaseError = ReleaseLater(error);
	NSDictionary *attributes = @{
		(id)kSecAttrKeyType: (id)kSecAttrKeyTypeECSECPrimeRandom,
		(id)kSecAttrKeySizeInBits: @256,
		(id)kSecAttrTokenID: (id)kSecAttrTokenIDSecureEnclave,
		(id)kSecPrivateKeyAttrs: @{ (id)kSecAttrIsPermanent: @NO },
	};
	const auto original = SecKeyCreateRandomKey(
		(CFDictionaryRef)attributes,
		&error);
	if (!original) {
		LogError(error);
		return false;
	}
	const auto releaseOriginal = ReleaseLater(original);
	const auto blob = TokenBlob(original);
	if (blob.isEmpty()) {
		return false;
	}
	LAContext *context = [[LAContext alloc] init];
	const auto releaseContext = gsl::finally([&] { [context release]; });
	context.interactionNotAllowed = YES;
	const auto restored = RestoreEnclaveKey(blob, context, &error);
	if (!restored) {
		LogError(error);
		return false;
	}
	const auto releaseRestored = ReleaseLater(restored);
	const auto publicKey = PublicKeyBytes(original);
	return !publicKey.isEmpty() && (PublicKeyBytes(restored) == publicKey);
}

[[nodiscard]] bool EvaluateAvailability() {
	@autoreleasepool {

	return OwnerAuthenticationPossible() && EnclaveRoundTrips();

	}
}

[[nodiscard]] ProtectionError ClassifyAuthentication(CFIndex code) {
	switch (code) {
	case LAErrorUserCancel:
	case LAErrorUserFallback:
	case LAErrorSystemCancel:
	case LAErrorAppCancel:
		return ProtectionError::Cancelled;
	case LAErrorAuthenticationFailed:
	case LAErrorBiometryLockout:
		return ProtectionError::AuthenticationFailed;
	}
	return ProtectionError::Unavailable;
}

[[nodiscard]] ProtectionError ClassifyToken(CFIndex code) {
	switch (code) {
	case kCryptoTokenKitCorruptedData:
		return ProtectionError::Absent;
	case kCryptoTokenKitCanceledByUser:
		return ProtectionError::Cancelled;
	case kCryptoTokenKitAuthenticationFailed:
		return ProtectionError::AuthenticationFailed;
	}
	return ProtectionError::Unavailable;
}

[[nodiscard]] ProtectionError ClassifyStatus(CFIndex code) {
	switch (code) {
	case errSecUserCanceled:
		return ProtectionError::Cancelled;
	case errSecAuthFailed:
		return ProtectionError::AuthenticationFailed;
	case errSecParam:
		return ProtectionError::Corrupt;
	}
	return ProtectionError::Unavailable;
}

// The verdict follows the CFError's domain and code alone. Only
// CryptoTokenKit's corrupted-blob code is Absent: the probe proved it at
// restore and at first use, before any authentication and without UI. The
// sheet's cancels and refusals are typed, an ECIES tag failure after
// presence is Corrupt, and everything else - a not-interactive or invalid
// context included - stays Unavailable, which preserves the keyring. A
// foreign Mac's blob is expected to surface as that same corrupted-blob
// code, its SEP wrap failing its own authentication; that is assumed from
// one Mac, not established, and a different code lands Unavailable, the
// fail-safe side. CryptoTokenKit's codes are spelled out above because its
// header is not among this file's.
[[nodiscard]] ProtectionError Classify(CFErrorRef error) {
	if (!error) {
		return ProtectionError::Unavailable;
	}
	LogError(error);
	const auto domain = CFErrorGetDomain(error);
	const auto code = CFErrorGetCode(error);
	const auto matches = [&](CFStringRef expected) {
		return domain
			&& (CFStringCompare(domain, expected, 0) == kCFCompareEqualTo);
	};
	return matches((CFStringRef)LAErrorDomain)
		? ClassifyAuthentication(code)
		: matches(kCryptoTokenKitDomain)
		? ClassifyToken(code)
		: matches((CFStringRef)NSOSStatusErrorDomain)
		? ClassifyStatus(code)
		: ProtectionError::Unavailable;
}

[[nodiscard]] QByteArray SealWrapKey(
		SecKeyRef key,
		const Wallet::SecureBytes &wrapKey,
		CFErrorRef *error) {
	const auto publicKey = SecKeyCopyPublicKey(key);
	if (!publicKey) {
		return QByteArray();
	}
	const auto releasePublicKey = ReleaseLater(publicKey);
	const auto span = wrapKey.span();
	const auto plain = CFDataCreateWithBytesNoCopy(
		kCFAllocatorDefault,
		reinterpret_cast<const UInt8*>(span.data()),
		span.size(),
		kCFAllocatorNull);
	const auto releasePlain = ReleaseLater(plain);
	const auto sealed = SecKeyCreateEncryptedData(
		publicKey,
		kAlgorithm,
		plain,
		error);
	if (!sealed) {
		return QByteArray();
	}
	const auto releaseSealed = ReleaseLater(sealed);
	return QByteArray::fromCFData(sealed);
}

[[nodiscard]] Wallet::ProtectionEnrollResult Enroll() {
	@autoreleasepool {

	CFErrorRef error = nullptr;
	const auto releaseError = ReleaseLater(error);
	const auto key = CreateEnclaveKey(&error);
	if (!key) {
		LogError(error);
		return { .error = ProtectionError::Unavailable };
	}
	const auto releaseKey = ReleaseLater(key);
	auto payload = Payload{ .keyBlob = TokenBlob(key) };
	if (payload.keyBlob.isEmpty()) {
		return { .error = ProtectionError::Unavailable };
	}
	auto wrapKey = Wallet::SecureBytes(Wallet::kVaultKeySize);
	base::RandomFill(wrapKey.span());
	payload.sealedWrapKey = SealWrapKey(key, wrapKey, &error);
	if (payload.sealedWrapKey.isEmpty()) {
		LogError(error);
		return { .error = ProtectionError::Unavailable };
	}
	return {
		.wrap = Wallet::VaultPreparedWrap{
			.wrap = Wallet::VaultWrap{
				.kind = Wallet::VaultKind::TouchId,
				.salt = RandomBytes(Wallet::kVaultSaltSize),
				.openSecret = SerializePayload(payload),
			},
			.wrapKey = std::move(wrapKey),
		},
		.error = ProtectionError::None,
	};

	}
}

[[nodiscard]] ProtectionError PrepareUnwrap(
		UnwrapOperation &op,
		const QByteArray &keyBlob) {
	@autoreleasepool {

	CFErrorRef error = nullptr;
	const auto releaseError = ReleaseLater(error);
	op.context = [[LAContext alloc] init];
	op.context.localizedReason = Q2NSString(
		tr::lng_wallet_protection_touchid_reason(tr::now));
	op.key = RestoreEnclaveKey(keyBlob, op.context, &error);
	return op.key ? ProtectionError::None : Classify(error);

	}
}

// The plaintext CFData is a copy of the wrap key that cannot be cleansed;
// it lives only until this function returns.
[[nodiscard]] Wallet::SecureBytes DecryptWrapKey(
		const UnwrapOperation &op,
		CFErrorRef *error) {
	const auto sealed = op.sealedWrapKey.toCFData();
	const auto releaseSealed = ReleaseLater(sealed);
	const auto plain = SecKeyCreateDecryptedData(
		op.key,
		kAlgorithm,
		sealed,
		error);
	if (!plain) {
		return Wallet::SecureBytes();
	}
	const auto releasePlain = ReleaseLater(plain);
	return Wallet::SecureBytes(bytes::make_span(
		CFDataGetBytePtr(plain),
		CFDataGetLength(plain)));
}

[[nodiscard]] Wallet::ProtectionUnwrapResult Unseal(
		const UnwrapOperation &op) {
	CFErrorRef error = nullptr;
	const auto releaseError = ReleaseLater(error);
	const auto wrapKey = DecryptWrapKey(op, &error);
	if (wrapKey.empty()) {
		return { .error = Classify(error) };
	}
	auto key = Wallet::UnwrapVaultKey(op.wrap, wrapKey);
	if (!key) {
		return { .error = ProtectionError::Corrupt };
	}
	return { .key = std::move(key), .error = ProtectionError::None };
}

void AnswerUnwrap(
		Fn<void(Wallet::ProtectionUnwrapResult)> done,
		ProtectionError error) {
	crl::on_main([done = std::move(done), error] {
		done({ .error = error });
	});
}

class TouchIdProtection final : public Wallet::ProtectionProvider {
public:
	[[nodiscard]] Wallet::VaultKind kind() const override;
	[[nodiscard]] rpl::producer<bool> available() const override;
	[[nodiscard]] rpl::producer<QString> title() const override;
	[[nodiscard]] rpl::producer<QString> description() const override;
	[[nodiscard]] rpl::producer<QString> label() const override;

	void enroll(
		not_null<Storage::Account*> local,
		Fn<void(Wallet::ProtectionEnrollResult)> done) override;
	void unwrap(
		not_null<Storage::Account*> local,
		Wallet::VaultWrap wrap,
		Fn<void(Wallet::ProtectionUnwrapResult)> done) override;
	void remove(
		not_null<Storage::Account*> local,
		Wallet::VaultWrap wrap,
		Fn<void(ProtectionError)> done) override;

private:
	mutable std::optional<rpl::variable<bool>> _available;

};

UnwrapOperation::~UnwrapOperation() {
	[context release];
	if (key) {
		CFRelease(key);
	}
}

Wallet::VaultKind TouchIdProtection::kind() const {
	return Wallet::VaultKind::TouchId;
}

rpl::producer<bool> TouchIdProtection::available() const {
	// Evaluated once: neither fact changes meaningfully within a process.
	if (!_available) {
		_available.emplace(EvaluateAvailability());
	}
	return _available->value();
}

rpl::producer<QString> TouchIdProtection::title() const {
	return tr::lng_settings_use_touchid();
}

rpl::producer<QString> TouchIdProtection::description() const {
	return tr::lng_wallet_protection_touchid_about();
}

rpl::producer<QString> TouchIdProtection::label() const {
	return tr::lng_wallet_protection_touchid_label();
}

void TouchIdProtection::enroll(
		not_null<Storage::Account*> local,
		Fn<void(Wallet::ProtectionEnrollResult)> done) {
	crl::on_main([done = std::move(done), result = Enroll()]() mutable {
		done(std::move(result));
	});
}

void TouchIdProtection::unwrap(
		not_null<Storage::Account*> local,
		Wallet::VaultWrap wrap,
		Fn<void(Wallet::ProtectionUnwrapResult)> done) {
	auto payload = ParsePayload(wrap.openSecret);
	if (!payload) {
		AnswerUnwrap(std::move(done), ProtectionError::Corrupt);
		return;
	}
	auto op = std::make_shared<UnwrapOperation>();
	op->sealedWrapKey = std::move(payload->sealedWrapKey);
	op->wrap = std::move(wrap);
	op->done = std::move(done);
	const auto error = PrepareUnwrap(*op, payload->keyBlob);
	if (error != ProtectionError::None) {
		AnswerUnwrap(std::move(op->done), error);
		return;
	}
	crl::async([op = std::move(op)]() mutable {
		auto result = Unseal(*op);
		crl::on_main([op = std::move(op), result = std::move(result)]() mutable {
			op->done(std::move(result));
		});
	});
}

void TouchIdProtection::remove(
		not_null<Storage::Account*> local,
		Wallet::VaultWrap wrap,
		Fn<void(ProtectionError)> done) {
	// The enclave key exists only in the retired device wrap's blob.
	crl::on_main([done = std::move(done)] {
		done(ProtectionError::None);
	});
}

} // namespace

void RegisterWalletProtectionProvider() {
	Wallet::RegisterProtectionProvider(
		std::make_unique<TouchIdProtection>());
}

} // namespace Platform
