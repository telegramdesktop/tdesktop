/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/mtproto_auth_key.h"
#include "base/bytes.h"

#include <QtCore/QDataStream>

namespace Serialize {

class ByteArrayWriter final {
public:
	explicit ByteArrayWriter(int expectedSize = 0);

	[[nodiscard]] QDataStream &underlying() {
		return _stream;
	}
	[[nodiscard]] operator QDataStream &() {
		return _stream;
	}
	[[nodiscard]] QByteArray result() &&;

private:
	QByteArray _result;
	QDataStream _stream;

};

template <typename T>
inline ByteArrayWriter &operator<<(ByteArrayWriter &stream, const T &data) {
	stream.underlying() << data;
	return stream;
}

class ByteArrayReader final {
public:
	explicit ByteArrayReader(QByteArray data);

	[[nodiscard]] QDataStream &underlying() {
		return _stream;
	}
	[[nodiscard]] operator QDataStream &() {
		return _stream;
	}

	[[nodiscard]] bool atEnd() const {
		return _stream.atEnd();
	}
	[[nodiscard]] bool status() const {
		return _stream.status();
	}
	[[nodiscard]] bool ok() const {
		return _stream.status() == QDataStream::Ok;
	}

private:
	QByteArray _data;
	QDataStream _stream;

};

template <typename T>
inline ByteArrayReader &operator>>(ByteArrayReader &stream, T &data) {
	if (!stream.ok()) {
		data = T();
	} else {
		stream.underlying() >> data;
		if (!stream.ok()) {
			data = T();
		}
	}
	return stream;
}

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

} // namespace Serialize
