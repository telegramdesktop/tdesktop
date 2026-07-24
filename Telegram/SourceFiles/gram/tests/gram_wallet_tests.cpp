/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/tests/gram_tests.h"

#include "gram/crypto/gram_mnemonic.h"
#include "gram/ton/gram_address.h"
#include "gram/ton/gram_boc.h"
#include "gram/ton/gram_cell.h"
#include "gram/ton/gram_message.h"
#include "gram/wallet/gram_wallet_v5.h"

#include <QtCore/QFile>

#include <vector>

namespace Gram::Tests {
namespace {

const auto kFixturePublicKeyHex = QByteArray(
	"f6c450a16bb1c514e22f1977e390a3025599aa1e7b00068a6aacf2119484c1bd");

const auto kFixtureStateInitBase64 = QByteArray()
	+ "te6cckECFgEAArEAAgE0ARUBFP8A9KQT9LzyyAsCAgEgAw4CAUgEBQLc0CDXScEg"
	+ "kVuPYyDXCx8gghBleHRuvSGCEHNpbnS9sJJfA+CCEGV4dG66jrSAINchAdB01yH6"
	+ "QDD6RPgo+kQwWL2RW+DtRNCBAUHXIfQFgwf0Dm+hMZEw4YBA1yFwf9s84DEg10mB"
	+ "AoC5kTDgcOIREAIBIAYNAgEgBwoCAW4ICQAZrc52omhAIOuQ64X/wAAZrx32omhA"
	+ "EOuQ64WPwAIBSAsMABezJftRNBx1yHXCx+AAEbJi+1E0NcKAIAAZvl8PaiaECAoO"
	+ "uQ+gLAEC8g8BHiDXCx+CEHNpZ2668uCKfxAB5o7w7aLt+yGDCNciAoMI1yMggCDX"
	+ "IdMf0x/TH+1E0NIA0x8g0x/T/9cKAAr5AUDM+RCaKJRfCtsx4fLAh98Cs1AHsPLQ"
	+ "hFEluvLghVA2uvLghvgju/LQiCKS+ADeAaR/yMoAyx8BzxbJ7VQgkvgP3nDbPNgR"
	+ "A/btou37AvQEIW6SbCGOTAIh1zkwcJQhxwCzji0B1yggdh5DbCDXScAI8uCTINdK"
	+ "wALy4JMg1x0GxxLCAFIwsPLQiddM1zkwAaTobBKEB7vy4JPXSsAA8uCT7VXi0gAB"
	+ "wACRW+Dr1ywIFCCRcJYB1ywIHBLiUhCx4w8g10oSExQAlgH6QAH6RPgo+kQwWLry"
	+ "4JHtRNCBAUHXGPQFBJ1/yMoAQASDB/RT8uCLjhQDgwf0W/LgjCLXCgAhbgGzsPLQ"
	+ "kOLIUAPPFhL0AMntVAByMNcsCCSOLSHy4JLSAO1E0NIAURO68tCPVFAwkTGcAYEB"
	+ "QNch1woA8uCO4sjKAFjPFsntVJPywI3iABCTW9sx4ddM0ABRgAAAAD///4j7YihQ"
	+ "tdjiinEXjLvxyFGBKszVDz2AA0U1VnkIykJg3qCxZgt/";

const auto kFixtureEQ = u"EQDSLOFVamNZzdy4LulclcCBEFkRReZ7WscBCLAw3Pg53kAk"_q;
const auto kFixtureUQ = u"UQDSLOFVamNZzdy4LulclcCBEFkRReZ7WscBCLAw3Pg53h3h"_q;
const auto kFixture0Q = u"0QDSLOFVamNZzdy4LulclcCBEFkRReZ7WscBCLAw3Pg53qZr"_q;

[[nodiscard]] std::vector<QString> FixtureWords() {
	return {
		u"hospital"_q, u"stove"_q, u"relief"_q, u"fringe"_q,
		u"tongue"_q, u"always"_q, u"charge"_q, u"angry"_q,
		u"urge"_q, u"sentence"_q, u"again"_q, u"match"_q,
		u"nerve"_q, u"inquiry"_q, u"senior"_q, u"coconut"_q,
		u"label"_q, u"tumble"_q, u"carry"_q, u"category"_q,
		u"beauty"_q, u"bean"_q, u"road"_q, u"solution"_q,
	};
}

[[nodiscard]] std::optional<KeyPair> FixtureKey() {
	return MnemonicToKeyPair(FixtureWords(), MnemonicType::Ton);
}

[[nodiscard]] QByteArray ReadFixture(const QString &name) {
	auto file = QFile(
		QString::fromUtf8(GRAM_TEST_FIXTURES_PATH) + u"/"_q + name);
	if (!file.open(QIODevice::ReadOnly)) {
		return QByteArray();
	}
	return file.readAll().trimmed();
}

[[nodiscard]] TransferRequest FixtureTransferRequest(const Address &dest) {
	auto request = TransferRequest();
	auto message = TransferMessage();
	message.destination = dest;
	message.bounce = true;
	message.amountNano = 10000000;
	message.body = BuildCommentBody(u"test"_q);
	request.messages.push_back(message);
	request.seqno = 5;
	request.validUntil = 1753300000;
	return request;
}

[[nodiscard]] Address TestDest(uchar byte) {
	return Address{ 0, QByteArray(32, char(byte)) };
}

[[nodiscard]] Address DefaultDest() {
	return Address{
		0,
		QByteArray::fromHex(
			"1234567890abcdef1234567890abcdef"
			"1234567890abcdef1234567890abcdef"),
	};
}

[[nodiscard]] Cell Uint32Body(quint32 value) {
	return CellBuilder().storeUint(value, 32).finish();
}

[[nodiscard]] Cell MakeExternalIn(
		qint64 importFee,
		bool withInit,
		const Cell &body,
		const Address &dest) {
	auto builder = CellBuilder();
	builder.storeBit(true);
	builder.storeBit(false);
	builder.storeAddress(std::nullopt);
	builder.storeAddress(dest);
	builder.storeCoins(importFee);
	if (withInit) {
		const auto initCell = BuildStateInit({
			CellBuilder().storeUint(0xff, 8).finish(),
			CellBuilder().storeUint(0, 8).finish(),
		});
		builder.storeBit(true);
		builder.storeBit(true);
		builder.storeRef(initCell);
	} else {
		builder.storeBit(false);
	}
	builder.storeBit(true);
	builder.storeRef(body);
	return builder.finish();
}

} // namespace

std::vector<Check> WalletChecks() {
	return {
		{ u"w5_fixture_state_init"_q, [] {
			const auto key = FixtureKey();
			if (!key) {
				return u"mnemonic: key derivation failed"_q;
			}
			const auto keyFailure = CompareHex(
				key->publicKey,
				kFixturePublicKeyHex);
			if (!keyFailure.isEmpty()) {
				return u"publicKey: "_q + keyFailure;
			}
			const auto init = BuildStateInit({
				WalletV5Code(),
				WalletV5InitData(key->publicKey),
			});
			const auto expected = QByteArray::fromBase64(
				kFixtureStateInitBase64);
			const auto failure = CompareHex(
				SerializeBoc(init),
				expected.toHex());
			return failure.isEmpty()
				? QString()
				: (u"stateInit: "_q + failure);
		} },
		{ u"w5_fixture_addresses"_q, [] {
			const auto address = WalletV5Address(
				QByteArray::fromHex(kFixturePublicKeyHex));
			if (FormatFriendly(address, true) != kFixtureEQ) {
				return u"bounceable mismatch"_q;
			} else if (FormatFriendly(address, false) != kFixtureUQ) {
				return u"non-bounceable mismatch"_q;
			} else if (FormatFriendly(address, false, true) != kFixture0Q) {
				return u"testnet mismatch"_q;
			}
			return QString();
		} },
		{ u"comment_snake_roundtrip"_q, [] {
			const auto texts = std::vector<QString>{
				QString(),
				u"test"_q,
				QString(123, QChar(u'a')),
				QString(124, QChar(u'a')),
				QString(250, QChar(u'a')),
				QString(410, QChar(u'a')),
				u"тест é中文"_q,
			};
			for (const auto &text : texts) {
				const auto length = QString::number(text.size());
				const auto cell = BuildCommentBody(text);
				auto slice = cell.parse();
				if (slice.loadUint(32) != 0) {
					return u"op mismatch for length "_q + length;
				}
				const auto loaded = slice.loadStringTail();
				if (loaded != text) {
					return u"text mismatch for length "_q + length;
				} else if (slice.remainingBits() != 0
					|| slice.remainingRefs() != 0
					|| !slice.ok()) {
					return u"not fully consumed for length "_q + length;
				}
			}
			if (BuildCommentBody(QString()).refsCount() != 0) {
				return u"empty text produced a ref"_q;
			} else if (BuildCommentBody(
				QString(123, QChar(u'a'))).refsCount() != 0) {
				return u"123-byte text produced a ref"_q;
			}
			return QString();
		} },
		{ u"w5_seqno_from_state_data"_q, [] {
			const auto key = QByteArray::fromHex(kFixturePublicKeyHex);
			const auto data = CellBuilder()
				.storeBit(true)
				.storeUint(5, 32)
				.storeUint(kDefaultWalletId, 32)
				.storeBytes(key)
				.storeBit(false)
				.finish();
			const auto boc = SerializeBoc(data);
			if (SeqnoFromStateData(boc) != 5) {
				return u"seqno: expected 5"_q;
			}
			const auto loadedKey = PublicKeyFromStateData(boc);
			if (!loadedKey || *loadedKey != key) {
				return u"publicKey: round-trip mismatch"_q;
			}
			const auto short32 = SerializeBoc(
				CellBuilder().storeUint(7, 32).finish());
			if (SeqnoFromStateData(short32)) {
				return u"short cell: expected nullopt"_q;
			}
			const auto empty = SerializeBoc(CellBuilder().finish());
			if (SeqnoFromStateData(empty)) {
				return u"empty cell: expected nullopt"_q;
			} else if (SeqnoFromStateData(QByteArray())) {
				return u"empty bytes: expected nullopt"_q;
			} else if (SeqnoFromStateData(QByteArray("notaboc"))) {
				return u"garbage: expected nullopt"_q;
			}
			return QString();
		} },
		{ u"tep467_import_fee_ignored"_q, [] {
			const auto dest = DefaultDest();
			const auto body = Uint32Body(0xdeadbeef);
			const auto a = NormalizedExternalHash(
				MakeExternalIn(0, false, body, dest));
			const auto b = NormalizedExternalHash(
				MakeExternalIn(999999, false, body, dest));
			if (a.isEmpty() || b.isEmpty()) {
				return u"empty hash"_q;
			} else if (a != b) {
				return u"import fee changed the hash"_q;
			}
			return QString();
		} },
		{ u"tep467_state_init_stripped"_q, [] {
			const auto dest = DefaultDest();
			const auto body = Uint32Body(0xdeadbeef);
			const auto without = NormalizedExternalHash(
				MakeExternalIn(0, false, body, dest));
			const auto with = NormalizedExternalHash(
				MakeExternalIn(0, true, body, dest));
			if (without.isEmpty() || with.isEmpty()) {
				return u"empty hash"_q;
			} else if (without != with) {
				return u"state init changed the hash"_q;
			}
			return QString();
		} },
		{ u"tep467_idempotent"_q, [] {
			const auto dest = DefaultDest();
			const auto message = MakeExternalIn(
				1000,
				true,
				Uint32Body(0xdeadbeef),
				dest);
			const auto normalized = NormalizeExternalMessage(message);
			if (!normalized) {
				return u"normalize: expected a value"_q;
			}
			const auto renormalized = NormalizeExternalMessage(*normalized);
			if (!renormalized) {
				return u"renormalize: expected a value"_q;
			} else if (renormalized->hash() != normalized->hash()) {
				return u"idempotency: hash changed"_q;
			} else if (normalized->hash()
				!= NormalizedExternalHash(message)) {
				return u"hash: accessor mismatch"_q;
			}
			return QString();
		} },
		{ u"tep467_distinguishes"_q, [] {
			const auto dest = DefaultDest();
			const auto body1 = NormalizedExternalHash(
				MakeExternalIn(0, false, Uint32Body(1), dest));
			const auto body2 = NormalizedExternalHash(
				MakeExternalIn(0, false, Uint32Body(2), dest));
			if (body1.isEmpty() || body2.isEmpty()) {
				return u"empty body hash"_q;
			} else if (body1 == body2) {
				return u"different bodies share a hash"_q;
			}
			const auto body = Uint32Body(0xdeadbeef);
			const auto dest1 = NormalizedExternalHash(
				MakeExternalIn(0, false, body, TestDest(0x11)));
			const auto dest2 = NormalizedExternalHash(
				MakeExternalIn(0, false, body, TestDest(0x22)));
			if (dest1.isEmpty() || dest2.isEmpty()) {
				return u"empty dest hash"_q;
			} else if (dest1 == dest2) {
				return u"different destinations share a hash"_q;
			}
			return QString();
		} },
		{ u"tep467_rejects_non_external"_q, [] {
			const auto dest = DefaultDest();
			const auto internal = BuildInternalMessage(
				dest,
				true,
				1000000000,
				std::nullopt,
				std::nullopt);
			if (NormalizeExternalMessage(internal)) {
				return u"internal: expected nullopt"_q;
			} else if (!NormalizedExternalHash(internal).isEmpty()) {
				return u"internal: expected empty hash"_q;
			}
			const auto plain = Uint32Body(5);
			if (NormalizeExternalMessage(plain)) {
				return u"plain: expected nullopt"_q;
			} else if (!NormalizedExternalHash(plain).isEmpty()) {
				return u"plain: expected empty hash"_q;
			}
			return QString();
		} },
		{ u"w5_signed_transfer_roundtrip"_q, [] {
			const auto key = FixtureKey();
			if (!key) {
				return u"mnemonic: key derivation failed"_q;
			}
			const auto parsed = ParseAddress(kFixtureEQ);
			if (!parsed) {
				return u"address: parse failed"_q;
			}
			const auto request = FixtureTransferRequest(parsed->address);
			const auto real = BuildSignedTransfer(*key, request);
			const auto realCell = DeserializeBoc(real);
			if (!realCell) {
				return u"real: deserialize failed"_q;
			}
			const auto realFailure = CompareHex(
				SerializeBoc(*realCell),
				real.toHex());
			if (!realFailure.isEmpty()) {
				return u"real: "_q + realFailure;
			}
			const auto fake = BuildFakeSignedTransfer(
				key->publicKey,
				request);
			if (fake == real) {
				return u"fake: equals real transfer"_q;
			}
			const auto fakeCell = DeserializeBoc(fake);
			if (!fakeCell) {
				return u"fake: deserialize failed"_q;
			}
			const auto fakeFailure = CompareHex(
				SerializeBoc(*fakeCell),
				fake.toHex());
			if (!fakeFailure.isEmpty()) {
				return u"fake: "_q + fakeFailure;
			}
			return QString();
		} },
		{ u"w5_parity_signed_transfer"_q, [] {
			const auto expected = ReadFixture(u"signed-transfer.boc.b64"_q);
			if (expected.isEmpty()) {
				return u"fixture read failed: signed-transfer.boc.b64"_q;
			}
			const auto key = FixtureKey();
			if (!key) {
				return u"mnemonic: key derivation failed"_q;
			}
			const auto parsed = ParseAddress(kFixtureEQ);
			if (!parsed) {
				return u"address: parse failed"_q;
			}
			const auto real = BuildSignedTransfer(
				*key,
				FixtureTransferRequest(parsed->address));
			return CompareHex(real, QByteArray::fromBase64(expected).toHex());
		} },
		{ u"w5_parity_fake_signed_transfer"_q, [] {
			const auto expected = ReadFixture(
				u"signed-transfer-fake.boc.b64"_q);
			if (expected.isEmpty()) {
				return u"fixture read failed: signed-transfer-fake.boc.b64"_q;
			}
			const auto key = FixtureKey();
			if (!key) {
				return u"mnemonic: key derivation failed"_q;
			}
			const auto parsed = ParseAddress(kFixtureEQ);
			if (!parsed) {
				return u"address: parse failed"_q;
			}
			const auto fake = BuildFakeSignedTransfer(
				key->publicKey,
				FixtureTransferRequest(parsed->address));
			return CompareHex(fake, QByteArray::fromBase64(expected).toHex());
		} },
		{ u"w5_parity_normalized_hash"_q, [] {
			const auto expectedReal = ReadFixture(u"normalized-ext-hash.hex"_q);
			if (expectedReal.isEmpty()) {
				return u"fixture read failed: normalized-ext-hash.hex"_q;
			}
			const auto expectedFake = ReadFixture(
				u"normalized-ext-hash-fake.hex"_q);
			if (expectedFake.isEmpty()) {
				return u"fixture read failed: normalized-ext-hash-fake.hex"_q;
			}
			if (expectedReal == expectedFake) {
				return u"fixtures: real and fake hashes must differ"_q;
			}
			const auto key = FixtureKey();
			if (!key) {
				return u"mnemonic: key derivation failed"_q;
			}
			const auto parsed = ParseAddress(kFixtureEQ);
			if (!parsed) {
				return u"address: parse failed"_q;
			}
			const auto request = FixtureTransferRequest(parsed->address);
			const auto realCell = DeserializeBoc(
				BuildSignedTransfer(*key, request));
			if (!realCell) {
				return u"real: deserialize failed"_q;
			}
			const auto realFailure = CompareHex(
				NormalizedExternalHash(*realCell),
				expectedReal);
			if (!realFailure.isEmpty()) {
				return u"real: "_q + realFailure;
			}
			const auto fakeCell = DeserializeBoc(
				BuildFakeSignedTransfer(key->publicKey, request));
			if (!fakeCell) {
				return u"fake: deserialize failed"_q;
			}
			const auto fakeFailure = CompareHex(
				NormalizedExternalHash(*fakeCell),
				expectedFake);
			return fakeFailure.isEmpty()
				? QString()
				: (u"fake: "_q + fakeFailure);
		} },
		{ u"w5_parity_code_cell_hash"_q, [] {
			const auto expected = ReadFixture(u"w5-code-cell-hash.hex"_q);
			if (expected.isEmpty()) {
				return u"fixture read failed: w5-code-cell-hash.hex"_q;
			}
			return CompareHex(WalletV5Code().hash(), expected);
		} },
	};
}

} // namespace Gram::Tests
