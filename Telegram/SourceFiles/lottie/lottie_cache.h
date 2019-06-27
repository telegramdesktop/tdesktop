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

struct FrameRequest;

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
	enum class Encoder : qint8 {
		YUV420A4_LZ4,
	};

	CacheState(const QByteArray &data, const FrameRequest &request);

	void init(
		QSize original,
		int frameRate,
		int framesCount,
		const FrameRequest &request);
	[[nodiscard]] int frameRate() const;
	[[nodiscard]] int framesReady() const;
	[[nodiscard]] int framesCount() const;
	[[nodiscard]] QSize originalSize() const;
	[[nodiscard]] QImage takeFirstFrame();

	[[nodiscard]] bool renderFrame(
		QImage &to,
		const FrameRequest &request,
		int index);
	void appendFrame(
		const QImage &frame,
		const FrameRequest &request,
		int index);

private:
	struct ReadResult {
		bool ok = false;
		bool xored = false;
	};
	int headerSize() const;
	void prepareBuffers();
	void finalizeEncoding();

	void writeHeader();
	[[nodiscard]] bool readHeader(const FrameRequest &request);
	[[nodiscard]] ReadResult readCompressedFrame();

	QByteArray _data;
	std::vector<QByteArray> _compressedFrames;
	QByteArray _compressBuffer;
	QByteArray _xorCompressBuffer;
	QSize _size;
	QSize _original;
	AlignedStorage _uncompressed;
	AlignedStorage _previous;
	QImage _firstFrame;
	int _frameRate = 0;
	int _framesCount = 0;
	int _framesReady = 0;
	int _offset = 0;
	int _offsetFrameIndex = 0;
	Encoder _encoder = Encoder::YUV420A4_LZ4;

};

} // namespace Lottie
