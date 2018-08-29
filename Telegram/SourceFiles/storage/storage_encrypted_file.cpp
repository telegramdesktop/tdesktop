/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/storage_encrypted_file.h"

#include "base/openssl_help.h"

namespace Storage {
namespace {

constexpr auto kBlockSize = CtrState::kBlockSize;

enum class Format : uint32 {
	Format_0,
};

struct BasicHeader {
	BasicHeader();

	void setFormat(Format format) {
		this->format = static_cast<uint32>(format);
	}
	Format getFormat() const {
		return static_cast<Format>(format);
	}

	bytes::array<kSaltSize> salt = { { bytes::type() } };
	uint32 format : 8;
	uint32 reserved1 : 24;
	uint32 reserved2 = 0;
	uint64 applicationVersion = 0;
	bytes::array<openssl::kSha256Size> checksum = { { bytes::type() } };
};

BasicHeader::BasicHeader()
: format(static_cast<uint32>(Format::Format_0))
, reserved1(0) {
}

} // namespace

File::Result File::open(
		const QString &path,
		Mode mode,
		const EncryptionKey &key) {
	close();

	const auto info = QFileInfo(path);
	const auto dir = info.absoluteDir();
	if (mode != Mode::Read && !dir.exists()) {
		if (!QDir().mkpath(dir.absolutePath())) {
			return Result::Failed;
		}
	}

	_data.setFileName(info.absoluteFilePath());
	const auto result = attemptOpen(mode, key);
	if (result != Result::Success) {
		close();
	}
	return result;

	static_assert(sizeof(BasicHeader) == kSaltSize
		+ sizeof(uint64) * 2
		+ openssl::kSha256Size, "Unexpected paddings in the header.");
	static_assert(
		(sizeof(BasicHeader) - kSaltSize) % kBlockSize == 0,
		"Not way to encrypt the header.");
}

File::Result File::attemptOpen(Mode mode, const EncryptionKey &key) {
	switch (mode) {
	case Mode::Read: return attemptOpenForRead(key);
	case Mode::ReadAppend: return attemptOpenForReadAppend(key);
	case Mode::Write: return attemptOpenForWrite(key);
	}
	Unexpected("Mode in Storage::File::attemptOpen.");
}

File::Result File::attemptOpenForRead(const EncryptionKey &key) {
	if (!_data.open(QIODevice::ReadOnly)) {
		return Result::Failed;
	}
	return readHeader(key);
}

File::Result File::attemptOpenForReadAppend(const EncryptionKey &key) {
	if (!_lock.lock(_data, QIODevice::ReadWrite)) {
		return Result::LockFailed;
	}
	const auto size = _data.size();
	if (!size) {
		return writeHeader(key) ? Result::Success : Result::Failed;
	}
	return readHeader(key);
}

File::Result File::attemptOpenForWrite(const EncryptionKey &key) {
	if (!_lock.lock(_data, QIODevice::WriteOnly)) {
		return Result::LockFailed;
	}
	return writeHeader(key) ? Result::Success : Result::Failed;
}

bool File::writeHeader(const EncryptionKey &key) {
	Expects(!_state.has_value());
	Expects(_data.pos() == 0);

	const auto magic = bytes::make_span("TDEF");
	if (!writePlain(magic.subspan(0, FileLock::kSkipBytes))) {
		return false;
	}

	auto header = BasicHeader();
	bytes::set_random(header.salt);
	_state = key.prepareCtrState(header.salt);

	const auto headerBytes = bytes::object_as_span(&header);
	const auto checkSize = headerBytes.size() - header.checksum.size();
	bytes::copy(
		header.checksum,
		openssl::Sha256(
			key.data(),
			headerBytes.subspan(0, checkSize)));

	if (writePlain(header.salt) != header.salt.size()) {
		return false;
	} else if (!write(headerBytes.subspan(header.salt.size()))) {
		return false;
	}
	_dataSize = 0;
	return true;
}

File::Result File::readHeader(const EncryptionKey &key) {
	Expects(!_state.has_value());
	Expects(_data.pos() == 0);

	if (!_data.seek(FileLock::kSkipBytes)) {
		return Result::Failed;
	}
	auto header = BasicHeader();
	const auto headerBytes = bytes::object_as_span(&header);
	if (readPlain(headerBytes) != headerBytes.size()) {
		return Result::Failed;
	}
	_state = key.prepareCtrState(header.salt);
	decrypt(headerBytes.subspan(header.salt.size()));

	const auto checkSize = headerBytes.size() - header.checksum.size();
	const auto checksum = openssl::Sha256(
		key.data(),
		headerBytes.subspan(0, checkSize));
	if (bytes::compare(header.checksum, checksum) != 0) {
		return Result::WrongKey;
	} else if (header.getFormat() != Format::Format_0) {
		return Result::Failed;
	}
	_dataSize = _data.size()
		- int64(sizeof(BasicHeader))
		- FileLock::kSkipBytes;
	Assert(_dataSize >= 0);
	if (const auto bad = (_dataSize % kBlockSize)) {
		_dataSize -= bad;
	}
	return Result::Success;
}

size_type File::readPlain(bytes::span bytes) {
	return _data.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
}

size_type File::writePlain(bytes::const_span bytes) {
	return _data.write(
		reinterpret_cast<const char*>(bytes.data()),
		bytes.size());
}

void File::decrypt(bytes::span bytes) {
	Expects(_state.has_value());

	_state->decrypt(bytes, _encryptionOffset);
	_encryptionOffset += bytes.size();
}

void File::encrypt(bytes::span bytes) {
	Expects(_state.has_value());

	_state->encrypt(bytes, _encryptionOffset);
	_encryptionOffset += bytes.size();
}

size_type File::read(bytes::span bytes) {
	Expects(bytes.size() % kBlockSize == 0);

	auto count = readPlain(bytes);
	if (const auto back = -(count % kBlockSize)) {
		if (!_data.seek(_data.pos() + back)) {
			return 0;
		}
		count += back;
	}
	if (count) {
		decrypt(bytes.subspan(0, count));
	}
	return count;
}

bool File::write(bytes::span bytes) {
	Expects(bytes.size() % kBlockSize == 0);

	if (!isOpen()) {
		return false;
	}
	encrypt(bytes);
	const auto count = writePlain(bytes);
	if (count == bytes.size()) {
		_dataSize = std::max(_dataSize, offset());
	} else {
		decryptBack(bytes);
		if (count > 0) {
			_data.seek(_data.pos() - count);
		}
		return false;
	}
	return true;
}

void File::decryptBack(bytes::span bytes) {
	Expects(_encryptionOffset >= bytes.size());

	_encryptionOffset -= bytes.size();
	decrypt(bytes);
	_encryptionOffset -= bytes.size();
}

size_type File::readWithPadding(bytes::span bytes) {
	const auto size = bytes.size();
	const auto part = size % kBlockSize;
	const auto good = size - part;
	if (good) {
		const auto succeed = read(bytes.subspan(0, good));
		if (succeed != good) {
			return succeed;
		}
	}
	if (!part) {
		return good;
	}
	auto storage = bytes::array<kBlockSize>();
	const auto padded = bytes::make_span(storage);
	const auto succeed = read(padded);
	if (!succeed) {
		return good;
	}
	Assert(succeed == kBlockSize);
	bytes::copy(bytes.subspan(good), padded.subspan(0, part));
	return size;
}

bool File::writeWithPadding(bytes::span bytes) {
	const auto size = bytes.size();
	const auto part = size % kBlockSize;
	const auto good = size - part;
	if (good && !write(bytes.subspan(0, good))) {
		return false;
	}
	if (!part) {
		return true;
	}
	auto storage = bytes::array<kBlockSize>();
	const auto padded = bytes::make_span(storage);
	bytes::copy(padded, bytes.subspan(good));
	bytes::set_random(padded.subspan(part));
	if (write(padded)) {
		return true;
	}
	if (good) {
		decryptBack(bytes.subspan(0, good));
		_data.seek(_data.pos() - good);
	}
	return false;
}

bool File::flush() {
	return _data.flush();
}

void File::close() {
	_lock.unlock();
	_data.close();
	_data.setFileName(QString());
	_dataSize = _encryptionOffset = 0;
	_state = base::none;
}

bool File::isOpen() const {
	return _data.isOpen();
}

int64 File::size() const {
	return _dataSize;
}

int64 File::offset() const {
	const auto realOffset = kSaltSize + _encryptionOffset;
	const auto skipOffset = sizeof(BasicHeader);
	return (realOffset >= skipOffset) ? (realOffset - skipOffset) : 0;
}

bool File::seek(int64 offset) {
	const auto realOffset = sizeof(BasicHeader) + offset;
	if (offset < 0 || offset > _dataSize) {
		return false;
	} else if (!_data.seek(FileLock::kSkipBytes + realOffset)) {
		return false;
	}
	_encryptionOffset = realOffset - kSaltSize;
	return true;
}

bool File::Move(const QString &from, const QString &to) {
	QFile source(from);
	if (!source.exists()) {
		return false;
	}
	QFile destination(to);
	if (destination.exists()) {
		{
			FileLock locker;
			if (!locker.lock(destination, QIODevice::WriteOnly)) {
				return false;
			}
		}
		destination.close();
		if (!destination.remove()) {
			return false;
		}
	}
	return source.rename(to);
}


} // namespace Storage
