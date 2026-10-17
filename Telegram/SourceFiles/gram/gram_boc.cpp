/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/gram_boc.h"

#include "base/random.h"

#include <QtCore/QByteArray>

namespace Gram {
namespace {

constexpr auto kBocMagic = quint32(0xB5EE9C72);
constexpr auto kSignatureBytes = 64;
// The signed rotation request: the signature, then the opcode, the wallet id,
// the expiration and the sequence number, then the new public key, with that
// key's ownership proof in the single reference.
constexpr auto kRequestBytes = kSignatureBytes + 4 + 4 + 4 + 4 + 32;
constexpr auto kRequestRefs = 1;
constexpr auto kExternalOpcode = quint32(0xFBBA99C8);
constexpr auto kInternalOpcode = quint32(0xFBBA99C7);
constexpr auto kMaxCells = 64;
constexpr auto kMaxBytes = 64 * 1024;

struct Cell {
	int dataFrom = 0;
	int dataSize = 0;
	int refsFrom = 0;
	int refs = 0;
	bool wholeBytes = false;
};

struct ParsedBoc {
	QByteArray data;
	std::vector<Cell> cells;
	int refSize = 0;
	int root = 0;
	bool hasCrc = false;
};

[[nodiscard]] std::optional<quint64> ReadNumber(
		const QByteArray &data,
		int &offset,
		int size) {
	if (size < 1 || size > 8 || offset < 0 || data.size() - offset < size) {
		return {};
	}
	auto result = quint64(0);
	for (auto i = 0; i != size; ++i) {
		result = (result << 8) | uchar(data[offset + i]);
	}
	offset += size;
	return result;
}

[[nodiscard]] quint32 ReadOpcode(const QByteArray &data, int offset) {
	auto result = quint32(0);
	for (auto i = 0; i != 4; ++i) {
		result = (result << 8) | uchar(data[offset + i]);
	}
	return result;
}

// Every cell is two descriptor bytes, its data, then one index per reference.
// The first descriptor counts the references and marks an exotic cell, the
// second gives the data length and whether the last byte carries a completion
// tag instead of eight data bits.
[[nodiscard]] std::optional<std::vector<Cell>> ParseCells(
		const QByteArray &data,
		int from,
		int till,
		int count,
		int refSize) {
	auto result = std::vector<Cell>();
	result.reserve(count);
	auto offset = from;
	for (auto i = 0; i != count; ++i) {
		if (till - offset < 2) {
			return {};
		}
		const auto first = uchar(data[offset]);
		const auto second = uchar(data[offset + 1]);
		offset += 2;
		const auto refs = int(first & 0x07);
		const auto dataSize = int(second >> 1) + int(second & 0x01);
		if ((first & 0xF8) != 0 || refs > 4 || till - offset < dataSize) {
			return {};
		}
		result.push_back({
			.dataFrom = offset,
			.dataSize = dataSize,
			.refsFrom = offset + dataSize,
			.refs = refs,
			.wholeBytes = ((second & 0x01) == 0),
		});
		offset += dataSize;
		if (till - offset < refs * refSize) {
			return {};
		}
		offset += refs * refSize;
	}
	return (offset == till)
		? std::make_optional(std::move(result))
		: std::nullopt;
}

// The one cell shaped like a signed rotation request. A message carrying any
// other number of them is not the message this was asked to disarm.
[[nodiscard]] int FindRequest(
		const QByteArray &data,
		const std::vector<Cell> &cells) {
	auto result = -1;
	for (auto i = 0, count = int(cells.size()); i != count; ++i) {
		const auto &cell = cells[i];
		if (!cell.wholeBytes
			|| cell.refs != kRequestRefs
			|| cell.dataSize != kRequestBytes) {
			continue;
		}
		const auto opcode = ReadOpcode(data, cell.dataFrom + kSignatureBytes);
		if (opcode != kExternalOpcode && opcode != kInternalOpcode) {
			continue;
		} else if (result >= 0) {
			return -1;
		}
		result = i;
	}
	return result;
}

[[nodiscard]] quint32 Crc32c(const QByteArray &data, int size) {
	auto result = quint32(0xFFFFFFFFU);
	for (auto i = 0; i != size; ++i) {
		result ^= uchar(data[i]);
		for (auto bit = 0; bit != 8; ++bit) {
			result = (result & 1)
				? ((result >> 1) ^ 0x82F63B78U)
				: (result >> 1);
		}
	}
	return ~result;
}

[[nodiscard]] std::optional<ParsedBoc> ParseBoc(const QString &bocBase64) {
	auto data = QByteArray::fromBase64(
		bocBase64.toLatin1(),
		QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
	if (data.isEmpty() || data.size() > kMaxBytes) {
		return {};
	}
	auto offset = 0;
	const auto magic = ReadNumber(data, offset, 4);
	const auto flags = ReadNumber(data, offset, 1);
	if (!magic || *magic != kBocMagic || !flags) {
		return {};
	}
	const auto hasIndex = ((*flags & 0x80) != 0);
	const auto hasCrc = ((*flags & 0x40) != 0);
	const auto hasCacheBits = ((*flags & 0x20) != 0);
	const auto refSize = int(*flags & 0x07);
	if (hasCacheBits || refSize < 1 || refSize > 4) {
		return {};
	}
	const auto offsetSize = ReadNumber(data, offset, 1);
	if (!offsetSize || *offsetSize < 1 || *offsetSize > 8) {
		return {};
	}
	const auto cells = ReadNumber(data, offset, refSize);
	const auto roots = ReadNumber(data, offset, refSize);
	const auto absent = ReadNumber(data, offset, refSize);
	const auto size = ReadNumber(data, offset, int(*offsetSize));
	if (!cells
		|| !roots
		|| !absent
		|| !size
		|| *absent != 0
		|| *roots != 1
		|| *cells < 1
		|| *cells > kMaxCells
		|| *size > quint64(kMaxBytes)) {
		return {};
	}
	const auto root = ReadNumber(data, offset, refSize);
	if (!root) {
		return {};
	}
	const auto listed = hasIndex ? (int(*cells) * int(*offsetSize)) : 0;
	if (data.size() - offset < listed) {
		return {};
	}
	offset += listed;
	const auto tail = hasCrc ? 4 : 0;
	if (data.size() - offset != int(*size) + tail) {
		return {};
	}
	auto parsed = ParseCells(
		data,
		offset,
		offset + int(*size),
		int(*cells),
		refSize);
	if (!parsed) {
		return {};
	}
	return ParsedBoc{
		.data = std::move(data),
		.cells = std::move(*parsed),
		.refSize = refSize,
		.root = int(*root),
		.hasCrc = hasCrc,
	};
}

} // namespace

QString BreakRotationSignature(const QString &bocBase64) {
	auto parsed = ParseBoc(bocBase64);
	if (!parsed) {
		return QString();
	}
	auto &data = parsed->data;
	const auto index = FindRequest(data, parsed->cells);
	if (index < 0) {
		return QString();
	}
	base::RandomFill(
		data.data() + parsed->cells[index].dataFrom,
		kSignatureBytes);
	if (parsed->hasCrc) {
		const auto till = int(data.size()) - 4;
		const auto crc = Crc32c(data, till);
		for (auto i = 0; i != 4; ++i) {
			data[till + i] = char(uchar((crc >> (8 * i)) & 0xFF));
		}
	}
	return QString::fromLatin1(data.toBase64());
}

std::optional<QString> TextCommentFromBoc(const QString &bocBase64) {
	const auto parsed = ParseBoc(bocBase64);
	if (!parsed) {
		return std::nullopt;
	}
	const auto &data = parsed->data;
	const auto &cells = parsed->cells;
	const auto count = int(cells.size());
	auto visited = std::vector<bool>(count, false);
	auto bytes = QByteArray();
	auto index = parsed->root;
	for (auto step = 0; step != kMaxCells; ++step) {
		if (index < 0 || index >= count || visited[index]) {
			return std::nullopt;
		}
		visited[index] = true;
		const auto &cell = cells[index];
		if (!cell.wholeBytes || cell.refs > 1) {
			return std::nullopt;
		}
		auto from = cell.dataFrom;
		auto size = cell.dataSize;
		if (!step) {
			if (size < 4 || ReadOpcode(data, from) != 0) {
				return std::nullopt;
			}
			from += 4;
			size -= 4;
		}
		bytes.append(data.constData() + from, size);
		if (!cell.refs) {
			const auto text = QString::fromUtf8(bytes);
			return (text.toUtf8() != bytes)
				? std::nullopt
				: std::make_optional(text);
		}
		auto offset = cell.refsFrom;
		const auto next = ReadNumber(data, offset, parsed->refSize);
		if (!next || *next >= quint64(count)) {
			return std::nullopt;
		}
		index = int(*next);
	}
	return std::nullopt;
}

} // namespace Gram
