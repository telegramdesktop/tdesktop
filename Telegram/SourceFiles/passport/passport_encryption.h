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

struct EncryptedData {
	base::byte_vector hash;
	base::byte_vector bytes;
};

EncryptedData EncryptData(
	base::const_byte_span dataSecret,
	const std::map<QString, QString> &data);

std::map<QString, QString> DecryptData(
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

} // namespace Passport
