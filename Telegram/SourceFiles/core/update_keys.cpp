/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/update_keys.h"

#include "update_keys_data.h"

namespace Core::Updates {

QByteArray RootPublicKeyPem() {
	return QByteArray::fromRawData(
		reinterpret_cast<const char*>(details::kRootPublicKeyPem),
		sizeof(details::kRootPublicKeyPem));
}

QByteArray EmbeddedManifest() {
	return QByteArray::fromRawData(
		reinterpret_cast<const char*>(details::kEmbeddedManifest),
		sizeof(details::kEmbeddedManifest));
}

QByteArray EmbeddedManifestSignature() {
	return QByteArray::fromRawData(
		reinterpret_cast<const char*>(details::kEmbeddedManifestSig),
		sizeof(details::kEmbeddedManifestSig));
}

} // namespace Core::Updates
