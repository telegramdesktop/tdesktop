/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/storage_account.h"

#include "storage/localstorage.h"
#include "storage/storage_domain.h"
#include "storage/storage_encryption.h"
#include "storage/storage_clear_legacy.h"
#include "storage/cache/storage_cache_types.h"
#include "storage/details/storage_file_utilities.h"
#include "storage/details/storage_settings_scheme.h"
#include "storage/serialize_common.h"
#include "storage/serialize_peer.h"
#include "storage/serialize_document.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "mtproto/mtproto_config.h"
#include "mtproto/mtproto_dc_options.h"
#include "mtproto/mtp_instance.h"
#include "history/history.h"
#include "core/application.h"
#include "core/file_location.h"
#include "data/stickers/data_stickers.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_user.h"
#include "data/data_drafts.h"
#include "export/export_settings.h"
#include "window/themes/window_theme.h"

namespace Storage {
namespace {

using namespace details;
using Database = Cache::Database;

constexpr auto kDelayedWriteTimeout = crl::time(1000);

constexpr auto kStickersVersionTag = quint32(-1);
constexpr auto kStickersSerializeVersion = 1;
constexpr auto kMaxSavedStickerSetsCount = 1000;
constexpr auto kDefaultStickerInstallDate = TimeId(1);

constexpr auto kSinglePeerTypeUser = qint32(1);
constexpr auto kSinglePeerTypeChat = qint32(2);
constexpr auto kSinglePeerTypeChannel = qint32(3);
constexpr auto kSinglePeerTypeSelf = qint32(4);
constexpr auto kSinglePeerTypeEmpty = qint32(0);

enum { // Local Storage Keys
	lskUserMap = 0x00,
	lskDraft = 0x01, // data: PeerId peer
	lskDraftPosition = 0x02, // data: PeerId peer
	lskLegacyImages = 0x03, // legacy
	lskLocations = 0x04, // no data
	lskLegacyStickerImages = 0x05, // legacy
	lskLegacyAudios = 0x06, // legacy
	lskRecentStickersOld = 0x07, // no data
	lskBackgroundOldOld = 0x08, // no data
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
	lskBackgroundOld = 0x14, // no data
	lskSelfSerialized = 0x15, // serialized self
};

[[nodiscard]] FileKey ComputeDataNameKey(const QString &dataName) {
	// We dropped old test authorizations when migrated to multi auth.
	//const auto testAddition = (cTestMode() ? qsl(":/test/") : QString());
	const auto testAddition = QString();
	const auto dataNameUtf8 = (dataName + testAddition).toUtf8();
	FileKey dataNameHash[2] = { 0 };
	hashMd5(dataNameUtf8.constData(), dataNameUtf8.size(), dataNameHash);
	return dataNameHash[0];
}

[[nodiscard]] QString BaseGlobalPath() {
	return cWorkingDir() + qsl("tdata/");
}

[[nodiscard]] QString ComputeDatabasePath(const QString &dataName) {
	return BaseGlobalPath()
		+ "user_" + dataName
		// We dropped old test authorizations when migrated to multi auth.
		//+ (cTestMode() ? "[test]" : "")
		+ '/';
}

[[nodiscard]] QString LegacyTempDirectory() {
	return cWorkingDir() + qsl("tdata/tdld/");
}

} // namespace

Account::Account(not_null<Main::Account*> owner, const QString &dataName)
: _owner(owner)
, _dataName(dataName)
, _dataNameKey(ComputeDataNameKey(dataName))
, _basePath(BaseGlobalPath() + ToFilePart(_dataNameKey) + QChar('/'))
, _tempPath(BaseGlobalPath() + "temp_" + _dataName + QChar('/'))
, _databasePath(ComputeDatabasePath(dataName))
, _cacheTotalSizeLimit(Database::Settings().totalSizeLimit)
, _cacheBigFileTotalSizeLimit(Database::Settings().totalSizeLimit)
, _cacheTotalTimeLimit(Database::Settings().totalTimeLimit)
, _cacheBigFileTotalTimeLimit(Database::Settings().totalTimeLimit)
, _writeMapTimer([=] { writeMap(); })
, _writeLocationsTimer([=] { writeLocations(); }) {
}

Account::~Account() {
	if (_localKey && _mapChanged) {
		writeMap();
	}
}

QString Account::tempDirectory() const {
	return _tempPath;
}

StartResult Account::legacyStart(const QByteArray &passcode) {
	const auto result = readMapWith(MTP::AuthKeyPtr(), passcode);
	if (result == ReadMapResult::Failed) {
		Assert(_localKey == nullptr);
	} else if (result == ReadMapResult::IncorrectPasscode) {
		return StartResult::IncorrectPasscodeLegacy;
	}
	clearLegacyFiles();
	return StartResult::Success;
}

std::unique_ptr<MTP::Config> Account::start(MTP::AuthKeyPtr localKey) {
	Expects(localKey != nullptr);

	_localKey = std::move(localKey);
	readMapWith(_localKey);
	clearLegacyFiles();
	return readMtpConfig();
}

void Account::startAdded(MTP::AuthKeyPtr localKey) {
	Expects(localKey != nullptr);

	_localKey = std::move(localKey);
	clearLegacyFiles();
}

void Account::clearLegacyFiles() {
	const auto weak = base::make_weak(_owner.get());
	ClearLegacyFiles(_basePath, [weak, this](
			FnMut<void(base::flat_set<QString>&&)> then) {
		crl::on_main(weak, [this, then = std::move(then)]() mutable {
			then(collectGoodNames());
		});
	});
}

base::flat_set<QString> Account::collectGoodNames() const {
	const auto keys = {
		_locationsKey,
		_settingsKey,
		_installedStickersKey,
		_featuredStickersKey,
		_recentStickersKey,
		_favedStickersKey,
		_archivedStickersKey,
		_recentStickersKeyOld,
		_savedGifsKey,
		_legacyBackgroundKeyNight,
		_legacyBackgroundKeyDay,
		_recentHashtagsAndBotsKey,
		_exportSettingsKey,
		_trustedBotsKey,
	};
	auto result = base::flat_set<QString>{
		"map0",
		"map1",
		"maps",
		"configs",
	};
	const auto push = [&](FileKey key) {
		if (!key) {
			return;
		}
		auto name = ToFilePart(key) + '0';
		result.emplace(name);
		name[name.size() - 1] = '1';
		result.emplace(name);
		name[name.size() - 1] = 's';
		result.emplace(name);
	};
	for (const auto &[key, value] : _draftsMap) {
		push(value);
	}
	for (const auto &[key, value] : _draftCursorsMap) {
		push(value);
	}
	for (const auto &value : keys) {
		push(value);
	}
	return result;
}

Account::ReadMapResult Account::readMapWith(
		MTP::AuthKeyPtr localKey,
		const QByteArray &legacyPasscode) {
	auto ms = crl::now();

	FileReadDescriptor mapData;
	if (!ReadFile(mapData, qsl("map"), _basePath)) {
		return ReadMapResult::Failed;
	}
	LOG(("App Info: reading map..."));

	QByteArray legacySalt, legacyKeyEncrypted, mapEncrypted;
	mapData.stream >> legacySalt >> legacyKeyEncrypted >> mapEncrypted;
	if (!CheckStreamStatus(mapData.stream)) {
		return ReadMapResult::Failed;
	}
	if (!localKey) {
		if (legacySalt.size() != LocalEncryptSaltSize) {
			LOG(("App Error: bad salt in map file, size: %1").arg(legacySalt.size()));
			return ReadMapResult::Failed;
		}
		auto legacyPasscodeKey = CreateLegacyLocalKey(legacyPasscode, legacySalt);

		EncryptedDescriptor keyData;
		if (!DecryptLocal(keyData, legacyKeyEncrypted, legacyPasscodeKey)) {
			LOG(("App Info: could not decrypt pass-protected key from map file, maybe bad password..."));
			return ReadMapResult::IncorrectPasscode;
		}
		auto key = Serialize::read<MTP::AuthKey::Data>(keyData.stream);
		if (keyData.stream.status() != QDataStream::Ok || !keyData.stream.atEnd()) {
			LOG(("App Error: could not read pass-protected key from map file"));
			return ReadMapResult::Failed;
		}
		localKey = std::make_shared<MTP::AuthKey>(key);
	}

	EncryptedDescriptor map;
	if (!DecryptLocal(map, mapEncrypted, localKey)) {
		LOG(("App Error: could not decrypt map."));
		return ReadMapResult::Failed;
	}
	LOG(("App Info: reading encrypted map..."));

	QByteArray selfSerialized;
	base::flat_map<PeerId, FileKey> draftsMap;
	base::flat_map<PeerId, FileKey> draftCursorsMap;
	base::flat_map<PeerId, bool> draftsNotReadMap;
	quint64 locationsKey = 0, reportSpamStatusesKey = 0, trustedBotsKey = 0;
	quint64 recentStickersKeyOld = 0;
	quint64 installedStickersKey = 0, featuredStickersKey = 0, recentStickersKey = 0, favedStickersKey = 0, archivedStickersKey = 0;
	quint64 savedGifsKey = 0;
	quint64 legacyBackgroundKeyDay = 0, legacyBackgroundKeyNight = 0;
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
				draftsMap.emplace(p, key);
				draftsNotReadMap.emplace(p, true);
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
				draftCursorsMap.emplace(p, key);
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
			ClearKey(reportSpamStatusesKey, _basePath);
		} break;
		case lskTrustedBots: {
			map.stream >> trustedBotsKey;
		} break;
		case lskRecentStickersOld: {
			map.stream >> recentStickersKeyOld;
		} break;
		case lskBackgroundOldOld: {
			map.stream >> (Window::Theme::IsNightMode()
				? legacyBackgroundKeyNight
				: legacyBackgroundKeyDay);
		} break;
		case lskBackgroundOld: {
			map.stream >> legacyBackgroundKeyDay >> legacyBackgroundKeyNight;
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
			return ReadMapResult::Failed;
		}
		if (!CheckStreamStatus(map.stream)) {
			return ReadMapResult::Failed;
		}
	}

	_localKey = std::move(localKey);

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
	_legacyBackgroundKeyDay = legacyBackgroundKeyDay;
	_legacyBackgroundKeyNight = legacyBackgroundKeyNight;
	_settingsKey = userSettingsKey;
	_recentHashtagsAndBotsKey = recentHashtagsAndBotsKey;
	_exportSettingsKey = exportSettingsKey;
	_oldMapVersion = mapData.version;

	if (_oldMapVersion < AppVersion) {
		writeMapDelayed();
	} else {
		_mapChanged = false;
	}

	if (_locationsKey) {
		readLocations();
	}
	if (_legacyBackgroundKeyDay || _legacyBackgroundKeyNight) {
		Local::moveLegacyBackground(
			_basePath,
			_localKey,
			_legacyBackgroundKeyDay,
			_legacyBackgroundKeyNight);
	}

	auto stored = readSessionSettings();
	readMtpData();

	DEBUG_LOG(("selfSerialized set: %1").arg(selfSerialized.size()));
	_owner->setSessionFromStorage(
		std::move(stored),
		std::move(selfSerialized),
		_oldMapVersion);

	LOG(("Map read time: %1").arg(crl::now() - ms));

	return ReadMapResult::Success;
}

void Account::writeMapDelayed() {
	_mapChanged = true;
	_writeMapTimer.callOnce(kDelayedWriteTimeout);
}

void Account::writeMapQueued() {
	_mapChanged = true;
	crl::on_main(_owner, [=] {
		writeMap();
	});
}

void Account::writeMap() {
	Expects(_localKey != nullptr);

	_writeMapTimer.cancel();
	if (!_mapChanged) {
		return;
	}
	_mapChanged = false;

	if (!QDir().exists(_basePath)) {
		QDir().mkpath(_basePath);
	}

	FileWriteDescriptor map(u"map"_q, _basePath);
	map.writeData(QByteArray());
	map.writeData(QByteArray());

	uint32 mapSize = 0;
	const auto self = [&] {
		if (!_owner->sessionExists()) {
			DEBUG_LOG(("AuthSelf Warning: Session does not exist."));
			return QByteArray();
		}
		const auto self = _owner->session().user();
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
	if (!_draftsMap.empty()) mapSize += sizeof(quint32) * 2 + _draftsMap.size() * sizeof(quint64) * 2;
	if (!_draftCursorsMap.empty()) mapSize += sizeof(quint32) * 2 + _draftCursorsMap.size() * sizeof(quint64) * 2;
	if (_locationsKey) mapSize += sizeof(quint32) + sizeof(quint64);
	if (_trustedBotsKey) mapSize += sizeof(quint32) + sizeof(quint64);
	if (_recentStickersKeyOld) mapSize += sizeof(quint32) + sizeof(quint64);
	if (_installedStickersKey || _featuredStickersKey || _recentStickersKey || _archivedStickersKey) {
		mapSize += sizeof(quint32) + 4 * sizeof(quint64);
	}
	if (_favedStickersKey) mapSize += sizeof(quint32) + sizeof(quint64);
	if (_savedGifsKey) mapSize += sizeof(quint32) + sizeof(quint64);
	if (_settingsKey) mapSize += sizeof(quint32) + sizeof(quint64);
	if (_recentHashtagsAndBotsKey) mapSize += sizeof(quint32) + sizeof(quint64);
	if (_exportSettingsKey) mapSize += sizeof(quint32) + sizeof(quint64);

	EncryptedDescriptor mapData(mapSize);
	if (!self.isEmpty()) {
		mapData.stream << quint32(lskSelfSerialized) << self;
	}
	if (!_draftsMap.empty()) {
		mapData.stream << quint32(lskDraft) << quint32(_draftsMap.size());
		for (const auto &[key, value] : _draftsMap) {
			mapData.stream << quint64(value) << quint64(key);
		}
	}
	if (!_draftCursorsMap.empty()) {
		mapData.stream << quint32(lskDraftPosition) << quint32(_draftCursorsMap.size());
		for (const auto &[key, value] : _draftCursorsMap) {
			mapData.stream << quint64(value) << quint64(key);
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
	if (_settingsKey) {
		mapData.stream << quint32(lskUserSettings) << quint64(_settingsKey);
	}
	if (_recentHashtagsAndBotsKey) {
		mapData.stream << quint32(lskRecentHashtagsAndBots) << quint64(_recentHashtagsAndBotsKey);
	}
	if (_exportSettingsKey) {
		mapData.stream << quint32(lskExportSettings) << quint64(_exportSettingsKey);
	}
	map.writeEncrypted(mapData, _localKey);

	_mapChanged = false;
}

void Account::reset() {
	auto names = collectGoodNames();
	_draftsMap.clear();
	_draftCursorsMap.clear();
	_draftsNotReadMap.clear();
	_locationsKey = _trustedBotsKey = 0;
	_recentStickersKeyOld = 0;
	_installedStickersKey = 0;
	_featuredStickersKey = 0;
	_recentStickersKey = 0;
	_favedStickersKey = 0;
	_archivedStickersKey = 0;
	_savedGifsKey = 0;
	_legacyBackgroundKeyDay = _legacyBackgroundKeyNight = 0;
	_settingsKey = _recentHashtagsAndBotsKey = _exportSettingsKey = 0;
	_oldMapVersion = 0;
	_fileLocations.clear();
	_fileLocationPairs.clear();
	_fileLocationAliases.clear();
	_cacheTotalSizeLimit = Database::Settings().totalSizeLimit;
	_cacheTotalTimeLimit = Database::Settings().totalTimeLimit;
	_cacheBigFileTotalSizeLimit = Database::Settings().totalSizeLimit;
	_cacheBigFileTotalTimeLimit = Database::Settings().totalTimeLimit;
	_mapChanged = true;
	writeMap();
	writeMtpData();

	crl::async([base = _basePath, temp = _tempPath, names = std::move(names)] {
		for (const auto &name : names) {
			if (!name.endsWith(qstr("map0"))
				&& !name.endsWith(qstr("map1"))
				&& !name.endsWith(qstr("maps"))
				&& !name.endsWith(qstr("configs"))) {
				QFile::remove(base + name);
			}
		}
		QDir(LegacyTempDirectory()).removeRecursively();
		QDir(temp).removeRecursively();
	});
}

void Account::writeLocations() {
	_writeLocationsTimer.cancel();
	if (!_locationsChanged) {
		return;
	}
	_locationsChanged = false;

	if (_fileLocations.isEmpty()) {
		if (_locationsKey) {
			ClearKey(_locationsKey, _basePath);
			_locationsKey = 0;
			writeMapDelayed();
		}
	} else {
		if (!_locationsKey) {
			_locationsKey = GenerateKey(_basePath);
			writeMapQueued();
		}
		quint32 size = 0;
		for (auto i = _fileLocations.cbegin(), e = _fileLocations.cend(); i != e; ++i) {
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
		for (auto i = _fileLocationAliases.cbegin(), e = _fileLocationAliases.cend(); i != e; ++i) {
			// alias + location
			size += sizeof(quint64) * 2 + sizeof(quint64) * 2;
		}

		EncryptedDescriptor data(size);
		auto legacyTypeField = 0;
		for (auto i = _fileLocations.cbegin(); i != _fileLocations.cend(); ++i) {
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
		for (auto i = _fileLocationAliases.cbegin(), e = _fileLocationAliases.cend(); i != e; ++i) {
			data.stream << quint64(i.key().first) << quint64(i.key().second) << quint64(i.value().first) << quint64(i.value().second);
		}

		FileWriteDescriptor file(_locationsKey, _basePath);
		file.writeEncrypted(data, _localKey);
	}
}

void Account::writeLocationsQueued() {
	_locationsChanged = true;
	crl::on_main(_owner, [=] {
		writeLocations();
	});
}

void Account::writeLocationsDelayed() {
	_locationsChanged = true;
	_writeLocationsTimer.callOnce(kDelayedWriteTimeout);
}

void Account::readLocations() {
	FileReadDescriptor locations;
	if (!ReadEncryptedFile(locations, _locationsKey, _basePath, _localKey)) {
		ClearKey(_locationsKey, _basePath);
		_locationsKey = 0;
		writeMapDelayed();
		return;
	}

	bool endMarkFound = false;
	while (!locations.stream.atEnd()) {
		quint64 first, second;
		QByteArray bookmark;
		Core::FileLocation loc;
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
			_fileLocationPairs.insert(loc.fname, { key, loc });
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
				ClearKey(key, _basePath);
			}
		}
	}
}

void Account::writeSessionSettings() {
	writeSessionSettings(nullptr);
}

void Account::writeSessionSettings(Main::SessionSettings *stored) {
	if (_readingUserSettings) {
		LOG(("App Error: attempt to write settings while reading them!"));
		return;
	}
	LOG(("App Info: writing encrypted user settings..."));

	if (!_settingsKey) {
		_settingsKey = GenerateKey(_basePath);
		writeMapQueued();
	}

	auto recentEmojiPreloadData = cRecentEmojiPreload();
	if (recentEmojiPreloadData.isEmpty()) {
		recentEmojiPreloadData.reserve(GetRecentEmoji().size());
		for (auto &item : GetRecentEmoji()) {
			recentEmojiPreloadData.push_back(qMakePair(item.first->id(), item.second));
		}
	}
	auto userDataInstance = stored
		? stored
		: _owner->getSessionSettings();
	auto userData = userDataInstance
		? userDataInstance->serialize()
		: QByteArray();

	auto recentStickers = cRecentStickersPreload();
	if (recentStickers.isEmpty() && _owner->sessionExists()) {
		const auto &stickers = _owner->session().data().stickers();
		recentStickers.reserve(stickers.getRecentPack().size());
		for (const auto &pair : std::as_const(stickers.getRecentPack())) {
			recentStickers.push_back(qMakePair(pair.first->id, pair.second));
		}
	}

	uint32 size = 24 * (sizeof(quint32) + sizeof(qint32));
	size += sizeof(quint32);

	size += sizeof(quint32) + sizeof(qint32);
	for (auto &item : recentEmojiPreloadData) {
		size += Serialize::stringSize(item.first) + sizeof(item.second);
	}

	size += sizeof(quint32) + sizeof(qint32) + cEmojiVariants().size() * (sizeof(uint32) + sizeof(uint64));
	size += sizeof(quint32) + sizeof(qint32) + recentStickers.size() * (sizeof(uint64) + sizeof(ushort));
	size += sizeof(quint32) + 3 * sizeof(qint32);
	size += sizeof(quint32) + 2 * sizeof(qint32);
	size += sizeof(quint32) + sizeof(qint64) + sizeof(qint32);
	if (!userData.isEmpty()) {
		size += sizeof(quint32) + Serialize::bytearraySize(userData);
	}

	EncryptedDescriptor data(size);
	data.stream << quint32(dbiUseExternalVideoPlayer) << qint32(cUseExternalVideoPlayer());
	data.stream << quint32(dbiCacheSettings) << qint64(_cacheTotalSizeLimit) << qint32(_cacheTotalTimeLimit) << qint64(_cacheBigFileTotalSizeLimit) << qint32(_cacheBigFileTotalTimeLimit);
	if (!userData.isEmpty()) {
		data.stream << quint32(dbiSessionSettings) << userData;
	}
	data.stream << quint32(dbiRecentEmoji) << recentEmojiPreloadData;
	data.stream << quint32(dbiEmojiVariants) << cEmojiVariants();
	data.stream << quint32(dbiRecentStickers) << recentStickers;

	FileWriteDescriptor file(_settingsKey, _basePath);
	file.writeEncrypted(data, _localKey);
}

ReadSettingsContext Account::prepareReadSettingsContext() const {
	return ReadSettingsContext{
		.legacyHasCustomDayBackground = (_legacyBackgroundKeyDay != 0)
	};
}

std::unique_ptr<Main::SessionSettings> Account::readSessionSettings() {
	ReadSettingsContext context;
	FileReadDescriptor userSettings;
	if (!ReadEncryptedFile(userSettings, _settingsKey, _basePath, _localKey)) {
		LOG(("App Info: could not read encrypted user settings..."));

		Local::readOldUserSettings(true, context);
		auto result = applyReadContext(std::move(context));

		writeSessionSettings(result.get());

		return result;
	}

	LOG(("App Info: reading encrypted user settings..."));
	_readingUserSettings = true;
	while (!userSettings.stream.atEnd()) {
		quint32 blockId;
		userSettings.stream >> blockId;
		if (!CheckStreamStatus(userSettings.stream)) {
			_readingUserSettings = false;
			writeSessionSettings();
			return nullptr;
		}

		if (!ReadSetting(blockId, userSettings.stream, userSettings.version, context)) {
			_readingUserSettings = false;
			writeSessionSettings();
			return nullptr;
		}
	}
	_readingUserSettings = false;
	LOG(("App Info: encrypted user settings read."));

	auto result = applyReadContext(std::move(context));
	if (context.legacyRead) {
		writeSessionSettings(result.get());
	}
	return result;
}

std::unique_ptr<Main::SessionSettings> Account::applyReadContext(
		ReadSettingsContext &&context) {
	ApplyReadFallbackConfig(context);

	if (context.cacheTotalSizeLimit) {
		_cacheTotalSizeLimit = context.cacheTotalSizeLimit;
		_cacheTotalTimeLimit = context.cacheTotalTimeLimit;
		_cacheBigFileTotalSizeLimit = context.cacheBigFileTotalSizeLimit;
		_cacheBigFileTotalTimeLimit = context.cacheBigFileTotalTimeLimit;

		const auto &normal = Database::Settings();
		Assert(_cacheTotalSizeLimit > normal.maxDataSize);
		Assert(_cacheBigFileTotalSizeLimit > normal.maxDataSize);
	}

	if (!context.mtpAuthorization.isEmpty()) {
		_owner->setMtpAuthorization(context.mtpAuthorization);
	} else {
		for (auto &key : context.mtpLegacyKeys) {
			_owner->setLegacyMtpKey(std::move(key));
		}
		if (context.mtpLegacyMainDcId) {
			_owner->setMtpMainDcId(context.mtpLegacyMainDcId);
			_owner->setSessionUserId(context.mtpLegacyUserId);
		}
	}

	if (context.tileRead) {
		Window::Theme::Background()->setTileDayValue(context.tileDay);
		Window::Theme::Background()->setTileNightValue(context.tileNight);
	}

	return std::move(context.sessionSettingsStorage);
}

void Account::writeMtpData() {
	Expects(_localKey != nullptr);

	const auto serialized = _owner->serializeMtpAuthorization();
	const auto size = sizeof(quint32) + Serialize::bytearraySize(serialized);

	FileWriteDescriptor mtp(ToFilePart(_dataNameKey), BaseGlobalPath());
	EncryptedDescriptor data(size);
	data.stream << quint32(dbiMtpAuthorization) << serialized;
	mtp.writeEncrypted(data, _localKey);
}

void Account::readMtpData() {
	auto context = prepareReadSettingsContext();

	FileReadDescriptor mtp;
	if (!ReadEncryptedFile(mtp, ToFilePart(_dataNameKey), BaseGlobalPath(), _localKey)) {
		if (_localKey) {
			Local::readOldMtpData(true, context);
			applyReadContext(std::move(context));
			writeMtpData();
		}
		return;
	}

	LOG(("App Info: reading encrypted mtp data..."));
	while (!mtp.stream.atEnd()) {
		quint32 blockId;
		mtp.stream >> blockId;
		if (!CheckStreamStatus(mtp.stream)) {
			return writeMtpData();
		}

		if (!ReadSetting(blockId, mtp.stream, mtp.version, context)) {
			return writeMtpData();
		}
	}
	applyReadContext(std::move(context));
}

void Account::writeMtpConfig() {
	Expects(_localKey != nullptr);

	const auto serialized = _owner->mtp().config().serialize();
	const auto size = Serialize::bytearraySize(serialized);

	FileWriteDescriptor file(u"config"_q, _basePath);
	EncryptedDescriptor data(size);
	data.stream << serialized;
	file.writeEncrypted(data, _localKey);
}

std::unique_ptr<MTP::Config> Account::readMtpConfig() {
	Expects(_localKey != nullptr);

	FileReadDescriptor file;
	if (!ReadEncryptedFile(file, "config", _basePath, _localKey)) {
		return nullptr;
	}

	LOG(("App Info: reading encrypted mtp config..."));
	auto serialized = QByteArray();
	file.stream >> serialized;
	if (!CheckStreamStatus(file.stream)) {
		return nullptr;
	}
	return MTP::Config::FromSerialized(serialized);
}

void Account::writeDrafts(not_null<History*> history) {
	Storage::MessageDraft storedLocalDraft, storedEditDraft;
	MessageCursor localCursor, editCursor;
	if (const auto localDraft = history->localDraft()) {
		if (_owner->session().supportMode()
			|| !Data::draftsAreEqual(localDraft, history->cloudDraft())) {
			storedLocalDraft = Storage::MessageDraft{
				localDraft->msgId,
				localDraft->textWithTags,
				localDraft->previewCancelled
			};
			localCursor = localDraft->cursor;
		}
	}
	if (const auto editDraft = history->editDraft()) {
		storedEditDraft = Storage::MessageDraft{
			editDraft->msgId,
			editDraft->textWithTags,
			editDraft->previewCancelled
		};
		editCursor = editDraft->cursor;
	}
	writeDrafts(
		history->peer->id,
		storedLocalDraft,
		storedEditDraft);
	writeDraftCursors(history->peer->id, localCursor, editCursor);
}

void Account::writeDrafts(
		const PeerId &peer,
		const MessageDraft &localDraft,
		const MessageDraft &editDraft) {
	if (localDraft.msgId <= 0 && localDraft.textWithTags.text.isEmpty() && editDraft.msgId <= 0) {
		auto i = _draftsMap.find(peer);
		if (i != _draftsMap.cend()) {
			ClearKey(i->second, _basePath);
			_draftsMap.erase(i);
			writeMapDelayed();
		}

		_draftsNotReadMap.remove(peer);
	} else {
		auto i = _draftsMap.find(peer);
		if (i == _draftsMap.cend()) {
			i = _draftsMap.emplace(peer, GenerateKey(_basePath)).first;
			writeMapQueued();
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

		FileWriteDescriptor file(i->second, _basePath);
		file.writeEncrypted(data, _localKey);

		_draftsNotReadMap.remove(peer);
	}
}

void Account::clearDraftCursors(const PeerId &peer) {
	const auto i = _draftCursorsMap.find(peer);
	if (i != _draftCursorsMap.cend()) {
		ClearKey(i->second, _basePath);
		_draftCursorsMap.erase(i);
		writeMapDelayed();
	}
}

void Account::readDraftCursors(
		const PeerId &peer,
		MessageCursor &localCursor,
		MessageCursor &editCursor) {
	const auto j = _draftCursorsMap.find(peer);
	if (j == _draftCursorsMap.cend()) {
		return;
	}

	FileReadDescriptor draft;
	if (!ReadEncryptedFile(draft, j->second, _basePath, _localKey)) {
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

void Account::readDraftsWithCursors(not_null<History*> history) {
	PeerId peer = history->peer->id;
	if (!_draftsNotReadMap.remove(peer)) {
		clearDraftCursors(peer);
		return;
	}

	const auto j = _draftsMap.find(peer);
	if (j == _draftsMap.cend()) {
		clearDraftCursors(peer);
		return;
	}
	FileReadDescriptor draft;
	if (!ReadEncryptedFile(draft, j->second, _basePath, _localKey)) {
		ClearKey(j->second, _basePath);
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
		ClearKey(j->second, _basePath);
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
	readDraftCursors(peer, msgCursor, editCursor);

	if (!history->localDraft()) {
		if (msgData.text.isEmpty() && !msgReplyTo) {
			history->clearLocalDraft();
		} else {
			history->setLocalDraft(std::make_unique<Data::Draft>(
				msgData,
				msgReplyTo,
				msgCursor,
				msgPreviewCancelled));
		}
	}
	if (!editMsgId) {
		history->clearEditDraft();
	} else {
		history->setEditDraft(std::make_unique<Data::Draft>(
			editData,
			editMsgId,
			editCursor,
			editPreviewCancelled));
	}
}

void Account::writeDraftCursors(
		const PeerId &peer,
		const MessageCursor &msgCursor,
		const MessageCursor &editCursor) {
	if (msgCursor == MessageCursor() && editCursor == MessageCursor()) {
		clearDraftCursors(peer);
	} else {
		auto i = _draftCursorsMap.find(peer);
		if (i == _draftCursorsMap.cend()) {
			i = _draftCursorsMap.emplace(peer, GenerateKey(_basePath)).first;
			writeMapQueued();
		}

		EncryptedDescriptor data(sizeof(quint64) + sizeof(qint32) * 3);
		data.stream
			<< quint64(peer)
			<< qint32(msgCursor.position)
			<< qint32(msgCursor.anchor)
			<< qint32(msgCursor.scroll);
		data.stream
			<< qint32(editCursor.position)
			<< qint32(editCursor.anchor)
			<< qint32(editCursor.scroll);

		FileWriteDescriptor file(i->second, _basePath);
		file.writeEncrypted(data, _localKey);
	}
}

bool Account::hasDraftCursors(const PeerId &peer) {
	return _draftCursorsMap.contains(peer);
}

bool Account::hasDraft(const PeerId &peer) {
	return _draftsMap.contains(peer);
}

void Account::writeFileLocation(MediaKey location, const Core::FileLocation &local) {
	if (local.fname.isEmpty()) {
		return;
	}
	if (!local.inMediaCache()) {
		const auto aliasIt = _fileLocationAliases.constFind(location);
		if (aliasIt != _fileLocationAliases.cend()) {
			location = aliasIt.value();
		}

		const auto i = _fileLocationPairs.find(local.fname);
		if (i != _fileLocationPairs.cend()) {
			if (i.value().second == local) {
				if (i.value().first != location) {
					_fileLocationAliases.insert(location, i.value().first);
					writeLocationsQueued();
				}
				return;
			}
			if (i.value().first != location) {
				for (auto j = _fileLocations.find(i.value().first), e = _fileLocations.end(); (j != e) && (j.key() == i.value().first); ++j) {
					if (j.value() == i.value().second) {
						_fileLocations.erase(j);
						break;
					}
				}
				_fileLocationPairs.erase(i);
			}
		}
		_fileLocationPairs.insert(local.fname, { location, local });
	} else {
		for (auto i = _fileLocations.find(location); (i != _fileLocations.end()) && (i.key() == location);) {
			if (i.value().inMediaCache() || i.value().check()) {
				return;
			}
			i = _fileLocations.erase(i);
		}
	}
	_fileLocations.insert(location, local);
	writeLocationsQueued();
}

void Account::removeFileLocation(MediaKey location) {
	auto i = _fileLocations.find(location);
	if (i == _fileLocations.end()) {
		return;
	}
	while (i != _fileLocations.end() && (i.key() == location)) {
		i = _fileLocations.erase(i);
	}
	writeLocationsQueued();
}

Core::FileLocation Account::readFileLocation(MediaKey location) {
	const auto aliasIt = _fileLocationAliases.constFind(location);
	if (aliasIt != _fileLocationAliases.cend()) {
		location = aliasIt.value();
	}

	for (auto i = _fileLocations.find(location); (i != _fileLocations.end()) && (i.key() == location);) {
		if (!i.value().inMediaCache() && !i.value().check()) {
			_fileLocationPairs.remove(i.value().fname);
			i = _fileLocations.erase(i);
			writeLocationsDelayed();
			continue;
		}
		return i.value();
	}
	return Core::FileLocation();
}

EncryptionKey Account::cacheKey() const {
	Expects(_localKey != nullptr);

	return EncryptionKey(bytes::make_vector(_localKey->data()));
}

EncryptionKey Account::cacheBigFileKey() const {
	return cacheKey();
}

QString Account::cachePath() const {
	Expects(!_databasePath.isEmpty());

	return _databasePath + "cache";
}

Cache::Database::Settings Account::cacheSettings() const {
	auto result = Cache::Database::Settings();
	result.clearOnWrongKey = true;
	result.totalSizeLimit = _cacheTotalSizeLimit;
	result.totalTimeLimit = _cacheTotalTimeLimit;
	result.maxDataSize = kMaxFileInMemory;
	return result;
}

void Account::updateCacheSettings(
		Cache::Database::SettingsUpdate &update,
		Cache::Database::SettingsUpdate &updateBig) {
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
	writeSessionSettings();
}

QString Account::cacheBigFilePath() const {
	Expects(!_databasePath.isEmpty());

	return _databasePath + "media_cache";
}

Cache::Database::Settings Account::cacheBigFileSettings() const {
	auto result = Cache::Database::Settings();
	result.clearOnWrongKey = true;
	result.totalSizeLimit = _cacheBigFileTotalSizeLimit;
	result.totalTimeLimit = _cacheBigFileTotalTimeLimit;
	result.maxDataSize = kMaxFileInMemory;
	return result;
}

void Account::writeStickerSet(
		QDataStream &stream,
		const Data::StickersSet &set) {
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
		Serialize::writeImageLocation(stream, set.thumbnailLocation());
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

// CheckSet is a functor on Data::StickersSet, which returns a StickerSetCheckResult.
template <typename CheckSet>
void Account::writeStickerSets(
		FileKey &stickersKey,
		CheckSet checkSet,
		const Data::StickersSetsOrder &order) {
	const auto &sets = _owner->session().data().stickers().sets();
	if (sets.empty()) {
		if (stickersKey) {
			ClearKey(stickersKey, _basePath);
			stickersKey = 0;
			writeMapDelayed();
		}
		return;
	}

	// versionTag + version + count
	quint32 size = sizeof(quint32) + sizeof(qint32) + sizeof(qint32);

	int32 setsCount = 0;
	for (const auto &[id, set] : sets) {
		const auto raw = set.get();
		auto result = checkSet(*raw);
		if (result == StickerSetCheckResult::Abort) {
			return;
		} else if (result == StickerSetCheckResult::Skip) {
			continue;
		}

		// id + access + title + shortName + stickersCount + hash + flags + installDate
		size += sizeof(quint64) * 2
			+ Serialize::stringSize(raw->title)
			+ Serialize::stringSize(raw->shortName)
			+ sizeof(qint32) * 4
			+ Serialize::imageLocationSize(raw->thumbnailLocation());
		if (raw->flags & MTPDstickerSet_ClientFlag::f_not_loaded) {
			continue;
		}

		for (const auto sticker : raw->stickers) {
			size += Serialize::Document::sizeInStream(sticker);
		}

		size += sizeof(qint32); // datesCount
		if (!raw->dates.empty()) {
			Assert(raw->stickers.size() == raw->dates.size());
			size += raw->dates.size() * sizeof(qint32);
		}

		size += sizeof(qint32); // emojiCount
		for (auto j = raw->emoji.cbegin(), e = raw->emoji.cend(); j != e; ++j) {
			size += Serialize::stringSize(j.key()->id()) + sizeof(qint32) + (j->size() * sizeof(quint64));
		}

		++setsCount;
	}
	if (!setsCount && order.isEmpty()) {
		if (stickersKey) {
			ClearKey(stickersKey, _basePath);
			stickersKey = 0;
			writeMapDelayed();
		}
		return;
	}
	size += sizeof(qint32) + (order.size() * sizeof(quint64));

	if (!stickersKey) {
		stickersKey = GenerateKey(_basePath);
		writeMapQueued();
	}
	EncryptedDescriptor data(size);
	data.stream
		<< quint32(kStickersVersionTag)
		<< qint32(kStickersSerializeVersion)
		<< qint32(setsCount);
	for (const auto &[id, set] : sets) {
		auto result = checkSet(*set);
		if (result == StickerSetCheckResult::Abort) {
			return;
		} else if (result == StickerSetCheckResult::Skip) {
			continue;
		}
		writeStickerSet(data.stream, *set);
	}
	data.stream << order;

	FileWriteDescriptor file(stickersKey, _basePath);
	file.writeEncrypted(data, _localKey);
}

void Account::readStickerSets(
		FileKey &stickersKey,
		Data::StickersSetsOrder *outOrder,
		MTPDstickerSet::Flags readingFlags) {
	FileReadDescriptor stickers;
	if (!ReadEncryptedFile(stickers, stickersKey, _basePath, _localKey)) {
		ClearKey(stickersKey, _basePath);
		stickersKey = 0;
		writeMapDelayed();
		return;
	}

	const auto failed = [&] {
		ClearKey(stickersKey, _basePath);
		stickersKey = 0;
	};

	auto &sets = _owner->session().data().stickers().setsRef();
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
	if (!CheckStreamStatus(stickers.stream)
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
		ImageLocation setThumbnail;

		stickers.stream
			>> setId
			>> setAccess
			>> setTitle
			>> setShortName
			>> scnt
			>> setHash
			>> setFlagsValue
			>> setInstallDate;
		const auto thumbnail = Serialize::readImageLocation(
			stickers.version,
			stickers.stream);
		if (!thumbnail || !CheckStreamStatus(stickers.stream)) {
			return failed();
		} else if (thumbnail->valid() && thumbnail->isLegacy()) {
			setThumbnail = thumbnail->convertToModern(
				LocationType::StickerSetThumb,
				setId,
				setAccess);
		} else {
			setThumbnail = *thumbnail;
		}

		setFlags = MTPDstickerSet::Flags::from_raw(setFlagsValue);
		if (setId == Data::Stickers::DefaultSetId) {
			setTitle = tr::lng_stickers_default_set(tr::now);
			setFlags |= MTPDstickerSet::Flag::f_official | MTPDstickerSet_ClientFlag::f_special;
		} else if (setId == Data::Stickers::CustomSetId) {
			setTitle = qsl("Custom stickers");
			setFlags |= MTPDstickerSet_ClientFlag::f_special;
		} else if (setId == Data::Stickers::CloudRecentSetId) {
			setTitle = tr::lng_recent_stickers(tr::now);
			setFlags |= MTPDstickerSet_ClientFlag::f_special;
		} else if (setId == Data::Stickers::FavedSetId) {
			setTitle = Lang::Hard::FavedSetTitle();
			setFlags |= MTPDstickerSet_ClientFlag::f_special;
		} else if (!setId) {
			continue;
		}

		auto it = sets.find(setId);
		if (it == sets.cend()) {
			// We will set this flags from order lists when reading those stickers.
			setFlags &= ~(MTPDstickerSet::Flag::f_installed_date | MTPDstickerSet_ClientFlag::f_featured);
			it = sets.emplace(setId, std::make_unique<Data::StickersSet>(
				&_owner->session().data(),
				setId,
				setAccess,
				setTitle,
				setShortName,
				0,
				setHash,
				MTPDstickerSet::Flags(setFlags),
				setInstallDate)).first;
			it->second->setThumbnail(
				ImageWithLocation{ .location = setThumbnail });
		}
		const auto set = it->second.get();
		auto inputSet = MTP_inputStickerSetID(MTP_long(set->id), MTP_long(set->access));
		const auto fillStickers = set->stickers.isEmpty();

		if (scnt < 0) { // disabled not loaded set
			if (!set->count || fillStickers) {
				set->count = -scnt;
			}
			continue;
		}

		if (fillStickers) {
			set->stickers.reserve(scnt);
			set->count = 0;
		}

		Serialize::Document::StickerSetInfo info(setId, setAccess, setShortName);
		base::flat_set<DocumentId> read;
		for (int32 j = 0; j < scnt; ++j) {
			auto document = Serialize::Document::readStickerFromStream(
				&_owner->session(),
				stickers.version,
				stickers.stream, info);
			if (!CheckStreamStatus(stickers.stream)) {
				return failed();
			} else if (!document
				|| !document->sticker()
				|| read.contains(document->id)) {
				continue;
			}
			read.emplace(document->id);
			if (fillStickers) {
				set->stickers.push_back(document);
				if (!(set->flags & MTPDstickerSet_ClientFlag::f_special)) {
					if (document->sticker()->set.type() != mtpc_inputStickerSetID) {
						document->sticker()->set = inputSet;
					}
				}
				++set->count;
			}
		}

		qint32 datesCount = 0;
		stickers.stream >> datesCount;
		if (datesCount > 0) {
			if (datesCount != scnt) {
				return failed();
			}
			const auto fillDates = (set->id == Data::Stickers::CloudRecentSetId)
				&& (set->stickers.size() == datesCount);
			if (fillDates) {
				set->dates.clear();
				set->dates.reserve(datesCount);
			}
			for (auto i = 0; i != datesCount; ++i) {
				qint32 date = 0;
				stickers.stream >> date;
				if (fillDates) {
					set->dates.push_back(TimeId(date));
				}
			}
		}

		qint32 emojiCount = 0;
		stickers.stream >> emojiCount;
		if (!CheckStreamStatus(stickers.stream) || emojiCount < 0) {
			return failed();
		}
		for (int32 j = 0; j < emojiCount; ++j) {
			QString emojiString;
			qint32 stickersCount;
			stickers.stream >> emojiString >> stickersCount;
			Data::StickersPack pack;
			pack.reserve(stickersCount);
			for (int32 k = 0; k < stickersCount; ++k) {
				quint64 id;
				stickers.stream >> id;
				const auto doc = _owner->session().data().document(id);
				if (!doc->sticker()) continue;

				pack.push_back(doc);
			}
			if (fillStickers) {
				if (auto emoji = Ui::Emoji::Find(emojiString)) {
					emoji = emoji->original();
					set->emoji.insert(emoji, pack);
				}
			}
		}
	}

	// Read orders of installed and featured stickers.
	if (outOrder) {
		auto outOrderCount = quint32();
		stickers.stream >> outOrderCount;
		if (!CheckStreamStatus(stickers.stream) || outOrderCount > 1000) {
			return failed();
		}
		outOrder->reserve(outOrderCount);
		for (auto i = 0; i != outOrderCount; ++i) {
			auto value = uint64();
			stickers.stream >> value;
			if (!CheckStreamStatus(stickers.stream)) {
				outOrder->clear();
				return failed();
			}
			outOrder->push_back(value);
		}
	}
	if (!CheckStreamStatus(stickers.stream)) {
		return failed();
	}

	// Set flags that we dropped above from the order.
	if (readingFlags && outOrder) {
		for (const auto setId : std::as_const(*outOrder)) {
			auto it = sets.find(setId);
			if (it != sets.cend()) {
				const auto set = it->second.get();
				set->flags |= readingFlags;
				if ((readingFlags == MTPDstickerSet::Flag::f_installed_date)
					&& !set->installDate) {
					set->installDate = kDefaultStickerInstallDate;
				}
			}
		}
	}
}

void Account::writeInstalledStickers() {
	writeStickerSets(_installedStickersKey, [](const Data::StickersSet &set) {
		if (set.id == Data::Stickers::CloudRecentSetId || set.id == Data::Stickers::FavedSetId) { // separate files for them
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
	}, _owner->session().data().stickers().setsOrder());
}

void Account::writeFeaturedStickers() {
	writeStickerSets(_featuredStickersKey, [](const Data::StickersSet &set) {
		if (set.id == Data::Stickers::CloudRecentSetId
			|| set.id == Data::Stickers::FavedSetId) { // separate files for them
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
	}, _owner->session().data().stickers().featuredSetsOrder());
}

void Account::writeRecentStickers() {
	writeStickerSets(_recentStickersKey, [](const Data::StickersSet &set) {
		if (set.id != Data::Stickers::CloudRecentSetId || set.stickers.isEmpty()) {
			return StickerSetCheckResult::Skip;
		}
		return StickerSetCheckResult::Write;
	}, Data::StickersSetsOrder());
}

void Account::writeFavedStickers() {
	writeStickerSets(_favedStickersKey, [](const Data::StickersSet &set) {
		if (set.id != Data::Stickers::FavedSetId || set.stickers.isEmpty()) {
			return StickerSetCheckResult::Skip;
		}
		return StickerSetCheckResult::Write;
	}, Data::StickersSetsOrder());
}

void Account::writeArchivedStickers() {
	writeStickerSets(_archivedStickersKey, [](const Data::StickersSet &set) {
		if (!(set.flags & MTPDstickerSet::Flag::f_archived) || set.stickers.isEmpty()) {
			return StickerSetCheckResult::Skip;
		}
		return StickerSetCheckResult::Write;
	}, _owner->session().data().stickers().archivedSetsOrder());
}

void Account::importOldRecentStickers() {
	if (!_recentStickersKeyOld) {
		return;
	}

	FileReadDescriptor stickers;
	if (!ReadEncryptedFile(stickers, _recentStickersKeyOld, _basePath, _localKey)) {
		ClearKey(_recentStickersKeyOld, _basePath);
		_recentStickersKeyOld = 0;
		writeMapDelayed();
		return;
	}

	auto &sets = _owner->session().data().stickers().setsRef();
	sets.clear();

	auto &order = _owner->session().data().stickers().setsOrderRef();
	order.clear();

	auto &recent = cRefRecentStickers();
	recent.clear();

	const auto def = sets.emplace(
		Data::Stickers::DefaultSetId,
		std::make_unique<Data::StickersSet>(
			&_owner->session().data(),
			Data::Stickers::DefaultSetId,
			uint64(0),
			tr::lng_stickers_default_set(tr::now),
			QString(),
			0, // count
			0, // hash
			(MTPDstickerSet::Flag::f_official
				| MTPDstickerSet::Flag::f_installed_date
				| MTPDstickerSet_ClientFlag::f_special),
			kDefaultStickerInstallDate)).first->second.get();
	const auto custom = sets.emplace(
		Data::Stickers::CustomSetId,
		std::make_unique<Data::StickersSet>(
			&_owner->session().data(),
			Data::Stickers::CustomSetId,
			uint64(0),
			qsl("Custom stickers"),
			QString(),
			0, // count
			0, // hash
			(MTPDstickerSet::Flag::f_installed_date
				| MTPDstickerSet_ClientFlag::f_special),
			kDefaultStickerInstallDate)).first->second.get();

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

		const auto doc = _owner->session().data().document(
			id,
			access,
			QByteArray(),
			date,
			attributes,
			mime,
			QByteArray(),
			ImageWithLocation(),
			ImageWithLocation(),
			dc,
			size);
		if (!doc->sticker()) {
			continue;
		}

		if (value > 0) {
			def->stickers.push_back(doc);
			++def->count;
		} else {
			custom->stickers.push_back(doc);
			++custom->count;
		}
		if (qAbs(value) > 1
			&& (recent.size()
				< _owner->session().serverConfig().stickersRecentLimit)) {
			recent.push_back(qMakePair(doc, qAbs(value)));
		}
	}
	if (def->stickers.isEmpty()) {
		sets.remove(Data::Stickers::DefaultSetId);
	} else {
		order.push_front(Data::Stickers::DefaultSetId);
	}
	if (custom->stickers.isEmpty()) {
		sets.remove(Data::Stickers::CustomSetId);
	}

	writeInstalledStickers();
	writeSessionSettings();

	ClearKey(_recentStickersKeyOld, _basePath);
	_recentStickersKeyOld = 0;
	writeMapDelayed();
}

void Account::readInstalledStickers() {
	if (!_installedStickersKey) {
		return importOldRecentStickers();
	}

	_owner->session().data().stickers().setsRef().clear();
	readStickerSets(
		_installedStickersKey,
		&_owner->session().data().stickers().setsOrderRef(),
		MTPDstickerSet::Flag::f_installed_date);
}

void Account::readFeaturedStickers() {
	readStickerSets(
		_featuredStickersKey,
		&_owner->session().data().stickers().featuredSetsOrderRef(),
		MTPDstickerSet::Flags() | MTPDstickerSet_ClientFlag::f_featured);

	const auto &sets = _owner->session().data().stickers().sets();
	const auto &order = _owner->session().data().stickers().featuredSetsOrder();
	int unreadCount = 0;
	for (const auto setId : order) {
		auto it = sets.find(setId);
		if (it != sets.cend()
			&& (it->second->flags & MTPDstickerSet_ClientFlag::f_unread)) {
			++unreadCount;
		}
	}
	_owner->session().data().stickers().setFeaturedSetsUnreadCount(unreadCount);
}

void Account::readRecentStickers() {
	readStickerSets(_recentStickersKey);
}

void Account::readFavedStickers() {
	readStickerSets(_favedStickersKey);
}

void Account::readArchivedStickers() {
	static bool archivedStickersRead = false;
	if (!archivedStickersRead) {
		readStickerSets(_archivedStickersKey, &_owner->session().data().stickers().archivedSetsOrderRef());
		archivedStickersRead = true;
	}
}

void Account::writeSavedGifs() {
	auto &saved = _owner->session().data().stickers().savedGifs();
	if (saved.isEmpty()) {
		if (_savedGifsKey) {
			ClearKey(_savedGifsKey, _basePath);
			_savedGifsKey = 0;
			writeMapDelayed();
		}
	} else {
		quint32 size = sizeof(quint32); // count
		for_const (auto gif, saved) {
			size += Serialize::Document::sizeInStream(gif);
		}

		if (!_savedGifsKey) {
			_savedGifsKey = GenerateKey(_basePath);
			writeMapQueued();
		}
		EncryptedDescriptor data(size);
		data.stream << quint32(saved.size());
		for_const (auto gif, saved) {
			Serialize::Document::writeToStream(data.stream, gif);
		}
		FileWriteDescriptor file(_savedGifsKey, _basePath);
		file.writeEncrypted(data, _localKey);
	}
}

void Account::readSavedGifs() {
	if (!_savedGifsKey) return;

	FileReadDescriptor gifs;
	if (!ReadEncryptedFile(gifs, _savedGifsKey, _basePath, _localKey)) {
		ClearKey(_savedGifsKey, _basePath);
		_savedGifsKey = 0;
		writeMapDelayed();
		return;
	}

	auto &saved = _owner->session().data().stickers().savedGifsRef();
	const auto failed = [&] {
		ClearKey(_savedGifsKey, _basePath);
		_savedGifsKey = 0;
		saved.clear();
	};
	saved.clear();

	quint32 cnt;
	gifs.stream >> cnt;
	saved.reserve(cnt);
	OrderedSet<DocumentId> read;
	for (uint32 i = 0; i < cnt; ++i) {
		auto document = Serialize::Document::readFromStream(
			&_owner->session(),
			gifs.version,
			gifs.stream);
		if (!CheckStreamStatus(gifs.stream)) {
			return failed();
		} else if (!document || !document->isGifv()) {
			continue;
		}

		if (read.contains(document->id)) continue;
		read.insert(document->id);

		saved.push_back(document);
	}
}

void Account::writeRecentHashtagsAndBots() {
	const auto &write = cRecentWriteHashtags();
	const auto &search = cRecentSearchHashtags();
	const auto &bots = cRecentInlineBots();

	if (write.isEmpty() && search.isEmpty() && bots.isEmpty()) {
		readRecentHashtagsAndBots();
	}
	if (write.isEmpty() && search.isEmpty() && bots.isEmpty()) {
		if (_recentHashtagsAndBotsKey) {
			ClearKey(_recentHashtagsAndBotsKey, _basePath);
			_recentHashtagsAndBotsKey = 0;
			writeMapDelayed();
		}
		return;
	}
	if (!_recentHashtagsAndBotsKey) {
		_recentHashtagsAndBotsKey = GenerateKey(_basePath);
		writeMapQueued();
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
	FileWriteDescriptor file(_recentHashtagsAndBotsKey, _basePath);
	file.writeEncrypted(data, _localKey);
}

void Account::readRecentHashtagsAndBots() {
	if (_recentHashtagsAndBotsWereRead) return;
	_recentHashtagsAndBotsWereRead = true;

	if (!_recentHashtagsAndBotsKey) return;

	FileReadDescriptor hashtags;
	if (!ReadEncryptedFile(hashtags, _recentHashtagsAndBotsKey, _basePath, _localKey)) {
		ClearKey(_recentHashtagsAndBotsKey, _basePath);
		_recentHashtagsAndBotsKey = 0;
		writeMapDelayed();
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
					&_owner->session(),
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

std::optional<RecentHashtagPack> Account::saveRecentHashtags(
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
			readRecentHashtagsAndBots();
			recent = getPack();
		}
		found = true;
		Local::incrementRecentHashtag(recent, tag);
	}
	return found ? base::make_optional(recent) : std::nullopt;
}

void Account::saveRecentSentHashtags(const QString &text) {
	const auto result = saveRecentHashtags(
		[] { return cRecentWriteHashtags(); },
		text);
	if (result) {
		cSetRecentWriteHashtags(*result);
		writeRecentHashtagsAndBots();
	}
}

void Account::saveRecentSearchHashtags(const QString &text) {
	const auto result = saveRecentHashtags(
		[] { return cRecentSearchHashtags(); },
		text);
	if (result) {
		cSetRecentSearchHashtags(*result);
		writeRecentHashtagsAndBots();
	}
}

void Account::writeExportSettings(const Export::Settings &settings) {
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
			ClearKey(_exportSettingsKey, _basePath);
			_exportSettingsKey = 0;
			writeMapDelayed();
		}
		return;
	}
	if (!_exportSettingsKey) {
		_exportSettingsKey = GenerateKey(_basePath);
		writeMapQueued();
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

	FileWriteDescriptor file(_exportSettingsKey, _basePath);
	file.writeEncrypted(data, _localKey);
}

Export::Settings Account::readExportSettings() {
	FileReadDescriptor file;
	if (!ReadEncryptedFile(file, _exportSettingsKey, _basePath, _localKey)) {
		ClearKey(_exportSettingsKey, _basePath);
		_exportSettingsKey = 0;
		writeMapDelayed();
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

void Account::writeSelf() {
	writeMapDelayed();
}

void Account::readSelf(
		not_null<Main::Session*> session,
		const QByteArray &serialized,
		int32 streamVersion) {
	QDataStream stream(serialized);
	const auto user = session->user();
	const auto wasLoadedStatus = user->loadedStatus();
	user->setLoadedStatus(PeerData::LoadedStatus::Not);
	const auto self = Serialize::readPeer(
		session,
		streamVersion,
		stream);
	if (!self || !self->isSelf() || self != user) {
		user->setLoadedStatus(wasLoadedStatus);
		return;
	}

	QString about;
	stream >> about;
	if (CheckStreamStatus(stream)) {
		self->asUser()->setAbout(about);
	}
}

void Account::writeTrustedBots() {
	if (_trustedBots.empty()) {
		if (_trustedBotsKey) {
			ClearKey(_trustedBotsKey, _basePath);
			_trustedBotsKey = 0;
			writeMapDelayed();
		}
		return;
	}
	if (!_trustedBotsKey) {
		_trustedBotsKey = GenerateKey(_basePath);
		writeMapQueued();
	}
	quint32 size = sizeof(qint32) + _trustedBots.size() * sizeof(quint64);
	EncryptedDescriptor data(size);
	data.stream << qint32(_trustedBots.size());
	for_const (auto botId, _trustedBots) {
		data.stream << quint64(botId);
	}

	FileWriteDescriptor file(_trustedBotsKey, _basePath);
	file.writeEncrypted(data, _localKey);
}

void Account::readTrustedBots() {
	if (!_trustedBotsKey) return;

	FileReadDescriptor trusted;
	if (!ReadEncryptedFile(trusted, _trustedBotsKey, _basePath, _localKey)) {
		ClearKey(_trustedBotsKey, _basePath);
		_trustedBotsKey = 0;
		writeMapDelayed();
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

void Account::markBotTrusted(not_null<UserData*> bot) {
	if (!isBotTrusted(bot)) {
		_trustedBots.insert(bot->id);
		writeTrustedBots();
	}
}

bool Account::isBotTrusted(not_null<UserData*> bot) {
	if (!_trustedBotsRead) {
		readTrustedBots();
		_trustedBotsRead = true;
	}
	return _trustedBots.contains(bot->id);
}

bool Account::encrypt(
		const void *src,
		void *dst,
		uint32 len,
		const void *key128) const {
	if (!_localKey) {
		return false;
	}
	MTP::aesEncryptLocal(src, dst, len, _localKey, key128);
	return true;
}

bool Account::decrypt(
		const void *src,
		void *dst,
		uint32 len,
		const void *key128) const {
	if (!_localKey) {
		return false;
	}
	MTP::aesDecryptLocal(src, dst, len, _localKey, key128);
	return true;
}

} // namespace Storage
