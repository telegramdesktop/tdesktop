/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/details/storage_file_utilities.h"

#include "core/version.h"
#include "mtproto/mtproto_auth_key.h"
#include "base/platform/base_platform_file_utilities.h"
#include "base/openssl_help.h"
#include "base/random.h"

#include <crl/crl_object_on_thread.h>
#include <openssl/core_names.h>
#include <openssl/kdf.h>
#include <openssl/opensslv.h>
#include <openssl/params.h>
#include <QtCore/QtEndian>
#include <QtCore/QSaveFile>

namespace Storage {
namespace details {
namespace {

constexpr char TdfMagic[] = { 'T', 'D', 'F', '$' };
constexpr auto TdfMagicLen = int(sizeof(TdfMagic));

constexpr auto kStrongIterationsCount = 100'000;

// The memory-hard passcode wrap targets at most roughly twice the full
// legacy 100,000-iteration PBKDF2-HMAC-SHA512 derivation in CreateLocalKey,
// including its prehash and 256-byte output. Paired measurements use the same
// machine and time window. Optimized OpenSSL calibration leaves conservative
// headroom with low-end devices in mind; timings still vary by device and
// library configuration. Windows x86 is calibrated separately with lower
// time and scrypt N costs. Keeping Argon2 at 64 MiB instead of 128 MiB leaves
// more contiguous-allocation headroom in a fragmented 32-bit address space.
// Parallelism stays at a single lane and a single thread, which lets OpenSSL
// run its single-threaded fill without a thread pool enabled in the library
// context. Every wrap records its family and these three numbers beside its
// own salt, so changing defaults only affects wraps written afterwards.
// Existing wraps use their recorded parameters. Independent read ceilings
// leave a compatibility margin above the shipped costs so later builds can
// raise the write parameters without making their files unreadable here.
constexpr auto kPasscodeArgon2MemoryKiB = quint32(65'536);
constexpr auto kPasscodeArgon2Lanes = quint32(1);
#if defined Q_OS_WIN && defined Q_PROCESSOR_X86_32
constexpr auto kPasscodeArgon2Time = quint32(4);
constexpr auto kPasscodeScryptN = quint32(65'536);
#else // Q_OS_WIN && Q_PROCESSOR_X86_32
constexpr auto kPasscodeArgon2Time = quint32(8);
constexpr auto kPasscodeScryptN = quint32(131'072);
#endif // Q_OS_WIN && Q_PROCESSOR_X86_32
constexpr auto kPasscodeArgon2MaxMemoryKiB = quint32(262'144);
constexpr auto kPasscodeArgon2MaxTime = quint32(64);
constexpr auto kPasscodeArgon2MaxLanes = quint32(16);
static_assert(kPasscodeArgon2MemoryKiB <= kPasscodeArgon2MaxMemoryKiB);
static_assert(kPasscodeArgon2Time <= kPasscodeArgon2MaxTime);
static_assert(kPasscodeArgon2Lanes <= kPasscodeArgon2MaxLanes);
constexpr auto kPasscodeScryptR = quint32(8);
constexpr auto kPasscodeScryptP = quint32(1);
constexpr auto kPasscodeScryptMaxMem = quint64(192) * 1024 * 1024;
#if OPENSSL_VERSION_NUMBER >= 0x30200000L
constexpr auto kPasscodeArgon2Name = "ARGON2ID";
#endif // OPENSSL_VERSION_NUMBER >= 0x30200000L

struct WriteEntry {
	QString basePath;
	QString base;
	QByteArray data;
	QByteArray md5;
};

class WriteManager final {
public:
	explicit WriteManager(crl::weak_on_thread<WriteManager> weak);

	void write(WriteEntry &&entry);
	[[nodiscard]] bool writeSync(WriteEntry &&entry);
	void writeSyncAll();

private:
	void scheduleWrite();
	void writeScheduled();
	bool writeOneScheduledNow();
	bool writeNow(WriteEntry &&entry);

	template <typename File>
	[[nodiscard]] bool open(File &file, const WriteEntry &entry, char postfix);

	[[nodiscard]] QString path(const WriteEntry &entry, char postfix) const;
	[[nodiscard]] bool writeHeader(
		const QString &basePath,
		QFileDevice &file);

	crl::weak_on_thread<WriteManager> _weak;
	std::deque<WriteEntry> _scheduled;

};

class AsyncWriteManager final {
public:
	void write(WriteEntry &&entry);
	[[nodiscard]] bool writeSync(WriteEntry &&entry);
	void sync();
	void stop();

private:
	std::optional<crl::object_on_thread<WriteManager>> _manager;
	bool _finished = false;

};

WriteManager::WriteManager(crl::weak_on_thread<WriteManager> weak)
: _weak(std::move(weak)) {
}

void WriteManager::write(WriteEntry &&entry) {
	const auto i = ranges::find(_scheduled, entry.base, &WriteEntry::base);
	if (i == end(_scheduled)) {
		_scheduled.push_back(std::move(entry));
	} else {
		*i = std::move(entry);
	}
	scheduleWrite();
}

bool WriteManager::writeSync(WriteEntry &&entry) {
	const auto i = ranges::find(_scheduled, entry.base, &WriteEntry::base);
	if (i != end(_scheduled)) {
		_scheduled.erase(i);
	}
	return writeNow(std::move(entry));
}

bool WriteManager::writeNow(WriteEntry &&entry) {
	const auto path = [&](char postfix) {
		return this->path(entry, postfix);
	};
	const auto open = [&](auto &file, char postfix) {
		return this->open(file, entry, postfix);
	};
	const auto write = [&](auto &file) {
		return file.write(entry.data) == entry.data.size()
			&& file.write(entry.md5) == entry.md5.size();
	};
	const auto safe = path('s');
	const auto simple = path('0');
	const auto backup = path('1');
	QSaveFile save;
	if (open(save, 's')) {
		const auto written = write(save);
		if (save.commit()) {
			QFile::remove(simple);
			QFile::remove(backup);
			const auto ok = written
				&& (save.error() == QFileDevice::NoError);
			if (!ok) {
				LOG(("Storage Error: Could not write '%1'.").arg(safe));
			}
			return ok;
		}
		LOG(("Storage Error: Could not commit '%1'.").arg(safe));
	}
	QFile plain;
	if (open(plain, '0')) {
		const auto written = write(plain);
		base::Platform::FlushFileData(plain);
		plain.close();

		const auto ok = written
			&& (plain.error() == QFileDevice::NoError);
		if (!ok) {
			LOG(("Storage Error: Could not write '%1'.").arg(simple));
			QFile::remove(simple);
			return false;
		}
		QFile::remove(backup);
		if (base::Platform::RenameWithOverwrite(simple, safe)) {
			return true;
		}
		QFile::remove(safe);
		LOG(("Storage Error: Could not rename '%1' to '%2', removing.").arg(
			simple,
			safe));
	}
	return false;
}

void WriteManager::writeSyncAll() {
	while (writeOneScheduledNow()) {
	}
}

bool WriteManager::writeOneScheduledNow() {
	if (_scheduled.empty()) {
		return false;
	}

	auto entry = std::move(_scheduled.front());
	_scheduled.pop_front();

	writeNow(std::move(entry));
	return true;
}

bool WriteManager::writeHeader(const QString &basePath, QFileDevice &file) {
	if (!file.open(QIODevice::WriteOnly)) {
		const auto dir = QDir(basePath);
		if (dir.exists()) {
			return false;
		} else if (!QDir().mkpath(dir.absolutePath())) {
			return false;
		} else if (!file.open(QIODevice::WriteOnly)) {
			return false;
		}
	}
	file.write(TdfMagic, TdfMagicLen);
	const auto version = qint32(AppVersion);
	file.write((const char*)&version, sizeof(version));
	return true;
}

QString WriteManager::path(const WriteEntry &entry, char postfix) const {
	return entry.base + postfix;
}

template <typename File>
bool WriteManager::open(File &file, const WriteEntry &entry, char postfix) {
	const auto name = path(entry, postfix);
	file.setFileName(name);
	if (!writeHeader(entry.basePath, file)) {
		LOG(("Storage Error: Could not open '%1' for writing.").arg(name));
		return false;
	}
	return true;
}

void WriteManager::scheduleWrite() {
	_weak.with([](WriteManager &that) {
		that.writeScheduled();
	});
}

void WriteManager::writeScheduled() {
	if (writeOneScheduledNow() && !_scheduled.empty()) {
		scheduleWrite();
	}
}

void AsyncWriteManager::write(WriteEntry &&entry) {
	Expects(!_finished);

	if (!_manager) {
		_manager.emplace();
	}
	_manager->with([entry = std::move(entry)](WriteManager &manager) mutable {
		manager.write(std::move(entry));
	});
}

bool AsyncWriteManager::writeSync(WriteEntry &&entry) {
	Expects(!_finished);

	if (!_manager) {
		_manager.emplace();
	}
	auto result = false;
	_manager->with_sync([&](WriteManager &manager) {
		result = manager.writeSync(std::move(entry));
	});
	return result;
}

void AsyncWriteManager::sync() {
	if (_manager) {
		_manager->with_sync([](WriteManager &manager) {
			manager.writeSyncAll();
		});
	}
}

void AsyncWriteManager::stop() {
	if (_manager) {
		sync();
		_manager.reset();
	}
	_finished = true;
}

AsyncWriteManager Manager;

} // namespace

QString ToFilePart(FileKey val) {
	QString result;
	result.reserve(0x10);
	for (int32 i = 0; i < 0x10; ++i) {
		uchar v = (val & 0x0F);
		result.push_back((v < 0x0A) ? QChar('0' + v) : QChar('A' + (v - 0x0A)));
		val >>= 4;
	}
	return result;
}

bool KeyAlreadyUsed(QString &name) {
	name += '0';
	if (QFileInfo::exists(name)) {
		return true;
	}
	name[name.size() - 1] = '1';
	if (QFileInfo::exists(name)) {
		return true;
	}
	name[name.size() - 1] = 's';
	if (QFileInfo::exists(name)) {
		return true;
	}
	return false;
}

FileKey GenerateKey(const QString &basePath) {
	FileKey result;
	QString path;
	path.reserve(basePath.size() + 0x11);
	path += basePath;
	do {
		result = base::RandomValue<FileKey>();
		path.resize(basePath.size());
		path += ToFilePart(result);
	} while (!result || KeyAlreadyUsed(path));

	return result;
}

void ClearKey(const FileKey &key, const QString &basePath) {
	QString name;
	name.reserve(basePath.size() + 0x11);
	name.append(basePath).append(ToFilePart(key)).append('0');
	QFile::remove(name);
	name[name.size() - 1] = '1';
	QFile::remove(name);
	name[name.size() - 1] = 's';
	QFile::remove(name);
}

bool CheckStreamStatus(QDataStream &stream) {
	if (stream.status() != QDataStream::Ok) {
		LOG(("Bad data stream status: %1").arg(stream.status()));
		return false;
	}
	return true;
}

MTP::AuthKeyPtr CreateLocalKey(
		const QByteArray &passcode,
		const QByteArray &salt) {
	const auto s = bytes::make_span(salt);
	const auto hash = openssl::Sha512(s, bytes::make_span(passcode), s);
	const auto iterationsCount = passcode.isEmpty()
		? 1 // Don't slow down for no password.
		: kStrongIterationsCount;

	auto key = MTP::AuthKey::Data{ { gsl::byte{} } };
	PKCS5_PBKDF2_HMAC(
		reinterpret_cast<const char*>(hash.data()),
		hash.size(),
		reinterpret_cast<const unsigned char*>(s.data()),
		s.size(),
		iterationsCount,
		EVP_sha512(),
		key.size(),
		reinterpret_cast<unsigned char*>(key.data()));
	return std::make_shared<MTP::AuthKey>(key);
}

MTP::AuthKeyPtr CreateLegacyLocalKey(
		const QByteArray &passcode,
		const QByteArray &salt) {
	auto key = MTP::AuthKey::Data{ { gsl::byte{} } };
	const auto iterationsCount = passcode.isEmpty()
		? LocalEncryptNoPwdIterCount // Don't slow down for no password.
		: LocalEncryptIterCount;

	PKCS5_PBKDF2_HMAC_SHA1(
		passcode.constData(),
		passcode.size(),
		(uchar*)salt.data(),
		salt.size(),
		iterationsCount,
		key.size(),
		(uchar*)key.data());

	return std::make_shared<MTP::AuthKey>(key);
}

bool PasscodeKdf::valid() const {
	if (!time || !parallel) {
		return false;
	} else if (kind == kPasscodeKdfArgon2id) {
		return (memory >= 8 * parallel);
	} else if (kind == kPasscodeKdfScrypt) {
		return (memory >= 2) && !(memory & (memory - 1));
	}
	return false;
}

bool PasscodeKdf::costWithinLimits() const {
	return (kind != kPasscodeKdfArgon2id)
		|| (memory <= kPasscodeArgon2MaxMemoryKiB
			&& time <= kPasscodeArgon2MaxTime
			&& parallel <= kPasscodeArgon2MaxLanes);
}

PasscodeKdf DefaultPasscodeKdf() {
#if OPENSSL_VERSION_NUMBER >= 0x30200000L
	static const auto argon2 = [] {
		const auto algorithm = EVP_KDF_fetch(
			nullptr,
			kPasscodeArgon2Name,
			nullptr);
		if (!algorithm) {
			return false;
		}
		EVP_KDF_free(algorithm);
		return true;
	}();
#else // OPENSSL_VERSION_NUMBER >= 0x30200000L
	constexpr auto argon2 = false;
#endif // OPENSSL_VERSION_NUMBER < 0x30200000L
	return argon2
		? PasscodeKdf{
			.kind = kPasscodeKdfArgon2id,
			.memory = kPasscodeArgon2MemoryKiB,
			.time = kPasscodeArgon2Time,
			.parallel = kPasscodeArgon2Lanes,
		} : PasscodeKdf{
			.kind = kPasscodeKdfScrypt,
			.memory = kPasscodeScryptN,
			.time = kPasscodeScryptR,
			.parallel = kPasscodeScryptP,
		};
}

MTP::AuthKeyPtr CreatePasscodeKey(
		const QByteArray &passcode,
		const QByteArray &salt,
		const PasscodeKdf &kdf) {
	if (passcode.isEmpty()
		|| salt.size() < kPasscodeSaltMinSize
		|| !kdf.costWithinLimits()
		|| !kdf.valid()) {
		return nullptr;
	}
	auto key = MTP::AuthKey::Data{ { gsl::byte{} } };
	const auto to = reinterpret_cast<unsigned char*>(key.data());
	if (kdf.kind == kPasscodeKdfArgon2id) {
#if OPENSSL_VERSION_NUMBER >= 0x30200000L
		const auto algorithm = EVP_KDF_fetch(
			nullptr,
			kPasscodeArgon2Name,
			nullptr);
		if (!algorithm) {
			LOG(("App Error: Argon2id is not available."));
			return nullptr;
		}
		const auto algorithmGuard = gsl::finally([&] {
			EVP_KDF_free(algorithm);
		});
		const auto context = EVP_KDF_CTX_new(algorithm);
		if (!context) {
			LOG(("App Error: Could not create the Argon2id context."));
			return nullptr;
		}
		const auto contextGuard = gsl::finally([&] {
			EVP_KDF_CTX_free(context);
		});
		auto memory = uint32_t(kdf.memory);
		auto time = uint32_t(kdf.time);
		auto lanes = uint32_t(kdf.parallel);
		auto threads = uint32_t(1);
		const auto params = std::array{
			OSSL_PARAM_construct_octet_string(
				OSSL_KDF_PARAM_PASSWORD,
				const_cast<char*>(passcode.constData()),
				size_t(passcode.size())),
			OSSL_PARAM_construct_octet_string(
				OSSL_KDF_PARAM_SALT,
				const_cast<char*>(salt.constData()),
				size_t(salt.size())),
			OSSL_PARAM_construct_uint32(
				OSSL_KDF_PARAM_ARGON2_MEMCOST,
				&memory),
			OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ITER, &time),
			OSSL_PARAM_construct_uint32(
				OSSL_KDF_PARAM_ARGON2_LANES,
				&lanes),
			OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_THREADS, &threads),
			OSSL_PARAM_construct_end(),
		};
		if (EVP_KDF_derive(context, to, key.size(), params.data()) != 1) {
			LOG(("App Error: Argon2id derivation failed."));
			return nullptr;
		}
#else // OPENSSL_VERSION_NUMBER >= 0x30200000L
		LOG(("App Error: Argon2id is not available."));
		return nullptr;
#endif // OPENSSL_VERSION_NUMBER < 0x30200000L
	} else if (kdf.kind == kPasscodeKdfScrypt) {
		if (!EVP_PBE_scrypt(
			passcode.constData(),
			size_t(passcode.size()),
			reinterpret_cast<const unsigned char*>(salt.constData()),
			size_t(salt.size()),
			uint64_t(kdf.memory),
			uint64_t(kdf.time),
			uint64_t(kdf.parallel),
			kPasscodeScryptMaxMem,
			to,
			key.size())) {
			LOG(("App Error: Scrypt derivation failed."));
			return nullptr;
		}
	} else {
		LOG(("App Error: Unknown passcode KDF family."));
		return nullptr;
	}
	return std::make_shared<MTP::AuthKey>(key);
}

FileReadDescriptor::~FileReadDescriptor() {
	if (version) {
		stream.setDevice(nullptr);
		if (buffer.isOpen()) {
			buffer.close();
		}
		buffer.setBuffer(nullptr);
	}
}

EncryptedDescriptor::EncryptedDescriptor() {
}

EncryptedDescriptor::EncryptedDescriptor(uint32 size) {
	uint32 fullSize = sizeof(uint32) + size;
	if (fullSize & 0x0F) fullSize += 0x10 - (fullSize & 0x0F);
	data.reserve(fullSize);

	data.resize(sizeof(uint32));
	buffer.setBuffer(&data);
	buffer.open(QIODevice::WriteOnly);
	buffer.seek(sizeof(uint32));
	stream.setDevice(&buffer);
	stream.setVersion(QDataStream::Qt_5_1);
}

EncryptedDescriptor::~EncryptedDescriptor() {
	finish();
}

void EncryptedDescriptor::finish() {
	if (stream.device()) stream.setDevice(nullptr);
	if (buffer.isOpen()) buffer.close();
	buffer.setBuffer(nullptr);
}

FileWriteDescriptor::FileWriteDescriptor(
	const FileKey &key,
	const QString &basePath,
	bool sync)
: FileWriteDescriptor(ToFilePart(key), basePath, sync) {
}

FileWriteDescriptor::FileWriteDescriptor(
	const QString &name,
	const QString &basePath,
	bool sync)
: _basePath(basePath)
, _sync(sync) {
	init(name);
}

FileWriteDescriptor::~FileWriteDescriptor() {
	finish();
}

void FileWriteDescriptor::init(const QString &name) {
	_base = _basePath + name;
	_buffer.setBuffer(&_safeData);
	const auto opened = _buffer.open(QIODevice::WriteOnly);
	Assert(opened);
	_stream.setDevice(&_buffer);
}

void FileWriteDescriptor::writeData(const QByteArray &data) {
	if (!_stream.device()) {
		return;
	}
	_stream << data;
	quint32 len = data.isNull() ? 0xffffffff : data.size();
	if (QSysInfo::ByteOrder != QSysInfo::BigEndian) {
		len = qbswap(len);
	}
	_md5.feed(&len, sizeof(len));
	_md5.feed(data.constData(), data.size());
	_fullSize += sizeof(len) + data.size();
}

void FileWriteDescriptor::writeEncrypted(
	EncryptedDescriptor &data,
	const MTP::AuthKeyPtr &key) {
	writeData(PrepareEncrypted(data, key));
}

bool FileWriteDescriptor::finish() {
	if (!_stream.device()) {
		return _result;
	}

	_stream.setDevice(nullptr);
	_md5.feed(&_fullSize, sizeof(_fullSize));
	qint32 version = AppVersion;
	_md5.feed(&version, sizeof(version));
	_md5.feed(TdfMagic, TdfMagicLen);

	_buffer.close();

	auto entry = WriteEntry{
		.basePath = _basePath,
		.base = _base,
		.data = _safeData,
		.md5 = QByteArray((const char*)_md5.result(), 0x10)
	};
	if (_sync) {
		_result = Manager.writeSync(std::move(entry));
	} else {
		Manager.write(std::move(entry));
	}
	return _result;
}

[[nodiscard]] QByteArray PrepareEncrypted(
		EncryptedDescriptor &data,
		const MTP::AuthKeyPtr &key) {
	data.finish();
	QByteArray &toEncrypt(data.data);

	// prepare for encryption
	uint32 size = toEncrypt.size(), fullSize = size;
	if (fullSize & 0x0F) {
		fullSize += 0x10 - (fullSize & 0x0F);
		toEncrypt.resize(fullSize);
		base::RandomFill(toEncrypt.data() + size, fullSize - size);
	}
	*(uint32*)toEncrypt.data() = size;
	QByteArray encrypted(0x10 + fullSize, Qt::Uninitialized); // 128bit of sha1 - key128, sizeof(data), data
	hashSha1(toEncrypt.constData(), toEncrypt.size(), encrypted.data());
	MTP::aesEncryptLocal(toEncrypt.constData(), encrypted.data() + 0x10, fullSize, key, encrypted.constData());

	return encrypted;
}

bool ReadFile(
		FileReadDescriptor &result,
		const QString &name,
		const QString &basePath) {
	const auto base = basePath + name;

	// detect order of read attempts
	QString toTry[2];
	const auto modern = base + 's';
	if (QFileInfo::exists(modern)) {
		toTry[0] = modern;
	} else {
		// Legacy way.
		toTry[0] = base + '0';
		QFileInfo toTry0(toTry[0]);
		if (toTry0.exists()) {
			toTry[1] = basePath + name + '1';
			QFileInfo toTry1(toTry[1]);
			if (toTry1.exists()) {
				QDateTime mod0 = toTry0.lastModified();
				QDateTime mod1 = toTry1.lastModified();
				if (mod0 < mod1) {
					std::swap(toTry[0], toTry[1]);
				}
			} else {
				toTry[1] = QString();
			}
		} else {
			toTry[0][toTry[0].size() - 1] = '1';
		}
	}
	for (int32 i = 0; i < 2; ++i) {
		QString fname(toTry[i]);
		if (fname.isEmpty()) break;

		QFile f(fname);
		if (!f.open(QIODevice::ReadOnly)) {
			DEBUG_LOG(("App Info: failed to open '%1' for reading"
				).arg(name));
			continue;
		}

		// check magic
		char magic[TdfMagicLen];
		if (f.read(magic, TdfMagicLen) != TdfMagicLen) {
			DEBUG_LOG(("App Info: failed to read magic from '%1'"
				).arg(name));
			continue;
		}
		if (memcmp(magic, TdfMagic, TdfMagicLen)) {
			DEBUG_LOG(("App Info: bad magic %1 in '%2'").arg(
				Logs::mb(magic, TdfMagicLen).str(),
				name));
			continue;
		}

		// read app version
		qint32 version;
		if (f.read((char*)&version, sizeof(version)) != sizeof(version)) {
			DEBUG_LOG(("App Info: failed to read version from '%1'"
				).arg(name));
			continue;
		}
		if (version > AppVersion) {
			DEBUG_LOG(("App Info: version too big %1 for '%2', my version %3"
				).arg(version
				).arg(name
				).arg(AppVersion));
			continue;
		}

		// read data
		QByteArray bytes = f.read(f.size());
		int32 dataSize = bytes.size() - 16;
		if (dataSize < 0) {
			DEBUG_LOG(("App Info: bad file '%1', could not read sign part"
				).arg(name));
			continue;
		}

		// check signature
		HashMd5 md5;
		md5.feed(bytes.constData(), dataSize);
		md5.feed(&dataSize, sizeof(dataSize));
		md5.feed(&version, sizeof(version));
		md5.feed(magic, TdfMagicLen);
		if (memcmp(md5.result(), bytes.constData() + dataSize, 16)) {
			DEBUG_LOG(("App Info: bad file '%1', signature did not match"
				).arg(name));
			continue;
		}

		bytes.resize(dataSize);
		result.data = bytes;
		bytes = QByteArray();

		result.version = version;
		result.buffer.setBuffer(&result.data);
		result.buffer.open(QIODevice::ReadOnly);
		result.stream.setDevice(&result.buffer);
		result.stream.setVersion(QDataStream::Qt_5_1);

		if ((i == 0 && !toTry[1].isEmpty()) || i == 1) {
			QFile::remove(toTry[1 - i]);
		}

		return true;
	}
	return false;
}

bool DecryptLocal(
		EncryptedDescriptor &result,
		const QByteArray &encrypted,
		const MTP::AuthKeyPtr &key) {
	if (encrypted.size() <= 16 || (encrypted.size() & 0x0F)) {
		LOG(("App Error: bad encrypted part size: %1").arg(encrypted.size()));
		return false;
	}
	uint32 fullLen = encrypted.size() - 16;

	QByteArray decrypted;
	decrypted.resize(fullLen);
	const char *encryptedKey = encrypted.constData(), *encryptedData = encrypted.constData() + 16;
	aesDecryptLocal(encryptedData, decrypted.data(), fullLen, key, encryptedKey);
	uchar sha1Buffer[20];
	if (memcmp(hashSha1(decrypted.constData(), decrypted.size(), sha1Buffer), encryptedKey, 16)) {
		LOG(("App Info: bad decrypt key, data not decrypted - incorrect password?"));
		return false;
	}

	uint32 dataLen = *(const uint32*)decrypted.constData();
	if (dataLen > uint32(decrypted.size()) || dataLen <= fullLen - 16 || dataLen < sizeof(uint32)) {
		LOG(("App Error: bad decrypted part size: %1, fullLen: %2, decrypted size: %3").arg(dataLen).arg(fullLen).arg(decrypted.size()));
		return false;
	}

	decrypted.resize(dataLen);
	result.data = decrypted;
	decrypted = QByteArray();

	result.buffer.setBuffer(&result.data);
	result.buffer.open(QIODevice::ReadOnly);
	result.buffer.seek(sizeof(uint32)); // skip len
	result.stream.setDevice(&result.buffer);
	result.stream.setVersion(QDataStream::Qt_5_1);

	return true;
}

bool ReadEncryptedFile(
		FileReadDescriptor &result,
		const QString &name,
		const QString &basePath,
		const MTP::AuthKeyPtr &key) {
	if (!ReadFile(result, name, basePath)) {
		return false;
	}
	QByteArray encrypted;
	result.stream >> encrypted;

	EncryptedDescriptor data;
	if (!DecryptLocal(data, encrypted, key)) {
		result.stream.setDevice(nullptr);
		if (result.buffer.isOpen()) result.buffer.close();
		result.buffer.setBuffer(nullptr);
		result.data = QByteArray();
		result.version = 0;
		return false;
	}

	result.stream.setDevice(0);
	if (result.buffer.isOpen()) {
		result.buffer.close();
	}
	result.buffer.setBuffer(0);
	result.data = data.data;
	result.buffer.setBuffer(&result.data);
	result.buffer.open(QIODevice::ReadOnly);
	result.buffer.seek(data.buffer.pos());
	result.stream.setDevice(&result.buffer);
	result.stream.setVersion(QDataStream::Qt_5_1);

	return true;
}

bool ReadEncryptedFile(
		FileReadDescriptor &result,
		const FileKey &fkey,
		const QString &basePath,
		const MTP::AuthKeyPtr &key) {
	return ReadEncryptedFile(result, ToFilePart(fkey), basePath, key);
}

void Sync() {
	Manager.sync();
}

void Finish() {
	Manager.stop();
}

} // namespace details
} // namespace Storage
