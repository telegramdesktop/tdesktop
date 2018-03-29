/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Passport {

bytes::vector GenerateSecretBytes();

bytes::vector EncryptSecureSecret(
	bytes::const_span salt,
	bytes::const_span secret,
	bytes::const_span password);
bytes::vector DecryptSecureSecret(
	bytes::const_span salt,
	bytes::const_span encryptedSecret,
	bytes::const_span password);

bytes::vector SerializeData(const std::map<QString, QString> &data);
std::map<QString, QString> DeserializeData(bytes::const_span bytes);

struct EncryptedData {
	bytes::vector secret;
	bytes::vector hash;
	bytes::vector bytes;
};

EncryptedData EncryptData(bytes::const_span bytes);

EncryptedData EncryptData(
	bytes::const_span bytes,
	bytes::const_span dataSecret);

bytes::vector DecryptData(
	bytes::const_span encrypted,
	bytes::const_span dataHash,
	bytes::const_span dataSecret);

bytes::vector PrepareValueHash(
	bytes::const_span dataHash,
	bytes::const_span valueSecret);

bytes::vector EncryptValueSecret(
	bytes::const_span valueSecret,
	bytes::const_span secret,
	bytes::const_span valueHash);

bytes::vector DecryptValueSecret(
	bytes::const_span encrypted,
	bytes::const_span secret,
	bytes::const_span valueHash);

bytes::vector PrepareFilesHash(
	gsl::span<bytes::const_span> fileHashes,
	bytes::const_span valueSecret);

uint64 CountSecureSecretHash(bytes::const_span secret);

} // namespace Passport
