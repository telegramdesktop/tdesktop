/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/localstorage.h"
//
#include "storage/serialize_common.h"
#include "storage/storage_account.h"
#include "storage/details/storage_file_utilities.h"
#include "storage/details/storage_settings_scheme.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "base/platform/base_platform_info.h"
#include "ui/effects/animation_value.h"
#include "core/update_checker.h"
#include "core/file_location.h"
#include "core/application.h"
#include "media/audio/media_audio.h"
#include "mtproto/mtproto_config.h"
#include "mtproto/mtproto_dc_options.h"
#include "main/main_domain.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "window/themes/window_theme.h"
#include "lang/lang_instance.h"
#include "facades.h"

#include <QtCore/QDirIterator>

#ifndef Q_OS_WIN
#include <unistd.h>
#endif // Q_OS_WIN

//extern "C" {
//#include <openssl/evp.h>
//} // extern "C"

namespace Local {
namespace {

constexpr auto kThemeFileSizeLimit = 5 * 1024 * 1024;
constexpr auto kFileLoaderQueueStopTimeout = crl::time(5000);

constexpr auto kSavedBackgroundFormat = QImage::Format_ARGB32_Premultiplied;
constexpr auto kWallPaperLegacySerializeTagId = int32(-111);
constexpr auto kWallPaperSerializeTagId = int32(-112);
constexpr auto kWallPaperSidesLimit = 10'000;

const auto kThemeNewPathRelativeTag = qstr("special://new_tag");

using namespace Storage::details;
using Storage::FileKey;

using Database = Storage::Cache::Database;

QString _basePath, _userBasePath, _userDbPath;

bool _started = false;
TaskQueue *_localLoader = nullptr;

QByteArray _settingsSalt;

auto OldKey = MTP::AuthKeyPtr();
auto SettingsKey = MTP::AuthKeyPtr();

FileKey _themeKeyDay = 0;
FileKey _themeKeyNight = 0;

// Theme key legacy may be read in start() with settings.
// But it should be moved to keyDay or keyNight inside InitialLoadTheme()
// and never used after.
FileKey _themeKeyLegacy = 0;
FileKey _langPackKey = 0;
FileKey _languagesKey = 0;

FileKey _backgroundKeyDay = 0;
FileKey _backgroundKeyNight = 0;
bool _useGlobalBackgroundKeys = false;
bool _backgroundCanWrite = true;

int32 _oldSettingsVersion = 0;
bool _settingsRewritten = false;
bool _settingsRewriteNeeded = false;
bool _settingsWriteAllowed = false;

enum class WriteMapWhen {
	Now,
	Fast,
	Soon,
};

bool _working() {
	return !_basePath.isEmpty();
}

bool CheckStreamStatus(QDataStream &stream) {
	if (stream.status() != QDataStream::Ok) {
		LOG(("Bad data stream status: %1").arg(stream.status()));
		return false;
	}
	return true;
}

[[nodiscard]] const MTP::Config &LookupFallbackConfig() {
	static const auto lookupConfig = [](not_null<Main::Account*> account) {
		const auto mtp = &account->mtp();
		const auto production = MTP::Environment::Production;
		return (mtp->environment() == production)
			? &mtp->config()
			: nullptr;
	};
	const auto &app = Core::App();
	const auto &domain = app.domain();
	if (!domain.started()) {
		return app.fallbackProductionConfig();
	}
	if (const auto result = lookupConfig(&app.activeAccount())) {
		return *result;
	}
	for (const auto &[_, account] : domain.accounts()) {
		if (const auto result = lookupConfig(account.get())) {
			return *result;
		}
	}
	return app.fallbackProductionConfig();
}

void applyReadContext(ReadSettingsContext &&context) {
	ApplyReadFallbackConfig(context);

	_themeKeyLegacy = context.themeKeyLegacy;
	_themeKeyDay = context.themeKeyDay;
	_themeKeyNight = context.themeKeyNight;
	_backgroundKeyDay = context.backgroundKeyDay;
	_backgroundKeyNight = context.backgroundKeyNight;
	_useGlobalBackgroundKeys = context.backgroundKeysRead;
	_langPackKey = context.langPackKey;
	_languagesKey = context.languagesKey;
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
			if (!CheckStreamStatus(stream)) break;

			if (blockId == dbiVersion) {
				stream >> version;
				if (!CheckStreamStatus(stream)) break;

				if (version > AppVersion) break;
			} else if (!ReadSetting(blockId, stream, version, context)) {
				break;
			}
		}
		file.close();
		result = true;
	}
	if (remove) file.remove();
	return result;
}

void _readOldUserSettingsFields(
		QIODevice *device,
		qint32 &version,
		ReadSettingsContext &context) {
	QDataStream stream(device);
	stream.setVersion(QDataStream::Qt_5_1);

	while (!stream.atEnd()) {
		quint32 blockId;
		stream >> blockId;
		if (!CheckStreamStatus(stream)) {
			break;
		}

		if (blockId == dbiVersion) {
			stream >> version;
			if (!CheckStreamStatus(stream)) {
				break;
			}

			if (version > AppVersion) return;
		} else if (blockId == dbiEncryptedWithSalt) {
			QByteArray salt, data, decrypted;
			stream >> salt >> data;
			if (!CheckStreamStatus(stream)) {
				break;
			}

			if (salt.size() != 32) {
				LOG(("App Error: bad salt in old user config encrypted part, size: %1").arg(salt.size()));
				continue;
			}

			OldKey = CreateLegacyLocalKey(QByteArray(), salt);

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
		} else if (!ReadSetting(blockId, stream, version, context)) {
			return;
		}
	}
}

bool _readOldUserSettings(bool remove, ReadSettingsContext &context) {
	bool result = false;
	// We dropped old test authorizations when migrated to multi auth.
	//const auto testPrefix = (cTestMode() ? qsl("_test") : QString());
	const auto testPrefix = QString();
	QFile file(cWorkingDir() + cDataFile() + testPrefix + qsl("_config"));
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

void _readOldMtpDataFields(
		QIODevice *device,
		qint32 &version,
		ReadSettingsContext &context) {
	QDataStream stream(device);
	stream.setVersion(QDataStream::Qt_5_1);

	while (!stream.atEnd()) {
		quint32 blockId;
		stream >> blockId;
		if (!CheckStreamStatus(stream)) {
			break;
		}

		if (blockId == dbiVersion) {
			stream >> version;
			if (!CheckStreamStatus(stream)) {
				break;
			}

			if (version > AppVersion) return;
		} else if (blockId == dbiEncrypted) {
			QByteArray data, decrypted;
			stream >> data;
			if (!CheckStreamStatus(stream)) {
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
		} else if (!ReadSetting(blockId, stream, version, context)) {
			return;
		}
	}
}

bool _readOldMtpData(bool remove, ReadSettingsContext &context) {
	bool result = false;
	// We dropped old test authorizations when migrated to multi auth.
	//const auto testPostfix = (cTestMode() ? qsl("_test") : QString());
	const auto testPostfix = QString();
	QFile file(cWorkingDir() + cDataFile() + testPostfix);
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

} // namespace

void finish() {
	delete base::take(_localLoader);
}

void InitialLoadTheme();
bool ApplyDefaultNightMode();
void readLangPack();

void start() {
	Expects(_basePath.isEmpty());

	_localLoader = new TaskQueue(kFileLoaderQueueStopTimeout);

	_basePath = cWorkingDir() + qsl("tdata/");
	if (!QDir().exists(_basePath)) QDir().mkpath(_basePath);

	ReadSettingsContext context;
	FileReadDescriptor settingsData;
	// We dropped old test authorizations when migrated to multi auth.
	//const auto name = cTestMode() ? qsl("settings_test") : qsl("settings");
	const auto name = u"settings"_q;
	if (!ReadFile(settingsData, name, _basePath)) {
		_readOldSettings(true, context);
		_readOldUserSettings(false, context); // needed further in _readUserSettings
		_readOldMtpData(false, context); // needed further in _readMtpData
		applyReadContext(std::move(context));

		_settingsRewriteNeeded = true;
		ApplyDefaultNightMode();
		return;
	}
	LOG(("App Info: reading settings..."));

	QByteArray salt, settingsEncrypted;
	settingsData.stream >> salt >> settingsEncrypted;
	if (!CheckStreamStatus(settingsData.stream)) {
		return writeSettings();
	}

	if (salt.size() != LocalEncryptSaltSize) {
		LOG(("App Error: bad salt in settings file, size: %1").arg(salt.size()));
		return writeSettings();
	}
	SettingsKey = CreateLegacyLocalKey(QByteArray(), salt);

	EncryptedDescriptor settings;
	if (!DecryptLocal(settings, settingsEncrypted, SettingsKey)) {
		LOG(("App Error: could not decrypt settings from settings file..."));
		return writeSettings();
	}

	LOG(("App Info: reading encrypted settings..."));
	while (!settings.stream.atEnd()) {
		quint32 blockId;
		settings.stream >> blockId;
		if (!CheckStreamStatus(settings.stream)) {
			return writeSettings();
		}

		if (!ReadSetting(blockId, settings.stream, settingsData.version, context)) {
			return writeSettings();
		}
	}

	_oldSettingsVersion = settingsData.version;
	_settingsSalt = salt;

	applyReadContext(std::move(context));
	if (context.legacyRead) {
		writeSettings();
	}

	InitialLoadTheme();

	if (context.tileRead && _useGlobalBackgroundKeys) {
		Window::Theme::Background()->setTileDayValue(context.tileDay);
		Window::Theme::Background()->setTileNightValue(context.tileNight);
	}

	readLangPack();
}

void writeSettings() {
	if (!_settingsWriteAllowed) {
		_settingsRewriteNeeded = true;

		// We need to generate SettingsKey anyway,
		// for the moveLegacyBackground to work.
		if (SettingsKey) {
			return;
		}
	}
	if (_basePath.isEmpty()) {
		LOG(("App Error: _basePath is empty in writeSettings()"));
		return;
	}

	if (!QDir().exists(_basePath)) QDir().mkpath(_basePath);

	// We dropped old test authorizations when migrated to multi auth.
	//const auto name = cTestMode() ? qsl("settings_test") : qsl("settings");
	const auto name = u"settings"_q;
	FileWriteDescriptor settings(name, _basePath);
	if (_settingsSalt.isEmpty() || !SettingsKey) {
		_settingsSalt.resize(LocalEncryptSaltSize);
		memset_rand(_settingsSalt.data(), _settingsSalt.size());
		SettingsKey = CreateLegacyLocalKey(QByteArray(), _settingsSalt);
	}
	settings.writeData(_settingsSalt);

	if (!_settingsWriteAllowed) {
		EncryptedDescriptor data(0);
		settings.writeEncrypted(data, SettingsKey);
		return;
	}
	const auto configSerialized = LookupFallbackConfig().serialize();
	const auto applicationSettings = Core::App().settings().serialize();

	quint32 size = 9 * (sizeof(quint32) + sizeof(qint32));
	size += sizeof(quint32) + Serialize::bytearraySize(configSerialized);
	size += sizeof(quint32) + Serialize::bytearraySize(applicationSettings);
	size += sizeof(quint32) + Serialize::stringSize(cDialogLastPath());

	auto &proxies = Global::RefProxiesList();
	const auto &proxy = Global::SelectedProxy();
	auto proxyIt = ranges::find(proxies, proxy);
	if (proxy.type != MTP::ProxyData::Type::None
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
	size += sizeof(quint32) + sizeof(quint64) * 2;
	if (_langPackKey) {
		size += sizeof(quint32) + sizeof(quint64);
	}
	size += sizeof(quint32) + sizeof(qint32) * 8;

	EncryptedDescriptor data(size);
	data.stream << quint32(dbiAutoStart) << qint32(cAutoStart());
	data.stream << quint32(dbiStartMinimized) << qint32(cStartMinimized());
	data.stream << quint32(dbiSendToMenu) << qint32(cSendToMenu());
	data.stream << quint32(dbiWorkMode) << qint32(Global::WorkMode().value());
	data.stream << quint32(dbiSeenTrayTooltip) << qint32(cSeenTrayTooltip());
	data.stream << quint32(dbiAutoUpdate) << qint32(cAutoUpdate());
	data.stream << quint32(dbiLastUpdateCheck) << qint32(cLastUpdateCheck());
	data.stream << quint32(dbiScalePercent) << qint32(cConfigScale());
	data.stream << quint32(dbiFallbackProductionConfig) << configSerialized;
	data.stream << quint32(dbiApplicationSettings) << applicationSettings;
	data.stream << quint32(dbiDialogLastPath) << cDialogLastPath();
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
	if (_useGlobalBackgroundKeys) {
		data.stream
			<< quint32(dbiBackgroundKey)
			<< quint64(_backgroundKeyDay)
			<< quint64(_backgroundKeyNight);
		data.stream
			<< quint32(dbiTileBackground)
			<< qint32(Window::Theme::Background()->tileDay() ? 1 : 0)
			<< qint32(Window::Theme::Background()->tileNight() ? 1 : 0);
	}
	if (_langPackKey) {
		data.stream << quint32(dbiLangPackKey) << quint64(_langPackKey);
	}
	if (_languagesKey) {
		data.stream << quint32(dbiLanguagesKey) << quint64(_languagesKey);
	}

	settings.writeEncrypted(data, SettingsKey);
}

void rewriteSettingsIfNeeded() {
	if (_settingsWriteAllowed) {
		return;
	}
	_settingsWriteAllowed = true;
	if (_oldSettingsVersion < AppVersion || _settingsRewriteNeeded) {
		writeSettings();
	}
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

void writeBackground(const Data::WallPaper &paper, const QImage &image) {
	Expects(_settingsWriteAllowed);

	if (!_backgroundCanWrite) {
		return;
	}

	_useGlobalBackgroundKeys = true;
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
		backgroundKey = GenerateKey(_basePath);
		writeSettings();
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

	FileWriteDescriptor file(backgroundKey, _basePath);
	file.writeEncrypted(data, SettingsKey);
}

bool readBackground() {
	FileReadDescriptor bg;
	auto &backgroundKey = Window::Theme::IsNightMode()
		? _backgroundKeyNight
		: _backgroundKeyDay;
	if (!ReadEncryptedFile(bg, backgroundKey, _basePath, SettingsKey)) {
		if (backgroundKey) {
			ClearKey(backgroundKey, _basePath);
			backgroundKey = 0;
			writeSettings();
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

void moveLegacyBackground(
		const QString &fromBasePath,
		const MTP::AuthKeyPtr &fromLocalKey,
		uint64 legacyBackgroundKeyDay,
		uint64 legacyBackgroundKeyNight) {
	if (_useGlobalBackgroundKeys
		|| (!legacyBackgroundKeyDay && !legacyBackgroundKeyNight)) {
		return;
	}
	const auto move = [&](uint64 from, FileKey &to) {
		if (!from || to) {
			return;
		}
		to = GenerateKey(_basePath);
		FileReadDescriptor read;
		if (!ReadEncryptedFile(read, from, fromBasePath, fromLocalKey)) {
			return;
		}
		EncryptedDescriptor data;
		data.data = read.data;
		FileWriteDescriptor write(to, _basePath);
		write.writeEncrypted(data, SettingsKey);
	};
	move(legacyBackgroundKeyDay, _backgroundKeyDay);
	move(legacyBackgroundKeyNight, _backgroundKeyNight);
	_useGlobalBackgroundKeys = true;
	_settingsRewriteNeeded = true;
}

void reset() {
	if (_localLoader) {
		_localLoader->stop();
	}

	Window::Theme::Background()->reset();
	_oldSettingsVersion = 0;
	Core::App().settings().resetOnLastLogout();
	writeSettings();
}

int32 oldSettingsVersion() {
	return _oldSettingsVersion;
}

class CountWaveformTask : public Task {
public:
	CountWaveformTask(not_null<Data::DocumentMedia*> media)
	: _doc(media->owner())
	, _loc(_doc->location(true))
	, _data(media->bytes())
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
			_doc->owner().requestDocumentViewRepaint(_doc);
		}
	}
	~CountWaveformTask() {
		if (_data.isEmpty() && _doc) {
			_loc.accessDisable();
		}
	}

protected:
	DocumentData *_doc = nullptr;
	Core::FileLocation _loc;
	QByteArray _data;
	VoiceWaveform _waveform;
	char _wavemax;

};

void countVoiceWaveform(not_null<Data::DocumentMedia*> media) {
	const auto document = media->owner();
	if (const auto voice = document->voice()) {
		if (_localLoader) {
			voice->waveform.resize(1 + sizeof(TaskId));
			voice->waveform[0] = -1; // counting
			TaskId taskId = _localLoader->addTask(
				std::make_unique<CountWaveformTask>(media));
			memcpy(voice->waveform.data() + 1, &taskId, sizeof(taskId));
		}
	}
}

void cancelTask(TaskId id) {
	if (_localLoader) {
		_localLoader->cancelTask(id);
	}
}

Window::Theme::Saved readThemeUsingKey(FileKey key) {
	using namespace Window::Theme;

	FileReadDescriptor theme;
	if (!ReadEncryptedFile(theme, key, _basePath, SettingsKey)) {
		return {};
	}

	auto tag = QString();
	auto result = Saved();
	auto &object = result.object;
	auto &cache = result.cache;
	theme.stream >> object.content;
	theme.stream >> tag >> object.pathAbsolute;
	if (tag == kThemeNewPathRelativeTag) {
		auto creator = qint32();
		theme.stream
			>> object.pathRelative
			>> object.cloud.id
			>> object.cloud.accessHash
			>> object.cloud.slug
			>> object.cloud.title
			>> object.cloud.documentId
			>> creator;
		object.cloud.createdBy = creator;
	} else {
		object.pathRelative = tag;
	}
	if (theme.stream.status() != QDataStream::Ok) {
		return {};
	}

	auto ignoreCache = false;
	if (!object.cloud.id) {
		auto file = QFile(object.pathRelative);
		if (object.pathRelative.isEmpty() || !file.exists()) {
			file.setFileName(object.pathAbsolute);
		}
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
			if (object.content != fileContent) {
				object.content = fileContent;
				ignoreCache = true;
			}
		}
	}
	if (!ignoreCache) {
		quint32 backgroundIsTiled = 0;
		theme.stream
			>> cache.paletteChecksum
			>> cache.contentChecksum
			>> cache.colors
			>> cache.background
			>> backgroundIsTiled;
		cache.tiled = (backgroundIsTiled == 1);
		if (theme.stream.status() != QDataStream::Ok) {
			return {};
		}
	}
	return result;
}

std::optional<QString> InitialLoadThemeUsingKey(FileKey key) {
	auto read = readThemeUsingKey(key);
	const auto result = read.object.pathAbsolute;
	if (read.object.content.isEmpty()
		|| !Window::Theme::Initialize(std::move(read))) {
		return std::nullopt;
	}
	return result;
}

void writeTheme(const Window::Theme::Saved &saved) {
	using namespace Window::Theme;

	if (_themeKeyLegacy) {
		return;
	}
	auto &themeKey = IsNightMode()
		? _themeKeyNight
		: _themeKeyDay;
	if (saved.object.content.isEmpty()) {
		if (themeKey) {
			ClearKey(themeKey, _basePath);
			themeKey = 0;
			writeSettings();
		}
		return;
	}

	if (!themeKey) {
		themeKey = GenerateKey(_basePath);
		writeSettings();
	}

	const auto &object = saved.object;
	const auto &cache = saved.cache;
	const auto tag = QString(kThemeNewPathRelativeTag);
	quint32 size = Serialize::bytearraySize(object.content)
		+ Serialize::stringSize(tag)
		+ Serialize::stringSize(object.pathAbsolute)
		+ Serialize::stringSize(object.pathRelative)
		+ sizeof(uint64) * 3
		+ Serialize::stringSize(object.cloud.slug)
		+ Serialize::stringSize(object.cloud.title)
		+ sizeof(qint32)
		+ sizeof(qint32) * 2
		+ Serialize::bytearraySize(cache.colors)
		+ Serialize::bytearraySize(cache.background)
		+ sizeof(quint32);
	EncryptedDescriptor data(size);
	data.stream
		<< object.content
		<< tag
		<< object.pathAbsolute
		<< object.pathRelative
		<< object.cloud.id
		<< object.cloud.accessHash
		<< object.cloud.slug
		<< object.cloud.title
		<< object.cloud.documentId
		<< qint32(object.cloud.createdBy)
		<< cache.paletteChecksum
		<< cache.contentChecksum
		<< cache.colors
		<< cache.background
		<< quint32(cache.tiled ? 1 : 0);

	FileWriteDescriptor file(themeKey, _basePath);
	file.writeEncrypted(data, SettingsKey);
}

void clearTheme() {
	writeTheme(Window::Theme::Saved());
}

void InitialLoadTheme() {
	const auto key = (_themeKeyLegacy != 0)
		? _themeKeyLegacy
		: (Window::Theme::IsNightMode()
			? _themeKeyNight
			: _themeKeyDay);
	if (!key) {
		return;
	} else if (const auto path = InitialLoadThemeUsingKey(key)) {
		if (_themeKeyLegacy) {
			Window::Theme::SetNightModeValue(*path
				== Window::Theme::NightThemePath());
			(Window::Theme::IsNightMode()
				? _themeKeyNight
				: _themeKeyDay) = base::take(_themeKeyLegacy);
		}
	} else {
		clearTheme();
	}
}

bool ApplyDefaultNightMode() {
	const auto NightByDefault = Platform::IsMacStoreBuild();
	if (!NightByDefault
		|| Window::Theme::IsNightMode()
		|| _themeKeyDay
		|| _themeKeyNight
		|| _themeKeyLegacy) {
		return false;
	}
	Core::App().startSettingsAndBackground();
	Window::Theme::ToggleNightMode();
	Window::Theme::KeepApplied();
	return true;
}

Window::Theme::Saved readThemeAfterSwitch() {
	const auto key = Window::Theme::IsNightMode()
		? _themeKeyNight
		: _themeKeyDay;
	return readThemeUsingKey(key);
}

void readLangPack() {
	FileReadDescriptor langpack;
	if (!_langPackKey || !ReadEncryptedFile(langpack, _langPackKey, _basePath, SettingsKey)) {
		return;
	}
	auto data = QByteArray();
	langpack.stream >> data;
	if (langpack.stream.status() == QDataStream::Ok) {
		Lang::GetInstance().fillFromSerialized(data, langpack.version);
	}
}

void writeLangPack() {
	auto langpack = Lang::GetInstance().serialize();
	if (!_langPackKey) {
		_langPackKey = GenerateKey(_basePath);
		writeSettings();
	}

	EncryptedDescriptor data(Serialize::bytearraySize(langpack));
	data.stream << langpack;

	FileWriteDescriptor file(_langPackKey, _basePath);
	file.writeEncrypted(data, SettingsKey);
}

void saveRecentLanguages(const std::vector<Lang::Language> &list) {
	if (list.empty()) {
		if (_languagesKey) {
			ClearKey(_languagesKey, _basePath);
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
		_languagesKey = GenerateKey(_basePath);
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

	FileWriteDescriptor file(_languagesKey, _basePath);
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
	if (!_languagesKey || !ReadEncryptedFile(languages, _languagesKey, _basePath, SettingsKey)) {
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

Window::Theme::Object ReadThemeContent() {
	using namespace Window::Theme;

	auto &themeKey = IsNightMode() ? _themeKeyNight : _themeKeyDay;
	if (!themeKey) {
		return Object();
	}

	FileReadDescriptor theme;
	if (!ReadEncryptedFile(theme, themeKey, _basePath, SettingsKey)) {
		return Object();
	}

	QByteArray content;
	QString pathRelative, pathAbsolute;
	theme.stream >> content >> pathRelative >> pathAbsolute;
	if (theme.stream.status() != QDataStream::Ok) {
		return Object();
	}

	auto result = Object();
	result.pathAbsolute = pathAbsolute;
	result.content = content;
	return result;
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

bool readOldMtpData(bool remove, ReadSettingsContext &context) {
	return _readOldMtpData(remove, context);
}

bool readOldUserSettings(bool remove, ReadSettingsContext &context) {
	return _readOldUserSettings(remove, context);
}

} // namespace Local
