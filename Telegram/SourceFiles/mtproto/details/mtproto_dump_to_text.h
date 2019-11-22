/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/core_types.h"
#include "mtproto/details/mtproto_serialized_request.h"

namespace MTP::details {

// Human-readable text serialization
QString DumpToText(const mtpPrime *&from, const mtpPrime *end);

struct DumpToTextBuffer {
	static constexpr auto kBufferSize = 1024 * 1024; // 1 mb start size

	DumpToTextBuffer()
		: p(new char[kBufferSize])
		, alloced(kBufferSize) {
	}
	~DumpToTextBuffer() {
		delete[] p;
	}

	DumpToTextBuffer &add(const QString &data) {
		auto d = data.toUtf8();
		return add(d.constData(), d.size());
	}

	DumpToTextBuffer &add(const char *data, int32 len = -1) {
		if (len < 0) len = strlen(data);
		if (!len) return (*this);

		ensureLength(len);
		memcpy(p + size, data, len);
		size += len;
		return (*this);
	}

	DumpToTextBuffer &addSpaces(int32 level) {
		int32 len = level * 2;
		if (!len) return (*this);

		ensureLength(len);
		for (char *ptr = p + size, *end = ptr + len; ptr != end; ++ptr) {
			*ptr = ' ';
		}
		size += len;
		return (*this);
	}

	DumpToTextBuffer &error(const char *problem = "could not decode type") {
		return add("[ERROR] (").add(problem).add(")");
	}

	void ensureLength(int32 add) {
		if (size + add <= alloced) return;

		int32 newsize = size + add;
		if (newsize % kBufferSize) {
			newsize += kBufferSize - (newsize % kBufferSize);
		}
		char *b = new char[newsize];
		memcpy(b, p, size);
		alloced = newsize;
		delete[] p;
		p = b;
	}

	char *p = nullptr;
	int size = 0;
	int alloced = 0;

};

[[nodiscard]] bool DumpToTextCore(DumpToTextBuffer &to, const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons, uint32 level, mtpPrime vcons = 0);

} // namespace MTP::details
