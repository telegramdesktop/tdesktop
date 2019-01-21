/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/bytes.h"
#include "base/algorithm.h"
#include "base/basic_types.h"

extern "C" {
#include <openssl/bn.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <openssl/aes.h>
#include <openssl/modes.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
} // extern "C"

namespace openssl {

class Context {
public:
	Context() : _data(BN_CTX_new()) {
	}
	Context(const Context &other) = delete;
	Context(Context &&other) : _data(base::take(other._data)) {
	}
	Context &operator=(const Context &other) = delete;
	Context &operator=(Context &&other) {
		_data = base::take(other._data);
		return *this;
	}
	~Context() {
		if (_data) {
			BN_CTX_free(_data);
		}
	}

	BN_CTX *raw() const {
		return _data;
	}

private:
	BN_CTX *_data = nullptr;

};

class BigNum {
public:
	BigNum() : _data(BN_new()) {
	}
	BigNum(const BigNum &other) : BigNum() {
		*this = other;
	}
	BigNum &operator=(const BigNum &other) {
		if (other.failed() || !BN_copy(raw(), other.raw())) {
			_failed = true;
		}
		return *this;
	}
	~BigNum() {
		BN_clear_free(raw());
	}

	explicit BigNum(unsigned int word) : BigNum() {
		setWord(word);
	}
	explicit BigNum(bytes::const_span bytes) : BigNum() {
		setBytes(bytes);
	}

	void setWord(unsigned int word) {
		if (!BN_set_word(raw(), word)) {
			_failed = true;
		}
	}
	void setBytes(bytes::const_span bytes) {
		if (!BN_bin2bn(
				reinterpret_cast<const unsigned char*>(bytes.data()),
				bytes.size(),
				raw())) {
			_failed = true;
		}
	}

	void setAdd(const BigNum &a, const BigNum &b) {
		if (a.failed() || b.failed()) {
			_failed = true;
		} else if (!BN_add(raw(), a.raw(), b.raw())) {
			_failed = true;
		}
	}
	void setSub(const BigNum &a, const BigNum &b) {
		if (a.failed() || b.failed()) {
			_failed = true;
		} else if (!BN_sub(raw(), a.raw(), b.raw())) {
			_failed = true;
		}
	}
	void setSubWord(unsigned int word) {
		if (failed()) {
			return;
		} else if (!BN_sub_word(raw(), word)) {
			_failed = true;
		}
	}
	void setMul(
			const BigNum &a,
			const BigNum &b,
			const Context &context = Context()) {
		if (a.failed() || b.failed()) {
			_failed = true;
		} else if (!BN_mul(raw(), a.raw(), b.raw(), context.raw())) {
			_failed = true;
		}
	}
	BN_ULONG setDivWord(BN_ULONG word) {
		Expects(word != 0);
		if (failed()) {
			return (BN_ULONG)-1;
		}

		auto result = BN_div_word(raw(), word);
		if (result == (BN_ULONG)-1) {
			_failed = true;
		}
		return result;
	}
	void setModSub(
			const BigNum &a,
			const BigNum &b,
			const BigNum &m,
			const Context &context = Context()) {
		if (a.failed() || b.failed() || m.failed()) {
			_failed = true;
		} else if (a.isNegative() || b.isNegative() || m.isNegative()) {
			_failed = true;
		} else if (!BN_mod_sub(raw(), a.raw(), b.raw(), m.raw(), context.raw())) {
			_failed = true;
		} else if (isNegative()) {
			_failed = true;
		}
	}
	void setModMul(
			const BigNum &a,
			const BigNum &b,
			const BigNum &m,
			const Context &context = Context()) {
		if (a.failed() || b.failed() || m.failed()) {
			_failed = true;
		} else if (a.isNegative() || b.isNegative() || m.isNegative()) {
			_failed = true;
		} else if (!BN_mod_mul(raw(), a.raw(), b.raw(), m.raw(), context.raw())) {
			_failed = true;
		} else if (isNegative()) {
			_failed = true;
		}
	}
	void setModExp(
			const BigNum &base,
			const BigNum &power,
			const BigNum &m,
			const Context &context = Context()) {
		if (base.failed() || power.failed() || m.failed()) {
			_failed = true;
		} else if (base.isNegative() || power.isNegative() || m.isNegative()) {
			_failed = true;
		} else if (!BN_mod_exp(raw(), base.raw(), power.raw(), m.raw(), context.raw())) {
			_failed = true;
		} else if (isNegative()) {
			_failed = true;
		}
	}

	bool isNegative() const {
		return failed() ? false : BN_is_negative(raw());
	}

	bool isPrime(const Context &context = Context()) const {
		if (failed()) {
			return false;
		}
		constexpr auto kMillerRabinIterationCount = 30;
		auto result = BN_is_prime_ex(
			raw(),
			kMillerRabinIterationCount,
			context.raw(),
			NULL);
		if (result == 1) {
			return true;
		} else if (result != 0) {
			_failed = true;
		}
		return false;
	}

	BN_ULONG modWord(BN_ULONG word) const {
		Expects(word != 0);
		if (failed()) {
			return (BN_ULONG)-1;
		}

		auto result = BN_mod_word(raw(), word);
		if (result == (BN_ULONG)-1) {
			_failed = true;
		}
		return result;
	}

	int bitsSize() const {
		return failed() ? 0 : BN_num_bits(raw());
	}
	int bytesSize() const {
		return failed() ? 0 : BN_num_bytes(raw());
	}

	bytes::vector getBytes() const {
		if (failed()) {
			return {};
		}
		auto length = BN_num_bytes(raw());
		auto result = bytes::vector(length);
		auto resultSize = BN_bn2bin(
			raw(),
			reinterpret_cast<unsigned char*>(result.data()));
		Assert(resultSize == length);
		return result;
	}

	BIGNUM *raw() {
		return _data;
	}
	const BIGNUM *raw() const {
		return _data;
	}
	BIGNUM *takeRaw() {
		return base::take(_data);
	}

	bool failed() const {
		return _failed;
	}

	static BigNum Add(const BigNum &a, const BigNum &b) {
		BigNum result;
		result.setAdd(a, b);
		return result;
	}
	static BigNum Sub(const BigNum &a, const BigNum &b) {
		BigNum result;
		result.setSub(a, b);
		return result;
	}
	static BigNum Mul(
			const BigNum &a,
			const BigNum &b,
			const Context &context = Context()) {
		BigNum result;
		result.setMul(a, b, context);
		return result;
	}
	static BigNum ModSub(
			const BigNum &a,
			const BigNum &b,
			const BigNum &mod,
			const Context &context = Context()) {
		BigNum result;
		result.setModSub(a, b, mod, context);
		return result;
	}
	static BigNum ModMul(
			const BigNum &a,
			const BigNum &b,
			const BigNum &mod,
			const Context &context = Context()) {
		BigNum result;
		result.setModMul(a, b, mod, context);
		return result;
	}
	static BigNum ModExp(
			const BigNum &base,
			const BigNum &power,
			const BigNum &mod,
			const Context &context = Context()) {
		BigNum result;
		result.setModExp(base, power, mod, context);
		return result;
	}
	static BigNum Failed() {
		BigNum result;
		result._failed = true;
		return result;
	}

private:
	BIGNUM *_data = nullptr;
	mutable bool _failed = false;

};

namespace details {

template <typename Context, typename Method, typename Arg>
inline void ShaUpdate(Context context, Method method, Arg &&arg) {
	const auto span = bytes::make_span(arg);
	method(context, span.data(), span.size());
}

template <typename Context, typename Method, typename Arg, typename ...Args>
inline void ShaUpdate(Context context, Method method, Arg &&arg, Args &&...args) {
	const auto span = bytes::make_span(arg);
	method(context, span.data(), span.size());
	ShaUpdate(context, method, args...);
}

template <size_type Size, typename Method>
inline bytes::vector Sha(Method method, bytes::const_span data) {
	auto result = bytes::vector(Size);
	method(
		reinterpret_cast<const unsigned char*>(data.data()),
		data.size(),
		reinterpret_cast<unsigned char*>(result.data()));
	return result;
}

template <
	size_type Size,
	typename Context,
	typename Init,
	typename Update,
	typename Finalize,
	typename ...Args,
	typename = std::enable_if_t<(sizeof...(Args) > 1)>>
bytes::vector Sha(
		Context context,
		Init init,
		Update update,
		Finalize finalize,
		Args &&...args) {
	auto result = bytes::vector(Size);

	init(&context);
	ShaUpdate(&context, update, args...);
	finalize(reinterpret_cast<unsigned char*>(result.data()), &context);

	return result;
}

template <
	size_type Size,
	typename Evp>
bytes::vector Pbkdf2(
		bytes::const_span password,
		bytes::const_span salt,
		int iterations,
		Evp evp) {
	auto result = bytes::vector(Size);
	PKCS5_PBKDF2_HMAC(
		reinterpret_cast<const char*>(password.data()),
		password.size(),
		reinterpret_cast<const unsigned char*>(salt.data()),
		salt.size(),
		iterations,
		evp,
		result.size(),
		reinterpret_cast<unsigned char*>(result.data()));
	return result;
}

} // namespace details

constexpr auto kSha1Size = size_type(SHA_DIGEST_LENGTH);
constexpr auto kSha256Size = size_type(SHA256_DIGEST_LENGTH);
constexpr auto kSha512Size = size_type(SHA512_DIGEST_LENGTH);

inline bytes::vector Sha1(bytes::const_span data) {
	return details::Sha<kSha1Size>(SHA1, data);
}

template <
	typename ...Args,
	typename = std::enable_if_t<(sizeof...(Args) > 1)>>
inline bytes::vector Sha1(Args &&...args) {
	return details::Sha<kSha1Size>(
		SHA_CTX(),
		SHA1_Init,
		SHA1_Update,
		SHA1_Final,
		args...);
}

inline bytes::vector Sha256(bytes::const_span data) {
	return details::Sha<kSha256Size>(SHA256, data);
}

template <
	typename ...Args,
	typename = std::enable_if_t<(sizeof...(Args) > 1)>>
inline bytes::vector Sha256(Args &&...args) {
	return details::Sha<kSha256Size>(
		SHA256_CTX(),
		SHA256_Init,
		SHA256_Update,
		SHA256_Final,
		args...);
}

inline bytes::vector Sha512(bytes::const_span data) {
	return details::Sha<kSha512Size>(SHA512, data);
}

template <
	typename ...Args,
	typename = std::enable_if_t<(sizeof...(Args) > 1)>>
inline bytes::vector Sha512(Args &&...args) {
	return details::Sha<kSha512Size>(
		SHA512_CTX(),
		SHA512_Init,
		SHA512_Update,
		SHA512_Final,
		args...);
}

inline void AddRandomSeed(bytes::const_span data) {
	RAND_seed(data.data(), data.size());
}

inline bytes::vector Pbkdf2Sha512(
		bytes::const_span password,
		bytes::const_span salt,
		int iterations) {
	return details::Pbkdf2<kSha512Size>(
		password,
		salt,
		iterations,
		EVP_sha512());
}

} // namespace openssl

namespace bytes {

inline void set_random(span destination) {
	RAND_bytes(
		reinterpret_cast<unsigned char*>(destination.data()),
		destination.size());
}

} // namespace bytes
