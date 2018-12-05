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

namespace Core {
class Changelogs;
} // namespace Core

class AuthSessionSettings final {
public:
	void moveFrom(AuthSessionSettings &&other) {
		_variables = std::move(other._variables);
	}
	QByteArray serialize() const;
	void constructFromSerialized(const QByteArray &serialized);

	void setLastSeenWarningSeen(bool lastSeenWarningSeen) {
		_variables.lastSeenWarningSeen = lastSeenWarningSeen;
	}
	bool lastSeenWarningSeen() const {
		return _variables.lastSeenWarningSeen;
	}
	void setSendFilesWay(SendFilesWay way) {
		_variables.sendFilesWay = way;
	}
	SendFilesWay sendFilesWay() const {
		return _variables.sendFilesWay;
	}
	void setSendSubmitWay(Ui::InputSubmitSettings value) {
		_variables.sendSubmitWay = value;
	}
	Ui::InputSubmitSettings sendSubmitWay() const {
		return _variables.sendSubmitWay;
	}

	void setSupportSwitch(Support::SwitchSettings value) {
		_variables.supportSwitch = value;
	}
	Support::SwitchSettings supportSwitch() const {
		return _variables.supportSwitch;
	}
	void setSupportFixChatsOrder(bool fix) {
		_variables.supportFixChatsOrder = fix;
	}
	bool supportFixChatsOrder() const {
		return _variables.supportFixChatsOrder;
	}
	void setSupportTemplatesAutocomplete(bool enabled) {
		_variables.supportTemplatesAutocomplete = enabled;
	}
	bool supportTemplatesAutocomplete() const {
		return _variables.supportTemplatesAutocomplete;
	}
	void setSupportChatsTimeSlice(int slice);
	int supportChatsTimeSlice() const;
	rpl::producer<int> supportChatsTimeSliceValue() const;

	ChatHelpers::SelectorTab selectorTab() const {
		return _variables.selectorTab;
	}
	void setSelectorTab(ChatHelpers::SelectorTab tab) {
		_variables.selectorTab = tab;
	}
	bool tabbedSelectorSectionEnabled() const {
		return _variables.tabbedSelectorSectionEnabled;
	}
	void setTabbedSelectorSectionEnabled(bool enabled);
	bool thirdSectionInfoEnabled() const {
		return _variables.thirdSectionInfoEnabled;
	}
	void setThirdSectionInfoEnabled(bool enabled);
	rpl::producer<bool> thirdSectionInfoEnabledValue() const;
	int thirdSectionExtendedBy() const {
		return _variables.thirdSectionExtendedBy;
	}
	void setThirdSectionExtendedBy(int savedValue) {
		_variables.thirdSectionExtendedBy = savedValue;
	}
	bool tabbedReplacedWithInfo() const {
		return _tabbedReplacedWithInfo;
	}
	void setTabbedReplacedWithInfo(bool enabled);
	rpl::producer<bool> tabbedReplacedWithInfoValue() const;
	void setSmallDialogsList(bool enabled) {
		_variables.smallDialogsList = enabled;
	}
	bool smallDialogsList() const {
		return _variables.smallDialogsList;
	}
	void setLastTimeVideoPlayedAt(TimeMs time) {
		_lastTimeVideoPlayedAt = time;
	}
	TimeMs lastTimeVideoPlayedAt() const {
		return _lastTimeVideoPlayedAt;
	}
	void setSoundOverride(const QString &key, const QString &path) {
		_variables.soundOverrides.insert(key, path);
	}
	void clearSoundOverrides() {
		_variables.soundOverrides.clear();
	}
	QString getSoundPath(const QString &key) const;
	void setTabbedSelectorSectionTooltipShown(int shown) {
		_variables.tabbedSelectorSectionTooltipShown = shown;
	}
	int tabbedSelectorSectionTooltipShown() const {
		return _variables.tabbedSelectorSectionTooltipShown;
	}
	void setFloatPlayerColumn(Window::Column column) {
		_variables.floatPlayerColumn = column;
	}
	Window::Column floatPlayerColumn() const {
		return _variables.floatPlayerColumn;
	}
	void setFloatPlayerCorner(RectPart corner) {
		_variables.floatPlayerCorner = corner;
	}
	RectPart floatPlayerCorner() const {
		return _variables.floatPlayerCorner;
	}
	void setDialogsWidthRatio(float64 ratio);
	float64 dialogsWidthRatio() const;
	rpl::producer<float64> dialogsWidthRatioChanges() const;
	void setThirdColumnWidth(int width);
	int thirdColumnWidth() const;
	rpl::producer<int> thirdColumnWidthChanges() const;

	void setGroupStickersSectionHidden(PeerId peerId) {
		_variables.groupStickersSectionHidden.insert(peerId);
	}
	bool isGroupStickersSectionHidden(PeerId peerId) const {
		return _variables.groupStickersSectionHidden.contains(peerId);
	}
	void removeGroupStickersSectionHidden(PeerId peerId) {
		_variables.groupStickersSectionHidden.remove(peerId);
	}

	bool hadLegacyCallsPeerToPeerNobody() const {
		return _variables.hadLegacyCallsPeerToPeerNobody;
	}

	bool includeMutedCounter() const {
		return _variables.includeMutedCounter;
	}
	void setIncludeMutedCounter(bool value) {
		_variables.includeMutedCounter = value;
	}
	bool countUnreadMessages() const {
		return _variables.countUnreadMessages;
	}
	void setCountUnreadMessages(bool value) {
		_variables.countUnreadMessages = value;
	}
	bool exeLaunchWarning() const {
		return _variables.exeLaunchWarning;
	}
	void setExeLaunchWarning(bool warning) {
		_variables.exeLaunchWarning = warning;
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

		static constexpr auto kDefaultSupportChatsLimitSlice
			= 7 * 24 * 60 * 60;

		Support::SwitchSettings supportSwitch;
		bool supportFixChatsOrder = true;
		bool supportTemplatesAutocomplete = true;
		rpl::variable<int> supportChatsTimeSlice
			= kDefaultSupportChatsLimitSlice;
	};

	rpl::event_stream<bool> _thirdSectionInfoEnabledValue;
	bool _tabbedReplacedWithInfo = false;
	rpl::event_stream<bool> _tabbedReplacedWithInfoValue;

	Variables _variables;
	TimeMs _lastTimeVideoPlayedAt = 0;

};

// One per Messenger.
class AuthSession;
AuthSession &Auth();

class AuthSession final
	: public base::has_weak_ptr
	, private base::Subscriber {
public:
	AuthSession(const MTPUser &user);

	AuthSession(const AuthSession &other) = delete;
	AuthSession &operator=(const AuthSession &other) = delete;

	static bool Exists();

	UserId userId() const {
		return _user->bareId();
	}
	PeerId userPeerId() const {
		return _user->id;
	}
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

	base::Observable<void> &downloaderTaskFinished();

	Window::Notifications::System &notifications() {
		return *_notifications;
	}

	Data::Session &data() {
		return *_data;
	}
	AuthSessionSettings &settings() {
		return _settings;
	}
	void moveSettingsFrom(AuthSessionSettings &&other);
	void saveSettingsDelayed(TimeMs delay = kDefaultSaveDelay);

	ApiWrap &api() {
		return *_api;
	}

	Calls::Instance &calls() {
		return *_calls;
	}

	void checkAutoLock();
	void checkAutoLockIn(TimeMs time);

	rpl::lifetime &lifetime() {
		return _lifetime;
	}

	base::Observable<DocumentData*> documentUpdated;
	base::Observable<std::pair<not_null<HistoryItem*>, MsgId>> messageIdChanging;

	bool supportMode() const;
	Support::Helper &supportHelper() const;
	Support::Templates &supportTemplates() const;

	~AuthSession();

private:
	static constexpr auto kDefaultSaveDelay = TimeMs(1000);

	const not_null<UserData*> _user;
	AuthSessionSettings _settings;
	base::Timer _saveDataTimer;

	TimeMs _shouldLockAt = 0;
	base::Timer _autoLockTimer;

	const std::unique_ptr<ApiWrap> _api;
	const std::unique_ptr<Calls::Instance> _calls;
	const std::unique_ptr<Storage::Downloader> _downloader;
	const std::unique_ptr<Storage::Uploader> _uploader;
	const std::unique_ptr<Storage::Facade> _storage;
	const std::unique_ptr<Window::Notifications::System> _notifications;

	// _data depends on _downloader / _uploader, including destructor.
	const std::unique_ptr<Data::Session> _data;

	// _changelogs depends on _data, subscribes on chats loading event.
	const std::unique_ptr<Core::Changelogs> _changelogs;

	const std::unique_ptr<Support::Helper> _supportHelper;

	rpl::lifetime _lifetime;

};
