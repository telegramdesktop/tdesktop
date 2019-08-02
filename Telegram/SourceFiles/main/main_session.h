/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/event_stream.h>
#include <rpl/filter.h>
#include <rpl/variable.h>
#include "base/timer.h"
#include "data/data_auto_download.h"

class ApiWrap;
enum class SendFilesWay;

namespace Ui {
enum class InputSubmitSettings;
} // namespace Ui

namespace Support {
enum class SwitchSettings;
class Helper;
class Templates;
} // namespace Support

namespace Data {
class Session;
} // namespace Data

namespace Storage {
class Downloader;
class Uploader;
class Facade;
} // namespace Storage

namespace Window {
namespace Notifications {
class System;
} // namespace Notifications
enum class Column;
} // namespace Window

namespace Calls {
class Instance;
} // namespace Calls

namespace ChatHelpers {
enum class SelectorTab;
} // namespace ChatHelpers

namespace Stickers {
class EmojiPack;
} // namespace Stickers;

namespace Core {
class Changelogs;
} // namespace Core

namespace Main {

class Account;
class AppConfig;

class Settings final {
public:
	void moveFrom(Settings &&other) {
		_variables = std::move(other._variables);
	}
	[[nodiscard]] QByteArray serialize() const;
	void constructFromSerialized(const QByteArray &serialized);

	void setLastSeenWarningSeen(bool lastSeenWarningSeen) {
		_variables.lastSeenWarningSeen = lastSeenWarningSeen;
	}
	[[nodiscard]] bool lastSeenWarningSeen() const {
		return _variables.lastSeenWarningSeen;
	}
	void setSendFilesWay(SendFilesWay way) {
		_variables.sendFilesWay = way;
	}
	[[nodiscard]] SendFilesWay sendFilesWay() const {
		return _variables.sendFilesWay;
	}
	void setSendSubmitWay(Ui::InputSubmitSettings value) {
		_variables.sendSubmitWay = value;
	}
	[[nodiscard]] Ui::InputSubmitSettings sendSubmitWay() const {
		return _variables.sendSubmitWay;
	}

	void setSupportSwitch(Support::SwitchSettings value) {
		_variables.supportSwitch = value;
	}
	[[nodiscard]] Support::SwitchSettings supportSwitch() const {
		return _variables.supportSwitch;
	}
	void setSupportFixChatsOrder(bool fix) {
		_variables.supportFixChatsOrder = fix;
	}
	[[nodiscard]] bool supportFixChatsOrder() const {
		return _variables.supportFixChatsOrder;
	}
	void setSupportTemplatesAutocomplete(bool enabled) {
		_variables.supportTemplatesAutocomplete = enabled;
	}
	[[nodiscard]] bool supportTemplatesAutocomplete() const {
		return _variables.supportTemplatesAutocomplete;
	}
	void setSupportChatsTimeSlice(int slice);
	[[nodiscard]] int supportChatsTimeSlice() const;
	[[nodiscard]] rpl::producer<int> supportChatsTimeSliceValue() const;
	void setSupportAllSearchResults(bool all);
	[[nodiscard]] bool supportAllSearchResults() const;
	[[nodiscard]] rpl::producer<bool> supportAllSearchResultsValue() const;

	[[nodiscard]] ChatHelpers::SelectorTab selectorTab() const {
		return _variables.selectorTab;
	}
	void setSelectorTab(ChatHelpers::SelectorTab tab) {
		_variables.selectorTab = tab;
	}
	[[nodiscard]] bool tabbedSelectorSectionEnabled() const {
		return _variables.tabbedSelectorSectionEnabled;
	}
	void setTabbedSelectorSectionEnabled(bool enabled);
	[[nodiscard]] bool thirdSectionInfoEnabled() const {
		return _variables.thirdSectionInfoEnabled;
	}
	void setThirdSectionInfoEnabled(bool enabled);
	[[nodiscard]] rpl::producer<bool> thirdSectionInfoEnabledValue() const;
	[[nodiscard]] int thirdSectionExtendedBy() const {
		return _variables.thirdSectionExtendedBy;
	}
	void setThirdSectionExtendedBy(int savedValue) {
		_variables.thirdSectionExtendedBy = savedValue;
	}
	[[nodiscard]] bool tabbedReplacedWithInfo() const {
		return _tabbedReplacedWithInfo;
	}
	void setTabbedReplacedWithInfo(bool enabled);
	[[nodiscard]] rpl::producer<bool> tabbedReplacedWithInfoValue() const;
	void setSmallDialogsList(bool enabled) {
		_variables.smallDialogsList = enabled;
	}
	[[nodiscard]] bool smallDialogsList() const {
		return _variables.smallDialogsList;
	}
	void setSoundOverride(const QString &key, const QString &path) {
		_variables.soundOverrides.insert(key, path);
	}
	void clearSoundOverrides() {
		_variables.soundOverrides.clear();
	}
	[[nodiscard]] QString getSoundPath(const QString &key) const;
	void setTabbedSelectorSectionTooltipShown(int shown) {
		_variables.tabbedSelectorSectionTooltipShown = shown;
	}
	[[nodiscard]] int tabbedSelectorSectionTooltipShown() const {
		return _variables.tabbedSelectorSectionTooltipShown;
	}
	void setFloatPlayerColumn(Window::Column column) {
		_variables.floatPlayerColumn = column;
	}
	[[nodiscard]] Window::Column floatPlayerColumn() const {
		return _variables.floatPlayerColumn;
	}
	void setFloatPlayerCorner(RectPart corner) {
		_variables.floatPlayerCorner = corner;
	}
	[[nodiscard]] RectPart floatPlayerCorner() const {
		return _variables.floatPlayerCorner;
	}
	void setDialogsWidthRatio(float64 ratio);
	[[nodiscard]] float64 dialogsWidthRatio() const;
	[[nodiscard]] rpl::producer<float64> dialogsWidthRatioChanges() const;
	void setThirdColumnWidth(int width);
	[[nodiscard]] int thirdColumnWidth() const;
	[[nodiscard]] rpl::producer<int> thirdColumnWidthChanges() const;

	void setGroupStickersSectionHidden(PeerId peerId) {
		_variables.groupStickersSectionHidden.insert(peerId);
	}
	[[nodiscard]] bool isGroupStickersSectionHidden(PeerId peerId) const {
		return _variables.groupStickersSectionHidden.contains(peerId);
	}
	void removeGroupStickersSectionHidden(PeerId peerId) {
		_variables.groupStickersSectionHidden.remove(peerId);
	}

	[[nodiscard]] Data::AutoDownload::Full &autoDownload() {
		return _variables.autoDownload;
	}
	[[nodiscard]] const Data::AutoDownload::Full &autoDownload() const {
		return _variables.autoDownload;
	}

	void setArchiveCollapsed(bool collapsed);
	[[nodiscard]] bool archiveCollapsed() const;
	[[nodiscard]] rpl::producer<bool> archiveCollapsedChanges() const;

	void setArchiveInMainMenu(bool inMainMenu);
	[[nodiscard]] bool archiveInMainMenu() const;
	[[nodiscard]] rpl::producer<bool> archiveInMainMenuChanges() const;

	void setNotifyAboutPinned(bool notify);
	[[nodiscard]] bool notifyAboutPinned() const;
	[[nodiscard]] rpl::producer<bool> notifyAboutPinnedChanges() const;

	void setSkipArchiveInSearch(bool skip);
	[[nodiscard]] bool skipArchiveInSearch() const;
	[[nodiscard]] rpl::producer<bool> skipArchiveInSearchChanges() const;

	[[nodiscard]] bool hadLegacyCallsPeerToPeerNobody() const {
		return _variables.hadLegacyCallsPeerToPeerNobody;
	}

	[[nodiscard]] bool includeMutedCounter() const {
		return _variables.includeMutedCounter;
	}
	void setIncludeMutedCounter(bool value) {
		_variables.includeMutedCounter = value;
	}
	[[nodiscard]] bool countUnreadMessages() const {
		return _variables.countUnreadMessages;
	}
	void setCountUnreadMessages(bool value) {
		_variables.countUnreadMessages = value;
	}
	[[nodiscard]] bool exeLaunchWarning() const {
		return _variables.exeLaunchWarning;
	}
	void setExeLaunchWarning(bool warning) {
		_variables.exeLaunchWarning = warning;
	}
	[[nodiscard]] bool autoplayGifs() const {
		return _variables.autoplayGifs;
	}
	void setAutoplayGifs(bool value) {
		_variables.autoplayGifs = value;
	}
	[[nodiscard]] bool loopAnimatedStickers() const {
		return _variables.loopAnimatedStickers;
	}
	void setLoopAnimatedStickers(bool value) {
		_variables.loopAnimatedStickers = value;
	}
	void setLargeEmoji(bool value);
	[[nodiscard]] bool largeEmoji() const;
	[[nodiscard]] rpl::producer<bool> largeEmojiChanges() const;
	void setReplaceEmoji(bool value);
	[[nodiscard]] bool replaceEmoji() const;
	[[nodiscard]] rpl::producer<bool> replaceEmojiValue() const;
	[[nodiscard]] rpl::producer<bool> replaceEmojiChanges() const;
	[[nodiscard]] bool suggestEmoji() const {
		return _variables.suggestEmoji;
	}
	void setSuggestEmoji(bool value) {
		_variables.suggestEmoji = value;
	}
	[[nodiscard]] bool suggestStickersByEmoji() const {
		return _variables.suggestStickersByEmoji;
	}
	void setSuggestStickersByEmoji(bool value) {
		_variables.suggestStickersByEmoji = value;
	}

private:
	struct Variables {
		Variables();

		static constexpr auto kDefaultDialogsWidthRatio = 5. / 14;
		static constexpr auto kDefaultThirdColumnWidth = 0;

		bool lastSeenWarningSeen = false;
		SendFilesWay sendFilesWay;
		ChatHelpers::SelectorTab selectorTab; // per-window
		bool tabbedSelectorSectionEnabled = false; // per-window
		int tabbedSelectorSectionTooltipShown = 0;
		QMap<QString, QString> soundOverrides;
		Window::Column floatPlayerColumn; // per-window
		RectPart floatPlayerCorner; // per-window
		base::flat_set<PeerId> groupStickersSectionHidden;
		bool thirdSectionInfoEnabled = true; // per-window
		bool smallDialogsList = false; // per-window
		int thirdSectionExtendedBy = -1; // per-window
		rpl::variable<float64> dialogsWidthRatio
			= kDefaultDialogsWidthRatio; // per-window
		rpl::variable<int> thirdColumnWidth
			= kDefaultThirdColumnWidth; // per-window
		Ui::InputSubmitSettings sendSubmitWay;
		bool hadLegacyCallsPeerToPeerNobody = false;
		bool includeMutedCounter = true;
		bool countUnreadMessages = true;
		bool exeLaunchWarning = true;
		Data::AutoDownload::Full autoDownload;
		rpl::variable<bool> archiveCollapsed = false;
		rpl::variable<bool> archiveInMainMenu = false;
		rpl::variable<bool> notifyAboutPinned = true;
		rpl::variable<bool> skipArchiveInSearch = false;
		bool autoplayGifs = true;
		bool loopAnimatedStickers = true;
		rpl::variable<bool> largeEmoji = true;
		rpl::variable<bool> replaceEmoji = true;
		bool suggestEmoji = true;
		bool suggestStickersByEmoji = true;

		static constexpr auto kDefaultSupportChatsLimitSlice
			= 7 * 24 * 60 * 60;

		Support::SwitchSettings supportSwitch;
		bool supportFixChatsOrder = true;
		bool supportTemplatesAutocomplete = true;
		rpl::variable<int> supportChatsTimeSlice
			= kDefaultSupportChatsLimitSlice;
		rpl::variable<bool> supportAllSearchResults = false;
	};

	rpl::event_stream<bool> _thirdSectionInfoEnabledValue;
	bool _tabbedReplacedWithInfo = false;
	rpl::event_stream<bool> _tabbedReplacedWithInfoValue;

	Variables _variables;

};

class Session final
	: public base::has_weak_ptr
	, private base::Subscriber {
public:
	Session(not_null<Main::Account*> account, const MTPUser &user);
	~Session();

	Session(const Session &other) = delete;
	Session &operator=(const Session &other) = delete;

	static bool Exists();

	Main::Account &account() const;

	UserId userId() const;
	PeerId userPeerId() const;
	not_null<UserData*> user() const {
		return _user;
	}
	bool validateSelf(const MTPUser &user);

	Storage::Downloader &downloader() {
		return *_downloader;
	}
	Storage::Uploader &uploader() {
		return *_uploader;
	}
	Storage::Facade &storage() {
		return *_storage;
	}
	Stickers::EmojiPack &emojiStickersPack() {
		return *_emojiStickersPack;
	}
	AppConfig &appConfig() {
		return *_appConfig;
	}

	base::Observable<void> &downloaderTaskFinished();

	Window::Notifications::System &notifications() {
		return *_notifications;
	}

	Data::Session &data() {
		return *_data;
	}
	Settings &settings() {
		return _settings;
	}
	void moveSettingsFrom(Settings &&other);
	void saveSettingsDelayed(crl::time delay = kDefaultSaveDelay);

	not_null<MTP::Instance*> mtp();
	ApiWrap &api() {
		return *_api;
	}

	Calls::Instance &calls() {
		return *_calls;
	}

	void checkAutoLock();
	void checkAutoLockIn(crl::time time);
	void localPasscodeChanged();
	void termsDeleteNow();

	rpl::lifetime &lifetime() {
		return _lifetime;
	}

	base::Observable<DocumentData*> documentUpdated;
	base::Observable<std::pair<not_null<HistoryItem*>, MsgId>> messageIdChanging;

	bool supportMode() const;
	Support::Helper &supportHelper() const;
	Support::Templates &supportTemplates() const;

private:
	static constexpr auto kDefaultSaveDelay = crl::time(1000);

	const not_null<Main::Account*> _account;

	Settings _settings;
	base::Timer _saveDataTimer;

	crl::time _shouldLockAt = 0;
	base::Timer _autoLockTimer;

	const std::unique_ptr<ApiWrap> _api;
	const std::unique_ptr<AppConfig> _appConfig;
	const std::unique_ptr<Calls::Instance> _calls;
	const std::unique_ptr<Storage::Downloader> _downloader;
	const std::unique_ptr<Storage::Uploader> _uploader;
	const std::unique_ptr<Storage::Facade> _storage;
	const std::unique_ptr<Window::Notifications::System> _notifications;

	// _data depends on _downloader / _uploader / _notifications.
	const std::unique_ptr<Data::Session> _data;
	const not_null<UserData*> _user;

	// _emojiStickersPack depends on _data.
	const std::unique_ptr<Stickers::EmojiPack> _emojiStickersPack;

	// _changelogs depends on _data, subscribes on chats loading event.
	const std::unique_ptr<Core::Changelogs> _changelogs;

	const std::unique_ptr<Support::Helper> _supportHelper;

	rpl::lifetime _lifetime;

};

} // namespace Main

Main::Session &Auth();
