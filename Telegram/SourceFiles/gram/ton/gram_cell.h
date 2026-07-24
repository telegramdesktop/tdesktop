/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "gram/ton/gram_address.h"

#include <QtCore/QByteArray>

#include <memory>
#include <optional>
#include <vector>

namespace Gram {

class CellSlice;

class Cell final {
public:
	Cell();
	Cell(QByteArray data, int bits, std::vector<Cell> refs);

	[[nodiscard]] const QByteArray &hash() const;
	[[nodiscard]] const QByteArray &data() const;
	[[nodiscard]] int bitsCount() const;
	[[nodiscard]] int refsCount() const;
	[[nodiscard]] const Cell &ref(int index) const;
	[[nodiscard]] quint16 depth() const;
	[[nodiscard]] CellSlice parse() const;

	friend bool operator==(const Cell &a, const Cell &b) {
		return a.hash() == b.hash();
	}

private:
	struct Data;
	std::shared_ptr<const Data> _data;

};

class CellBuilder final {
public:
	CellBuilder &storeBit(bool value);
	CellBuilder &storeUint(quint64 value, int bits);
	CellBuilder &storeBytes(const QByteArray &data);
	CellBuilder &storeCoins(qint64 nano);
	CellBuilder &storeAddress(const std::optional<Address> &address);
	CellBuilder &storeRef(Cell cell);
	CellBuilder &storeSlice(const CellSlice &slice);
	CellBuilder &storeStringTail(const QString &text);
	CellBuilder &storeMaybeRef(const std::optional<Cell> &cell);
	[[nodiscard]] int availableBits() const;
	[[nodiscard]] int availableRefs() const;
	[[nodiscard]] Cell finish();

private:
	void putBit(bool value);
	void putUint(quint64 value, int bits);

	QByteArray _data = QByteArray(128, 0);
	int _bits = 0;
	std::vector<Cell> _refs;

};

class CellSlice final {
public:
	explicit CellSlice(Cell cell);

	[[nodiscard]] bool ok() const;
	[[nodiscard]] int remainingBits() const;
	[[nodiscard]] int remainingRefs() const;

	bool loadBit();
	quint64 loadUint(int bits);
	QByteArray loadBytes(int count);
	qint64 loadCoins();
	std::optional<Address> loadAddress();
	Cell loadRef();
	std::optional<Cell> loadMaybeRef();
	QString loadStringTail();
	void skip(int bits);

private:
	[[nodiscard]] bool check(int bits, int refs = 0);

	Cell _cell;
	int _bitOffset = 0;
	int _refOffset = 0;
	bool _failed = false;

};

} // namespace Gram
