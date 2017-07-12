/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "mtproto/core_types.h"

#include "zlib.h"

#include "lang/lang_keys.h"

QString mtpWrapNumber(float64 number) {
	return QString::number(number);
}

void mtpTextSerializeCore(MTPStringLogger &to, const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons, uint32 level, mtpPrime vcons) {
	switch (mtpTypeId(cons)) {
	case mtpc_int: {
		MTPint value;
		value.read(from, end, cons);
		to.add(mtpWrapNumber(value.v)).add(" [INT]");
	} break;

	case mtpc_long: {
		MTPlong value;
		value.read(from, end, cons);
		to.add(mtpWrapNumber(value.v)).add(" [LONG]");
	} break;

	case mtpc_int128: {
		MTPint128 value;
		value.read(from, end, cons);
		to.add(mtpWrapNumber(value.h)).add(" * 2^64 + ").add(mtpWrapNumber(value.l)).add(" [INT128]");
	} break;

	case mtpc_int256: {
		MTPint256 value;
		value.read(from, end, cons);
		to.add(mtpWrapNumber(value.h.h)).add(" * 2^192 + ").add(mtpWrapNumber(value.h.l)).add(" * 2^128 + ").add(mtpWrapNumber(value.l.h)).add(" * 2 ^ 64 + ").add(mtpWrapNumber(value.l.l)).add(" [INT256]");
	} break;

	case mtpc_double: {
		MTPdouble value;
		value.read(from, end, cons);
		to.add(mtpWrapNumber(value.v)).add(" [DOUBLE]");
	} break;

	case mtpc_string: {
		MTPstring value;
		value.read(from, end, cons);
		auto strUtf8 = value.v;
		auto str = QString::fromUtf8(strUtf8);
		if (str.toUtf8() == strUtf8) {
			to.add("\"").add(str.replace('\\', "\\\\").replace('"', "\\\"").replace('\n', "\\n")).add("\" [STRING]");
		} else if (strUtf8.size() < 64) {
			to.add(Logs::mb(strUtf8.constData(), strUtf8.size()).str()).add(" [").add(mtpWrapNumber(strUtf8.size())).add(" BYTES]");
		} else {
			to.add(Logs::mb(strUtf8.constData(), 16).str()).add("... [").add(mtpWrapNumber(strUtf8.size())).add(" BYTES]");
		}
	} break;

	case mtpc_vector: {
		if (from >= end) {
			throw Exception("from >= end in vector");
		}
		int32 cnt = *(from++);
		to.add("[ vector<0x").add(mtpWrapNumber(vcons, 16)).add(">");
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
				to.add("[LAYER").add(mtpWrapNumber(i + 1)).add("] "); mtpTextSerializeType(to, from, end, 0, level);
				return;
			}
		}
		if (cons == mtpc_invokeWithLayer) {
			if (from >= end) {
				throw Exception("from >= end in invokeWithLayer");
			}
			int32 layer = *(from++);
			to.add("[LAYER").add(mtpWrapNumber(layer)).add("] "); mtpTextSerializeType(to, from, end, 0, level);
			return;
		}
		throw Exception(QString("unknown cons 0x%1").arg(cons, 0, 16));
	} break;
	}
}

const MTPReplyMarkup MTPnullMarkup = MTP_replyKeyboardMarkup(MTP_flags(0), MTP_vector<MTPKeyboardButtonRow>(0));
const MTPVector<MTPMessageEntity> MTPnullEntities = MTP_vector<MTPMessageEntity>(0);
const MTPMessageFwdHeader MTPnullFwdHeader = MTP_messageFwdHeader(MTP_flags(0), MTPint(), MTPint(), MTPint(), MTPint(), MTPstring());

QString stickerSetTitle(const MTPDstickerSet &s) {
	QString title = qs(s.vtitle);
	if ((s.vflags.v & MTPDstickerSet::Flag::f_official) && !title.compare(qstr("Great Minds"), Qt::CaseInsensitive)) {
		return lang(lng_stickers_default_set);
	}
	return title;
}
