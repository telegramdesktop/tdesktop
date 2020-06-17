/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/serialize_common.h"

namespace Serialize {

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
