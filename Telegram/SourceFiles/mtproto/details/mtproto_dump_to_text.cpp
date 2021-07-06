/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/details/mtproto_dump_to_text.h"

#include "scheme-dump_to_text.h"
#include "scheme.h"

#include "zlib.h"

namespace MTP::details {

bool DumpToTextCore(DumpToTextBuffer &to, const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons, uint32 level, mtpPrime vcons) {
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
			to.add("[ vector<0x").add(QString::number(vcons, 16)).add("> (").add(QString::number(cnt)).add(")");
			if (cnt) {
				to.add("\n").addSpaces(level);
				for (int32 i = 0; i < cnt; ++i) {
					to.add("  ");
					if (!DumpToTextType(to, from, end, vcons, level + 1)) {
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
		return DumpToTextType(to, newFrom, newEnd, 0, level);
	} break;

	default: {
		for (uint32 i = 1; i < mtpLayerMaxSingle; ++i) {
			if (cons == mtpLayers[i]) {
				to.add("[LAYER").add(QString::number(i + 1)).add("] ");
				return DumpToTextType(to, from, end, 0, level);
			}
		}
		if (cons == mtpc_invokeWithLayer) {
			if (from >= end) {
				return false;
			}
			int32 layer = *(from++);
			to.add("[LAYER").add(QString::number(layer)).add("] ");
			return DumpToTextType(to, from, end, 0, level);
		}
	} break;
	}
	return false;
}

QString DumpToText(const mtpPrime *&from, const mtpPrime *end) {
	DumpToTextBuffer to;
	[[maybe_unused]] bool result = DumpToTextType(to, from, end, mtpc_core_message);
	return QString::fromUtf8(to.p, to.size);
}

} // namespace MTP::details
