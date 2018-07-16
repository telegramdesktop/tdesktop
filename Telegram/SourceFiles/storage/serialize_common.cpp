/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/serialize_common.h"

namespace Serialize {

void writeStorageImageLocation(
		QDataStream &stream,
		const StorageImageLocation &location) {
	stream
		<< qint32(location.width())
		<< qint32(location.height())
		<< qint32(location.dc())
		<< quint64(location.volume())
		<< qint32(location.local())
		<< quint64(location.secret());
	stream << location.fileReference();
}

StorageImageLocation readStorageImageLocation(
		int streamAppVersion,
		QDataStream &stream) {
	qint32 width, height, dc, local;
	quint64 volume, secret;
	QByteArray fileReference;
	stream >> width >> height >> dc >> volume >> local >> secret;
	if (streamAppVersion >= 1003013) {
		stream >> fileReference;
	}
	return StorageImageLocation(
		width,
		height,
		dc,
		volume,
		local,
		secret,
		fileReference);
}

int storageImageLocationSize(const StorageImageLocation &location) {
	// width + height + dc + volume + local + secret + fileReference
	return sizeof(qint32)
		+ sizeof(qint32)
		+ sizeof(qint32)
		+ sizeof(quint64)
		+ sizeof(qint32)
		+ sizeof(quint64)
		+ bytearraySize(location.fileReference());
}

} // namespace Serialize
