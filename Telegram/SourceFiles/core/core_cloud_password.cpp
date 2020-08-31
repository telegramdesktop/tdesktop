/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/core_cloud_password.h"

#include "base/openssl_help.h"
#include "mtproto/mtproto_dh_utils.h"

namespace Core {
namespace {

using namespace openssl;

constexpr auto kAdditionalSalt = size_type(32);

constexpr auto kSizeForHash = 256;
bytes::vector NumBytesForHash(bytes::const_span number) {
	const auto fill = kSizeForHash - number.size();
	if (!fill) {
		return bytes::make_vector(number);
	}
	auto result = bytes::vector(kSizeForHash);
	const auto storage = bytes::make_span(result);
	bytes::set_with_const(storage.subspan(0, fill), bytes::type(0));
	bytes::copy(storage.subspan(fill), number);
	return result;
}

bytes::vector BigNumForHash(const BigNum &number) {
	auto result = number.getBytes();
	if (result.size() == kSizeForHash) {
		return result;
	}
	return NumBytesForHash(result);
}

bool IsPositive(const BigNum &number) {
	return !number.isNegative() && (number.bitsSize() > 0);
}

bool IsGoodLarge(const BigNum &number, const BigNum &p) {
	return IsPositive(number) && IsPositive(BigNum::Sub(p, number));
}

bytes::vector Xor(bytes::const_span a, bytes::const_span b) {
	Expects(a.size() == b.size());

	auto result = bytes::vector(a.size());
	for (auto i = index_type(); i != a.size(); ++i) {
		result[i] = a[i] ^ b[i];
	}
	return result;
};

bytes::vector ComputeHash(
		const CloudPasswordAlgoModPow &algo,
		bytes::const_span password) {
	const auto hash1 = Sha256(algo.salt1, password, algo.salt1);
	const auto hash2 = Sha256(algo.salt2, hash1, algo.salt2);
	const auto hash3 = Pbkdf2Sha512(hash2, algo.salt1, algo.kIterations);
	return Sha256(algo.salt2, hash3, algo.salt2);
}

CloudPasswordDigest ComputeDigest(
		const CloudPasswordAlgoModPow &algo,
		bytes::const_span password) {
	if (!MTP::IsPrimeAndGood(algo.p, algo.g)) {
		LOG(("API Error: Bad p/g in cloud password creation!"));
		return {};
	}
	const auto value = BigNum::ModExp(
		BigNum(algo.g),
		BigNum(ComputeHash(algo, password)),
		BigNum(algo.p));
	if (value.failed()) {
		LOG(("API Error: Failed to count g_x in cloud password creation!"));
		return {};
	}
	return { BigNumForHash(value) };
}

CloudPasswordResult ComputeCheck(
		const CloudPasswordCheckRequest &request,
		const CloudPasswordAlgoModPow &algo,
		bytes::const_span hash) {
	const auto failed = [] {
		return CloudPasswordResult{ MTP_inputCheckPasswordEmpty() };
	};

	const auto p = BigNum(algo.p);
	const auto g = BigNum(algo.g);
	const auto B = BigNum(request.B);
	if (!MTP::IsPrimeAndGood(algo.p, algo.g)) {
		LOG(("API Error: Bad p/g in cloud password creation!"));
		return failed();
	} else if (!IsGoodLarge(B, p)) {
		LOG(("API Error: Bad B in cloud password check!"));
		return failed();
	}

	const auto context = Context();
	const auto x = BigNum(hash);
	const auto pForHash = NumBytesForHash(algo.p);
	const auto gForHash = BigNumForHash(g);
	const auto BForHash = NumBytesForHash(request.B);
	const auto g_x = BigNum::ModExp(g, x, p, context);
	const auto k = BigNum(Sha256(pForHash, gForHash));
	const auto kg_x = BigNum::ModMul(k, g_x, p, context);

	const auto GenerateAndCheckRandom = [&] {
		constexpr auto kRandomSize = 256;
		while (true) {
			auto random = bytes::vector(kRandomSize);
			bytes::set_random(random);
			const auto a = BigNum(random);
			const auto A = BigNum::ModExp(g, a, p, context);
			if (MTP::IsGoodModExpFirst(A, p)) {
				auto AForHash = BigNumForHash(A);
				const auto u = BigNum(Sha256(AForHash, BForHash));
				if (IsPositive(u)) {
					return std::make_tuple(a, std::move(AForHash), u);
				}
			}
		}
	};

	const auto [a, AForHash, u] = GenerateAndCheckRandom();
	const auto g_b = BigNum::ModSub(B, kg_x, p, context);
	if (!MTP::IsGoodModExpFirst(g_b, p)) {
		LOG(("API Error: Bad g_b in cloud password check!"));
		return failed();
	}
	const auto ux = BigNum::Mul(u, x, context);
	const auto a_ux = BigNum::Add(a, ux);
	const auto S = BigNum::ModExp(g_b, a_ux, p, context);
	if (S.failed()) {
		LOG(("API Error: Failed to count S in cloud password check!"));
		return failed();
	}
	const auto K = Sha256(BigNumForHash(S));
	const auto M1 = Sha256(
		Xor(Sha256(pForHash), Sha256(gForHash)),
		Sha256(algo.salt1),
		Sha256(algo.salt2),
		AForHash,
		BForHash,
		K);
	return CloudPasswordResult{ MTP_inputCheckPasswordSRP(
		MTP_long(request.id),
		MTP_bytes(AForHash),
		MTP_bytes(M1))
	};
}

bytes::vector ComputeHash(
		v::null_t,
		bytes::const_span password) {
	Unexpected("Bad secure secret algorithm.");
}

bytes::vector ComputeHash(
		const SecureSecretAlgoSHA512 &algo,
		bytes::const_span password) {
	return Sha512(algo.salt, password, algo.salt);
}

bytes::vector ComputeHash(
		const SecureSecretAlgoPBKDF2 &algo,
		bytes::const_span password) {
	return Pbkdf2Sha512(password, algo.salt, algo.kIterations);
}

} // namespace

CloudPasswordAlgo ParseCloudPasswordAlgo(const MTPPasswordKdfAlgo &data) {
	return data.match([](const MTPDpasswordKdfAlgoModPow &data) {
		return CloudPasswordAlgo(CloudPasswordAlgoModPow{
			bytes::make_vector(data.vsalt1().v),
			bytes::make_vector(data.vsalt2().v),
			data.vg().v,
			bytes::make_vector(data.vp().v) });
	}, [](const MTPDpasswordKdfAlgoUnknown &data) {
		return CloudPasswordAlgo();
	});
}

CloudPasswordCheckRequest ParseCloudPasswordCheckRequest(
		const MTPDaccount_password &data) {
	const auto algo = data.vcurrent_algo();
	return CloudPasswordCheckRequest{
		data.vsrp_id().value_or_empty(),
		bytes::make_vector(data.vsrp_B().value_or_empty()),
		(algo ? ParseCloudPasswordAlgo(*algo) : CloudPasswordAlgo())
	};
}

CloudPasswordAlgo ValidateNewCloudPasswordAlgo(CloudPasswordAlgo &&parsed) {
	if (!parsed.is<CloudPasswordAlgoModPow>()) {
		return v::null;
	}
	auto &value = parsed.get_unchecked<CloudPasswordAlgoModPow>();
	const auto already = value.salt1.size();
	value.salt1.resize(already + kAdditionalSalt);
	bytes::set_random(bytes::make_span(value.salt1).subspan(already));
	return std::move(parsed);
}

MTPPasswordKdfAlgo PrepareCloudPasswordAlgo(const CloudPasswordAlgo &data) {
	return data.match([](const CloudPasswordAlgoModPow &data) {
		return MTP_passwordKdfAlgoModPow(
			MTP_bytes(data.salt1),
			MTP_bytes(data.salt2),
			MTP_int(data.g),
			MTP_bytes(data.p));
	}, [](v::null_t) {
		return MTP_passwordKdfAlgoUnknown();
	});
}

CloudPasswordResult::operator bool() const {
	return (result.type() != mtpc_inputCheckPasswordEmpty);
}

bytes::vector ComputeCloudPasswordHash(
		const CloudPasswordAlgo &algo,
		bytes::const_span password) {
	return algo.match([&](const CloudPasswordAlgoModPow &data) {
		return ComputeHash(data, password);
	}, [](v::null_t) -> bytes::vector {
		Unexpected("Bad cloud password algorithm.");
	});
}

CloudPasswordDigest ComputeCloudPasswordDigest(
		const CloudPasswordAlgo &algo,
		bytes::const_span password) {
	return algo.match([&](const CloudPasswordAlgoModPow &data) {
		return ComputeDigest(data, password);
	}, [](v::null_t) -> CloudPasswordDigest {
		Unexpected("Bad cloud password algorithm.");
	});
}

CloudPasswordResult ComputeCloudPasswordCheck(
		const CloudPasswordCheckRequest &request,
		bytes::const_span hash) {
	return request.algo.match([&](const CloudPasswordAlgoModPow &data) {
		return ComputeCheck(request, data, hash);
	}, [](v::null_t) -> CloudPasswordResult {
		Unexpected("Bad cloud password algorithm.");
	});
}

SecureSecretAlgo ParseSecureSecretAlgo(
		const MTPSecurePasswordKdfAlgo &data) {
	return data.match([](
	const MTPDsecurePasswordKdfAlgoPBKDF2HMACSHA512iter100000 &data) {
		return SecureSecretAlgo(SecureSecretAlgoPBKDF2{
			bytes::make_vector(data.vsalt().v) });
	}, [](const MTPDsecurePasswordKdfAlgoSHA512 &data) {
		return SecureSecretAlgo(SecureSecretAlgoSHA512{
			bytes::make_vector(data.vsalt().v) });
	}, [](const MTPDsecurePasswordKdfAlgoUnknown &data) {
		return SecureSecretAlgo();
	});
}

SecureSecretAlgo ValidateNewSecureSecretAlgo(SecureSecretAlgo &&parsed) {
	if (!parsed.is<SecureSecretAlgoPBKDF2>()) {
		return v::null;
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
	}, [](v::null_t) {
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

CloudPasswordState ParseCloudPasswordState(
		const MTPDaccount_password &data) {
	auto result = CloudPasswordState();
	result.request = ParseCloudPasswordCheckRequest(data);
	result.unknownAlgorithm = data.vcurrent_algo() && !result.request;
	result.hasRecovery = data.is_has_recovery();
	result.notEmptyPassport = data.is_has_secure_values();
	result.hint = qs(data.vhint().value_or_empty());
	result.newPassword = ValidateNewCloudPasswordAlgo(
		ParseCloudPasswordAlgo(data.vnew_algo()));
	result.newSecureSecret = ValidateNewSecureSecretAlgo(
		ParseSecureSecretAlgo(data.vnew_secure_algo()));
	result.unconfirmedPattern =
		qs(data.vemail_unconfirmed_pattern().value_or_empty());
	return result;
}

} // namespace Core