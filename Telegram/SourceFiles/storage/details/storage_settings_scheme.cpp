/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/details/storage_settings_scheme.h"

#include "storage/details/storage_file_utilities.h"
#include "storage/cache/storage_cache_database.h"
#include "storage/serialize_common.h"
#include "core/application.h"
#include "mtproto/mtproto_config.h"
#include "ui/effects/animation_value.h"
#include "ui/widgets/input_fields.h"
#include "window/themes/window_theme.h"
#include "core/update_checker.h"
#include "platform/platform_specific.h"
#include "boxes/send_files_box.h"
#include "facades.h"

namespace Storage {
namespace details {
namespace {

using Cache::Database;

[[nodiscard]] bool NoTimeLimit(qint32 storedLimitValue) {
	// This is a workaround for a bug in storing the cache time limit.
	// See https://github.com/telegramdesktop/tdesktop/issues/5611
	return !storedLimitValue
		|| (storedLimitValue == qint32(std::numeric_limits<int32>::max()))
		|| (storedLimitValue == qint32(std::numeric_limits<int64>::max()));
}

} // namespace

bool ReadSetting(
		quint32 blockId,
		QDataStream &stream,
		int version,
		ReadSettingsContext &context) {
	switch (blockId) {
	case dbiDcOptionOldOld: {
		quint32 dcId, port;
		QString host, ip;
		stream >> dcId >> host >> ip >> port;
		if (!CheckStreamStatus(stream)) return false;

		context.fallbackConfigLegacyDcOptions.constructAddOne(
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
		if (!CheckStreamStatus(stream)) return false;

		context.fallbackConfigLegacyDcOptions.constructAddOne(
			dcIdWithShift,
			MTPDdcOption::Flags::from_raw(flags),
			ip.toStdString(),
			port,
			{});
	} break;

	case dbiDcOptionsOld: {
		auto serialized = QByteArray();
		stream >> serialized;
		if (!CheckStreamStatus(stream)) return false;

		context.fallbackConfigLegacyDcOptions.constructFromSerialized(
			serialized);
	} break;

	case dbiApplicationSettings: {
		auto serialized = QByteArray();
		stream >> serialized;
		if (!CheckStreamStatus(stream)) return false;

		Core::App().settings().constructFromSerialized(serialized);
	} break;

	case dbiChatSizeMaxOld: {
		qint32 maxSize;
		stream >> maxSize;
		if (!CheckStreamStatus(stream)) return false;

		context.fallbackConfigLegacyChatSizeMax = maxSize;
	} break;

	case dbiSavedGifsLimitOld: {
		qint32 limit;
		stream >> limit;
		if (!CheckStreamStatus(stream)) return false;

		context.fallbackConfigLegacySavedGifsLimit = limit;
	} break;

	case dbiStickersRecentLimitOld: {
		qint32 limit;
		stream >> limit;
		if (!CheckStreamStatus(stream)) return false;

		context.fallbackConfigLegacyStickersRecentLimit = limit;
	} break;

	case dbiStickersFavedLimitOld: {
		qint32 limit;
		stream >> limit;
		if (!CheckStreamStatus(stream)) return false;

		context.fallbackConfigLegacyStickersFavedLimit = limit;
	} break;

	case dbiMegagroupSizeMaxOld: {
		qint32 maxSize;
		stream >> maxSize;
		if (!CheckStreamStatus(stream)) return false;

		context.fallbackConfigLegacyMegagroupSizeMax = maxSize;
	} break;

	case dbiUser: {
		quint32 dcId;
		qint32 userId;
		stream >> userId >> dcId;
		if (!CheckStreamStatus(stream)) return false;

		DEBUG_LOG(("MTP Info: user found, dc %1, uid %2").arg(dcId).arg(userId));
		context.mtpLegacyMainDcId = dcId;
		context.mtpLegacyUserId = userId;
	} break;

	case dbiKey: {
		qint32 dcId;
		stream >> dcId;
		auto key = Serialize::read<MTP::AuthKey::Data>(stream);
		if (!CheckStreamStatus(stream)) return false;

		context.mtpLegacyKeys.push_back(std::make_shared<MTP::AuthKey>(
			MTP::AuthKey::Type::ReadFromFile,
			dcId,
			key));
	} break;

	case dbiMtpAuthorization: {
		auto serialized = QByteArray();
		stream >> serialized;
		if (!CheckStreamStatus(stream)) return false;

		context.mtpAuthorization = serialized;
	} break;

	case dbiAutoStart: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		cSetAutoStart(v == 1);
	} break;

	case dbiStartMinimized: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		cSetStartMinimized(v == 1);
	} break;

	case dbiSendToMenu: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		cSetSendToMenu(v == 1);
	} break;

	case dbiUseExternalVideoPlayer: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		cSetUseExternalVideoPlayer(v == 1);
	} break;

	case dbiCacheSettingsOld: {
		qint64 size;
		qint32 time;
		stream >> size >> time;
		if (!CheckStreamStatus(stream)
			|| size <= Database::Settings().maxDataSize
			|| (!NoTimeLimit(time) && time < 0)) {
			return false;
		}
		context.cacheTotalSizeLimit = size;
		context.cacheTotalTimeLimit = NoTimeLimit(time) ? 0 : time;
		context.cacheBigFileTotalSizeLimit = size;
		context.cacheBigFileTotalTimeLimit = NoTimeLimit(time) ? 0 : time;
	} break;

	case dbiCacheSettings: {
		qint64 size, sizeBig;
		qint32 time, timeBig;
		stream >> size >> time >> sizeBig >> timeBig;
		if (!CheckStreamStatus(stream)
			|| size <= Database::Settings().maxDataSize
			|| sizeBig <= Database::Settings().maxDataSize
			|| (!NoTimeLimit(time) && time < 0)
			|| (!NoTimeLimit(timeBig) && timeBig < 0)) {
			return false;
		}

		context.cacheTotalSizeLimit = size;
		context.cacheTotalTimeLimit = NoTimeLimit(time) ? 0 : time;
		context.cacheBigFileTotalSizeLimit = sizeBig;
		context.cacheBigFileTotalTimeLimit = NoTimeLimit(timeBig) ? 0 : timeBig;
	} break;

	case dbiAnimationsDisabled: {
		qint32 disabled;
		stream >> disabled;
		if (!CheckStreamStatus(stream)) {
			return false;
		}

		anim::SetDisabled(disabled == 1);
	} break;

	case dbiSoundFlashBounceNotify: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		Global::SetSoundNotify((v & 0x01) == 0x01);
		Global::SetFlashBounceNotify((v & 0x02) == 0x00);
	} break;

	case dbiAutoDownloadOld: {
		qint32 photo, audio, gif;
		stream >> photo >> audio >> gif;
		if (!CheckStreamStatus(stream)) return false;

		using namespace Data::AutoDownload;
		auto &settings = context.sessionSettings().autoDownload();
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
		set(Type::AutoPlayGIF, gif);
		set(Type::AutoPlayVideoMessage, gif);
	} break;

	case dbiAutoPlayOld: {
		qint32 gif;
		stream >> gif;
		if (!CheckStreamStatus(stream)) return false;

		if (!gif) {
			using namespace Data::AutoDownload;
			auto &settings = context.sessionSettings().autoDownload();
			const auto types = {
				Type::AutoPlayGIF,
				Type::AutoPlayVideo,
				Type::AutoPlayVideoMessage,
			};
			const auto sources = {
				Source::User,
				Source::Group,
				Source::Channel
			};
			for (const auto source : sources) {
				for (const auto type : types) {
					settings.setBytesLimit(source, type, 0);
				}
			}
		}
	} break;

	case dbiDialogsModeOld: {
		qint32 enabled, modeInt;
		stream >> enabled >> modeInt;
		if (!CheckStreamStatus(stream)) return false;
	} break;

	case dbiDialogsFilters: {
		qint32 enabled;
		stream >> enabled;
		if (!CheckStreamStatus(stream)) return false;

		Global::SetDialogsFiltersEnabled(enabled == 1);
	} break;

	case dbiModerateMode: {
		qint32 enabled;
		stream >> enabled;
		if (!CheckStreamStatus(stream)) return false;

		Global::SetModerateModeEnabled(enabled == 1);
	} break;

	case dbiIncludeMutedOld: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		context.sessionSettings().setIncludeMutedCounter(v == 1);
	} break;

	case dbiShowingSavedGifsOld: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;
	} break;

	case dbiDesktopNotify: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		Global::SetDesktopNotify(v == 1);
	} break;

	case dbiWindowsNotificationsOld: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;
	} break;

	case dbiNativeNotifications: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		Global::SetNativeNotifications(v == 1);
	} break;

	case dbiNotificationsCount: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		Global::SetNotificationsCount((v > 0 ? v : 3));
	} break;

	case dbiNotificationsCorner: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		Global::SetNotificationsCorner(static_cast<Notify::ScreenCorner>((v >= 0 && v < 4) ? v : 2));
	} break;

	case dbiDialogsWidthRatioOld: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		context.sessionSettings().setDialogsWidthRatio(v / 1000000.);
	} break;

	case dbiLastSeenWarningSeenOld: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		context.sessionSettings().setLastSeenWarningSeen(v == 1);
	} break;

	case dbiSessionSettings: {
		QByteArray v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		context.sessionSettingsStorage
			= Main::SessionSettings::FromSerialized(v);
	} break;

	case dbiWorkMode: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		auto newMode = [v] {
			switch (v) {
			case dbiwmTrayOnly: return dbiwmTrayOnly;
			case dbiwmWindowOnly: return dbiwmWindowOnly;
			};
			return dbiwmWindowAndTray;
		};
		Global::RefWorkMode().set(newMode());
	} break;

	case dbiTxtDomainStringOldOld: {
		QString v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;
	} break;

	case dbiTxtDomainStringOld: {
		QString v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		context.fallbackConfigLegacyTxtDomainString = v;
	} break;

	case dbiConnectionTypeOld: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		MTP::ProxyData proxy;
		switch (v) {
		case dbictHttpProxy:
		case dbictTcpProxy: {
			qint32 port;
			stream >> proxy.host >> port >> proxy.user >> proxy.password;
			if (!CheckStreamStatus(stream)) return false;

			proxy.port = uint32(port);
			proxy.type = (v == dbictTcpProxy)
				? MTP::ProxyData::Type::Socks5
				: MTP::ProxyData::Type::Http;
		} break;
		};
		Global::SetSelectedProxy(proxy ? proxy : MTP::ProxyData());
		Global::SetProxySettings(proxy
			? MTP::ProxyData::Settings::Enabled
			: MTP::ProxyData::Settings::System);
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
		if (!CheckStreamStatus(stream)) {
			return false;
		}

		const auto readProxy = [&] {
			qint32 proxyType, port;
			MTP::ProxyData proxy;
			stream >> proxyType >> proxy.host >> port >> proxy.user >> proxy.password;
			proxy.port = port;
			proxy.type = (proxyType == dbictTcpProxy)
				? MTP::ProxyData::Type::Socks5
				: (proxyType == dbictHttpProxy)
				? MTP::ProxyData::Type::Http
				: (proxyType == kProxyTypeShift + int(MTP::ProxyData::Type::Socks5))
				? MTP::ProxyData::Type::Socks5
				: (proxyType == kProxyTypeShift + int(MTP::ProxyData::Type::Http))
				? MTP::ProxyData::Type::Http
				: (proxyType == kProxyTypeShift + int(MTP::ProxyData::Type::Mtproto))
				? MTP::ProxyData::Type::Mtproto
				: MTP::ProxyData::Type::None;
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

			auto list = std::vector<MTP::ProxyData>();
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
			if (!CheckStreamStatus(stream)) {
				return false;
			}
			Global::SetProxiesList(list);
			if (connectionType == dbictProxiesListOld) {
				settings = static_cast<qint32>(
					(index > 0 && index <= list.size()
						? MTP::ProxyData::Settings::Enabled
						: MTP::ProxyData::Settings::System));
				index = std::abs(index);
			}
			if (index > 0 && index <= list.size()) {
				Global::SetSelectedProxy(list[index - 1]);
			} else {
				Global::SetSelectedProxy(MTP::ProxyData());
			}

			const auto unchecked = static_cast<MTP::ProxyData::Settings>(settings);
			switch (unchecked) {
			case MTP::ProxyData::Settings::Enabled:
				Global::SetProxySettings(Global::SelectedProxy()
					? MTP::ProxyData::Settings::Enabled
					: MTP::ProxyData::Settings::System);
				break;
			case MTP::ProxyData::Settings::Disabled:
			case MTP::ProxyData::Settings::System:
				Global::SetProxySettings(unchecked);
				break;
			default:
				Global::SetProxySettings(MTP::ProxyData::Settings::System);
				break;
			}
			Global::SetUseProxyForCalls(calls == 1);
		} else {
			const auto proxy = readProxy();
			if (!CheckStreamStatus(stream)) {
				return false;
			}
			if (proxy) {
				Global::SetProxiesList({ 1, proxy });
				Global::SetSelectedProxy(proxy);
				if (connectionType == dbictTcpProxy
					|| connectionType == dbictHttpProxy) {
					Global::SetProxySettings(MTP::ProxyData::Settings::Enabled);
				} else {
					Global::SetProxySettings(MTP::ProxyData::Settings::System);
				}
			} else {
				Global::SetProxiesList({});
				Global::SetSelectedProxy(MTP::ProxyData());
				Global::SetProxySettings(MTP::ProxyData::Settings::System);
			}
		}
		Core::App().refreshGlobalProxy();
	} break;

	case dbiThemeKeyOld: {
		quint64 key = 0;
		stream >> key;
		if (!CheckStreamStatus(stream)) return false;

		context.themeKeyLegacy = key;
	} break;

	case dbiThemeKey: {
		quint64 keyDay = 0, keyNight = 0;
		quint32 nightMode = 0;
		stream >> keyDay >> keyNight >> nightMode;
		if (!CheckStreamStatus(stream)) return false;

		context.themeKeyDay = keyDay;
		context.themeKeyNight = keyNight;
		Window::Theme::SetNightModeValue(nightMode == 1);
	} break;

	case dbiLangPackKey: {
		quint64 langPackKey = 0;
		stream >> langPackKey;
		if (!CheckStreamStatus(stream)) return false;

		context.langPackKey = langPackKey;
	} break;

	case dbiLanguagesKey: {
		quint64 languagesKey = 0;
		stream >> languagesKey;
		if (!CheckStreamStatus(stream)) return false;

		context.languagesKey = languagesKey;
	} break;

	case dbiTryIPv6: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		Global::SetTryIPv6(v == 1);
	} break;

	case dbiSeenTrayTooltip: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		cSetSeenTrayTooltip(v == 1);
	} break;

	case dbiAutoUpdate: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		cSetAutoUpdate(v == 1);
		if (!Core::UpdaterDisabled() && !cAutoUpdate()) {
			Core::UpdateChecker().stop();
		}
	} break;

	case dbiLastUpdateCheck: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		cSetLastUpdateCheck(v);
	} break;

	case dbiScaleOld: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		SetScaleChecked([&] {
			constexpr auto kAuto = 0;
			constexpr auto kOne = 1;
			constexpr auto kOneAndQuarter = 2;
			constexpr auto kOneAndHalf = 3;
			constexpr auto kTwo = 4;
			switch (v) {
			case kAuto: return style::kScaleAuto;
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
		if (!CheckStreamStatus(stream)) return false;

		// If cConfigScale() has value then it was set via command line.
		if (cConfigScale() == style::kScaleAuto) {
			SetScaleChecked(v);
		}
	} break;

	case dbiLangOld: { // deprecated
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;
	} break;

	case dbiLangFileOld: { // deprecated
		QString v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;
	} break;

	case dbiWindowPosition: {
		auto position = TWindowPos();
		stream >> position.x >> position.y >> position.w >> position.h;
		stream >> position.moncrc >> position.maximized;
		if (!CheckStreamStatus(stream)) return false;

		DEBUG_LOG(("Window Pos: Read from storage %1, %2, %3, %4 (maximized %5)").arg(position.x).arg(position.y).arg(position.w).arg(position.h).arg(Logs::b(position.maximized)));
		cSetWindowPos(position);
	} break;

	case dbiLoggedPhoneNumberOld: { // deprecated
		QString v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;
	} break;

	case dbiMutePeerOld: { // deprecated
		quint64 peerId;
		stream >> peerId;
		if (!CheckStreamStatus(stream)) return false;
	} break;

	case dbiMutedPeersOld: { // deprecated
		quint32 count;
		stream >> count;
		if (!CheckStreamStatus(stream)) return false;

		for (uint32 i = 0; i < count; ++i) {
			quint64 peerId;
			stream >> peerId;
		}
		if (!CheckStreamStatus(stream)) return false;
	} break;

	case dbiSendKeyOld: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		using SendSettings = Ui::InputSubmitSettings;
		const auto unchecked = static_cast<SendSettings>(v);

		if (unchecked != SendSettings::Enter
			&& unchecked != SendSettings::CtrlEnter) {
			return false;
		}
		context.sessionSettings().setSendSubmitWay(unchecked);
	} break;

	case dbiCatsAndDogs: { // deprecated
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;
	} break;

	case dbiTileBackgroundOld: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		bool tile = (version < 8005 && !context.hasCustomDayBackground)
			? false
			: (v == 1);
		if (Window::Theme::IsNightMode()) {
			context.tileNight = tile;
		} else {
			context.tileDay = tile;
		}
	} break;

	case dbiTileBackground: {
		qint32 tileDay, tileNight;
		stream >> tileDay >> tileNight;
		if (!CheckStreamStatus(stream)) return false;

		context.tileDay = (tileDay == 1);
		context.tileNight = (tileNight == 1);
	} break;

	case dbiAdaptiveForWide: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		Global::SetAdaptiveForWide(v == 1);
	} break;

	case dbiAutoLock: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		Global::SetAutoLock(v);
		Global::RefLocalPasscodeChanged().notify();
	} break;

	case dbiReplaceEmojiOld: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		context.sessionSettings().setReplaceEmoji(v == 1);
	} break;

	case dbiSuggestEmojiOld: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		context.sessionSettings().setSuggestEmoji(v == 1);
	} break;

	case dbiSuggestStickersByEmojiOld: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		context.sessionSettings().setSuggestStickersByEmoji(v == 1);
	} break;

	case dbiDefaultAttach: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;
	} break;

	case dbiNotifyView: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		switch (v) {
		case dbinvShowNothing: Global::SetNotifyView(dbinvShowNothing); break;
		case dbinvShowName: Global::SetNotifyView(dbinvShowName); break;
		default: Global::SetNotifyView(dbinvShowPreview); break;
		}
	} break;

	case dbiAskDownloadPath: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		Global::SetAskDownloadPath(v == 1);
	} break;

	case dbiDownloadPathOld: {
		QString v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;
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
		if (!CheckStreamStatus(stream)) return false;

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
		if (!CheckStreamStatus(stream)) return false;

		context.sessionSettings().setSendFilesWay((v == 1)
			? SendFilesWay::Album
			: SendFilesWay::Files);
	} break;

	case dbiEmojiTabOld: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		// deprecated
	} break;

	case dbiRecentEmojiOldOld: {
		RecentEmojiPreloadOldOld v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

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
		if (!CheckStreamStatus(stream)) return false;

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
		if (!CheckStreamStatus(stream)) return false;

		cSetRecentEmojiPreload(v);
	} break;

	case dbiRecentStickers: {
		RecentStickerPreload v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		cSetRecentStickersPreload(v);
	} break;

	case dbiEmojiVariantsOld: {
		EmojiColorVariantsOld v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

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
		if (!CheckStreamStatus(stream)) return false;

		cSetEmojiVariants(v);
	} break;

	case dbiHiddenPinnedMessagesOld: {
		auto v = QMap<PeerId, MsgId>();
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		for (auto i = v.begin(), e = v.end(); i != e; ++i) {
			context.sessionSettings().setHiddenPinnedMessageId(
				i.key(),
				i.value());
		}
	} break;

	case dbiDialogLastPath: {
		QString path;
		stream >> path;
		if (!CheckStreamStatus(stream)) return false;

		cSetDialogLastPath(path);
	} break;

	case dbiSongVolume: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		Global::SetSongVolume(snap(v / 1e6, 0., 1.));
	} break;

	case dbiVideoVolume: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		Global::SetVideoVolume(snap(v / 1e6, 0., 1.));
	} break;

	case dbiPlaybackSpeed: {
		qint32 v;
		stream >> v;
		if (!CheckStreamStatus(stream)) return false;

		Global::SetVoiceMsgPlaybackDoubled(v == 2);
	} break;

	case dbiCallSettings: {
		QByteArray callSettings;
		stream >> callSettings;
		if (!CheckStreamStatus(stream)) return false;

		context.callSettings = callSettings;
	} break;

	case dbiFallbackProductionConfig: {
		QByteArray config;
		stream >> config;
		if (!CheckStreamStatus(stream)) return false;

		context.fallbackConfig = config;
	} break;

	default:
		LOG(("App Error: unknown blockId in _readSetting: %1").arg(blockId));
		return false;
	}

	return true;
}

void ApplyReadFallbackConfig(ReadSettingsContext &context) {
	if (context.fallbackConfig.isEmpty()) {
		auto &config = Core::App().fallbackProductionConfig();
		config.dcOptions().addFromOther(
			std::move(context.fallbackConfigLegacyDcOptions));
		if (context.fallbackConfigLegacyChatSizeMax > 0) {
			config.setChatSizeMax(context.fallbackConfigLegacyChatSizeMax);
		}
		if (context.fallbackConfigLegacySavedGifsLimit > 0) {
			config.setSavedGifsLimit(
				context.fallbackConfigLegacySavedGifsLimit);
		}
		if (context.fallbackConfigLegacyStickersRecentLimit > 0) {
			config.setStickersRecentLimit(
				context.fallbackConfigLegacyStickersRecentLimit);
		}
		if (context.fallbackConfigLegacyStickersFavedLimit > 0) {
			config.setStickersFavedLimit(
				context.fallbackConfigLegacyStickersFavedLimit);
		}
		if (context.fallbackConfigLegacyMegagroupSizeMax > 0) {
			config.setMegagroupSizeMax(
				context.fallbackConfigLegacyMegagroupSizeMax);
		}
		if (!context.fallbackConfigLegacyTxtDomainString.isEmpty()) {
			config.setTxtDomainString(
				context.fallbackConfigLegacyTxtDomainString);
		}
	} else {
		Core::App().constructFallbackProductionConfig(context.fallbackConfig);
	}
}

} // namespace details
} // namespace Storage
