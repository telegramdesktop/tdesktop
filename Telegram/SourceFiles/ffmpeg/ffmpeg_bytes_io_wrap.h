/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ffmpeg/ffmpeg_utility.h"

namespace FFmpeg {

struct ReadBytesWrap {
	int64 size = 0;
	int64 offset = 0;
	const uchar *data = nullptr;

	static int Read(void *opaque, uint8_t *buf, int buf_size) {
		auto wrap = static_cast<ReadBytesWrap*>(opaque);
		const auto toRead = std::min(
			int64(buf_size),
			wrap->size - wrap->offset);
		if (!toRead) {
			return AVERROR_EOF;
		} else if (toRead > 0) {
			memcpy(buf, wrap->data + wrap->offset, toRead);
			wrap->offset += toRead;
		}
		return toRead;
	}
	static int64_t Seek(void *opaque, int64_t offset, int whence) {
		auto wrap = static_cast<ReadBytesWrap*>(opaque);
		auto updated = int64(-1);
		switch (whence) {
		case SEEK_SET: updated = offset; break;
		case SEEK_CUR: updated = wrap->offset + offset; break;
		case SEEK_END: updated = wrap->size + offset; break;
		case AVSEEK_SIZE: return wrap->size; break;
		}
		if (updated < 0 || updated > wrap->size) {
			return -1;
		}
		wrap->offset = updated;
		return updated;
	}
};

struct WriteBytesWrap {
	QByteArray content;
	int64 offset = 0;

#if DA_FFMPEG_CONST_WRITE_CALLBACK
	static int Write(void *opaque, const uint8_t *_buf, int buf_size) {
		uint8_t *buf = const_cast<uint8_t *>(_buf);
#else
	static int Write(void *opaque, uint8_t *buf, int buf_size) {
#endif
		auto wrap = static_cast<WriteBytesWrap*>(opaque);
		if (const auto total = wrap->offset + int64(buf_size)) {
			const auto size = int64(wrap->content.size());
			constexpr auto kReserve = 1024 * 1024;
			wrap->content.reserve((total / kReserve) * kReserve);
			const auto overwrite = std::min(
				size - wrap->offset,
				int64(buf_size));
			if (overwrite) {
				memcpy(wrap->content.data() + wrap->offset, buf, overwrite);
			}
			if (const auto append = buf_size - overwrite) {
				wrap->content.append(
					reinterpret_cast<const char*>(buf) + overwrite,
					append);
			}
			wrap->offset += buf_size;
		}
		return buf_size;
	}

	static int64_t Seek(void *opaque, int64_t offset, int whence) {
		auto wrap = static_cast<WriteBytesWrap*>(opaque);
		const auto &content = wrap->content;
		const auto checkedSeek = [&](int64_t offset) {
			if (offset < 0 || offset > int64(content.size())) {
				return int64_t(-1);
			}
			return int64_t(wrap->offset = offset);
		};
		switch (whence) {
		case SEEK_SET: return checkedSeek(offset);
		case SEEK_CUR: return checkedSeek(wrap->offset + offset);
		case SEEK_END: return checkedSeek(int64(content.size()) + offset);
		case AVSEEK_SIZE: return int64(content.size());
		}
		return -1;
	}
};

} // namespace FFmpeg
