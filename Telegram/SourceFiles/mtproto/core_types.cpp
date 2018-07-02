/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/core_types.h"

#include "zlib.h"

namespace MTP {
namespace {

uint32 CountPaddingAmountInInts(uint32 requestSize, bool extended) {
#ifdef TDESKTOP_MTPROTO_OLD
	return ((8 + requestSize) & 0x03)
		? (4 - ((8 + requestSize) & 0x03))
		: 0;
#else // TDESKTOP_MTPROTO_OLD
	auto result = ((8 + requestSize) & 0x03)
		? (4 - ((8 + requestSize) & 0x03))
		: 0;

	// At least 12 bytes of random padding.
	if (result < 3) {
		result += 4;
	}

	if (extended) {
		// Some more random padding.
		result += ((rand_value<uchar>() & 0x0F) << 2);
	}

	return result;
#endif // TDESKTOP_MTPROTO_OLD
}

} // namespace

SecureRequest::SecureRequest(const details::SecureRequestCreateTag &tag)
: _data(std::make_shared<SecureRequestData>(tag)) {
}

SecureRequest SecureRequest::Prepare(uint32 size, uint32 reserveSize) {
	const auto finalSize = std::max(size, reserveSize);

	auto result = SecureRequest(details::SecureRequestCreateTag{});
	result->reserve(kMessageBodyPosition + finalSize);
	result->resize(kMessageBodyPosition);
	result->back() = (size << 2);
	return result;
}

uint32 SecureRequest::innerLength() const {
	if (!_data || _data->size() <= kMessageBodyPosition) {
		return 0;
	}
	return (*_data)[kMessageLengthPosition];
}

void SecureRequest::write(mtpBuffer &to) const {
	if (!_data || _data->size() <= kMessageBodyPosition) {
		return;
	}
	uint32 was = to.size(), s = innerLength() / sizeof(mtpPrime);
	to.resize(was + s);
	memcpy(
		to.data() + was,
		_data->constData() + kMessageBodyPosition,
		s * sizeof(mtpPrime));
}

SecureRequestData *SecureRequest::operator->() const {
	Expects(_data != nullptr);

	return _data.get();
}

SecureRequestData &SecureRequest::operator*() const {
	Expects(_data != nullptr);

	return *_data;
}

SecureRequest::operator bool() const {
	return (_data != nullptr);
}

void SecureRequest::addPadding(bool extended) {
	if (_data->size() <= kMessageBodyPosition) return;

	const auto requestSize = (innerLength() >> 2);
	const auto padding = CountPaddingAmountInInts(requestSize, extended);
	const auto fullSize = kMessageBodyPosition + requestSize + padding;
	if (uint32(_data->size()) != fullSize) {
		_data->resize(fullSize);
		if (padding > 0) {
			memset_rand(
				_data->data() + (fullSize - padding),
				padding * sizeof(mtpPrime));
		}
	}
}

uint32 SecureRequest::messageSize() const {
	if (_data->size() <= kMessageBodyPosition) {
		return 0;
	}
	const auto ints = (innerLength() >> 2);
	return kMessageIdInts + kSeqNoInts + kMessageLengthInts + ints;
}

bool SecureRequest::isSentContainer() const {
	if (_data->size() <= kMessageBodyPosition) {
		return false;
	}
	return (!_data->msDate && !(*_data)[kSeqNoPosition]); // msDate = 0, seqNo = 0
}

bool SecureRequest::isStateRequest() const {
	if (_data->size() <= kMessageBodyPosition) {
		return false;
	}
	const auto type = mtpTypeId((*_data)[kMessageBodyPosition]);
	return (type == mtpc_msgs_state_req);
}

bool SecureRequest::needAck() const {
	if (_data->size() <= kMessageBodyPosition) {
		return false;
	}
	const auto type = mtpTypeId((*_data)[kMessageBodyPosition]);
	switch (type) {
	case mtpc_msg_container:
	case mtpc_msgs_ack:
	case mtpc_http_wait:
	case mtpc_bad_msg_notification:
	case mtpc_msgs_all_info:
	case mtpc_msgs_state_info:
	case mtpc_msg_detailed_info:
	case mtpc_msg_new_detailed_info:
		return false;
	}
	return true;
}

} // namespace MTP

Exception::Exception(const QString &msg) noexcept : _msg(msg.toUtf8()) {
	LOG(("Exception: %1").arg(msg));
}

mtpErrorUnexpected::mtpErrorUnexpected(
	mtpTypeId typeId,
	const QString &type) noexcept
: Exception(
	QString("MTP Unexpected type id #%1 read in %2"
	).arg(uint32(typeId), 0, 16
	).arg(type)) {
}

mtpErrorInsufficient::mtpErrorInsufficient() noexcept
: Exception("MTP Insufficient bytes in input buffer") {
}

mtpErrorBadTypeId::mtpErrorBadTypeId(
	mtpTypeId typeId,
	const QString &type) noexcept
: Exception(
	QString("MTP Bad type id #%1 passed to constructor of %2"
	).arg(uint32(typeId), 0, 16
	).arg(type)) {
}

const char *Exception::what() const noexcept {
	return _msg.constData();
}

uint32 MTPstring::innerLength() const {
	uint32 l = v.length();
	if (l < 254) {
		l += 1;
	} else {
		l += 4;
	}
	uint32 d = l & 0x03;
	if (d) l += (4 - d);
	return l;
}

void MTPstring::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {
	if (from + 1 > end) throw mtpErrorInsufficient();
	if (cons != mtpc_string) throw mtpErrorUnexpected(cons, "MTPstring");

	uint32 l;
	const uchar *buf = (const uchar*)from;
	if (buf[0] == 254) {
		l = (uint32)buf[1] + ((uint32)buf[2] << 8) + ((uint32)buf[3] << 16);
		buf += 4;
		from += ((l + 4) >> 2) + (((l + 4) & 0x03) ? 1 : 0);
	} else {
		l = (uint32)buf[0];
		++buf;
		from += ((l + 1) >> 2) + (((l + 1) & 0x03) ? 1 : 0);
	}
	if (from > end) throw mtpErrorInsufficient();

	v = QByteArray(reinterpret_cast<const char*>(buf), l);
}

void MTPstring::write(mtpBuffer &to) const {
	uint32 l = v.length(), s = l + ((l < 254) ? 1 : 4), was = to.size();
	if (s & 0x03) {
		s += 4;
	}
	s >>= 2;
	to.resize(was + s);
	char *buf = (char*)&to[was];
	if (l < 254) {
		uchar sl = (uchar)l;
		*(buf++) = *(char*)(&sl);
	} else {
		*(buf++) = (char)254;
		*(buf++) = (char)(l & 0xFF);
		*(buf++) = (char)((l >> 8) & 0xFF);
		*(buf++) = (char)((l >> 16) & 0xFF);
	}
	memcpy(buf, v.constData(), l);
}

void mtpTextSerializeCore(MTPStringLogger &to, const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons, uint32 level, mtpPrime vcons) {
	switch (mtpTypeId(cons)) {
	case mtpc_int: {
		MTPint value;
		value.read(from, end, cons);
		to.add(QString::number(value.v)).add(" [INT]");
	} break;

	case mtpc_long: {
		MTPlong value;
		value.read(from, end, cons);
		to.add(QString::number(value.v)).add(" [LONG]");
	} break;

	case mtpc_int128: {
		MTPint128 value;
		value.read(from, end, cons);
		to.add(QString::number(value.h)).add(" * 2^64 + ").add(QString::number(value.l)).add(" [INT128]");
	} break;

	case mtpc_int256: {
		MTPint256 value;
		value.read(from, end, cons);
		to.add(QString::number(value.h.h)).add(" * 2^192 + ").add(QString::number(value.h.l)).add(" * 2^128 + ").add(QString::number(value.l.h)).add(" * 2 ^ 64 + ").add(QString::number(value.l.l)).add(" [INT256]");
	} break;

	case mtpc_double: {
		MTPdouble value;
		value.read(from, end, cons);
		to.add(QString::number(value.v)).add(" [DOUBLE]");
	} break;

	case mtpc_string: {
		MTPstring value;
		value.read(from, end, cons);
		auto strUtf8 = value.v;
		auto str = QString::fromUtf8(strUtf8);
		if (str.toUtf8() == strUtf8) {
			to.add("\"").add(str.replace('\\', "\\\\").replace('"', "\\\"").replace('\n', "\\n")).add("\" [STRING]");
		} else if (strUtf8.size() < 64) {
			to.add(Logs::mb(strUtf8.constData(), strUtf8.size()).str()).add(" [").add(QString::number(strUtf8.size())).add(" BYTES]");
		} else {
			to.add(Logs::mb(strUtf8.constData(), 16).str()).add("... [").add(QString::number(strUtf8.size())).add(" BYTES]");
		}
	} break;

	case mtpc_vector: {
		if (from >= end) {
			throw Exception("from >= end in vector");
		}
		int32 cnt = *(from++);
		to.add("[ vector<0x").add(QString::number(vcons, 16)).add(">");
		if (cnt) {
			to.add("\n").addSpaces(level);
			for (int32 i = 0; i < cnt; ++i) {
				to.add("  ");
				mtpTextSerializeType(to, from, end, vcons, level + 1);
				to.add(",\n").addSpaces(level);
			}
		} else {
			to.add(" ");
		}
		to.add("]");
	} break;

	case mtpc_gzip_packed: {
		MTPstring packed;
		packed.read(from, end); // read packed string as serialized mtp string type
		uint32 packedLen = packed.v.size(), unpackedChunk = packedLen;
		mtpBuffer result; // * 4 because of mtpPrime type
		result.resize(0);

		z_stream stream;
		stream.zalloc = 0;
		stream.zfree = 0;
		stream.opaque = 0;
		stream.avail_in = 0;
		stream.next_in = 0;
		int res = inflateInit2(&stream, 16 + MAX_WBITS);
		if (res != Z_OK) {
			throw Exception(QString("ungzip init, code: %1").arg(res));
		}
		stream.avail_in = packedLen;
		stream.next_in = reinterpret_cast<Bytef*>(packed.v.data());
		stream.avail_out = 0;
		while (!stream.avail_out) {
			result.resize(result.size() + unpackedChunk);
			stream.avail_out = unpackedChunk * sizeof(mtpPrime);
			stream.next_out = (Bytef*)&result[result.size() - unpackedChunk];
			int res = inflate(&stream, Z_NO_FLUSH);
			if (res != Z_OK && res != Z_STREAM_END) {
				inflateEnd(&stream);
				throw Exception(QString("ungzip unpack, code: %1").arg(res));
			}
		}
		if (stream.avail_out & 0x03) {
			uint32 badSize = result.size() * sizeof(mtpPrime) - stream.avail_out;
			throw Exception(QString("ungzip bad length, size: %1").arg(badSize));
		}
		result.resize(result.size() - (stream.avail_out >> 2));
		inflateEnd(&stream);

		if (!result.size()) {
			throw Exception("ungzip void data");
		}
		const mtpPrime *newFrom = result.constData(), *newEnd = result.constData() + result.size();
		to.add("[GZIPPED] "); mtpTextSerializeType(to, newFrom, newEnd, 0, level);
	} break;

	default: {
		for (uint32 i = 1; i < mtpLayerMaxSingle; ++i) {
			if (cons == mtpLayers[i]) {
				to.add("[LAYER").add(QString::number(i + 1)).add("] "); mtpTextSerializeType(to, from, end, 0, level);
				return;
			}
		}
		if (cons == mtpc_invokeWithLayer) {
			if (from >= end) {
				throw Exception("from >= end in invokeWithLayer");
			}
			int32 layer = *(from++);
			to.add("[LAYER").add(QString::number(layer)).add("] "); mtpTextSerializeType(to, from, end, 0, level);
			return;
		}
		throw Exception(QString("unknown cons 0x%1").arg(cons, 0, 16));
	} break;
	}
}
