/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Passport {

base::byte_vector GenerateSecretBytes();

base::byte_vector EncryptSecretBytes(
	base::const_byte_span secret,
	base::const_byte_span passwordHashForSecret);
base::byte_vector DecryptSecretBytes(
	base::const_byte_span encryptedSecret,
	base::const_byte_span passwordHashForSecret);

base::byte_vector PasswordHashForSecret(base::const_byte_span passwordUtf8);

base::byte_vector SerializeData(const std::map<QString, QString> &data);
std::map<QString, QString> DeserializeData(base::const_byte_span bytes);

struct EncryptedData {
	base::byte_vector secret;
	base::byte_vector hash;
	base::byte_vector bytes;
};

EncryptedData EncryptData(base::const_byte_span bytes);

EncryptedData EncryptData(
	base::const_byte_span bytes,
	base::const_byte_span dataSecret);

base::byte_vector DecryptData(
	base::const_byte_span encrypted,
	base::const_byte_span dataHash,
	base::const_byte_span dataSecret);

base::byte_vector PrepareValueHash(
	base::const_byte_span dataHash,
	base::const_byte_span valueSecret);

base::byte_vector EncryptValueSecret(
	base::const_byte_span valueSecret,
	base::const_byte_span secret,
	base::const_byte_span valueHash);

base::byte_vector DecryptValueSecret(
	base::const_byte_span encrypted,
	base::const_byte_span secret,
	base::const_byte_span valueHash);

base::byte_vector PrepareFilesHash(
	gsl::span<base::const_byte_span> fileHashes,
	base::const_byte_span valueSecret);

} // namespace Passport
