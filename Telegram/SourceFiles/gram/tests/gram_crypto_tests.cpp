/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/tests/gram_tests.h"

#include "base/openssl_help.h"
#include "gram/crypto/gram_hmac.h"

namespace Gram::Tests {
namespace {

[[nodiscard]] QString CompareHex(
		const QByteArray &got,
		const QByteArray &expectedHex) {
	if (got == QByteArray::fromHex(expectedHex)) {
		return QString();
	}
	return u"got "_q
		+ QString::fromLatin1(got.toHex())
		+ u", expected "_q
		+ QString::fromLatin1(expectedHex);
}

[[nodiscard]] QString CheckHmacSha512(
		const QByteArray &key,
		const QByteArray &data,
		const QByteArray &expectedHex) {
	return CompareHex(
		HmacSha512(bytes::make_span(key), bytes::make_span(data)),
		expectedHex);
}

[[nodiscard]] QString CheckPbkdf2Sha512(
		const QByteArray &password,
		const QByteArray &salt,
		int iterations,
		const QByteArray &expectedHex) {
	const auto result = openssl::Pbkdf2Sha512(
		bytes::make_span(password),
		bytes::make_span(salt),
		iterations);
	return CompareHex(
		QByteArray(
			reinterpret_cast<const char*>(result.data()),
			result.size()),
		expectedHex);
}

} // namespace

std::vector<Check> CryptoChecks() {
	return {
		{ u"hmac_sha512_rfc4231_case2"_q, [] {
			return CheckHmacSha512(
				QByteArray("Jefe"),
				QByteArray::fromHex(
					"7768617420646f2079612077616e7420666f72206e6f7468696e673f"),
				"164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea2505549758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737");
		} },
		{ u"hmac_sha512_rfc4231_case1"_q, [] {
			return CheckHmacSha512(
				QByteArray::fromHex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b"),
				QByteArray::fromHex("4869205468657265"),
				"87aa7cdea5ef619d4ff0b4241a1d6cb02379f4e2ce4ec2787ad0b30545e17cdedaa833b7d6b8a702038b274eaea3f4e4be9d914eeb61f1702e696c203a126854");
		} },
		{ u"hmac_sha512_rfc4231_case3"_q, [] {
			return CheckHmacSha512(
				QByteArray::fromHex("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
				QByteArray::fromHex(
					"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"),
				"fa73b0089d56a284efb0f0756c890be9b1b5dbdd8ee81a3655f83e33b2279d39bf3e848279a722c806b485a47e67c807b946a337bee8942674278859e13292fb");
		} },
		{ u"pbkdf2_sha512_iterations_1"_q, [] {
			return CheckPbkdf2Sha512(
				QByteArray("password"),
				QByteArray("salt"),
				1,
				"867f70cf1ade02cff3752599a3a53dc4af34c7a669815ae5d513554e1c8cf252c02d470a285a0501bad999bfe943c08f050235d7d68b1da55e63f73b60a57fce");
		} },
		{ u"pbkdf2_sha512_iterations_2"_q, [] {
			return CheckPbkdf2Sha512(
				QByteArray("password"),
				QByteArray("salt"),
				2,
				"e1d9c16aa681708a45f5c7c4e215ceb66e011a2e9f0040713f18aefdb866d53cf76cab2868a39b9f7840edce4fef5a82be67335c77a6068e04112754f27ccf4e");
		} },
		{ u"pbkdf2_sha512_iterations_4096"_q, [] {
			return CheckPbkdf2Sha512(
				QByteArray("password"),
				QByteArray("salt"),
				4096,
				"d197b1b33db0143e018b12f3d1d1479e6cdebdcc97c5c0f87f6902e072f457b5143f30602641b3d55cd335988cb36b84376060ecd532e039b742a239434af2d5");
		} },
		{ u"pbkdf2_sha512_long_input"_q, [] {
			return CheckPbkdf2Sha512(
				QByteArray("passwordPASSWORDpassword"),
				QByteArray("saltSALTsaltSALTsaltSALTsaltSALTsalt"),
				4096,
				"8c0511f4c6e597c6ac6315d8f0362e225f3c501495ba23b868c005174dc4ee71115b59f9e60cd9532fa33e0f75aefe30225c583a186cd82bd4daea9724a3d3b8");
		} },
	};
}

} // namespace Gram::Tests
