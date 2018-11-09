/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "auth_session.h"

#include "apiwrap.h"
#include "messenger.h"
#include "core/changelogs.h"
#include "storage/file_download.h"
#include "storage/file_upload.h"
#include "storage/localstorage.h"
#include "storage/storage_facade.h"
#include "storage/serialize_common.h"
#include "data/data_session.h"
#include "window/notifications_manager.h"
#include "window/themes/window_theme.h"
#include "platform/platform_specific.h"
#include "calls/calls_instance.h"
#include "window/section_widget.h"
#include "chat_helpers/tabbed_selector.h"
#include "boxes/send_files_box.h"
#include "ui/widgets/input_fields.h"
#include "support/support_common.h"
#include "support/support_templates.h"
#include "observer_peer.h"

namespace {

constexpr auto kAutoLockTimeoutLateMs = TimeMs(3000);

} // namespace

AuthSessionSettings::Variables::Variables()
: sendFilesWay(SendFilesWay::Album)
, selectorTab(ChatHelpers::SelectorTab::Emoji)
, floatPlayerColumn(Window::Column::Second)
, floatPlayerCorner(RectPart::TopRight)
, sendSubmitWay(Ui::InputSubmitSettings::Enter)
, supportSwitch(Support::SwitchSettings::Next) {
}

QByteArray AuthSessionSettings::serialize() const {
	auto size = sizeof(qint32) * 10;
	for (auto i = _variables.soundOverrides.cbegin(), e = _variables.soundOverrides.cend(); i != e; ++i) {
		size += Serialize::stringSize(i.key()) + Serialize::stringSize(i.value());
	}
	size += _variables.groupStickersSectionHidden.size() * sizeof(quint64);

	auto result = QByteArray();
	result.reserve(size);
	{
		QDataStream stream(&result, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		stream << static_cast<qint32>(_variables.selectorTab);
		stream << qint32(_variables.lastSeenWarningSeen ? 1 : 0);
		stream << qint32(_variables.tabbedSelectorSectionEnabled ? 1 : 0);
		stream << qint32(_variables.soundOverrides.size());
		for (auto i = _variables.soundOverrides.cbegin(), e = _variables.soundOverrides.cend(); i != e; ++i) {
			stream << i.key() << i.value();
		}
		stream << qint32(_variables.tabbedSelectorSectionTooltipShown);
		stream << qint32(_variables.floatPlayerColumn);
		stream << qint32(_variables.floatPlayerCorner);
		stream << qint32(_variables.groupStickersSectionHidden.size());
		for (auto peerId : _variables.groupStickersSectionHidden) {
			stream << quint64(peerId);
		}
		stream << qint32(_variables.thirdSectionInfoEnabled ? 1 : 0);
		stream << qint32(_variables.smallDialogsList ? 1 : 0);
		stream << qint32(snap(
			qRound(_variables.dialogsWidthRatio.current() * 1000000),
			0,
			1000000));
		stream << qint32(_variables.thirdColumnWidth.current());
		stream << qint32(_variables.thirdSectionExtendedBy);
		stream << qint32(_variables.sendFilesWay);
		stream << qint32(_variables.callsPeerToPeer.current());
		stream << qint32(_variables.sendSubmitWay);
		stream << qint32(_variables.supportSwitch);
		stream << qint32(_variables.supportFixChatsOrder ? 1 : 0);
		stream << qint32(_variables.supportTemplatesAutocomplete ? 1 : 0);
		stream << qint32(_variables.supportChatsTimeSlice.current());
	}
	return result;
}

void AuthSessionSettings::constructFromSerialized(const QByteArray &serialized) {
	if (serialized.isEmpty()) {
		return;
	}

	QDataStream stream(serialized);
	stream.setVersion(QDataStream::Qt_5_1);
	qint32 selectorTab = static_cast<qint32>(ChatHelpers::SelectorTab::Emoji);
	qint32 lastSeenWarningSeen = 0;
	qint32 tabbedSelectorSectionEnabled = 1;
	qint32 tabbedSelectorSectionTooltipShown = 0;
	qint32 floatPlayerColumn = static_cast<qint32>(Window::Column::Second);
	qint32 floatPlayerCorner = static_cast<qint32>(RectPart::TopRight);
	QMap<QString, QString> soundOverrides;
	base::flat_set<PeerId> groupStickersSectionHidden;
	qint32 thirdSectionInfoEnabled = 0;
	qint32 smallDialogsList = 0;
	float64 dialogsWidthRatio = _variables.dialogsWidthRatio.current();
	int thirdColumnWidth = _variables.thirdColumnWidth.current();
	int thirdSectionExtendedBy = _variables.thirdSectionExtendedBy;
	qint32 sendFilesWay = static_cast<qint32>(_variables.sendFilesWay);
	qint32 callsPeerToPeer = qint32(_variables.callsPeerToPeer.current());
	qint32 sendSubmitWay = static_cast<qint32>(_variables.sendSubmitWay);
	qint32 supportSwitch = static_cast<qint32>(_variables.supportSwitch);
	qint32 supportFixChatsOrder = _variables.supportFixChatsOrder ? 1 : 0;
	qint32 supportTemplatesAutocomplete = _variables.supportTemplatesAutocomplete ? 1 : 0;
	qint32 supportChatsTimeSlice = _variables.supportChatsTimeSlice.current();

	stream >> selectorTab;
	stream >> lastSeenWarningSeen;
	if (!stream.atEnd()) {
		stream >> tabbedSelectorSectionEnabled;
	}
	if (!stream.atEnd()) {
		auto count = qint32(0);
		stream >> count;
		if (stream.status() == QDataStream::Ok) {
			for (auto i = 0; i != count; ++i) {
				QString key, value;
				stream >> key >> value;
				soundOverrides[key] = value;
			}
		}
	}
	if (!stream.atEnd()) {
		stream >> tabbedSelectorSectionTooltipShown;
	}
	if (!stream.atEnd()) {
		stream >> floatPlayerColumn >> floatPlayerCorner;
	}
	if (!stream.atEnd()) {
		auto count = qint32(0);
		stream >> count;
		if (stream.status() == QDataStream::Ok) {
			for (auto i = 0; i != count; ++i) {
				quint64 peerId;
				stream >> peerId;
				groupStickersSectionHidden.insert(peerId);
			}
		}
	}
	if (!stream.atEnd()) {
		stream >> thirdSectionInfoEnabled;
		stream >> smallDialogsList;
	}
	if (!stream.atEnd()) {
		qint32 value = 0;
		stream >> value;
		dialogsWidthRatio = snap(value / 1000000., 0., 1.);

		stream >> value;
		thirdColumnWidth = value;

		stream >> value;
		thirdSectionExtendedBy = value;
	}
	if (!stream.atEnd()) {
		stream >> sendFilesWay;
	}
	if (!stream.atEnd()) {
		stream >> callsPeerToPeer;
	}
	if (!stream.atEnd()) {
		stream >> sendSubmitWay;
		stream >> supportSwitch;
		stream >> supportFixChatsOrder;
	}
	if (!stream.atEnd()) {
		stream >> supportTemplatesAutocomplete;
	}
	if (!stream.atEnd()) {
		stream >> supportChatsTimeSlice;
	}
	if (stream.status() != QDataStream::Ok) {
		LOG(("App Error: "
			"Bad data for AuthSessionSettings::constructFromSerialized()"));
		return;
	}

	auto uncheckedTab = static_cast<ChatHelpers::SelectorTab>(selectorTab);
	switch (uncheckedTab) {
	case ChatHelpers::SelectorTab::Emoji:
	case ChatHelpers::SelectorTab::Stickers:
	case ChatHelpers::SelectorTab::Gifs: _variables.selectorTab = uncheckedTab; break;
	}
	_variables.lastSeenWarningSeen = (lastSeenWarningSeen == 1);
	_variables.tabbedSelectorSectionEnabled = (tabbedSelectorSectionEnabled == 1);
	_variables.soundOverrides = std::move(soundOverrides);
	_variables.tabbedSelectorSectionTooltipShown = tabbedSelectorSectionTooltipShown;
	auto uncheckedColumn = static_cast<Window::Column>(floatPlayerColumn);
	switch (uncheckedColumn) {
	case Window::Column::First:
	case Window::Column::Second:
	case Window::Column::Third: _variables.floatPlayerColumn = uncheckedColumn; break;
	}
	auto uncheckedCorner = static_cast<RectPart>(floatPlayerCorner);
	switch (uncheckedCorner) {
	case RectPart::TopLeft:
	case RectPart::TopRight:
	case RectPart::BottomLeft:
	case RectPart::BottomRight: _variables.floatPlayerCorner = uncheckedCorner; break;
	}
	_variables.groupStickersSectionHidden = std::move(groupStickersSectionHidden);
	_variables.thirdSectionInfoEnabled = thirdSectionInfoEnabled;
	_variables.smallDialogsList = smallDialogsList;
	_variables.dialogsWidthRatio = dialogsWidthRatio;
	_variables.thirdColumnWidth = thirdColumnWidth;
	_variables.thirdSectionExtendedBy = thirdSectionExtendedBy;
	if (_variables.thirdSectionInfoEnabled) {
		_variables.tabbedSelectorSectionEnabled = false;
	}
	auto uncheckedSendFilesWay = static_cast<SendFilesWay>(sendFilesWay);
	switch (uncheckedSendFilesWay) {
	case SendFilesWay::Album:
	case SendFilesWay::Photos:
	case SendFilesWay::Files: _variables.sendFilesWay = uncheckedSendFilesWay; break;
	}
	auto uncheckedCallsPeerToPeer = static_cast<Calls::PeerToPeer>(callsPeerToPeer);
	switch (uncheckedCallsPeerToPeer) {
	case Calls::PeerToPeer::DefaultContacts:
	case Calls::PeerToPeer::DefaultEveryone:
	case Calls::PeerToPeer::Everyone:
	case Calls::PeerToPeer::Contacts:
	case Calls::PeerToPeer::Nobody: _variables.callsPeerToPeer = uncheckedCallsPeerToPeer; break;
	}
	auto uncheckedSendSubmitWay = static_cast<Ui::InputSubmitSettings>(
		sendSubmitWay);
	switch (uncheckedSendSubmitWay) {
	case Ui::InputSubmitSettings::Enter:
	case Ui::InputSubmitSettings::CtrlEnter: _variables.sendSubmitWay = uncheckedSendSubmitWay; break;
	}
	auto uncheckedSupportSwitch = static_cast<Support::SwitchSettings>(
		supportSwitch);
	switch (uncheckedSupportSwitch) {
	case Support::SwitchSettings::None:
	case Support::SwitchSettings::Next:
	case Support::SwitchSettings::Previous: _variables.supportSwitch = uncheckedSupportSwitch; break;
	}
	_variables.supportFixChatsOrder = (supportFixChatsOrder == 1);
	_variables.supportTemplatesAutocomplete = (supportTemplatesAutocomplete == 1);
	_variables.supportChatsTimeSlice = supportChatsTimeSlice;
}

void AuthSessionSettings::setSupportChatsTimeSlice(int slice) {
	_variables.supportChatsTimeSlice = slice;
}

int AuthSessionSettings::supportChatsTimeSlice() const {
	return _variables.supportChatsTimeSlice.current();
}

rpl::producer<int> AuthSessionSettings::supportChatsTimeSliceValue() const {
	return _variables.supportChatsTimeSlice.value();
}

void AuthSessionSettings::setTabbedSelectorSectionEnabled(bool enabled) {
	_variables.tabbedSelectorSectionEnabled = enabled;
	if (enabled) {
		setThirdSectionInfoEnabled(false);
	}
	setTabbedReplacedWithInfo(false);
}

rpl::producer<bool> AuthSessionSettings::tabbedReplacedWithInfoValue() const {
	return _tabbedReplacedWithInfoValue.events_starting_with(
		tabbedReplacedWithInfo());
}

void AuthSessionSettings::setThirdSectionInfoEnabled(bool enabled) {
	if (_variables.thirdSectionInfoEnabled != enabled) {
		_variables.thirdSectionInfoEnabled = enabled;
		if (enabled) {
			setTabbedSelectorSectionEnabled(false);
		}
		setTabbedReplacedWithInfo(false);
		_thirdSectionInfoEnabledValue.fire_copy(enabled);
	}
}

rpl::producer<bool> AuthSessionSettings::thirdSectionInfoEnabledValue() const {
	return _thirdSectionInfoEnabledValue.events_starting_with(
		thirdSectionInfoEnabled());
}

void AuthSessionSettings::setTabbedReplacedWithInfo(bool enabled) {
	if (_tabbedReplacedWithInfo != enabled) {
		_tabbedReplacedWithInfo = enabled;
		_tabbedReplacedWithInfoValue.fire_copy(enabled);
	}
}

QString AuthSessionSettings::getSoundPath(const QString &key) const {
	auto it = _variables.soundOverrides.constFind(key);
	if (it != _variables.soundOverrides.end()) {
		return it.value();
	}
	return qsl(":/sounds/") + key + qsl(".mp3");
}

void AuthSessionSettings::setDialogsWidthRatio(float64 ratio) {
	_variables.dialogsWidthRatio = ratio;
}

float64 AuthSessionSettings::dialogsWidthRatio() const {
	return _variables.dialogsWidthRatio.current();
}

rpl::producer<float64> AuthSessionSettings::dialogsWidthRatioChanges() const {
	return _variables.dialogsWidthRatio.changes();
}

void AuthSessionSettings::setThirdColumnWidth(int width) {
	_variables.thirdColumnWidth = width;
}

int AuthSessionSettings::thirdColumnWidth() const {
	return _variables.thirdColumnWidth.current();
}

rpl::producer<int> AuthSessionSettings::thirdColumnWidthChanges() const {
	return _variables.thirdColumnWidth.changes();
}

AuthSession &Auth() {
	auto result = Messenger::Instance().authSession();
	Assert(result != nullptr);
	return *result;
}

AuthSession::AuthSession(const MTPUser &user)
: _user(App::user(user.match([](const auto &data) { return data.vid.v; })))
, _autoLockTimer([this] { checkAutoLock(); })
, _api(std::make_unique<ApiWrap>(this))
, _calls(std::make_unique<Calls::Instance>())
, _downloader(std::make_unique<Storage::Downloader>())
, _uploader(std::make_unique<Storage::Uploader>())
, _storage(std::make_unique<Storage::Facade>())
, _notifications(std::make_unique<Window::Notifications::System>(this))
, _data(std::make_unique<Data::Session>(this))
, _changelogs(Core::Changelogs::Create(this))
, _supportTemplates(
	(Support::ValidateAccount(user)
		? std::make_unique<Support::Templates>(this)
		: nullptr)) {
	App::feedUser(user);

	_saveDataTimer.setCallback([=] {
		Local::writeUserSettings();
	});
	Messenger::Instance().passcodeLockChanges(
	) | rpl::start_with_next([=] {
		_shouldLockAt = 0;
	}, _lifetime);
	Messenger::Instance().lockChanges(
	) | rpl::start_with_next([=] {
		notifications().updateAll();
	}, _lifetime);
	subscribe(Global::RefConnectionTypeChanged(), [=] {
		_api->refreshProxyPromotion();
	});
	_api->refreshProxyPromotion();
	_api->requestTermsUpdate();
	_api->requestFullPeer(_user);

	crl::on_main(this, [=] {
		using Flag = Notify::PeerUpdate::Flag;
		const auto events = Flag::NameChanged
			| Flag::UsernameChanged
			| Flag::PhotoChanged
			| Flag::AboutChanged
			| Flag::UserPhoneChanged;
		subscribe(
			Notify::PeerUpdated(),
			Notify::PeerUpdatedHandler(
				events,
				[=](const Notify::PeerUpdate &update) {
					if (update.peer == _user) {
						Local::writeSelf();
					}
				}));
	});

	Window::Theme::Background()->start();
}

bool AuthSession::Exists() {
	if (const auto messenger = Messenger::InstancePointer()) {
		return (messenger->authSession() != nullptr);
	}
	return false;
}

base::Observable<void> &AuthSession::downloaderTaskFinished() {
	return downloader().taskFinished();
}

bool AuthSession::validateSelf(const MTPUser &user) {
	if (user.type() != mtpc_user || !user.c_user().is_self()) {
		LOG(("API Error: bad self user received."));
		return false;
	} else if (user.c_user().vid.v != userId()) {
		LOG(("Auth Error: wrong self user received."));
		crl::on_main(this, [] { Messenger::Instance().logOut(); });
		return false;
	}
	return true;
}

void AuthSession::saveSettingsDelayed(TimeMs delay) {
	Expects(this == &Auth());

	_saveDataTimer.callOnce(delay);
}

void AuthSession::checkAutoLock() {
	if (!Global::LocalPasscode()
		|| Messenger::Instance().passcodeLocked()) {
		return;
	}

	Messenger::Instance().checkLocalTime();
	auto now = getms(true);
	auto shouldLockInMs = Global::AutoLock() * 1000LL;
	auto idleForMs = psIdleTime();
	auto notPlayingVideoForMs = now - settings().lastTimeVideoPlayedAt();
	auto checkTimeMs = qMin(idleForMs, notPlayingVideoForMs);
	if (checkTimeMs >= shouldLockInMs || (_shouldLockAt > 0 && now > _shouldLockAt + kAutoLockTimeoutLateMs)) {
		Messenger::Instance().lockByPasscode();
	} else {
		_shouldLockAt = now + (shouldLockInMs - checkTimeMs);
		_autoLockTimer.callOnce(shouldLockInMs - checkTimeMs);
	}
}

void AuthSession::checkAutoLockIn(TimeMs time) {
	if (_autoLockTimer.isActive()) {
		auto remain = _autoLockTimer.remainingTime();
		if (remain > 0 && remain <= time) return;
	}
	_autoLockTimer.callOnce(time);
}

bool AuthSession::supportMode() const {
	return (_supportTemplates != nullptr);
}

not_null<Support::Templates*> AuthSession::supportTemplates() const {
	Expects(supportMode());

	return _supportTemplates.get();
}

AuthSession::~AuthSession() = default;
