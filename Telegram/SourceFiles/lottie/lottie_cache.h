/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ffmpeg/ffmpeg_utility.h"

#include <QImage>
#include <QSize>
#include <QByteArray>

namespace Lottie {

struct FrameRequest;

class EncodedStorage {
public:
	void allocate(int width, int height);

	int width() const;
	int height() const;

	char *data();
	const char *data() const;
	int size() const;

	uint8_t *yData();
	const uint8_t *yData() const;
	int yBytesPerLine() const;
	uint8_t *uData();
	const uint8_t *uData() const;
	int uBytesPerLine() const;
	uint8_t *vData();
	const uint8_t *vData() const;
	int vBytesPerLine() const;
	uint8_t *aData();
	const uint8_t *aData() const;
	int aBytesPerLine() const;

private:
	void reallocate();

	int _width = 0;
	int _height = 0;
	QByteArray _data;

};

class Cache {
public:
	enum class Encoder : qint8 {
		YUV420A4_LZ4,
	};

	Cache(
		const QByteArray &data,
		const FrameRequest &request,
		FnMut<void(QByteArray &&cached)> put);

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

	~Cache();

private:
	struct ReadResult {
		bool ok = false;
		bool xored = false;
	};
	struct EncodeFields {
		std::vector<QByteArray> compressedFrames;
		QByteArray compressBuffer;
		QByteArray xorCompressBuffer;
		QImage cache;
		FFmpeg::SwscalePointer context;
		int totalSize = 0;
	};
	int headerSize() const;
	void prepareBuffers();
	void finalizeEncoding();

	void writeHeader();
	void updateFramesReadyCount();
	[[nodiscard]] bool readHeader(const FrameRequest &request);
	[[nodiscard]] ReadResult readCompressedFrame();

	QByteArray _data;
	EncodeFields _encode;
	QSize _size;
	QSize _original;
	EncodedStorage _uncompressed;
	EncodedStorage _previous;
	FFmpeg::SwscalePointer _decodeContext;
	QImage _firstFrame;
	int _frameRate = 0;
	int _framesCount = 0;
	int _framesReady = 0;
	int _offset = 0;
	int _offsetFrameIndex = 0;
	Encoder _encoder = Encoder::YUV420A4_LZ4;
	FnMut<void(QByteArray &&cached)> _put;

};

} // namespace Lottie
