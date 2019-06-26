/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lottie/lottie_cache.h"

#include "lottie/lottie_frame_renderer.h"
#include "base/bytes.h"

#include <QDataStream>

namespace Lottie {
namespace {

constexpr auto kAlignStorage = 16;

bool UncompressToRaw(AlignedStorage &to, bytes::const_span from) {
	if (from.empty() || from.size() > to.rawSize()) {
		return false;
	} else if (from.size() == to.rawSize()) {
		memcpy(to.raw(), from.data(), from.size());
		return true;
	} else {
		// #TODO stickers
		return false;
	}
}

void Decode(QImage &to, const AlignedStorage &from, const QSize &fromSize) {
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
	auto from = static_cast<char*>(raw());
	auto to = static_cast<char*>(aligned());
	for (auto i = 0; i != _lines; ++i) {
		memcpy(from, to, fromPerLine);
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
	auto from = static_cast<char*>(aligned());
	auto to = static_cast<char*>(raw());
	for (auto i = 0; i != _lines; ++i) {
		memcpy(from, to, toPerLine);
		from += fromPerLine;
		to += toPerLine;
	}
}

CacheState::CacheState(const QByteArray &data, QSize box)
: _data(data) {
	if (!readHeader(box)) {
		_framesReady = 0;
	}
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

bool CacheState::readHeader(QSize box) {
	if (_data.isEmpty()) {
		return false;

	}
	QDataStream stream(&_data, QIODevice::ReadOnly);

	auto encoder = uchar(0);
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
		|| FrameRequest{ box }.size(original) != size) {
		return false;
	}
	_size = size;
	_original = original;
	_frameRate = frameRate;
	_framesCount = framesCount;
	_framesReady = framesReady;
	prepareBuffers();
	if (!readCompressedDelta(stream.device()->pos())) {
		return false;
	}
	_uncompressed.copyRawToAligned();
	std::swap(_uncompressed, _previous);
	Decode(_firstFrame, _previous, _size);
	return true;
}

QImage CacheState::takeFirstFrame() {
	return std::move(_firstFrame);
}

void CacheState::prepareBuffers() {
	_uncompressed.allocate(_size.width() * 4, _size.height());
}

int CacheState::uncompressedDeltaSize() const {
	return _size.width() * _size.height() * 4; // #TODO stickers
}

bool CacheState::readCompressedDelta(int offset) {
	auto length = qint32(0);
	const auto part = bytes::make_span(_data).subspan(offset);
	if (part.size() < sizeof(length)) {
		return false;
	}
	bytes::copy(bytes::object_as_span(&length), part);
	const auto bytes = part.subspan(sizeof(length));
	const auto uncompressedSize = uncompressedDeltaSize();

	_offset = offset + length;
	return (length <= bytes.size())
		? UncompressToRaw(_uncompressed, bytes.subspan(0, length))
		: false;
}

} // namespace Lottie
