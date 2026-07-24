/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/ton/gram_boc.h"

#include "gram/ton/gram_crc.h"

namespace Gram {
namespace {

constexpr auto kMagic = quint32(0xB5EE9C72);
constexpr auto kFlagHasIdx = 0x80;
constexpr auto kFlagHasCrc = 0x40;
constexpr auto kFlagReserved = 0x18;
constexpr auto kSizeMask = 0x07;
constexpr auto kMaxCellBits = 1023;
constexpr auto kMaxCellRefs = 4;
constexpr auto kMaxDepth = 0xFFFF;

[[nodiscard]] int BitLength(quint64 value) {
	auto result = 0;
	while (value) {
		++result;
		value >>= 1;
	}
	return result;
}

[[nodiscard]] int SizeBytes(quint64 value) {
	const auto bytes = (BitLength(value) + 7) / 8;
	return (bytes < 1) ? 1 : bytes;
}

[[nodiscard]] int PopCount3(int mask) {
	return (mask & 1) + ((mask >> 1) & 1) + ((mask >> 2) & 1);
}

void AppendUint(QByteArray &to, quint64 value, int bytes) {
	for (auto i = bytes - 1; i >= 0; --i) {
		to.append(char((value >> (8 * i)) & 0xFF));
	}
}

[[nodiscard]] QByteArray PadData(const QByteArray &data, int bits) {
	auto result = data;
	if (bits % 8 != 0) {
		const auto index = bits / 8;
		result[index] = char(uchar(result[index]) | (1 << (7 - bits % 8)));
	}
	return result;
}

class Reader final {
public:
	Reader(const char *data, int size);

	[[nodiscard]] bool failed() const;
	[[nodiscard]] int position() const;
	[[nodiscard]] int remaining() const;

	quint64 readUint(int bytes);
	QByteArray readBytes(int count);
	void skip(quint64 count);

private:
	const char *_data = nullptr;
	int _size = 0;
	int _position = 0;
	bool _failed = false;

};

struct SortedCell {
	Cell cell;
	std::vector<int> refs;
};

Reader::Reader(const char *data, int size) : _data(data), _size(size) {
}

bool Reader::failed() const {
	return _failed;
}

int Reader::position() const {
	return _position;
}

int Reader::remaining() const {
	return _size - _position;
}

quint64 Reader::readUint(int bytes) {
	if (_failed || bytes > remaining()) {
		_failed = true;
		return 0;
	}
	auto result = quint64(0);
	for (auto i = 0; i != bytes; ++i) {
		result = (result << 8) | quint64(uchar(_data[_position++]));
	}
	return result;
}

QByteArray Reader::readBytes(int count) {
	if (_failed || count > remaining()) {
		_failed = true;
		return QByteArray();
	}
	auto result = QByteArray(_data + _position, count);
	_position += count;
	return result;
}

void Reader::skip(quint64 count) {
	if (_failed || count > quint64(remaining())) {
		_failed = true;
		return;
	}
	_position += int(count);
}

std::vector<SortedCell> SortCells(const Cell &root) {
	auto cells = std::vector<Cell>();
	auto children = std::vector<std::vector<int>>();
	auto slotByHash = base::flat_map<QByteArray, int>();
	const auto enqueue = [&](const Cell &cell) {
		const auto i = slotByHash.find(cell.hash());
		if (i != slotByHash.end()) {
			return i->second;
		}
		const auto slot = int(cells.size());
		slotByHash.emplace(cell.hash(), slot);
		cells.push_back(cell);
		children.emplace_back();
		return slot;
	};
	enqueue(root);
	for (auto i = 0; i != int(cells.size()); ++i) {
		const auto cell = cells[i];
		const auto count = cell.refsCount();
		auto refs = std::vector<int>();
		refs.reserve(count);
		for (auto r = 0; r != count; ++r) {
			refs.push_back(enqueue(cell.ref(r)));
		}
		children[i] = std::move(refs);
	}

	// Upstream topological order (ton-core topologicalSort.ts): the root
	// comes first, every parent is emitted before all of its children, and
	// a shared child is placed after ALL cells that reference it. This is a
	// post-order depth-first walk from the root visiting refs in REVERSED
	// order, appending each finished cell to `sorted`; the final indices are
	// the reverse of that walk, so the root receives index 0.
	auto sorted = std::vector<int>();
	auto permanent = std::vector<char>(cells.size(), 0);
	struct Frame {
		int slot = 0;
		int next = 0;
	};
	auto stack = std::vector<Frame>();
	stack.push_back({ 0, int(children[0].size()) - 1 });
	while (!stack.empty()) {
		auto &frame = stack.back();
		if (frame.next >= 0) {
			const auto child = children[frame.slot][frame.next];
			--frame.next;
			if (!permanent[child]) {
				stack.push_back({ child, int(children[child].size()) - 1 });
			}
		} else {
			sorted.push_back(frame.slot);
			permanent[frame.slot] = 1;
			stack.pop_back();
		}
	}

	const auto n = int(sorted.size());
	auto finalIndex = std::vector<int>(n, 0);
	for (auto j = 0; j != n; ++j) {
		finalIndex[sorted[j]] = n - 1 - j;
	}
	auto result = std::vector<SortedCell>(n);
	for (auto j = 0; j != n; ++j) {
		const auto slot = sorted[j];
		const auto index = n - 1 - j;
		result[index].cell = cells[slot];
		result[index].refs.reserve(children[slot].size());
		for (const auto childSlot : children[slot]) {
			result[index].refs.push_back(finalIndex[childSlot]);
		}
	}
	return result;
}

} // namespace

QByteArray SerializeBoc(const Cell &root, bool withCrc) {
	const auto ordered = SortCells(root);
	const auto cellsNum = int(ordered.size());
	const auto sizeBytes = SizeBytes(quint64(cellsNum));
	auto totalCellSize = quint64(0);
	for (const auto &entry : ordered) {
		totalCellSize += 2
			+ quint64(entry.cell.data().size())
			+ quint64(entry.refs.size()) * sizeBytes;
	}
	const auto offsetBytes = SizeBytes(totalCellSize);

	auto result = QByteArray();
	AppendUint(result, kMagic, 4);
	result.append(char((withCrc ? kFlagHasCrc : 0) | sizeBytes));
	result.append(char(offsetBytes));
	AppendUint(result, quint64(cellsNum), sizeBytes);
	AppendUint(result, 1, sizeBytes);
	AppendUint(result, 0, sizeBytes);
	AppendUint(result, totalCellSize, offsetBytes);
	AppendUint(result, 0, sizeBytes);
	for (const auto &entry : ordered) {
		const auto &cell = entry.cell;
		const auto bits = cell.bitsCount();
		result.append(char(entry.refs.size()));
		result.append(char(bits / 8 + (bits + 7) / 8));
		result.append(PadData(cell.data(), bits));
		for (const auto index : entry.refs) {
			AppendUint(result, quint64(index), sizeBytes);
		}
	}
	if (withCrc) {
		const auto crc = Crc32C(result);
		for (auto i = 0; i != 4; ++i) {
			result.append(char((crc >> (8 * i)) & 0xFF));
		}
	}
	return result;
}

std::optional<Cell> DeserializeBoc(const QByteArray &data) {
	auto reader = Reader(data.constData(), int(data.size()));
	if (reader.readUint(4) != kMagic) {
		return std::nullopt;
	}
	const auto flags = reader.readUint(1);
	if (reader.failed() || (flags & kFlagReserved)) {
		return std::nullopt;
	}
	const auto hasIdx = (flags & kFlagHasIdx) != 0;
	const auto hasCrc = (flags & kFlagHasCrc) != 0;
	const auto sizeBytes = int(flags & kSizeMask);
	if (sizeBytes < 1 || sizeBytes > 4) {
		return std::nullopt;
	}
	const auto offBytes = int(reader.readUint(1));
	if (reader.failed() || offBytes < 1 || offBytes > 8) {
		return std::nullopt;
	}
	const auto cells = reader.readUint(sizeBytes);
	const auto roots = reader.readUint(sizeBytes);
	const auto absent = reader.readUint(sizeBytes);
	const auto totalCellSize = reader.readUint(offBytes);
	if (reader.failed()) {
		return std::nullopt;
	} else if (!cells || roots != 1 || absent != 0) {
		return std::nullopt;
	} else if (cells * 2 > totalCellSize) {
		return std::nullopt;
	}
	const auto rootIndex = reader.readUint(sizeBytes);
	if (reader.failed() || rootIndex >= cells) {
		return std::nullopt;
	}
	if (hasIdx) {
		reader.skip(cells * quint64(offBytes));
		if (reader.failed()) {
			return std::nullopt;
		}
	}
	if (totalCellSize > quint64(reader.remaining())) {
		return std::nullopt;
	}
	const auto cellDataStart = reader.position();
	auto cellReader = Reader(data.constData() + cellDataStart, int(totalCellSize));
	reader.skip(totalCellSize);

	struct Record {
		QByteArray data;
		int bits = 0;
		std::vector<int> refs;
	};
	const auto count = int(cells);
	auto records = std::vector<Record>(count);
	for (auto i = 0; i != count; ++i) {
		const auto d1 = cellReader.readUint(1);
		if (cellReader.failed()) {
			return std::nullopt;
		}
		const auto refsCount = int(d1 & 7);
		if (refsCount > kMaxCellRefs || (d1 & 8)) {
			return std::nullopt;
		}
		const auto hasHashes = (d1 & 16) != 0;
		const auto levelMask = int(d1 >> 5);
		const auto d2 = cellReader.readUint(1);
		if (cellReader.failed()) {
			return std::nullopt;
		}
		if (hasHashes) {
			const auto hashes = PopCount3(levelMask & 7) + 1;
			cellReader.skip(quint64(hashes) * 32);
			cellReader.skip(quint64(hashes) * 2);
			if (cellReader.failed()) {
				return std::nullopt;
			}
		}
		const auto dataBytes = int((d2 + 1) / 2);
		auto bytes = cellReader.readBytes(dataBytes);
		if (cellReader.failed()) {
			return std::nullopt;
		}
		auto bits = 0;
		if (d2 & 1) {
			const auto last = uchar(bytes[dataBytes - 1]);
			if (!last) {
				return std::nullopt;
			}
			auto zeros = 0;
			while (!((last >> zeros) & 1)) {
				++zeros;
			}
			bits = dataBytes * 8 - zeros - 1;
		} else {
			bits = dataBytes * 8;
		}
		if (bits > kMaxCellBits) {
			return std::nullopt;
		}
		bytes.resize((bits + 7) / 8);
		if (bits % 8 != 0) {
			const auto index = bits / 8;
			const auto keep = bits % 8;
			bytes[index] = char(uchar(bytes[index]) & (0xFF << (8 - keep)));
		}
		auto refs = std::vector<int>();
		refs.reserve(refsCount);
		for (auto r = 0; r != refsCount; ++r) {
			const auto index = cellReader.readUint(sizeBytes);
			if (cellReader.failed()) {
				return std::nullopt;
			} else if (index <= quint64(i) || index >= cells) {
				return std::nullopt;
			}
			refs.push_back(int(index));
		}
		records[i].data = std::move(bytes);
		records[i].bits = bits;
		records[i].refs = std::move(refs);
	}
	if (cellReader.remaining() != 0) {
		return std::nullopt;
	}
	if (hasCrc) {
		if (reader.remaining() != 4) {
			return std::nullopt;
		}
		const auto covered = reader.position();
		const auto expected = Crc32C(data.left(covered));
		auto actual = quint32(0);
		for (auto i = 0; i != 4; ++i) {
			actual |= quint32(uchar(data[covered + i])) << (8 * i);
		}
		if (actual != expected) {
			return std::nullopt;
		}
	} else if (reader.remaining() != 0) {
		return std::nullopt;
	}

	auto built = std::vector<Cell>(count);
	for (auto i = count - 1; i >= 0; --i) {
		auto refs = std::vector<Cell>();
		refs.reserve(records[i].refs.size());
		auto maxDepth = 0;
		for (const auto r : records[i].refs) {
			const auto &child = built[r];
			const auto depth = int(child.depth());
			if (depth > maxDepth) {
				maxDepth = depth;
			}
			refs.push_back(child);
		}
		if (!refs.empty() && (1 + maxDepth) > kMaxDepth) {
			return std::nullopt;
		}
		built[i] = Cell(records[i].data, records[i].bits, std::move(refs));
	}
	return built[int(rootIndex)];
}

} // namespace Gram
