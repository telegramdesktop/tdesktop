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

	const auto requestSize = (tl::count_length(*this) >> 2);
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
	const auto ints = (tl::count_length(*this) >> 2);
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

size_t SecureRequest::sizeInBytes() const {
	return (_data && _data->size() > kMessageBodyPosition)
		? (*_data)[kMessageLengthPosition]
		: 0;
}

const void *SecureRequest::dataInBytes() const {
	return (_data && _data->size() > kMessageBodyPosition)
		? (_data->constData() + kMessageBodyPosition)
		: nullptr;
}

} // namespace MTP

bool mtpTextSerializeCore(MTPStringLogger &to, const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons, uint32 level, mtpPrime vcons) {
	switch (mtpTypeId(cons)) {
	case mtpc_int: {
		MTPint value;
		if (value.read(from, end, cons)) {
			to.add(QString::number(value.v)).add(" [INT]");
			return true;
		}
	} break;

	case mtpc_long: {
		MTPlong value;
		if (value.read(from, end, cons)) {
			to.add(QString::number(value.v)).add(" [LONG]");
			return true;
		}
	} break;

	case mtpc_int128: {
		MTPint128 value;
		if (value.read(from, end, cons)) {
			to.add(QString::number(value.h)).add(" * 2^64 + ").add(QString::number(value.l)).add(" [INT128]");
			return true;
		}
	} break;

	case mtpc_int256: {
		MTPint256 value;
		if (value.read(from, end, cons)) {
			to.add(QString::number(value.h.h)).add(" * 2^192 + ").add(QString::number(value.h.l)).add(" * 2^128 + ").add(QString::number(value.l.h)).add(" * 2 ^ 64 + ").add(QString::number(value.l.l)).add(" [INT256]");
			return true;
		}
	} break;

	case mtpc_double: {
		MTPdouble value;
		if (value.read(from, end, cons)) {
			to.add(QString::number(value.v)).add(" [DOUBLE]");
			return true;
		}
	} break;

	case mtpc_string: {
		MTPstring value;
		if (value.read(from, end, cons)) {
			auto strUtf8 = value.v;
			auto str = QString::fromUtf8(strUtf8);
			if (str.toUtf8() == strUtf8) {
				to.add("\"").add(str.replace('\\', "\\\\").replace('"', "\\\"").replace('\n', "\\n")).add("\" [STRING]");
			} else if (strUtf8.size() < 64) {
				to.add(Logs::mb(strUtf8.constData(), strUtf8.size()).str()).add(" [").add(QString::number(strUtf8.size())).add(" BYTES]");
			} else {
				to.add(Logs::mb(strUtf8.constData(), 16).str()).add("... [").add(QString::number(strUtf8.size())).add(" BYTES]");
			}
			return true;
		}
	} break;

	case mtpc_vector: {
		if (from < end) {
			int32 cnt = *(from++);
			to.add("[ vector<0x").add(QString::number(vcons, 16)).add(">");
			if (cnt) {
				to.add("\n").addSpaces(level);
				for (int32 i = 0; i < cnt; ++i) {
					to.add("  ");
					if (!mtpTextSerializeType(to, from, end, vcons, level + 1)) {
						return false;
					}
					to.add(",\n").addSpaces(level);
				}
			} else {
				to.add(" ");
			}
			to.add("]");
			return true;
		}
	} break;

	case mtpc_gzip_packed: {
		MTPstring packed;
		// read packed string as serialized mtp string type
		if (!packed.read(from, end)) {
			return false;
		}
		uint32 packedLen = packed.v.size(), unpackedChunk = packedLen;
		mtpBuffer result; // * 4 because of mtpPrime type
		result.resize(0);

		z_stream stream;
		stream.zalloc = nullptr;
		stream.zfree = nullptr;
		stream.opaque = nullptr;
		stream.avail_in = 0;
		stream.next_in = nullptr;
		int res = inflateInit2(&stream, 16 + MAX_WBITS);
		if (res != Z_OK) {
			return false;
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
				return false;
			}
		}
		if (stream.avail_out & 0x03) {
			return false;
		}
		result.resize(result.size() - (stream.avail_out >> 2));
		inflateEnd(&stream);

		if (result.empty()) {
			return false;
		}
		const mtpPrime *newFrom = result.constData(), *newEnd = result.constData() + result.size();
		to.add("[GZIPPED] ");
		return mtpTextSerializeType(to, newFrom, newEnd, 0, level);
	} break;

	default: {
		for (uint32 i = 1; i < mtpLayerMaxSingle; ++i) {
			if (cons == mtpLayers[i]) {
				to.add("[LAYER").add(QString::number(i + 1)).add("] ");
				return mtpTextSerializeType(to, from, end, 0, level);
			}
		}
		if (cons == mtpc_invokeWithLayer) {
			if (from >= end) {
				return false;
			}
			int32 layer = *(from++);
			to.add("[LAYER").add(QString::number(layer)).add("] ");
			return mtpTextSerializeType(to, from, end, 0, level);
		}
	} break;
	}
	return false;
}
