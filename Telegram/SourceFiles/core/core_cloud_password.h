/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/bytes.h"

namespace Core {

struct CloudPasswordAlgoPBKDF2 {
	static constexpr auto kIterations = 100000;

	bytes::vector salt1;
	bytes::vector salt2;
};

inline bool operator==(
		const CloudPasswordAlgoPBKDF2 &a,
		const CloudPasswordAlgoPBKDF2 &b) {
	return (a.salt1 == b.salt1) && (a.salt2 == b.salt2);
}

using CloudPasswordAlgo = base::optional_variant<CloudPasswordAlgoPBKDF2>;

CloudPasswordAlgo ParseCloudPasswordAlgo(const MTPPasswordKdfAlgo &data);
CloudPasswordAlgo ValidateNewCloudPasswordAlgo(CloudPasswordAlgo &&parsed);
MTPPasswordKdfAlgo PrepareCloudPasswordAlgo(const CloudPasswordAlgo &data);

bytes::vector ComputeCloudPasswordHash(
	const CloudPasswordAlgo &algo,
	bytes::const_span password);

struct SecureSecretAlgoSHA512 {
	bytes::vector salt;
};

inline bool operator==(
		const SecureSecretAlgoSHA512 &a,
		const SecureSecretAlgoSHA512 &b) {
	return (a.salt == b.salt);
}

struct SecureSecretAlgoPBKDF2 {
	static constexpr auto kIterations = 100000;

	bytes::vector salt;
};

inline bool operator==(
		const SecureSecretAlgoPBKDF2 &a,
		const SecureSecretAlgoPBKDF2 &b) {
	return (a.salt == b.salt);
}

using SecureSecretAlgo = base::optional_variant<
	SecureSecretAlgoSHA512,
	SecureSecretAlgoPBKDF2>;

SecureSecretAlgo ParseSecureSecretAlgo(
	const MTPSecurePasswordKdfAlgo &data);
SecureSecretAlgo ValidateNewSecureSecretAlgo(SecureSecretAlgo &&parsed);
MTPSecurePasswordKdfAlgo PrepareSecureSecretAlgo(
	const SecureSecretAlgo &data);

bytes::vector ComputeSecureSecretHash(
	const SecureSecretAlgo &algo,
	bytes::const_span password);

} // namespace Core
