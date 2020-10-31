/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/clip/media_clip_check_streaming.h"

#include "core/file_location.h"
#include "base/bytes.h"
#include "logs.h"

#include <QtCore/QtEndian>
#include <QtCore/QBuffer>

namespace Media {
namespace Clip {
namespace {

constexpr auto kHeaderSize = 8;
constexpr auto kFindMoovBefore = 128 * 1024;

template <typename Type>
Type ReadBigEndian(bytes::const_span data) {
	const auto bytes = data.subspan(0, sizeof(Type)).data();
	return qFromBigEndian(*reinterpret_cast<const Type*>(bytes));
}

bool IsAtom(bytes::const_span header, const char (&atom)[5]) {
	const auto check = header.subspan(4, 4);
	return bytes::compare(
		header.subspan(4, 4),
		bytes::make_span(atom).subspan(0, 4)) == 0;
}

} // namespace

bool CheckStreamingSupport(
		const Core::FileLocation &location,
		QByteArray data) {
	QBuffer buffer;
	QFile file;
	if (data.isEmpty()) {
		file.setFileName(location.name());
	} else {
		buffer.setBuffer(&data);
	}
	const auto size = data.isEmpty()
		? file.size()
		: data.size();
	const auto device = data.isEmpty()
		? static_cast<QIODevice*>(&file)
		: static_cast<QIODevice*>(&buffer);

	if (size < kHeaderSize || !device->open(QIODevice::ReadOnly)) {
		return false;
	}

	auto lastReadPosition = 0;
	char atomHeader[kHeaderSize] = { 0 };
	auto atomHeaderBytes = bytes::make_span(atomHeader);
	while (true) {
		const auto position = device->pos();
		if (device->read(atomHeader, kHeaderSize) != kHeaderSize) {
			break;
		}

		if (lastReadPosition >= kFindMoovBefore) {
			return false;
		} else if (IsAtom(atomHeaderBytes, "moov")) {
			return true;
		}

		const auto length = [&] {
			const auto result = ReadBigEndian<uint32>(atomHeaderBytes);
			if (result != 1) {
				return uint64(result);
			}
			char atomSize64[kHeaderSize] = { 0 };
			if (device->read(atomSize64, kHeaderSize) != kHeaderSize) {
				return uint64(-1);
			}
			auto atomSize64Bytes = bytes::make_span(atomSize64);
			return ReadBigEndian<uint64>(atomSize64Bytes);
		}();
		if (position + length > size) {
			break;
		}
		device->seek(position + length);
		lastReadPosition = position;
	}
	return false;
}

} // namespace Clip
} // namespace Media
