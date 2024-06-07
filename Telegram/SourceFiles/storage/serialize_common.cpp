/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/serialize_common.h"

namespace Serialize {

ByteArrayWriter::ByteArrayWriter(int expectedSize)
: _stream(&_result, QIODevice::WriteOnly) {
	if (expectedSize) {
		_result.reserve(expectedSize);
	}
	_stream.setVersion(QDataStream::Qt_5_1);
}

QByteArray ByteArrayWriter::result() && {
	_stream.device()->close();
	return std::move(_result);
}

ByteArrayReader::ByteArrayReader(QByteArray data)
: _data(std::move(data))
, _stream(&_data, QIODevice::ReadOnly) {
	_stream.setVersion(QDataStream::Qt_5_1);
}

void writeColor(QDataStream &stream, const QColor &color) {
	stream << (quint32(uchar(color.red()))
		| (quint32(uchar(color.green())) << 8)
		| (quint32(uchar(color.blue())) << 16)
		| (quint32(uchar(color.alpha())) << 24));
}

QColor readColor(QDataStream &stream) {
	auto value = quint32();
	stream >> value;
	return QColor(
		int(value & 0xFFU),
		int((value >> 8) & 0xFFU),
		int((value >> 16) & 0xFFU),
		int((value >> 24) & 0xFFU));
}

} // namespace Serialize
