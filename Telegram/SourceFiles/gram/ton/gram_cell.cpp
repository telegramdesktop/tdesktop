/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/ton/gram_cell.h"

#include "base/openssl_help.h"

namespace Gram {
namespace {

constexpr auto kMaxBits = 1023;
constexpr auto kMaxRefs = 4;
constexpr auto kHashSize = 32;

[[nodiscard]] int BitLength(quint64 value) {
	auto result = 0;
	while (value) {
		++result;
		value >>= 1;
	}
	return result;
}

[[nodiscard]] QByteArray PackPadded(const QByteArray &data, int bits) {
	auto result = data;
	if (bits % 8 != 0) {
		const auto index = bits / 8;
		result[index] = result[index] | char(1 << (7 - bits % 8));
	}
	return result;
}

void WriteSnake(const QByteArray &bytes, CellBuilder &builder) {
	if (bytes.isEmpty()) {
		return;
	}
	const auto available = builder.availableBits() / 8;
	if (int(bytes.size()) > available) {
		builder.storeBytes(bytes.left(available));
		auto tail = CellBuilder();
		WriteSnake(bytes.mid(available), tail);
		builder.storeRef(tail.finish());
	} else {
		builder.storeBytes(bytes);
	}
}

} // namespace

struct Cell::Data {
	~Data();

	QByteArray data;
	int bits = 0;
	std::vector<Cell> refs;
	QByteArray hash;
	quint16 depth = 0;

};

Cell::Data::~Data() {
	auto stack = std::move(refs);
	while (!stack.empty()) {
		auto cell = std::move(stack.back());
		stack.pop_back();
		if (cell._data.use_count() != 1) {
			continue;
		}
		auto &nested = const_cast<Data*>(cell._data.get())->refs;
		for (auto &ref : nested) {
			stack.push_back(std::move(ref));
		}
		nested.clear();
	}
}

Cell::Cell() {
	static const auto empty = [] {
		auto payload = std::make_shared<Data>();
		const auto digest = openssl::Sha256(
			bytes::make_span(QByteArray(2, 0)));
		payload->hash = QByteArray(
			reinterpret_cast<const char*>(digest.data()),
			int(digest.size()));
		return payload;
	}();
	_data = empty;
}

Cell::Cell(QByteArray data, int bits, std::vector<Cell> refs) {
	Expects(bits >= 0 && bits <= kMaxBits);
	Expects(refs.size() <= kMaxRefs);
	Expects(data.size() == (bits + 7) / 8);

	// The representation hash is SHA256 over d1, d2, the padded data and
	// then two SEPARATE blocks: first every ref depth as a 16-bit big
	// endian value, and only after all depths every ref's 32-byte hash.
	// Interleaving depth and hash per ref would still look correct for
	// cells with zero or one ref but produce wrong hashes for two or more.
	auto repr = QByteArray();
	repr.append(char(refs.size()));
	repr.append(char(bits / 8 + (bits + 7) / 8));
	repr.append(PackPadded(data, bits));
	for (const auto &ref : refs) {
		const auto childDepth = ref.depth();
		repr.append(char((childDepth >> 8) & 0xFF));
		repr.append(char(childDepth & 0xFF));
	}
	for (const auto &ref : refs) {
		repr.append(ref.hash());
	}

	auto depth = 0;
	for (const auto &ref : refs) {
		const auto candidate = int(ref.depth()) + 1;
		if (candidate > depth) {
			depth = candidate;
		}
	}
	Expects(depth <= 0xFFFF);

	const auto digest = openssl::Sha256(bytes::make_span(repr));
	auto payload = std::make_shared<Data>();
	payload->data = std::move(data);
	payload->bits = bits;
	payload->refs = std::move(refs);
	payload->hash = QByteArray(
		reinterpret_cast<const char*>(digest.data()),
		int(digest.size()));
	payload->depth = quint16(depth);
	_data = std::move(payload);
}

const QByteArray &Cell::hash() const {
	return _data->hash;
}

const QByteArray &Cell::data() const {
	return _data->data;
}

int Cell::bitsCount() const {
	return _data->bits;
}

int Cell::refsCount() const {
	return int(_data->refs.size());
}

const Cell &Cell::ref(int index) const {
	Expects(index >= 0 && index < int(_data->refs.size()));

	return _data->refs[index];
}

quint16 Cell::depth() const {
	return _data->depth;
}

CellSlice Cell::parse() const {
	return CellSlice(*this);
}

CellBuilder &CellBuilder::storeBit(bool value) {
	Expects(_bits + 1 <= kMaxBits);

	putBit(value);
	return *this;
}

CellBuilder &CellBuilder::storeUint(quint64 value, int bits) {
	Expects(bits >= 0 && bits <= 64);
	Expects(bits == 64 || value < (quint64(1) << bits));
	Expects(_bits + bits <= kMaxBits);

	putUint(value, bits);
	return *this;
}

CellBuilder &CellBuilder::storeBytes(const QByteArray &data) {
	Expects(_bits + int(data.size()) * 8 <= kMaxBits);

	for (const auto byte : data) {
		putUint(uchar(byte), 8);
	}
	return *this;
}

CellBuilder &CellBuilder::storeCoins(qint64 nano) {
	Expects(nano >= 0);

	if (!nano) {
		return storeUint(0, 4);
	}
	const auto sizeBytes = (BitLength(quint64(nano)) + 7) / 8;
	storeUint(quint64(sizeBytes), 4);
	storeUint(quint64(nano), sizeBytes * 8);
	return *this;
}

CellBuilder &CellBuilder::storeAddress(const std::optional<Address> &address) {
	if (!address) {
		return storeUint(0, 2);
	}
	const auto &value = *address;
	Expects(value.workchain >= -128 && value.workchain <= 127);
	Expects(value.hash.size() == kHashSize);

	storeUint(2, 2);
	storeBit(false);
	storeUint(quint8(value.workchain), 8);
	storeBytes(value.hash);
	return *this;
}

CellBuilder &CellBuilder::storeRef(Cell cell) {
	Expects(int(_refs.size()) < kMaxRefs);

	_refs.push_back(std::move(cell));
	return *this;
}

CellBuilder &CellBuilder::storeSlice(const CellSlice &slice) {
	auto copy = slice;
	const auto bits = copy.remainingBits();
	const auto refs = copy.remainingRefs();
	Expects(_bits + bits <= kMaxBits);
	Expects(int(_refs.size()) + refs <= kMaxRefs);

	for (auto i = 0; i != bits; ++i) {
		putBit(copy.loadBit());
	}
	for (auto i = 0; i != refs; ++i) {
		storeRef(copy.loadRef());
	}
	return *this;
}

CellBuilder &CellBuilder::storeStringTail(const QString &text) {
	WriteSnake(text.toUtf8(), *this);
	return *this;
}

CellBuilder &CellBuilder::storeMaybeRef(const std::optional<Cell> &cell) {
	if (cell) {
		storeBit(true);
		storeRef(*cell);
	} else {
		storeBit(false);
	}
	return *this;
}

int CellBuilder::availableBits() const {
	return kMaxBits - _bits;
}

int CellBuilder::availableRefs() const {
	return kMaxRefs - int(_refs.size());
}

Cell CellBuilder::finish() {
	auto result = Cell(
		_data.left((_bits + 7) / 8),
		_bits,
		std::move(_refs));
	_data = QByteArray(128, 0);
	_bits = 0;
	_refs.clear();
	return result;
}

void CellBuilder::putBit(bool value) {
	if (value) {
		const auto index = _bits / 8;
		_data[index] = _data[index] | char(1 << (7 - (_bits % 8)));
	}
	++_bits;
}

void CellBuilder::putUint(quint64 value, int bits) {
	for (auto i = bits - 1; i >= 0; --i) {
		putBit((value >> i) & 1);
	}
}

CellSlice::CellSlice(Cell cell) : _cell(std::move(cell)) {
}

bool CellSlice::ok() const {
	return !_failed;
}

int CellSlice::remainingBits() const {
	return _cell.bitsCount() - _bitOffset;
}

int CellSlice::remainingRefs() const {
	return _cell.refsCount() - _refOffset;
}

bool CellSlice::check(int bits, int refs) {
	if (_failed || bits > remainingBits() || refs > remainingRefs()) {
		_failed = true;
		return false;
	}
	return true;
}

bool CellSlice::loadBit() {
	if (!check(1)) {
		return false;
	}
	const auto &data = _cell.data();
	const auto bit = (uchar(data[_bitOffset / 8])
		>> (7 - (_bitOffset % 8))) & 1;
	++_bitOffset;
	return bit != 0;
}

quint64 CellSlice::loadUint(int bits) {
	Expects(bits >= 0 && bits <= 64);

	if (!check(bits)) {
		return 0;
	}
	const auto &data = _cell.data();
	auto result = quint64(0);
	for (auto i = 0; i != bits; ++i) {
		const auto bit = (uchar(data[_bitOffset / 8])
			>> (7 - (_bitOffset % 8))) & 1;
		result = (result << 1) | quint64(bit);
		++_bitOffset;
	}
	return result;
}

QByteArray CellSlice::loadBytes(int count) {
	Expects(count >= 0);

	if (!check(count * 8)) {
		return QByteArray();
	}
	auto result = QByteArray(count, 0);
	for (auto i = 0; i != count; ++i) {
		result[i] = char(loadUint(8));
	}
	return result;
}

qint64 CellSlice::loadCoins() {
	const auto size = int(loadUint(4));
	if (size > 8) {
		_failed = true;
	}
	if (_failed) {
		return 0;
	} else if (!size) {
		return 0;
	}
	const auto value = loadUint(size * 8);
	if (_failed) {
		return 0;
	} else if (size == 8 && (value >> 63)) {
		_failed = true;
		return 0;
	}
	return qint64(value);
}

std::optional<Address> CellSlice::loadAddress() {
	const auto tag = loadUint(2);
	if (_failed) {
		return std::nullopt;
	} else if (!tag) {
		return std::nullopt;
	} else if (tag != 2) {
		_failed = true;
		return std::nullopt;
	}
	const auto anycast = loadBit();
	if (_failed) {
		return std::nullopt;
	} else if (anycast) {
		// Anycast addresses are unused by wallet contracts; reject them.
		_failed = true;
		return std::nullopt;
	}
	const auto workchain = qint8(loadUint(8));
	const auto hash = loadBytes(kHashSize);
	if (_failed) {
		return std::nullopt;
	}
	auto result = Address();
	result.workchain = qint32(workchain);
	result.hash = hash;
	return result;
}

Cell CellSlice::loadRef() {
	if (!check(0, 1)) {
		return Cell();
	}
	return _cell.ref(_refOffset++);
}

std::optional<Cell> CellSlice::loadMaybeRef() {
	const auto has = loadBit();
	if (_failed || !has) {
		return std::nullopt;
	}
	auto ref = loadRef();
	if (_failed) {
		return std::nullopt;
	}
	return ref;
}

QString CellSlice::loadStringTail() {
	if (_failed) {
		return QString();
	}
	auto result = QByteArray();
	auto chain = std::optional<CellSlice>();
	auto current = this;
	while (true) {
		const auto bits = current->remainingBits();
		const auto refs = current->remainingRefs();
		if (bits % 8 != 0 || refs > 1) {
			_failed = true;
			return QString();
		}
		result.append(current->loadBytes(bits / 8));
		if (!current->ok()) {
			_failed = true;
			return QString();
		}
		if (refs != 1) {
			break;
		}
		auto ref = current->loadRef();
		if (!current->ok()) {
			_failed = true;
			return QString();
		}
		chain.emplace(ref.parse());
		current = &*chain;
	}
	return QString::fromUtf8(result);
}

void CellSlice::skip(int bits) {
	Expects(bits >= 0);

	if (check(bits)) {
		_bitOffset += bits;
	}
}

} // namespace Gram
