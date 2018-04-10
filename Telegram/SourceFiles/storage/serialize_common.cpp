/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/serialize_common.h"

namespace Serialize {

void writeStorageImageLocation(QDataStream &stream, const StorageImageLocation &loc) {
	stream << qint32(loc.width()) << qint32(loc.height());
	stream << qint32(loc.dc()) << quint64(loc.volume()) << qint32(loc.local()) << quint64(loc.secret());
}

StorageImageLocation readStorageImageLocation(QDataStream &stream) {
	qint32 width, height, dc, local;
	quint64 volume, secret;
	stream >> width >> height >> dc >> volume >> local >> secret;
	return StorageImageLocation(width, height, dc, volume, local, secret);
}

int storageImageLocationSize() {
	// width + height + dc + volume + local + secret
	return sizeof(qint32) + sizeof(qint32) + sizeof(qint32) + sizeof(quint64) + sizeof(qint32) + sizeof(quint64);
}

} // namespace Serialize
