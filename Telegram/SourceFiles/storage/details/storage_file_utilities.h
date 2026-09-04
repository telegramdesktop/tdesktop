/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "storage/storage_account.h"

#include <QtCore/QBuffer>

namespace Storage {
namespace details {

[[nodiscard]] QString ToFilePart(FileKey val);
[[nodiscard]] bool KeyAlreadyUsed(QString &name);
[[nodiscard]] FileKey GenerateKey(const QString &basePath);
void ClearKey(const FileKey &key, const QString &basePath);

[[nodiscard]] bool CheckStreamStatus(QDataStream &stream);
[[nodiscard]] MTP::AuthKeyPtr CreateLocalKey(
	const QByteArray &passcode,
	const QByteArray &salt);
[[nodiscard]] MTP::AuthKeyPtr CreateLegacyLocalKey(
	const QByteArray &passcode,
	const QByteArray &salt);

struct PasscodeKdf final {
	quint32 kind = 0;
	quint32 memory = 0;
	quint32 time = 0;
	quint32 parallel = 0;

	[[nodiscard]] bool valid() const;
	[[nodiscard]] bool costWithinLimits() const;
};

inline constexpr auto kPasscodeKdfArgon2id = quint32(1);
inline constexpr auto kPasscodeKdfScrypt = quint32(2);
inline constexpr auto kPasscodeSaltMinSize = 8;

[[nodiscard]] PasscodeKdf DefaultPasscodeKdf();
[[nodiscard]] MTP::AuthKeyPtr CreatePasscodeKey(
	const QByteArray &passcode,
	const QByteArray &salt,
	const PasscodeKdf &kdf);

struct FileReadDescriptor final {
	~FileReadDescriptor();

	int32 version = 0;
	QByteArray data;
	QBuffer buffer;
	QDataStream stream;
};

struct EncryptedDescriptor final {
	EncryptedDescriptor();
	explicit EncryptedDescriptor(uint32 size);
	~EncryptedDescriptor();

	void finish();

	QByteArray data;
	QBuffer buffer;
	QDataStream stream;
};

[[nodiscard]] QByteArray PrepareEncrypted(
	EncryptedDescriptor &data,
	const MTP::AuthKeyPtr &key);

class FileWriteDescriptor final {
public:
	FileWriteDescriptor(
		const FileKey &key,
		const QString &basePath,
		bool sync = false);
	FileWriteDescriptor(
		const QString &name,
		const QString &basePath,
		bool sync = false);
	~FileWriteDescriptor();

	void writeData(const QByteArray &data);
	void writeEncrypted(
		EncryptedDescriptor &data,
		const MTP::AuthKeyPtr &key);

	// Commits the buffered bytes: sync descriptors report whether the
	// record durably reached disk, async writes are fire-and-forget and
	// report true. Idempotent - a repeated call (as from the destructor)
	// returns the remembered result.
	bool finish();

private:
	void init(const QString &name);

	const QString _basePath;
	QBuffer _buffer;
	QDataStream _stream;
	QByteArray _safeData;
	QString _base;
	HashMd5 _md5;
	int _fullSize = 0;
	bool _sync = false;
	bool _result = true;

};

bool ReadFile(
	FileReadDescriptor &result,
	const QString &name,
	const QString &basePath);

bool DecryptLocal(
	EncryptedDescriptor &result,
	const QByteArray &encrypted,
	const MTP::AuthKeyPtr &key);

bool ReadEncryptedFile(
	FileReadDescriptor &result,
	const QString &name,
	const QString &basePath,
	const MTP::AuthKeyPtr &key);

bool ReadEncryptedFile(
	FileReadDescriptor &result,
	const FileKey &fkey,
	const QString &basePath,
	const MTP::AuthKeyPtr &key);

void Sync();
void Finish();

} // namespace details
} // namespace Storage
