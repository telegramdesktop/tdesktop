/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/localstorage.h"

#include "storage/serialize_document.h"
#include "storage/serialize_common.h"
#include "storage/storage_encrypted_file.h"
#include "storage/storage_clear_legacy.h"
#include "chat_helpers/stickers.h"
#include "data/data_drafts.h"
#include "data/data_user.h"
#include "boxes/send_files_box.h"
#include "ui/widgets/input_fields.h"
#include "ui/emoji_config.h"
#include "export/export_settings.h"
#include "api/api_hash.h"
#include "core/crash_reports.h"
#include "core/update_checker.h"
#include "observer_peer.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "lang/lang_keys.h"
#include "lang/lang_cloud_manager.h"
#include "main/main_account.h"
#include "media/audio/media_audio.h"
#include "mtproto/dc_options.h"
#include "core/application.h"
#include "apiwrap.h"
#include "main/main_session.h"
#include "window/themes/window_theme.h"
#include "window/window_session_controller.h"
#include "window/themes/window_theme_editor.h"
#include "base/flags.h"
#include "data/data_session.h"
#include "history/history.h"

extern "C" {
#include <openssl/evp.h>
} // extern "C"

namespace Local {
namespace {

constexpr auto kThemeFileSizeLimit = 5 * 1024 * 1024;
constexpr auto kFileLoaderQueueStopTimeout = crl::time(5000);
constexpr auto kDefaultStickerInstallDate = TimeId(1);
constexpr auto kProxyTypeShift = 1024;
constexpr auto kWriteMapTimeout = crl::time(1000);
constexpr auto kSavedBackgroundFormat = QImage::Format_ARGB32_Premultiplied;

constexpr auto kWallPaperLegacySerializeTagId = int32(-111);
constexpr auto kWallPaperSerializeTagId = int32(-112);
constexpr auto kWallPaperSidesLimit = 10'000;

constexpr auto kSinglePeerTypeUser = qint32(1);
constexpr auto kSinglePeerTypeChat = qint32(2);
constexpr auto kSinglePeerTypeChannel = qint32(3);
constexpr auto kSinglePeerTypeSelf = qint32(4);
constexpr auto kSinglePeerTypeEmpty = qint32(0);

constexpr auto kStickersVersionTag = quint32(-1);
constexpr auto kStickersSerializeVersion = 1;
constexpr auto kMaxSavedStickerSetsCount = 1000;

using Database = Storage::Cache::Database;
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

QString _basePath, _userBasePath, _userDbPath;

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
			stream.setDevice(nullptr);
			if (buffer.isOpen()) buffer.close();
			buffer.setBuffer(nullptr);
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
		if (stream.device()) stream.setDevice(nullptr);
		if (buffer.isOpen()) buffer.close();
		buffer.setBuffer(nullptr);
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

		stream.setDevice(nullptr);

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
		result.stream.setDevice(nullptr);
		if (result.buffer.isOpen()) result.buffer.close();
		result.buffer.setBuffer(nullptr);
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
	lskLegacyImages = 0x03, // legacy
	lskLocations = 0x04, // no data
	lskLegacyStickerImages = 0x05, // legacy
	lskLegacyAudios = 0x06, // legacy
	lskRecentStickersOld = 0x07, // no data
	lskBackgroundOld = 0x08, // no data
	lskUserSettings = 0x09, // no data
	lskRecentHashtagsAndBots = 0x0a, // no data
	lskStickersOld = 0x0b, // no data
	lskSavedPeersOld = 0x0c, // no data
	lskReportSpamStatusesOld = 0x0d, // no data
	lskSavedGifsOld = 0x0e, // no data
	lskSavedGifs = 0x0f, // no data
	lskStickersKeys = 0x10, // no data
	lskTrustedBots = 0x11, // no data
	lskFavedStickers = 0x12, // no data
	lskExportSettings = 0x13, // no data
	lskBackground = 0x14, // no data
	lskSelfSerialized = 0x15, // serialized self
};

enum {
	dbiKey = 0x00,
	dbiUser = 0x01,
	dbiDcOptionOldOld = 0x02,
	dbiChatSizeMax = 0x03,
	dbiMutePeer = 0x04,
	dbiSendKeyOld = 0x05,
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
	dbiReplaceEmojiOld = 0x13,
	dbiAskDownloadPath = 0x14,
	dbiDownloadPathOld = 0x15,
	dbiScaleOld = 0x16,
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
	dbiTileBackgroundOld = 0x21,
	dbiAutoLock = 0x22,
	dbiDialogLastPath = 0x23,
	dbiRecentEmojiOld = 0x24,
	dbiEmojiVariantsOld = 0x25,
	dbiRecentStickers = 0x26,
	dbiDcOptionOld = 0x27,
	dbiTryIPv6 = 0x28,
	dbiSongVolume = 0x29,
	dbiWindowsNotificationsOld = 0x30,
	dbiIncludeMutedOld = 0x31,
	dbiMegagroupSizeMax = 0x32,
	dbiDownloadPath = 0x33,
	dbiAutoDownloadOld = 0x34,
	dbiSavedGifsLimit = 0x35,
	dbiShowingSavedGifsOld = 0x36,
	dbiAutoPlayOld = 0x37,
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
	dbiThemeKeyOld = 0x47,
	dbiDialogsWidthRatioOld = 0x48,
	dbiUseExternalVideoPlayer = 0x49,
	dbiDcOptions = 0x4a,
	dbiMtpAuthorization = 0x4b,
	dbiLastSeenWarningSeenOld = 0x4c,
	dbiSessionSettings = 0x4d,
	dbiLangPackKey = 0x4e,
	dbiConnectionType = 0x4f,
	dbiStickersFavedLimit = 0x50,
	dbiSuggestStickersByEmojiOld = 0x51,
	dbiSuggestEmojiOld = 0x52,
	dbiTxtDomainStringOld = 0x53,
	dbiThemeKey = 0x54,
	dbiTileBackground = 0x55,
	dbiCacheSettingsOld = 0x56,
	dbiAnimationsDisabled = 0x57,
	dbiScalePercent = 0x58,
	dbiPlaybackSpeed = 0x59,
	dbiLanguagesKey = 0x5a,
	dbiCallSettings = 0x5b,
	dbiCacheSettings = 0x5c,
	dbiTxtDomainString = 0x5d,
	dbiApplicationSettings = 0x5e,

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
	dbictProxiesListOld = 4,
	dbictProxiesList = 5,
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
FileKey _locationsKey = 0, _trustedBotsKey = 0;

using TrustedBots = OrderedSet<uint64>;
TrustedBots _trustedBots;
bool _trustedBotsRead = false;

FileKey _recentStickersKeyOld = 0;
FileKey _installedStickersKey = 0, _featuredStickersKey = 0, _recentStickersKey = 0, _favedStickersKey = 0, _archivedStickersKey = 0;
FileKey _savedGifsKey = 0;

FileKey _backgroundKeyDay = 0;
FileKey _backgroundKeyNight = 0;
bool _backgroundCanWrite = true;

FileKey _themeKeyDay = 0;
FileKey _themeKeyNight = 0;

// Theme key legacy may be read in start() with settings.
// But it should be moved to keyDay or keyNight inside loadTheme()
// and never used after.
FileKey _themeKeyLegacy = 0;

bool _readingUserSettings = false;
FileKey _userSettingsKey = 0;
FileKey _recentHashtagsAndBotsKey = 0;
bool _recentHashtagsAndBotsWereRead = false;
qint64 _cacheTotalSizeLimit = Database::Settings().totalSizeLimit;
qint32 _cacheTotalTimeLimit = Database::Settings().totalTimeLimit;
qint64 _cacheBigFileTotalSizeLimit = Database::Settings().totalSizeLimit;
qint32 _cacheBigFileTotalTimeLimit = Database::Settings().totalTimeLimit;

bool NoTimeLimit(qint32 storedLimitValue) {
	// This is a workaround for a bug in storing the cache time limit.
	// See https://github.com/telegramdesktop/tdesktop/issues/5611
	return !storedLimitValue
		|| (storedLimitValue == qint32(std::numeric_limits<int32>::max()))
		|| (storedLimitValue == qint32(std::numeric_limits<int64>::max()));
}

FileKey _exportSettingsKey = 0;

FileKey _langPackKey = 0;
FileKey _languagesKey = 0;

bool _mapChanged = false;
int32 _oldMapVersion = 0, _oldSettingsVersion = 0;

enum class WriteMapWhen {
	Now,
	Fast,
	Soon,
};

std::unique_ptr<Main::Settings> StoredSessionSettings;
Main::Settings &GetStoredSessionSettings() {
	if (!StoredSessionSettings) {
		StoredSessionSettings = std::make_unique<Main::Settings>();
	}
	return *StoredSessionSettings;
}

void _writeMap(WriteMapWhen when = WriteMapWhen::Soon);

void _writeLocations(WriteMapWhen when = WriteMapWhen::Soon) {
	Expects(_manager != nullptr);

	if (when != WriteMapWhen::Now) {
		_manager->writeLocations(when == WriteMapWhen::Fast);
		return;
	}
	if (!_working()) return;

	_manager->writingLocations();
	if (_fileLocations.isEmpty()) {
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
		if (!loc.inMediaCache()) {
			_fileLocationPairs.insert(loc.fname, FileLocationPair(key, loc));
		}
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
			quint32 webLocationsCount;
			locations.stream >> webLocationsCount;
			for (quint32 i = 0; i < webLocationsCount; ++i) {
				QString url;
				quint64 key;
				qint32 size;
				locations.stream >> url >> key >> size;
				clearKey(key, FileOption::User);
			}
		}
	}
}

struct ReadSettingsContext {
	MTP::DcOptions dcOptions;
};

void applyReadContext(ReadSettingsContext &&context) {
	Core::App().dcOptions()->addFromOther(std::move(context.dcOptions));
}

QByteArray serializeCallSettings(){
	QByteArray result=QByteArray();
	uint32 size = 3*sizeof(qint32) + Serialize::stringSize(Global::CallOutputDeviceID()) + Serialize::stringSize(Global::CallInputDeviceID());
	result.reserve(size);
	QDataStream stream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << Global::CallOutputDeviceID();
	stream << qint32(Global::CallOutputVolume());
	stream << Global::CallInputDeviceID();
	stream << qint32(Global::CallInputVolume());
	stream << qint32(Global::CallAudioDuckingEnabled() ? 1 : 0);
	return result;
}

void deserializeCallSettings(QByteArray& settings){
	QDataStream stream(&settings, QIODevice::ReadOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	QString outputDeviceID;
	QString inputDeviceID;
	qint32 outputVolume;
	qint32 inputVolume;
	qint32 duckingEnabled;

	stream >> outputDeviceID;
	stream >> outputVolume;
	stream >> inputDeviceID;
	stream >> inputVolume;
	stream >> duckingEnabled;
	if(_checkStreamStatus(stream)){
		Global::SetCallOutputDeviceID(outputDeviceID);
		Global::SetCallOutputVolume(outputVolume);
		Global::SetCallInputDeviceID(inputDeviceID);
		Global::SetCallInputVolume(inputVolume);
		Global::SetCallAudioDuckingEnabled(duckingEnabled);
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

	case dbiApplicationSettings: {
		auto serialized = QByteArray();
		stream >> serialized;
		if (!_checkStreamStatus(stream)) return false;

		Core::App().settings().constructFromSerialized(serialized);
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
		Core::App().activeAccount().setMtpMainDcId(dcId);
		Core::App().activeAccount().setSessionUserId(userId);
	} break;

	case dbiKey: {
		qint32 dcId;
		stream >> dcId;
		auto key = Serialize::read<MTP::AuthKey::Data>(stream);
		if (!_checkStreamStatus(stream)) return false;

		Core::App().activeAccount().setMtpKey(dcId, key);
	} break;

	case dbiMtpAuthorization: {
		auto serialized = QByteArray();
		stream >> serialized;
		if (!_checkStreamStatus(stream)) return false;

		Core::App().activeAccount().setMtpAuthorization(serialized);
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

	case dbiCacheSettingsOld: {
		qint64 size;
		qint32 time;
		stream >> size >> time;
		if (!_checkStreamStatus(stream)
			|| size <= Database::Settings().maxDataSize
			|| (!NoTimeLimit(time) && time < 0)) {
			return false;
		}
		_cacheTotalSizeLimit = size;
		_cacheTotalTimeLimit = NoTimeLimit(time) ? 0 : time;
		_cacheBigFileTotalSizeLimit = size;
		_cacheBigFileTotalTimeLimit = NoTimeLimit(time) ? 0 : time;
	} break;

	case dbiCacheSettings: {
		qint64 size, sizeBig;
		qint32 time, timeBig;
		stream >> size >> time >> sizeBig >> timeBig;
		if (!_checkStreamStatus(stream)
			|| size <= Database::Settings().maxDataSize
			|| sizeBig <= Database::Settings().maxDataSize
			|| (!NoTimeLimit(time) && time < 0)
			|| (!NoTimeLimit(timeBig) && timeBig < 0)) {
			return false;
		}

		_cacheTotalSizeLimit = size;
		_cacheTotalTimeLimit = NoTimeLimit(time) ? 0 : time;
		_cacheBigFileTotalSizeLimit = sizeBig;
		_cacheBigFileTotalTimeLimit = NoTimeLimit(timeBig) ? 0 : timeBig;
	} break;

	case dbiAnimationsDisabled: {
		qint32 disabled;
		stream >> disabled;
		if (!_checkStreamStatus(stream)) {
			return false;
		}

		anim::SetDisabled(disabled == 1);
	} break;

	case dbiSoundNotify: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetSoundNotify(v == 1);
	} break;

	case dbiAutoDownloadOld: {
		qint32 photo, audio, gif;
		stream >> photo >> audio >> gif;
		if (!_checkStreamStatus(stream)) return false;

		using namespace Data::AutoDownload;
		auto &settings = GetStoredSessionSettings().autoDownload();
		const auto disabled = [](qint32 value, qint32 mask) {
			return (value & mask) != 0;
		};
		const auto set = [&](Type type, qint32 value) {
			constexpr auto kNoPrivate = qint32(0x01);
			constexpr auto kNoGroups = qint32(0x02);
			if (disabled(value, kNoPrivate)) {
				settings.setBytesLimit(Source::User, type, 0);
			}
			if (disabled(value, kNoGroups)) {
				settings.setBytesLimit(Source::Group, type, 0);
				settings.setBytesLimit(Source::Channel, type, 0);
			}
		};
		set(Type::Photo, photo);
		set(Type::VoiceMessage, audio);
		set(Type::GIF, gif);
		set(Type::VideoMessage, gif);
	} break;

	case dbiAutoPlayOld: {
		qint32 gif;
		stream >> gif;
		if (!_checkStreamStatus(stream)) return false;

		GetStoredSessionSettings().setAutoplayGifs(gif == 1);
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

	case dbiIncludeMutedOld: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		GetStoredSessionSettings().setIncludeMutedCounter(v == 1);
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

		GetStoredSessionSettings().setDialogsWidthRatio(v / 1000000.);
	} break;

	case dbiLastSeenWarningSeenOld: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		GetStoredSessionSettings().setLastSeenWarningSeen(v == 1);
	} break;

	case dbiSessionSettings: {
		QByteArray v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		GetStoredSessionSettings().constructFromSerialized(v);
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

	case dbiTxtDomainStringOld: {
		QString v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;
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
		Global::SetProxySettings(proxy
			? ProxyData::Settings::Enabled
			: ProxyData::Settings::System);
		if (proxy) {
			Global::SetProxiesList({ 1, proxy });
		} else {
			Global::SetProxiesList({});
		}
		Core::App().refreshGlobalProxy();
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
		if (connectionType == dbictProxiesListOld
			|| connectionType == dbictProxiesList) {
			qint32 count = 0, index = 0;
			stream >> count >> index;
			qint32 settings = 0, calls = 0;
			if (connectionType == dbictProxiesList) {
				stream >> settings >> calls;
			} else if (std::abs(index) > count) {
				calls = 1;
				index -= (index > 0 ? count : -count);
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
			if (connectionType == dbictProxiesListOld) {
				settings = static_cast<qint32>(
					(index > 0 && index <= list.size()
						? ProxyData::Settings::Enabled
						: ProxyData::Settings::System));
				index = std::abs(index);
			}
			if (index > 0 && index <= list.size()) {
				Global::SetSelectedProxy(list[index - 1]);
			} else {
				Global::SetSelectedProxy(ProxyData());
			}

			const auto unchecked = static_cast<ProxyData::Settings>(settings);
			switch (unchecked) {
			case ProxyData::Settings::Enabled:
				Global::SetProxySettings(Global::SelectedProxy()
					? ProxyData::Settings::Enabled
					: ProxyData::Settings::System);
				break;
			case ProxyData::Settings::Disabled:
			case ProxyData::Settings::System:
				Global::SetProxySettings(unchecked);
				break;
			default:
				Global::SetProxySettings(ProxyData::Settings::System);
				break;
			}
			Global::SetUseProxyForCalls(calls == 1);
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
					Global::SetProxySettings(ProxyData::Settings::Enabled);
				} else {
					Global::SetProxySettings(ProxyData::Settings::System);
				}
			} else {
				Global::SetProxiesList({});
				Global::SetSelectedProxy(ProxyData());
				Global::SetProxySettings(ProxyData::Settings::System);
			}
		}
		Core::App().refreshGlobalProxy();
	} break;

	case dbiThemeKeyOld: {
		quint64 key = 0;
		stream >> key;
		if (!_checkStreamStatus(stream)) return false;

		_themeKeyLegacy = key;
	} break;

	case dbiThemeKey: {
		quint64 keyDay = 0, keyNight = 0;
		quint32 nightMode = 0;
		stream >> keyDay >> keyNight >> nightMode;
		if (!_checkStreamStatus(stream)) return false;

		_themeKeyDay = keyDay;
		_themeKeyNight = keyNight;
		Window::Theme::SetNightModeValue(nightMode == 1);
	} break;

	case dbiLangPackKey: {
		quint64 langPackKey = 0;
		stream >> langPackKey;
		if (!_checkStreamStatus(stream)) return false;

		_langPackKey = langPackKey;
	} break;

	case dbiLanguagesKey: {
		quint64 languagesKey = 0;
		stream >> languagesKey;
		if (!_checkStreamStatus(stream)) return false;

		_languagesKey = languagesKey;
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

	case dbiScaleOld: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		SetScaleChecked([&] {
			constexpr auto kAuto = 0;
			constexpr auto kOne = 1;
			constexpr auto kOneAndQuarter = 2;
			constexpr auto kOneAndHalf = 3;
			constexpr auto kTwo = 4;
			switch (v) {
			case kAuto: return kInterfaceScaleAuto;
			case kOne: return 100;
			case kOneAndQuarter: return 125;
			case kOneAndHalf: return 150;
			case kTwo: return 200;
			}
			return cConfigScale();
		}());
	} break;

	case dbiScalePercent: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		// If cConfigScale() has value then it was set via command line.
		if (cConfigScale() == kInterfaceScaleAuto) {
			SetScaleChecked(v);
		}
	} break;

	case dbiLangOld: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;
	} break;

	case dbiLangFileOld: {
		QString v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;
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

	case dbiSendKeyOld: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		using SendSettings = Ui::InputSubmitSettings;
		const auto unchecked = static_cast<SendSettings>(v);

		if (unchecked != SendSettings::Enter
			&& unchecked != SendSettings::CtrlEnter) {
			return false;
		}
		GetStoredSessionSettings().setSendSubmitWay(unchecked);
	} break;

	case dbiCatsAndDogs: { // deprecated
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;
	} break;

	case dbiTileBackgroundOld: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		bool tile = (version < 8005 && !_backgroundKeyDay)
			? false
			: (v == 1);
		if (Window::Theme::IsNightMode()) {
			Window::Theme::Background()->setTileNightValue(tile);
		} else {
			Window::Theme::Background()->setTileDayValue(tile);
		}
	} break;

	case dbiTileBackground: {
		qint32 tileDay, tileNight;
		stream >> tileDay >> tileNight;
		if (!_checkStreamStatus(stream)) return false;

		Window::Theme::Background()->setTileDayValue(tileDay == 1);
		Window::Theme::Background()->setTileNightValue(tileNight == 1);
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

	case dbiReplaceEmojiOld: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		GetStoredSessionSettings().setReplaceEmoji(v == 1);
	} break;

	case dbiSuggestEmojiOld: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		GetStoredSessionSettings().setSuggestEmoji(v == 1);
	} break;

	case dbiSuggestStickersByEmojiOld: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		GetStoredSessionSettings().setSuggestStickersByEmoji(v == 1);
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

		GetStoredSessionSettings().setSendFilesWay((v == 1)
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

	case dbiPlaybackSpeed: {
		qint32 v;
		stream >> v;
		if (!_checkStreamStatus(stream)) return false;

		Global::SetVoiceMsgPlaybackDoubled(v == 2);
	} break;

	case dbiCallSettings: {
		QByteArray callSettings;
		stream >> callSettings;
		if(!_checkStreamStatus(stream)) return false;

		deserializeCallSettings(callSettings);
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
	auto userDataInstance = StoredSessionSettings
		? StoredSessionSettings.get()
		: Core::App().activeAccount().getSessionSettings();
	auto userData = userDataInstance
		? userDataInstance->serialize()
		: QByteArray();
	auto callSettings = serializeCallSettings();

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
	size += sizeof(quint32) + 2 * sizeof(qint32);
	size += sizeof(quint32) + sizeof(qint64) + sizeof(qint32);
	if (!Global::HiddenPinnedMessages().isEmpty()) {
		size += sizeof(quint32) + sizeof(qint32) + Global::HiddenPinnedMessages().size() * (sizeof(PeerId) + sizeof(MsgId));
	}
	if (!userData.isEmpty()) {
		size += sizeof(quint32) + Serialize::bytearraySize(userData);
	}
	size += sizeof(quint32) + Serialize::bytearraySize(callSettings);

	EncryptedDescriptor data(size);
	data.stream
		<< quint32(dbiTileBackground)
		<< qint32(Window::Theme::Background()->tileDay() ? 1 : 0)
		<< qint32(Window::Theme::Background()->tileNight() ? 1 : 0);
	data.stream << quint32(dbiAdaptiveForWide) << qint32(Global::AdaptiveForWide() ? 1 : 0);
	data.stream << quint32(dbiAutoLock) << qint32(Global::AutoLock());
	data.stream << quint32(dbiSoundNotify) << qint32(Global::SoundNotify());
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
	data.stream << quint32(dbiDialogsMode) << qint32(Global::DialogsModeEnabled() ? 1 : 0) << static_cast<qint32>(Global::DialogsMode());
	data.stream << quint32(dbiModerateMode) << qint32(Global::ModerateModeEnabled() ? 1 : 0);
	data.stream << quint32(dbiUseExternalVideoPlayer) << qint32(cUseExternalVideoPlayer());
	data.stream << quint32(dbiCacheSettings) << qint64(_cacheTotalSizeLimit) << qint32(_cacheTotalTimeLimit) << qint64(_cacheBigFileTotalSizeLimit) << qint32(_cacheBigFileTotalTimeLimit);
	if (!userData.isEmpty()) {
		data.stream << quint32(dbiSessionSettings) << userData;
	}
	data.stream << quint32(dbiPlaybackSpeed) << qint32(Global::VoiceMsgPlaybackDoubled() ? 2 : 1);

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
	data.stream << qint32(dbiCallSettings) << callSettings;

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

	auto mtpAuthorizationSerialized = Core::App().activeAccount().serializeMtpAuthorization();

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
	auto ms = crl::now();
	QByteArray dataNameUtf8 = (cDataFile() + (cTestMode() ? qsl(":/test/") : QString())).toUtf8();
	FileKey dataNameHash[2];
	hashMd5(dataNameUtf8.constData(), dataNameUtf8.size(), dataNameHash);
	_dataNameKey = dataNameHash[0];
	_userBasePath = _basePath + toFilePart(_dataNameKey) + QChar('/');
	_userDbPath = _basePath
		+ "user_" + cDataFile()
		+ (cTestMode() ? "[test]" : "")
		+ '/';

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

	QByteArray selfSerialized;
	DraftsMap draftsMap, draftCursorsMap;
	DraftsNotReadMap draftsNotReadMap;
	quint64 locationsKey = 0, reportSpamStatusesKey = 0, trustedBotsKey = 0;
	quint64 recentStickersKeyOld = 0;
	quint64 installedStickersKey = 0, featuredStickersKey = 0, recentStickersKey = 0, favedStickersKey = 0, archivedStickersKey = 0;
	quint64 savedGifsKey = 0;
	quint64 backgroundKeyDay = 0, backgroundKeyNight = 0;
	quint64 userSettingsKey = 0, recentHashtagsAndBotsKey = 0, exportSettingsKey = 0;
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
		case lskSelfSerialized: {
			map.stream >> selfSerialized;
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
		case lskLegacyImages:
		case lskLegacyStickerImages:
		case lskLegacyAudios: {
			quint32 count = 0;
			map.stream >> count;
			for (quint32 i = 0; i < count; ++i) {
				FileKey key;
				quint64 first, second;
				qint32 size;
				map.stream >> key >> first >> second >> size;
				// Just ignore the key, it will be removed as a leaked one.
			}
		} break;
		case lskLocations: {
			map.stream >> locationsKey;
		} break;
		case lskReportSpamStatusesOld: {
			map.stream >> reportSpamStatusesKey;
			clearKey(reportSpamStatusesKey);
		} break;
		case lskTrustedBots: {
			map.stream >> trustedBotsKey;
		} break;
		case lskRecentStickersOld: {
			map.stream >> recentStickersKeyOld;
		} break;
		case lskBackgroundOld: {
			map.stream >> (Window::Theme::IsNightMode()
				? backgroundKeyNight
				: backgroundKeyDay);
		} break;
		case lskBackground: {
			map.stream >> backgroundKeyDay >> backgroundKeyNight;
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
		case lskSavedPeersOld: {
			quint64 key;
			map.stream >> key;
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

	_locationsKey = locationsKey;
	_trustedBotsKey = trustedBotsKey;
	_recentStickersKeyOld = recentStickersKeyOld;
	_installedStickersKey = installedStickersKey;
	_featuredStickersKey = featuredStickersKey;
	_recentStickersKey = recentStickersKey;
	_favedStickersKey = favedStickersKey;
	_archivedStickersKey = archivedStickersKey;
	_savedGifsKey = savedGifsKey;
	_backgroundKeyDay = backgroundKeyDay;
	_backgroundKeyNight = backgroundKeyNight;
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

	_readUserSettings();
	_readMtpData();

	DEBUG_LOG(("selfSerialized set: %1").arg(selfSerialized.size()));
	Core::App().activeAccount().setSessionFromStorage(
		std::move(StoredSessionSettings),
		std::move(selfSerialized),
		_oldMapVersion);

	LOG(("Map read time: %1").arg(crl::now() - ms));
	if (_oldSettingsVersion < AppVersion) {
		writeSettings();
	}
	return ReadMapDone;
}

void _writeMap(WriteMapWhen when) {
	Expects(_manager != nullptr);

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
	const auto self = [] {
		if (!Main::Session::Exists()) {
			DEBUG_LOG(("AuthSelf Warning: Session does not exist."));
			return QByteArray();
		}
		const auto self = Auth().user();
		if (self->phone().isEmpty()) {
			DEBUG_LOG(("AuthSelf Error: Phone is empty."));
			return QByteArray();
		}
		auto result = QByteArray();
		result.reserve(Serialize::peerSize(self)
			+ Serialize::stringSize(self->about()));
		{
			QBuffer buffer(&result);
			buffer.open(QIODevice::WriteOnly);
			QDataStream stream(&buffer);
			Serialize::writePeer(stream, self);
			stream << self->about();
		}
		return result;
	}();
	if (!self.isEmpty()) mapSize += sizeof(quint32) + Serialize::bytearraySize(self);
	if (!_draftsMap.isEmpty()) mapSize += sizeof(quint32) * 2 + _draftsMap.size() * sizeof(quint64) * 2;
	if (!_draftCursorsMap.isEmpty()) mapSize += sizeof(quint32) * 2 + _draftCursorsMap.size() * sizeof(quint64) * 2;
	if (_locationsKey) mapSize += sizeof(quint32) + sizeof(quint64);
	if (_trustedBotsKey) mapSize += sizeof(quint32) + sizeof(quint64);
	if (_recentStickersKeyOld) mapSize += sizeof(quint32) + sizeof(quint64);
	if (_installedStickersKey || _featuredStickersKey || _recentStickersKey || _archivedStickersKey) {
		mapSize += sizeof(quint32) + 4 * sizeof(quint64);
	}
	if (_favedStickersKey) mapSize += sizeof(quint32) + sizeof(quint64);
	if (_savedGifsKey) mapSize += sizeof(quint32) + sizeof(quint64);
	if (_backgroundKeyDay || _backgroundKeyNight) mapSize += sizeof(quint32) + sizeof(quint64) + sizeof(quint64);
	if (_userSettingsKey) mapSize += sizeof(quint32) + sizeof(quint64);
	if (_recentHashtagsAndBotsKey) mapSize += sizeof(quint32) + sizeof(quint64);
	if (_exportSettingsKey) mapSize += sizeof(quint32) + sizeof(quint64);

	EncryptedDescriptor mapData(mapSize);
	if (!self.isEmpty()) {
		mapData.stream << quint32(lskSelfSerialized) << self;
	}
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
	if (_locationsKey) {
		mapData.stream << quint32(lskLocations) << quint64(_locationsKey);
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
	if (_backgroundKeyDay || _backgroundKeyNight) {
		mapData.stream
			<< quint32(lskBackground)
			<< quint64(_backgroundKeyDay)
			<< quint64(_backgroundKeyNight);
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
}

} // namespace

void finish() {
	if (_manager) {
		_writeMap(WriteMapWhen::Now);
		_manager->finish();
		_manager->deleteLater();
		_manager = nullptr;
		delete base::take(_localLoader);
	}
}

void loadTheme();
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

	loadTheme();
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

	const auto dcOptionsSerialized = Core::App().dcOptions()->serialize();
	const auto applicationSettings = Core::App().settings().serialize();

	quint32 size = 12 * (sizeof(quint32) + sizeof(qint32));
	size += sizeof(quint32) + Serialize::bytearraySize(dcOptionsSerialized);
	size += sizeof(quint32) + Serialize::bytearraySize(applicationSettings);
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

	// Theme keys and night mode.
	size += sizeof(quint32) + sizeof(quint64) * 2 + sizeof(quint32);
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
	data.stream << quint32(dbiScalePercent) << qint32(cConfigScale());
	data.stream << quint32(dbiDcOptions) << dcOptionsSerialized;
	data.stream << quint32(dbiApplicationSettings) << applicationSettings;
	data.stream << quint32(dbiLoggedPhoneNumber) << cLoggedPhoneNumber();
	data.stream << quint32(dbiTxtDomainString) << Global::TxtDomainString();
	data.stream << quint32(dbiAnimationsDisabled) << qint32(anim::Disabled() ? 1 : 0);

	data.stream << quint32(dbiConnectionType) << qint32(dbictProxiesList);
	data.stream << qint32(proxies.size());
	data.stream << qint32(proxyIt - begin(proxies)) + 1;
	data.stream << qint32(Global::ProxySettings());
	data.stream << qint32(Global::UseProxyForCalls() ? 1 : 0);
	for (const auto &proxy : proxies) {
		data.stream << qint32(kProxyTypeShift + int(proxy.type));
		data.stream << proxy.host << qint32(proxy.port) << proxy.user << proxy.password;
	}

	data.stream << quint32(dbiTryIPv6) << qint32(Global::TryIPv6());
	data.stream
		<< quint32(dbiThemeKey)
		<< quint64(_themeKeyDay)
		<< quint64(_themeKeyNight)
		<< quint32(Window::Theme::IsNightMode() ? 1 : 0);
	if (_langPackKey) {
		data.stream << quint32(dbiLangPackKey) << quint64(_langPackKey);
	}
	if (_languagesKey) {
		data.stream << quint32(dbiLanguagesKey) << quint64(_languagesKey);
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
	_draftsNotReadMap.clear();
	_locationsKey = _trustedBotsKey = 0;
	_recentStickersKeyOld = 0;
	_installedStickersKey = _featuredStickersKey = _recentStickersKey = _favedStickersKey = _archivedStickersKey = 0;
	_savedGifsKey = 0;
	_backgroundKeyDay = _backgroundKeyNight = 0;
	Window::Theme::Background()->reset();
	_userSettingsKey = _recentHashtagsAndBotsKey = _exportSettingsKey = 0;
	_oldMapVersion = _oldSettingsVersion = 0;
	_cacheTotalSizeLimit = Database::Settings().totalSizeLimit;
	_cacheTotalTimeLimit = Database::Settings().totalTimeLimit;
	_cacheBigFileTotalSizeLimit = Database::Settings().totalSizeLimit;
	_cacheBigFileTotalTimeLimit = Database::Settings().totalTimeLimit;
	StoredSessionSettings.reset();
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

base::flat_set<QString> CollectGoodNames() {
	const auto keys = {
		_locationsKey,
		_userSettingsKey,
		_installedStickersKey,
		_featuredStickersKey,
		_recentStickersKey,
		_favedStickersKey,
		_archivedStickersKey,
		_recentStickersKeyOld,
		_savedGifsKey,
		_backgroundKeyNight,
		_backgroundKeyDay,
		_recentHashtagsAndBotsKey,
		_exportSettingsKey,
		_trustedBotsKey
	};
	auto result = base::flat_set<QString>{ "map0", "map1" };
	const auto push = [&](FileKey key) {
		if (!key) {
			return;
		}
		auto name = toFilePart(key) + '0';
		result.emplace(name);
		name[name.size() - 1] = '1';
		result.emplace(name);
	};
	for (const auto &value : _draftsMap) {
		push(value);
	}
	for (const auto &value : _draftCursorsMap) {
		push(value);
	}
	for (const auto &value : keys) {
		push(value);
	}
	return result;
}

void FilterLegacyFiles(FnMut<void(base::flat_set<QString>&&)> then) {
	crl::on_main([then = std::move(then)]() mutable {
		then(CollectGoodNames());
	});
}

ReadMapState readMap(const QByteArray &pass) {
	ReadMapState result = _readMap(pass);
	if (result == ReadMapFailed) {
		_mapChanged = true;
		_writeMap(WriteMapWhen::Now);
	}
	if (result != ReadMapPassNeeded) {
		Storage::ClearLegacyFiles(_userBasePath, FilterLegacyFiles);
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
	if (local.fname.isEmpty()) {
		return;
	}
	if (!local.inMediaCache()) {
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
		_fileLocationPairs.insert(local.fname, FileLocationPair(location, local));
	} else {
		for (FileLocations::iterator i = _fileLocations.find(location); (i != _fileLocations.end()) && (i.key() == location);) {
			if (i.value().inMediaCache() || i.value().check()) {
				return;
			}
			i = _fileLocations.erase(i);
		}
	}
	_fileLocations.insert(location, local);
	_writeLocations(WriteMapWhen::Fast);
}

void removeFileLocation(MediaKey location) {
	FileLocations::iterator i = _fileLocations.find(location);
	if (i == _fileLocations.end()) {
		return;
	}
	while (i != _fileLocations.end() && (i.key() == location)) {
		i = _fileLocations.erase(i);
	}
	_writeLocations(WriteMapWhen::Fast);
}

FileLocation readFileLocation(MediaKey location) {
	FileLocationAliases::const_iterator aliasIt = _fileLocationAliases.constFind(location);
	if (aliasIt != _fileLocationAliases.cend()) {
		location = aliasIt.value();
	}

	for (FileLocations::iterator i = _fileLocations.find(location); (i != _fileLocations.end()) && (i.key() == location);) {
		if (!i.value().inMediaCache() && !i.value().check()) {
			_fileLocationPairs.remove(i.value().fname);
			i = _fileLocations.erase(i);
			_writeLocations();
			continue;
		}
		return i.value();
	}
	return FileLocation();
}

Storage::EncryptionKey cacheKey() {
	Expects(LocalKey != nullptr);

	return Storage::EncryptionKey(bytes::make_vector(LocalKey->data()));
}

Storage::EncryptionKey cacheBigFileKey() {
	return cacheKey();
}

QString cachePath() {
	Expects(!_userDbPath.isEmpty());

	return _userDbPath + "cache";
}

Storage::Cache::Database::Settings cacheSettings() {
	auto result = Storage::Cache::Database::Settings();
	result.clearOnWrongKey = true;
	result.totalSizeLimit = _cacheTotalSizeLimit;
	result.totalTimeLimit = _cacheTotalTimeLimit;
	result.maxDataSize = Storage::kMaxFileInMemory;
	return result;
}

void updateCacheSettings(
		Storage::Cache::Database::SettingsUpdate &update,
		Storage::Cache::Database::SettingsUpdate &updateBig) {
	Expects(update.totalSizeLimit > Database::Settings().maxDataSize);
	Expects(update.totalTimeLimit >= 0);
	Expects(updateBig.totalSizeLimit > Database::Settings().maxDataSize);
	Expects(updateBig.totalTimeLimit >= 0);

	if (_cacheTotalSizeLimit == update.totalSizeLimit
		&& _cacheTotalTimeLimit == update.totalTimeLimit
		&& _cacheBigFileTotalSizeLimit == updateBig.totalSizeLimit
		&& _cacheBigFileTotalTimeLimit == updateBig.totalTimeLimit) {
		return;
	}
	_cacheTotalSizeLimit = update.totalSizeLimit;
	_cacheTotalTimeLimit = update.totalTimeLimit;
	_cacheBigFileTotalSizeLimit = updateBig.totalSizeLimit;
	_cacheBigFileTotalTimeLimit = updateBig.totalTimeLimit;
	_writeUserSettings();
}

QString cacheBigFilePath() {
	Expects(!_userDbPath.isEmpty());

	return _userDbPath + "media_cache";
}

Storage::Cache::Database::Settings cacheBigFileSettings() {
	auto result = Storage::Cache::Database::Settings();
	result.clearOnWrongKey = true;
	result.totalSizeLimit = _cacheBigFileTotalSizeLimit;
	result.totalTimeLimit = _cacheBigFileTotalTimeLimit;
	result.maxDataSize = Storage::kMaxFileInMemory;
	return result;
}

class CountWaveformTask : public Task {
public:
	CountWaveformTask(DocumentData *doc)
		: _doc(doc)
		, _loc(doc->location(true))
		, _data(doc->data())
		, _wavemax(0) {
		if (_data.isEmpty() && !_loc.accessEnable()) {
			_doc = nullptr;
		}
	}
	void process() override {
		if (!_doc) return;

		_waveform = audioCountWaveform(_loc, _data);
		_wavemax = _waveform.empty()
			? char(0)
			: *ranges::max_element(_waveform);
	}
	void finish() override {
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
	~CountWaveformTask() {
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
	const auto writeInfo = [&](int count) {
		stream
			<< quint64(set.id)
			<< quint64(set.access)
			<< set.title
			<< set.shortName
			<< qint32(count)
			<< qint32(set.hash)
			<< qint32(set.flags)
			<< qint32(set.installDate);
		Serialize::writeStorageImageLocation(
			stream,
			set.thumbnail ? set.thumbnail->location() : StorageImageLocation());
	};
	if (set.flags & MTPDstickerSet_ClientFlag::f_not_loaded) {
		writeInfo(-set.count);
		return;
	} else if (set.stickers.isEmpty()) {
		return;
	}

	writeInfo(set.stickers.size());
	for (const auto &sticker : set.stickers) {
		Serialize::Document::writeToStream(stream, sticker);
	}
	stream << qint32(set.dates.size());
	if (!set.dates.empty()) {
		Assert(set.dates.size() == set.stickers.size());
		for (const auto date : set.dates) {
			stream << qint32(date);
		}
	}
	stream << qint32(set.emoji.size());
	for (auto j = set.emoji.cbegin(), e = set.emoji.cend(); j != e; ++j) {
		stream << j.key()->id() << qint32(j->size());
		for (const auto sticker : *j) {
			stream << quint64(sticker->id);
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

	const auto &sets = Auth().data().stickerSets();
	if (sets.isEmpty()) {
		if (stickersKey) {
			clearKey(stickersKey);
			stickersKey = 0;
			_mapChanged = true;
		}
		_writeMap();
		return;
	}

	// versionTag + version + count
	quint32 size = sizeof(quint32) + sizeof(qint32) + sizeof(qint32);

	int32 setsCount = 0;
	for (const auto &set : sets) {
		auto result = checkSet(set);
		if (result == StickerSetCheckResult::Abort) {
			return;
		} else if (result == StickerSetCheckResult::Skip) {
			continue;
		}

		// id + access + title + shortName + stickersCount + hash + flags + installDate
		size += sizeof(quint64) * 2
			+ Serialize::stringSize(set.title)
			+ Serialize::stringSize(set.shortName)
			+ sizeof(qint32) * 4
			+ Serialize::storageImageLocationSize(set.thumbnail
				? set.thumbnail->location()
				: StorageImageLocation());
		if (set.flags & MTPDstickerSet_ClientFlag::f_not_loaded) {
			continue;
		}

		for (const auto sticker : set.stickers) {
			sticker->refreshStickerThumbFileReference();
			size += Serialize::Document::sizeInStream(sticker);
		}

		size += sizeof(qint32); // datesCount
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
	data.stream
		<< quint32(kStickersVersionTag)
		<< qint32(kStickersSerializeVersion)
		<< qint32(setsCount);
	for (const auto &set : sets) {
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

	const auto failed = [&] {
		clearKey(stickersKey);
		stickersKey = 0;
	};

	auto &sets = Auth().data().stickerSetsRef();
	if (outOrder) outOrder->clear();

	quint32 versionTag = 0;
	qint32 version = 0;
	stickers.stream >> versionTag >> version;
	if (versionTag != kStickersVersionTag
		|| version != kStickersSerializeVersion) {
		// Old data, without sticker set thumbnails.
		return failed();
	}
	qint32 count = 0;
	stickers.stream >> count;
	if (!_checkStreamStatus(stickers.stream)
		|| (count < 0)
		|| (count > kMaxSavedStickerSetsCount)) {
		return failed();
	}
	for (auto i = 0; i != count; ++i) {
		using LocationType = StorageFileLocation::Type;

		quint64 setId = 0, setAccess = 0;
		QString setTitle, setShortName;
		qint32 scnt = 0;
		qint32 setInstallDate = 0;
		qint32 setHash = 0;
		MTPDstickerSet::Flags setFlags = 0;
		qint32 setFlagsValue = 0;
		StorageImageLocation setThumbnail;

		stickers.stream
			>> setId
			>> setAccess
			>> setTitle
			>> setShortName
			>> scnt
			>> setHash
			>> setFlagsValue
			>> setInstallDate;
		const auto thumbnail = Serialize::readStorageImageLocation(
			stickers.version,
			stickers.stream);
		if (!thumbnail || !_checkStreamStatus(stickers.stream)) {
			return failed();
		} else if (thumbnail->valid()
			&& thumbnail->type() == LocationType::Legacy) {
			setThumbnail = thumbnail->convertToModern(
				LocationType::StickerSetThumb,
				setId,
				setAccess);
		} else {
			setThumbnail = *thumbnail;
		}

		setFlags = MTPDstickerSet::Flags::from_raw(setFlagsValue);
		if (setId == Stickers::DefaultSetId) {
			setTitle = tr::lng_stickers_default_set(tr::now);
			setFlags |= MTPDstickerSet::Flag::f_official | MTPDstickerSet_ClientFlag::f_special;
		} else if (setId == Stickers::CustomSetId) {
			setTitle = qsl("Custom stickers");
			setFlags |= MTPDstickerSet_ClientFlag::f_special;
		} else if (setId == Stickers::CloudRecentSetId) {
			setTitle = tr::lng_recent_stickers(tr::now);
			setFlags |= MTPDstickerSet_ClientFlag::f_special;
		} else if (setId == Stickers::FavedSetId) {
			setTitle = Lang::Hard::FavedSetTitle();
			setFlags |= MTPDstickerSet_ClientFlag::f_special;
		} else if (!setId) {
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
				setInstallDate,
				Images::CreateStickerSetThumbnail(setThumbnail)));
		}
		auto &set = it.value();
		auto inputSet = MTP_inputStickerSetID(MTP_long(set.id), MTP_long(set.access));
		const auto fillStickers = set.stickers.isEmpty();

		if (scnt < 0) { // disabled not loaded set
			if (!set.count || fillStickers) {
				set.count = -scnt;
			}
			continue;
		}

		if (fillStickers) {
			set.stickers.reserve(scnt);
			set.count = 0;
		}

		Serialize::Document::StickerSetInfo info(setId, setAccess, setShortName);
		base::flat_set<DocumentId> read;
		for (int32 j = 0; j < scnt; ++j) {
			auto document = Serialize::Document::readStickerFromStream(stickers.version, stickers.stream, info);
			if (!_checkStreamStatus(stickers.stream)) {
				return failed();
			} else if (!document
				|| !document->sticker()
				|| read.contains(document->id)) {
				continue;
			}
			read.emplace(document->id);
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

		qint32 datesCount = 0;
		stickers.stream >> datesCount;
		if (datesCount > 0) {
			if (datesCount != scnt) {
				return failed();
			}
			const auto fillDates = (set.id == Stickers::CloudRecentSetId)
				&& (set.stickers.size() == datesCount);
			if (fillDates) {
				set.dates.clear();
				set.dates.reserve(datesCount);
			}
			for (auto i = 0; i != datesCount; ++i) {
				qint32 date = 0;
				stickers.stream >> date;
				if (fillDates) {
					set.dates.push_back(TimeId(date));
				}
			}
		}

		qint32 emojiCount = 0;
		stickers.stream >> emojiCount;
		if (!_checkStreamStatus(stickers.stream) || emojiCount < 0) {
			return failed();
		}
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

	// Read orders of installed and featured stickers.
	if (outOrder) {
		stickers.stream >> *outOrder;
	}
	if (!_checkStreamStatus(stickers.stream)) {
		return failed();
	}

	// Set flags that we dropped above from the order.
	if (readingFlags && outOrder) {
		for (const auto setId : std::as_const(*outOrder)) {
			auto it = sets.find(setId);
			if (it != sets.cend()) {
				it->flags |= readingFlags;
				if ((readingFlags == MTPDstickerSet::Flag::f_installed_date)
					&& !it->installDate) {
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
		tr::lng_stickers_default_set(tr::now),
		QString(),
		0, // count
		0, // hash
		(MTPDstickerSet::Flag::f_official
			| MTPDstickerSet::Flag::f_installed_date
			| MTPDstickerSet_ClientFlag::f_special),
		kDefaultStickerInstallDate,
		ImagePtr())).value();
	auto &custom = sets.insert(Stickers::CustomSetId, Stickers::Set(
		Stickers::CustomSetId,
		uint64(0),
		qsl("Custom stickers"),
		QString(),
		0, // count
		0, // hash
		(MTPDstickerSet::Flag::f_installed_date
			| MTPDstickerSet_ClientFlag::f_special),
		kDefaultStickerInstallDate,
		ImagePtr())).value();

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
			QByteArray(),
			date,
			attributes,
			mime,
			ImagePtr(),
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
	auto result = Api::HashInit();
	for (const auto document : vector) {
		Api::HashUpdate(result, document->id);
	}
	return Api::HashFinalize(result);
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
	auto result = Api::HashInit();
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
				Api::HashUpdate(result, j->hash);
			}
		}
	}
	return (!checkOutdatedInfo || !foundOutdated)
		? Api::HashFinalize(result)
		: 0;
}

int32 countRecentStickersHash() {
	return countSpecialStickerSetHash(Stickers::CloudRecentSetId);
}

int32 countFavedStickersHash() {
	return countSpecialStickerSetHash(Stickers::FavedSetId);
}

int32 countFeaturedStickersHash() {
	auto result = Api::HashInit();
	const auto &sets = Auth().data().stickerSets();
	const auto &featured = Auth().data().featuredStickerSetsOrder();
	for (const auto setId : featured) {
		Api::HashUpdate(result, setId);

		auto it = sets.constFind(setId);
		if (it != sets.cend() && (it->flags & MTPDstickerSet_ClientFlag::f_unread)) {
			Api::HashUpdate(result, 1);
		}
	}
	return Api::HashFinalize(result);
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
	const auto failed = [&] {
		clearKey(_savedGifsKey);
		_savedGifsKey = 0;
		saved.clear();
	};
	saved.clear();

	quint32 cnt;
	gifs.stream >> cnt;
	saved.reserve(cnt);
	OrderedSet<DocumentId> read;
	for (uint32 i = 0; i < cnt; ++i) {
		auto document = Serialize::Document::readFromStream(gifs.version, gifs.stream);
		if (!_checkStreamStatus(gifs.stream)) {
			return failed();
		} else if (!document || !document->isGifv()) {
			continue;
		}

		if (read.contains(document->id)) continue;
		read.insert(document->id);

		saved.push_back(document);
	}
}

void writeBackground(const Data::WallPaper &paper, const QImage &image) {
	if (!_working() || !_backgroundCanWrite) {
		return;
	}

	if (!LocalKey) {
		LOG(("App Error: localkey not created in writeBackground()"));
		return;
	}

	auto &backgroundKey = Window::Theme::IsNightMode()
		? _backgroundKeyNight
		: _backgroundKeyDay;
	auto imageData = QByteArray();
	if (!image.isNull()) {
		const auto width = qint32(image.width());
		const auto height = qint32(image.height());
		const auto perpixel = (image.depth() >> 3);
		const auto srcperline = image.bytesPerLine();
		const auto srcsize = srcperline * height;
		const auto dstperline = width * perpixel;
		const auto dstsize = dstperline * height;
		const auto copy = (image.format() != kSavedBackgroundFormat)
			? image.convertToFormat(kSavedBackgroundFormat)
			: image;
		imageData.resize(2 * sizeof(qint32) + dstsize);

		auto dst = bytes::make_detached_span(imageData);
		bytes::copy(dst, bytes::object_as_span(&width));
		dst = dst.subspan(sizeof(qint32));
		bytes::copy(dst, bytes::object_as_span(&height));
		dst = dst.subspan(sizeof(qint32));
		const auto src = bytes::make_span(image.constBits(), srcsize);
		if (srcsize == dstsize) {
			bytes::copy(dst, src);
		} else {
			for (auto y = 0; y != height; ++y) {
				bytes::copy(dst, src.subspan(y * srcperline, dstperline));
				dst = dst.subspan(dstperline);
			}
		}
	}
	if (!backgroundKey) {
		backgroundKey = genKey();
		_mapChanged = true;
		_writeMap(WriteMapWhen::Fast);
	}
	const auto serialized = paper.serialize();
	quint32 size = sizeof(qint32)
		+ Serialize::bytearraySize(serialized)
		+ Serialize::bytearraySize(imageData);
	EncryptedDescriptor data(size);
	data.stream
		<< qint32(kWallPaperSerializeTagId)
		<< serialized
		<< imageData;

	FileWriteDescriptor file(backgroundKey);
	file.writeEncrypted(data);
}

bool readBackground() {
	FileReadDescriptor bg;
	auto &backgroundKey = Window::Theme::IsNightMode()
		? _backgroundKeyNight
		: _backgroundKeyDay;
	if (!readEncryptedFile(bg, backgroundKey)) {
		if (backgroundKey) {
			clearKey(backgroundKey);
			backgroundKey = 0;
			_mapChanged = true;
			_writeMap();
		}
		return false;
	}

	qint32 legacyId = 0;
	bg.stream >> legacyId;
	const auto paper = [&] {
		if (legacyId == kWallPaperLegacySerializeTagId) {
			quint64 id = 0;
			quint64 accessHash = 0;
			quint32 flags = 0;
			QString slug;
			bg.stream
				>> id
				>> accessHash
				>> flags
				>> slug;
			return Data::WallPaper::FromLegacySerialized(
				id,
				accessHash,
				flags,
				slug);
		} else if (legacyId == kWallPaperSerializeTagId) {
			QByteArray serialized;
			bg.stream >> serialized;
			return Data::WallPaper::FromSerialized(serialized);
		} else {
			return Data::WallPaper::FromLegacyId(legacyId);
		}
	}();
	if (bg.stream.status() != QDataStream::Ok || !paper) {
		return false;
	}

	QByteArray imageData;
	bg.stream >> imageData;
	const auto isOldEmptyImage = (bg.stream.status() != QDataStream::Ok);
	if (isOldEmptyImage
		|| Data::IsLegacy1DefaultWallPaper(*paper)
		|| Data::IsDefaultWallPaper(*paper)) {
		_backgroundCanWrite = false;
		if (isOldEmptyImage || bg.version < 8005) {
			Window::Theme::Background()->set(Data::DefaultWallPaper());
			Window::Theme::Background()->setTile(false);
		} else {
			Window::Theme::Background()->set(*paper);
		}
		_backgroundCanWrite = true;
		return true;
	} else if (Data::IsThemeWallPaper(*paper) && imageData.isEmpty()) {
		_backgroundCanWrite = false;
		Window::Theme::Background()->set(*paper);
		_backgroundCanWrite = true;
		return true;
	}
	auto image = QImage();
	if (legacyId == kWallPaperSerializeTagId) {
		const auto perpixel = 4;
		auto src = bytes::make_span(imageData);
		auto width = qint32();
		auto height = qint32();
		if (src.size() > 2 * sizeof(qint32)) {
			bytes::copy(
				bytes::object_as_span(&width),
				src.subspan(0, sizeof(qint32)));
			src = src.subspan(sizeof(qint32));
			bytes::copy(
				bytes::object_as_span(&height),
				src.subspan(0, sizeof(qint32)));
			src = src.subspan(sizeof(qint32));
			if (width + height <= kWallPaperSidesLimit
				&& src.size() == width * height * perpixel) {
				image = QImage(
					width,
					height,
					QImage::Format_ARGB32_Premultiplied);
				if (!image.isNull()) {
					const auto srcperline = width * perpixel;
					const auto srcsize = srcperline * height;
					const auto dstperline = image.bytesPerLine();
					const auto dstsize = dstperline * height;
					Assert(srcsize == dstsize);
					bytes::copy(
						bytes::make_span(image.bits(), dstsize),
						src);
				}
			}
		}
	} else {
		auto buffer = QBuffer(&imageData);
		auto reader = QImageReader(&buffer);
#ifndef OS_MAC_OLD
		reader.setAutoTransform(true);
#endif // OS_MAC_OLD
		if (!reader.read(&image)) {
			image = QImage();
		}
	}
	if (!image.isNull() || paper->backgroundColor()) {
		_backgroundCanWrite = false;
		Window::Theme::Background()->set(*paper, std::move(image));
		_backgroundCanWrite = true;
		return true;
	}
	return false;
}

Window::Theme::Saved readThemeUsingKey(FileKey key) {
	FileReadDescriptor theme;
	if (!readEncryptedFile(theme, key, FileOption::Safe, SettingsKey)) {
		return {};
	}

	auto result = Window::Theme::Saved();
	theme.stream >> result.content;
	theme.stream >> result.pathRelative >> result.pathAbsolute;
	if (theme.stream.status() != QDataStream::Ok) {
		return {};
	}

	QFile file(result.pathRelative);
	if (result.pathRelative.isEmpty() || !file.exists()) {
		file.setFileName(result.pathAbsolute);
	}

	auto changed = false;
	if (!file.fileName().isEmpty()
		&& file.exists()
		&& file.open(QIODevice::ReadOnly)) {
		if (file.size() > kThemeFileSizeLimit) {
			LOG(("Error: theme file too large: %1 "
				"(should be less than 5 MB, got %2)"
				).arg(file.fileName()
				).arg(file.size()));
			return {};
		}
		auto fileContent = file.readAll();
		file.close();
		if (result.content != fileContent) {
			result.content = fileContent;
			changed = true;
		}
	}
	if (!changed) {
		quint32 backgroundIsTiled = 0;
		theme.stream
			>> result.cache.paletteChecksum
			>> result.cache.contentChecksum
			>> result.cache.colors
			>> result.cache.background
			>> backgroundIsTiled;
		result.cache.tiled = (backgroundIsTiled == 1);
		if (theme.stream.status() != QDataStream::Ok) {
			return {};
		}
	}
	return result;
}

QString loadThemeUsingKey(FileKey key) {
	auto read = readThemeUsingKey(key);
	const auto result = read.pathAbsolute;
	return (!read.content.isEmpty() && Window::Theme::Load(std::move(read)))
		? result
		: QString();
}

void writeTheme(const Window::Theme::Saved &saved) {
	if (_themeKeyLegacy) {
		return;
	}
	auto &themeKey = Window::Theme::IsNightMode()
		? _themeKeyNight
		: _themeKeyDay;
	if (saved.content.isEmpty()) {
		if (themeKey) {
			clearKey(themeKey);
			themeKey = 0;
			writeSettings();
		}
		return;
	}

	if (!themeKey) {
		themeKey = genKey(FileOption::Safe);
		writeSettings();
	}

	auto backgroundTiled = static_cast<quint32>(saved.cache.tiled ? 1 : 0);
	quint32 size = Serialize::bytearraySize(saved.content);
	size += Serialize::stringSize(saved.pathRelative) + Serialize::stringSize(saved.pathAbsolute);
	size += sizeof(int32) * 2 + Serialize::bytearraySize(saved.cache.colors) + Serialize::bytearraySize(saved.cache.background) + sizeof(quint32);
	EncryptedDescriptor data(size);
	data.stream << saved.content;
	data.stream << saved.pathRelative << saved.pathAbsolute;
	data.stream << saved.cache.paletteChecksum << saved.cache.contentChecksum << saved.cache.colors << saved.cache.background << backgroundTiled;

	FileWriteDescriptor file(themeKey, FileOption::Safe);
	file.writeEncrypted(data, SettingsKey);
}

void clearTheme() {
	writeTheme(Window::Theme::Saved());
}

void loadTheme() {
	const auto key = (_themeKeyLegacy != 0)
		? _themeKeyLegacy
		: (Window::Theme::IsNightMode()
			? _themeKeyNight
			: _themeKeyDay);
	if (!key) {
		return;
	} else if (const auto path = loadThemeUsingKey(key); !path.isEmpty()) {
		if (_themeKeyLegacy) {
			Window::Theme::SetNightModeValue(path
				== Window::Theme::NightThemePath());
			(Window::Theme::IsNightMode()
				? _themeKeyNight
				: _themeKeyDay) = base::take(_themeKeyLegacy);
		}
	} else {
		clearTheme();
	}
}

Window::Theme::Saved readThemeAfterSwitch() {
	const auto key = Window::Theme::IsNightMode()
		? _themeKeyNight
		: _themeKeyDay;
	return readThemeUsingKey(key);
}

void readLangPack() {
	FileReadDescriptor langpack;
	if (!_langPackKey || !readEncryptedFile(langpack, _langPackKey, FileOption::Safe, SettingsKey)) {
		return;
	}
	auto data = QByteArray();
	langpack.stream >> data;
	if (langpack.stream.status() == QDataStream::Ok) {
		Lang::Current().fillFromSerialized(data, langpack.version);
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

void saveRecentLanguages(const std::vector<Lang::Language> &list) {
	if (list.empty()) {
		if (_languagesKey) {
			clearKey(_languagesKey, FileOption::Safe);
			_languagesKey = 0;
			writeSettings();
		}
		return;
	}

	auto size = sizeof(qint32);
	for (const auto &language : list) {
		size += Serialize::stringSize(language.id)
			+ Serialize::stringSize(language.pluralId)
			+ Serialize::stringSize(language.baseId)
			+ Serialize::stringSize(language.name)
			+ Serialize::stringSize(language.nativeName);
	}
	if (!_languagesKey) {
		_languagesKey = genKey(FileOption::Safe);
		writeSettings();
	}

	EncryptedDescriptor data(size);
	data.stream << qint32(list.size());
	for (const auto &language : list) {
		data.stream
			<< language.id
			<< language.pluralId
			<< language.baseId
			<< language.name
			<< language.nativeName;
	}

	FileWriteDescriptor file(_languagesKey, FileOption::Safe);
	file.writeEncrypted(data, SettingsKey);
}

void pushRecentLanguage(const Lang::Language &language) {
	if (language.id.startsWith('#')) {
		return;
	}
	auto list = readRecentLanguages();
	list.erase(
		ranges::remove_if(
			list,
			[&](const Lang::Language &v) { return (v.id == language.id); }),
		end(list));
	list.insert(list.begin(), language);

	saveRecentLanguages(list);
}

void removeRecentLanguage(const QString &id) {
	auto list = readRecentLanguages();
	list.erase(
		ranges::remove_if(
			list,
			[&](const Lang::Language &v) { return (v.id == id); }),
		end(list));

	saveRecentLanguages(list);
}

std::vector<Lang::Language> readRecentLanguages() {
	FileReadDescriptor languages;
	if (!_languagesKey || !readEncryptedFile(languages, _languagesKey, FileOption::Safe, SettingsKey)) {
		return {};
	}
	qint32 count = 0;
	languages.stream >> count;
	if (count <= 0) {
		return {};
	}
	auto result = std::vector<Lang::Language>();
	result.reserve(count);
	for (auto i = 0; i != count; ++i) {
		auto language = Lang::Language();
		languages.stream
			>> language.id
			>> language.pluralId
			>> language.baseId
			>> language.name
			>> language.nativeName;
		result.push_back(language);
	}
	if (languages.stream.status() != QDataStream::Ok) {
		return {};
	}
	return result;
}

bool copyThemeColorsToPalette(const QString &destination) {
	auto &themeKey = Window::Theme::IsNightMode()
		? _themeKeyNight
		: _themeKeyDay;
	if (!themeKey) {
		return false;
	}

	FileReadDescriptor theme;
	if (!readEncryptedFile(theme, themeKey, FileOption::Safe, SettingsKey)) {
		return false;
	}

	QByteArray themeContent;
	QString pathRelative, pathAbsolute;
	theme.stream >> themeContent >> pathRelative >> pathAbsolute;
	if (theme.stream.status() != QDataStream::Ok) {
		return false;
	}

	return Window::Theme::CopyColorsToPalette(
		destination,
		pathAbsolute,
		themeContent);
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
		for (auto i = write.cbegin(), e = write.cend(); i != e;  ++i) {
			if (!i->first.isEmpty()) {
				size += Serialize::stringSize(i->first) + sizeof(quint16);
				++writeCnt;
			}
		}
		for (auto i = search.cbegin(), e = search.cend(); i != e; ++i) {
			if (!i->first.isEmpty()) {
				size += Serialize::stringSize(i->first) + sizeof(quint16);
				++searchCnt;
			}
		}
		for (auto i = bots.cbegin(), e = bots.cend(); i != e; ++i) {
			size += Serialize::peerSize(*i);
		}

		EncryptedDescriptor data(size);
		data.stream << quint32(writeCnt) << quint32(searchCnt);
		for (auto i = write.cbegin(), e = write.cend(); i != e; ++i) {
			if (!i->first.isEmpty()) data.stream << i->first << quint16(i->second);
		}
		for (auto i = search.cbegin(), e = search.cend(); i != e; ++i) {
			if (!i->first.isEmpty()) data.stream << i->first << quint16(i->second);
		}
		data.stream << quint32(botsCnt);
		for (auto i = bots.cbegin(), e = bots.cend(); i != e; ++i) {
			Serialize::writePeer(data.stream, *i);
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
			for (auto i = 0; i < botsCount; ++i) {
				const auto peer = Serialize::readPeer(
					hashtags.version,
					hashtags.stream);
				if (!peer) {
					return; // Broken data.
				} else if (peer->isUser()
					&& peer->asUser()->isBot()
					&& !peer->asUser()->botInfo->inlinePlaceholder.isEmpty()
					&& !peer->asUser()->username.isEmpty()) {
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

std::optional<RecentHashtagPack> saveRecentHashtags(
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
	return found ? base::make_optional(recent) : std::nullopt;
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
		&& settings.availableAt == check.availableAt
		&& !settings.onlySinglePeer()) {
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
			+ Serialize::stringSize(settings.path)
			+ sizeof(qint32) * 2 + sizeof(quint64);
		EncryptedDescriptor data(size);
		data.stream
			<< quint32(settings.types)
			<< quint32(settings.fullChats)
			<< quint32(settings.media.types)
			<< quint32(settings.media.sizeLimit)
			<< quint32(settings.format)
			<< settings.path
			<< quint32(settings.availableAt);
		settings.singlePeer.match([&](const MTPDinputPeerUser & user) {
			data.stream
				<< kSinglePeerTypeUser
				<< qint32(user.vuser_id().v)
				<< quint64(user.vaccess_hash().v);
		}, [&](const MTPDinputPeerChat & chat) {
			data.stream << kSinglePeerTypeChat << qint32(chat.vchat_id().v);
		}, [&](const MTPDinputPeerChannel & channel) {
			data.stream
				<< kSinglePeerTypeChannel
				<< qint32(channel.vchannel_id().v)
				<< quint64(channel.vaccess_hash().v);
		}, [&](const MTPDinputPeerSelf &) {
			data.stream << kSinglePeerTypeSelf;
		}, [&](const MTPDinputPeerEmpty &) {
			data.stream << kSinglePeerTypeEmpty;
		}, [&](const MTPDinputPeerUserFromMessage &) {
			Unexpected("From message peer in single peer export settings.");
		}, [&](const MTPDinputPeerChannelFromMessage &) {
			Unexpected("From message peer in single peer export settings.");
		});
		data.stream << qint32(settings.singlePeerFrom);
		data.stream << qint32(settings.singlePeerTill);

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
	qint32 singlePeerType = 0, singlePeerBareId = 0;
	quint64 singlePeerAccessHash = 0;
	qint32 singlePeerFrom = 0, singlePeerTill = 0;
	file.stream
		>> types
		>> fullChats
		>> mediaTypes
		>> mediaSizeLimit
		>> format
		>> path
		>> availableAt;
	if (!file.stream.atEnd()) {
		file.stream >> singlePeerType;
		switch (singlePeerType) {
		case kSinglePeerTypeUser:
		case kSinglePeerTypeChannel: {
			file.stream >> singlePeerBareId >> singlePeerAccessHash;
		} break;
		case kSinglePeerTypeChat: file.stream >> singlePeerBareId; break;
		case kSinglePeerTypeSelf:
		case kSinglePeerTypeEmpty: break;
		default: return Export::Settings();
		}
	}
	if (!file.stream.atEnd()) {
		file.stream >> singlePeerFrom >> singlePeerTill;
	}
	auto result = Export::Settings();
	result.types = Export::Settings::Types::from_raw(types);
	result.fullChats = Export::Settings::Types::from_raw(fullChats);
	result.media.types = Export::MediaSettings::Types::from_raw(mediaTypes);
	result.media.sizeLimit = mediaSizeLimit;
	result.format = Export::Output::Format(format);
	result.path = path;
	result.availableAt = availableAt;
	result.singlePeer = [&] {
		switch (singlePeerType) {
		case kSinglePeerTypeUser:
			return MTP_inputPeerUser(
				MTP_int(singlePeerBareId),
				MTP_long(singlePeerAccessHash));
		case kSinglePeerTypeChat:
			return MTP_inputPeerChat(MTP_int(singlePeerBareId));
		case kSinglePeerTypeChannel:
			return MTP_inputPeerChannel(
				MTP_int(singlePeerBareId),
				MTP_long(singlePeerAccessHash));
		case kSinglePeerTypeSelf:
			return MTP_inputPeerSelf();
		case kSinglePeerTypeEmpty:
			return MTP_inputPeerEmpty();
		}
		Unexpected("Type in export data single peer.");
	}();
	result.singlePeerFrom = singlePeerFrom;
	result.singlePeerTill = singlePeerTill;
	return (file.stream.status() == QDataStream::Ok && result.validate())
		? result
		: Export::Settings();
}

void writeSelf() {
	_mapChanged = true;
	_writeMap();
}

void readSelf(const QByteArray &serialized, int32 streamVersion) {
	QDataStream stream(serialized);
	const auto user = Auth().user();
	const auto wasLoadedStatus = std::exchange(
		user->loadedStatus,
		PeerData::NotLoaded);
	const auto self = Serialize::readPeer(streamVersion, stream);
	if (!self || !self->isSelf() || self != user) {
		user->loadedStatus = wasLoadedStatus;
		return;
	}

	QString about;
	stream >> about;
	if (_checkStreamStatus(stream)) {
		self->asUser()->setAbout(about);
	}
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
		_writeMap();
	} else {
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
		{
			QMutexLocker lock(&data->mutex);
			if (data->tasks.isEmpty()) {
				data->working = false;
				break;
			}
			task = data->tasks.at(0);
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
		_mapWriteTimer.start(fast ? 1 : kWriteMapTimeout);
	} else if (_mapWriteTimer.remainingTime() <= 0) {
		mapWriteTimeout();
	}
}

void Manager::writingMap() {
	_mapWriteTimer.stop();
}

void Manager::writeLocations(bool fast) {
	if (!_locationsWriteTimer.isActive() || fast) {
		_locationsWriteTimer.start(fast ? 1 : kWriteMapTimeout);
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
