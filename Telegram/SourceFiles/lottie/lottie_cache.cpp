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
#include <range/v3/numeric/accumulate.hpp>

namespace Lottie {
namespace {

constexpr auto kAlignStorage = 16;

bool UncompressToRaw(AlignedStorage &to, bytes::const_span from) {
	if (from.empty() || from.size() > to.rawSize()) {
		return false;
	} else if (from.size() == to.rawSize()) {
		memcpy(to.raw(), from.data(), from.size());
		return true;
	}
	const auto result = LZ4_decompress_safe(
		reinterpret_cast<const char*>(from.data()),
		static_cast<char*>(to.raw()),
		from.size(),
		to.rawSize());
	return (result == to.rawSize());
}

void CompressFromRaw(QByteArray &to, const AlignedStorage &from) {
	const auto size = from.rawSize();
	const auto max = LZ4_compressBound(size);
	to.reserve(max);
	to.resize(max);
	const auto compressed = LZ4_compress_default(
		static_cast<const char*>(from.raw()),
		to.data(),
		size,
		to.size());
	Assert(compressed > 0);
	if (compressed >= size) {
		to.resize(size);
		memcpy(to.data(), from.raw(), size);
	} else {
		to.resize(compressed);
	}
}

void Decode(QImage &to, AlignedStorage &from, const QSize &fromSize) {
	from.copyRawToAligned();
	if (!FFmpeg::GoodStorageForFrame(to, fromSize)) {
		to = FFmpeg::CreateFrameStorage(fromSize);
	}
	auto fromBytes = static_cast<const char*>(from.aligned());
	auto toBytes = to.bits();
	const auto fromPerLine = from.bytesPerLine();
	const auto toPerLine = to.bytesPerLine();
	for (auto i = 0; i != to.height(); ++i) {
		memcpy(toBytes, fromBytes, to.width() * 4);
		fromBytes += fromPerLine;
		toBytes += toPerLine;
	}
}

void Encode(AlignedStorage &to, const QImage &from, const QSize &toSize) {
	auto fromBytes = from.bits();
	auto toBytes = static_cast<char*>(to.aligned());
	const auto fromPerLine = from.bytesPerLine();
	const auto toPerLine = to.bytesPerLine();
	for (auto i = 0; i != to.lines(); ++i) {
		memcpy(toBytes, fromBytes, from.width() * 4);
		fromBytes += fromPerLine;
		toBytes += toPerLine;
	}
	to.copyAlignedToRaw();
}

void Xor(AlignedStorage &to, const AlignedStorage &from) {
	Expects(to.rawSize() == from.rawSize());

	using Block = std::conditional_t<
		sizeof(void*) == sizeof(uint64),
		uint64,
		uint32>;
	constexpr auto kBlockSize = sizeof(Block);
	const auto amount = from.rawSize();
	const auto fromBytes = reinterpret_cast<const uchar*>(from.raw());
	const auto toBytes = reinterpret_cast<uchar*>(to.raw());
	const auto skip = reinterpret_cast<quintptr>(toBytes) % kBlockSize;
	const auto blocks = (amount - skip) / kBlockSize;
	for (auto i = 0; i != skip; ++i) {
		toBytes[i] ^= fromBytes[i];
	}
	const auto fromBlocks = reinterpret_cast<const Block*>(fromBytes + skip);
	const auto toBlocks = reinterpret_cast<Block*>(toBytes + skip);
	for (auto i = 0; i != blocks; ++i) {
		toBlocks[i] ^= fromBlocks[i];
	}
	const auto left = amount - skip - (blocks * kBlockSize);
	for (auto i = amount - left; i != amount; ++i) {
		toBytes[i] ^= fromBytes[i];
	}
}

} // namespace

void AlignedStorage::allocate(int packedBytesPerLine, int lines) {
	Expects(packedBytesPerLine >= 0);
	Expects(lines >= 0);

	_packedBytesPerLine = packedBytesPerLine;
	_lines = lines;
	reallocate();
}

void AlignedStorage::reallocate() {
	const auto perLine = bytesPerLine();
	const auto total = perLine * _lines;
	_buffer = QByteArray(total + kAlignStorage - 1, Qt::Uninitialized);
	_raw = (perLine != _packedBytesPerLine)
		? QByteArray(_packedBytesPerLine * _lines, Qt::Uninitialized)
		: QByteArray();
}

int AlignedStorage::lines() const {
	return _lines;
}

int AlignedStorage::rawSize() const {
	return _lines * _packedBytesPerLine;
}

void *AlignedStorage::raw() {
	return (bytesPerLine() == _packedBytesPerLine) ? aligned() : _raw.data();
}

const void *AlignedStorage::raw() const {
	return (bytesPerLine() == _packedBytesPerLine) ? aligned() : _raw.data();
}

int AlignedStorage::bytesPerLine() const {
	return kAlignStorage
		* ((_packedBytesPerLine + kAlignStorage - 1) / kAlignStorage);
}

void *AlignedStorage::aligned() {
	const auto result = reinterpret_cast<quintptr>(_buffer.data());
	return reinterpret_cast<void*>(kAlignStorage
		* ((result + kAlignStorage - 1) / kAlignStorage));
}

const void *AlignedStorage::aligned() const {
	const auto result = reinterpret_cast<quintptr>(_buffer.data());
	return reinterpret_cast<void*>(kAlignStorage
		* ((result + kAlignStorage - 1) / kAlignStorage));
}

void AlignedStorage::copyRawToAligned() {
	const auto fromPerLine = _packedBytesPerLine;
	const auto toPerLine = bytesPerLine();
	if (fromPerLine == toPerLine) {
		return;
	}
	auto from = static_cast<const char*>(raw());
	auto to = static_cast<char*>(aligned());
	for (auto i = 0; i != _lines; ++i) {
		memcpy(to, from, fromPerLine);
		from += fromPerLine;
		to += toPerLine;
	}
}

void AlignedStorage::copyAlignedToRaw() {
	const auto fromPerLine = bytesPerLine();
	const auto toPerLine = _packedBytesPerLine;
	if (fromPerLine == toPerLine) {
		return;
	}
	auto from = static_cast<const char*>(aligned());
	auto to = static_cast<char*>(raw());
	for (auto i = 0; i != _lines; ++i) {
		memcpy(to, from, toPerLine);
		from += fromPerLine;
		to += toPerLine;
	}
}

CacheState::CacheState(const QByteArray &data, const FrameRequest &request)
: _data(data) {
	if (!readHeader(request)) {
		_framesReady = 0;
		_data = QByteArray();
	}
}

void CacheState::init(
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

int CacheState::frameRate() const {
	return _frameRate;
}

int CacheState::framesReady() const {
	return _framesReady;
}

int CacheState::framesCount() const {
	return _framesCount;
}

QSize CacheState::originalSize() const {
	return _original;
}

bool CacheState::readHeader(const FrameRequest &request) {
	if (_data.isEmpty()) {
		return false;

	}
	QDataStream stream(&_data, QIODevice::ReadOnly);

	auto encoder = quint8(0);
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
		|| (frameRate > kMaxFrameRate)
		|| (framesCount <= 0)
		|| (framesCount > kMaxFramesCount)
		|| (framesReady <= 0)
		|| (framesReady > framesCount)
		|| request.size(original) != size) {
		return false;
	}
	_size = size;
	_original = original;
	_frameRate = frameRate;
	_framesCount = framesCount;
	_framesReady = framesReady;
	prepareBuffers();
	return renderFrame(_firstFrame, request, 0);
}

QImage CacheState::takeFirstFrame() {
	return std::move(_firstFrame);
}

bool CacheState::renderFrame(
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
	if (!readCompressedDelta()) {
		_framesReady = 0;
		_data = QByteArray();
		return false;
	}
	if (index == 0) {
		std::swap(_uncompressed, _previous);
	} else {
		Xor(_previous, _uncompressed);
	}
	Decode(to, _previous, _size);
	return true;
}

void CacheState::appendFrame(
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
		_compressedFrames.reserve(_framesCount);
		prepareBuffers();
	}
	Encode(_uncompressed, frame, _size);
	if (index == 0) {
		writeCompressedDelta();
		std::swap(_uncompressed, _previous);
	} else {
		std::swap(_uncompressed, _previous);
		Xor(_uncompressed, _previous);
		writeCompressedDelta();
	}
	if (++_framesReady == _framesCount) {
		finalizeEncoding();
	}
}

void CacheState::finalizeEncoding() {
	const auto size = (_data.isEmpty() ? headerSize() : _data.size())
		+ (_compressedFrames.size() * sizeof(qint32))
		+ ranges::accumulate(
			_compressedFrames,
			0,
			std::plus(),
			&QByteArray::size);
	if (_data.isEmpty()) {
		_data.reserve(size);
		writeHeader();
	}
	const auto offset = _data.size();
	_data.resize(size);
	auto to = _data.data() + offset;
	for (const auto &block : _compressedFrames) {
		const auto amount = qint32(block.size());
		memcpy(to, &amount, sizeof(qint32));
		to += sizeof(qint32);
		memcpy(to, block.data(), amount);
		to += amount;
	}
	_compressedFrames.clear();
	_compressedFrames.shrink_to_fit();
	_compressBuffer = QByteArray();
}

int CacheState::headerSize() const {
	return 8 * sizeof(qint32);
}

void CacheState::writeHeader() {
	Expects(_data.isEmpty());

	QDataStream stream(&_data, QIODevice::WriteOnly);

	stream
		<< static_cast<qint32>(Encoder::YUV420A4_LZ4)
		<< _size
		<< _original
		<< qint32(_frameRate)
		<< qint32(_framesCount)
		<< qint32(_framesReady);
}

void CacheState::writeCompressedDelta() {
	CompressFromRaw(_compressBuffer, _uncompressed);
	_compressedFrames.push_back(_compressBuffer);
	_compressedFrames.back().detach();
}

void CacheState::prepareBuffers() {
	_uncompressed.allocate(_size.width() * 4, _size.height());
	_previous.allocate(_size.width() * 4, _size.height());
}

bool CacheState::readCompressedDelta() {
	auto length = qint32(0);
	const auto part = bytes::make_span(_data).subspan(_offset);
	if (part.size() < sizeof(length)) {
		return false;
	}
	bytes::copy(
		bytes::object_as_span(&length),
		part.subspan(0, sizeof(length)));
	const auto bytes = part.subspan(sizeof(length));

	_offset += sizeof(length) + length;
	++_offsetFrameIndex;
	return (length <= bytes.size())
		? UncompressToRaw(_uncompressed, bytes.subspan(0, length))
		: false;
}

} // namespace Lottie
