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
	bool write(bytes::span bytes);

	size_type readWithPadding(bytes::span bytes);
	bool writeWithPadding(bytes::span bytes);

	bool flush();

	bool isOpen() const;
	int64 size() const;
	int64 offset() const;
	bool seek(int64 offset);

	void close();

	static bool Move(const QString &from, const QString &to);

private:
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
	void decryptBack(bytes::span bytes);

	QFile _data;
	FileLock _lock;
	int64 _encryptionOffset = 0;
	int64 _dataSize = 0;

	std::optional<CtrState> _state;

};

} // namespace Storage
