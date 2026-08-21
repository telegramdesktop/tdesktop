/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "packer.h"

#include "core/update_verify.h"

#include <QtCore/QDateTime>

#include <optional>
#include <utility>
#include <vector>

bool BetaChannel = false;
quint64 AlphaVersion = 0;
bool OnlyAlphaKey = false;

std::optional<Core::Updates::Channel> V2Channel;
quint32 V2Counter = 0;
QString V2KeysLoc;
QString V2LocalKeyFile;
QString V2LocalKeyId;
QString V2SigningInputFile;
QString V2UnsignedFile;
std::vector<std::pair<QString, QString>> V2EmbedSignatures;
Core::Updates::Target V2Target;

const char *PublicKey = "\
-----BEGIN RSA PUBLIC KEY-----\n\
MIGJAoGBAMA4ViQrjkPZ9xj0lrer3r23JvxOnrtE8nI69XLGSr+sRERz9YnUptnU\n\
BZpkIfKaRcl6XzNJiN28cVwO1Ui5JSa814UAiDHzWUqCaXUiUEQ6NmNTneiGx2sQ\n\
+9PKKlb8mmr3BB9A45ZNwLT6G9AK3+qkZLHojeSA+m84/a6GP4svAgMBAAE=\n\
-----END RSA PUBLIC KEY-----\
";

const char *PublicBetaKey = "\
-----BEGIN RSA PUBLIC KEY-----\n\
MIGJAoGBALWu9GGs0HED7KG7BM73CFZ6o0xufKBRQsdnq3lwA8nFQEvmdu+g/I1j\n\
0LQ+0IQO7GW4jAgzF/4+soPDb6uHQeNFrlVx1JS9DZGhhjZ5rf65yg11nTCIHZCG\n\
w/CVnbwQOw0g5GBwwFV3r0uTTvy44xx8XXxk+Qknu4eBCsmrAFNnAgMBAAE=\n\
-----END RSA PUBLIC KEY-----\
";

#ifndef PACKER_DISABLE_PRIVATE
extern const char *PrivateKey;
extern const char *PrivateBetaKey;
#include "../../../../DesktopPrivate/packer_private.h" // RSA PRIVATE KEYS for update signing
#include "../../../../DesktopPrivate/alpha_private.h" // private key for alpha version file generation
#else // PACKER_DISABLE_PRIVATE
// V2 packing needs no DesktopPrivate keys: the empty stubs make the v1
// path fail with a clear error instead of signing with a wrong key.
const char *PrivateKey = "";
const char *PrivateBetaKey = "";
static const char *AlphaPrivateKey = "";
#endif // PACKER_DISABLE_PRIVATE

QString countAlphaVersionSignature(quint64 version);

// sha1 hash
typedef unsigned char uchar;
typedef unsigned int uint32;
typedef signed int int32;

namespace{

struct BIODeleter {
	void operator()(BIO *value) {
		BIO_free(value);
	}
};

inline auto makeBIO(const void *buf, int len) {
	return std::unique_ptr<BIO, BIODeleter>{
		BIO_new_mem_buf(buf, len),
	};
}

inline uint32 sha1Shift(uint32 v, uint32 shift) {
	return ((v << shift) | (v >> (32 - shift)));
}

void sha1PartHash(uint32 *sha, uint32 *temp) {
	uint32 a = sha[0], b = sha[1], c = sha[2], d = sha[3], e = sha[4], round = 0;

#define _shiftswap(f, v) { \
		uint32 t = sha1Shift(a, 5) + (f) + e + v + temp[round]; \
		e = d; \
		d = c; \
		c = sha1Shift(b, 30); \
		b = a; \
		a = t; \
		++round; \
	}

#define _shiftshiftswap(f, v) { \
		temp[round] = sha1Shift((temp[round - 3] ^ temp[round - 8] ^ temp[round - 14] ^ temp[round - 16]), 1); \
		_shiftswap(f, v) \
	}

	while (round < 16) _shiftswap((b & c) | (~b & d), 0x5a827999)
	while (round < 20) _shiftshiftswap((b & c) | (~b & d), 0x5a827999)
	while (round < 40) _shiftshiftswap(b ^ c ^ d, 0x6ed9eba1)
	while (round < 60) _shiftshiftswap((b & c) | (b & d) | (c & d), 0x8f1bbcdc)
	while (round < 80) _shiftshiftswap(b ^ c ^ d, 0xca62c1d6)

#undef _shiftshiftswap
#undef _shiftswap

	sha[0] += a;
	sha[1] += b;
	sha[2] += c;
	sha[3] += d;
	sha[4] += e;
}

} // namespace

int32 *hashSha1(const void *data, uint32 len, void *dest) {
	const uchar *buf = (const uchar *)data;

	uint32 temp[80], block = 0, end;
	uint32 sha[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
	for (end = block + 64; block + 64 <= len; end = block + 64) {
		for (uint32 i = 0; block < end; block += 4) {
			temp[i++] = (uint32) buf[block + 3]
					| (((uint32) buf[block + 2]) << 8)
					| (((uint32) buf[block + 1]) << 16)
					| (((uint32) buf[block]) << 24);
		}
		sha1PartHash(sha, temp);
	}

	end = len - block;
	memset(temp, 0, sizeof(uint32) * 16);
	uint32 last = 0;
	for (; last < end; ++last) {
		temp[last >> 2] |= (uint32)buf[last + block] << ((3 - (last & 0x03)) << 3);
	}
	temp[last >> 2] |= 0x80 << ((3 - (last & 3)) << 3);
	if (end >= 56) {
		sha1PartHash(sha, temp);
		memset(temp, 0, sizeof(uint32) * 16);
	}
	temp[15] = len << 3;
	sha1PartHash(sha, temp);

	uchar *sha1To = (uchar*)dest;

	for (int32 i = 19; i >= 0; --i) {
		sha1To[i] = (sha[i >> 2] >> (((3 - i) & 0x03) << 3)) & 0xFF;
	}

	return (int32*)sha1To;
}

namespace {

using Core::Updates::Channel;

void AppendLeU32(QByteArray &to, quint32 value) {
	const char bytes[4] = {
		char(value & 0xFF),
		char((value >> 8) & 0xFF),
		char((value >> 16) & 0xFF),
		char((value >> 24) & 0xFF),
	};
	to.append(bytes, 4);
}

void AppendLeU64(QByteArray &to, quint64 value) {
	AppendLeU32(to, quint32(value & 0xFFFFFFFFULL));
	AppendLeU32(to, quint32(value >> 32));
}

[[nodiscard]] QByteArray ReadFile(const QString &path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		cout << "Can't read '" << path.toUtf8().constData() << "'..\n";
		return QByteArray();
	}
	return file.readAll();
}

// td-update-{os}-{arch}-{base}[-beta|-canary-{counter}[-private]], the same
// suffix the installers and portable archives carry after their version.
[[nodiscard]] QString V2NameSuffix(Channel channel, quint32 counter) {
	switch (channel) {
	case Channel::Stable: return QString();
	case Channel::Beta: return QString("-beta");
	case Channel::CanaryPublic: return QString("-canary-%1").arg(counter);
	case Channel::CanaryPrivate:
		return QString("-canary-%1-private").arg(counter);
	}
	return QString();
}

[[nodiscard]] QString V2FileName(
		Channel channel,
		quint32 base,
		quint32 counter) {
	return QString("td-update-%1-%2-%3%4"
	).arg(QString::fromLatin1(Core::Updates::OsName(V2Target.os))
	).arg(QString::fromLatin1(Core::Updates::ArchName(V2Target.arch))
	).arg(base
	).arg(V2NameSuffix(channel, counter));
}

struct V2Keys {
	QByteArray rootPublicKeyPem;
	Core::Updates::Manifest manifest;
};

// The manifest root signature is verified before anything gets embedded,
// this is the build-time tripwire against packing with broken key files.
[[nodiscard]] std::optional<V2Keys> LoadV2Keys() {
	const auto rootPem = ReadFile(V2KeysLoc + "/root-public.pem");
	const auto manifest = ReadFile(V2KeysLoc + "/manifest.min.json");
	const auto signature = ReadFile(V2KeysLoc + "/manifest.sig");
	if (rootPem.isEmpty() || manifest.isEmpty() || signature.isEmpty()) {
		return std::nullopt;
	}
	auto error = QString();
	auto parsed = Core::Updates::ParseVerifiedManifest(
		manifest,
		signature,
		rootPem,
		&error);
	if (!parsed) {
		cout << "Manifest verification failed: "
			<< error.toUtf8().constData() << "\n";
		return std::nullopt;
	}
	return V2Keys{ rootPem, std::move(*parsed) };
}

// The build-time expiry watchdog: the client never rejects an expired
// manifest (revocation is the kill switch), so this is the only place
// the approaching dates are surfaced, ahead of the moment a channel key
// expires and packing starts failing verification.
void ReportExpiry(const Core::Updates::Manifest &manifest, Channel channel) {
	constexpr auto kWarnDays = 90;
	const auto now = QDateTime::currentSecsSinceEpoch();
	const auto report = [&](const QByteArray &what, qint64 expires) {
		if (!expires) {
			return;
		}
		const auto days = (expires - now) / (24 * 60 * 60);
		if (days < 0) {
			cout << "WARNING: " << what.constData() << " expired "
				<< -days << " days ago!\n";
		} else if (days < kWarnDays) {
			cout << "WARNING: " << what.constData() << " expires in "
				<< days << " days!\n";
		} else {
			cout << what.constData() << " expires in " << days << " days.\n";
		}
	};
	report("The manifest", manifest.expires);
	const auto i = manifest.channels.find(Core::Updates::ChannelName(channel));
	if (i == manifest.channels.end()) {
		return;
	}
	for (const auto &group : i->second) {
		for (const auto &id : group) {
			for (const auto &key : manifest.keys) {
				if (key.id == id) {
					report("Key '" + id + "'", key.expires);
				}
			}
		}
	}
}

[[nodiscard]] QByteArray BuildV2Envelope(
		Channel channel,
		Core::Updates::Target target,
		quint64 version,
		qint64 created,
		const QByteArray &manifest,
		const QByteArray &manifestSignature,
		const std::vector<std::pair<QByteArray, QByteArray>> &signatures,
		const QByteArray &payload) {
	auto result = QByteArray();
	result.append(Core::Updates::kEnvelopeMagic, 4);
	AppendLeU32(result, Core::Updates::kEnvelopeFormat);
	result.append(char(uchar(channel)));
	result.append(char(uchar(target.os)));
	result.append(char(uchar(target.arch)));
	AppendLeU64(result, version);
	AppendLeU64(result, quint64(created));
	AppendLeU32(result, quint32(manifest.size()));
	result.append(manifest);
	AppendLeU32(result, quint32(manifestSignature.size()));
	result.append(manifestSignature);
	AppendLeU32(result, quint32(signatures.size()));
	for (const auto &[keyId, signature] : signatures) {
		AppendLeU32(result, quint32(keyId.size()));
		result.append(keyId);

		// ES256 signatures are stored as raw r||s (64 bytes) exactly as
		// Azure Key Vault produces them, the client converts r||s to DER
		// at verify time. The envelope never contains DER.
		AppendLeU32(result, quint32(signature.size()));
		result.append(signature);
	}
	AppendLeU32(result, quint32(payload.size()));
	result.append(payload);
	return result;
}

[[nodiscard]] QByteArray SignEd25519(
		const QByteArray &privateKeyPem,
		const QByteArray &message) {
	const auto bio = makeBIO(privateKeyPem.constData(), privateKeyPem.size());
	const auto key = PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr);
	if (!key) {
		cout << "Could not read the Ed25519 private key!\n";
		return QByteArray();
	}
	if (EVP_PKEY_id(key) != EVP_PKEY_ED25519) {
		cout << "The private key is not an Ed25519 key!\n";
		EVP_PKEY_free(key);
		return QByteArray();
	}
	auto result = QByteArray(64, Qt::Uninitialized);
	auto length = size_t(result.size());
	const auto context = EVP_MD_CTX_new();
	const auto success = context
		&& (EVP_DigestSignInit(context, nullptr, nullptr, nullptr, key) == 1)
		&& (EVP_DigestSign(
			context,
			reinterpret_cast<uchar*>(result.data()),
			&length,
			reinterpret_cast<const uchar*>(message.constData()),
			size_t(message.size())) == 1)
		&& (length == 64);
	EVP_MD_CTX_free(context);
	EVP_PKEY_free(key);
	if (!success) {
		cout << "Ed25519 signing failed!\n";
		return QByteArray();
	}
	return result;
}

// Runs the exact client verification over the finished file, so a package
// that would be rejected by clients can never leave the build machine.
[[nodiscard]] bool VerifyPackedV2(
		const QByteArray &fileBytes,
		const V2Keys &keys,
		Channel channel,
		Core::Updates::Target target) {
	auto error = QString();
	const auto verified = Core::Updates::VerifyUpdate(
		fileBytes,
		channel,
		true, // betaSet
		target,
		0, // runningVersion
		keys.manifest,
		keys.rootPublicKeyPem,
		QDateTime::currentSecsSinceEpoch(),
		&error);
	if (!verified) {
		cout << "Packed update verification failed: "
			<< error.toUtf8().constData() << "\n";
		return false;
	}
	return true;
}

[[nodiscard]] bool WriteWholeFile(
		const QString &path,
		const QByteArray &content) {
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)
		|| file.write(content) != content.size()) {
		cout << "Can't write '" << path.toUtf8().constData() << "'..\n";
		return false;
	}
	return true;
}

int WriteV2Update(const QByteArray &payload, quint32 baseVersion) {
	const auto keys = LoadV2Keys();
	if (!keys) {
		return -1;
	}
	const auto channel = *V2Channel;
	ReportExpiry(keys->manifest, channel);
	const auto version = Core::Updates::MakeUpdateVersion(
		baseVersion,
		V2Counter);
	const auto name = V2FileName(channel, baseVersion, V2Counter);

	auto signatures = std::vector<std::pair<QByteArray, QByteArray>>();
	const auto unsigned_ = BuildV2Envelope(
		channel,
		V2Target,
		version,
		QDateTime::currentSecsSinceEpoch(),
		keys->manifest.bytes,
		keys->manifest.signature,
		signatures,
		payload);

	auto error = QString();
	const auto envelope = Core::Updates::ParseEnvelope(unsigned_, &error);
	if (!envelope) {
		cout << "Built envelope did not parse back: "
			<< error.toUtf8().constData() << "\n";
		return -1;
	}
	const auto signingInput = Core::Updates::SigningInput(*envelope);
	if (signingInput.isEmpty()) {
		cout << "Could not build the signing input!\n";
		return -1;
	}

	if (!V2SigningInputFile.isEmpty()) {
		if (!WriteWholeFile(V2SigningInputFile, signingInput)
			|| !WriteWholeFile(name + ".unsigned", unsigned_)) {
			return -1;
		}
		cout << "Unsigned update '" << name.toUtf8().constData()
			<< ".unsigned' and signing input written, waiting for external signatures..\n";
		return 0;
	}

	const auto keyPem = ReadFile(V2LocalKeyFile);
	if (keyPem.isEmpty()) {
		return -1;
	}
	const auto signature = SignEd25519(keyPem, signingInput);
	if (signature.isEmpty()) {
		return -1;
	}
	signatures.push_back({ V2LocalKeyId.toUtf8(), signature });

	const auto result = BuildV2Envelope(
		channel,
		V2Target,
		version,
		envelope->created,
		keys->manifest.bytes,
		keys->manifest.signature,
		signatures,
		payload);
	if (!VerifyPackedV2(result, *keys, channel, V2Target)
		|| !WriteWholeFile(name, result)) {
		return -1;
	}
	cout << "Update file '" << name.toUtf8().constData()
		<< "' written successfully!\n";
	return 0;
}

int EmbedV2Signatures() {
	if (V2KeysLoc.isEmpty()) {
		cout << "The -keys-loc param is required to embed signatures!\n";
		return -1;
	} else if (!V2UnsignedFile.endsWith(".unsigned")) {
		cout << "The -unsigned param must point to an .unsigned file!\n";
		return -1;
	}
	const auto keys = LoadV2Keys();
	if (!keys) {
		return -1;
	}
	const auto bytes = ReadFile(V2UnsignedFile);
	if (bytes.isEmpty()) {
		return -1;
	}
	auto error = QString();
	const auto envelope = Core::Updates::ParseEnvelope(bytes, &error);
	if (!envelope) {
		cout << "Could not parse the unsigned update: "
			<< error.toUtf8().constData() << "\n";
		return -1;
	} else if (!envelope->signatures.empty()) {
		cout << "The unsigned update already has signatures!\n";
		return -1;
	} else if (V2Channel && *V2Channel != envelope->channel) {
		cout << "The -channel param does not match the unsigned update!\n";
		return -1;
	}
	ReportExpiry(keys->manifest, envelope->channel);

	auto signatures = std::vector<std::pair<QByteArray, QByteArray>>();
	for (const auto &[keyId, file] : V2EmbedSignatures) {
		const auto signature = ReadFile(file);
		if (signature.isEmpty()) {
			return -1;
		}
		signatures.push_back({ keyId.toUtf8(), signature });
	}

	// Multi-group channels (stable/beta are 2-of-2) combine an external
	// cloud signature with the local Ed25519 key in one embed pass.
	if (!V2LocalKeyFile.isEmpty()) {
		if (V2LocalKeyId.isEmpty()) {
			cout << "The -local-key param requires -local-key-id!\n";
			return -1;
		}
		const auto keyPem = ReadFile(V2LocalKeyFile);
		if (keyPem.isEmpty()) {
			return -1;
		}
		const auto signingInput = Core::Updates::SigningInput(*envelope);
		if (signingInput.isEmpty()) {
			cout << "Could not build the signing input!\n";
			return -1;
		}
		const auto signature = SignEd25519(keyPem, signingInput);
		if (signature.isEmpty()) {
			return -1;
		}
		signatures.push_back({ V2LocalKeyId.toUtf8(), signature });
	}
	if (signatures.empty()) {
		cout << "No signatures to embed, pass -embed-signatures or -local-key!\n";
		return -1;
	}

	const auto result = BuildV2Envelope(
		envelope->channel,
		envelope->target,
		envelope->version,
		envelope->created,
		envelope->manifest,
		envelope->manifestSignature,
		signatures,
		envelope->payload);
	const auto name = V2UnsignedFile.left(
		V2UnsignedFile.size() - int(strlen(".unsigned")));
	if (!VerifyPackedV2(result, *keys, envelope->channel, envelope->target)
		|| !WriteWholeFile(name, result)) {
		return -1;
	}
	cout << "Update file '" << name.toUtf8().constData()
		<< "' written successfully!\n";
	return 0;
}

} // namespace

QString AlphaSignature;

int writeAlphaKey() {
	if (!AlphaVersion) {
		return 0;
	}
	QString keyName(QString("talpha_%1_key").arg(AlphaVersion));
	QFile key(keyName);
	if (!key.open(QIODevice::WriteOnly)) {
		cout << "Can't open '" << keyName.toUtf8().constData() << "' for write..\n";
		return -1;
	}
	key.write(AlphaSignature.toUtf8());
	key.close();
	return 0;
}

int main(int argc, char *argv[])
{
	QString workDir;

	QString remove;
	int version = 0;
	[[maybe_unused]] bool targetwin64 = false;
	[[maybe_unused]] bool targetwinarm = false;
	[[maybe_unused]] bool targetarmac = false;
	QFileInfoList files;
	for (int i = 0; i < argc; ++i) {
		if (string("-path") == argv[i] && i + 1 < argc) {
			QString path = workDir + QString(argv[i + 1]);
			QFileInfo info(path);
			files.push_back(info);
			if (remove.isEmpty()) remove = info.canonicalPath() + "/";
		} else if (string("-target") == argv[i] && i + 1 < argc) {
			targetwin64 = (string("win64") == argv[i + 1]);
			targetwinarm = (string("winarm") == argv[i + 1]);
		} else if (string("-arch") == argv[i] && i + 1 < argc) {
			targetarmac = (string("arm64") == argv[i + 1]);
			if (!targetarmac && string("x86_64") != argv[i + 1]) {
				cout << "Bad -arch param value passed: " << argv[i + 1] << "\n";
				return -1;
			}
		} else if (string("-version") == argv[i] && i + 1 < argc) {
			version = QString(argv[i + 1]).toInt();
		} else if (string("-beta") == argv[i]) {
			BetaChannel = true;
		} else if (string("-channel") == argv[i] && i + 1 < argc) {
			V2Channel = Core::Updates::ChannelFromName(argv[i + 1]);
			if (!V2Channel) {
				cout << "Bad -channel param value passed: " << argv[i + 1] << "\n";
				return -1;
			}
		} else if (string("-counter") == argv[i] && i + 1 < argc) {
			V2Counter = QString(argv[i + 1]).toUInt();
		} else if (string("-keys-loc") == argv[i] && i + 1 < argc) {
			V2KeysLoc = QString(argv[i + 1]);
		} else if (string("-local-key") == argv[i] && i + 1 < argc) {
			V2LocalKeyFile = QString(argv[i + 1]);
		} else if (string("-local-key-id") == argv[i] && i + 1 < argc) {
			V2LocalKeyId = QString(argv[i + 1]);
		} else if (string("-emit-signing-input") == argv[i] && i + 1 < argc) {
			V2SigningInputFile = QString(argv[i + 1]);
		} else if (string("-unsigned") == argv[i] && i + 1 < argc) {
			V2UnsignedFile = QString(argv[i + 1]);
		} else if (string("-embed-signatures") == argv[i]) {
			while (i + 1 < argc && argv[i + 1][0] != '-') {
				const auto entry = QString(argv[++i]);
				const auto colon = entry.indexOf(':');
				if (colon <= 0 || colon == entry.size() - 1) {
					cout << "Bad -embed-signatures entry, expected id:sigfile: " << entry.toUtf8().constData() << "\n";
					return -1;
				}
				V2EmbedSignatures.push_back({
					entry.left(colon),
					entry.mid(colon + 1),
				});
			}
			if (V2EmbedSignatures.empty()) {
				cout << "No -embed-signatures entries passed!\n";
				return -1;
			}
		} else if (string("-alphakey") == argv[i]) {
			OnlyAlphaKey = true;
		} else if (string("-alpha") == argv[i] && i + 1 < argc) {
			AlphaVersion = QString(argv[i + 1]).toULongLong();
			if (AlphaVersion > version * 1000ULL && AlphaVersion < (version + 1) * 1000ULL) {
				BetaChannel = false;
				AlphaSignature = countAlphaVersionSignature(AlphaVersion);
				if (AlphaSignature.isEmpty()) {
					return -1;
				}
			} else {
				cout << "Bad -alpha param value passed, should be for the same version: " << version << ", alpha: " << AlphaVersion << "\n";
				return -1;
			}
		}
	}
	if (OnlyAlphaKey) {
		return writeAlphaKey();
	}

	{
		using Core::Updates::Os;
		using Core::Updates::Arch;
#ifdef Q_OS_WIN
		V2Target.os = Os::Windows;
		V2Target.arch = targetwinarm
			? Arch::Arm
			: targetwin64
			? Arch::X64
			: Arch::X86;
#elif defined Q_OS_MAC
		V2Target.os = Os::Mac;
		V2Target.arch = targetarmac ? Arch::Arm : Arch::X64;
#else
		V2Target.os = Os::Linux;
		V2Target.arch = Arch::X64;
#endif
	}

	if (!V2UnsignedFile.isEmpty()) {
		return EmbedV2Signatures();
	} else if (V2Channel) {
		const auto canary = (*V2Channel == Channel::CanaryPublic)
			|| (*V2Channel == Channel::CanaryPrivate);
		if (AlphaVersion || BetaChannel) {
			cout << "The -channel param cannot be combined with -alpha or -beta!\n";
			return -1;
		} else if (V2KeysLoc.isEmpty()) {
			cout << "The -keys-loc param is required for -channel packing!\n";
			return -1;
		} else if (canary != (V2Counter > 0)) {
			cout << "Canary channels require a positive -counter, others require none!\n";
			return -1;
		} else if (!V2EmbedSignatures.empty()) {
			cout << "The -embed-signatures param requires -unsigned!\n";
			return -1;
		} else if (V2SigningInputFile.isEmpty()
			&& (V2LocalKeyFile.isEmpty() || V2LocalKeyId.isEmpty())) {
			cout << "Either -emit-signing-input or -local-key with -local-key-id is required!\n";
			return -1;
		}
	} else if (V2Counter
		|| !V2KeysLoc.isEmpty()
		|| !V2LocalKeyFile.isEmpty()
		|| !V2SigningInputFile.isEmpty()
		|| !V2EmbedSignatures.empty()) {
		cout << "The v2 params require the -channel param!\n";
		return -1;
	}

	if (files.isEmpty() || remove.isEmpty() || version <= 1016 || version > 999999999) {
#ifdef Q_OS_WIN
		cout << "Usage: Packer.exe -path {file} -version {version} OR Packer.exe -path {dir} -version {version}\n";
#elif defined Q_OS_MAC
		cout << "Usage: Packer.app -path {file} -version {version} OR Packer.app -path {dir} -version {version}\n";
#else
		cout << "Usage: Packer -path {file} -version {version} OR Packer -path {dir} -version {version}\n";
#endif
		cout << "V2 envelopes: add -channel {stable|beta|canary-public|canary-private} -keys-loc {dir}\n";
		cout << "  with -local-key {pem} -local-key-id {id} for one-pass Ed25519 signing, or\n";
		cout << "  with -emit-signing-input {file} and later -unsigned {file} -embed-signatures {id}:{sigfile} ...\n";
		cout << "  (canary channels also require -counter {n})\n";
		return -1;
	}

	bool hasDirs = true;
	while (hasDirs) {
		hasDirs = false;
		for (QFileInfoList::iterator i = files.begin(); i != files.end(); ++i) {
			QFileInfo info(*i);
			QString fullPath = info.canonicalFilePath();
			if (info.isDir()) {
				hasDirs = true;
				files.erase(i);
				QDir d = QDir(info.absoluteFilePath());
				QString fullDir = d.canonicalPath();
				QStringList entries = d.entryList(QDir::Files | QDir::Dirs | QDir::NoSymLinks | QDir::NoDotAndDotDot);
				files.append(d.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoSymLinks | QDir::NoDotAndDotDot));
				break;
			} else if (!info.isReadable()) {
				cout << "Can't read: " << info.absoluteFilePath().toUtf8().constData() << "\n";
				return -1;
			} else if (info.isHidden()) {
				hasDirs = true;
				files.erase(i);
				break;
			}
		}
	}
	for (QFileInfoList::iterator i = files.begin(); i != files.end(); ++i) {
		QFileInfo info(*i);
		if (!info.canonicalFilePath().startsWith(remove)) {
			cout << "Can't find '" << remove.toUtf8().constData() << "' in file '" << info.canonicalFilePath().toUtf8().constData() << "' :(\n";
			return -1;
		}
	}

	QByteArray result;
	{
		QBuffer buffer(&result);
		buffer.open(QIODevice::WriteOnly);
		QDataStream stream(&buffer);
		stream.setVersion(QDataStream::Qt_5_1);

		if (AlphaVersion) {
			stream << quint32(0x7FFFFFFF);
			stream << quint64(AlphaVersion);
		} else {
			stream << quint32(version);
		}

		stream << quint32(files.size());
		cout << "Found " << files.size() << " file" << (files.size() == 1 ? "" : "s") << "..\n";
		for (QFileInfoList::iterator i = files.begin(); i != files.end(); ++i) {
			QFileInfo info(*i);
			QString fullName = info.canonicalFilePath();
			QString name = fullName.mid(remove.length());
			cout << name.toUtf8().constData() << " (" << info.size() << ")\n";

			QFile f(fullName);
			if (!f.open(QIODevice::ReadOnly)) {
				cout << "Can't open '" << fullName.toUtf8().constData() << "' for read..\n";
				return -1;
			}
			QByteArray inner = f.readAll();
			stream << name << quint32(inner.size()) << inner;
#ifndef Q_OS_WIN
			stream << (QFileInfo(fullName).isExecutable() ? true : false);
#endif
		}
		if (stream.status() != QDataStream::Ok) {
			cout << "Stream status is bad: " << stream.status() << "\n";
			return -1;
		}
	}

	int32 resultSize = result.size();
	cout << "Compression start, size: " << resultSize << "\n";

	QByteArray compressed, resultCheck;
#if defined Q_OS_WIN && !defined PACKER_USE_PACKAGED // use Lzma SDK for win
	const int32 hSigLen = 128, hShaLen = 20, hPropsLen = LZMA_PROPS_SIZE, hOriginalSizeLen = sizeof(int32), hSize = hSigLen + hShaLen + hPropsLen + hOriginalSizeLen; // header

	compressed.resize(hSize + resultSize + 1024 * 1024); // rsa signature + sha1 + lzma props + max compressed size

	size_t compressedLen = compressed.size() - hSize;
	size_t outPropsSize = LZMA_PROPS_SIZE;
	uchar *_dest = (uchar*)(compressed.data() + hSize);
	size_t *_destLen = &compressedLen;
	const uchar *_src = (const uchar*)(result.constData());
	size_t _srcLen = result.size();
	uchar *_outProps = (uchar*)(compressed.data() + hSigLen + hShaLen);
	int res = LzmaCompress(_dest, _destLen, _src, _srcLen, _outProps, &outPropsSize, 9, 64 * 1024 * 1024, 4, 0, 2, 273, 2);
	if (res != SZ_OK) {
		cout << "Error in compression: " << res << "\n";
		return -1;
	}
	compressed.resize(int(hSize + compressedLen));
	memcpy(compressed.data() + hSigLen + hShaLen + hPropsLen, &resultSize, hOriginalSizeLen);

	cout << "Compressed to size: " << compressedLen << "\n";

	cout << "Checking uncompressed..\n";

	int32 resultCheckLen;
	memcpy(&resultCheckLen, compressed.constData() + hSigLen + hShaLen + hPropsLen, hOriginalSizeLen);
	if (resultCheckLen <= 0 || resultCheckLen > 1024 * 1024 * 1024) {
		cout << "Bad result len: " << resultCheckLen << "\n";
		return -1;
	}
	resultCheck.resize(resultCheckLen);

	size_t resultLen = resultCheck.size();
	SizeT srcLen = compressedLen;
	int uncompressRes = LzmaUncompress((uchar*)resultCheck.data(), &resultLen, (const uchar*)(compressed.constData() + hSize), &srcLen, (const uchar*)(compressed.constData() + hSigLen + hShaLen), LZMA_PROPS_SIZE);
	if (uncompressRes != SZ_OK) {
		cout << "Uncompress failed: " << uncompressRes << "\n";
		return -1;
	}
	if (resultLen != size_t(result.size())) {
		cout << "Uncompress bad size: " << resultLen << ", was: " << result.size() << "\n";
		return -1;
	}
#else // use liblzma for others
	const int32 hSigLen = 128, hShaLen = 20, hPropsLen = 0, hOriginalSizeLen = sizeof(int32), hSize = hSigLen + hShaLen + hOriginalSizeLen; // header

	compressed.resize(hSize + resultSize + 1024 * 1024); // rsa signature + sha1 + lzma props + max compressed size

	size_t compressedLen = compressed.size() - hSize;

	lzma_stream stream = LZMA_STREAM_INIT;

	int preset = 9 | LZMA_PRESET_EXTREME;
	lzma_ret ret = lzma_easy_encoder(&stream, preset, LZMA_CHECK_CRC64);
	if (ret != LZMA_OK) {
		const char *msg;
		switch (ret) {
			case LZMA_MEM_ERROR: msg = "Memory allocation failed"; break;
			case LZMA_OPTIONS_ERROR: msg = "Specified preset is not supported"; break;
			case LZMA_UNSUPPORTED_CHECK: msg = "Specified integrity check is not supported"; break;
			default: msg = "Unknown error, possibly a bug"; break;
		}
		cout << "Error initializing the encoder: " << msg << " (error code " << ret << ")\n";
		return -1;
	}

	stream.avail_in = resultSize;
	stream.next_in = (uint8_t*)result.constData();
	stream.avail_out = compressedLen;
	stream.next_out = (uint8_t*)(compressed.data() + hSize);

	lzma_ret res = lzma_code(&stream, LZMA_FINISH);
	compressedLen -= stream.avail_out;
	lzma_end(&stream);
	if (res != LZMA_OK && res != LZMA_STREAM_END) {
		const char *msg;
		switch (res) {
			case LZMA_MEM_ERROR: msg = "Memory allocation failed"; break;
			case LZMA_DATA_ERROR: msg = "File size limits exceeded"; break;
			default: msg = "Unknown error, possibly a bug"; break;
		}
		cout << "Error in compression: " << msg << " (error code " << res << ")\n";
		return -1;
	}

	compressed.resize(int(hSize + compressedLen));
	memcpy(compressed.data() + hSigLen + hShaLen, &resultSize, hOriginalSizeLen);

	cout << "Compressed to size: " << compressedLen << "\n";

	cout << "Checking uncompressed..\n";

	int32 resultCheckLen;
	memcpy(&resultCheckLen, compressed.constData() + hSigLen + hShaLen, hOriginalSizeLen);
	if (resultCheckLen <= 0 || resultCheckLen > 1024 * 1024 * 1024) {
		cout << "Bad result len: " << resultCheckLen << "\n";
		return -1;
	}
	resultCheck.resize(resultCheckLen);

	size_t resultLen = resultCheck.size();

	stream = LZMA_STREAM_INIT;

	ret = lzma_stream_decoder(&stream, UINT64_MAX, LZMA_CONCATENATED);
	if (ret != LZMA_OK) {
		const char *msg;
		switch (ret) {
			case LZMA_MEM_ERROR: msg = "Memory allocation failed"; break;
			case LZMA_OPTIONS_ERROR: msg = "Specified preset is not supported"; break;
			case LZMA_UNSUPPORTED_CHECK: msg = "Specified integrity check is not supported"; break;
			default: msg = "Unknown error, possibly a bug"; break;
		}
		cout << "Error initializing the decoder: " << msg << " (error code " << ret << ")\n";
		return -1;
	}

	stream.avail_in = compressedLen;
	stream.next_in = (uint8_t*)(compressed.constData() + hSize);
	stream.avail_out = resultLen;
	stream.next_out = (uint8_t*)resultCheck.data();

	res = lzma_code(&stream, LZMA_FINISH);
	const auto availIn = stream.avail_in;
	const auto availOut = stream.avail_out;
	lzma_end(&stream);
	if (availIn) {
		cout << "Error in decompression, " << availIn << " bytes left in _in of " << compressedLen << " whole.\n";
		return -1;
	} else if (availOut) {
		cout << "Error in decompression, " << availOut << " bytes free left in _out of " << resultLen << " whole.\n";
		return -1;
	}
	if (res != LZMA_OK && res != LZMA_STREAM_END) {
		const char *msg;
		switch (res) {
			case LZMA_MEM_ERROR: msg = "Memory allocation failed"; break;
			case LZMA_FORMAT_ERROR: msg = "The input data is not in the .xz format"; break;
			case LZMA_OPTIONS_ERROR: msg = "Unsupported compression options"; break;
			case LZMA_DATA_ERROR: msg = "Compressed file is corrupt"; break;
			case LZMA_BUF_ERROR: msg = "Compressed data is truncated or otherwise corrupt"; break;
			default: msg = "Unknown error, possibly a bug"; break;
		}
		cout << "Error in decompression: " << msg << " (error code " << res << ")\n";
		return -1;
	}
#endif
	if (memcmp(result.constData(), resultCheck.constData(), resultLen)) {
		cout << "Data differ :(\n";
		return -1;
	}
	/**/
	result = resultCheck = QByteArray();

	if (V2Channel) {
		// The payload keeps the exact v1 layout after its signature and
		// hash prefix ([lzma props on Windows,] original size, compressed
		// bytes), so clients decompress it with the unchanged v1 code.
		return WriteV2Update(
			compressed.mid(hSigLen + hShaLen),
			quint32(version));
	}

	cout << "Counting SHA1 hash..\n";

	uchar sha1Buffer[20];
	memcpy(compressed.data() + hSigLen, hashSha1(compressed.constData() + hSigLen + hShaLen, uint32(compressedLen + hPropsLen + hOriginalSizeLen), sha1Buffer), hShaLen); // count sha1

	uint32 siglen = 0;

	cout << "Signing..\n";
	RSA *prKey = [] {
		const auto bio = makeBIO(
			const_cast<char*>(
				(BetaChannel || AlphaVersion)
					? PrivateBetaKey
					: PrivateKey),
			-1);
		return PEM_read_bio_RSAPrivateKey(bio.get(), 0, 0, 0);
	}();
	if (!prKey) {
		cout << "Could not read RSA private key!\n";
		return -1;
	}
	if (RSA_size(prKey) != hSigLen) {
		cout << "Bad private key, size: " << RSA_size(prKey) << "\n";
		RSA_free(prKey);
		return -1;
	}
	if (RSA_sign(NID_sha1, (const uchar*)(compressed.constData() + hSigLen), hShaLen, (uchar*)(compressed.data()), &siglen, prKey) != 1) { // count signature
		cout << "Signing failed!\n";
		RSA_free(prKey);
		return -1;
	}
	RSA_free(prKey);

	if (siglen != hSigLen) {
		cout << "Bad signature length: " << siglen << "\n";
		return -1;
	}

	cout << "Checking signature..\n";
	RSA *pbKey = [] {
		const auto bio = makeBIO(
			const_cast<char*>(
				(BetaChannel || AlphaVersion)
					? PublicBetaKey
					: PublicKey),
			-1);
		return PEM_read_bio_RSAPublicKey(bio.get(), 0, 0, 0);
	}();
	if (!pbKey) {
		cout << "Could not read RSA public key!\n";
		return -1;
	}
	if (RSA_verify(NID_sha1, (const uchar*)(compressed.constData() + hSigLen), hShaLen, (const uchar*)(compressed.constData()), siglen, pbKey) != 1) { // verify signature
		RSA_free(pbKey);
		cout << "Signature verification failed!\n";
		return -1;
	}
	cout << "Signature verified!\n";
	RSA_free(pbKey);
#ifdef Q_OS_WIN
	QString outName((targetwinarm ? QString("tarm64upd%1") : targetwin64 ? QString("tx64upd%1") : QString("tupdate%1")).arg(AlphaVersion ? AlphaVersion : version));
#elif defined Q_OS_MAC
	QString outName((targetarmac ? QString("tarmacupd%1") : QString("tmacupd%1")).arg(AlphaVersion ? AlphaVersion : version));
#else
	QString outName(QString("tlinuxupd%1").arg(AlphaVersion ? AlphaVersion : version));
#endif
	if (AlphaVersion) {
		outName += "_" + AlphaSignature;
	}
	QFile out(outName);
	if (!out.open(QIODevice::WriteOnly)) {
		cout << "Can't open '" << outName.toUtf8().constData() << "' for write..\n";
		return -1;
	}
	out.write(compressed);
	out.close();

	cout << "Update file '" << outName.toUtf8().constData() << "' written successfully!\n";

	return writeAlphaKey();
}

QString countAlphaVersionSignature(quint64 version) { // duplicated in autoupdater.cpp
	QByteArray cAlphaPrivateKey(AlphaPrivateKey);
	if (cAlphaPrivateKey.isEmpty()) {
		cout << "Error: Trying to count alpha version signature without alpha private key!\n";
		return QString();
	}

	QByteArray signedData = (QLatin1String("TelegramBeta_") + QString::number(version, 16).toLower()).toUtf8();

	static const int32 shaSize = 20, keySize = 128;

	uchar sha1Buffer[shaSize];
	hashSha1(signedData.constData(), signedData.size(), sha1Buffer); // count sha1

	uint32 siglen = 0;

	RSA *prKey = [&] {
		const auto bio = makeBIO(
			const_cast<char*>(cAlphaPrivateKey.constData()),
			-1);
		return PEM_read_bio_RSAPrivateKey(bio.get(), 0, 0, 0);
	}();
	if (!prKey) {
		cout << "Error: Could not read alpha private key!\n";
		return QString();
	}
	if (RSA_size(prKey) != keySize) {
		cout << "Error: Bad alpha private key size: " << RSA_size(prKey) << "\n";
		RSA_free(prKey);
		return QString();
	}
	QByteArray signature;
	signature.resize(keySize);
	if (RSA_sign(NID_sha1, (const uchar*)(sha1Buffer), shaSize, (uchar*)(signature.data()), &siglen, prKey) != 1) { // count signature
		cout << "Error: Counting alpha version signature failed!\n";
		RSA_free(prKey);
		return QString();
	}
	RSA_free(prKey);

	if (siglen != keySize) {
		cout << "Error: Bad alpha version signature length: " << siglen << "\n";
		return QString();
	}

	signature = signature.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
	signature = signature.replace('-', '8').replace('_', 'B');
	return QString::fromUtf8(signature.mid(19, 32));
}
