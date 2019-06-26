/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QImage>
#include <QSize>
#include <QByteArray>

namespace Lottie {

class AlignedStorage {
public:
	void allocate(int packedBytesPerLine, int lines);

	int lines() const;
	int rawSize() const;

	// Gives a pointer to packedBytesPerLine * lines bytes of memory.
	void *raw();
	const void *raw() const;

	// Gives a stride value in the aligned storage (% 16 == 0).
	int bytesPerLine() const;

	// Gives a pointer to the aligned memory (% 16 == 0).
	void *aligned();
	const void *aligned() const;

	void copyRawToAligned();
	void copyAlignedToRaw();

private:
	void reallocate();

	int _packedBytesPerLine = 0;
	int _lines = 0;
	QByteArray _raw;
	QByteArray _buffer;

};

class CacheState {
public:
	enum class Encoder : uchar {
		YUV420A4_LZ4,
	};

	CacheState(const QByteArray &data, QSize box);

	[[nodiscard]] int frameRate() const;
	[[nodiscard]] int framesReady() const;
	[[nodiscard]] int framesCount() const;
	[[nodiscard]] QImage takeFirstFrame();

private:
	[[nodiscard]] bool readHeader(QSize box);
	void prepareBuffers();
	[[nodiscard]] bool readCompressedDelta(int offset);
	[[nodiscard]] int uncompressedDeltaSize() const;

	QByteArray _data;
	QSize _size;
	QSize _original;
	AlignedStorage _uncompressed;
	AlignedStorage _previous;
	QImage _firstFrame;
	int _frameRate = 0;
	int _framesCount = 0;
	int _framesReady = 0;
	int _offset = 0;
	Encoder _encoder = Encoder::YUV420A4_LZ4;

};

} // namespace Lottie
