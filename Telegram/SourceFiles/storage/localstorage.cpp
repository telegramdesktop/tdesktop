/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/localstorage.h"

#include "storage/serialize_document.h"
#include "storage/serialize_common.h"
#include "chat_helpers/stickers.h"
#include "data/data_drafts.h"
#include "boxes/send_files_box.h"
#include "window/themes/window_theme.h"
#include "export/export_settings.h"
#include "core/crash_reports.h"
#include "core/update_checker.h"
#include "observer_peer.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "lang/lang_keys.h"
#include "media/media_audio.h"
#include "mtproto/dc_options.h"
#include "messenger.h"
#include "application.h"
#include "apiwrap.h"
#include "auth_session.h"
#include "window/window_controller.h"
#include "base/flags.h"
#include "data/data_session.h"
#include "history/history.h"

extern "C" {
#include <openssl/evp.h>
} // extern "C"

namespace Local {
namespace {

constexpr auto kThemeFileSizeLimit = 5 * 1024 * 1024;
constexpr auto kFileLoaderQueueStopTimeout = TimeMs(5000);
constexpr auto kDefaultStickerInstallDate = TimeId(1);
constexpr auto kProxyTypeShift = 1024;

using FileKey = quint64;

constexpr char tdfMagic[] = { 'T', 'D', 'F', '$' };
constexpr auto tdfMagicLen = int(sizeof(tdfMagic));

QString toFilePart(FileKey val) {
	QString result;
	result.reserve(0x10);
	for (int32 i = 0; i < 0x10; ++i) {
		uchar v = (val & 0x0F);
		result.push_back((v < 0x0A) ? ('0' + v) : ('A' + (v - 0x0A)));
		val >>= 4;
	}
	return result;
}

QString _basePath, _userBasePath;

bool _started = false;
internal::Manager *_manager = nullptr;
TaskQueue *_localLoader = nullptr;

bool _working() {
	return _manager && !_basePath.isEmpty();
}

bool _userWorking() {
	return _manager && !_basePath.isEmpty() && !_userBasePath.isEmpty();
}

enum class FileOption {
	User = (1 << 0),
	Safe = (1 << 1),
};
using FileOptions = base::flags<FileOption>;
inline constexpr auto is_flag_type(FileOption) { return true; };

bool keyAlreadyUsed(QString &name, FileOptions options = FileOption::User | FileOption::Safe) {
	name += '0';
	if (QFileInfo(name).exists()) return true;
	if (options & (FileOption::Safe)) {
		name[name.size() - 1] = '1';
		return QFileInfo(name).exists();
	}
	return false;
}

FileKey genKey(FileOptions options = FileOption::User | FileOption::Safe) {
	if (options & FileOption::User) {
		if (!_userWorking()) return 0;
	} else {
		if (!_working()) return 0;
	}

	FileKey result;
	QString base = (options & FileOption::User) ? _userBasePath : _basePath, path;
	path.reserve(base.size() + 0x11);
	path += base;
	do {
		result = rand_value<FileKey>();
		path.resize(base.size());
		path += toFilePart(result);
	} while (!result || keyAlreadyUsed(path, options));

	return result;
}

void clearKey(const FileKey &key, FileOptions options = FileOption::User | FileOption::Safe) {
	if (options & FileOption::User) {
		if (!_userWorking()) return;
	} else {
		if (!_working()) return;
	}

	QString base = (options & FileOption::User) ? _userBasePath : _basePath, name;
	name.reserve(base.size() + 0x11);
	name.append(base).append(toFilePart(key)).append('0');
	QFile::remove(name);
	if (options & FileOption::Safe) {
		name[name.size() - 1] = '1';
		QFile::remove(name);
	}
}

bool _checkStreamStatus(QDataStream &stream) {
	if (stream.status() != QDataStream::Ok) {
		LOG(("Bad data stream status: %1").arg(stream.status()));
		return false;
	}
	return true;
}

QByteArray _settingsSalt, _passKeySalt, _passKeyEncrypted;

constexpr auto kLocalKeySize = MTP::AuthKey::kSize;

auto OldKey = MTP::AuthKeyPtr();
auto SettingsKey = MTP::AuthKeyPtr();
auto PassKey = MTP::AuthKeyPtr();
auto LocalKey = MTP::AuthKeyPtr();

void createLocalKey(const QByteArray &pass, QByteArray *salt, MTP::AuthKeyPtr *result) {
	auto key = MTP::AuthKey::Data { { gsl::byte{} } };
	auto iterCount = pass.size() ? LocalEncryptIterCount : LocalEncryptNoPwdIterCount; // dont slow down for no password
	auto newSalt = QByteArray();
	if (!salt) {
		newSalt.resize(LocalEncryptSaltSize);
		memset_rand(newSalt.data(), newSalt.size());
		salt = &newSalt;

		cSetLocalSalt(newSalt);
	}

	PKCS5_PBKDF2_HMAC_SHA1(pass.constData(), pass.size(), (uchar*)salt->data(), salt->size(), iterCount, key.size(), (uchar*)key.data());

	*result = std::make_shared<MTP::AuthKey>(key);
}

struct FileReadDescriptor {
	int32 version = 0;
	QByteArray data;
	QBuffer buffer;
	QDataStream stream;
	~FileReadDescriptor() {
		if (version) {
			stream.setDevice(0);
			if (buffer.isOpen()) buffer.close();
			buffer.setBuffer(0);
		}
	}
};

struct EncryptedDescriptor {
	EncryptedDescriptor() {
	}
	EncryptedDescriptor(uint32 size) {
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
	QByteArray data;
	QBuffer buffer;
	QDataStream stream;
	void finish() {
		if (stream.device()) stream.setDevice(0);
		if (buffer.isOpen()) buffer.close();
		buffer.setBuffer(0);
	}
	~EncryptedDescriptor() {
		finish();
	}
};

struct FileWriteDescriptor {
	FileWriteDescriptor(const FileKey &key, FileOptions options = FileOption::User | FileOption::Safe) {
		init(toFilePart(key), options);
	}
	FileWriteDescriptor(const QString &name, FileOptions options = FileOption::User | FileOption::Safe) {
		init(name, options);
	}
	void init(const QString &name, FileOptions options) {
		if (options & FileOption::User) {
			if (!_userWorking()) return;
		} else {
			if (!_working()) return;
		}

		// detect order of read attempts and file version
		QString toTry[2];
		toTry[0] = ((options & FileOption::User) ? _userBasePath : _basePath) + name + '0';
		if (options & FileOption::Safe) {
			toTry[1] = ((options & FileOption::User) ? _userBasePath : _basePath) + name + '1';
			QFileInfo toTry0(toTry[0]);
			QFileInfo toTry1(toTry[1]);
			if (toTry0.exists()) {
				if (toTry1.exists()) {
					QDateTime mod0 = toTry0.lastModified(), mod1 = toTry1.lastModified();
					if (mod0 > mod1) {
						qSwap(toTry[0], toTry[1]);
					}
				} else {
					qSwap(toTry[0], toTry[1]);
				}
				toDelete = toTry[1];
			} else if (toTry1.exists()) {
				toDelete = toTry[1];
			}
		}

		file.setFileName(toTry[0]);
		if (file.open(QIODevice::WriteOnly)) {
			file.write(tdfMagic, tdfMagicLen);
			qint32 version = AppVersion;
			file.write((const char*)&version, sizeof(version));

			stream.setDevice(&file);
			stream.setVersion(QDataStream::Qt_5_1);
		}
	}
	bool writeData(const QByteArray &data) {
		if (!file.isOpen()) return false;

		stream << data;
		quint32 len = data.isNull() ? 0xffffffff : data.size();
		if (QSysInfo::ByteOrder != QSysInfo::BigEndian) {
			len = qbswap(len);
		}
		md5.feed(&len, sizeof(len));
		md5.feed(data.constData(), data.size());
		dataSize += sizeof(len) + data.size();

		return true;
	}
	static QByteArray prepareEncrypted(EncryptedDescriptor &data, const MTP::AuthKeyPtr &key = LocalKey) {
		data.finish();
		QByteArray &toEncrypt(data.data);

		// prepare for encryption
		uint32 size = toEncrypt.size(), fullSize = size;
		if (fullSize & 0x0F) {
			fullSize += 0x10 - (fullSize & 0x0F);
			toEncrypt.resize(fullSize);
			memset_rand(toEncrypt.data() + size, fullSize - size);
		}
		*(uint32*)toEncrypt.data() = size;
		QByteArray encrypted(0x10 + fullSize, Qt::Uninitialized); // 128bit of sha1 - key128, sizeof(data), data
		hashSha1(toEncrypt.constData(), toEncrypt.size(), encrypted.data());
		MTP::aesEncryptLocal(toEncrypt.constData(), encrypted.data() + 0x10, fullSize, key, encrypted.constData());

		return encrypted;
	}
	bool writeEncrypted(EncryptedDescriptor &data, const MTP::AuthKeyPtr &key = LocalKey) {
		return writeData(prepareEncrypted(data, key));
	}
	void finish() {
		if (!file.isOpen()) return;

		stream.setDevice(0);

		md5.feed(&dataSize, sizeof(dataSize));
		qint32 version = AppVersion;
		md5.feed(&version, sizeof(version));
		md5.feed(tdfMagic, tdfMagicLen);
		file.write((const char*)md5.result(), 0x10);
		file.close();

		if (!toDelete.isEmpty()) {
			QFile::remove(toDelete);
		}
	}
	QFile file;
	QDataStream stream;

	QString toDelete;

	HashMd5 md5;
	int32 dataSize = 0;

	~FileWriteDescriptor() {
		finish();
	}
};

bool readFile(FileReadDescriptor &result, const QString &name, FileOptions options = FileOption::User | FileOption::Safe) {
	if (options & FileOption::User) {
		if (!_userWorking()) return false;
	} else {
		if (!_working()) return false;
	}

	// detect order of read attempts
	QString toTry[2];
	toTry[0] = ((options & FileOption::User) ? _userBasePath : _basePath) + name + '0';
	if (options & FileOption::Safe) {
		QFileInfo toTry0(toTry[0]);
		if (toTry0.exists()) {
			toTry[1] = ((options & FileOption::User) ? _userBasePath : _basePath) + name + '1';
			QFileInfo toTry1(toTry[1]);
			if (toTry1.exists()) {
				QDateTime mod0 = toTry0.lastModified(), mod1 = toTry1.lastModified();
				if (mod0 < mod1) {
					qSwap(toTry[0], toTry[1]);
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
			DEBUG_LOG(("App Info: failed to open '%1' for reading").arg(name));
			continue;
		}

		// check magic
		char magic[tdfMagicLen];
		if (f.read(magic, tdfMagicLen) != tdfMagicLen) {
			DEBUG_LOG(("App Info: failed to read magic from '%1'").arg(name));
			continue;
		}
		if (memcmp(magic, tdfMagic, tdfMagicLen)) {
			DEBUG_LOG(("App Info: bad magic %1 in '%2'").arg(Logs::mb(magic, tdfMagicLen).str()).arg(name));
			continue;
		}

		// read app version
		qint32 version;
		if (f.read((char*)&version, sizeof(version)) != sizeof(version)) {
			DEBUG_LOG(("App Info: failed to read version from '%1'").arg(name));
			continue;
		}
		if (version > AppVersion) {
			DEBUG_LOG(("App Info: version too big %1 for '%2', my version %3").arg(version).arg(name).arg(AppVersion));
			continue;
		}

		// read data
		QByteArray bytes = f.read(f.size());
		int32 dataSize = bytes.size() - 16;
		if (dataSize < 0) {
			DEBUG_LOG(("App Info: bad file '%1', could not read sign part").arg(name));
			continue;
		}

		// check signature
		HashMd5 md5;
		md5.feed(bytes.constData(), dataSize);
		md5.feed(&dataSize, sizeof(dataSize));
		md5.feed(&version, sizeof(version));
		md5.feed(magic, tdfMagicLen);
		if (memcmp(md5.result(), bytes.constData() + dataSize, 16)) {
			DEBUG_LOG(("App Info: bad file '%1', signature did not match").arg(name));
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

bool decryptLocal(EncryptedDescriptor &result, const QByteArray &encrypted, const MTP::AuthKeyPtr &key = LocalKey) {
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

bool readEncryptedFile(FileReadDescriptor &result, const QString &name, FileOptions options = FileOption::User | FileOption::Safe, const MTP::AuthKeyPtr &key = LocalKey) {
	if (!readFile(result, name, options)) {
		return false;
	}
	QByteArray encrypted;
	result.stream >> encrypted;

	EncryptedDescriptor data;
	if (!decryptLocal(data, encrypted, key)) {
		result.stream.setDevice(0);
		if (result.buffer.isOpen()) result.buffer.close();
		result.buffer.setBuffer(0);
		result.data = QByteArray();
		result.version = 0;
		return false;
	}

	result.stream.setDevice(0);
	if (result.buffer.isOpen()) result.buffer.close();
	result.buffer.setBuffer(0);
	result.data = data.data;
	result.buffer.setBuffer(&result.data);
	result.buffer.open(QIODevice::ReadOnly);
	result.buffer.seek(data.buffer.pos());
	result.stream.setDevice(&result.buffer);
	result.stream.setVersion(QDataStream::Qt_5_1);

	return true;
}

bool readEncryptedFile(FileReadDescriptor &result, const FileKey &fkey, FileOptions options = FileOption::User | FileOption::Safe, const MTP::AuthKeyPtr &key = LocalKey) {
	return readEncryptedFile(result, toFilePart(fkey), options, key);
}

FileKey _dataNameKey = 0;

enum { // Local Storage Keys
	lskUserMap = 0x00,
	lskDraft = 0x01, // data: PeerId peer
	lskDraftPosition = 0x02, // data: PeerId peer
	lskImages = 0x03, // data: StorageKey location
	lskLocations = 0x04, // no data
	lskStickerImages = 0x05, // data: StorageKey location
	lskAudios = 0x06, // data: StorageKey location
	lskRecentStickersOld = 0x07, // no data
	lskBackground = 0x08, // no data
	lskUserSettings = 0x09, // no data
	lskRecentHashtagsAndBots = 0x0a, // no data
	lskStickersOld = 0x0b, // no data
	lskSavedPeers = 0x0c, // no data
	lskReportSpamStatuses = 0x0d, // no data
	lskSavedGifsOld = 0x0e, // no data
	lskSavedGifs = 0x0f, // no data
	lskStickersKeys = 0x10, // no data
	lskTrustedBots = 0x11, // no data
	lskFavedStickers = 0x12, // no data
	lskExportSettings = 0x13, // no data
};

enum {
	dbiKey = 0x00,
	dbiUser = 0x01,
	dbiDcOptionOldOld = 0x02,
	dbiChatSizeMax = 0x03,
	dbiMutePeer = 0x04,
	dbiSendKey = 0x05,
	dbiAutoStart = 0x06,
	dbiStartMinimized = 0x07,
	dbiSoundNotify = 0x08,
	dbiWorkMode = 0x09,
	dbiSeenTrayTooltip = 0x0a,
	dbiDesktopNotify = 0x0b,
	dbiAutoUpdate = 0x0c,
	dbiLastUpdateCheck = 0x0d,
	dbiWindowPosition = 0x0e,
	dbiConnectionTypeOld = 0x0f,
	// 0x10 reserved
	dbiDefaultAttach = 0x11,
	dbiCatsAndDogs = 0x12,
	dbiReplaceEmoji = 0x13,
	dbiAskDownloadPath = 0x14,
	dbiDownloadPathOld = 0x15,
	dbiScale = 0x16,
	dbiEmojiTabOld = 0x17,
	dbiRecentEmojiOldOld = 0x18,
	dbiLoggedPhoneNumber = 0x19,
	dbiMutedPeers = 0x1a,
	// 0x1b reserved
	dbiNotifyView = 0x1c,
	dbiSendToMenu = 0x1d,
	dbiCompressPastedImage = 0x1e,
	dbiLangOld = 0x1f,
	dbiLangFileOld = 0x20,
	dbiTileBackground = 0x21,
	dbiAutoLock = 0x22,
	dbiDialogLastPath = 0x23,
	dbiRecentEmojiOld = 0x24,
	dbiEmojiVariantsOld = 0x25,
	dbiRecentStickers = 0x26,
	dbiDcOptionOld = 0x27,
	dbiTryIPv6 = 0x28,
	dbiSongVolume = 0x29,
	dbiWindowsNotificationsOld = 0x30,
	dbiIncludeMuted = 0x31,
	dbiMegagroupSizeMax = 0x32,
	dbiDownloadPath = 0x33,
	dbiAutoDownload = 0x34,
	dbiSavedGifsLimit = 0x35,
	dbiShowingSavedGifsOld = 0x36,
	dbiAutoPlay = 0x37,
	dbiAdaptiveForWide = 0x38,
	dbiHiddenPinnedMessages = 0x39,
	dbiRecentEmoji = 0x3a,
	dbiEmojiVariants = 0x3b,
	dbiDialogsMode = 0x40,
	dbiModerateMode = 0x41,
	dbiVideoVolume = 0x42,
	dbiStickersRecentLimit = 0x43,
	dbiNativeNotifications = 0x44,
	dbiNotificationsCount  = 0x45,
	dbiNotificationsCorner = 0x46,
	dbiThemeKey = 0x47,
	dbiDialogsWidthRatioOld = 0x48,
	dbiUseExternalVideoPlayer = 0x49,
	dbiDcOptions = 0x4a,
	dbiMtpAuthorization = 0x4b,
	dbiLastSeenWarningSeenOld = 0x4c,
	dbiAuthSessionSettings = 0x4d,
	dbiLangPackKey = 0x4e,
	dbiConnectionType = 0x4f,
	dbiStickersFavedLimit = 0x50,
	dbiSuggestStickersByEmoji = 0x51,
	dbiSuggestEmoji = 0x52,
	dbiTxtDomainString = 0x53,

	dbiEncryptedWithSalt = 333,
	dbiEncrypted = 444,

	// 500-600 reserved

	dbiVersion = 666,
};

enum {
	dbictAuto = 0,
	dbictHttpAuto = 1, // not used
	dbictHttpProxy = 2,
	dbictTcpProxy = 3,
	dbictProxiesList = 4,
};

typedef QMap<PeerId, FileKey> DraftsMap;
DraftsMap _draftsMap, _draftCursorsMap;
typedef QMap<PeerId, bool> DraftsNotReadMap;
DraftsNotReadMap _draftsNotReadMap;

typedef QPair<FileKey, qint32> FileDesc; // file, size

typedef QMultiMap<MediaKey, FileLocation> FileLocations;
FileLocations _fileLocations;
typedef QPair<MediaKey, FileLocation> FileLocationPair;
typedef QMap<QString, FileLocationPair> FileLocationPairs;
FileLocationPairs _fileLocationPairs;
typedef QMap<MediaKey, MediaKey> FileLocationAliases;
FileLocationAliases _fileLocationAliases;
typedef QMap<QString, FileDesc> WebFilesMap;
WebFilesMap _webFilesMap;
uint64 _storageWebFilesSize = 0;
FileKey _locationsKey = 0, _reportSpamStatusesKey = 0, _trustedBotsKey = 0;

using TrustedBots = OrderedSet<uint64>;
TrustedBots _trustedBots;
bool _trustedBotsRead = false;

FileKey _recentStickersKeyOld = 0;
FileKey _installedStickersKey = 0, _featuredStickersKey = 0, _recentStickersKey = 0, _favedStickersKey = 0, _archivedStickersKey = 0;
FileKey _savedGifsKey = 0;

FileKey _backgroundKey = 0;
bool _backgroundWasRead = false;
bool _backgroundCanWrite = true;

FileKey _themeKey = 0;
QString _themeAbsolutePath;
QString _themePaletteAbsolutePath;

bool _readingUserSettings = false;
FileKey _userSettingsKey = 0;
FileKey _recentHashtagsAndBotsKey = 0;
bool _recentHashtagsAndBotsWereRead = false;

FileKey _exportSettingsKey = 0;

FileKey _savedPeersKey = 0;
FileKey _langPackKey = 0;

typedef QMap<StorageKey, FileDesc> StorageMap;
StorageMap _imagesMap, _stickerImagesMap, _audiosMap;
qint64 _storageImagesSize = 0, _storageStickersSize = 0, _storageAudiosSize = 0;

bool _mapChanged = false;
int32 _oldMapVersion = 0, _oldSettingsVersion = 0;

enum class WriteMapWhen {
	Now,
	Fast,
	Soon,
};

std::unique_ptr<AuthSessionSettings> StoredAuthSessionCache;
AuthSessionSettings &GetStoredAuthSessionCache() {
	if (!StoredAuthSessionCache) {
		StoredAuthSessionCache = std::make_unique<AuthSessionSettings>();
	}
	return *StoredAuthSessionCache;
}

void _writeMap(WriteMapWhen when = WriteMapWhen::Soon);

void _writeLocations(WriteMapWhen when = WriteMapWhen::Soon) {
	if (when != WriteMapWhen::Now) {
		_manager->writeLocations(when == WriteMapWhen::Fast);
		return;
	}
	if (!_working()) return;

	_manager->writingLocations();
	if (_fileLocations.isEmpty() && _webFilesMap.isEmpty()) {
		if (_locationsKey) {
			clearKey(_locationsKey);
			_locationsKey = 0;
			_mapChanged = true;
			_writeMap();
		}
	} else {
		if (!_locationsKey) {
			_locationsKey = genKey();
			_mapChanged = true;
			_writeMap(WriteMapWhen::Fast);
		}
		quint32 size = 0;
		for (FileLocations::const_iterator i = _fileLocations.cbegin(), e = _fileLocations.cend(); i != e; ++i) {
			// location + type + namelen + name
			size += sizeof(quint64) * 2 + sizeof(quint32) + Serialize::stringSize(i.value().name());
			if (AppVersion > 9013) {
				// bookmark
				size += Serialize::bytearraySize(i.value().bookmark());
			}
			// date + size
			size += Serialize::dateTimeSize() + sizeof(quint32);
		}

		//end mark
		size += sizeof(quint64) * 2 + sizeof(quint32) + Serialize::stringSize(QString());
		if (AppVersion > 9013) {
			size += Serialize::bytearraySize(QByteArray());
		}
		size += Serialize::dateTimeSize() + sizeof(quint32);

		size += sizeof(quint32); // aliases count
		for (FileLocationAliases::const_iterator i = _fileLocationAliases.cbegin(), e = _fileLocationAliases.cend(); i != e; ++i) {
			// alias + location
			size += sizeof(quint64) * 2 + sizeof(quint64) * 2;
		}

		size += sizeof(quint32); // web files count
		for (WebFilesMap::const_iterator i = _webFilesMap.cbegin(), e = _webFilesMap.cend(); i != e; ++i) {
			// url + filekey + size
			size += Serialize::stringSize(i.key()) + sizeof(quint64) + sizeof(qint32);
		}

		EncryptedDescriptor data(size);
		auto legacyTypeField = 0;
		for (FileLocations::const_iterator i = _fileLocations.cbegin(); i != _fileLocations.cend(); ++i) {
			data.stream << quint64(i.key().first) << quint64(i.key().second) << quint32(legacyTypeField) << i.value().name();
			if (AppVersion > 9013) {
				data.stream << i.value().bookmark();
			}
			data.stream << i.value().modified << quint32(i.value().size);
		}

		data.stream << quint64(0) << quint64(0) << quint32(0) << QString();
		if (AppVersion > 9013) {
			data.stream << QByteArray();
		}
		data.stream << QDateTime::currentDateTime() << quint32(0);

		data.stream << quint32(_fileLocationAliases.size());
		for (FileLocationAliases::const_iterator i = _fileLocationAliases.cbegin(), e = _fileLocationAliases.cend(); i != e; ++i) {
			data.stream << quint64(i.key().first) << quint64(i.key().second) << quint64(i.value().first) << quint64(i.value().second);
		}

		data.stream << quint32(_webFilesMap.size());
		for (WebFilesMap::const_iterator i = _webFilesMap.cbegin(), e = _webFilesMap.cend(); i != e; ++i) {
			data.stream << i.key() << quint64(i.value().first) << qint32(i.value().second);
		}

		FileWriteDescriptor file(_locationsKey);
		file.writeEncrypted(data);
	}
}

void _readLocations() {
	FileReadDescriptor locations;
	if (!readEncryptedFile(locations, _locationsKey)) {
		clearKey(_locationsKey);
		_locationsKey = 0;
		_writeMap();
		return;
	}

	bool endMarkFound = false;
	while (!locations.stream.atEnd()) {
		quint64 first, second;
		QByteArray bookmark;
		FileLocation loc;
		quint32 legacyTypeField = 0;
		locations.stream >> first >> second >> legacyTypeField >> loc.fname;
		if (locations.version > 9013) {
			locations.stream >> bookmark;
		}
		locations.stream >> loc.modified >> loc.size;
		loc.setBookmark(bookmark);

		if (!first && !second && !legacyTypeField && loc.fname.isEmpty() && !loc.size) { // end mark
			endMarkFound = true;
			break;
		}

		MediaKey key(first, second);

		_fileLocations.insert(key, loc);
		_fileLocationPairs.insert(loc.fname, FileLocationPair(key, loc));
	}

	if (endMarkFound) {
		quint32 cnt;
		locations.stream >> cnt;
		for (quint32 i = 0; i < cnt; ++i) {
			quint64 kfirst, ksecond, vfirst, vsecond;
			locations.stream >> kfirst >> ksecond >> vfirst >> vsecond;
			_fileLocationAliases.insert(MediaKey(kfirst, ksecond), MediaKey(vfirst, vsecond));
		}

		if (!locations.stream.atEnd()) {
			_storageWebFilesSize = 0;
			_webFilesMap.clear();

			quint32 webLocationsCount;
			locations.stream >> webLocationsCount;
			for (quint32 i = 0; i < webLocationsCount; ++i) {
				QString url;
				quint64 key;
				qint32 size;
				locations.stream >> url >> key >> size;
				_webFilesMap.insert(url, FileDesc(key, size));
				_storageWebFilesSize += size;
			}
		}
	}
}

void _writeReportSpamStatuses() {
	if (!_working()) return;

	if (cReportSpamStatuses().isEmpty()) {
		if (_reportSpamStatusesKey) {
			clearKey(_reportSpamStatusesKey);
			_reportSpamStatusesKey = 0;
			_mapChanged = true;
			_writeMap();
		}
	} else {
		if (!_reportSpamStatusesKey) {
			_reportSpamStatusesKey = genKey();
			_mapChanged = true;
			_writeMap(WriteMapWhen::Fast);
		}
		const ReportSpamStatuses &statuses(cReportSpamStatuses());

		quint32 size = sizeof(qint32);
		for (ReportSpamStatuses::const_iterator i = statuses.cbegin(), e = statuses.cend(); i != e; ++i) {
			// peer + status
			size += sizeof(quint64) + sizeof(qint32);
		}

		EncryptedDescriptor data(size);
		data.stream << qint32(statuses.size());
		for (ReportSpamStatuses::const_iterator i = statuses.cbegin(), e = statuses.cend(); i != e; ++i) {
			data.stream << quint64(i.key()) << qint32(i.value());
		}

		FileWriteDescriptor file(_reportSpamStatusesKey);
		file.writeEncrypted(data);
	}
}

void _readReportSpamStatuses() {
	FileReadDescriptor statuses;
	if (!readEncryptedFile(statuses, _reportSpamStatusesKey)) {
		clearKey(_reportSpamStatusesKey);
		_reportSpamStatusesKey = 0;
		_writeMap();
		return;
	}

	ReportSpamStatuses &map(cRefReportSpamStatuses());
	map.clear();

	qint32 size = 0;
	statuses.stream >> size;
	for (int32 i = 0; i < size; ++i) {
		quint64 peer = 0;
		qint32 status = 0;
		statuses.stream >> peer >> status;
		map.insert(peer, DBIPeerReportSpamStatus(status));
	}
}

struct ReadSettingsContext {
	int legacyLanguageId = Lang::kLegacyLanguageNone;
	QString legacyLanguageFile;
	MTP::DcOptions dcOptions;
};

void applyReadContext(ReadSettingsContext &&context) {
	Messenger::Instance().dcOptions()->addFromOther(std::move(context.dcOptions));
	if (context.legacyLanguageId != Lang::kLegacyLanguageNone) {
		Lang::Current().fillFromLegacy(context.legacyLanguageId, context.legacyLanguageFile);
		writeLangPack();
	}
}

bool _readSetting(quint32 blockId, QDataStream &stream, int version, ReadSettingsContext &context) {
	switch (blockId) {
	case dbiDcOptionOldOld: {
		quint32 dcId, port;
		QString host, ip;
		stream >> dcId >> host >> ip >> port;
		if (!_checkStreamStatus(stream)) return false;

		context.dcOptions.constructAddOne(
			dcId,
			0,
			ip.toStdString(),
			port,
			{});
	} break;

	case dbiDcOptionOld: {
		quint32 dcIdWithShift, port;
		qint32 flags;
		QString ip;
		stream >> dcIdWithShift >> flags >> ip >> port;
		if (!_checkStreamStatus(stream)) return false;

		context.dcOptions.constructAddOne(
			dcIdWithShift,
			MTPDdcOption::Flags::from_raw(flags),
			ip.toStdString(),
			port,
			{});
	} break;

	case dbiDcOptions: {
		auto serialized = QByteArray();
		stream >> serialized;
		if (!_checkStreamStatus(stream)) return false;

		context.dcOptions.constructFromSerialized(serialized);
	} break;

	case dbiChatSizeMax: {
		qint32 maxSize;
		stream >> maxSize;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetChatSizeMax(maxSize);
	} break;

	case dbiSavedGifsLimit: {
		qint32 limit;
		stream >> limit;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetSavedGifsLimit(limit);
	} break;

	case dbiStickersRecentLimit: {
		qint32 limit;
		stream >> limit;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetStickersRecentLimit(limit);
	} break;

	case dbiStickersFavedLimit: {
		qint32 limit;
		stream >> limit;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetStickersFavedLimit(limit);
	} break;

	case dbiMegagroupSizeMax: {
		qint32 maxSize;
		stream >> maxSize;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetMegagroupSizeMax(maxSize);
	} break;

	case dbiUser: {
		quint32 dcId;
		qint32 userId;
		stream >> userId >> dcId;
		if (!_checkStreamStatus(stream)) return false;

		DEBUG_LOG(("MTP Info: user found, dc %1, uid %2").arg(dcId).arg(userId));
		Messenger::Instance().setMtpMainDcId(dcId);
		Messenger::Instance().setAuthSessionUserId(userId);
	} break;

	case dbiKey: {
		qint32 dcId;
		stream >> dcId;
		auto key = Serialize::read<MTP::AuthKey::Data>(stream);
		if (!_checkStreamStatus(stream)) return false;

		Messenger::Instance().setMtpKey(dcId, key);
	} break;

	case dbiMtpAuthorization: {
		auto serialized = QByteArray();
		stream >> serialized;
		if (!_checkStreamStatus(stream)) return false;

		Messenger::Instance().setMtpAuthorization(serialized);
	} break;

	case dbiAutoStart: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		cSetAutoStart(v == 1);
	} break;

	case dbiStartMinimized: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		cSetStartMinimized(v == 1);
	} break;

	case dbiSendToMenu: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		cSetSendToMenu(v == 1);
	} break;

	case dbiUseExternalVideoPlayer: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		cSetUseExternalVideoPlayer(v == 1);
	} break;

	case dbiSoundNotify: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetSoundNotify(v == 1);
	} break;

	case dbiAutoDownload: {
		qint32 photo, audio, gif;
		stream >> photo >> audio >> gif;
		if (!_checkStreamStatus(stream)) return false;

		cSetAutoDownloadPhoto(photo);
		cSetAutoDownloadAudio(audio);
		cSetAutoDownloadGif(gif);
	} break;

	case dbiAutoPlay: {
		qint32 gif;
		stream >> gif;
		if (!_checkStreamStatus(stream)) return false;

		cSetAutoPlayGif(gif == 1);
	} break;

	case dbiDialogsMode: {
		qint32 enabled, modeInt;
		stream >> enabled >> modeInt;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetDialogsModeEnabled(enabled == 1);
		auto mode = Dialogs::Mode::All;
		if (enabled) {
			mode = static_cast<Dialogs::Mode>(modeInt);
			if (mode != Dialogs::Mode::All && mode != Dialogs::Mode::Important) {
				mode = Dialogs::Mode::All;
			}
		}
		Global::SetDialogsMode(mode);
	} break;

	case dbiModerateMode: {
		qint32 enabled;
		stream >> enabled;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetModerateModeEnabled(enabled == 1);
	} break;

	case dbiIncludeMuted: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetIncludeMuted(v == 1);
	} break;

	case dbiShowingSavedGifsOld: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;
	} break;

	case dbiDesktopNotify: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetDesktopNotify(v == 1);
		if (App::wnd()) App::wnd()->updateTrayMenu();
	} break;

	case dbiWindowsNotificationsOld: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;
	} break;

	case dbiNativeNotifications: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetNativeNotifications(v == 1);
	} break;

	case dbiNotificationsCount: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetNotificationsCount((v > 0 ? v : 3));
	} break;

	case dbiNotificationsCorner: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetNotificationsCorner(static_cast<Notify::ScreenCorner>((v >= 0 && v < 4) ? v : 2));
	} break;

	case dbiDialogsWidthRatioOld: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		GetStoredAuthSessionCache().setDialogsWidthRatio(v / 1000000.);
	} break;

	case dbiLastSeenWarningSeenOld: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		GetStoredAuthSessionCache().setLastSeenWarningSeen(v == 1);
	} break;

	case dbiAuthSessionSettings: {
		QByteArray v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		GetStoredAuthSessionCache().constructFromSerialized(v);
	} break;

	case dbiWorkMode: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		auto newMode = [v] {
			switch (v) {
			case dbiwmTrayOnly: return dbiwmTrayOnly;
			case dbiwmWindowOnly: return dbiwmWindowOnly;
			};
			return dbiwmWindowAndTray;
		};
		Global::RefWorkMode().set(newMode());
	} break;

	case dbiTxtDomainString: {
		QString v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetTxtDomainString(v);
	} break;

	case dbiConnectionTypeOld: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		ProxyData proxy;
		switch (v) {
		case dbictHttpProxy:
		case dbictTcpProxy: {
			qint32 port;
			stream >> proxy.host >> port >> proxy.user >> proxy.password;
			if (!_checkStreamStatus(stream)) return false;

			proxy.port = uint32(port);
			proxy.type = (v == dbictTcpProxy)
				? ProxyData::Type::Socks5
				: ProxyData::Type::Http;
		} break;
		};
		Global::SetSelectedProxy(proxy ? proxy : ProxyData());
		Global::SetUseProxy(proxy ? true : false);
		if (proxy) {
			Global::SetProxiesList({ 1, proxy });
		} else {
			Global::SetProxiesList({});
		}
		Sandbox::refreshGlobalProxy();
	} break;

	case dbiConnectionType: {
		qint32 connectionType;
		stream >> connectionType;
		if (!_checkStreamStatus(stream)) {
			return false;
		}

		const auto readProxy = [&] {
			qint32 proxyType, port;
			ProxyData proxy;
			stream >> proxyType >> proxy.host >> port >> proxy.user >> proxy.password;
			proxy.port = port;
			proxy.type = (proxyType == dbictTcpProxy)
				? ProxyData::Type::Socks5
				: (proxyType == dbictHttpProxy)
				? ProxyData::Type::Http
				: (proxyType == kProxyTypeShift + int(ProxyData::Type::Socks5))
				? ProxyData::Type::Socks5
				: (proxyType == kProxyTypeShift + int(ProxyData::Type::Http))
				? ProxyData::Type::Http
				: (proxyType == kProxyTypeShift + int(ProxyData::Type::Mtproto))
				? ProxyData::Type::Mtproto
				: ProxyData::Type::None;
			return proxy;
		};
		if (connectionType == dbictProxiesList) {
			qint32 count = 0, index = 0;
			stream >> count >> index;
			if (std::abs(index) > count) {
				Global::SetUseProxyForCalls(true);
				index -= (index > 0 ? count : -count);
			} else {
				Global::SetUseProxyForCalls(false);
			}

			auto list = std::vector<ProxyData>();
			for (auto i = 0; i < count; ++i) {
				const auto proxy = readProxy();
				if (proxy) {
					list.push_back(proxy);
				} else if (index < -list.size()) {
					++index;
				} else if (index > list.size()) {
					--index;
				}
			}
			if (!_checkStreamStatus(stream)) {
				return false;
			}
			Global::SetProxiesList(list);
			Global::SetUseProxy(index > 0 && index <= list.size());
			index = std::abs(index);
			if (index > 0 && index <= list.size()) {
				Global::SetSelectedProxy(list[index - 1]);
			} else {
				Global::SetSelectedProxy(ProxyData());
			}
		} else {
			const auto proxy = readProxy();
			if (!_checkStreamStatus(stream)) {
				return false;
			}
			if (proxy) {
				Global::SetProxiesList({ 1, proxy });
				Global::SetSelectedProxy(proxy);
				if (connectionType == dbictTcpProxy
					|| connectionType == dbictHttpProxy) {
					Global::SetUseProxy(true);
				} else {
					Global::SetUseProxy(false);
				}
			} else {
				Global::SetProxiesList({});
				Global::SetSelectedProxy(ProxyData());
				Global::SetUseProxy(false);
			}
		}
		Sandbox::refreshGlobalProxy();
	} break;

	case dbiThemeKey: {
		quint64 themeKey = 0;
		stream >> themeKey;
		if (!_checkStreamStatus(stream)) return false;

		_themeKey = themeKey;
	} break;

	case dbiLangPackKey: {
		quint64 langPackKey = 0;
		stream >> langPackKey;
		if (!_checkStreamStatus(stream)) return false;

		_langPackKey = langPackKey;
	} break;

	case dbiTryIPv6: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetTryIPv6(v == 1);
	} break;

	case dbiSeenTrayTooltip: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		cSetSeenTrayTooltip(v == 1);
	} break;

	case dbiAutoUpdate: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		cSetAutoUpdate(v == 1);
		if (!Core::UpdaterDisabled() && !cAutoUpdate()) {
			Core::UpdateChecker().stop();
		}
	} break;

	case dbiLastUpdateCheck: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		cSetLastUpdateCheck(v);
	} break;

	case dbiScale: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		DBIScale s = cRealScale();
		switch (v) {
		case dbisAuto: s = dbisAuto; break;
		case dbisOne: s = dbisOne; break;
		case dbisOneAndQuarter: s = dbisOneAndQuarter; break;
		case dbisOneAndHalf: s = dbisOneAndHalf; break;
		case dbisTwo: s = dbisTwo; break;
		}
		if (cRetina()) s = dbisOne;
		cSetConfigScale(s);
		cSetRealScale(s);
	} break;

	case dbiLangOld: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		context.legacyLanguageId = v;
	} break;

	case dbiLangFileOld: {
		QString v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		context.legacyLanguageFile = v;
	} break;

	case dbiWindowPosition: {
		auto position = TWindowPos();
		stream >> position.x >> position.y >> position.w >> position.h;
		stream >> position.moncrc >> position.maximized;
		if (!_checkStreamStatus(stream)) return false;

		DEBUG_LOG(("Window Pos: Read from storage %1, %2, %3, %4 (maximized %5)").arg(position.x).arg(position.y).arg(position.w).arg(position.h).arg(Logs::b(position.maximized)));
		cSetWindowPos(position);
	} break;

	case dbiLoggedPhoneNumber: {
		QString v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		cSetLoggedPhoneNumber(v);
	} break;

	case dbiMutePeer: { // deprecated
		quint64 peerId;
		stream >> peerId;
		if (!_checkStreamStatus(stream)) return false;
	} break;

	case dbiMutedPeers: { // deprecated
		quint32 count;
		stream >> count;
		if (!_checkStreamStatus(stream)) return false;

		for (uint32 i = 0; i < count; ++i) {
			quint64 peerId;
			stream >> peerId;
		}
		if (!_checkStreamStatus(stream)) return false;
	} break;

	case dbiSendKey: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		cSetCtrlEnter(v == dbiskCtrlEnter);
		if (App::main()) App::main()->ctrlEnterSubmitUpdated();
	} break;

	case dbiCatsAndDogs: { // deprecated
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;
	} break;

	case dbiTileBackground: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		bool tile = (version < 8005 && !_backgroundKey) ? false : (v == 1);
		Window::Theme::Background()->setTile(tile);
	} break;

	case dbiAdaptiveForWide: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetAdaptiveForWide(v == 1);
	} break;

	case dbiAutoLock: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetAutoLock(v);
		Global::RefLocalPasscodeChanged().notify();
	} break;

	case dbiReplaceEmoji: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetReplaceEmoji(v == 1);
	} break;

	case dbiSuggestEmoji: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetSuggestEmoji(v == 1);
	} break;

	case dbiSuggestStickersByEmoji: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetSuggestStickersByEmoji(v == 1);
	} break;

	case dbiDefaultAttach: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;
	} break;

	case dbiNotifyView: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		switch (v) {
		case dbinvShowNothing: Global::SetNotifyView(dbinvShowNothing); break;
		case dbinvShowName: Global::SetNotifyView(dbinvShowName); break;
		default: Global::SetNotifyView(dbinvShowPreview); break;
		}
	} break;

	case dbiAskDownloadPath: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetAskDownloadPath(v == 1);
	} break;

	case dbiDownloadPathOld: {
		QString v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;
#ifndef OS_WIN_STORE
		if (!v.isEmpty() && v != qstr("tmp") && !v.endsWith('/')) v += '/';
		Global::SetDownloadPath(v);
		Global::SetDownloadPathBookmark(QByteArray());
		Global::RefDownloadPathChanged().notify();
#endif // OS_WIN_STORE
	} break;

	case dbiDownloadPath: {
		QString v;
		QByteArray bookmark;
		stream >> v >> bookmark;
		if (!_checkStreamStatus(stream)) return false;
#ifndef OS_WIN_STORE
		if (!v.isEmpty() && v != qstr("tmp") && !v.endsWith('/')) v += '/';
		Global::SetDownloadPath(v);
		Global::SetDownloadPathBookmark(bookmark);
		psDownloadPathEnableAccess();
		Global::RefDownloadPathChanged().notify();
#endif // OS_WIN_STORE
	} break;

	case dbiCompressPastedImage: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		GetStoredAuthSessionCache().setSendFilesWay((v == 1)
			? SendFilesWay::Album
			: SendFilesWay::Files);
	} break;

	case dbiEmojiTabOld: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		// deprecated
	} break;

	case dbiRecentEmojiOldOld: {
		RecentEmojiPreloadOldOld v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		if (!v.isEmpty()) {
			RecentEmojiPreload p;
			p.reserve(v.size());
			for (auto &item : v) {
				auto oldKey = uint64(item.first);
				switch (oldKey) {
				case 0xD83CDDEFLLU: oldKey = 0xD83CDDEFD83CDDF5LLU; break;
				case 0xD83CDDF0LLU: oldKey = 0xD83CDDF0D83CDDF7LLU; break;
				case 0xD83CDDE9LLU: oldKey = 0xD83CDDE9D83CDDEALLU; break;
				case 0xD83CDDE8LLU: oldKey = 0xD83CDDE8D83CDDF3LLU; break;
				case 0xD83CDDFALLU: oldKey = 0xD83CDDFAD83CDDF8LLU; break;
				case 0xD83CDDEBLLU: oldKey = 0xD83CDDEBD83CDDF7LLU; break;
				case 0xD83CDDEALLU: oldKey = 0xD83CDDEAD83CDDF8LLU; break;
				case 0xD83CDDEELLU: oldKey = 0xD83CDDEED83CDDF9LLU; break;
				case 0xD83CDDF7LLU: oldKey = 0xD83CDDF7D83CDDFALLU; break;
				case 0xD83CDDECLLU: oldKey = 0xD83CDDECD83CDDE7LLU; break;
				}
				auto id = Ui::Emoji::IdFromOldKey(oldKey);
				if (!id.isEmpty()) {
					p.push_back(qMakePair(id, item.second));
				}
			}
			cSetRecentEmojiPreload(p);
		}
	} break;

	case dbiRecentEmojiOld: {
		RecentEmojiPreloadOld v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		if (!v.isEmpty()) {
			RecentEmojiPreload p;
			p.reserve(v.size());
			for (auto &item : v) {
				auto id = Ui::Emoji::IdFromOldKey(item.first);
				if (!id.isEmpty()) {
					p.push_back(qMakePair(id, item.second));
				}
			}
			cSetRecentEmojiPreload(p);
		}
	} break;

	case dbiRecentEmoji: {
		RecentEmojiPreload v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		cSetRecentEmojiPreload(v);
	} break;

	case dbiRecentStickers: {
		RecentStickerPreload v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		cSetRecentStickersPreload(v);
	} break;

	case dbiEmojiVariantsOld: {
		EmojiColorVariantsOld v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		EmojiColorVariants variants;
		for (auto i = v.cbegin(), e = v.cend(); i != e; ++i) {
			auto id = Ui::Emoji::IdFromOldKey(static_cast<uint64>(i.key()));
			if (!id.isEmpty()) {
				auto index = Ui::Emoji::ColorIndexFromOldKey(i.value());
				if (index >= 0) {
					variants.insert(id, index);
				}
			}
		}
		cSetEmojiVariants(variants);
	} break;

	case dbiEmojiVariants: {
		EmojiColorVariants v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		cSetEmojiVariants(v);
	} break;

	case dbiHiddenPinnedMessages: {
		Global::HiddenPinnedMessagesMap v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetHiddenPinnedMessages(v);
	} break;

	case dbiDialogLastPath: {
		QString path;
		stream >> path;
		if (!_checkStreamStatus(stream)) return false;

		cSetDialogLastPath(path);
	} break;

	case dbiSongVolume: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetSongVolume(snap(v / 1e6, 0., 1.));
	} break;

	case dbiVideoVolume: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetVideoVolume(snap(v / 1e6, 0., 1.));
	} break;

	default:
	LOG(("App Error: unknown blockId in _readSetting: %1").arg(blockId));
	return false;
	}

	return true;
}

bool _readOldSettings(bool remove, ReadSettingsContext &context) {
	bool result = false;
	QFile file(cWorkingDir() + qsl("tdata/config"));
	if (file.open(QIODevice::ReadOnly)) {
		LOG(("App Info: reading old config..."));
		QDataStream stream(&file);
		stream.setVersion(QDataStream::Qt_5_1);

		qint32 version = 0;
		while (!stream.atEnd()) {
			quint32 blockId;
			stream >> blockId;
			if (!_checkStreamStatus(stream)) break;

			if (blockId == dbiVersion) {
				stream >> version;
				if (!_checkStreamStatus(stream)) break;

				if (version > AppVersion) break;
			} else if (!_readSetting(blockId, stream, version, context)) {
				break;
			}
		}
		file.close();
		result = true;
	}
	if (remove) file.remove();
	return result;
}

void _readOldUserSettingsFields(QIODevice *device, qint32 &version, ReadSettingsContext &context) {
	QDataStream stream(device);
	stream.setVersion(QDataStream::Qt_5_1);

	while (!stream.atEnd()) {
		quint32 blockId;
		stream >> blockId;
		if (!_checkStreamStatus(stream)) {
			break;
		}

		if (blockId == dbiVersion) {
			stream >> version;
			if (!_checkStreamStatus(stream)) {
				break;
			}

			if (version > AppVersion) return;
		} else if (blockId == dbiEncryptedWithSalt) {
			QByteArray salt, data, decrypted;
			stream >> salt >> data;
			if (!_checkStreamStatus(stream)) {
				break;
			}

			if (salt.size() != 32) {
				LOG(("App Error: bad salt in old user config encrypted part, size: %1").arg(salt.size()));
				continue;
			}

			createLocalKey(QByteArray(), &salt, &OldKey);

			if (data.size() <= 16 || (data.size() & 0x0F)) {
				LOG(("App Error: bad encrypted part size in old user config: %1").arg(data.size()));
				continue;
			}
			uint32 fullDataLen = data.size() - 16;
			decrypted.resize(fullDataLen);
			const char *dataKey = data.constData(), *encrypted = data.constData() + 16;
			aesDecryptLocal(encrypted, decrypted.data(), fullDataLen, OldKey, dataKey);
			uchar sha1Buffer[20];
			if (memcmp(hashSha1(decrypted.constData(), decrypted.size(), sha1Buffer), dataKey, 16)) {
				LOG(("App Error: bad decrypt key, data from old user config not decrypted"));
				continue;
			}
			uint32 dataLen = *(const uint32*)decrypted.constData();
			if (dataLen > uint32(decrypted.size()) || dataLen <= fullDataLen - 16 || dataLen < 4) {
				LOG(("App Error: bad decrypted part size in old user config: %1, fullDataLen: %2, decrypted size: %3").arg(dataLen).arg(fullDataLen).arg(decrypted.size()));
				continue;
			}
			decrypted.resize(dataLen);
			QBuffer decryptedStream(&decrypted);
			decryptedStream.open(QIODevice::ReadOnly);
			decryptedStream.seek(4); // skip size
			LOG(("App Info: reading encrypted old user config..."));

			_readOldUserSettingsFields(&decryptedStream, version, context);
		} else if (!_readSetting(blockId, stream, version, context)) {
			return;
		}
	}
}

bool _readOldUserSettings(bool remove, ReadSettingsContext &context) {
	bool result = false;
	QFile file(cWorkingDir() + cDataFile() + (cTestMode() ? qsl("_test") : QString()) + qsl("_config"));
	if (file.open(QIODevice::ReadOnly)) {
		LOG(("App Info: reading old user config..."));
		qint32 version = 0;
		_readOldUserSettingsFields(&file, version, context);
		file.close();
		result = true;
	}
	if (remove) file.remove();
	return result;
}

void _readOldMtpDataFields(QIODevice *device, qint32 &version, ReadSettingsContext &context) {
	QDataStream stream(device);
	stream.setVersion(QDataStream::Qt_5_1);

	while (!stream.atEnd()) {
		quint32 blockId;
		stream >> blockId;
		if (!_checkStreamStatus(stream)) {
			break;
		}

		if (blockId == dbiVersion) {
			stream >> version;
			if (!_checkStreamStatus(stream)) {
				break;
			}

			if (version > AppVersion) return;
		} else if (blockId == dbiEncrypted) {
			QByteArray data, decrypted;
			stream >> data;
			if (!_checkStreamStatus(stream)) {
				break;
			}

			if (!OldKey) {
				LOG(("MTP Error: reading old encrypted keys without old key!"));
				continue;
			}

			if (data.size() <= 16 || (data.size() & 0x0F)) {
				LOG(("MTP Error: bad encrypted part size in old keys: %1").arg(data.size()));
				continue;
			}
			uint32 fullDataLen = data.size() - 16;
			decrypted.resize(fullDataLen);
			const char *dataKey = data.constData(), *encrypted = data.constData() + 16;
			aesDecryptLocal(encrypted, decrypted.data(), fullDataLen, OldKey, dataKey);
			uchar sha1Buffer[20];
			if (memcmp(hashSha1(decrypted.constData(), decrypted.size(), sha1Buffer), dataKey, 16)) {
				LOG(("MTP Error: bad decrypt key, data from old keys not decrypted"));
				continue;
			}
			uint32 dataLen = *(const uint32*)decrypted.constData();
			if (dataLen > uint32(decrypted.size()) || dataLen <= fullDataLen - 16 || dataLen < 4) {
				LOG(("MTP Error: bad decrypted part size in old keys: %1, fullDataLen: %2, decrypted size: %3").arg(dataLen).arg(fullDataLen).arg(decrypted.size()));
				continue;
			}
			decrypted.resize(dataLen);
			QBuffer decryptedStream(&decrypted);
			decryptedStream.open(QIODevice::ReadOnly);
			decryptedStream.seek(4); // skip size
			LOG(("App Info: reading encrypted old keys..."));

			_readOldMtpDataFields(&decryptedStream, version, context);
		} else if (!_readSetting(blockId, stream, version, context)) {
			return;
		}
	}
}

bool _readOldMtpData(bool remove, ReadSettingsContext &context) {
	bool result = false;
	QFile file(cWorkingDir() + cDataFile() + (cTestMode() ? qsl("_test") : QString()));
	if (file.open(QIODevice::ReadOnly)) {
		LOG(("App Info: reading old keys..."));
		qint32 version = 0;
		_readOldMtpDataFields(&file, version, context);
		file.close();
		result = true;
	}
	if (remove) file.remove();
	return result;
}

void _writeUserSettings() {
	if (_readingUserSettings) {
		LOG(("App Error: attempt to write settings while reading them!"));
		return;
	}
	LOG(("App Info: writing encrypted user settings..."));

	if (!_userSettingsKey) {
		_userSettingsKey = genKey();
		_mapChanged = true;
		_writeMap(WriteMapWhen::Fast);
	}

	auto recentEmojiPreloadData = cRecentEmojiPreload();
	if (recentEmojiPreloadData.isEmpty()) {
		recentEmojiPreloadData.reserve(Ui::Emoji::GetRecent().size());
		for (auto &item : Ui::Emoji::GetRecent()) {
			recentEmojiPreloadData.push_back(qMakePair(item.first->id(), item.second));
		}
	}
	auto userDataInstance = StoredAuthSessionCache
		? StoredAuthSessionCache.get()
		: Messenger::Instance().getAuthSessionSettings();
	auto userData = userDataInstance
		? userDataInstance->serialize()
		: QByteArray();

	uint32 size = 23 * (sizeof(quint32) + sizeof(qint32));
	size += sizeof(quint32) + Serialize::stringSize(Global::AskDownloadPath() ? QString() : Global::DownloadPath()) + Serialize::bytearraySize(Global::AskDownloadPath() ? QByteArray() : Global::DownloadPathBookmark());

	size += sizeof(quint32) + sizeof(qint32);
	for (auto &item : recentEmojiPreloadData) {
		size += Serialize::stringSize(item.first) + sizeof(item.second);
	}

	size += sizeof(quint32) + sizeof(qint32) + cEmojiVariants().size() * (sizeof(uint32) + sizeof(uint64));
	size += sizeof(quint32) + sizeof(qint32) + (cRecentStickersPreload().isEmpty() ? Stickers::GetRecentPack().size() : cRecentStickersPreload().size()) * (sizeof(uint64) + sizeof(ushort));
	size += sizeof(quint32) + Serialize::stringSize(cDialogLastPath());
	size += sizeof(quint32) + 3 * sizeof(qint32);
	size += sizeof(quint32) + 2 * sizeof(qint32);
	if (!Global::HiddenPinnedMessages().isEmpty()) {
		size += sizeof(quint32) + sizeof(qint32) + Global::HiddenPinnedMessages().size() * (sizeof(PeerId) + sizeof(MsgId));
	}
	if (!userData.isEmpty()) {
		size += sizeof(quint32) + Serialize::bytearraySize(userData);
	}

	EncryptedDescriptor data(size);
	data.stream << quint32(dbiSendKey) << qint32(cCtrlEnter() ? dbiskCtrlEnter : dbiskEnter);
	data.stream << quint32(dbiTileBackground) << qint32(Window::Theme::Background()->tileForSave() ? 1 : 0);
	data.stream << quint32(dbiAdaptiveForWide) << qint32(Global::AdaptiveForWide() ? 1 : 0);
	data.stream << quint32(dbiAutoLock) << qint32(Global::AutoLock());
	data.stream << quint32(dbiReplaceEmoji) << qint32(Global::ReplaceEmoji() ? 1 : 0);
	data.stream << quint32(dbiSuggestEmoji) << qint32(Global::SuggestEmoji() ? 1 : 0);
	data.stream << quint32(dbiSuggestStickersByEmoji) << qint32(Global::SuggestStickersByEmoji() ? 1 : 0);
	data.stream << quint32(dbiSoundNotify) << qint32(Global::SoundNotify());
	data.stream << quint32(dbiIncludeMuted) << qint32(Global::IncludeMuted());
	data.stream << quint32(dbiDesktopNotify) << qint32(Global::DesktopNotify());
	data.stream << quint32(dbiNotifyView) << qint32(Global::NotifyView());
	data.stream << quint32(dbiNativeNotifications) << qint32(Global::NativeNotifications());
	data.stream << quint32(dbiNotificationsCount) << qint32(Global::NotificationsCount());
	data.stream << quint32(dbiNotificationsCorner) << qint32(Global::NotificationsCorner());
	data.stream << quint32(dbiAskDownloadPath) << qint32(Global::AskDownloadPath());
	data.stream << quint32(dbiDownloadPath) << (Global::AskDownloadPath() ? QString() : Global::DownloadPath()) << (Global::AskDownloadPath() ? QByteArray() : Global::DownloadPathBookmark());
	data.stream << quint32(dbiDialogLastPath) << cDialogLastPath();
	data.stream << quint32(dbiSongVolume) << qint32(qRound(Global::SongVolume() * 1e6));
	data.stream << quint32(dbiVideoVolume) << qint32(qRound(Global::VideoVolume() * 1e6));
	data.stream << quint32(dbiAutoDownload) << qint32(cAutoDownloadPhoto()) << qint32(cAutoDownloadAudio()) << qint32(cAutoDownloadGif());
	data.stream << quint32(dbiDialogsMode) << qint32(Global::DialogsModeEnabled() ? 1 : 0) << static_cast<qint32>(Global::DialogsMode());
	data.stream << quint32(dbiModerateMode) << qint32(Global::ModerateModeEnabled() ? 1 : 0);
	data.stream << quint32(dbiAutoPlay) << qint32(cAutoPlayGif() ? 1 : 0);
	data.stream << quint32(dbiUseExternalVideoPlayer) << qint32(cUseExternalVideoPlayer());
	if (!userData.isEmpty()) {
		data.stream << quint32(dbiAuthSessionSettings) << userData;
	}

	{
		data.stream << quint32(dbiRecentEmoji) << recentEmojiPreloadData;
	}
	data.stream << quint32(dbiEmojiVariants) << cEmojiVariants();
	{
		auto v = cRecentStickersPreload();
		if (v.isEmpty()) {
			v.reserve(Stickers::GetRecentPack().size());
			for_const (auto &pair, Stickers::GetRecentPack()) {
				v.push_back(qMakePair(pair.first->id, pair.second));
			}
		}
		data.stream << quint32(dbiRecentStickers) << v;
	}
	if (!Global::HiddenPinnedMessages().isEmpty()) {
		data.stream << quint32(dbiHiddenPinnedMessages) << Global::HiddenPinnedMessages();
	}

	FileWriteDescriptor file(_userSettingsKey);
	file.writeEncrypted(data);
}

void _readUserSettings() {
	ReadSettingsContext context;
	FileReadDescriptor userSettings;
	if (!readEncryptedFile(userSettings, _userSettingsKey)) {
		LOG(("App Info: could not read encrypted user settings..."));

		_readOldUserSettings(true, context);
		applyReadContext(std::move(context));

		return _writeUserSettings();
	}

	LOG(("App Info: reading encrypted user settings..."));
	_readingUserSettings = true;
	while (!userSettings.stream.atEnd()) {
		quint32 blockId;
		userSettings.stream >> blockId;
		if (!_checkStreamStatus(userSettings.stream)) {
			_readingUserSettings = false;
			return _writeUserSettings();
		}

		if (!_readSetting(blockId, userSettings.stream, userSettings.version, context)) {
			_readingUserSettings = false;
			return _writeUserSettings();
		}
	}
	_readingUserSettings = false;
	LOG(("App Info: encrypted user settings read."));

	applyReadContext(std::move(context));
}

void _writeMtpData() {
	FileWriteDescriptor mtp(toFilePart(_dataNameKey), FileOption::Safe);
	if (!LocalKey) {
		LOG(("App Error: localkey not created in _writeMtpData()"));
		return;
	}

	auto mtpAuthorizationSerialized = Messenger::Instance().serializeMtpAuthorization();

	quint32 size = sizeof(quint32) + Serialize::bytearraySize(mtpAuthorizationSerialized);

	EncryptedDescriptor data(size);
	data.stream << quint32(dbiMtpAuthorization) << mtpAuthorizationSerialized;
	mtp.writeEncrypted(data);
}

void _readMtpData() {
	ReadSettingsContext context;
	FileReadDescriptor mtp;
	if (!readEncryptedFile(mtp, toFilePart(_dataNameKey), FileOption::Safe)) {
		if (LocalKey) {
			_readOldMtpData(true, context);
			applyReadContext(std::move(context));

			_writeMtpData();
		}
		return;
	}

	LOG(("App Info: reading encrypted mtp data..."));
	while (!mtp.stream.atEnd()) {
		quint32 blockId;
		mtp.stream >> blockId;
		if (!_checkStreamStatus(mtp.stream)) {
			return _writeMtpData();
		}

		if (!_readSetting(blockId, mtp.stream, mtp.version, context)) {
			return _writeMtpData();
		}
	}
	applyReadContext(std::move(context));
}

ReadMapState _readMap(const QByteArray &pass) {
	auto ms = getms();
	QByteArray dataNameUtf8 = (cDataFile() + (cTestMode() ? qsl(":/test/") : QString())).toUtf8();
	FileKey dataNameHash[2];
	hashMd5(dataNameUtf8.constData(), dataNameUtf8.size(), dataNameHash);
	_dataNameKey = dataNameHash[0];
	_userBasePath = _basePath + toFilePart(_dataNameKey) + QChar('/');

	FileReadDescriptor mapData;
	if (!readFile(mapData, qsl("map"))) {
		return ReadMapFailed;
	}
	LOG(("App Info: reading map..."));

	QByteArray salt, keyEncrypted, mapEncrypted;
	mapData.stream >> salt >> keyEncrypted >> mapEncrypted;
	if (!_checkStreamStatus(mapData.stream)) {
		return ReadMapFailed;
	}

	if (salt.size() != LocalEncryptSaltSize) {
		LOG(("App Error: bad salt in map file, size: %1").arg(salt.size()));
		return ReadMapFailed;
	}
	createLocalKey(pass, &salt, &PassKey);

	EncryptedDescriptor keyData, map;
	if (!decryptLocal(keyData, keyEncrypted, PassKey)) {
		LOG(("App Info: could not decrypt pass-protected key from map file, maybe bad password..."));
		return ReadMapPassNeeded;
	}
	auto key = Serialize::read<MTP::AuthKey::Data>(keyData.stream);
	if (keyData.stream.status() != QDataStream::Ok || !keyData.stream.atEnd()) {
		LOG(("App Error: could not read pass-protected key from map file"));
		return ReadMapFailed;
	}
	LocalKey = std::make_shared<MTP::AuthKey>(key);

	_passKeyEncrypted = keyEncrypted;
	_passKeySalt = salt;

	if (!decryptLocal(map, mapEncrypted)) {
		LOG(("App Error: could not decrypt map."));
		return ReadMapFailed;
	}
	LOG(("App Info: reading encrypted map..."));

	DraftsMap draftsMap, draftCursorsMap;
	DraftsNotReadMap draftsNotReadMap;
	StorageMap imagesMap, stickerImagesMap, audiosMap;
	qint64 storageImagesSize = 0, storageStickersSize = 0, storageAudiosSize = 0;
	quint64 locationsKey = 0, reportSpamStatusesKey = 0, trustedBotsKey = 0;
	quint64 recentStickersKeyOld = 0;
	quint64 installedStickersKey = 0, featuredStickersKey = 0, recentStickersKey = 0, favedStickersKey = 0, archivedStickersKey = 0;
	quint64 savedGifsKey = 0;
	quint64 backgroundKey = 0, userSettingsKey = 0, recentHashtagsAndBotsKey = 0, savedPeersKey = 0, exportSettingsKey = 0;
	while (!map.stream.atEnd()) {
		quint32 keyType;
		map.stream >> keyType;
		switch (keyType) {
		case lskDraft: {
			quint32 count = 0;
			map.stream >> count;
			for (quint32 i = 0; i < count; ++i) {
				FileKey key;
				quint64 p;
				map.stream >> key >> p;
				draftsMap.insert(p, key);
				draftsNotReadMap.insert(p, true);
			}
		} break;
		case lskDraftPosition: {
			quint32 count = 0;
			map.stream >> count;
			for (quint32 i = 0; i < count; ++i) {
				FileKey key;
				quint64 p;
				map.stream >> key >> p;
				draftCursorsMap.insert(p, key);
			}
		} break;
		case lskImages: {
			quint32 count = 0;
			map.stream >> count;
			for (quint32 i = 0; i < count; ++i) {
				FileKey key;
				quint64 first, second;
				qint32 size;
				map.stream >> key >> first >> second >> size;
				imagesMap.insert(StorageKey(first, second), FileDesc(key, size));
				storageImagesSize += size;
			}
		} break;
		case lskStickerImages: {
			quint32 count = 0;
			map.stream >> count;
			for (quint32 i = 0; i < count; ++i) {
				FileKey key;
				quint64 first, second;
				qint32 size;
				map.stream >> key >> first >> second >> size;
				stickerImagesMap.insert(StorageKey(first, second), FileDesc(key, size));
				storageStickersSize += size;
			}
		} break;
		case lskAudios: {
			quint32 count = 0;
			map.stream >> count;
			for (quint32 i = 0; i < count; ++i) {
				FileKey key;
				quint64 first, second;
				qint32 size;
				map.stream >> key >> first >> second >> size;
				audiosMap.insert(StorageKey(first, second), FileDesc(key, size));
				storageAudiosSize += size;
			}
		} break;
		case lskLocations: {
			map.stream >> locationsKey;
		} break;
		case lskReportSpamStatuses: {
			map.stream >> reportSpamStatusesKey;
		} break;
		case lskTrustedBots: {
			map.stream >> trustedBotsKey;
		} break;
		case lskRecentStickersOld: {
			map.stream >> recentStickersKeyOld;
		} break;
		case lskBackground: {
			map.stream >> backgroundKey;
		} break;
		case lskUserSettings: {
			map.stream >> userSettingsKey;
		} break;
		case lskRecentHashtagsAndBots: {
			map.stream >> recentHashtagsAndBotsKey;
		} break;
		case lskStickersOld: {
			map.stream >> installedStickersKey;
		} break;
		case lskStickersKeys: {
			map.stream >> installedStickersKey >> featuredStickersKey >> recentStickersKey >> archivedStickersKey;
		} break;
		case lskFavedStickers: {
			map.stream >> favedStickersKey;
		} break;
		case lskSavedGifsOld: {
			quint64 key;
			map.stream >> key;
		} break;
		case lskSavedGifs: {
			map.stream >> savedGifsKey;
		} break;
		case lskSavedPeers: {
			map.stream >> savedPeersKey;
		} break;
		case lskExportSettings: {
			map.stream >> exportSettingsKey;
		} break;
		default:
		LOG(("App Error: unknown key type in encrypted map: %1").arg(keyType));
		return ReadMapFailed;
		}
		if (!_checkStreamStatus(map.stream)) {
			return ReadMapFailed;
		}
	}

	_draftsMap = draftsMap;
	_draftCursorsMap = draftCursorsMap;
	_draftsNotReadMap = draftsNotReadMap;

	_imagesMap = imagesMap;
	_storageImagesSize = storageImagesSize;
	_stickerImagesMap = stickerImagesMap;
	_storageStickersSize = storageStickersSize;
	_audiosMap = audiosMap;
	_storageAudiosSize = storageAudiosSize;

	_locationsKey = locationsKey;
	_reportSpamStatusesKey = reportSpamStatusesKey;
	_trustedBotsKey = trustedBotsKey;
	_recentStickersKeyOld = recentStickersKeyOld;
	_installedStickersKey = installedStickersKey;
	_featuredStickersKey = featuredStickersKey;
	_recentStickersKey = recentStickersKey;
	_favedStickersKey = favedStickersKey;
	_archivedStickersKey = archivedStickersKey;
	_savedGifsKey = savedGifsKey;
	_savedPeersKey = savedPeersKey;
	_backgroundKey = backgroundKey;
	_userSettingsKey = userSettingsKey;
	_recentHashtagsAndBotsKey = recentHashtagsAndBotsKey;
	_exportSettingsKey = exportSettingsKey;
	_oldMapVersion = mapData.version;
	if (_oldMapVersion < AppVersion) {
		_mapChanged = true;
		_writeMap();
	} else {
		_mapChanged = false;
	}

	if (_locationsKey) {
		_readLocations();
	}
	if (_reportSpamStatusesKey) {
		_readReportSpamStatuses();
	}

	_readUserSettings();
	_readMtpData();

	Messenger::Instance().setAuthSessionFromStorage(std::move(StoredAuthSessionCache));

	LOG(("Map read time: %1").arg(getms() - ms));
	if (_oldSettingsVersion < AppVersion) {
		writeSettings();
	}
	return ReadMapDone;
}

void _writeMap(WriteMapWhen when) {
	if (when != WriteMapWhen::Now) {
		_manager->writeMap(when == WriteMapWhen::Fast);
		return;
	}
	_manager->writingMap();
	if (!_mapChanged) return;
	if (_userBasePath.isEmpty()) {
		LOG(("App Error: _userBasePath is empty in writeMap()"));
		return;
	}

	if (!QDir().exists(_userBasePath)) QDir().mkpath(_userBasePath);

	FileWriteDescriptor map(qsl("map"));
	if (_passKeySalt.isEmpty() || _passKeyEncrypted.isEmpty()) {
		QByteArray pass(kLocalKeySize, Qt::Uninitialized), salt(LocalEncryptSaltSize, Qt::Uninitialized);
		memset_rand(pass.data(), pass.size());
		memset_rand(salt.data(), salt.size());
		createLocalKey(pass, &salt, &LocalKey);

		_passKeySalt.resize(LocalEncryptSaltSize);
		memset_rand(_passKeySalt.data(), _passKeySalt.size());
		createLocalKey(QByteArray(), &_passKeySalt, &PassKey);

		EncryptedDescriptor passKeyData(kLocalKeySize);
		LocalKey->write(passKeyData.stream);
		_passKeyEncrypted = FileWriteDescriptor::prepareEncrypted(passKeyData, PassKey);
	}
	map.writeData(_passKeySalt);
	map.writeData(_passKeyEncrypted);

	uint32 mapSize = 0;
	if (!_draftsMap.isEmpty()) mapSize += sizeof(quint32) * 2 + _draftsMap.size() * sizeof(quint64) * 2;
	if (!_draftCursorsMap.isEmpty()) mapSize += sizeof(quint32) * 2 + _draftCursorsMap.size() * sizeof(quint64) * 2;
	if (!_imagesMap.isEmpty()) mapSize += sizeof(quint32) * 2 + _imagesMap.size() * (sizeof(quint64) * 3 + sizeof(qint32));
	if (!_stickerImagesMap.isEmpty()) mapSize += sizeof(quint32) * 2 + _stickerImagesMap.size() * (sizeof(quint64) * 3 + sizeof(qint32));
	if (!_audiosMap.isEmpty()) mapSize += sizeof(quint32) * 2 + _audiosMap.size() * (sizeof(quint64) * 3 + sizeof(qint32));
	if (_locationsKey) mapSize += sizeof(quint32) + sizeof(quint64);
	if (_reportSpamStatusesKey) mapSize += sizeof(quint32) + sizeof(quint64);
	if (_trustedBotsKey) mapSize += sizeof(quint32) + sizeof(quint64);
	if (_recentStickersKeyOld) mapSize += sizeof(quint32) + sizeof(quint64);
	if (_installedStickersKey || _featuredStickersKey || _recentStickersKey || _archivedStickersKey) {
		mapSize += sizeof(quint32) + 4 * sizeof(quint64);
	}
	if (_favedStickersKey) mapSize += sizeof(quint32) + sizeof(quint64);
	if (_savedGifsKey) mapSize += sizeof(quint32) + sizeof(quint64);
	if (_savedPeersKey) mapSize += sizeof(quint32) + sizeof(quint64);
	if (_backgroundKey) mapSize += sizeof(quint32) + sizeof(quint64);
	if (_userSettingsKey) mapSize += sizeof(quint32) + sizeof(quint64);
	if (_recentHashtagsAndBotsKey) mapSize += sizeof(quint32) + sizeof(quint64);
	if (_exportSettingsKey) mapSize += sizeof(quint32) + sizeof(quint64);

	if (mapSize > 30 * 1024 * 1024) {
		CrashReports::SetAnnotation("MapSize", QString("%1,%2,%3,%4,%5"
		).arg(_draftsMap.size()
		).arg(_draftCursorsMap.size()
		).arg(_imagesMap.size()
		).arg(_stickerImagesMap.size()
		).arg(_audiosMap.size()
		));
	}

	EncryptedDescriptor mapData(mapSize);

	if (!_draftsMap.isEmpty()) {
		mapData.stream << quint32(lskDraft) << quint32(_draftsMap.size());
		for (DraftsMap::const_iterator i = _draftsMap.cbegin(), e = _draftsMap.cend(); i != e; ++i) {
			mapData.stream << quint64(i.value()) << quint64(i.key());
		}
	}
	if (!_draftCursorsMap.isEmpty()) {
		mapData.stream << quint32(lskDraftPosition) << quint32(_draftCursorsMap.size());
		for (DraftsMap::const_iterator i = _draftCursorsMap.cbegin(), e = _draftCursorsMap.cend(); i != e; ++i) {
			mapData.stream << quint64(i.value()) << quint64(i.key());
		}
	}
	if (!_imagesMap.isEmpty()) {
		mapData.stream << quint32(lskImages) << quint32(_imagesMap.size());
		for (StorageMap::const_iterator i = _imagesMap.cbegin(), e = _imagesMap.cend(); i != e; ++i) {
			mapData.stream << quint64(i.value().first) << quint64(i.key().first) << quint64(i.key().second) << qint32(i.value().second);
		}
	}
	if (!_stickerImagesMap.isEmpty()) {
		mapData.stream << quint32(lskStickerImages) << quint32(_stickerImagesMap.size());
		for (StorageMap::const_iterator i = _stickerImagesMap.cbegin(), e = _stickerImagesMap.cend(); i != e; ++i) {
			mapData.stream << quint64(i.value().first) << quint64(i.key().first) << quint64(i.key().second) << qint32(i.value().second);
		}
	}
	if (!_audiosMap.isEmpty()) {
		mapData.stream << quint32(lskAudios) << quint32(_audiosMap.size());
		for (StorageMap::const_iterator i = _audiosMap.cbegin(), e = _audiosMap.cend(); i != e; ++i) {
			mapData.stream << quint64(i.value().first) << quint64(i.key().first) << quint64(i.key().second) << qint32(i.value().second);
		}
	}
	if (_locationsKey) {
		mapData.stream << quint32(lskLocations) << quint64(_locationsKey);
	}
	if (_reportSpamStatusesKey) {
		mapData.stream << quint32(lskReportSpamStatuses) << quint64(_reportSpamStatusesKey);
	}
	if (_trustedBotsKey) {
		mapData.stream << quint32(lskTrustedBots) << quint64(_trustedBotsKey);
	}
	if (_recentStickersKeyOld) {
		mapData.stream << quint32(lskRecentStickersOld) << quint64(_recentStickersKeyOld);
	}
	if (_installedStickersKey || _featuredStickersKey || _recentStickersKey || _archivedStickersKey) {
		mapData.stream << quint32(lskStickersKeys);
		mapData.stream << quint64(_installedStickersKey) << quint64(_featuredStickersKey) << quint64(_recentStickersKey) << quint64(_archivedStickersKey);
	}
	if (_favedStickersKey) {
		mapData.stream << quint32(lskFavedStickers) << quint64(_favedStickersKey);
	}
	if (_savedGifsKey) {
		mapData.stream << quint32(lskSavedGifs) << quint64(_savedGifsKey);
	}
	if (_savedPeersKey) {
		mapData.stream << quint32(lskSavedPeers) << quint64(_savedPeersKey);
	}
	if (_backgroundKey) {
		mapData.stream << quint32(lskBackground) << quint64(_backgroundKey);
	}
	if (_userSettingsKey) {
		mapData.stream << quint32(lskUserSettings) << quint64(_userSettingsKey);
	}
	if (_recentHashtagsAndBotsKey) {
		mapData.stream << quint32(lskRecentHashtagsAndBots) << quint64(_recentHashtagsAndBotsKey);
	}
	if (_exportSettingsKey) {
		mapData.stream << quint32(lskExportSettings) << quint64(_exportSettingsKey);
	}
	map.writeEncrypted(mapData);

	_mapChanged = false;

	if (mapSize > 30 * 1024 * 1024) {
		CrashReports::ClearAnnotation("MapSize");
	}
}

} // namespace

void finish() {
	if (_manager) {
		_writeMap(WriteMapWhen::Now);
		_manager->finish();
		_manager->deleteLater();
		_manager = 0;
		delete base::take(_localLoader);
	}
}

void readTheme();
void readLangPack();

void start() {
	Expects(!_manager);

	_manager = new internal::Manager();
	_localLoader = new TaskQueue(kFileLoaderQueueStopTimeout);

	_basePath = cWorkingDir() + qsl("tdata/");
	if (!QDir().exists(_basePath)) QDir().mkpath(_basePath);

	ReadSettingsContext context;
	FileReadDescriptor settingsData;
	if (!readFile(settingsData, cTestMode() ? qsl("settings_test") : qsl("settings"), FileOption::Safe)) {
		_readOldSettings(true, context);
		_readOldUserSettings(false, context); // needed further in _readUserSettings
		_readOldMtpData(false, context); // needed further in _readMtpData
		applyReadContext(std::move(context));

		return writeSettings();
	}
	LOG(("App Info: reading settings..."));

	QByteArray salt, settingsEncrypted;
	settingsData.stream >> salt >> settingsEncrypted;
	if (!_checkStreamStatus(settingsData.stream)) {
		return writeSettings();
	}

	if (salt.size() != LocalEncryptSaltSize) {
		LOG(("App Error: bad salt in settings file, size: %1").arg(salt.size()));
		return writeSettings();
	}
	createLocalKey(QByteArray(), &salt, &SettingsKey);

	EncryptedDescriptor settings;
	if (!decryptLocal(settings, settingsEncrypted, SettingsKey)) {
		LOG(("App Error: could not decrypt settings from settings file, maybe bad passcode..."));
		return writeSettings();
	}

	LOG(("App Info: reading encrypted settings..."));
	while (!settings.stream.atEnd()) {
		quint32 blockId;
		settings.stream >> blockId;
		if (!_checkStreamStatus(settings.stream)) {
			return writeSettings();
		}

		if (!_readSetting(blockId, settings.stream, settingsData.version, context)) {
			return writeSettings();
		}
	}

	_oldSettingsVersion = settingsData.version;
	_settingsSalt = salt;

	readTheme();
	readLangPack();

	applyReadContext(std::move(context));
}

void writeSettings() {
	if (_basePath.isEmpty()) {
		LOG(("App Error: _basePath is empty in writeSettings()"));
		return;
	}

	if (!QDir().exists(_basePath)) QDir().mkpath(_basePath);

	FileWriteDescriptor settings(cTestMode() ? qsl("settings_test") : qsl("settings"), FileOption::Safe);
	if (_settingsSalt.isEmpty() || !SettingsKey) {
		_settingsSalt.resize(LocalEncryptSaltSize);
		memset_rand(_settingsSalt.data(), _settingsSalt.size());
		createLocalKey(QByteArray(), &_settingsSalt, &SettingsKey);
	}
	settings.writeData(_settingsSalt);

	auto dcOptionsSerialized = Messenger::Instance().dcOptions()->serialize();

	quint32 size = 12 * (sizeof(quint32) + sizeof(qint32));
	size += sizeof(quint32) + Serialize::bytearraySize(dcOptionsSerialized);
	size += sizeof(quint32) + Serialize::stringSize(cLoggedPhoneNumber());
	size += sizeof(quint32) + Serialize::stringSize(Global::TxtDomainString());

	auto &proxies = Global::RefProxiesList();
	const auto &proxy = Global::SelectedProxy();
	auto proxyIt = ranges::find(proxies, proxy);
	if (proxy.type != ProxyData::Type::None
		&& proxyIt == end(proxies)) {
		proxies.push_back(proxy);
		proxyIt = end(proxies) - 1;
	}
	size += sizeof(quint32) + sizeof(qint32) + sizeof(qint32) + sizeof(qint32);
	for (const auto &proxy : proxies) {
		size += sizeof(qint32) + Serialize::stringSize(proxy.host) + sizeof(qint32) + Serialize::stringSize(proxy.user) + Serialize::stringSize(proxy.password);
	}

	if (_themeKey) {
		size += sizeof(quint32) + sizeof(quint64);
	}
	if (_langPackKey) {
		size += sizeof(quint32) + sizeof(quint64);
	}
	size += sizeof(quint32) + sizeof(qint32) * 8;

	EncryptedDescriptor data(size);
	data.stream << quint32(dbiChatSizeMax) << qint32(Global::ChatSizeMax());
	data.stream << quint32(dbiMegagroupSizeMax) << qint32(Global::MegagroupSizeMax());
	data.stream << quint32(dbiSavedGifsLimit) << qint32(Global::SavedGifsLimit());
	data.stream << quint32(dbiStickersRecentLimit) << qint32(Global::StickersRecentLimit());
	data.stream << quint32(dbiStickersFavedLimit) << qint32(Global::StickersFavedLimit());
	data.stream << quint32(dbiAutoStart) << qint32(cAutoStart());
	data.stream << quint32(dbiStartMinimized) << qint32(cStartMinimized());
	data.stream << quint32(dbiSendToMenu) << qint32(cSendToMenu());
	data.stream << quint32(dbiWorkMode) << qint32(Global::WorkMode().value());
	data.stream << quint32(dbiSeenTrayTooltip) << qint32(cSeenTrayTooltip());
	data.stream << quint32(dbiAutoUpdate) << qint32(cAutoUpdate());
	data.stream << quint32(dbiLastUpdateCheck) << qint32(cLastUpdateCheck());
	data.stream << quint32(dbiScale) << qint32(cConfigScale());
	data.stream << quint32(dbiDcOptions) << dcOptionsSerialized;
	data.stream << quint32(dbiLoggedPhoneNumber) << cLoggedPhoneNumber();
	data.stream << quint32(dbiTxtDomainString) << Global::TxtDomainString();

	data.stream << quint32(dbiConnectionType) << qint32(dbictProxiesList);
	data.stream << qint32(proxies.size());
	const auto index = qint32(proxyIt - begin(proxies))
		+ qint32(Global::UseProxyForCalls() ? proxies.size() : 0)
		+ 1;
	data.stream << (Global::UseProxy() ? index : -index);
	for (const auto &proxy : proxies) {
		data.stream << qint32(kProxyTypeShift + int(proxy.type));
		data.stream << proxy.host << qint32(proxy.port) << proxy.user << proxy.password;
	}

	data.stream << quint32(dbiTryIPv6) << qint32(Global::TryIPv6());
	if (_themeKey) {
		data.stream << quint32(dbiThemeKey) << quint64(_themeKey);
	}
	if (_langPackKey) {
		data.stream << quint32(dbiLangPackKey) << quint64(_langPackKey);
	}

	auto position = cWindowPos();
	data.stream << quint32(dbiWindowPosition) << qint32(position.x) << qint32(position.y) << qint32(position.w) << qint32(position.h);
	data.stream << qint32(position.moncrc) << qint32(position.maximized);

	DEBUG_LOG(("Window Pos: Writing to storage %1, %2, %3, %4 (maximized %5)").arg(position.x).arg(position.y).arg(position.w).arg(position.h).arg(Logs::b(position.maximized)));

	settings.writeEncrypted(data, SettingsKey);
}

void writeUserSettings() {
	_writeUserSettings();
}

void writeMtpData() {
	_writeMtpData();
}

const QString &AutoupdatePrefix(const QString &replaceWith = {}) {
	Expects(!Core::UpdaterDisabled());

	static auto value = QString();
	if (!replaceWith.isEmpty()) {
		value = replaceWith;
	}
	return value;
}

QString autoupdatePrefixFile() {
	Expects(!Core::UpdaterDisabled());

	return cWorkingDir() + "tdata/prefix";
}

const QString &readAutoupdatePrefixRaw() {
	Expects(!Core::UpdaterDisabled());

	const auto &result = AutoupdatePrefix();
	if (!result.isEmpty()) {
		return result;
	}
	QFile f(autoupdatePrefixFile());
	if (f.open(QIODevice::ReadOnly)) {
		const auto value = QString::fromUtf8(f.readAll());
		if (!value.isEmpty()) {
			return AutoupdatePrefix(value);
		}
	}
	return AutoupdatePrefix("https://updates.tdesktop.com");
}

void writeAutoupdatePrefix(const QString &prefix) {
	if (Core::UpdaterDisabled()) {
		return;
	}

	const auto current = readAutoupdatePrefixRaw();
	if (current != prefix) {
		AutoupdatePrefix(prefix);
		QFile f(autoupdatePrefixFile());
		if (f.open(QIODevice::WriteOnly)) {
			f.write(prefix.toUtf8());
			f.close();
		}
		if (cAutoUpdate()) {
			Core::UpdateChecker checker;
			checker.start();
		}
	}
}

QString readAutoupdatePrefix() {
	Expects(!Core::UpdaterDisabled());

	auto result = readAutoupdatePrefixRaw();
	return result.replace(QRegularExpression("/+$"), QString());
}

void reset() {
	if (_localLoader) {
		_localLoader->stop();
	}

	_passKeySalt.clear(); // reset passcode, local key
	_draftsMap.clear();
	_draftCursorsMap.clear();
	_fileLocations.clear();
	_fileLocationPairs.clear();
	_fileLocationAliases.clear();
	_imagesMap.clear();
	_draftsNotReadMap.clear();
	_stickerImagesMap.clear();
	_audiosMap.clear();
	_storageImagesSize = _storageStickersSize = _storageAudiosSize = 0;
	_webFilesMap.clear();
	_storageWebFilesSize = 0;
	_locationsKey = _reportSpamStatusesKey = _trustedBotsKey = 0;
	_recentStickersKeyOld = 0;
	_installedStickersKey = _featuredStickersKey = _recentStickersKey = _favedStickersKey = _archivedStickersKey = 0;
	_savedGifsKey = 0;
	_backgroundKey = _userSettingsKey = _recentHashtagsAndBotsKey = _savedPeersKey = _exportSettingsKey = 0;
	_oldMapVersion = _oldSettingsVersion = 0;
	StoredAuthSessionCache.reset();
	_mapChanged = true;
	_writeMap(WriteMapWhen::Now);

	_writeMtpData();
}

bool checkPasscode(const QByteArray &passcode) {
	auto checkKey = MTP::AuthKeyPtr();
	createLocalKey(passcode, &_passKeySalt, &checkKey);
	return checkKey->equals(PassKey);
}

void setPasscode(const QByteArray &passcode) {
	createLocalKey(passcode, &_passKeySalt, &PassKey);

	EncryptedDescriptor passKeyData(kLocalKeySize);
	LocalKey->write(passKeyData.stream);
	_passKeyEncrypted = FileWriteDescriptor::prepareEncrypted(passKeyData, PassKey);

	_mapChanged = true;
	_writeMap(WriteMapWhen::Now);

	Global::SetLocalPasscode(!passcode.isEmpty());
	Global::RefLocalPasscodeChanged().notify();
}

ReadMapState readMap(const QByteArray &pass) {
	ReadMapState result = _readMap(pass);
	if (result == ReadMapFailed) {
		_mapChanged = true;
		_writeMap(WriteMapWhen::Now);
	}
	return result;
}

int32 oldMapVersion() {
	return _oldMapVersion;
}

int32 oldSettingsVersion() {
	return _oldSettingsVersion;
}

void writeDrafts(const PeerId &peer, const MessageDraft &localDraft, const MessageDraft &editDraft) {
	if (!_working()) return;

	if (localDraft.msgId <= 0 && localDraft.textWithTags.text.isEmpty() && editDraft.msgId <= 0) {
		auto i = _draftsMap.find(peer);
		if (i != _draftsMap.cend()) {
			clearKey(i.value());
			_draftsMap.erase(i);
			_mapChanged = true;
			_writeMap();
		}

		_draftsNotReadMap.remove(peer);
	} else {
		auto i = _draftsMap.constFind(peer);
		if (i == _draftsMap.cend()) {
			i = _draftsMap.insert(peer, genKey());
			_mapChanged = true;
			_writeMap(WriteMapWhen::Fast);
		}

		auto msgTags = TextUtilities::SerializeTags(
			localDraft.textWithTags.tags);
		auto editTags = TextUtilities::SerializeTags(
			editDraft.textWithTags.tags);

		int size = sizeof(quint64);
		size += Serialize::stringSize(localDraft.textWithTags.text) + Serialize::bytearraySize(msgTags) + 2 * sizeof(qint32);
		size += Serialize::stringSize(editDraft.textWithTags.text) + Serialize::bytearraySize(editTags) + 2 * sizeof(qint32);

		EncryptedDescriptor data(size);
		data.stream << quint64(peer);
		data.stream << localDraft.textWithTags.text << msgTags;
		data.stream << qint32(localDraft.msgId) << qint32(localDraft.previewCancelled ? 1 : 0);
		data.stream << editDraft.textWithTags.text << editTags;
		data.stream << qint32(editDraft.msgId) << qint32(editDraft.previewCancelled ? 1 : 0);

		FileWriteDescriptor file(i.value());
		file.writeEncrypted(data);

		_draftsNotReadMap.remove(peer);
	}
}

void clearDraftCursors(const PeerId &peer) {
	DraftsMap::iterator i = _draftCursorsMap.find(peer);
	if (i != _draftCursorsMap.cend()) {
		clearKey(i.value());
		_draftCursorsMap.erase(i);
		_mapChanged = true;
		_writeMap();
	}
}

void _readDraftCursors(const PeerId &peer, MessageCursor &localCursor, MessageCursor &editCursor) {
	DraftsMap::iterator j = _draftCursorsMap.find(peer);
	if (j == _draftCursorsMap.cend()) {
		return;
	}

	FileReadDescriptor draft;
	if (!readEncryptedFile(draft, j.value())) {
		clearDraftCursors(peer);
		return;
	}
	quint64 draftPeer;
	qint32 localPosition = 0, localAnchor = 0, localScroll = QFIXED_MAX;
	qint32 editPosition = 0, editAnchor = 0, editScroll = QFIXED_MAX;
	draft.stream >> draftPeer >> localPosition >> localAnchor >> localScroll;
	if (!draft.stream.atEnd()) {
		draft.stream >> editPosition >> editAnchor >> editScroll;
	}

	if (draftPeer != peer) {
		clearDraftCursors(peer);
		return;
	}

	localCursor = MessageCursor(localPosition, localAnchor, localScroll);
	editCursor = MessageCursor(editPosition, editAnchor, editScroll);
}

void readDraftsWithCursors(History *h) {
	PeerId peer = h->peer->id;
	if (!_draftsNotReadMap.remove(peer)) {
		clearDraftCursors(peer);
		return;
	}

	DraftsMap::iterator j = _draftsMap.find(peer);
	if (j == _draftsMap.cend()) {
		clearDraftCursors(peer);
		return;
	}
	FileReadDescriptor draft;
	if (!readEncryptedFile(draft, j.value())) {
		clearKey(j.value());
		_draftsMap.erase(j);
		clearDraftCursors(peer);
		return;
	}

	quint64 draftPeer = 0;
	TextWithTags msgData, editData;
	QByteArray msgTagsSerialized, editTagsSerialized;
	qint32 msgReplyTo = 0, msgPreviewCancelled = 0, editMsgId = 0, editPreviewCancelled = 0;
	draft.stream >> draftPeer >> msgData.text;
	if (draft.version >= 9048) {
		draft.stream >> msgTagsSerialized;
	}
	if (draft.version >= 7021) {
		draft.stream >> msgReplyTo;
		if (draft.version >= 8001) {
			draft.stream >> msgPreviewCancelled;
			if (!draft.stream.atEnd()) {
				draft.stream >> editData.text;
				if (draft.version >= 9048) {
					draft.stream >> editTagsSerialized;
				}
				draft.stream >> editMsgId >> editPreviewCancelled;
			}
		}
	}
	if (draftPeer != peer) {
		clearKey(j.value());
		_draftsMap.erase(j);
		clearDraftCursors(peer);
		return;
	}

	msgData.tags = TextUtilities::DeserializeTags(
		msgTagsSerialized,
		msgData.text.size());
	editData.tags = TextUtilities::DeserializeTags(
		editTagsSerialized,
		editData.text.size());

	MessageCursor msgCursor, editCursor;
	_readDraftCursors(peer, msgCursor, editCursor);

	if (!h->localDraft()) {
		if (msgData.text.isEmpty() && !msgReplyTo) {
			h->clearLocalDraft();
		} else {
			h->setLocalDraft(std::make_unique<Data::Draft>(
				msgData,
				msgReplyTo,
				msgCursor,
				msgPreviewCancelled));
		}
	}
	if (!editMsgId) {
		h->clearEditDraft();
	} else {
		h->setEditDraft(std::make_unique<Data::Draft>(
			editData,
			editMsgId,
			editCursor,
			editPreviewCancelled));
	}
}

void writeDraftCursors(const PeerId &peer, const MessageCursor &msgCursor, const MessageCursor &editCursor) {
	if (!_working()) return;

	if (msgCursor == MessageCursor() && editCursor == MessageCursor()) {
		clearDraftCursors(peer);
	} else {
		DraftsMap::const_iterator i = _draftCursorsMap.constFind(peer);
		if (i == _draftCursorsMap.cend()) {
			i = _draftCursorsMap.insert(peer, genKey());
			_mapChanged = true;
			_writeMap(WriteMapWhen::Fast);
		}

		EncryptedDescriptor data(sizeof(quint64) + sizeof(qint32) * 3);
		data.stream << quint64(peer) << qint32(msgCursor.position) << qint32(msgCursor.anchor) << qint32(msgCursor.scroll);
		data.stream << qint32(editCursor.position) << qint32(editCursor.anchor) << qint32(editCursor.scroll);

		FileWriteDescriptor file(i.value());
		file.writeEncrypted(data);
	}
}

bool hasDraftCursors(const PeerId &peer) {
	return _draftCursorsMap.contains(peer);
}

bool hasDraft(const PeerId &peer) {
	return _draftsMap.contains(peer);
}

void writeFileLocation(MediaKey location, const FileLocation &local) {
	if (local.fname.isEmpty()) return;

	FileLocationAliases::const_iterator aliasIt = _fileLocationAliases.constFind(location);
	if (aliasIt != _fileLocationAliases.cend()) {
		location = aliasIt.value();
	}

	FileLocationPairs::iterator i = _fileLocationPairs.find(local.fname);
	if (i != _fileLocationPairs.cend()) {
		if (i.value().second == local) {
			if (i.value().first != location) {
				_fileLocationAliases.insert(location, i.value().first);
				_writeLocations(WriteMapWhen::Fast);
			}
			return;
		}
		if (i.value().first != location) {
			for (FileLocations::iterator j = _fileLocations.find(i.value().first), e = _fileLocations.end(); (j != e) && (j.key() == i.value().first);) {
				if (j.value() == i.value().second) {
					_fileLocations.erase(j);
					break;
				}
			}
			_fileLocationPairs.erase(i);
		}
	}
	_fileLocations.insert(location, local);
	_fileLocationPairs.insert(local.fname, FileLocationPair(location, local));
	_writeLocations(WriteMapWhen::Fast);
}

FileLocation readFileLocation(MediaKey location, bool check) {
	FileLocationAliases::const_iterator aliasIt = _fileLocationAliases.constFind(location);
	if (aliasIt != _fileLocationAliases.cend()) {
		location = aliasIt.value();
	}

	FileLocations::iterator i = _fileLocations.find(location);
	for (FileLocations::iterator i = _fileLocations.find(location); (i != _fileLocations.end()) && (i.key() == location);) {
		if (check) {
			if (!i.value().check()) {
				_fileLocationPairs.remove(i.value().fname);
				i = _fileLocations.erase(i);
				_writeLocations();
				continue;
			}
		}
		return i.value();
	}
	return FileLocation();
}

qint32 _storageImageSize(qint32 rawlen) {
	// fulllen + storagekey + type + len + data
	qint32 result = sizeof(uint32) + sizeof(quint64) * 2 + sizeof(quint32) + sizeof(quint32) + rawlen;
	if (result & 0x0F) result += 0x10 - (result & 0x0F);
	result += tdfMagicLen + sizeof(qint32) + sizeof(quint32) + 0x10 + 0x10; // magic + version + len of encrypted + part of sha1 + md5
	return result;
}

qint32 _storageStickerSize(qint32 rawlen) {
	// fulllen + storagekey + len + data
	qint32 result = sizeof(uint32) + sizeof(quint64) * 2 + sizeof(quint32) + rawlen;
	if (result & 0x0F) result += 0x10 - (result & 0x0F);
	result += tdfMagicLen + sizeof(qint32) + sizeof(quint32) + 0x10 + 0x10; // magic + version + len of encrypted + part of sha1 + md5
	return result;
}

qint32 _storageAudioSize(qint32 rawlen) {
	// fulllen + storagekey + len + data
	qint32 result = sizeof(uint32) + sizeof(quint64) * 2 + sizeof(quint32) + rawlen;
	if (result & 0x0F) result += 0x10 - (result & 0x0F);
	result += tdfMagicLen + sizeof(qint32) + sizeof(quint32) + 0x10 + 0x10; // magic + version + len of encrypted + part of sha1 + md5
	return result;
}

void writeImage(const StorageKey &location, const ImagePtr &image) {
	if (image->isNull() || !image->loaded()) return;
	if (_imagesMap.constFind(location) != _imagesMap.cend()) return;

	image->forget();
	writeImage(location, StorageImageSaved(image->savedData()), false);
}

void writeImage(const StorageKey &location, const StorageImageSaved &image, bool overwrite) {
	if (!_working()) return;

	qint32 size = _storageImageSize(image.data.size());
	StorageMap::const_iterator i = _imagesMap.constFind(location);
	if (i == _imagesMap.cend()) {
		i = _imagesMap.insert(location, FileDesc(genKey(FileOption::User), size));
		_storageImagesSize += size;
		_mapChanged = true;
		_writeMap();
	} else if (!overwrite) {
		return;
	}

	auto legacyTypeField = 0;

	EncryptedDescriptor data(sizeof(quint64) * 2 + sizeof(quint32) + sizeof(quint32) + image.data.size());
	data.stream << quint64(location.first) << quint64(location.second) << quint32(legacyTypeField) << image.data;

	FileWriteDescriptor file(i.value().first, FileOption::User);
	file.writeEncrypted(data);
	if (i.value().second != size) {
		_storageImagesSize += size;
		_storageImagesSize -= i.value().second;
		_imagesMap[location].second = size;
	}
}

class AbstractCachedLoadTask : public Task {
public:

	AbstractCachedLoadTask(const FileKey &key, const StorageKey &location, bool readImageFlag, mtpFileLoader *loader) :
		_key(key), _location(location), _readImageFlag(readImageFlag), _loader(loader), _result(0) {
	}
	void process() {
		FileReadDescriptor image;
		if (!readEncryptedFile(image, _key, FileOption::User)) {
			return;
		}

		QByteArray imageData;
		quint64 locFirst, locSecond;
		quint32 legacyTypeField = 0;
		readFromStream(image.stream, locFirst, locSecond, imageData);

		// we're saving files now before we have actual location
		//if (locFirst != _location.first || locSecond != _location.second) {
		//	return;
		//}

		_result = new Result(imageData, _readImageFlag);
	}
	void finish() {
		if (_result) {
			_loader->localLoaded(_result->image, _result->format, _result->pixmap);
		} else {
			clearInMap();
			_loader->localLoaded(StorageImageSaved());
		}
	}
	virtual void readFromStream(QDataStream &stream, quint64 &first, quint64 &second, QByteArray &data) = 0;
	virtual void clearInMap() = 0;
	virtual ~AbstractCachedLoadTask() {
		delete base::take(_result);
	}

protected:
	FileKey _key;
	StorageKey _location;
	bool _readImageFlag;
	struct Result {
		Result(const QByteArray &data, bool readImageFlag) : image(data) {
			if (readImageFlag) {
				auto realFormat = QByteArray();
				pixmap = App::pixmapFromImageInPlace(App::readImage(data, &realFormat, false));
				if (!pixmap.isNull()) {
					format = realFormat;
				}
			}
		}
		StorageImageSaved image;
		QByteArray format;
		QPixmap pixmap;

	};
	mtpFileLoader *_loader;
	Result *_result;

};

class ImageLoadTask : public AbstractCachedLoadTask {
public:
	ImageLoadTask(const FileKey &key, const StorageKey &location, mtpFileLoader *loader) :
	AbstractCachedLoadTask(key, location, true, loader) {
	}
	void readFromStream(QDataStream &stream, quint64 &first, quint64 &second, QByteArray &data) override {
		qint32 legacyTypeField = 0;
		stream >> first >> second >> legacyTypeField >> data;
	}
	void clearInMap() override {
		StorageMap::iterator j = _imagesMap.find(_location);
		if (j != _imagesMap.cend() && j->first == _key) {
			clearKey(_key, FileOption::User);
			_storageImagesSize -= j->second;
			_imagesMap.erase(j);
		}
	}
};

TaskId startImageLoad(const StorageKey &location, mtpFileLoader *loader) {
	StorageMap::const_iterator j = _imagesMap.constFind(location);
	if (j == _imagesMap.cend() || !_localLoader) {
		return 0;
	}
	return _localLoader->addTask(
		std::make_unique<ImageLoadTask>(j->first, location, loader));
}

bool willImageLoad(const StorageKey &location) {
	return _imagesMap.constFind(location) != _imagesMap.cend();
}

int32 hasImages() {
	return _imagesMap.size();
}

qint64 storageImagesSize() {
	return _storageImagesSize;
}

void writeStickerImage(const StorageKey &location, const QByteArray &sticker, bool overwrite) {
	if (!_working()) return;

	qint32 size = _storageStickerSize(sticker.size());
	StorageMap::const_iterator i = _stickerImagesMap.constFind(location);
	if (i == _stickerImagesMap.cend()) {
		i = _stickerImagesMap.insert(location, FileDesc(genKey(FileOption::User), size));
		_storageStickersSize += size;
		_mapChanged = true;
		_writeMap();
	} else if (!overwrite) {
		return;
	}
	EncryptedDescriptor data(sizeof(quint64) * 2 + sizeof(quint32) + sizeof(quint32) + sticker.size());
	data.stream << quint64(location.first) << quint64(location.second) << sticker;
	FileWriteDescriptor file(i.value().first, FileOption::User);
	file.writeEncrypted(data);
	if (i.value().second != size) {
		_storageStickersSize += size;
		_storageStickersSize -= i.value().second;
		_stickerImagesMap[location].second = size;
	}
}

class StickerImageLoadTask : public AbstractCachedLoadTask {
public:
	StickerImageLoadTask(const FileKey &key, const StorageKey &location, mtpFileLoader *loader) :
	AbstractCachedLoadTask(key, location, true, loader) {
	}
	void readFromStream(QDataStream &stream, quint64 &first, quint64 &second, QByteArray &data) {
		stream >> first >> second >> data;
	}
	void clearInMap() {
		auto j = _stickerImagesMap.find(_location);
		if (j != _stickerImagesMap.cend() && j->first == _key) {
			clearKey(j.value().first, FileOption::User);
			_storageStickersSize -= j.value().second;
			_stickerImagesMap.erase(j);
		}
	}
};

TaskId startStickerImageLoad(const StorageKey &location, mtpFileLoader *loader) {
	auto j = _stickerImagesMap.constFind(location);
	if (j == _stickerImagesMap.cend() || !_localLoader) {
		return 0;
	}
	return _localLoader->addTask(
		std::make_unique<StickerImageLoadTask>(j->first, location, loader));
}

bool willStickerImageLoad(const StorageKey &location) {
	return _stickerImagesMap.constFind(location) != _stickerImagesMap.cend();
}

bool copyStickerImage(const StorageKey &oldLocation, const StorageKey &newLocation) {
	auto i = _stickerImagesMap.constFind(oldLocation);
	if (i == _stickerImagesMap.cend()) {
		return false;
	}
	_stickerImagesMap.insert(newLocation, i.value());
	_mapChanged = true;
	_writeMap();
	return true;
}

int32 hasStickers() {
	return _stickerImagesMap.size();
}

qint64 storageStickersSize() {
	return _storageStickersSize;
}

void writeAudio(const StorageKey &location, const QByteArray &audio, bool overwrite) {
	if (!_working()) return;

	qint32 size = _storageAudioSize(audio.size());
	StorageMap::const_iterator i = _audiosMap.constFind(location);
	if (i == _audiosMap.cend()) {
		i = _audiosMap.insert(location, FileDesc(genKey(FileOption::User), size));
		_storageAudiosSize += size;
		_mapChanged = true;
		_writeMap();
	} else if (!overwrite) {
		return;
	}
	EncryptedDescriptor data(sizeof(quint64) * 2 + sizeof(quint32) + sizeof(quint32) + audio.size());
	data.stream << quint64(location.first) << quint64(location.second) << audio;
	FileWriteDescriptor file(i.value().first, FileOption::User);
	file.writeEncrypted(data);
	if (i.value().second != size) {
		_storageAudiosSize += size;
		_storageAudiosSize -= i.value().second;
		_audiosMap[location].second = size;
	}
}

class AudioLoadTask : public AbstractCachedLoadTask {
public:
	AudioLoadTask(const FileKey &key, const StorageKey &location, mtpFileLoader *loader) :
	AbstractCachedLoadTask(key, location, false, loader) {
	}
	void readFromStream(QDataStream &stream, quint64 &first, quint64 &second, QByteArray &data) {
		stream >> first >> second >> data;
	}
	void clearInMap() {
		auto j = _audiosMap.find(_location);
		if (j != _audiosMap.cend() && j->first == _key) {
			clearKey(j.value().first, FileOption::User);
			_storageAudiosSize -= j.value().second;
			_audiosMap.erase(j);
		}
	}
};

TaskId startAudioLoad(const StorageKey &location, mtpFileLoader *loader) {
	auto j = _audiosMap.constFind(location);
	if (j == _audiosMap.cend() || !_localLoader) {
		return 0;
	}
	return _localLoader->addTask(
		std::make_unique<AudioLoadTask>(j->first, location, loader));
}

bool copyAudio(const StorageKey &oldLocation, const StorageKey &newLocation) {
	auto i = _audiosMap.constFind(oldLocation);
	if (i == _audiosMap.cend()) {
		return false;
	}
	_audiosMap.insert(newLocation, i.value());
	_mapChanged = true;
	_writeMap();
	return true;
}

bool willAudioLoad(const StorageKey &location) {
	return _audiosMap.constFind(location) != _audiosMap.cend();
}

int32 hasAudios() {
	return _audiosMap.size();
}

qint64 storageAudiosSize() {
	return _storageAudiosSize;
}

qint32 _storageWebFileSize(const QString &url, qint32 rawlen) {
	// fulllen + url + len + data
	qint32 result = sizeof(uint32) + Serialize::stringSize(url) + sizeof(quint32) + rawlen;
	if (result & 0x0F) result += 0x10 - (result & 0x0F);
	result += tdfMagicLen + sizeof(qint32) + sizeof(quint32) + 0x10 + 0x10; // magic + version + len of encrypted + part of sha1 + md5
	return result;
}

void writeWebFile(const QString &url, const QByteArray &content, bool overwrite) {
	if (!_working()) return;

	qint32 size = _storageWebFileSize(url, content.size());
	WebFilesMap::const_iterator i = _webFilesMap.constFind(url);
	if (i == _webFilesMap.cend()) {
		i = _webFilesMap.insert(url, FileDesc(genKey(FileOption::User), size));
		_storageWebFilesSize += size;
		_writeLocations();
	} else if (!overwrite) {
		return;
	}
	EncryptedDescriptor data(Serialize::stringSize(url) + sizeof(quint32) + sizeof(quint32) + content.size());
	data.stream << url << content;
	FileWriteDescriptor file(i.value().first, FileOption::User);
	file.writeEncrypted(data);
	if (i.value().second != size) {
		_storageWebFilesSize += size;
		_storageWebFilesSize -= i.value().second;
		_webFilesMap[url].second = size;
	}
}

class WebFileLoadTask : public Task {
public:
	WebFileLoadTask(const FileKey &key, const QString &url, webFileLoader *loader)
		: _key(key)
		, _url(url)
		, _loader(loader)
		, _result(0) {
	}
	void process() {
		FileReadDescriptor image;
		if (!readEncryptedFile(image, _key, FileOption::User)) {
			return;
		}

		QByteArray imageData;
		QString url;
		image.stream >> url >> imageData;

		_result = new Result(imageData);
	}
	void finish() {
		if (_result) {
			_loader->localLoaded(_result->image, _result->format, _result->pixmap);
		} else {
			WebFilesMap::iterator j = _webFilesMap.find(_url);
			if (j != _webFilesMap.cend() && j->first == _key) {
				clearKey(j.value().first, FileOption::User);
				_storageWebFilesSize -= j.value().second;
				_webFilesMap.erase(j);
			}
			_loader->localLoaded(StorageImageSaved());
		}
	}
	virtual ~WebFileLoadTask() {
		delete base::take(_result);
	}

protected:
	FileKey _key;
	QString _url;
	struct Result {
		explicit Result(const QByteArray &data) : image(data) {
			QByteArray guessFormat;
			pixmap = App::pixmapFromImageInPlace(App::readImage(data, &guessFormat, false));
			if (!pixmap.isNull()) {
				format = guessFormat;
			}
		}
		StorageImageSaved image;
		QByteArray format;
		QPixmap pixmap;

	};
	webFileLoader *_loader;
	Result *_result;

};

TaskId startWebFileLoad(const QString &url, webFileLoader *loader) {
	WebFilesMap::const_iterator j = _webFilesMap.constFind(url);
	if (j == _webFilesMap.cend() || !_localLoader) {
		return 0;
	}
	return _localLoader->addTask(
		std::make_unique<WebFileLoadTask>(j->first, url, loader));
}

bool willWebFileLoad(const QString &url) {
	return _webFilesMap.constFind(url) != _webFilesMap.cend();
}

int32 hasWebFiles() {
	return _webFilesMap.size();
}

qint64 storageWebFilesSize() {
	return _storageWebFilesSize;
}

class CountWaveformTask : public Task {
public:
	CountWaveformTask(DocumentData *doc)
		: _doc(doc)
		, _loc(doc->location(true))
		, _data(doc->data())
		, _wavemax(0) {
		if (_data.isEmpty() && !_loc.accessEnable()) {
			_doc = 0;
		}
	}
	void process() {
		if (!_doc) return;

		_waveform = audioCountWaveform(_loc, _data);
		uchar wavemax = 0;
		for (int32 i = 0, l = _waveform.size(); i < l; ++i) {
			uchar waveat = _waveform.at(i);
			if (wavemax < waveat) wavemax = waveat;
		}
		_wavemax = wavemax;
	}
	void finish() {
		if (const auto voice = _doc ? _doc->voice() : nullptr) {
			if (!_waveform.isEmpty()) {
				voice->waveform = _waveform;
				voice->wavemax = _wavemax;
			}
			if (voice->waveform.isEmpty()) {
				voice->waveform.resize(1);
				voice->waveform[0] = -2;
				voice->wavemax = 0;
			} else if (voice->waveform[0] < 0) {
				voice->waveform[0] = -2;
				voice->wavemax = 0;
			}
			Auth().data().requestDocumentViewRepaint(_doc);
		}
	}
	virtual ~CountWaveformTask() {
		if (_data.isEmpty() && _doc) {
			_loc.accessDisable();
		}
	}

protected:
	DocumentData *_doc;
	FileLocation _loc;
	QByteArray _data;
	VoiceWaveform _waveform;
	char _wavemax;

};

void countVoiceWaveform(DocumentData *document) {
	if (const auto voice = document->voice()) {
		if (_localLoader) {
			voice->waveform.resize(1 + sizeof(TaskId));
			voice->waveform[0] = -1; // counting
			TaskId taskId = _localLoader->addTask(
				std::make_unique<CountWaveformTask>(document));
			memcpy(voice->waveform.data() + 1, &taskId, sizeof(taskId));
		}
	}
}

void cancelTask(TaskId id) {
	if (_localLoader) {
		_localLoader->cancelTask(id);
	}
}

void _writeStickerSet(QDataStream &stream, const Stickers::Set &set) {
	bool notLoaded = (set.flags & MTPDstickerSet_ClientFlag::f_not_loaded);
	if (notLoaded) {
		stream
			<< quint64(set.id)
			<< quint64(set.access)
			<< set.title
			<< set.shortName
			<< qint32(-set.count)
			<< qint32(set.hash)
			<< qint32(set.flags);
		if (AppVersion > 1002008) {
			stream << qint32(set.installDate);
		}
		return;
	} else {
		if (set.stickers.isEmpty()) return;
	}

	stream
		<< quint64(set.id)
		<< quint64(set.access)
		<< set.title
		<< set.shortName
		<< qint32(set.stickers.size())
		<< qint32(set.hash)
		<< qint32(set.flags);
	if (AppVersion > 1002008) {
		stream << qint32(set.installDate);
	}
	for (auto j = set.stickers.cbegin(), e = set.stickers.cend(); j != e; ++j) {
		Serialize::Document::writeToStream(stream, *j);
	}
	if (AppVersion > 1002008) {
		stream << qint32(set.dates.size());
		if (!set.dates.empty()) {
			Assert(set.dates.size() == set.stickers.size());
			for (const auto date : set.dates) {
				stream << qint32(date);
			}
		}
	}

	if (AppVersion > 9018) {
		stream << qint32(set.emoji.size());
		for (auto j = set.emoji.cbegin(), e = set.emoji.cend(); j != e; ++j) {
			stream << j.key()->id() << qint32(j->size());
			for (int32 k = 0, l = j->size(); k < l; ++k) {
				stream << quint64(j->at(k)->id);
			}
		}
	}
}

// In generic method _writeStickerSets() we look through all the sets and call a
// callback on each set to see, if we write it, skip it or abort the whole write.
enum class StickerSetCheckResult {
	Write,
	Skip,
	Abort,
};

// CheckSet is a functor on Stickers::Set, which returns a StickerSetCheckResult.
template <typename CheckSet>
void _writeStickerSets(FileKey &stickersKey, CheckSet checkSet, const Stickers::Order &order) {
	if (!_working()) return;

	auto &sets = Auth().data().stickerSets();
	if (sets.isEmpty()) {
		if (stickersKey) {
			clearKey(stickersKey);
			stickersKey = 0;
			_mapChanged = true;
		}
		_writeMap();
		return;
	}
	int32 setsCount = 0;
	QByteArray hashToWrite;
	quint32 size = sizeof(quint32) + Serialize::bytearraySize(hashToWrite);
	for_const (auto &set, sets) {
		auto result = checkSet(set);
		if (result == StickerSetCheckResult::Abort) {
			return;
		} else if (result == StickerSetCheckResult::Skip) {
			continue;
		}

		// id + access + title + shortName + stickersCount + hash + flags + installDate
		size += sizeof(quint64) * 2 + Serialize::stringSize(set.title) + Serialize::stringSize(set.shortName) + sizeof(quint32) + sizeof(qint32) * 3;
		for_const (auto &sticker, set.stickers) {
			size += Serialize::Document::sizeInStream(sticker);
		}
		size += sizeof(qint32); // dates count
		if (!set.dates.empty()) {
			Assert(set.stickers.size() == set.dates.size());
			size += set.dates.size() * sizeof(qint32);
		}

		size += sizeof(qint32); // emojiCount
		for (auto j = set.emoji.cbegin(), e = set.emoji.cend(); j != e; ++j) {
			size += Serialize::stringSize(j.key()->id()) + sizeof(qint32) + (j->size() * sizeof(quint64));
		}

		++setsCount;
	}
	if (!setsCount && order.isEmpty()) {
		if (stickersKey) {
			clearKey(stickersKey);
			stickersKey = 0;
			_mapChanged = true;
		}
		_writeMap();
		return;
	}
	size += sizeof(qint32) + (order.size() * sizeof(quint64));

	if (!stickersKey) {
		stickersKey = genKey();
		_mapChanged = true;
		_writeMap(WriteMapWhen::Fast);
	}
	EncryptedDescriptor data(size);
	data.stream << quint32(setsCount) << hashToWrite;
	for_const (auto &set, sets) {
		auto result = checkSet(set);
		if (result == StickerSetCheckResult::Abort) {
			return;
		} else if (result == StickerSetCheckResult::Skip) {
			continue;
		}
		_writeStickerSet(data.stream, set);
	}
	data.stream << order;

	FileWriteDescriptor file(stickersKey);
	file.writeEncrypted(data);
}

void _readStickerSets(FileKey &stickersKey, Stickers::Order *outOrder = nullptr, MTPDstickerSet::Flags readingFlags = 0) {
	FileReadDescriptor stickers;
	if (!readEncryptedFile(stickers, stickersKey)) {
		clearKey(stickersKey);
		stickersKey = 0;
		_writeMap();
		return;
	}

	bool readingInstalled = (readingFlags == MTPDstickerSet::Flag::f_installed_date);

	auto &sets = Auth().data().stickerSetsRef();
	if (outOrder) outOrder->clear();

	quint32 cnt;
	QByteArray hash;
	stickers.stream >> cnt >> hash; // ignore hash, it is counted
	if (readingInstalled && stickers.version < 8019) { // bad data in old caches
		cnt += 2; // try to read at least something
	}
	for (uint32 i = 0; i < cnt; ++i) {
		quint64 setId = 0, setAccess = 0;
		QString setTitle, setShortName;
		qint32 scnt = 0;
		auto setInstallDate = qint32(0);

		stickers.stream
			>> setId
			>> setAccess
			>> setTitle
			>> setShortName
			>> scnt;

		qint32 setHash = 0;
		MTPDstickerSet::Flags setFlags = 0;
		if (stickers.version > 8033) {
			qint32 setFlagsValue = 0;
			stickers.stream >> setHash >> setFlagsValue;
			setFlags = MTPDstickerSet::Flags::from_raw(setFlagsValue);
			if (setFlags & MTPDstickerSet_ClientFlag::f_not_loaded__old) {
				setFlags &= ~MTPDstickerSet_ClientFlag::f_not_loaded__old;
				setFlags |= MTPDstickerSet_ClientFlag::f_not_loaded;
			}
		}
		if (stickers.version > 1002008) {
			stickers.stream >> setInstallDate;
		}
		if (readingInstalled && stickers.version < 9061) {
			setFlags |= MTPDstickerSet::Flag::f_installed_date;
		}

		if (setId == Stickers::DefaultSetId) {
			setTitle = lang(lng_stickers_default_set);
			setFlags |= MTPDstickerSet::Flag::f_official | MTPDstickerSet_ClientFlag::f_special;
			if (readingInstalled && outOrder && stickers.version < 9061) {
				outOrder->push_front(setId);
			}
		} else if (setId == Stickers::CustomSetId) {
			setTitle = qsl("Custom stickers");
			setFlags |= MTPDstickerSet_ClientFlag::f_special;
		} else if (setId == Stickers::CloudRecentSetId) {
			setTitle = lang(lng_recent_stickers);
			setFlags |= MTPDstickerSet_ClientFlag::f_special;
		} else if (setId == Stickers::FavedSetId) {
			setTitle = Lang::Hard::FavedSetTitle();
			setFlags |= MTPDstickerSet_ClientFlag::f_special;
		} else if (setId) {
			if (readingInstalled && outOrder && stickers.version < 9061) {
				outOrder->push_back(setId);
			}
		} else {
			continue;
		}

		auto it = sets.find(setId);
		if (it == sets.cend()) {
			// We will set this flags from order lists when reading those stickers.
			setFlags &= ~(MTPDstickerSet::Flag::f_installed_date | MTPDstickerSet_ClientFlag::f_featured);
			it = sets.insert(setId, Stickers::Set(
				setId,
				setAccess,
				setTitle,
				setShortName,
				0,
				setHash,
				MTPDstickerSet::Flags(setFlags),
				setInstallDate));
		}
		auto &set = it.value();
		auto inputSet = MTP_inputStickerSetID(MTP_long(set.id), MTP_long(set.access));

		if (scnt < 0) { // disabled not loaded set
			if (!set.count || set.stickers.isEmpty()) {
				set.count = -scnt;
			}
			continue;
		}

		bool fillStickers = set.stickers.isEmpty();
		if (fillStickers) {
			set.stickers.reserve(scnt);
			set.count = 0;
		}

		Serialize::Document::StickerSetInfo info(setId, setAccess, setShortName);
		OrderedSet<DocumentId> read;
		for (int32 j = 0; j < scnt; ++j) {
			auto document = Serialize::Document::readStickerFromStream(stickers.version, stickers.stream, info);
			if (!document || !document->sticker()) continue;

			if (read.contains(document->id)) continue;
			read.insert(document->id);

			if (fillStickers) {
				set.stickers.push_back(document);
				if (!(set.flags & MTPDstickerSet_ClientFlag::f_special)) {
					if (document->sticker()->set.type() != mtpc_inputStickerSetID) {
						document->sticker()->set = inputSet;
					}
				}
				++set.count;
			}
		}

		if (stickers.version > 1002008) {
			auto datesCount = qint32(0);
			stickers.stream >> datesCount;
			if (datesCount > 0) {
				if (datesCount != scnt) {
					// Bad file.
					return;
				}
				set.dates.reserve(datesCount);
				for (auto i = 0; i != datesCount; ++i) {
					auto date = qint32();
					stickers.stream >> date;
					if (set.id == Stickers::CloudRecentSetId) {
						set.dates.push_back(TimeId(date));
					}
				}
			}
		}

		if (stickers.version > 9018) {
			qint32 emojiCount;
			stickers.stream >> emojiCount;
			for (int32 j = 0; j < emojiCount; ++j) {
				QString emojiString;
				qint32 stickersCount;
				stickers.stream >> emojiString >> stickersCount;
				Stickers::Pack pack;
				pack.reserve(stickersCount);
				for (int32 k = 0; k < stickersCount; ++k) {
					quint64 id;
					stickers.stream >> id;
					const auto doc = Auth().data().document(id);
					if (!doc->sticker()) continue;

					pack.push_back(doc);
				}
				if (fillStickers) {
					if (auto emoji = Ui::Emoji::Find(emojiString)) {
						emoji = emoji->original();
						set.emoji.insert(emoji, pack);
					}
				}
			}
		}
	}

	// Read orders of installed and featured stickers.
	if (outOrder && stickers.version >= 9061) {
		stickers.stream >> *outOrder;
	}

	// Set flags that we dropped above from the order.
	if (readingFlags && outOrder) {
		for_const (auto setId, *outOrder) {
			auto it = sets.find(setId);
			if (it != sets.cend()) {
				it->flags |= readingFlags;
				if (readingInstalled && !it->installDate) {
					it->installDate = kDefaultStickerInstallDate;
				}
			}
		}
	}
}

void writeInstalledStickers() {
	if (!Global::started()) return;

	_writeStickerSets(_installedStickersKey, [](const Stickers::Set &set) {
		if (set.id == Stickers::CloudRecentSetId || set.id == Stickers::FavedSetId) { // separate files for them
			return StickerSetCheckResult::Skip;
		} else if (set.flags & MTPDstickerSet_ClientFlag::f_special) {
			if (set.stickers.isEmpty()) { // all other special are "installed"
				return StickerSetCheckResult::Skip;
			}
		} else if (!(set.flags & MTPDstickerSet::Flag::f_installed_date) || (set.flags & MTPDstickerSet::Flag::f_archived)) {
			return StickerSetCheckResult::Skip;
		} else if (set.flags & MTPDstickerSet_ClientFlag::f_not_loaded) { // waiting to receive
			return StickerSetCheckResult::Abort;
		} else if (set.stickers.isEmpty()) {
			return StickerSetCheckResult::Skip;
		}
		return StickerSetCheckResult::Write;
	}, Auth().data().stickerSetsOrder());
}

void writeFeaturedStickers() {
	if (!Global::started()) return;

	_writeStickerSets(_featuredStickersKey, [](const Stickers::Set &set) {
		if (set.id == Stickers::CloudRecentSetId || set.id == Stickers::FavedSetId) { // separate files for them
			return StickerSetCheckResult::Skip;
		} else if (set.flags & MTPDstickerSet_ClientFlag::f_special) {
			return StickerSetCheckResult::Skip;
		} else if (!(set.flags & MTPDstickerSet_ClientFlag::f_featured)) {
			return StickerSetCheckResult::Skip;
		} else if (set.flags & MTPDstickerSet_ClientFlag::f_not_loaded) { // waiting to receive
			return StickerSetCheckResult::Abort;
		} else if (set.stickers.isEmpty()) {
			return StickerSetCheckResult::Skip;
		}
		return StickerSetCheckResult::Write;
	}, Auth().data().featuredStickerSetsOrder());
}

void writeRecentStickers() {
	if (!Global::started()) return;

	_writeStickerSets(_recentStickersKey, [](const Stickers::Set &set) {
		if (set.id != Stickers::CloudRecentSetId || set.stickers.isEmpty()) {
			return StickerSetCheckResult::Skip;
		}
		return StickerSetCheckResult::Write;
	}, Stickers::Order());
}

void writeFavedStickers() {
	if (!Global::started()) return;

	_writeStickerSets(_favedStickersKey, [](const Stickers::Set &set) {
		if (set.id != Stickers::FavedSetId || set.stickers.isEmpty()) {
			return StickerSetCheckResult::Skip;
		}
		return StickerSetCheckResult::Write;
	}, Stickers::Order());
}

void writeArchivedStickers() {
	if (!Global::started()) return;

	_writeStickerSets(_archivedStickersKey, [](const Stickers::Set &set) {
		if (!(set.flags & MTPDstickerSet::Flag::f_archived) || set.stickers.isEmpty()) {
			return StickerSetCheckResult::Skip;
		}
		return StickerSetCheckResult::Write;
	}, Auth().data().archivedStickerSetsOrder());
}

void importOldRecentStickers() {
	if (!_recentStickersKeyOld) return;

	FileReadDescriptor stickers;
	if (!readEncryptedFile(stickers, _recentStickersKeyOld)) {
		clearKey(_recentStickersKeyOld);
		_recentStickersKeyOld = 0;
		_writeMap();
		return;
	}

	auto &sets = Auth().data().stickerSetsRef();
	sets.clear();

	auto &order = Auth().data().stickerSetsOrderRef();
	order.clear();

	auto &recent = cRefRecentStickers();
	recent.clear();

	auto &def = sets.insert(Stickers::DefaultSetId, Stickers::Set(
		Stickers::DefaultSetId,
		uint64(0),
		lang(lng_stickers_default_set),
		QString(),
		0, // count
		0, // hash
		(MTPDstickerSet::Flag::f_official
			| MTPDstickerSet::Flag::f_installed_date
			| MTPDstickerSet_ClientFlag::f_special),
		kDefaultStickerInstallDate)).value();
	auto &custom = sets.insert(Stickers::CustomSetId, Stickers::Set(
		Stickers::CustomSetId,
		uint64(0),
		qsl("Custom stickers"),
		QString(),
		0, // count
		0, // hash
		(MTPDstickerSet::Flag::f_installed_date
			| MTPDstickerSet_ClientFlag::f_special),
		kDefaultStickerInstallDate)).value();

	QMap<uint64, bool> read;
	while (!stickers.stream.atEnd()) {
		quint64 id, access;
		QString name, mime, alt;
		qint32 date, dc, size, width, height, type;
		qint16 value;
		stickers.stream >> id >> value >> access >> date >> name >> mime >> dc >> size >> width >> height >> type;
		if (stickers.version >= 7021) {
			stickers.stream >> alt;
		}
		if (!value || read.contains(id)) continue;
		read.insert(id, true);

		QVector<MTPDocumentAttribute> attributes;
		if (!name.isEmpty()) attributes.push_back(MTP_documentAttributeFilename(MTP_string(name)));
		if (type == AnimatedDocument) {
			attributes.push_back(MTP_documentAttributeAnimated());
		} else if (type == StickerDocument) {
			attributes.push_back(MTP_documentAttributeSticker(MTP_flags(0), MTP_string(alt), MTP_inputStickerSetEmpty(), MTPMaskCoords()));
		}
		if (width > 0 && height > 0) {
			attributes.push_back(MTP_documentAttributeImageSize(MTP_int(width), MTP_int(height)));
		}

		const auto doc = Auth().data().document(
			id,
			access,
			int32(0),
			date,
			attributes,
			mime,
			ImagePtr(),
			dc,
			size,
			StorageImageLocation());
		if (!doc->sticker()) continue;

		if (value > 0) {
			def.stickers.push_back(doc);
			++def.count;
		} else {
			custom.stickers.push_back(doc);
			++custom.count;
		}
		if (recent.size() < Global::StickersRecentLimit() && qAbs(value) > 1) {
			recent.push_back(qMakePair(doc, qAbs(value)));
		}
	}
	if (def.stickers.isEmpty()) {
		sets.remove(Stickers::DefaultSetId);
	} else {
		order.push_front(Stickers::DefaultSetId);
	}
	if (custom.stickers.isEmpty()) sets.remove(Stickers::CustomSetId);

	writeInstalledStickers();
	writeUserSettings();

	clearKey(_recentStickersKeyOld);
	_recentStickersKeyOld = 0;
	_writeMap();
}

void readInstalledStickers() {
	if (!_installedStickersKey) {
		return importOldRecentStickers();
	}

	Auth().data().stickerSetsRef().clear();
	_readStickerSets(
		_installedStickersKey,
		&Auth().data().stickerSetsOrderRef(),
		MTPDstickerSet::Flag::f_installed_date);
}

void readFeaturedStickers() {
	_readStickerSets(
		_featuredStickersKey,
		&Auth().data().featuredStickerSetsOrderRef(),
		MTPDstickerSet::Flags() | MTPDstickerSet_ClientFlag::f_featured);

	auto &sets = Auth().data().stickerSets();
	int unreadCount = 0;
	for_const (auto setId, Auth().data().featuredStickerSetsOrder()) {
		auto it = sets.constFind(setId);
		if (it != sets.cend() && (it->flags & MTPDstickerSet_ClientFlag::f_unread)) {
			++unreadCount;
		}
	}
	Auth().data().setFeaturedStickerSetsUnreadCount(unreadCount);
}

void readRecentStickers() {
	_readStickerSets(_recentStickersKey);
}

void readFavedStickers() {
	_readStickerSets(_favedStickersKey);
}

void readArchivedStickers() {
	static bool archivedStickersRead = false;
	if (!archivedStickersRead) {
		_readStickerSets(_archivedStickersKey, &Auth().data().archivedStickerSetsOrderRef());
		archivedStickersRead = true;
	}
}

int32 countDocumentVectorHash(const QVector<DocumentData*> vector) {
	uint32 acc = 0;
	for_const (auto doc, vector) {
		auto docId = doc->id;
		acc = (acc * 20261) + uint32(docId >> 32);
		acc = (acc * 20261) + uint32(docId & 0xFFFFFFFF);
	}
	return int32(acc & 0x7FFFFFFF);
}

int32 countSpecialStickerSetHash(uint64 setId) {
	auto &sets = Auth().data().stickerSets();
	auto it = sets.constFind(setId);
	if (it != sets.cend()) {
		return countDocumentVectorHash(it->stickers);
	}
	return 0;
}

int32 countStickersHash(bool checkOutdatedInfo) {
	uint32 acc = 0;
	bool foundOutdated = false;
	auto &sets = Auth().data().stickerSets();
	auto &order = Auth().data().stickerSetsOrder();
	for (auto i = order.cbegin(), e = order.cend(); i != e; ++i) {
		auto j = sets.constFind(*i);
		if (j != sets.cend()) {
			if (j->id == Stickers::DefaultSetId) {
				foundOutdated = true;
			} else if (!(j->flags & MTPDstickerSet_ClientFlag::f_special)
				&& !(j->flags & MTPDstickerSet::Flag::f_archived)) {
				acc = (acc * 20261) + j->hash;
			}
		}
	}
	return (!checkOutdatedInfo || !foundOutdated) ? int32(acc & 0x7FFFFFFF) : 0;
}

int32 countRecentStickersHash() {
	return countSpecialStickerSetHash(Stickers::CloudRecentSetId);
}

int32 countFavedStickersHash() {
	return countSpecialStickerSetHash(Stickers::FavedSetId);
}

int32 countFeaturedStickersHash() {
	uint32 acc = 0;
	auto &sets = Auth().data().stickerSets();
	auto &featured = Auth().data().featuredStickerSetsOrder();
	for_const (auto setId, featured) {
		acc = (acc * 20261) + uint32(setId >> 32);
		acc = (acc * 20261) + uint32(setId & 0xFFFFFFFF);

		auto it = sets.constFind(setId);
		if (it != sets.cend() && (it->flags & MTPDstickerSet_ClientFlag::f_unread)) {
			acc = (acc * 20261) + 1U;
		}
	}
	return int32(acc & 0x7FFFFFFF);
}

int32 countSavedGifsHash() {
	return countDocumentVectorHash(Auth().data().savedGifs());
}

void writeSavedGifs() {
	if (!_working()) return;

	auto &saved = Auth().data().savedGifs();
	if (saved.isEmpty()) {
		if (_savedGifsKey) {
			clearKey(_savedGifsKey);
			_savedGifsKey = 0;
			_mapChanged = true;
		}
		_writeMap();
	} else {
		quint32 size = sizeof(quint32); // count
		for_const (auto gif, saved) {
			size += Serialize::Document::sizeInStream(gif);
		}

		if (!_savedGifsKey) {
			_savedGifsKey = genKey();
			_mapChanged = true;
			_writeMap(WriteMapWhen::Fast);
		}
		EncryptedDescriptor data(size);
		data.stream << quint32(saved.size());
		for_const (auto gif, saved) {
			Serialize::Document::writeToStream(data.stream, gif);
		}
		FileWriteDescriptor file(_savedGifsKey);
		file.writeEncrypted(data);
	}
}

void readSavedGifs() {
	if (!_savedGifsKey) return;

	FileReadDescriptor gifs;
	if (!readEncryptedFile(gifs, _savedGifsKey)) {
		clearKey(_savedGifsKey);
		_savedGifsKey = 0;
		_writeMap();
		return;
	}

	auto &saved = Auth().data().savedGifsRef();
	saved.clear();

	quint32 cnt;
	gifs.stream >> cnt;
	saved.reserve(cnt);
	OrderedSet<DocumentId> read;
	for (uint32 i = 0; i < cnt; ++i) {
		auto document = Serialize::Document::readFromStream(gifs.version, gifs.stream);
		if (!document || !document->isGifv()) continue;

		if (read.contains(document->id)) continue;
		read.insert(document->id);

		saved.push_back(document);
	}
}

void writeBackground(int32 id, const QImage &img) {
	if (!_working() || !_backgroundCanWrite) return;

	if (!LocalKey) {
		LOG(("App Error: localkey not created in writeBackground()"));
		return;
	}

	QByteArray bmp;
	if (!img.isNull()) {
		QBuffer buf(&bmp);
		if (!img.save(&buf, "BMP")) return;
	}
	if (!_backgroundKey) {
		_backgroundKey = genKey();
		_mapChanged = true;
		_writeMap(WriteMapWhen::Fast);
	}
	quint32 size = sizeof(qint32) + sizeof(quint32) + (bmp.isEmpty() ? 0 : (sizeof(quint32) + bmp.size()));
	EncryptedDescriptor data(size);
	data.stream << qint32(id) << bmp;

	FileWriteDescriptor file(_backgroundKey);
	file.writeEncrypted(data);
}

bool readBackground() {
	if (_backgroundWasRead) {
		return false;
	}
	_backgroundWasRead = true;

	FileReadDescriptor bg;
	if (!readEncryptedFile(bg, _backgroundKey)) {
		clearKey(_backgroundKey);
		_backgroundKey = 0;
		_writeMap();
		return false;
	}

	QByteArray pngData;
	qint32 id;
	bg.stream >> id >> pngData;
	auto oldEmptyImage = (bg.stream.status() != QDataStream::Ok);
	if (oldEmptyImage
		|| id == Window::Theme::kInitialBackground
		|| id == Window::Theme::kDefaultBackground) {
		_backgroundCanWrite = false;
		if (oldEmptyImage || bg.version < 8005) {
			Window::Theme::Background()->setImage(Window::Theme::kDefaultBackground);
			Window::Theme::Background()->setTile(false);
		} else {
			Window::Theme::Background()->setImage(id);
		}
		_backgroundCanWrite = true;
		return true;
	} else if (id == Window::Theme::kThemeBackground && pngData.isEmpty()) {
		_backgroundCanWrite = false;
		Window::Theme::Background()->setImage(id);
		_backgroundCanWrite = true;
		return true;
	}

	QImage image;
	QBuffer buf(&pngData);
	QImageReader reader(&buf);
#ifndef OS_MAC_OLD
	reader.setAutoTransform(true);
#endif // OS_MAC_OLD
	if (reader.read(&image)) {
		_backgroundCanWrite = false;
		Window::Theme::Background()->setImage(id, std::move(image));
		_backgroundCanWrite = true;
		return true;
	}
	return false;
}

bool readThemeUsingKey(FileKey key) {
	FileReadDescriptor theme;
	if (!readEncryptedFile(theme, key, FileOption::Safe, SettingsKey)) {
		return false;
	}

	QByteArray themeContent;
	QString pathRelative, pathAbsolute;
	Window::Theme::Cached cache;
	theme.stream >> themeContent;
	theme.stream >> pathRelative >> pathAbsolute;
	if (theme.stream.status() != QDataStream::Ok) {
		return false;
	}

	_themeAbsolutePath = pathAbsolute;
	_themePaletteAbsolutePath = Window::Theme::IsPaletteTestingPath(pathAbsolute) ? pathAbsolute : QString();

	QFile file(pathRelative);
	if (pathRelative.isEmpty() || !file.exists()) {
		file.setFileName(pathAbsolute);
	}

	auto changed = false;
	if (!file.fileName().isEmpty() && file.exists() && file.open(QIODevice::ReadOnly)) {
		if (file.size() > kThemeFileSizeLimit) {
			LOG(("Error: theme file too large: %1 (should be less than 5 MB, got %2)").arg(file.fileName()).arg(file.size()));
			return false;
		}
		auto fileContent = file.readAll();
		file.close();
		if (themeContent != fileContent) {
			themeContent = fileContent;
			changed = true;
		}
	}
	if (!changed) {
		quint32 backgroundIsTiled = 0;
		theme.stream >> cache.paletteChecksum >> cache.contentChecksum >> cache.colors >> cache.background >> backgroundIsTiled;
		cache.tiled = (backgroundIsTiled == 1);
		if (theme.stream.status() != QDataStream::Ok) {
			return false;
		}
	}
	return Window::Theme::Load(pathRelative, pathAbsolute, themeContent, cache);
}

void writeTheme(const QString &pathRelative, const QString &pathAbsolute, const QByteArray &content, const Window::Theme::Cached &cache) {
	if (content.isEmpty()) {
		_themeAbsolutePath = _themePaletteAbsolutePath = QString();
		if (_themeKey) {
			clearKey(_themeKey);
			_themeKey = 0;
			writeSettings();
		}
		return;
	}

	_themeAbsolutePath = pathAbsolute;
	_themePaletteAbsolutePath = Window::Theme::IsPaletteTestingPath(pathAbsolute) ? pathAbsolute : QString();
	if (!_themeKey) {
		_themeKey = genKey(FileOption::Safe);
		writeSettings();
	}

	auto backgroundTiled = static_cast<quint32>(cache.tiled ? 1 : 0);
	quint32 size = Serialize::bytearraySize(content);
	size += Serialize::stringSize(pathRelative) + Serialize::stringSize(pathAbsolute);
	size += sizeof(int32) * 2 + Serialize::bytearraySize(cache.colors) + Serialize::bytearraySize(cache.background) + sizeof(quint32);
	EncryptedDescriptor data(size);
	data.stream << content;
	data.stream << pathRelative << pathAbsolute;
	data.stream << cache.paletteChecksum << cache.contentChecksum << cache.colors << cache.background << backgroundTiled;

	FileWriteDescriptor file(_themeKey, FileOption::Safe);
	file.writeEncrypted(data, SettingsKey);
}

void clearTheme() {
	writeTheme(QString(), QString(), QByteArray(), Window::Theme::Cached());
}

void readTheme() {
	if (_themeKey && !readThemeUsingKey(_themeKey)) {
		clearTheme();
	}
}

bool hasTheme() {
	return (_themeKey != 0);
}

void readLangPack() {
	FileReadDescriptor langpack;
	if (!_langPackKey || !readEncryptedFile(langpack, _langPackKey, FileOption::Safe, SettingsKey)) {
		return;
	}
	auto data = QByteArray();
	langpack.stream >> data;
	if (langpack.stream.status() == QDataStream::Ok) {
		Lang::Current().fillFromSerialized(data);
	}
}

void writeLangPack() {
	auto langpack = Lang::Current().serialize();
	if (!_langPackKey) {
		_langPackKey = genKey(FileOption::Safe);
		writeSettings();
	}

	EncryptedDescriptor data(Serialize::bytearraySize(langpack));
	data.stream << langpack;

	FileWriteDescriptor file(_langPackKey, FileOption::Safe);
	file.writeEncrypted(data, SettingsKey);
}

QString themePaletteAbsolutePath() {
	return _themePaletteAbsolutePath;
}

QString themeAbsolutePath() {
	return _themeAbsolutePath;
}

bool copyThemeColorsToPalette(const QString &path) {
	if (!_themeKey) {
		return false;
	}

	FileReadDescriptor theme;
	if (!readEncryptedFile(theme, _themeKey, FileOption::Safe, SettingsKey)) {
		return false;
	}

	QByteArray themeContent;
	theme.stream >> themeContent;
	if (theme.stream.status() != QDataStream::Ok) {
		return false;
	}

	return Window::Theme::CopyColorsToPalette(path, themeContent);
}

uint32 _peerSize(PeerData *peer) {
	uint32 result = sizeof(quint64) + sizeof(quint64) + Serialize::storageImageLocationSize();
	if (peer->isUser()) {
		UserData *user = peer->asUser();

		// first + last + phone + username + access
		result += Serialize::stringSize(user->firstName) + Serialize::stringSize(user->lastName) + Serialize::stringSize(user->phone()) + Serialize::stringSize(user->username) + sizeof(quint64);

		// flags
		if (AppVersion >= 9012) {
			result += sizeof(qint32);
		}

		// onlineTill + contact + botInfoVersion
		result += sizeof(qint32) + sizeof(qint32) + sizeof(qint32);
	} else if (peer->isChat()) {
		ChatData *chat = peer->asChat();

		// name + count + date + version + admin + old forbidden + left + inviteLink
		result += Serialize::stringSize(chat->name) + sizeof(qint32) + sizeof(qint32) + sizeof(qint32) + sizeof(qint32) + sizeof(qint32) + sizeof(quint32) + Serialize::stringSize(chat->inviteLink());
	} else if (peer->isChannel()) {
		ChannelData *channel = peer->asChannel();

		// name + access + date + version + old forbidden + flags + inviteLink
		result += Serialize::stringSize(channel->name) + sizeof(quint64) + sizeof(qint32) + sizeof(qint32) + sizeof(qint32) + sizeof(quint32) + Serialize::stringSize(channel->inviteLink());
	}
	return result;
}

void _writePeer(QDataStream &stream, PeerData *peer) {
	stream << quint64(peer->id) << quint64(peer->userpicPhotoId());
	Serialize::writeStorageImageLocation(stream, peer->userpicLocation());
	if (const auto user = peer->asUser()) {
		stream
			<< user->firstName
			<< user->lastName
			<< user->phone()
			<< user->username
			<< quint64(user->accessHash());
		if (AppVersion >= 9012) {
			stream << qint32(user->flags());
		}
		if (AppVersion >= 9016) {
			const auto botInlinePlaceholder = user->botInfo
				? user->botInfo->inlinePlaceholder
				: QString();
			stream << botInlinePlaceholder;
		}
		const auto contactSerialized = [&] {
			switch (user->contactStatus()) {
			case UserData::ContactStatus::Contact: return 1;
			case UserData::ContactStatus::CanAdd: return 0;
			case UserData::ContactStatus::PhoneUnknown: return -1;
			}
			Unexpected("contactStatus in _writePeer()");
		}();
		stream
			<< qint32(user->onlineTill)
			<< qint32(contactSerialized)
			<< qint32(user->botInfo ? user->botInfo->version : -1);
	} else if (const auto chat = peer->asChat()) {
		stream
			<< chat->name
			<< qint32(chat->count)
			<< qint32(chat->date)
			<< qint32(chat->version)
			<< qint32(chat->creator)
			<< qint32(0)
			<< quint32(chat->flags())
			<< chat->inviteLink();
	} else if (const auto channel = peer->asChannel()) {
		stream
			<< channel->name
			<< quint64(channel->access)
			<< qint32(channel->date)
			<< qint32(channel->version)
			<< qint32(0)
			<< quint32(channel->flags())
			<< channel->inviteLink();
	}
}

PeerData *_readPeer(FileReadDescriptor &from, int32 fileVersion = 0) {
	quint64 peerId = 0, photoId = 0;
	from.stream >> peerId >> photoId;

	auto photoLoc = Serialize::readStorageImageLocation(from.stream);

	PeerData *result = App::peerLoaded(peerId);
	bool wasLoaded = (result != nullptr);
	if (!wasLoaded) {
		result = App::peer(peerId);
		result->loadedStatus = PeerData::FullLoaded;
	}
	if (const auto user = result->asUser()) {
		QString first, last, phone, username, inlinePlaceholder;
		quint64 access;
		qint32 flags = 0, onlineTill, contact, botInfoVersion;
		from.stream >> first >> last >> phone >> username >> access;
		if (from.version >= 9012) {
			from.stream >> flags;
		}
		if (from.version >= 9016 || fileVersion >= 9016) {
			from.stream >> inlinePlaceholder;
		}
		from.stream >> onlineTill >> contact >> botInfoVersion;

		const auto showPhone = !isServiceUser(user->id)
			&& (user->id != Auth().userPeerId())
			&& (contact <= 0);
		const auto pname = (showPhone && !phone.isEmpty())
			? App::formatPhone(phone)
			: QString();

		if (!wasLoaded) {
			user->setPhone(phone);
			user->setName(first, last, pname, username);

			user->setFlags(MTPDuser::Flags::from_raw(flags));
			user->setAccessHash(access);
			user->onlineTill = onlineTill;
			user->setContactStatus((contact > 0)
				? UserData::ContactStatus::Contact
				: (contact == 0)
				? UserData::ContactStatus::CanAdd
				: UserData::ContactStatus::PhoneUnknown);
			user->setBotInfoVersion(botInfoVersion);
			if (!inlinePlaceholder.isEmpty() && user->botInfo) {
				user->botInfo->inlinePlaceholder = inlinePlaceholder;
			}

			if (user->id == Auth().userPeerId()) {
				user->input = MTP_inputPeerSelf();
				user->inputUser = MTP_inputUserSelf();
			} else {
				user->input = MTP_inputPeerUser(MTP_int(peerToUser(user->id)), MTP_long(user->accessHash()));
				user->inputUser = MTP_inputUser(MTP_int(peerToUser(user->id)), MTP_long(user->accessHash()));
			}
		}
	} else if (const auto chat = result->asChat()) {
		QString name, inviteLink;
		qint32 count, date, version, creator, oldForbidden;
		quint32 flagsData, flags;
		from.stream >> name >> count >> date >> version >> creator >> oldForbidden >> flagsData >> inviteLink;

		if (from.version >= 9012) {
			flags = flagsData;
		} else {
			// flagsData was haveLeft
			flags = (flagsData == 1)
				? MTPDchat::Flags(MTPDchat::Flag::f_left)
				: MTPDchat::Flags(0);
		}
		if (oldForbidden) {
			flags |= quint32(MTPDchat_ClientFlag::f_forbidden);
		}
		if (!wasLoaded) {
			chat->setName(name);
			chat->count = count;
			chat->date = date;
			chat->version = version;
			chat->creator = creator;
			chat->setFlags(MTPDchat::Flags::from_raw(flags));
			chat->setInviteLink(inviteLink);

			chat->input = MTP_inputPeerChat(MTP_int(peerToChat(chat->id)));
			chat->inputChat = MTP_int(peerToChat(chat->id));
		}
	} else if (const auto channel = result->asChannel()) {
		QString name, inviteLink;
		quint64 access;
		qint32 date, version, oldForbidden;
		quint32 flags;
		from.stream >> name >> access >> date >> version >> oldForbidden >> flags >> inviteLink;
		if (oldForbidden) {
			flags |= quint32(MTPDchannel_ClientFlag::f_forbidden);
		}
		if (!wasLoaded) {
			channel->setName(name, QString());
			channel->access = access;
			channel->date = date;
			channel->version = version;
			channel->setFlags(MTPDchannel::Flags::from_raw(flags));
			channel->setInviteLink(inviteLink);

			channel->input = MTP_inputPeerChannel(MTP_int(peerToChannel(channel->id)), MTP_long(access));
			channel->inputChannel = MTP_inputChannel(MTP_int(peerToChannel(channel->id)), MTP_long(access));
		}
	}
	if (!wasLoaded) {
		result->setUserpic(
			photoId,
			photoLoc,
			photoLoc.isNull() ? ImagePtr() : ImagePtr(photoLoc));
	}
	return result;
}

void writeRecentHashtagsAndBots() {
	if (!_working()) return;

	const RecentHashtagPack &write(cRecentWriteHashtags()), &search(cRecentSearchHashtags());
	const RecentInlineBots &bots(cRecentInlineBots());
	if (write.isEmpty() && search.isEmpty() && bots.isEmpty()) readRecentHashtagsAndBots();
	if (write.isEmpty() && search.isEmpty() && bots.isEmpty()) {
		if (_recentHashtagsAndBotsKey) {
			clearKey(_recentHashtagsAndBotsKey);
			_recentHashtagsAndBotsKey = 0;
			_mapChanged = true;
		}
		_writeMap();
	} else {
		if (!_recentHashtagsAndBotsKey) {
			_recentHashtagsAndBotsKey = genKey();
			_mapChanged = true;
			_writeMap(WriteMapWhen::Fast);
		}
		quint32 size = sizeof(quint32) * 3, writeCnt = 0, searchCnt = 0, botsCnt = cRecentInlineBots().size();
		for (RecentHashtagPack::const_iterator i = write.cbegin(), e = write.cend(); i != e;  ++i) {
			if (!i->first.isEmpty()) {
				size += Serialize::stringSize(i->first) + sizeof(quint16);
				++writeCnt;
			}
		}
		for (RecentHashtagPack::const_iterator i = search.cbegin(), e = search.cend(); i != e; ++i) {
			if (!i->first.isEmpty()) {
				size += Serialize::stringSize(i->first) + sizeof(quint16);
				++searchCnt;
			}
		}
		for (RecentInlineBots::const_iterator i = bots.cbegin(), e = bots.cend(); i != e; ++i) {
			size += _peerSize(*i);
		}

		EncryptedDescriptor data(size);
		data.stream << quint32(writeCnt) << quint32(searchCnt);
		for (RecentHashtagPack::const_iterator i = write.cbegin(), e = write.cend(); i != e; ++i) {
			if (!i->first.isEmpty()) data.stream << i->first << quint16(i->second);
		}
		for (RecentHashtagPack::const_iterator i = search.cbegin(), e = search.cend(); i != e; ++i) {
			if (!i->first.isEmpty()) data.stream << i->first << quint16(i->second);
		}
		data.stream << quint32(botsCnt);
		for (RecentInlineBots::const_iterator i = bots.cbegin(), e = bots.cend(); i != e; ++i) {
			_writePeer(data.stream, *i);
		}
		FileWriteDescriptor file(_recentHashtagsAndBotsKey);
		file.writeEncrypted(data);
	}
}

void readRecentHashtagsAndBots() {
	if (_recentHashtagsAndBotsWereRead) return;
	_recentHashtagsAndBotsWereRead = true;

	if (!_recentHashtagsAndBotsKey) return;

	FileReadDescriptor hashtags;
	if (!readEncryptedFile(hashtags, _recentHashtagsAndBotsKey)) {
		clearKey(_recentHashtagsAndBotsKey);
		_recentHashtagsAndBotsKey = 0;
		_writeMap();
		return;
	}

	quint32 writeCount = 0, searchCount = 0, botsCount = 0;
	hashtags.stream >> writeCount >> searchCount;

	QString tag;
	quint16 count;

	RecentHashtagPack write, search;
	RecentInlineBots bots;
	if (writeCount) {
		write.reserve(writeCount);
		for (uint32 i = 0; i < writeCount; ++i) {
			hashtags.stream >> tag >> count;
			write.push_back(qMakePair(tag.trimmed(), count));
		}
	}
	if (searchCount) {
		search.reserve(searchCount);
		for (uint32 i = 0; i < searchCount; ++i) {
			hashtags.stream >> tag >> count;
			search.push_back(qMakePair(tag.trimmed(), count));
		}
	}
	cSetRecentWriteHashtags(write);
	cSetRecentSearchHashtags(search);

	if (!hashtags.stream.atEnd()) {
		hashtags.stream >> botsCount;
		if (botsCount) {
			bots.reserve(botsCount);
			for (uint32 i = 0; i < botsCount; ++i) {
				PeerData *peer = _readPeer(hashtags, 9016);
				if (peer && peer->isUser() && peer->asUser()->botInfo && !peer->asUser()->botInfo->inlinePlaceholder.isEmpty() && !peer->asUser()->username.isEmpty()) {
					bots.push_back(peer->asUser());
				}
			}
		}
		cSetRecentInlineBots(bots);
	}
}

void incrementRecentHashtag(RecentHashtagPack &recent, const QString &tag) {
	auto i = recent.begin(), e = recent.end();
	for (; i != e; ++i) {
		if (i->first == tag) {
			++i->second;
			if (qAbs(i->second) > 0x4000) {
				for (auto j = recent.begin(); j != e; ++j) {
					if (j->second > 1) {
						j->second /= 2;
					} else if (j->second > 0) {
						j->second = 1;
					}
				}
			}
			for (; i != recent.begin(); --i) {
				if (qAbs((i - 1)->second) > qAbs(i->second)) {
					break;
				}
				qSwap(*i, *(i - 1));
			}
			break;
		}
	}
	if (i == e) {
		while (recent.size() >= 64) recent.pop_back();
		recent.push_back(qMakePair(tag, 1));
		for (i = recent.end() - 1; i != recent.begin(); --i) {
			if ((i - 1)->second > i->second) {
				break;
			}
			qSwap(*i, *(i - 1));
		}
	}
}

base::optional<RecentHashtagPack> saveRecentHashtags(
		Fn<RecentHashtagPack()> getPack,
		const QString &text) {
	auto found = false;
	auto m = QRegularExpressionMatch();
	auto recent = getPack();
	for (auto i = 0, next = 0; (m = TextUtilities::RegExpHashtag().match(text, i)).hasMatch(); i = next) {
		i = m.capturedStart();
		next = m.capturedEnd();
		if (m.hasMatch()) {
			if (!m.capturedRef(1).isEmpty()) {
				++i;
			}
			if (!m.capturedRef(2).isEmpty()) {
				--next;
			}
		}
		const auto tag = text.mid(i + 1, next - i - 1);
		if (TextUtilities::RegExpHashtagExclude().match(tag).hasMatch()) {
			continue;
		}
		if (!found
			&& cRecentWriteHashtags().isEmpty()
			&& cRecentSearchHashtags().isEmpty()) {
			Local::readRecentHashtagsAndBots();
			recent = getPack();
		}
		found = true;
		incrementRecentHashtag(recent, tag);
	}
	return found ? base::make_optional(recent) : base::none;
}

void saveRecentSentHashtags(const QString &text) {
	const auto result = saveRecentHashtags(
		[] { return cRecentWriteHashtags(); },
		text);
	if (result) {
		cSetRecentWriteHashtags(*result);
		Local::writeRecentHashtagsAndBots();
	}
}

void saveRecentSearchHashtags(const QString &text) {
	const auto result = saveRecentHashtags(
		[] { return cRecentSearchHashtags(); },
		text);
	if (result) {
		cSetRecentSearchHashtags(*result);
		Local::writeRecentHashtagsAndBots();
	}
}

void WriteExportSettings(const Export::Settings &settings) {
	if (!_working()) return;

	const auto check = Export::Settings();
	if (settings.types == check.types
		&& settings.fullChats == check.fullChats
		&& settings.media.types == check.media.types
		&& settings.media.sizeLimit == check.media.sizeLimit
		&& settings.path == check.path
		&& settings.format == check.format
		&& settings.availableAt == check.availableAt) {
		if (_exportSettingsKey) {
			clearKey(_exportSettingsKey);
			_exportSettingsKey = 0;
			_mapChanged = true;
		}
		_writeMap();
	} else {
		if (!_exportSettingsKey) {
			_exportSettingsKey = genKey();
			_mapChanged = true;
			_writeMap(WriteMapWhen::Fast);
		}
		quint32 size = sizeof(quint32) * 6
			+ Serialize::stringSize(settings.path);
		EncryptedDescriptor data(size);
		data.stream
			<< quint32(settings.types)
			<< quint32(settings.fullChats)
			<< quint32(settings.media.types)
			<< quint32(settings.media.sizeLimit)
			<< quint32(settings.format)
			<< settings.path
			<< quint32(settings.availableAt);

		FileWriteDescriptor file(_exportSettingsKey);
		file.writeEncrypted(data);
	}
}

Export::Settings ReadExportSettings() {
	FileReadDescriptor file;
	if (!readEncryptedFile(file, _exportSettingsKey)) {
		clearKey(_exportSettingsKey);
		_exportSettingsKey = 0;
		_writeMap();
		return Export::Settings();
	}

	quint32 types = 0, fullChats = 0;
	quint32 mediaTypes = 0, mediaSizeLimit = 0;
	quint32 format = 0, availableAt = 0;
	QString path;
	file.stream
		>> types
		>> fullChats
		>> mediaTypes
		>> mediaSizeLimit
		>> format
		>> path
		>> availableAt;
	auto result = Export::Settings();
	result.types = Export::Settings::Types::from_raw(types);
	result.fullChats = Export::Settings::Types::from_raw(fullChats);
	result.media.types = Export::MediaSettings::Types::from_raw(mediaTypes);
	result.media.sizeLimit = mediaSizeLimit;
	result.format = Export::Output::Format(format);
	result.path = path;
	result.availableAt = availableAt;
	return (file.stream.status() == QDataStream::Ok && result.validate())
		? result
		: Export::Settings();
}

void writeSavedPeers() {
	if (!_working()) return;

	const SavedPeers &saved(cSavedPeers());
	if (saved.isEmpty()) {
		if (_savedPeersKey) {
			clearKey(_savedPeersKey);
			_savedPeersKey = 0;
			_mapChanged = true;
		}
		_writeMap();
	} else {
		if (!_savedPeersKey) {
			_savedPeersKey = genKey();
			_mapChanged = true;
			_writeMap(WriteMapWhen::Fast);
		}
		quint32 size = sizeof(quint32);
		for (SavedPeers::const_iterator i = saved.cbegin(); i != saved.cend(); ++i) {
			size += _peerSize(i.key()) + Serialize::dateTimeSize();
		}

		EncryptedDescriptor data(size);
		data.stream << quint32(saved.size());
		for (SavedPeers::const_iterator i = saved.cbegin(); i != saved.cend(); ++i) {
			_writePeer(data.stream, i.key());
			data.stream << i.value();
		}

		FileWriteDescriptor file(_savedPeersKey);
		file.writeEncrypted(data);
	}
}

void readSavedPeers() {
	if (!_savedPeersKey) return;

	FileReadDescriptor saved;
	if (!readEncryptedFile(saved, _savedPeersKey)) {
		clearKey(_savedPeersKey);
		_savedPeersKey = 0;
		_writeMap();
		return;
	}
	if (saved.version == 9011) { // broken dev version
		clearKey(_savedPeersKey);
		_savedPeersKey = 0;
		_writeMap();
		return;
	}

	quint32 count = 0;
	saved.stream >> count;
	cRefSavedPeers().clear();
	cRefSavedPeersByTime().clear();
	QList<PeerData*> peers;
	peers.reserve(count);
	for (uint32 i = 0; i < count; ++i) {
		PeerData *peer = _readPeer(saved);
		if (!peer) break;

		QDateTime t;
		saved.stream >> t;

		cRefSavedPeers().insert(peer, t);
		cRefSavedPeersByTime().insert(t, peer);
		peers.push_back(peer);
	}

	Auth().api().requestPeers(peers);
}

void addSavedPeer(PeerData *peer, const QDateTime &position) {
	auto &savedPeers = cRefSavedPeers();
	auto i = savedPeers.find(peer);
	if (i == savedPeers.cend()) {
		savedPeers.insert(peer, position);
	} else if (i.value() != position) {
		cRefSavedPeersByTime().remove(i.value(), peer);
		i.value() = position;
		cRefSavedPeersByTime().insert(i.value(), peer);
	}
	writeSavedPeers();
}

void removeSavedPeer(PeerData *peer) {
	auto &savedPeers = cRefSavedPeers();
	if (savedPeers.isEmpty()) return;

	auto i = savedPeers.find(peer);
	if (i != savedPeers.cend()) {
		cRefSavedPeersByTime().remove(i.value(), peer);
		savedPeers.erase(i);

		writeSavedPeers();
	}
}

void writeReportSpamStatuses() {
	_writeReportSpamStatuses();
}

void writeTrustedBots() {
	if (!_working()) return;

	if (_trustedBots.isEmpty()) {
		if (_trustedBotsKey) {
			clearKey(_trustedBotsKey);
			_trustedBotsKey = 0;
			_mapChanged = true;
			_writeMap();
		}
	} else {
		if (!_trustedBotsKey) {
			_trustedBotsKey = genKey();
			_mapChanged = true;
			_writeMap(WriteMapWhen::Fast);
		}
		quint32 size = sizeof(qint32) + _trustedBots.size() * sizeof(quint64);
		EncryptedDescriptor data(size);
		data.stream << qint32(_trustedBots.size());
		for_const (auto botId, _trustedBots) {
			data.stream << quint64(botId);
		}

		FileWriteDescriptor file(_trustedBotsKey);
		file.writeEncrypted(data);
	}
}

void readTrustedBots() {
	if (!_trustedBotsKey) return;

	FileReadDescriptor trusted;
	if (!readEncryptedFile(trusted, _trustedBotsKey)) {
		clearKey(_trustedBotsKey);
		_trustedBotsKey = 0;
		_writeMap();
		return;
	}

	qint32 size = 0;
	trusted.stream >> size;
	for (int i = 0; i < size; ++i) {
		quint64 botId = 0;
		trusted.stream >> botId;
		_trustedBots.insert(botId);
	}
}

void makeBotTrusted(UserData *bot) {
	if (!isBotTrusted(bot)) {
		_trustedBots.insert(bot->id);
		writeTrustedBots();
	}
}

bool isBotTrusted(UserData *bot) {
	if (!_trustedBotsRead) {
		readTrustedBots();
		_trustedBotsRead = true;
	}
	return _trustedBots.contains(bot->id);
}

bool encrypt(const void *src, void *dst, uint32 len, const void *key128) {
	if (!LocalKey) {
		return false;
	}
	MTP::aesEncryptLocal(src, dst, len, LocalKey, key128);
	return true;
}

bool decrypt(const void *src, void *dst, uint32 len, const void *key128) {
	if (!LocalKey) {
		return false;
	}
	MTP::aesDecryptLocal(src, dst, len, LocalKey, key128);
	return true;
}

struct ClearManagerData {
	QThread *thread;
	StorageMap images, stickers, audios;
	WebFilesMap webFiles;
	QMutex mutex;
	QList<int> tasks;
	bool working;
};

ClearManager::ClearManager() : data(new ClearManagerData()) {
	data->thread = new QThread();
	data->working = true;
}

bool ClearManager::addTask(int task) {
	QMutexLocker lock(&data->mutex);
	if (!data->working) return false;

	if (!data->tasks.isEmpty() && (data->tasks.at(0) == ClearManagerAll)) return true;
	if (task == ClearManagerAll) {
		data->tasks.clear();
		if (!_imagesMap.isEmpty()) {
			_imagesMap.clear();
			_storageImagesSize = 0;
			_mapChanged = true;
		}
		if (!_stickerImagesMap.isEmpty()) {
			_stickerImagesMap.clear();
			_storageStickersSize = 0;
			_mapChanged = true;
		}
		if (!_audiosMap.isEmpty()) {
			_audiosMap.clear();
			_storageAudiosSize = 0;
			_mapChanged = true;
		}
		if (!_draftsMap.isEmpty()) {
			_draftsMap.clear();
			_mapChanged = true;
		}
		if (!_draftCursorsMap.isEmpty()) {
			_draftCursorsMap.clear();
			_mapChanged = true;
		}
		if (_locationsKey) {
			_locationsKey = 0;
			_mapChanged = true;
		}
		if (_reportSpamStatusesKey) {
			_reportSpamStatusesKey = 0;
			_mapChanged = true;
		}
		if (_trustedBotsKey) {
			_trustedBotsKey = 0;
			_mapChanged = true;
		}
		if (_recentStickersKeyOld) {
			_recentStickersKeyOld = 0;
			_mapChanged = true;
		}
		if (_installedStickersKey || _featuredStickersKey || _recentStickersKey || _archivedStickersKey) {
			_installedStickersKey = _featuredStickersKey = _recentStickersKey = _archivedStickersKey = 0;
			_mapChanged = true;
		}
		if (_recentHashtagsAndBotsKey) {
			_recentHashtagsAndBotsKey = 0;
			_mapChanged = true;
		}
		if (_savedPeersKey) {
			_savedPeersKey = 0;
			_mapChanged = true;
		}
		_writeMap();
	} else {
		if (task & ClearManagerStorage) {
			if (data->images.isEmpty()) {
				data->images = _imagesMap;
			} else {
				for (StorageMap::const_iterator i = _imagesMap.cbegin(), e = _imagesMap.cend(); i != e; ++i) {
					StorageKey k = i.key();
					while (data->images.constFind(k) != data->images.cend()) {
						++k.second;
					}
					data->images.insert(k, i.value());
				}
			}
			if (!_imagesMap.isEmpty()) {
				_imagesMap.clear();
				_storageImagesSize = 0;
				_mapChanged = true;
			}
			if (data->stickers.isEmpty()) {
				data->stickers = _stickerImagesMap;
			} else {
				for (StorageMap::const_iterator i = _stickerImagesMap.cbegin(), e = _stickerImagesMap.cend(); i != e; ++i) {
					StorageKey k = i.key();
					while (data->stickers.constFind(k) != data->stickers.cend()) {
						++k.second;
					}
					data->stickers.insert(k, i.value());
				}
			}
			if (!_stickerImagesMap.isEmpty()) {
				_stickerImagesMap.clear();
				_storageStickersSize = 0;
				_mapChanged = true;
			}
			if (data->webFiles.isEmpty()) {
				data->webFiles = _webFilesMap;
			} else {
				for (WebFilesMap::const_iterator i = _webFilesMap.cbegin(), e = _webFilesMap.cend(); i != e; ++i) {
					QString k = i.key();
					while (data->webFiles.constFind(k) != data->webFiles.cend()) {
						k += '#';
					}
					data->webFiles.insert(k, i.value());
				}
			}
			if (!_webFilesMap.isEmpty()) {
				_webFilesMap.clear();
				_storageWebFilesSize = 0;
				_writeLocations();
			}
			if (data->audios.isEmpty()) {
				data->audios = _audiosMap;
			} else {
				for (StorageMap::const_iterator i = _audiosMap.cbegin(), e = _audiosMap.cend(); i != e; ++i) {
					StorageKey k = i.key();
					while (data->audios.constFind(k) != data->audios.cend()) {
						++k.second;
					}
					data->audios.insert(k, i.value());
				}
			}
			if (!_audiosMap.isEmpty()) {
				_audiosMap.clear();
				_storageAudiosSize = 0;
				_mapChanged = true;
			}
			_writeMap();
		}
		for (int32 i = 0, l = data->tasks.size(); i < l; ++i) {
			if (data->tasks.at(i) == task) return true;
		}
	}
	data->tasks.push_back(task);
	return true;
}

bool ClearManager::hasTask(ClearManagerTask task) {
	QMutexLocker lock(&data->mutex);
	if (data->tasks.isEmpty()) return false;
	if (data->tasks.at(0) == ClearManagerAll) return true;
	for (int32 i = 0, l = data->tasks.size(); i < l; ++i) {
		if (data->tasks.at(i) == task) return true;
	}
	return false;
}

void ClearManager::start() {
	moveToThread(data->thread);
	connect(data->thread, SIGNAL(started()), this, SLOT(onStart()));
	connect(data->thread, SIGNAL(finished()), data->thread, SLOT(deleteLater()));
	connect(data->thread, SIGNAL(finished()), this, SLOT(deleteLater()));
	data->thread->start();
}

void ClearManager::stop() {
	{
		QMutexLocker lock(&data->mutex);
		data->tasks.clear();
	}
	auto thread = data->thread;
	thread->quit();
	thread->wait();
}

ClearManager::~ClearManager() {
	delete data;
}

void ClearManager::onStart() {
	while (true) {
		int task = 0;
		bool result = false;
		StorageMap images, stickers, audios;
		WebFilesMap webFiles;
		{
			QMutexLocker lock(&data->mutex);
			if (data->tasks.isEmpty()) {
				data->working = false;
				break;
			}
			task = data->tasks.at(0);
			images = data->images;
			stickers = data->stickers;
			audios = data->audios;
			webFiles = data->webFiles;
		}
		switch (task) {
		case ClearManagerAll: {
			result = QDir(cTempDir()).removeRecursively();
			QDirIterator di(_userBasePath, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
			while (di.hasNext()) {
				di.next();
				const QFileInfo& fi = di.fileInfo();
				if (fi.isDir() && !fi.isSymLink()) {
					if (!QDir(di.filePath()).removeRecursively()) result = false;
				} else {
					QString path = di.filePath();
					if (!path.endsWith(qstr("map0")) && !path.endsWith(qstr("map1"))) {
						if (!QFile::remove(di.filePath())) result = false;
					}
				}
			}
		} break;
		case ClearManagerDownloads:
			result = QDir(cTempDir()).removeRecursively();
		break;
		case ClearManagerStorage:
			for (StorageMap::const_iterator i = images.cbegin(), e = images.cend(); i != e; ++i) {
				clearKey(i.value().first, FileOption::User);
			}
			for (StorageMap::const_iterator i = stickers.cbegin(), e = stickers.cend(); i != e; ++i) {
				clearKey(i.value().first, FileOption::User);
			}
			for (StorageMap::const_iterator i = audios.cbegin(), e = audios.cend(); i != e; ++i) {
				clearKey(i.value().first, FileOption::User);
			}
			for (WebFilesMap::const_iterator i = webFiles.cbegin(), e = webFiles.cend(); i != e; ++i) {
				clearKey(i.value().first, FileOption::User);
			}
			result = true;
		break;
		}
		{
			QMutexLocker lock(&data->mutex);
			if (!data->tasks.isEmpty() && data->tasks.at(0) == task) {
				data->tasks.pop_front();
			}
			if (data->tasks.isEmpty()) {
				data->working = false;
			}
			if (result) {
				emit succeed(task, data->working ? 0 : this);
			} else {
				emit failed(task, data->working ? 0 : this);
			}
			if (!data->working) break;
		}
	}
}

namespace internal {

Manager::Manager() {
	_mapWriteTimer.setSingleShot(true);
	connect(&_mapWriteTimer, SIGNAL(timeout()), this, SLOT(mapWriteTimeout()));
	_locationsWriteTimer.setSingleShot(true);
	connect(&_locationsWriteTimer, SIGNAL(timeout()), this, SLOT(locationsWriteTimeout()));
}

void Manager::writeMap(bool fast) {
	if (!_mapWriteTimer.isActive() || fast) {
		_mapWriteTimer.start(fast ? 1 : WriteMapTimeout);
	} else if (_mapWriteTimer.remainingTime() <= 0) {
		mapWriteTimeout();
	}
}

void Manager::writingMap() {
	_mapWriteTimer.stop();
}

void Manager::writeLocations(bool fast) {
	if (!_locationsWriteTimer.isActive() || fast) {
		_locationsWriteTimer.start(fast ? 1 : WriteMapTimeout);
	} else if (_locationsWriteTimer.remainingTime() <= 0) {
		locationsWriteTimeout();
	}
}

void Manager::writingLocations() {
	_locationsWriteTimer.stop();
}

void Manager::mapWriteTimeout() {
	_writeMap(WriteMapWhen::Now);
}

void Manager::locationsWriteTimeout() {
	_writeLocations(WriteMapWhen::Now);
}

void Manager::finish() {
	if (_mapWriteTimer.isActive()) {
		mapWriteTimeout();
	}
	if (_locationsWriteTimer.isActive()) {
		locationsWriteTimeout();
	}
}

} // namespace internal
} // namespace Local
