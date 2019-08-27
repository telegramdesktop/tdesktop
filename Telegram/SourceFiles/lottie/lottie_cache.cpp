/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lottie/lottie_cache.h"

#include "lottie/lottie_frame_renderer.h"
#include "ffmpeg/ffmpeg_utility.h"
#include "base/bytes.h"

#include <QDataStream>
#include <lz4.h>
#include <lz4hc.h>
#include <range/v3/numeric/accumulate.hpp>

namespace Lottie {
namespace {

constexpr auto kAlignStorage = 16;

// Must not exceed max database allowed entry size.
constexpr auto kMaxCacheSize = 10 * 1024 * 1024;

void Xor(EncodedStorage &to, const EncodedStorage &from) {
	Expects(to.size() == from.size());

	using Block = std::conditional_t<
		sizeof(void*) == sizeof(uint64),
		uint64,
		uint32>;
	constexpr auto kBlockSize = sizeof(Block);
	const auto amount = from.size();
	const auto fromBytes = reinterpret_cast<const uchar*>(from.data());
	const auto toBytes = reinterpret_cast<uchar*>(to.data());
	const auto blocks = amount / kBlockSize;
	const auto fromBlocks = reinterpret_cast<const Block*>(fromBytes);
	const auto toBlocks = reinterpret_cast<Block*>(toBytes);
	for (auto i = 0; i != blocks; ++i) {
		toBlocks[i] ^= fromBlocks[i];
	}
	const auto left = amount - (blocks * kBlockSize);
	for (auto i = amount - left; i != amount; ++i) {
		toBytes[i] ^= fromBytes[i];
	}
}

bool UncompressToRaw(EncodedStorage &to, bytes::const_span from) {
	if (from.empty() || from.size() > to.size()) {
		return false;
	} else if (from.size() == to.size()) {
		memcpy(to.data(), from.data(), from.size());
		return true;
	}
	const auto result = LZ4_decompress_safe(
		reinterpret_cast<const char*>(from.data()),
		to.data(),
		from.size(),
		to.size());
	return (result == to.size());
}

void CompressFromRaw(QByteArray &to, const EncodedStorage &from) {
	const auto size = from.size();
	const auto max = sizeof(qint32) + LZ4_compressBound(size);
	to.reserve(max);
	to.resize(max);
	const auto compressed = LZ4_compress_default(
		from.data(),
		to.data() + sizeof(qint32),
		size,
		to.size() - sizeof(qint32));
	Assert(compressed > 0);
	if (compressed >= size + sizeof(qint32)) {
		to.resize(size + sizeof(qint32));
		memcpy(to.data() + sizeof(qint32), from.data(), size);
	} else {
		to.resize(compressed + sizeof(qint32));
	}
	const auto length = qint32(to.size() - sizeof(qint32));
	bytes::copy(
		bytes::make_detached_span(to),
		bytes::object_as_span(&length));
}

void CompressAndSwapFrame(
		QByteArray &to,
		QByteArray *additional,
		EncodedStorage &frame,
		EncodedStorage &previous) {
	CompressFromRaw(to, frame);
	std::swap(frame, previous);
	if (!additional) {
		return;
	}

	// Check if XOR-d delta compresses better.
	Xor(frame, previous);
	CompressFromRaw(*additional, frame);
	if (additional->size() >= to.size()) {
		return;
	}
	std::swap(to, *additional);

	// Negative length means we XOR-d with the previous frame.
	const auto negativeLength = -qint32(to.size() - sizeof(qint32));
	bytes::copy(
		bytes::make_detached_span(to),
		bytes::object_as_span(&negativeLength));
}

void DecodeYUV2RGB(
		QImage &to,
		const EncodedStorage &from,
		FFmpeg::SwscalePointer &context) {
	context = FFmpeg::MakeSwscalePointer(
		to.size(),
		AV_PIX_FMT_YUV420P,
		to.size(),
		AV_PIX_FMT_BGRA,
		&context);
	Assert(context != nullptr);

	// AV_NUM_DATA_POINTERS defined in AVFrame struct
	const uint8_t *src[AV_NUM_DATA_POINTERS] = {
		from.yData(),
		from.uData(),
		from.vData(),
		nullptr
	};
	int srcLineSize[AV_NUM_DATA_POINTERS] = {
		from.yBytesPerLine(),
		from.uBytesPerLine(),
		from.vBytesPerLine(),
		0
	};
	uint8_t *dst[AV_NUM_DATA_POINTERS] = { to.bits(), nullptr };
	int dstLineSize[AV_NUM_DATA_POINTERS] = { to.bytesPerLine(), 0 };

	const auto lines = sws_scale(
		context.get(),
		src,
		srcLineSize,
		0,
		to.height(),
		dst,
		dstLineSize);

	Ensures(lines == to.height());
}

void DecodeAlpha(QImage &to, const EncodedStorage &from) {
	auto bytes = to.bits();
	auto alpha = from.aData();
	const auto perLine = to.bytesPerLine();
	const auto width = to.width();
	const auto height = to.height();
	for (auto i = 0; i != height; ++i) {
		auto ints = reinterpret_cast<uint32*>(bytes);
		const auto till = ints + width;
		while (ints != till) {
			const auto value = uint32(*alpha++);
			*ints = (*ints & 0x00FFFFFFU)
				| ((value & 0xF0U) << 24)
				| ((value & 0xF0U) << 20);
			++ints;
			*ints = (*ints & 0x00FFFFFFU)
				| (value << 28)
				| ((value & 0x0FU) << 24);
			++ints;
		}
		bytes += perLine;
	}
}

void Decode(
		QImage &to,
		const EncodedStorage &from,
		const QSize &fromSize,
		FFmpeg::SwscalePointer &context) {
	if (!FFmpeg::GoodStorageForFrame(to, fromSize)) {
		to = FFmpeg::CreateFrameStorage(fromSize);
	}
	DecodeYUV2RGB(to, from, context);
	DecodeAlpha(to, from);
	FFmpeg::PremultiplyInplace(to);
}

void EncodeRGB2YUV(
		EncodedStorage &to,
		const QImage &from,
		FFmpeg::SwscalePointer &context) {
	context = FFmpeg::MakeSwscalePointer(
		from.size(),
		AV_PIX_FMT_BGRA,
		from.size(),
		AV_PIX_FMT_YUV420P,
		&context);
	Assert(context != nullptr);

	// AV_NUM_DATA_POINTERS defined in AVFrame struct
	const uint8_t *src[AV_NUM_DATA_POINTERS] = { from.bits(), nullptr };
	int srcLineSize[AV_NUM_DATA_POINTERS] = { from.bytesPerLine(), 0 };
	uint8_t *dst[AV_NUM_DATA_POINTERS] = {
		to.yData(),
		to.uData(),
		to.vData(),
		nullptr
	};
	int dstLineSize[AV_NUM_DATA_POINTERS] = {
		to.yBytesPerLine(),
		to.uBytesPerLine(),
		to.vBytesPerLine(),
		0
	};

	const auto lines = sws_scale(
		context.get(),
		src,
		srcLineSize,
		0,
		from.height(),
		dst,
		dstLineSize);

	Ensures(lines == from.height());
}

void EncodeAlpha(EncodedStorage &to, const QImage &from) {
	auto bytes = from.bits();
	auto alpha = to.aData();
	const auto perLine = from.bytesPerLine();
	const auto width = from.width();
	const auto height = from.height();
	for (auto i = 0; i != height; ++i) {
		auto ints = reinterpret_cast<const uint32*>(bytes);
		const auto till = ints + width;
		for (; ints != till; ints += 2) {
			*alpha++ = (((*ints) >> 24) & 0xF0U) | ((*(ints + 1)) >> 28);
		}
		bytes += perLine;
	}
}

void Encode(
		EncodedStorage &to,
		const QImage &from,
		QImage &cache,
		FFmpeg::SwscalePointer &context) {
	FFmpeg::UnPremultiply(cache, from);
	EncodeRGB2YUV(to, cache, context);
	EncodeAlpha(to, cache);
}

int YLineSize(int width) {
	return ((width + kAlignStorage - 1) / kAlignStorage) * kAlignStorage;
}

int UVLineSize(int width) {
	return (((width / 2) + kAlignStorage - 1) / kAlignStorage) * kAlignStorage;
}

int YSize(int width, int height) {
	return YLineSize(width) * height;
}

int UVSize(int width, int height) {
	return UVLineSize(width) * (height / 2);
}

int ASize(int width, int height) {
	return (width * height) / 2;
}

} // namespace

void EncodedStorage::allocate(int width, int height) {
	Expects((width % 2) == 0 && (height % 2) == 0);

	if (YSize(width, height) != YSize(_width, _height)
		|| UVSize(width, height) != UVSize(_width, _height)
		|| ASize(width, height) != ASize(_width, _height)) {
		_width = width;
		_height = height;
		reallocate();
	}
}

void EncodedStorage::reallocate() {
	const auto total = YSize(_width, _height)
		+ 2 * UVSize(_width, _height)
		+ ASize(_width, _height);
	_data = QByteArray(total + kAlignStorage - 1, 0);
}

int EncodedStorage::width() const {
	return _width;
}

int EncodedStorage::height() const {
	return _height;
}

int EncodedStorage::size() const {
	return YSize(_width, _height)
		+ 2 * UVSize(_width, _height)
		+ ASize(_width, _height);
}

char *EncodedStorage::data() {
	const auto result = reinterpret_cast<quintptr>(_data.data());
	return reinterpret_cast<char*>(kAlignStorage
		* ((result + kAlignStorage - 1) / kAlignStorage));
}

const char *EncodedStorage::data() const {
	const auto result = reinterpret_cast<quintptr>(_data.data());
	return reinterpret_cast<const char*>(kAlignStorage
		* ((result + kAlignStorage - 1) / kAlignStorage));
}

uint8_t *EncodedStorage::yData() {
	return reinterpret_cast<uint8_t*>(data());
}

const uint8_t *EncodedStorage::yData() const {
	return reinterpret_cast<const uint8_t*>(data());
}

int EncodedStorage::yBytesPerLine() const {
	return YLineSize(_width);
}

uint8_t *EncodedStorage::uData() {
	return yData() + YSize(_width, _height);
}

const uint8_t *EncodedStorage::uData() const {
	return yData() + YSize(_width, _height);
}

int EncodedStorage::uBytesPerLine() const {
	return UVLineSize(_width);
}

uint8_t *EncodedStorage::vData() {
	return uData() + UVSize(_width, _height);
}

const uint8_t *EncodedStorage::vData() const {
	return uData() + UVSize(_width, _height);
}

int EncodedStorage::vBytesPerLine() const {
	return UVLineSize(_width);
}

uint8_t *EncodedStorage::aData() {
	return uData() + 2 * UVSize(_width, _height);
}

const uint8_t *EncodedStorage::aData() const {
	return uData() + 2 * UVSize(_width, _height);
}

int EncodedStorage::aBytesPerLine() const {
	return _width / 2;
}

Cache::Cache(
	const QByteArray &data,
	const FrameRequest &request,
	FnMut<void(QByteArray &&cached)> put)
: _data(data)
, _put(std::move(put)) {
	if (!readHeader(request)) {
		_framesReady = 0;
		_data = QByteArray();
	}
}

void Cache::init(
		QSize original,
		int frameRate,
		int framesCount,
		const FrameRequest &request) {
	_size = request.size(original);
	_original = original;
	_frameRate = frameRate;
	_framesCount = framesCount;
	_framesReady = 0;
	prepareBuffers();
}

int Cache::frameRate() const {
	return _frameRate;
}

int Cache::framesReady() const {
	return _framesReady;
}

int Cache::framesCount() const {
	return _framesCount;
}

QSize Cache::originalSize() const {
	return _original;
}

bool Cache::readHeader(const FrameRequest &request) {
	if (_data.isEmpty()) {
		return false;

	}
	QDataStream stream(&_data, QIODevice::ReadOnly);

	auto encoder = qint32(0);
	stream >> encoder;
	if (static_cast<Encoder>(encoder) != Encoder::YUV420A4_LZ4) {
		return false;
	}
	auto size = QSize();
	auto original = QSize();
	auto frameRate = qint32(0);
	auto framesCount = qint32(0);
	auto framesReady = qint32(0);
	stream
		>> size
		>> original
		>> frameRate
		>> framesCount
		>> framesReady;
	if (stream.status() != QDataStream::Ok
		|| original.isEmpty()
		|| (original.width() > kMaxSize)
		|| (original.height() > kMaxSize)
		|| (frameRate <= 0)
		|| (frameRate > kNormalFrameRate && frameRate != kMaxFrameRate)
		|| (framesCount <= 0)
		|| (framesCount > kMaxFramesCount)
		|| (framesReady <= 0)
		|| (framesReady > framesCount)
		|| request.size(original) != size) {
		return false;
	}
	_encoder = static_cast<Encoder>(encoder);
	_size = size;
	_original = original;
	_frameRate = frameRate;
	_framesCount = framesCount;
	_framesReady = framesReady;
	prepareBuffers();
	return renderFrame(_firstFrame, request, 0);
}

QImage Cache::takeFirstFrame() {
	return std::move(_firstFrame);
}

bool Cache::renderFrame(
		QImage &to,
		const FrameRequest &request,
		int index) {
	Expects(index >= _framesReady
		|| index == _offsetFrameIndex
		|| index == 0);

	if (index >= _framesReady) {
		return false;
	} else if (request.size(_original) != _size) {
		return false;
	} else if (index == 0) {
		_offset = headerSize();
		_offsetFrameIndex = 0;
	}
	const auto [ok, xored] = readCompressedFrame();
	if (!ok || (xored && index == 0)) {
		_framesReady = 0;
		_data = QByteArray();
		return false;
	} else if (index + 1 == _framesReady && _data.size() > _offset) {
		_data.resize(_offset);
	}
	if (xored) {
		Xor(_previous, _uncompressed);
	} else {
		std::swap(_uncompressed, _previous);
	}
	Decode(to, _previous, _size, _decodeContext);
	return true;
}

void Cache::appendFrame(
		const QImage &frame,
		const FrameRequest &request,
		int index) {
	if (request.size(_original) != _size) {
		_framesReady = 0;
		_data = QByteArray();
	}
	if (index != _framesReady) {
		return;
	}
	if (index == 0) {
		_size = request.size(_original);
		_encode = EncodeFields();
		_encode.compressedFrames.reserve(_framesCount);
		prepareBuffers();
	}
	Assert(frame.size() == _size);
	Encode(_uncompressed, frame, _encode.cache, _encode.context);
	CompressAndSwapFrame(
		_encode.compressBuffer,
		(index != 0) ? &_encode.xorCompressBuffer : nullptr,
		_uncompressed,
		_previous);
	const auto compressed = _encode.compressBuffer;
	const auto nowSize = (_data.isEmpty() ? headerSize() : _data.size())
		+ _encode.totalSize;
	const auto totalSize = nowSize + compressed.size();
	if (nowSize <= kMaxCacheSize && totalSize > kMaxCacheSize) {
		// Write to cache while we still can.
		finalizeEncoding();
	}
	_encode.totalSize += compressed.size();
	_encode.compressedFrames.push_back(compressed);
	_encode.compressedFrames.back().detach();
	if (++_framesReady == _framesCount) {
		finalizeEncoding();
	}
}

void Cache::finalizeEncoding() {
	if (_encode.compressedFrames.empty()) {
		return;
	}
	const auto size = (_data.isEmpty() ? headerSize() : _data.size())
		+ _encode.totalSize;
	if (_data.isEmpty()) {
		_data.reserve(size);
		writeHeader();
	} else {
		updateFramesReadyCount();
	}
	const auto offset = _data.size();
	_data.resize(size);
	auto to = _data.data() + offset;
	for (const auto &block : _encode.compressedFrames) {
		const auto amount = qint32(block.size());
		memcpy(to, block.data(), amount);
		to += amount;
	}
	if (_data.size() <= kMaxCacheSize) {
		_put(QByteArray(_data));
	}
	_encode = EncodeFields();
}

int Cache::headerSize() const {
	return 8 * sizeof(qint32);
}

void Cache::writeHeader() {
	Expects(_data.isEmpty());

	QDataStream stream(&_data, QIODevice::WriteOnly);

	stream
		<< static_cast<qint32>(_encoder)
		<< _size
		<< _original
		<< qint32(_frameRate)
		<< qint32(_framesCount)
		<< qint32(_framesReady);
}

void Cache::updateFramesReadyCount() {
	Expects(_data.size() >= headerSize());

	QDataStream stream(&_data, QIODevice::ReadWrite);
	stream.device()->seek(headerSize() - sizeof(qint32));
	stream << qint32(_framesReady);
}

void Cache::prepareBuffers() {
	// 12 bit per pixel in YUV420P.
	const auto bytesPerLine = _size.width();

	_uncompressed.allocate(bytesPerLine, _size.height());
	_previous.allocate(bytesPerLine, _size.height());
}

Cache::ReadResult Cache::readCompressedFrame() {
	if (_data.size() < _offset) {
		return { false };
	}
	auto length = qint32(0);
	const auto part = bytes::make_span(_data).subspan(_offset);
	if (part.size() < sizeof(length)) {
		return { false };
	}
	bytes::copy(
		bytes::object_as_span(&length),
		part.subspan(0, sizeof(length)));
	const auto bytes = part.subspan(sizeof(length));

	const auto xored = (length < 0);
	if (xored) {
		length = -length;
	}
	_offset += sizeof(length) + length;
	++_offsetFrameIndex;
	const auto ok = (length <= bytes.size())
		? UncompressToRaw(_uncompressed, bytes.subspan(0, length))
		: false;
	return { ok, xored };
}

Cache::~Cache() {
	finalizeEncoding();
}

} // namespace Lottie
