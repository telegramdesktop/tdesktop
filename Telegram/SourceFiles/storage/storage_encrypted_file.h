/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "storage/storage_file_lock.h"
#include "storage/storage_encryption.h"
#include "base/bytes.h"
#include "base/optional.h"

namespace Storage {

class File {
public:
	enum class Mode {
		Read,
		ReadAppend,
		Write,
	};
	enum class Result {
		Failed,
		LockFailed,
		WrongKey,
		Success,
	};
	Result open(const QString &path, Mode mode, const EncryptionKey &key);

	size_type read(bytes::span bytes);
	size_type write(bytes::span bytes);

	void close();

private:
	enum class Format : uint32 {
		Format_0,
	};
	struct BasicHeader;

	Result attemptOpen(Mode mode, const EncryptionKey &key);
	Result attemptOpenForRead(const EncryptionKey &key);
	Result attemptOpenForReadAppend(const EncryptionKey &key);
	Result attemptOpenForWrite(const EncryptionKey &key);

	bool writeHeader(const EncryptionKey &key);
	Result readHeader(const EncryptionKey &key);

	size_type readPlain(bytes::span bytes);
	size_type writePlain(bytes::const_span bytes);
	void decrypt(bytes::span bytes);
	void encrypt(bytes::span bytes);

	QFile _data;
	FileLock _lock;
	index_type _offset = 0;

	base::optional<CtrState> _state;

};

} // namespace Storage
