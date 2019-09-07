/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/auth_key.h"

namespace Serialize {

inline int stringSize(const QString &str) {
	return sizeof(quint32) + str.size() * sizeof(ushort);
}

inline int bytearraySize(const QByteArray &arr) {
	return sizeof(quint32) + arr.size();
}

inline int bytesSize(bytes::const_span bytes) {
	return sizeof(quint32) + bytes.size();
}

inline int colorSize() {
	return sizeof(quint32);
}

void writeColor(QDataStream &stream, const QColor &color);
QColor readColor(QDataStream &stream);

struct ReadBytesVectorWrap {
	bytes::vector &bytes;
};

inline ReadBytesVectorWrap bytes(bytes::vector &bytes) {
	return ReadBytesVectorWrap { bytes };
}

// Compatible with QDataStream &operator>>(QDataStream &, QByteArray &);
inline QDataStream &operator>>(QDataStream &stream, ReadBytesVectorWrap data) {
	auto &bytes = data.bytes;
	bytes.clear();
	quint32 len;
	stream >> len;
	if (stream.status() != QDataStream::Ok || len == 0xFFFFFFFF) {
		return stream;
	}

	constexpr auto kStep = quint32(1024 * 1024);
	for (auto allocated = quint32(0); allocated < len;) {
		auto blockSize = qMin(kStep, len - allocated);
		bytes.resize(allocated + blockSize);
		if (stream.readRawData(reinterpret_cast<char*>(bytes.data()) + allocated, blockSize) != blockSize) {
			bytes.clear();
			stream.setStatus(QDataStream::ReadPastEnd);
			return stream;
		}
		allocated += blockSize;
	}

	return stream;
}

struct WriteBytesWrap {
	bytes::const_span bytes;
};

inline WriteBytesWrap bytes(bytes::const_span bytes) {
	return WriteBytesWrap { bytes };
}

inline QDataStream &operator<<(QDataStream &stream, WriteBytesWrap data) {
	auto bytes = data.bytes;
	if (bytes.empty()) {
		stream << quint32(0xFFFFFFFF);
	} else {
		auto size = quint32(bytes.size());
		stream << size;
		stream.writeRawData(reinterpret_cast<const char*>(bytes.data()), size);
	}
	return stream;
}

inline QDataStream &operator<<(QDataStream &stream, ReadBytesVectorWrap data) {
	return stream << WriteBytesWrap { data.bytes };
}

inline int dateTimeSize() {
	return (sizeof(qint64) + sizeof(quint32) + sizeof(qint8));
}

int storageImageLocationSize(const StorageImageLocation &location);
void writeStorageImageLocation(
	QDataStream &stream,
	const StorageImageLocation &location);

// NB! This method can return StorageFileLocation with Type::Generic!
// The reader should discard it or convert to one of the valid modern types.
std::optional<StorageImageLocation> readStorageImageLocation(
	int streamAppVersion,
	QDataStream &stream);

template <typename T>
inline T read(QDataStream &stream) {
	auto result = T();
	stream >> result;
	return result;
}

template <>
inline MTP::AuthKey::Data read<MTP::AuthKey::Data>(QDataStream &stream) {
	auto result = MTP::AuthKey::Data();
	stream.readRawData(reinterpret_cast<char*>(result.data()), result.size());
	return result;
}

uint32 peerSize(not_null<PeerData*> peer);
void writePeer(QDataStream &stream, PeerData *peer);
PeerData *readPeer(int streamAppVersion, QDataStream &stream);
QString peekUserPhone(int streamAppVersion, QDataStream &stream);

} // namespace Serialize
