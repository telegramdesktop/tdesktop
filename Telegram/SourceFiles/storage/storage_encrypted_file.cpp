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

} // namespace

struct File::BasicHeader {
	bytes::array<kSaltSize> salt = { { bytes::type() } };
	Format format = Format::Format_0;
	uint32 reserved = 0;
	uint64 applicationVersion = 0;
	bytes::array<openssl::kSha256Size> checksum = { { bytes::type() } };
};

File::Result File::open(
		const QString &path,
		Mode mode,
		const EncryptionKey &key) {
	_data.setFileName(QFileInfo(path).absoluteFilePath());
	const auto result = attemptOpen(mode, key);
	if (result != Result::Success) {
		_state = base::none;
		_data.close();
		_lock.unlock();
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
	if (!_data.open(QIODevice::ReadWrite)) {
		return Result::Failed;
	} else if (!_lock.lock(_data)) {
		return Result::LockFailed;
	}
	const auto size = _data.size();
	if (!size) {
		return writeHeader(key) ? Result::Success : Result::Failed;
	}
	return readHeader(key);
}

File::Result File::attemptOpenForWrite(const EncryptionKey &key) {
	if (!_data.open(QIODevice::WriteOnly)) {
		return Result::Failed;
	} else if (!_lock.lock(_data)) {
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
	} else if (header.format != Format::Format_0) {
		return Result::Failed;
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

	_state->decrypt(bytes, _offset);
	_offset += bytes.size();
}

void File::encrypt(bytes::span bytes) {
	Expects(_state.has_value());

	_state->encrypt(bytes, _offset);
	_offset += bytes.size();
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

size_type File::write(bytes::span bytes) {
	Expects(bytes.size() % kBlockSize == 0);

	encrypt(bytes);
	auto count = writePlain(bytes);
	if (const auto back = (count % kBlockSize)) {
		if (!_data.seek(_data.pos() - back)) {
			return 0;
		}
		count -= back;
	}
	if (const auto back = (bytes.size() - count)) {
		_offset -= back;
		decrypt(bytes.subspan(count));
	}
	_data.flush();
	return count;
}

} // namespace Storage
