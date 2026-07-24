/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/tests/gram_tests.h"

#include "gram/crypto/gram_ed25519.h"
#include "gram/crypto/gram_slip10.h"

namespace Gram::Tests {
namespace {

struct Slip10Step {
	quint32 index = 0;
	QByteArray chainCodeHex;
	QByteArray secretHex;
	QByteArray publicHex;
};

[[nodiscard]] QString CheckSlip10Level(
		int level,
		const HdKeyState &state,
		const Slip10Step &step) {
	const auto chainCodeFailure = CompareHex(
		state.chainCode,
		step.chainCodeHex);
	if (!chainCodeFailure.isEmpty()) {
		return u"level %1 chainCode: "_q.arg(level) + chainCodeFailure;
	}
	const auto secretFailure = CompareHex(state.key, step.secretHex);
	if (!secretFailure.isEmpty()) {
		return u"level %1 secret: "_q.arg(level) + secretFailure;
	}
	const auto keypair = KeyPairFromSeed(state.key);
	const auto publicFailure = CompareHex(keypair.publicKey, step.publicHex);
	if (!publicFailure.isEmpty()) {
		return u"level %1 public: "_q.arg(level) + publicFailure;
	}
	const auto layoutFailure = CompareHex(
		keypair.secretKey,
		step.secretHex + step.publicHex);
	if (!layoutFailure.isEmpty()) {
		return u"level %1 layout: "_q.arg(level) + layoutFailure;
	}
	return QString();
}

[[nodiscard]] QString CheckSlip10Chain(
		const QByteArray &seedHex,
		const std::vector<Slip10Step> &steps) {
	auto state = Ed25519MasterKey(QByteArray::fromHex(seedHex));
	const auto masterFailure = CheckSlip10Level(0, state, steps[0]);
	if (!masterFailure.isEmpty()) {
		return masterFailure;
	}
	for (auto i = 1; i != int(steps.size()); ++i) {
		state = DeriveHardened(state, steps[i].index);
		const auto failure = CheckSlip10Level(i, state, steps[i]);
		if (!failure.isEmpty()) {
			return failure;
		}
	}
	return QString();
}

} // namespace

std::vector<Check> KeyChecks() {
	return {
		{ u"slip10_ed25519_vector0"_q, [] {
			return CheckSlip10Chain(
				"000102030405060708090a0b0c0d0e0f",
				{
					{ 0,
						"90046a93de5380a72b5e45010748567d5ea02bbf6522f979e05c0d8d8ca9fffb",
						"2b4be7f19ee27bbf30c667b642d5f4aa69fd169872f8fc3059c08ebae2eb19e7",
						"a4b2856bfec510abab89753fac1ac0e1112364e7d250545963f135f2a33188ed" },
					{ 0,
						"8b59aa11380b624e81507a27fedda59fea6d0b779a778918a2fd3590e16e9c69",
						"68e0fe46dfb67e368c75379acec591dad19df3cde26e63b93a8e704f1dade7a3",
						"8c8a13df77a28f3445213a0f432fde644acaa215fc72dcdf300d5efaa85d350c" },
					{ 1,
						"a320425f77d1b5c2505a6b1b27382b37368ee640e3557c315416801243552f14",
						"b1d0bad404bf35da785a64ca1ac54b2617211d2777696fbffaf208f746ae84f2",
						"1932a5270f335bed617d5b935c80aedb1a35bd9fc1e31acafd5372c30f5c1187" },
					{ 2,
						"2e69929e00b5ab250f49c3fb1c12f252de4fed2c1db88387094a0f8c4c9ccd6c",
						"92a5b23c0b8a99e37d07df3fb9966917f5d06e02ddbd909c7e184371463e9fc9",
						"ae98736566d30ed0e9d2f4486a64bc95740d89c7db33f52121f8ea8f76ff0fc1" },
					{ 2,
						"8f6d87f93d750e0efccda017d662a1b31a266e4a6f5993b15f5c1f07f74dd5cc",
						"30d1dc7e5fc04c31219ab25a27ae00b50f6fd66622f6e9c913253d6511d1e662",
						"8abae2d66361c879b900d204ad2cc4984fa2aa344dd7ddc46007329ac76c429c" },
					{ 1000000000,
						"68789923a0cac2cd5a29172a475fe9e0fb14cd6adb5ad98a3fa70333e7afa230",
						"8f94d394a8e8fd6b1bc2f3f49f5c47e385281d5c17e65324b0f62483e37e8793",
						"3c24da049451555d51a7014a37337aa4e12d41e485abccfa46b47dfb2af54b7a" },
				});
		} },
		{ u"slip10_ed25519_vector1"_q, [] {
			return CheckSlip10Chain(
				"fffcf9f6f3f0edeae7e4e1dedbd8d5d2cfccc9c6c3c0bdbab7b4b1aeaba8a5a29f9c999693908d8a8784817e7b7875726f6c696663605d5a5754514e4b484542",
				{
					{ 0,
						"ef70a74db9c3a5af931b5fe73ed8e1a53464133654fd55e7a66f8570b8e33c3b",
						"171cb88b1b3c1db25add599712e36245d75bc65a1a5c9e18d76f9f2b1eab4012",
						"8fe9693f8fa62a4305a140b9764c5ee01e455963744fe18204b4fb948249308a" },
					{ 0,
						"0b78a3226f915c082bf118f83618a618ab6dec793752624cbeb622acb562862d",
						"1559eb2bbec5790b0c65d8693e4d0875b1747f4970ae8b650486ed7470845635",
						"86fab68dcb57aa196c77c5f264f215a112c22a912c10d123b0d03c3c28ef1037" },
					{ 2147483647,
						"138f0b2551bcafeca6ff2aa88ba8ed0ed8de070841f0c4ef0165df8181eaad7f",
						"ea4f5bfe8694d8bb74b7b59404632fd5968b774ed545e810de9c32a4fb4192f4",
						"5ba3b9ac6e90e83effcd25ac4e58a1365a9e35a3d3ae5eb07b9e4d90bcf7506d" },
					{ 1,
						"73bd9fff1cfbde33a1b846c27085f711c0fe2d66fd32e139d3ebc28e5a4a6b90",
						"3757c7577170179c7868353ada796c839135b3d30554bbb74a4b1e4a5a58505c",
						"2e66aa57069c86cc18249aecf5cb5a9cebbfd6fadeab056254763874a9352b45" },
					{ 2147483646,
						"0902fe8a29f9140480a00ef244bd183e8a13288e4412d8389d140aac1794825a",
						"5837736c89570de861ebc173b1086da4f505d4adb387c6a1b1342d5e4ac9ec72",
						"e33c0f7d81d843c572275f287498e8d408654fdf0d1e065b84e2e6f157aab09b" },
					{ 2,
						"5d70af781f3a37b829f0d060924d5e960bdc02e85423494afc0b1a41bbe196d4",
						"551d333177df541ad876a60ea71f00447931c0a9da16f227c11ea080d7391b8d",
						"47150c75db263559a70d5778bf36abbab30fb061ad69f69ece61a72b0cfa4fc0" },
				});
		} },
		{ u"slip10_derive_path_consistency"_q, [] {
			const auto seed = QByteArray::fromHex(
				"000102030405060708090a0b0c0d0e0f");
			return CompareHex(
				DeriveEd25519Path(seed, { 0, 1, 2 }),
				"92a5b23c0b8a99e37d07df3fb9966917f5d06e02ddbd909c7e184371463e9fc9");
		} },
		{ u"ed25519_sign_verify_roundtrip"_q, [] {
			const auto keypair = KeyPairFromSeed(QByteArray::fromHex(
				"2b4be7f19ee27bbf30c667b642d5f4aa69fd169872f8fc3059c08ebae2eb19e7"));
			const auto message = QByteArray("gram test message");
			const auto signature = Sign(message, keypair.secretKey);
			if (signature.size() != 64) {
				return u"signature size %1, expected 64"_q.arg(signature.size());
			}
			if (!Verify(message, signature, keypair.publicKey)) {
				return u"valid signature failed to verify"_q;
			}
			auto tamperedMessage = message;
			tamperedMessage[0] = char(tamperedMessage[0] ^ 0x01);
			if (Verify(tamperedMessage, signature, keypair.publicKey)) {
				return u"verify accepted a tampered message"_q;
			}
			auto tamperedSignature = signature;
			tamperedSignature[0] = char(tamperedSignature[0] ^ 0x01);
			if (Verify(message, tamperedSignature, keypair.publicKey)) {
				return u"verify accepted a tampered signature"_q;
			}
			return QString();
		} },
		{ u"ed25519_fake_sign"_q, [] {
			const auto message = QByteArray("gram fake sign message");
			const auto first = FakeSign(message);
			const auto second = FakeSign(message);
			if (first.size() != 64) {
				return u"fake signature size %1, expected 64"_q.arg(first.size());
			}
			if (first != second) {
				return u"fake signatures differ between calls"_q;
			}
			const auto zeroKeypair = KeyPairFromSeed(QByteArray(32, 0));
			if (!Verify(message, first, zeroKeypair.publicKey)) {
				return u"fake signature does not verify against the zero-seed key"_q;
			}
			const auto keypair = KeyPairFromSeed(QByteArray::fromHex(
				"2b4be7f19ee27bbf30c667b642d5f4aa69fd169872f8fc3059c08ebae2eb19e7"));
			const auto real = Sign(message, keypair.secretKey);
			if (first == real) {
				return u"fake signature matches a real signature"_q;
			}
			return QString();
		} },
	};
}

} // namespace Gram::Tests
