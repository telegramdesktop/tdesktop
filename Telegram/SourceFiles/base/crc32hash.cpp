/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "base/crc32hash.h"

namespace base {
namespace {

class Crc32Table {
public:
	Crc32Table() {
		auto poly = std::uint32_t(0x04c11db7);
		for (auto i = 0; i != 256; ++i) {
			_data[i] = reflect(i, 8) << 24;
			for (auto j = 0; j != 8; ++j) {
				_data[i] = (_data[i] << 1) ^ (_data[i] & (1 << 31) ? poly : 0);
			}
			_data[i] = reflect(_data[i], 32);
		}
	}

	std::uint32_t operator[](int index) const {
		return _data[index];
	}

private:
	std::uint32_t reflect(std::uint32_t val, char ch) {
		auto result = std::uint32_t(0);
		for (int i = 1; i < (ch + 1); ++i) {
			if (val & 1) {
				result |= 1 << (ch - i);
			}
			val >>= 1;
		}
		return result;
	}

	std::uint32_t _data[256];

};

} // namespace

std::int32_t crc32(const void *data, int len) {
	static const auto kTable = Crc32Table();

	const auto buffer = static_cast<const std::uint8_t*>(data);

	auto crc = std::uint32_t(0xffffffff);
	for (auto i = 0; i != len; ++i) {
		crc = (crc >> 8) ^ kTable[(crc & 0xFF) ^ buffer[i]];
	}

	return static_cast<std::int32_t>(crc ^ 0xffffffff);
}

} // namespace base
