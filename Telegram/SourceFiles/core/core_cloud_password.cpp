/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/core_cloud_password.h"

#include "base/openssl_help.h"

namespace Core {
namespace {

constexpr auto kAdditionalSalt = size_type(8);

bytes::vector ComputeHash(
		base::none_type,
		bytes::const_span password) {
	Unexpected("Bad cloud password algorithm.");
}

bytes::vector ComputeHash(
		const CloudPasswordAlgoPBKDF2 &algo,
		bytes::const_span password) {
	const auto hash1 = openssl::Sha256(algo.salt1, password, algo.salt1);
	const auto hash2 = openssl::Sha256(algo.salt2, hash1, algo.salt2);
	return openssl::Pbkdf2Sha512(
		hash2,
		bytes::make_span(algo.salt1),
		algo.kIterations);
}

bytes::vector ComputeHash(
		const SecureSecretAlgoSHA512 &algo,
		bytes::const_span password) {
	return openssl::Sha512(algo.salt, password, algo.salt);
}

bytes::vector ComputeHash(
		const SecureSecretAlgoPBKDF2 &algo,
		bytes::const_span password) {
	return openssl::Pbkdf2Sha512(password, algo.salt, algo.kIterations);
}

} // namespace

CloudPasswordAlgo ParseCloudPasswordAlgo(const MTPPasswordKdfAlgo &data) {
	return data.match([](
	const MTPDpasswordKdfAlgoSHA256SHA256PBKDF2HMACSHA512iter100000 &data) {
		return CloudPasswordAlgo(CloudPasswordAlgoPBKDF2{
			bytes::make_vector(data.vsalt1.v),
			bytes::make_vector(data.vsalt2.v) });
	}, [](const MTPDpasswordKdfAlgoUnknown &data) {
		return CloudPasswordAlgo();
	});
}

CloudPasswordAlgo ValidateNewCloudPasswordAlgo(CloudPasswordAlgo &&parsed) {
	if (!parsed.is<CloudPasswordAlgoPBKDF2>()) {
		return base::none;
	}
	auto &value = parsed.get_unchecked<CloudPasswordAlgoPBKDF2>();
	const auto already = value.salt1.size();
	value.salt1.resize(already + kAdditionalSalt);
	bytes::set_random(bytes::make_span(value.salt1).subspan(already));
	return std::move(parsed);
}

MTPPasswordKdfAlgo PrepareCloudPasswordAlgo(const CloudPasswordAlgo &data) {
	return data.match([](const CloudPasswordAlgoPBKDF2 &data) {
		return MTP_passwordKdfAlgoSHA256SHA256PBKDF2HMACSHA512iter100000(
			MTP_bytes(data.salt1),
			MTP_bytes(data.salt2));
	}, [](base::none_type) {
		return MTP_passwordKdfAlgoUnknown();
	});
}

bytes::vector ComputeCloudPasswordHash(
		const CloudPasswordAlgo &algo,
		bytes::const_span password) {
	return algo.match([&](const auto &data) {
		return ComputeHash(data, password);
	});
}

SecureSecretAlgo ParseSecureSecretAlgo(
		const MTPSecurePasswordKdfAlgo &data) {
	return data.match([](
	const MTPDsecurePasswordKdfAlgoPBKDF2HMACSHA512iter100000 &data) {
		return SecureSecretAlgo(SecureSecretAlgoPBKDF2{
			bytes::make_vector(data.vsalt.v) });
	}, [](const MTPDsecurePasswordKdfAlgoSHA512 &data) {
		return SecureSecretAlgo(SecureSecretAlgoSHA512{
			bytes::make_vector(data.vsalt.v) });
	}, [](const MTPDsecurePasswordKdfAlgoUnknown &data) {
		return SecureSecretAlgo();
	});
}

SecureSecretAlgo ValidateNewSecureSecretAlgo(SecureSecretAlgo &&parsed) {
	if (!parsed.is<SecureSecretAlgoPBKDF2>()) {
		return base::none;
	}
	auto &value = parsed.get_unchecked<SecureSecretAlgoPBKDF2>();
	const auto already = value.salt.size();
	value.salt.resize(already + kAdditionalSalt);
	bytes::set_random(bytes::make_span(value.salt).subspan(already));
	return std::move(parsed);
}

MTPSecurePasswordKdfAlgo PrepareSecureSecretAlgo(
		const SecureSecretAlgo &data) {
	return data.match([](const SecureSecretAlgoPBKDF2 &data) {
		return MTP_securePasswordKdfAlgoPBKDF2HMACSHA512iter100000(
			MTP_bytes(data.salt));
	}, [](const SecureSecretAlgoSHA512 &data) {
		return MTP_securePasswordKdfAlgoSHA512(MTP_bytes(data.salt));
	}, [](base::none_type) {
		return MTP_securePasswordKdfAlgoUnknown();
	});
}

bytes::vector ComputeSecureSecretHash(
		const SecureSecretAlgo &algo,
		bytes::const_span password) {
	return algo.match([&](const auto &data) {
		return ComputeHash(data, password);
	});
}

} // namespace Core