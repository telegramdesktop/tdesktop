/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <openssl/bn.h>
#include <openssl/sha.h>
#include <openssl/rand.h>

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
	explicit BigNum(base::const_byte_span bytes) : BigNum() {
		setBytes(bytes);
	}

	void setWord(unsigned int word) {
		if (!BN_set_word(raw(), word)) {
			_failed = true;
		}
	}
	void setBytes(base::const_byte_span bytes) {
		if (!BN_bin2bn(
				reinterpret_cast<const unsigned char*>(bytes.data()),
				bytes.size(),
				raw())) {
			_failed = true;
		}
	}
	void setModExp(
			const BigNum &a,
			const BigNum &p,
			const BigNum &m,
			const Context &context = Context()) {
		if (a.failed() || p.failed() || m.failed()) {
			_failed = true;
		} else if (a.isNegative() || p.isNegative() || m.isNegative()) {
			_failed = true;
		} else if (!BN_mod_exp(raw(), a.raw(), p.raw(), m.raw(), context.raw())) {
			_failed = true;
		} else if (isNegative()) {
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

	base::byte_vector getBytes() const {
		if (failed()) {
			return base::byte_vector();
		}
		auto length = BN_num_bytes(raw());
		auto result = base::byte_vector(length, gsl::byte());
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

	static BigNum ModExp(const BigNum &base, const BigNum &power, const openssl::BigNum &mod) {
		BigNum result;
		result.setModExp(base, power, mod);
		return result;
	}

private:
	BIGNUM *_data = nullptr;
	mutable bool _failed = false;

};

inline BigNum operator-(const BigNum &a, const BigNum &b) {
	BigNum result;
	result.setSub(a, b);
	return result;
}

inline base::byte_array<SHA256_DIGEST_LENGTH> Sha256(base::const_byte_span bytes) {
	auto result = base::byte_array<SHA256_DIGEST_LENGTH>();
	SHA256(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), reinterpret_cast<unsigned char*>(result.data()));
	return result;
}

inline base::byte_array<SHA_DIGEST_LENGTH> Sha1(base::const_byte_span bytes) {
	auto result = base::byte_array<SHA_DIGEST_LENGTH>();
	SHA1(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), reinterpret_cast<unsigned char*>(result.data()));
	return result;
}

inline int FillRandom(base::byte_span bytes) {
	return RAND_bytes(reinterpret_cast<unsigned char*>(bytes.data()), bytes.size());
}

} // namespace openssl
