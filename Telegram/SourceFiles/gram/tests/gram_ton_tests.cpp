/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/tests/gram_tests.h"

#include "gram/ton/gram_address.h"
#include "gram/ton/gram_boc.h"
#include "gram/ton/gram_cell.h"
#include "gram/ton/gram_crc.h"

#include <tuple>

namespace Gram::Tests {
namespace {

const auto kW5CodeHex = QByteArray()
	+ "b5ee9c7201021401000281000114ff00f4a413f4bcf2c80b0102012002030201"
	+ "4804050102f20e02dcd020d749c120915b8f6320d70b1f2082106578746ebd21"
	+ "821073696e74bdb0925f03e082106578746eba8eb48020d72101d074d721fa40"
	+ "30fa44f828fa443058bd915be0ed44d0810141d721f4058307f40e6fa1319130"
	+ "e18040d721707fdb3ce03120d749810280b99130e070e2100f02012006070201"
	+ "2008090019be5f0f6a2684080a0eb90fa02c02016e0a0b0201480c0d0019adce"
	+ "76a2684020eb90eb85ffc00019af1df6a2684010eb90eb858fc00017b325fb51"
	+ "341c75c875c2c7e00011b262fb513435c28020011e20d70b1f82107369676eba"
	+ "f2e08a7f0f01e68ef0eda2edfb218308d722028308d723208020d721d31fd31f"
	+ "d31fed44d0d200d31f20d31fd3ffd70a000af90140ccf9109a28945f0adb31e1"
	+ "f2c087df02b35007b0f2d0845125baf2e0855036baf2e086f823bbf2d0882292"
	+ "f800de01a47fc8ca00cb1f01cf16c9ed542092f80fde70db3cd81003f6eda2ed"
	+ "fb02f404216e926c218e4c0221d73930709421c700b38e2d01d72820761e436c"
	+ "20d749c008f2e09320d74ac002f2e09320d71d06c712c2005230b0f2d089d74c"
	+ "d7393001a4e86c128407bbf2e093d74ac000f2e093ed55e2d20001c000915be0"
	+ "ebd72c08142091709601d72c081c12e25210b1e30f20d74a111213009601fa40"
	+ "01fa44f828fa443058baf2e091ed44d0810141d718f405049d7fc8ca00400483"
	+ "07f453f2e08b8e14038307f45bf2e08c22d70a00216e01b3b0f2d090e2c85003"
	+ "cf1612f400c9ed54007230d72c08248e2d21f2e092d200ed44d0d2005113baf2"
	+ "d08f54503091319c01810140d721d70a00f2e08ee2c8ca0058cf16c9ed5493f2"
	+ "c08de20010935bdb31e1d74cd0";

const auto kW5CodeHash = QByteArray(
	"20834b7b72b112147e1b2fb457b84e74d1a30f04f737d4f62a668e9552d2b72f");

const auto kSpecHashHex = QByteArray(
	"2cf55953e92efbeadab7ba725c3f93a0b23f842cbba72d7b8e6f510a70e422e3");

const auto kSpecRaw = QString(
	u"0:2cf55953e92efbeadab7ba725c3f93a0b23f842cbba72d7b8e6f510a70e422e3"_q);

const auto kEQ = QString(
	u"EQAs9VlT6S776tq3unJcP5Ogsj-ELLunLXuOb1EKcOQi4wJB"_q);
const auto kUQ = QString(
	u"UQAs9VlT6S776tq3unJcP5Ogsj-ELLunLXuOb1EKcOQi41-E"_q);
const auto kkQ = QString(
	u"kQAs9VlT6S776tq3unJcP5Ogsj-ELLunLXuOb1EKcOQi47nL"_q);
const auto k0Q = QString(
	u"0QAs9VlT6S776tq3unJcP5Ogsj-ELLunLXuOb1EKcOQi4-QO"_q);

[[nodiscard]] Address SpecAddress() {
	auto result = Address();
	result.workchain = 0;
	result.hash = QByteArray::fromHex(kSpecHashHex);
	return result;
}

[[nodiscard]] QString CheckCellGolden(
		const Cell &cell,
		const QByteArray &hashHex,
		const QByteArray &bocPlainHex,
		const QByteArray &bocCrcHex) {
	const auto hashFailure = CompareHex(cell.hash(), hashHex);
	if (!hashFailure.isEmpty()) {
		return u"hash: "_q + hashFailure;
	}
	const auto plain = SerializeBoc(cell, false);
	const auto plainFailure = CompareHex(plain, bocPlainHex);
	if (!plainFailure.isEmpty()) {
		return u"boc: "_q + plainFailure;
	}
	const auto crc = SerializeBoc(cell, true);
	const auto crcFailure = CompareHex(crc, bocCrcHex);
	if (!crcFailure.isEmpty()) {
		return u"boc_crc: "_q + crcFailure;
	}
	const auto reparsedPlain = DeserializeBoc(plain);
	if (!reparsedPlain || !(*reparsedPlain == cell)) {
		return u"reparse: plain form mismatch"_q;
	}
	const auto reparsedCrc = DeserializeBoc(crc);
	if (!reparsedCrc || !(*reparsedCrc == cell)) {
		return u"reparse: crc form mismatch"_q;
	}
	return QString();
}

[[nodiscard]] QString CheckRoundTrip(const QByteArray &bocHex) {
	const auto first = DeserializeBoc(QByteArray::fromHex(bocHex));
	if (!first) {
		return u"first deserialize failed"_q;
	}
	const auto serialized = SerializeBoc(*first);
	const auto second = DeserializeBoc(serialized);
	if (!second) {
		return u"second deserialize failed"_q;
	}
	if (!(*first == *second)) {
		return u"reserialized cell differs"_q;
	}
	return QString();
}

[[nodiscard]] QString CheckRejected(const QByteArray &boc) {
	if (DeserializeBoc(boc)) {
		return u"expected nullopt, got a cell"_q;
	}
	return QString();
}

[[nodiscard]] QString CheckFriendlyForm(
		const QString &text,
		bool bounceable,
		bool testnet) {
	const auto parsed = ParseAddress(text);
	if (!parsed) {
		return u"parse failed"_q;
	} else if (!parsed->friendly) {
		return u"expected friendly"_q;
	} else if (parsed->bounceable != bounceable) {
		return u"bounceable mismatch"_q;
	} else if (parsed->testnet != testnet) {
		return u"testnet mismatch"_q;
	} else if (!(parsed->address == SpecAddress())) {
		return u"address mismatch"_q;
	} else if (FormatRaw(parsed->address) != kSpecRaw) {
		return u"FormatRaw mismatch"_q;
	}
	return QString();
}

} // namespace

std::vector<Check> TonChecks() {
	return {
		{ u"crc32c_123456789"_q, [] {
			const auto value = Crc32C(QByteArray("123456789"));
			auto bytes = QByteArray();
			bytes.append(char(value & 0xFF));
			bytes.append(char((value >> 8) & 0xFF));
			bytes.append(char((value >> 16) & 0xFF));
			bytes.append(char((value >> 24) & 0xFF));
			return CompareHex(bytes, "839206e3");
		} },
		{ u"crc16_123456789"_q, [] {
			const auto value = Crc16(QByteArray("123456789"));
			auto bytes = QByteArray();
			bytes.append(char((value >> 8) & 0xFF));
			bytes.append(char(value & 0xFF));
			return CompareHex(bytes, "31c3");
		} },
		{ u"cell_empty_golden"_q, [] {
			const auto cell = CellBuilder().finish();
			return CheckCellGolden(
				cell,
				"96a296d224f285c67bee93c30f8a309157f0daa35dc5b87e410b78630a09cfc7",
				"b5ee9c72010101010002000000",
				"b5ee9c724101010100020000004cacb9cd");
		} },
		{ u"cell_uint32_golden"_q, [] {
			const auto cell = CellBuilder().storeUint(123456789, 32).finish();
			return CheckCellGolden(
				cell,
				"91e353dfca30bc835a6181f062313547c1d8934735352307ef6bbefda309fb57",
				"b5ee9c72010101010006000008075bcd15",
				"b5ee9c72410101010006000008075bcd15516e5092");
		} },
		{ u"cell_uint34_golden"_q, [] {
			const auto cell = CellBuilder().storeUint(123456789, 34).finish();
			return CheckCellGolden(
				cell,
				"464fa7b7c92403237d4b5bf81f4cf015b1acd88370a4c1ef112bcf41bac8e9dd",
				"b5ee9c7201010101000700000901d6f34560",
				"b5ee9c7241010101000700000901d6f345606f5d59a1");
		} },
		{ u"cell_single_ref_golden"_q, [] {
			const auto ref = CellBuilder().storeUint(123456789, 32).finish();
			const auto cell = CellBuilder()
				.storeUint(987654321, 32)
				.storeRef(ref)
				.finish();
			return CheckCellGolden(
				cell,
				"82869061cb173b673f81ddeabcca379dc123ce96d4eeeacd43b84f0e8d2a0b57",
				"b5ee9c7201010201000d0001083ade68b1010008075bcd15",
				"b5ee9c7241010201000d0001083ade68b1010008075bcd15496ffbe7");
		} },
		{ u"cell_shared_refs_golden"_q, [] {
			const auto ref = CellBuilder().storeUint(123456789, 32).finish();
			const auto cell = CellBuilder()
				.storeUint(987654321, 32)
				.storeRef(ref)
				.storeRef(ref)
				.storeRef(ref)
				.finish();
			return CheckCellGolden(
				cell,
				"724b34c1b7ea15913dff26f4b16316406a23d1704e2e4522f9a5f9c6927a8e30",
				"b5ee9c7201010201000f0003083ade68b10101010008075bcd15",
				"b5ee9c7241010201000f0003083ade68b10101010008075bcd15a9403da9");
		} },
		{ u"cell_store_load_roundtrip"_q, [] {
			const auto address = SpecAddress();
			const auto text = QString(200, QChar(u'a'));
			const auto tail = CellBuilder().storeStringTail(text).finish();
			const auto cell = CellBuilder()
				.storeBit(true)
				.storeCoins(0)
				.storeCoins(1500000000)
				.storeAddress(std::nullopt)
				.storeAddress(address)
				.storeMaybeRef(tail)
				.finish();
			auto slice = cell.parse();
			if (!slice.loadBit()) {
				return u"loadBit: expected true"_q;
			} else if (slice.loadCoins() != 0) {
				return u"loadCoins: expected 0"_q;
			} else if (slice.loadCoins() != 1500000000LL) {
				return u"loadCoins: expected 1500000000"_q;
			}
			const auto none = slice.loadAddress();
			if (none || !slice.ok()) {
				return u"loadAddress: expected addr_none with ok()"_q;
			}
			const auto loaded = slice.loadAddress();
			if (!loaded || !(*loaded == address)) {
				return u"loadAddress: address mismatch"_q;
			}
			const auto ref = slice.loadMaybeRef();
			if (!ref) {
				return u"loadMaybeRef: expected a ref"_q;
			}
			auto tailSlice = ref->parse();
			if (tailSlice.loadStringTail() != text) {
				return u"loadStringTail: text mismatch"_q;
			} else if (slice.remainingBits() != 0
				|| slice.remainingRefs() != 0) {
				return u"remaining: expected fully consumed"_q;
			} else if (!slice.ok()) {
				return u"ok: slice reported failure"_q;
			}
			return QString();
		} },
		{ u"boc_wallet_roundtrip_0"_q, [] {
			return CheckRoundTrip(
				"B5EE9C72410101010044000084FF0020DDA4F260810200D71820D70B1FED44D0D31FD3FFD15112BAF2A122F901541044F910F2A2F80001D31F3120D74A96D307D402FB00DED1A4C8CB1FCBFFC9ED5441FDF089");
		} },
		{ u"boc_wallet_roundtrip_1"_q, [] {
			return CheckRoundTrip(
				"B5EE9C724101010100530000A2FF0020DD2082014C97BA9730ED44D0D70B1FE0A4F260810200D71820D70B1FED44D0D31FD3FFD15112BAF2A122F901541044F910F2A2F80001D31F3120D74A96D307D402FB00DED1A4C8CB1FCBFFC9ED54D0E2786F");
		} },
		{ u"boc_wallet_roundtrip_2"_q, [] {
			return CheckRoundTrip(
				"B5EE9C7241010101005F0000BAFF0020DD2082014C97BA218201339CBAB19C71B0ED44D0D31FD70BFFE304E0A4F260810200D71820D70B1FED44D0D31FD3FFD15112BAF2A122F901541044F910F2A2F80001D31F3120D74A96D307D402FB00DED1A4C8CB1FCBFFC9ED54B5B86E42");
		} },
		{ u"boc_wallet_roundtrip_3"_q, [] {
			return CheckRoundTrip(
				"B5EE9C724101010100570000AAFF0020DD2082014C97BA9730ED44D0D70B1FE0A4F2608308D71820D31FD31F01F823BBF263ED44D0D31FD3FFD15131BAF2A103F901541042F910F2A2F800029320D74A96D307D402FB00E8D1A4C8CB1FCBFFC9ED54A1370BB6");
		} },
		{ u"boc_wallet_roundtrip_4"_q, [] {
			return CheckRoundTrip(
				"B5EE9C724101010100630000C2FF0020DD2082014C97BA218201339CBAB19C71B0ED44D0D31FD70BFFE304E0A4F2608308D71820D31FD31F01F823BBF263ED44D0D31FD3FFD15131BAF2A103F901541042F910F2A2F800029320D74A96D307D402FB00E8D1A4C8CB1FCBFFC9ED54044CD7A1");
		} },
		{ u"boc_wallet_roundtrip_5"_q, [] {
			return CheckRoundTrip(
				"B5EE9C724101010100620000C0FF0020DD2082014C97BA9730ED44D0D70B1FE0A4F2608308D71820D31FD31FD31FF82313BBF263ED44D0D31FD31FD3FFD15132BAF2A15144BAF2A204F901541055F910F2A3F8009320D74A96D307D402FB00E8D101A4C8CB1FCB1FCBFFC9ED543FBE6EE0");
		} },
		{ u"boc_wallet_roundtrip_6"_q, [] {
			return CheckRoundTrip(
				"B5EE9C724101010100710000DEFF0020DD2082014C97BA218201339CBAB19F71B0ED44D0D31FD31F31D70BFFE304E0A4F2608308D71820D31FD31FD31FF82313BBF263ED44D0D31FD31FD3FFD15132BAF2A15144BAF2A204F901541055F910F2A3F8009320D74A96D307D402FB00E8D101A4C8CB1FCB1FCBFFC9ED5410BD6DAD");
		} },
		{ u"w5_code_parse_golden"_q, [] {
			const auto data = QByteArray::fromHex(kW5CodeHex);
			const auto cell = DeserializeBoc(data);
			if (!cell) {
				return u"deserialize failed"_q;
			}
			const auto hashFailure = CompareHex(cell->hash(), kW5CodeHash);
			if (!hashFailure.isEmpty()) {
				return u"hash: "_q + hashFailure;
			}
			const auto reparsed = DeserializeBoc(SerializeBoc(*cell));
			if (!reparsed || !(*reparsed == *cell)) {
				return u"reparse: reserialized cell differs"_q;
			}
			return QString();
		} },
		{ u"address_raw_parse_format"_q, [] {
			const auto parsed = ParseAddress(kSpecRaw);
			if (!parsed) {
				return u"parse failed"_q;
			} else if (parsed->friendly
				|| !parsed->bounceable
				|| parsed->testnet) {
				return u"flags mismatch"_q;
			} else if (parsed->address.workchain != 0) {
				return u"workchain mismatch"_q;
			}
			const auto hashFailure = CompareHex(
				parsed->address.hash,
				kSpecHashHex);
			if (!hashFailure.isEmpty()) {
				return u"hash: "_q + hashFailure;
			} else if (FormatRaw(parsed->address) != kSpecRaw) {
				return u"FormatRaw mismatch"_q;
			}
			return QString();
		} },
		{ u"address_friendly_parse_forms"_q, [] {
			const auto forms = std::vector<std::tuple<QString, bool, bool>>{
				{ kEQ, true, false },
				{ kUQ, false, false },
				{ kkQ, true, true },
				{ k0Q, false, true },
				{ u"EQAs9VlT6S776tq3unJcP5Ogsj+ELLunLXuOb1EKcOQi4wJB"_q,
					true, false },
				{ u"UQAs9VlT6S776tq3unJcP5Ogsj+ELLunLXuOb1EKcOQi41+E"_q,
					false, false },
				{ u"kQAs9VlT6S776tq3unJcP5Ogsj+ELLunLXuOb1EKcOQi47nL"_q,
					true, true },
				{ u"0QAs9VlT6S776tq3unJcP5Ogsj+ELLunLXuOb1EKcOQi4+QO"_q,
					false, true },
			};
			for (const auto &[text, bounceable, testnet] : forms) {
				const auto failure = CheckFriendlyForm(
					text,
					bounceable,
					testnet);
				if (!failure.isEmpty()) {
					return text + u": "_q + failure;
				}
			}
			return QString();
		} },
		{ u"address_friendly_format"_q, [] {
			const auto address = SpecAddress();
			if (FormatFriendly(address, true) != kEQ) {
				return u"bounceable mismatch"_q;
			} else if (FormatFriendly(address, true, true) != kkQ) {
				return u"bounceable testnet mismatch"_q;
			} else if (FormatFriendly(address, false) != kUQ) {
				return u"non-bounceable mismatch"_q;
			} else if (FormatFriendly(address, false, true) != k0Q) {
				return u"non-bounceable testnet mismatch"_q;
			}
			return QString();
		} },
		{ u"address_own_wallet_convention"_q, [] {
			const auto address = SpecAddress();
			const auto own = FormatFriendly(address, false);
			if (!own.startsWith(u"UQ"_q) || own != kUQ) {
				return u"own-wallet form mismatch"_q;
			}
			const auto counterparty = FormatFriendly(address, true);
			if (!counterparty.startsWith(u"EQ"_q) || counterparty != kEQ) {
				return u"counterparty form mismatch"_q;
			}
			return QString();
		} },
		{ u"address_masterchain_raw"_q, [] {
			const auto raw = QString(
				u"-1:3333333333333333333333333333333333333333333333333333333333333333"_q);
			const auto parsed = ParseAddress(raw);
			if (!parsed) {
				return u"parse failed"_q;
			} else if (parsed->address.workchain != -1) {
				return u"workchain mismatch"_q;
			} else if (parsed->address.hash != QByteArray(32, 0x33)) {
				return u"hash mismatch"_q;
			} else if (FormatRaw(parsed->address) != raw) {
				return u"FormatRaw mismatch"_q;
			}
			const auto friendly = FormatFriendly(parsed->address, true);
			const auto reparsed = ParseAddress(friendly);
			if (!reparsed || !(reparsed->address == parsed->address)) {
				return u"friendly round-trip mismatch"_q;
			}
			return QString();
		} },
		{ u"address_equality"_q, [] {
			const auto a = SpecAddress();
			auto b = SpecAddress();
			auto c = SpecAddress();
			c.workchain = -1;
			auto d = SpecAddress();
			d.hash = QByteArray::fromHex(
				"2cf55953e92efbeadab7ba725c3f93a0b23f842cbba72d7b8e6f510a70e422e5");
			if (!(a == b)) {
				return u"equal addresses differ"_q;
			} else if (a == c) {
				return u"different workchains compared equal"_q;
			} else if (a == d) {
				return u"different hashes compared equal"_q;
			}
			return QString();
		} },
		{ u"address_invalid_inputs"_q, [] {
			const auto inputs = std::vector<QString>{
				u"0:2cf55953e92efbeadab7ba725c3f93a0b23f842cbba72d7b8e6f510a70e422"_q,
				u"0:2cf55953e92efbeadab7ba725c3f93a0b23f842cbba72d7b8e6f510a70e422e"_q,
				u"EQAs9VlT6S776tq3unJcP5Ogsj-ELLunLXuOb1EKcOQi4wJ"_q,
				u"ton://EQAs9VlT6S776tq3unJcP5Ogsj-ELLunLXuOb1EKcOQi4wJB"_q,
				u"ton://transfer/EQDXDCFLXgiTrjGSNVBuvKPZVYlPn3J_u96xxLas3_yoRWRk"_q,
				u"0:EQDXDCFLXgiTrjGSNVBuvKPZVYlPn3J_u96xxLas3_yoRWRk"_q,
				u"!@#$%^&*AAAAAAAAAAAAAA AAAAAAAAAA AAAAAAAAAAAA A"_q,
				u"                                                "_q,
				u"EQBs9VlT6S776tq3unJcP5Ogsj-ELLunLXuOb1EKcOQi4wJB"_q,
			};
			for (const auto &text : inputs) {
				if (ParseAddress(text)) {
					return text + u": expected nullopt"_q;
				}
			}
			return QString();
		} },
		{ u"boc_negative_truncated"_q, [] {
			const auto wallet = QByteArray::fromHex(
				"B5EE9C72410101010044000084FF0020DDA4F260810200D71820D70B1FED44D0D31FD3FFD15112BAF2A122F901541044F910F2A2F80001D31F3120D74A96D307D402FB00DED1A4C8CB1FCBFFC9ED5441FDF089");
			for (const auto &boc : {
				wallet.left(wallet.size() - 1),
				wallet.left(3),
				wallet.left(8),
				QByteArray(),
			}) {
				const auto failure = CheckRejected(boc);
				if (!failure.isEmpty()) {
					return failure;
				}
			}
			return QString();
		} },
		{ u"boc_negative_wrong_magic"_q, [] {
			auto boc = QByteArray::fromHex(
				"B5EE9C72410101010044000084FF0020DDA4F260810200D71820D70B1FED44D0D31FD3FFD15112BAF2A122F901541044F910F2A2F80001D31F3120D74A96D307D402FB00DED1A4C8CB1FCBFFC9ED5441FDF089");
			boc[0] = char(0xC5);
			return CheckRejected(boc);
		} },
		{ u"boc_negative_wrong_crc"_q, [] {
			auto boc = QByteArray::fromHex(
				"B5EE9C72410101010044000084FF0020DDA4F260810200D71820D70B1FED44D0D31FD3FFD15112BAF2A122F901541044F910F2A2F80001D31F3120D74A96D307D402FB00DED1A4C8CB1FCBFFC9ED5441FDF089");
			const auto last = boc.size() - 1;
			boc[last] = char(uchar(boc[last]) ^ 1);
			return CheckRejected(boc);
		} },
		{ u"boc_negative_exotic_flag"_q, [] {
			const auto literal = CheckRejected(QByteArray::fromHex(
				"b5ee9c72010101010002000800"));
			if (!literal.isEmpty()) {
				return u"literal: "_q + literal;
			}
			auto boc = SerializeBoc(CellBuilder().finish(), false);
			boc[11] = char(uchar(boc[11]) | 0x08);
			const auto programmatic = CheckRejected(boc);
			if (!programmatic.isEmpty()) {
				return u"programmatic: "_q + programmatic;
			}
			return QString();
		} },
		{ u"boc_negative_malformed"_q, [] {
			for (const auto &hex : {
				QByteArray("b5ee9c72010101010002000500"),
				QByteArray("b5ee9c7201010201000d0001083ade68b1000008075bcd15"),
				QByteArray("b5ee9c7201010201000d0001083ade68b1020008075bcd15"),
				QByteArray("b5ee9c72010101010002000020"),
			}) {
				const auto failure = CheckRejected(QByteArray::fromHex(hex));
				if (!failure.isEmpty()) {
					return failure;
				}
			}
			return QString();
		} },
	};
}

} // namespace Gram::Tests
