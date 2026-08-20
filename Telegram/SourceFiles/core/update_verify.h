/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// This translation unit is compiled into the client, the Packer and the
// focused unit test, so it must depend only on QtCore and OpenSSL: no LOG,
// no lib_base, no other SourceFiles headers. Errors are reported through
// the optional QString *error out-parameters instead of logging.

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <map>
#include <optional>
#include <vector>

namespace Core::Updates {

inline constexpr auto kEnvelopeMagic = "TDUP";
inline constexpr auto kEnvelopeFormat = quint32(2);

enum class Channel : uchar {
	Stable = 0,
	Beta = 1,
	CanaryPublic = 2,
	CanaryPrivate = 3,
};

[[nodiscard]] QByteArray ChannelName(Channel channel);
[[nodiscard]] std::optional<Channel> ChannelFromName(const QByteArray &name);

[[nodiscard]] constexpr quint64 MakeUpdateVersion(
		quint32 base,
		quint32 counter) {
	return (quint64(base) << 32) | quint64(counter);
}

[[nodiscard]] constexpr quint32 UpdateVersionBase(quint64 version) {
	return quint32(version >> 32);
}

[[nodiscard]] constexpr quint32 UpdateVersionCounter(quint64 version) {
	return quint32(version & 0xFFFFFFFFULL);
}

struct ManifestKey {
	QByteArray id;
	bool ed25519 = false;
	QByteArray x; // Raw 32 bytes.
	QByteArray y; // Raw 32 bytes, ES256 only.
	qint64 expires = 0; // 0 means the key never expires.
};

struct Manifest {
	quint32 version = 0;
	qint64 issued = 0;
	qint64 expires = 0;
	std::vector<ManifestKey> keys;

	// Channel name -> groups of key ids, the AND-of-ORs authorization:
	// every group must be satisfied by at least one valid signature.
	std::map<QByteArray, std::vector<std::vector<QByteArray>>> channels;

	std::vector<QByteArray> revoked;
	QByteArray bytes; // The exact signed JSON, kept verbatim.
	QByteArray signature; // Detached root Ed25519 signature over bytes.
};

// Verifies the detached root signature and parses the JSON defensively,
// both the manifest and the signature are attacker-reachable bytes.
[[nodiscard]] std::optional<Manifest> ParseVerifiedManifest(
	const QByteArray &json,
	const QByteArray &signature,
	const QByteArray &rootPublicKeyPem,
	QString *error = nullptr);

struct EnvelopeSignature {
	QByteArray keyId;
	QByteArray bytes;
};

struct Envelope {
	Channel channel = Channel::Stable;
	quint64 version = 0;
	qint64 created = 0;
	QByteArray manifest;
	QByteArray manifestSignature;
	std::vector<EnvelopeSignature> signatures;
	QByteArray payload;

	// All envelope bytes from the magic through the end of the manifest
	// bytes: the signatures can't sign themselves and the payload is bound
	// through its hash, see SigningInput().
	QByteArray signedRegion;
};

[[nodiscard]] bool IsV2UpdateFile(const QByteArray &data);

[[nodiscard]] std::optional<Envelope> ParseEnvelope(
	const QByteArray &data,
	QString *error = nullptr);

// signedRegion || SHA256(payload). Ed25519 keys sign this directly,
// ES256 keys sign its SHA-256 (that is what ECDSA-SHA256 means).
[[nodiscard]] QByteArray SigningInput(const Envelope &envelope);

[[nodiscard]] bool VerifySignature(
	const ManifestKey &key,
	const QByteArray &signingInput,
	const QByteArray &signature,
	QString *error = nullptr);

// The AND-of-ORs rule: every group of manifest.channels[channel] must
// contain at least one key that is listed, not revoked, not expired and
// has a valid signature over signingInput in the signatures list.
[[nodiscard]] bool VerifyChannelAuthorization(
	const Manifest &manifest,
	Channel channel,
	const QByteArray &signingInput,
	const std::vector<EnvelopeSignature> &signatures,
	qint64 now,
	QString *error = nullptr);

// betaSet means a stable build with the install-beta option, compile-time
// beta builds behave as if it is always set. Canary-public builds accept
// stable/beta packages only with a strictly greater base version (dormancy
// rescue), canary-private builds accept canary-private packages only.
[[nodiscard]] bool ChannelPolicyAllows(
	Channel build,
	bool betaSet,
	Channel package,
	quint64 packageVersion,
	quint64 runningVersion);

struct VerifiedUpdate {
	Envelope envelope;

	// The newest of the held manifest and the one carried by the package,
	// the one all authorization checks were performed against.
	Manifest manifest;

	// True when the package carried a manifest newer than the held one,
	// the caller should persist envelope.manifest / manifestSignature.
	bool adoptManifest = false;
};

[[nodiscard]] std::optional<VerifiedUpdate> VerifyUpdate(
	const QByteArray &data,
	Channel buildChannel,
	bool betaSet,
	quint64 runningVersion,
	const std::optional<Manifest> &held,
	const QByteArray &rootPublicKeyPem,
	qint64 now,
	QString *error = nullptr);

} // namespace Core::Updates
