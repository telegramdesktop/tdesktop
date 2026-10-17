/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/update_verify.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>

extern "C" {
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
} // extern "C"

#include <array>
#include <cstring>
#include <memory>

namespace Core::Updates {
namespace {

constexpr auto kMaxManifestSize = 256 * 1024;
constexpr auto kMaxManifestSignatureSize = 256;
constexpr auto kMaxSignatures = 16;
constexpr auto kMaxKeyIdSize = 64;
constexpr auto kMaxSignatureSize = 512;
constexpr auto kMaxKeys = 64;
constexpr auto kMaxChannels = 16;
constexpr auto kMaxGroups = 8;
constexpr auto kMaxGroupKeys = 16;
constexpr auto kMaxRevoked = 64;
constexpr auto kRawSignatureSize = 64;
constexpr auto kRawCoordinateSize = 32;
constexpr auto kSha256Size = 32;

struct EvpPkeyDeleter {
	void operator()(EVP_PKEY *value) {
		EVP_PKEY_free(value);
	}
};

struct EvpMdCtxDeleter {
	void operator()(EVP_MD_CTX *value) {
		EVP_MD_CTX_free(value);
	}
};

struct EcdsaSigDeleter {
	void operator()(ECDSA_SIG *value) {
		ECDSA_SIG_free(value);
	}
};

struct BioDeleter {
	void operator()(BIO *value) {
		BIO_free(value);
	}
};

using EvpPkey = std::unique_ptr<EVP_PKEY, EvpPkeyDeleter>;
using EvpMdCtx = std::unique_ptr<EVP_MD_CTX, EvpMdCtxDeleter>;

void SetError(QString *error, const QString &text) {
	if (error) {
		*error = text;
	}
}

[[nodiscard]] QByteArray Sha256(const QByteArray &data) {
	auto result = QByteArray(kSha256Size, Qt::Uninitialized);
	auto length = unsigned(0);
	if (!EVP_Digest(
			data.constData(),
			size_t(data.size()),
			reinterpret_cast<uchar*>(result.data()),
			&length,
			EVP_sha256(),
			nullptr)
		|| length != kSha256Size) {
		return QByteArray();
	}
	return result;
}

[[nodiscard]] std::optional<QByteArray> FromBase64Url(
		const QJsonValue &value,
		int expectedSize) {
	if (!value.isString()) {
		return std::nullopt;
	}
	const auto decoded = QByteArray::fromBase64Encoding(
		value.toString().toLatin1(),
		QByteArray::Base64UrlEncoding
			| QByteArray::AbortOnBase64DecodingErrors);
	if (!decoded || decoded.decoded.size() != expectedSize) {
		return std::nullopt;
	}
	return decoded.decoded;
}

[[nodiscard]] std::optional<qint64> ReadInt64(const QJsonValue &value) {
	if (!value.isDouble()) {
		return std::nullopt;
	}
	const auto number = value.toDouble();
	if (!(number > -9007199254740992.) || !(number < 9007199254740992.)) {
		return std::nullopt;
	}
	const auto result = qint64(number);
	if (double(result) != number) {
		return std::nullopt;
	}
	return result;
}

[[nodiscard]] EvpPkey LoadEd25519Pem(const QByteArray &pem) {
	const auto bio = std::unique_ptr<BIO, BioDeleter>(
		BIO_new_mem_buf(pem.constData(), pem.size()));
	if (!bio) {
		return nullptr;
	}
	auto result = EvpPkey(
		PEM_read_bio_PUBKEY(bio.get(), nullptr, nullptr, nullptr));
	if (result && EVP_PKEY_id(result.get()) != EVP_PKEY_ED25519) {
		return nullptr;
	}
	return result;
}

[[nodiscard]] EvpPkey MakeEd25519(const QByteArray &x) {
	if (x.size() != kRawCoordinateSize) {
		return nullptr;
	}
	return EvpPkey(EVP_PKEY_new_raw_public_key(
		EVP_PKEY_ED25519,
		nullptr,
		reinterpret_cast<const uchar*>(x.constData()),
		size_t(x.size())));
}

[[nodiscard]] EvpPkey MakeEs256(const QByteArray &x, const QByteArray &y) {
	if (x.size() != kRawCoordinateSize || y.size() != kRawCoordinateSize) {
		return nullptr;
	}

	// The SubjectPublicKeyInfo DER prefix for an uncompressed P-256 point,
	// followed by 0x04 || x || y, parsed with d2i_PUBKEY.
	static const uchar kSpkiPrefix[] = {
		0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce,
		0x3d, 0x02, 0x01, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d,
		0x03, 0x01, 0x07, 0x03, 0x42, 0x00,
	};
	auto der = QByteArray();
	der.reserve(int(sizeof(kSpkiPrefix)) + 1 + x.size() + y.size());
	der.append(
		reinterpret_cast<const char*>(kSpkiPrefix),
		sizeof(kSpkiPrefix));
	der.append(char(0x04));
	der.append(x);
	der.append(y);

	const auto *data = reinterpret_cast<const uchar*>(der.constData());
	return EvpPkey(d2i_PUBKEY(nullptr, &data, long(der.size())));
}

[[nodiscard]] bool VerifyEd25519(
		EVP_PKEY *key,
		const QByteArray &message,
		const QByteArray &signature) {
	if (!key || signature.size() != kRawSignatureSize) {
		return false;
	}
	const auto context = EvpMdCtx(EVP_MD_CTX_new());
	if (!context
		|| EVP_DigestVerifyInit(
			context.get(),
			nullptr,
			nullptr,
			nullptr,
			key) != 1) {
		return false;
	}
	return EVP_DigestVerify(
		context.get(),
		reinterpret_cast<const uchar*>(signature.constData()),
		size_t(signature.size()),
		reinterpret_cast<const uchar*>(message.constData()),
		size_t(message.size())) == 1;
}

// The envelope stores ES256 signatures as raw r||s (64 bytes) exactly as
// produced by Azure Key Vault, so the client converts r||s to DER here
// before handing them to the OpenSSL EVP verifier.
[[nodiscard]] QByteArray Es256RawToDer(const QByteArray &raw) {
	if (raw.size() != kRawSignatureSize) {
		return QByteArray();
	}
	const auto *bytes = reinterpret_cast<const uchar*>(raw.constData());
	const auto r = BN_bin2bn(bytes, kRawCoordinateSize, nullptr);
	const auto s = BN_bin2bn(
		bytes + kRawCoordinateSize,
		kRawCoordinateSize,
		nullptr);
	if (!r || !s) {
		BN_free(r);
		BN_free(s);
		return QByteArray();
	}
	const auto sig = std::unique_ptr<ECDSA_SIG, EcdsaSigDeleter>(
		ECDSA_SIG_new());
	if (!sig || ECDSA_SIG_set0(sig.get(), r, s) != 1) {
		BN_free(r);
		BN_free(s);
		return QByteArray();
	}
	const auto length = i2d_ECDSA_SIG(sig.get(), nullptr);
	if (length <= 0) {
		return QByteArray();
	}
	auto result = QByteArray(length, Qt::Uninitialized);
	auto *out = reinterpret_cast<uchar*>(result.data());
	if (i2d_ECDSA_SIG(sig.get(), &out) != length) {
		return QByteArray();
	}
	return result;
}

[[nodiscard]] bool VerifyEs256(
		EVP_PKEY *key,
		const QByteArray &message,
		const QByteArray &signature) {
	const auto der = Es256RawToDer(signature);
	if (!key || der.isEmpty()) {
		return false;
	}
	const auto context = EvpMdCtx(EVP_MD_CTX_new());
	if (!context
		|| EVP_DigestVerifyInit(
			context.get(),
			nullptr,
			EVP_sha256(),
			nullptr,
			key) != 1) {
		return false;
	}
	return EVP_DigestVerify(
		context.get(),
		reinterpret_cast<const uchar*>(der.constData()),
		size_t(der.size()),
		reinterpret_cast<const uchar*>(message.constData()),
		size_t(message.size())) == 1;
}

[[nodiscard]] std::optional<std::vector<ManifestKey>> ParseManifestKeys(
		const QJsonValue &value) {
	if (!value.isArray()) {
		return std::nullopt;
	}
	const auto list = value.toArray();
	if (list.size() > kMaxKeys) {
		return std::nullopt;
	}
	auto result = std::vector<ManifestKey>();
	result.reserve(size_t(list.size()));
	for (const auto &entry : list) {
		if (!entry.isObject()) {
			return std::nullopt;
		}
		const auto object = entry.toObject();
		const auto id = object.value(QLatin1String("id"));
		if (!id.isString()) {
			return std::nullopt;
		}
		auto key = ManifestKey();
		key.id = id.toString().toLatin1();
		if (key.id.isEmpty() || key.id.size() > kMaxKeyIdSize) {
			return std::nullopt;
		}
		for (const auto &existing : result) {
			if (existing.id == key.id) {
				return std::nullopt;
			}
		}
		if (const auto expires = object.value(QLatin1String("expires"))
			; !expires.isUndefined()) {
			const auto parsed = ReadInt64(expires);
			if (!parsed || *parsed <= 0) {
				return std::nullopt;
			}
			key.expires = *parsed;
		}
		const auto alg = object.value(QLatin1String("alg"));
		if (!alg.isString()) {
			return std::nullopt;
		}
		const auto algName = alg.toString();
		if (algName == QLatin1String("Ed25519")) {
			key.ed25519 = true;
			const auto x = FromBase64Url(
				object.value(QLatin1String("x")),
				kRawCoordinateSize);
			if (!x) {
				return std::nullopt;
			}
			key.x = *x;
		} else if (algName == QLatin1String("ES256")) {
			const auto crv = object.value(QLatin1String("crv"));
			if (!crv.isString()
				|| crv.toString() != QLatin1String("P-256")) {
				return std::nullopt;
			}
			const auto x = FromBase64Url(
				object.value(QLatin1String("x")),
				kRawCoordinateSize);
			const auto y = FromBase64Url(
				object.value(QLatin1String("y")),
				kRawCoordinateSize);
			if (!x || !y) {
				return std::nullopt;
			}
			key.x = *x;
			key.y = *y;
		} else {
			// An unknown algorithm never weakens verification: the key
			// simply can never satisfy a group on this client, while a
			// newer manifest listing it must stay adoptable.
			continue;
		}
		result.push_back(std::move(key));
	}
	return result;
}

[[nodiscard]] auto ParseManifestChannels(const QJsonValue &value)
-> std::optional<std::map<QByteArray, std::vector<std::vector<QByteArray>>>> {
	if (!value.isObject()) {
		return std::nullopt;
	}
	const auto object = value.toObject();
	if (object.size() > kMaxChannels) {
		return std::nullopt;
	}
	auto result = std::map<QByteArray, std::vector<std::vector<QByteArray>>>();
	for (auto i = object.constBegin(); i != object.constEnd(); ++i) {
		if (!i.value().isArray()) {
			return std::nullopt;
		}
		const auto groupsList = i.value().toArray();
		if (groupsList.isEmpty() || groupsList.size() > kMaxGroups) {
			return std::nullopt;
		}
		auto groups = std::vector<std::vector<QByteArray>>();
		groups.reserve(size_t(groupsList.size()));
		for (const auto &groupValue : groupsList) {
			if (!groupValue.isArray()) {
				return std::nullopt;
			}
			const auto idsList = groupValue.toArray();
			if (idsList.isEmpty() || idsList.size() > kMaxGroupKeys) {
				return std::nullopt;
			}
			auto ids = std::vector<QByteArray>();
			ids.reserve(size_t(idsList.size()));
			for (const auto &idValue : idsList) {
				if (!idValue.isString()) {
					return std::nullopt;
				}
				const auto id = idValue.toString().toLatin1();
				if (id.isEmpty() || id.size() > kMaxKeyIdSize) {
					return std::nullopt;
				}
				ids.push_back(id);
			}
			groups.push_back(std::move(ids));
		}
		result.emplace(i.key().toLatin1(), std::move(groups));
	}
	return result;
}

[[nodiscard]] std::optional<std::vector<QByteArray>> ParseManifestRevoked(
		const QJsonValue &value) {
	if (!value.isArray()) {
		return std::nullopt;
	}
	const auto list = value.toArray();
	if (list.size() > kMaxRevoked) {
		return std::nullopt;
	}
	auto result = std::vector<QByteArray>();
	result.reserve(size_t(list.size()));
	for (const auto &entry : list) {
		if (!entry.isString()) {
			return std::nullopt;
		}
		const auto id = entry.toString().toLatin1();
		if (id.isEmpty() || id.size() > kMaxKeyIdSize) {
			return std::nullopt;
		}
		result.push_back(id);
	}
	return result;
}

struct Reader {
	const uchar *data = nullptr;
	size_t size = 0;
	size_t offset = 0;

	[[nodiscard]] bool read(void *to, size_t count) {
		if (count > size - offset) {
			return false;
		}
		memcpy(to, data + offset, count);
		offset += count;
		return true;
	}

	[[nodiscard]] std::optional<quint8> readU8() {
		auto result = quint8();
		return read(&result, sizeof(result))
			? std::make_optional(result)
			: std::nullopt;
	}

	[[nodiscard]] std::optional<quint32> readU32() {
		auto bytes = std::array<uchar, 4>();
		if (!read(bytes.data(), bytes.size())) {
			return std::nullopt;
		}
		return quint32(bytes[0])
			| (quint32(bytes[1]) << 8)
			| (quint32(bytes[2]) << 16)
			| (quint32(bytes[3]) << 24);
	}

	[[nodiscard]] std::optional<quint64> readU64() {
		const auto low = readU32();
		const auto high = readU32();
		if (!low || !high) {
			return std::nullopt;
		}
		return quint64(*low) | (quint64(*high) << 32);
	}

	[[nodiscard]] std::optional<QByteArray> readBytes(quint32 count) {
		if (size_t(count) > size - offset) {
			return std::nullopt;
		}
		auto result = QByteArray(
			reinterpret_cast<const char*>(data + offset),
			qsizetype(count));
		offset += count;
		return result;
	}
};

} // namespace

QByteArray ChannelName(Channel channel) {
	switch (channel) {
	case Channel::Stable: return QByteArrayLiteral("stable");
	case Channel::Beta: return QByteArrayLiteral("beta");
	case Channel::CanaryPublic: return QByteArrayLiteral("canary-public");
	case Channel::CanaryPrivate: return QByteArrayLiteral("canary-private");
	}
	return QByteArray();
}

std::optional<Channel> ChannelFromName(const QByteArray &name) {
	if (name == "stable") {
		return Channel::Stable;
	} else if (name == "beta") {
		return Channel::Beta;
	} else if (name == "canary-public") {
		return Channel::CanaryPublic;
	} else if (name == "canary-private") {
		return Channel::CanaryPrivate;
	}
	return std::nullopt;
}

QByteArray OsName(Os os) {
	switch (os) {
	case Os::Windows: return QByteArrayLiteral("win");
	case Os::Mac: return QByteArrayLiteral("mac");
	case Os::Linux: return QByteArrayLiteral("linux");
	}
	return QByteArray();
}

QByteArray ArchName(Arch arch) {
	switch (arch) {
	case Arch::X86: return QByteArrayLiteral("x86");
	case Arch::X64: return QByteArrayLiteral("x64");
	case Arch::Arm: return QByteArrayLiteral("arm");
	}
	return QByteArray();
}

std::optional<Target> TargetFromPlatformKey(const QByteArray &key) {
	if (key == "win") {
		return Target{ Os::Windows, Arch::X86 };
	} else if (key == "win64") {
		return Target{ Os::Windows, Arch::X64 };
	} else if (key == "winarm") {
		return Target{ Os::Windows, Arch::Arm };
	} else if (key == "mac") {
		return Target{ Os::Mac, Arch::X64 };
	} else if (key == "armac") {
		return Target{ Os::Mac, Arch::Arm };
	} else if (key == "linux") {
		return Target{ Os::Linux, Arch::X64 };
	}
	return std::nullopt;
}

std::optional<Manifest> ParseVerifiedManifest(
		const QByteArray &json,
		const QByteArray &signature,
		const QByteArray &rootPublicKeyPem,
		QString *error) {
	if (json.isEmpty() || json.size() > kMaxManifestSize) {
		SetError(error, QStringLiteral("Bad manifest size."));
		return std::nullopt;
	}

	// The signature is checked before the JSON parser ever sees the bytes,
	// so the parser only runs on root-authenticated input.
	const auto root = LoadEd25519Pem(rootPublicKeyPem);
	if (!root) {
		SetError(error, QStringLiteral("Could not load the root key."));
		return std::nullopt;
	}
	if (!VerifyEd25519(root.get(), json, signature)) {
		SetError(error, QStringLiteral("Bad manifest root signature."));
		return std::nullopt;
	}

	auto parseError = QJsonParseError();
	const auto document = QJsonDocument::fromJson(json, &parseError);
	if (parseError.error != QJsonParseError::NoError
		|| !document.isObject()) {
		SetError(error, QStringLiteral("Could not parse manifest JSON."));
		return std::nullopt;
	}
	const auto object = document.object();

	const auto format = object.value(QLatin1String("format"));
	if (!format.isDouble() || format.toDouble() != 1.) {
		SetError(error, QStringLiteral("Bad manifest format."));
		return std::nullopt;
	}
	const auto version = ReadInt64(
		object.value(QLatin1String("manifest_version")));
	if (!version || *version < 1 || *version > qint64(0xFFFFFFFFLL)) {
		SetError(error, QStringLiteral("Bad manifest version."));
		return std::nullopt;
	}

	auto result = Manifest();
	result.version = quint32(*version);
	result.issued = ReadInt64(
		object.value(QLatin1String("issued"))).value_or(0);
	result.expires = ReadInt64(
		object.value(QLatin1String("expires"))).value_or(0);

	auto keys = ParseManifestKeys(object.value(QLatin1String("keys")));
	if (!keys) {
		SetError(error, QStringLiteral("Bad manifest keys."));
		return std::nullopt;
	}
	result.keys = std::move(*keys);

	auto channels = ParseManifestChannels(
		object.value(QLatin1String("channels")));
	if (!channels) {
		SetError(error, QStringLiteral("Bad manifest channels."));
		return std::nullopt;
	}
	result.channels = std::move(*channels);

	auto revoked = ParseManifestRevoked(
		object.value(QLatin1String("revoked")));
	if (!revoked) {
		SetError(error, QStringLiteral("Bad manifest revoked list."));
		return std::nullopt;
	}
	result.revoked = std::move(*revoked);

	result.bytes = json;
	result.signature = signature;
	return result;
}

bool IsV2UpdateFile(const QByteArray &data) {
	return data.size() >= 4 && !memcmp(data.constData(), kEnvelopeMagic, 4);
}

std::optional<Envelope> ParseEnvelope(
		const QByteArray &data,
		QString *error) {
	auto reader = Reader{
		reinterpret_cast<const uchar*>(data.constData()),
		size_t(data.size()),
	};

	if (!IsV2UpdateFile(data)) {
		SetError(error, QStringLiteral("Bad envelope magic."));
		return std::nullopt;
	}
	reader.offset = 4;

	const auto format = reader.readU32();
	if (!format || *format != kEnvelopeFormat) {
		SetError(error, QStringLiteral("Bad envelope format."));
		return std::nullopt;
	}

	auto result = Envelope();
	const auto channel = reader.readU8();
	if (!channel || *channel > quint8(Channel::CanaryPrivate)) {
		SetError(error, QStringLiteral("Bad envelope channel."));
		return std::nullopt;
	}
	result.channel = Channel(*channel);

	const auto os = reader.readU8();
	if (!os || *os > quint8(Os::Linux)) {
		SetError(error, QStringLiteral("Bad envelope os."));
		return std::nullopt;
	}
	const auto arch = reader.readU8();
	if (!arch || *arch > quint8(Arch::Arm)) {
		SetError(error, QStringLiteral("Bad envelope arch."));
		return std::nullopt;
	}
	result.target = Target{ Os(*os), Arch(*arch) };

	const auto version = reader.readU64();
	if (!version) {
		SetError(error, QStringLiteral("Bad envelope version."));
		return std::nullopt;
	}
	result.version = *version;

	const auto created = reader.readU64();
	if (!created) {
		SetError(error, QStringLiteral("Bad envelope timestamp."));
		return std::nullopt;
	}
	result.created = qint64(*created);

	const auto manifestLength = reader.readU32();
	if (!manifestLength
		|| !*manifestLength
		|| *manifestLength > kMaxManifestSize) {
		SetError(error, QStringLiteral("Bad envelope manifest size."));
		return std::nullopt;
	}
	const auto manifest = reader.readBytes(*manifestLength);
	if (!manifest) {
		SetError(error, QStringLiteral("Bad envelope manifest."));
		return std::nullopt;
	}
	result.manifest = *manifest;
	result.signedRegion = data.left(int(reader.offset));

	const auto manifestSigLength = reader.readU32();
	if (!manifestSigLength
		|| !*manifestSigLength
		|| *manifestSigLength > kMaxManifestSignatureSize) {
		SetError(
			error,
			QStringLiteral("Bad envelope manifest signature size."));
		return std::nullopt;
	}
	const auto manifestSig = reader.readBytes(*manifestSigLength);
	if (!manifestSig) {
		SetError(error, QStringLiteral("Bad envelope manifest signature."));
		return std::nullopt;
	}
	result.manifestSignature = *manifestSig;

	const auto count = reader.readU32();
	if (!count || *count > kMaxSignatures) {
		SetError(error, QStringLiteral("Bad envelope signature count."));
		return std::nullopt;
	}
	result.signatures.reserve(size_t(*count));
	for (auto i = quint32(0); i != *count; ++i) {
		const auto idLength = reader.readU32();
		if (!idLength || !*idLength || *idLength > kMaxKeyIdSize) {
			SetError(error, QStringLiteral("Bad envelope key id size."));
			return std::nullopt;
		}
		const auto id = reader.readBytes(*idLength);
		if (!id) {
			SetError(error, QStringLiteral("Bad envelope key id."));
			return std::nullopt;
		}
		const auto sigLength = reader.readU32();
		if (!sigLength || !*sigLength || *sigLength > kMaxSignatureSize) {
			SetError(error, QStringLiteral("Bad envelope signature size."));
			return std::nullopt;
		}
		const auto sig = reader.readBytes(*sigLength);
		if (!sig) {
			SetError(error, QStringLiteral("Bad envelope signature."));
			return std::nullopt;
		}
		result.signatures.push_back({ *id, *sig });
	}

	const auto payloadLength = reader.readU32();
	if (!payloadLength
		|| !*payloadLength
		|| *payloadLength > kMaxPayloadSize) {
		SetError(error, QStringLiteral("Bad envelope payload size."));
		return std::nullopt;
	}
	const auto payload = reader.readBytes(*payloadLength);
	if (!payload || reader.offset != reader.size) {
		SetError(error, QStringLiteral("Bad envelope payload."));
		return std::nullopt;
	}
	result.payload = *payload;

	return result;
}

QByteArray SigningInput(const Envelope &envelope) {
	const auto hash = Sha256(envelope.payload);
	if (hash.isEmpty() || envelope.signedRegion.isEmpty()) {
		return QByteArray();
	}
	return envelope.signedRegion + hash;
}

bool VerifySignature(
		const ManifestKey &key,
		const QByteArray &signingInput,
		const QByteArray &signature,
		QString *error) {
	if (signingInput.isEmpty()) {
		SetError(error, QStringLiteral("Empty signing input."));
		return false;
	}
	const auto pkey = key.ed25519
		? MakeEd25519(key.x)
		: MakeEs256(key.x, key.y);
	if (!pkey) {
		SetError(error, QStringLiteral("Could not build the public key."));
		return false;
	}
	const auto result = key.ed25519
		? VerifyEd25519(pkey.get(), signingInput, signature)
		: VerifyEs256(pkey.get(), signingInput, signature);
	if (!result) {
		SetError(error, QStringLiteral("Bad signature."));
	}
	return result;
}

bool VerifyChannelAuthorization(
		const Manifest &manifest,
		Channel channel,
		const QByteArray &signingInput,
		const std::vector<EnvelopeSignature> &signatures,
		qint64 now,
		QString *error) {
	const auto i = manifest.channels.find(ChannelName(channel));
	if (i == manifest.channels.end() || i->second.empty()) {
		SetError(error, QStringLiteral("Channel is not in the manifest."));
		return false;
	}
	const auto revoked = [&](const QByteArray &id) {
		for (const auto &entry : manifest.revoked) {
			if (entry == id) {
				return true;
			}
		}
		return false;
	};
	const auto findKey = [&](const QByteArray &id) -> const ManifestKey* {
		for (const auto &key : manifest.keys) {
			if (key.id == id) {
				return &key;
			}
		}
		return nullptr;
	};
	for (const auto &group : i->second) {
		auto satisfied = false;
		for (const auto &id : group) {
			if (revoked(id)) {
				continue;
			}
			const auto key = findKey(id);
			if (!key || (key->expires > 0 && key->expires <= now)) {
				continue;
			}
			for (const auto &signature : signatures) {
				if (signature.keyId == id
					&& VerifySignature(*key, signingInput, signature.bytes)) {
					satisfied = true;
					break;
				}
			}
			if (satisfied) {
				break;
			}
		}
		if (!satisfied) {
			SetError(
				error,
				QStringLiteral("No valid signature for a required group."));
			return false;
		}
	}
	return true;
}

bool ChannelPolicyAllows(
		Channel build,
		bool betaSet,
		Channel package,
		quint64 packageVersion,
		quint64 runningVersion) {
	switch (build) {
	case Channel::Stable:
		return (package == Channel::Stable)
			|| (betaSet && package == Channel::Beta);
	case Channel::Beta:
		return (package == Channel::Stable) || (package == Channel::Beta);
	case Channel::CanaryPublic:
		return (package == Channel::CanaryPublic)
			|| ((package == Channel::Stable || package == Channel::Beta)
				&& (UpdateVersionBase(packageVersion)
					> UpdateVersionBase(runningVersion)));
	case Channel::CanaryPrivate:
		return (package == Channel::CanaryPrivate);
	}
	return false;
}

std::optional<VerifiedUpdate> VerifyUpdate(
		const QByteArray &data,
		Channel buildChannel,
		bool betaSet,
		Target expectedTarget,
		quint64 runningVersion,
		const std::optional<Manifest> &held,
		const QByteArray &rootPublicKeyPem,
		qint64 now,
		QString *error) {
	auto envelope = ParseEnvelope(data, error);
	if (!envelope) {
		return std::nullopt;
	} else if (!(envelope->target == expectedTarget)) {
		SetError(
			error,
			QStringLiteral("Package target is not this platform."));
		return std::nullopt;
	}

	auto carried = ParseVerifiedManifest(
		envelope->manifest,
		envelope->manifestSignature,
		rootPublicKeyPem,
		error);
	if (!carried) {
		return std::nullopt;
	}

	auto result = VerifiedUpdate();
	const auto heldVersion = held ? held->version : quint32(0);
	if (carried->version > heldVersion) {
		result.adoptManifest = true;
		result.manifest = std::move(*carried);
	} else {
		result.manifest = *held;
	}

	if (!ChannelPolicyAllows(
			buildChannel,
			betaSet,
			envelope->channel,
			envelope->version,
			runningVersion)) {
		SetError(
			error,
			QStringLiteral("Package channel is not allowed for this build."));
		return std::nullopt;
	}

	const auto signingInput = SigningInput(*envelope);
	if (!VerifyChannelAuthorization(
			result.manifest,
			envelope->channel,
			signingInput,
			envelope->signatures,
			now,
			error)) {
		return std::nullopt;
	}

	if (envelope->version <= runningVersion) {
		SetError(error, QStringLiteral("Package version is not newer."));
		return std::nullopt;
	}

	result.envelope = std::move(*envelope);
	return result;
}

} // namespace Core::Updates
