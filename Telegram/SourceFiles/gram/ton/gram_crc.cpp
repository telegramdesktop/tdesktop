/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/ton/gram_crc.h"

namespace Gram {
namespace {

constexpr auto kCrc32CPoly = quint32(0x82F63B78);
constexpr auto kCrc16Poly = quint32(0x1021);

} // namespace

quint32 Crc32C(const QByteArray &data) {
	auto crc = quint32(0xFFFFFFFF);
	for (auto i = 0, count = int(data.size()); i != count; ++i) {
		crc ^= uchar(data[i]);
		for (auto bit = 0; bit != 8; ++bit) {
			crc = (crc & 1) ? ((crc >> 1) ^ kCrc32CPoly) : (crc >> 1);
		}
	}
	crc ^= 0xFFFFFFFF;
	return crc;
}

quint16 Crc16(const QByteArray &data) {
	auto reg = quint32(0);
	const auto count = int(data.size());
	for (auto i = 0; i != count + 2; ++i) {
		const auto byte = (i < count) ? uchar(data[i]) : uchar(0);
		for (auto mask = 0x80; mask != 0; mask >>= 1) {
			reg <<= 1;
			if (byte & mask) {
				++reg;
			}
			if (reg > 0xFFFF) {
				reg &= 0xFFFF;
				reg ^= kCrc16Poly;
			}
		}
	}
	return quint16(reg);
}

} // namespace Gram
