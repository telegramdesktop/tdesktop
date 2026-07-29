/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/tests/gram_tests.h"

#include "gram/ton/gram_transfer_link.h"

#include <vector>

namespace Gram::Tests {
namespace {

const auto kAddress = u"EQAs9VlT6S776tq3unJcP5Ogsj-ELLunLXuOb1EKcOQi4wJB"_q;
const auto kAddressPlus = u"EQAs9VlT6S776tq3unJcP5Ogsj+ELLunLXuOb1EKcOQi4wJB"_q;
const auto kAddressNonBounceable = u"UQAs9VlT6S776tq3unJcP5Ogsj-ELLunLXuOb1EKcOQi41-E"_q;
const auto kAddressUnderscore = u"EQDXDCFLXgiTrjGSNVBuvKPZVYlPn3J_u96xxLas3_yoRWRk"_q;
const auto kAddressSlash = u"EQDXDCFLXgiTrjGSNVBuvKPZVYlPn3J/u96xxLas3/yoRWRk"_q;
const auto kAddressRaw = u"0:2cf55953e92efbeadab7ba725c3f93a0b23f842cbba72d7b8e6f510a70e422e3"_q;
const auto kComment = u"hello мир"_q;
const auto kFullLink = u"ton://transfer/"_q
	+ kAddress
	+ u"?amount=1500000000&text=hello%20%D0%BC%D0%B8%D1%80"_q;

[[nodiscard]] QString Link(const QString &tail) {
	return u"ton://transfer/"_q + tail;
}

[[nodiscard]] TransferLink Sample() {
	auto result = TransferLink();
	result.address = kAddress;
	result.amountNano = 1500000000;
	result.comment = kComment;
	return result;
}

[[nodiscard]] QString CheckLink(
		const std::optional<TransferLink> &got,
		const TransferLink &expected) {
	if (!got) {
		return u"expected a value"_q;
	} else if (got->address != expected.address) {
		return u"address: got \""_q
			+ got->address
			+ u"\", expected \""_q
			+ expected.address
			+ u"\""_q;
	} else if (got->amountNano != expected.amountNano) {
		return u"amount: got "_q
			+ QString::number(got->amountNano)
			+ u", expected "_q
			+ QString::number(expected.amountNano);
	} else if (got->comment != expected.comment) {
		return u"comment: got \""_q
			+ got->comment
			+ u"\", expected \""_q
			+ expected.comment
			+ u"\""_q;
	}
	return QString();
}

[[nodiscard]] QString CheckRejected(const QString &url) {
	return ParseTransferLink(url)
		? (u"expected nullopt for \""_q + url + u"\""_q)
		: QString();
}

[[nodiscard]] QString CheckFormat(
		const TransferLink &link,
		const QString &expected) {
	const auto formatted = FormatTransferLink(link);
	if (formatted != expected) {
		return u"format: got \""_q
			+ formatted
			+ u"\", expected \""_q
			+ expected
			+ u"\""_q;
	}
	const auto failure = CheckLink(ParseTransferLink(formatted), link);
	if (!failure.isEmpty()) {
		return u"reparse: "_q + failure;
	}
	return QString();
}

} // namespace

std::vector<Check> LinkChecks() {
	return {
		{ u"link_parse_full"_q, [] {
			return CheckLink(ParseTransferLink(kFullLink), Sample());
		} },
		{ u"link_format_round_trip"_q, [] {
			return CheckFormat(Sample(), kFullLink);
		} },
		{ u"link_format_round_trip_reserved"_q, [] {
			auto link = TransferLink();
			link.address = kAddress;
			link.comment = u"a&b=c?d#e%f+g h"_q;
			const auto expected = Link(kAddress)
				+ u"?text=a%26b%3Dc%3Fd%23e%25f%2Bg%20h"_q;
			return CheckFormat(link, expected);
		} },
		{ u"link_parse_no_query"_q, [] {
			auto expected = TransferLink();
			expected.address = kAddress;
			const auto bare = CheckLink(
				ParseTransferLink(Link(kAddress)),
				expected);
			if (!bare.isEmpty()) {
				return u"no query: "_q + bare;
			}
			const auto empty = CheckLink(
				ParseTransferLink(Link(kAddress + u"?"_q)),
				expected);
			if (!empty.isEmpty()) {
				return u"empty query: "_q + empty;
			}
			return QString();
		} },
		{ u"link_format_omits_empty"_q, [] {
			struct Case {
				int64 amountNano = 0;
				QString comment;
				QString expected;
			};
			const auto cases = std::vector<Case>{
				{ 0, QString(), Link(kAddress) },
				{ 7, QString(), Link(kAddress) + u"?amount=7"_q },
				{ 0, u"hi"_q, Link(kAddress) + u"?text=hi"_q },
				{ -1, QString(), Link(kAddress) },
			};
			for (const auto &entry : cases) {
				auto link = TransferLink();
				link.address = kAddress;
				link.amountNano = entry.amountNano;
				link.comment = entry.comment;
				const auto formatted = FormatTransferLink(link);
				if (formatted != entry.expected) {
					return u"amount "_q
						+ QString::number(entry.amountNano)
						+ u", comment \""_q
						+ entry.comment
						+ u"\": got \""_q
						+ formatted
						+ u"\", expected \""_q
						+ entry.expected
						+ u"\""_q;
				}
			}
			return QString();
		} },
		{ u"link_format_address_verbatim"_q, [] {
			const auto forms = std::vector<QString>{
				kAddressRaw,
				kAddressPlus,
				kAddressSlash,
				kAddressUnderscore,
			};
			for (const auto &entry : forms) {
				auto link = TransferLink();
				link.address = entry;
				link.amountNano = 1500000000;
				link.comment = kComment;
				const auto expected = Link(entry)
					+ u"?amount=1500000000&text=hello%20%D0%BC%D0%B8%D1%80"_q;
				const auto failure = CheckFormat(link, expected);
				if (!failure.isEmpty()) {
					return entry + u": "_q + failure;
				}
			}
			return QString();
		} },
		{ u"link_parse_address_forms"_q, [] {
			const auto forms = std::vector<QString>{
				kAddressNonBounceable,
				kAddressPlus,
				kAddressUnderscore,
				kAddressSlash,
				kAddressRaw,
			};
			for (const auto &entry : forms) {
				auto expected = TransferLink();
				expected.address = entry;
				const auto failure = CheckLink(
					ParseTransferLink(Link(entry)),
					expected);
				if (!failure.isEmpty()) {
					return entry + u": "_q + failure;
				}
			}
			return QString();
		} },
		{ u"link_parse_case_insensitive_prefix"_q, [] {
			const auto prefixes = std::vector<QString>{
				u"TON://TRANSFER/"_q,
				u"Ton://Transfer/"_q,
			};
			auto expected = TransferLink();
			expected.address = kAddress;
			expected.amountNano = 1;
			for (const auto &prefix : prefixes) {
				const auto url = prefix + kAddress + u"?amount=1"_q;
				const auto failure = CheckLink(
					ParseTransferLink(url),
					expected);
				if (!failure.isEmpty()) {
					return prefix + u": "_q + failure;
				}
			}
			return QString();
		} },
		{ u"link_parse_ignores_extra_parameters"_q, [] {
			const auto url = Link(kAddress
				+ u"?bin=AAA&amount=1500000000&init=x"_q
				+ u"&text=hi&amount=99&text=bye"_q);
			auto expected = TransferLink();
			expected.address = kAddress;
			expected.amountNano = 1500000000;
			expected.comment = u"hi"_q;
			return CheckLink(ParseTransferLink(url), expected);
		} },
		{ u"link_parse_amount_edges"_q, [] {
			struct Case {
				QString amount;
				int64 expected = 0;
			};
			const auto cases = std::vector<Case>{
				{ u""_q, 0 },
				{ u"0"_q, 0 },
				{ u"007"_q, 7 },
				{ u"9223372036854775807"_q, 9223372036854775807LL },
			};
			for (const auto &entry : cases) {
				auto expected = TransferLink();
				expected.address = kAddress;
				expected.amountNano = entry.expected;
				const auto url = Link(
					kAddress + u"?amount="_q + entry.amount);
				const auto failure = CheckLink(
					ParseTransferLink(url),
					expected);
				if (!failure.isEmpty()) {
					return u"amount \""_q
						+ entry.amount
						+ u"\": "_q
						+ failure;
				}
			}
			return QString();
		} },
		{ u"link_negative_wrong_scheme"_q, [] {
			const auto bad = std::vector<QString>{
				u"https://ton.org/transfer/"_q + kAddress + u"?amount=1"_q,
				u"http://ton.org/transfer/"_q + kAddress,
				u"tg://transfer/"_q + kAddress,
				u"ton:transfer/"_q + kAddress,
				u"ton://transfer2/"_q + kAddress,
				u"transfer/"_q + kAddress,
				kAddress,
				u" "_q + Link(kAddress),
			};
			for (const auto &url : bad) {
				const auto failure = CheckRejected(url);
				if (!failure.isEmpty()) {
					return failure;
				}
			}
			return QString();
		} },
		{ u"link_negative_bad_address"_q, [] {
			const auto bad = std::vector<QString>{
				Link(QString()),
				Link(u"?amount=1"_q),
				Link(u"notanaddress"_q),
				Link(u"EQAs9VlT6S776tq3unJcP5Ogsj-ELLunLXuOb1EKcOQi4wJ"_q),
				Link(u"EQBs9VlT6S776tq3unJcP5Ogsj-ELLunLXuOb1EKcOQi4wJB"_q),
				Link(kAddress + u"/"_q),
				Link(kAddress + u" "_q),
				Link(kAddress + u"#frag"_q),
				Link(u"EQAs9VlT6S776tq3unJcP5Ogsj%2BELLunLXuOb1EKcOQi4wJB"_q),
				Link(u"0:2cf5"_q),
			};
			for (const auto &url : bad) {
				const auto failure = CheckRejected(url);
				if (!failure.isEmpty()) {
					return failure;
				}
			}
			return QString();
		} },
		{ u"link_negative_amount_not_numeric"_q, [] {
			const auto tails = std::vector<QString>{
				u"?amount=abc"_q,
				u"?amount=1.5"_q,
				u"?amount=1e9"_q,
				u"?amount=0x10"_q,
				u"?amount=+1"_q,
				u"?amount=%201"_q,
				u"?amount=1%20"_q,
				u"?amount=1#frag"_q,
			};
			for (const auto &tail : tails) {
				const auto failure = CheckRejected(Link(kAddress + tail));
				if (!failure.isEmpty()) {
					return failure;
				}
			}
			return QString();
		} },
		{ u"link_negative_amount_negative"_q, [] {
			const auto tails = std::vector<QString>{
				u"?amount=-1"_q,
				u"?amount=-1500000000"_q,
				u"?amount=-9223372036854775808"_q,
			};
			for (const auto &tail : tails) {
				const auto failure = CheckRejected(Link(kAddress + tail));
				if (!failure.isEmpty()) {
					return failure;
				}
			}
			return QString();
		} },
		{ u"link_negative_amount_out_of_range"_q, [] {
			const auto tails = std::vector<QString>{
				u"?amount=9223372036854775808"_q,
				u"?amount=18446744073709551616"_q,
				u"?amount=99999999999999999999999"_q,
			};
			for (const auto &tail : tails) {
				const auto failure = CheckRejected(Link(kAddress + tail));
				if (!failure.isEmpty()) {
					return failure;
				}
			}
			return QString();
		} },
		{ u"link_negative_empty_string"_q, [] {
			const auto bad = std::vector<QString>{
				QString(),
				u""_q,
				u"ton"_q,
				u"ton://"_q,
				u"ton://transfer"_q,
			};
			for (const auto &url : bad) {
				const auto failure = CheckRejected(url);
				if (!failure.isEmpty()) {
					return failure;
				}
			}
			return QString();
		} },
	};
}

} // namespace Gram::Tests
