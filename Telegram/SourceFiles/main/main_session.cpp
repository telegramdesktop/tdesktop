/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "main/main_session.h"

#include "apiwrap.h"
#include "core/application.h"
#include "core/changelogs.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "chat_helpers/stickers_emoji_pack.h"
#include "storage/file_download.h"
#include "storage/file_upload.h"
#include "storage/localstorage.h"
#include "storage/storage_facade.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "window/notifications_manager.h"
#include "window/themes/window_theme.h"
//#include "platform/platform_specific.h"
#include "calls/calls_instance.h"
#include "support/support_helper.h"
#include "observer_peer.h"

namespace Main {
namespace {

constexpr auto kAutoLockTimeoutLateMs = crl::time(3000);
constexpr auto kLegacyCallsPeerToPeerNobody = 4;

} // namespace

Session::Session(
	not_null<Main::Account*> account,
	const MTPUser &user)
: _account(account)
, _saveSettingsTimer([=] { Local::writeUserSettings(); })
, _autoLockTimer([=] { checkAutoLock(); })
, _api(std::make_unique<ApiWrap>(this))
, _appConfig(std::make_unique<AppConfig>(this))
, _calls(std::make_unique<Calls::Instance>(this))
, _downloader(std::make_unique<Storage::Downloader>(_api.get()))
, _uploader(std::make_unique<Storage::Uploader>(_api.get()))
, _storage(std::make_unique<Storage::Facade>())
, _notifications(std::make_unique<Window::Notifications::System>(this))
, _data(std::make_unique<Data::Session>(this))
, _user(_data->processUser(user))
, _emojiStickersPack(std::make_unique<Stickers::EmojiPack>(this))
, _changelogs(Core::Changelogs::Create(this))
, _supportHelper(Support::Helper::Create(this)) {
	Core::App().passcodeLockChanges(
	) | rpl::start_with_next([=] {
		_shouldLockAt = 0;
	}, _lifetime);
	Core::App().lockChanges(
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

Session::~Session() {
	ClickHandler::clearActive();
	ClickHandler::unpressed();
}

Main::Account &Session::account() const {
	return *_account;
}

bool Session::Exists() {
	return Core::IsAppLaunched()
		&& Core::App().activeAccount().sessionExists();
}

base::Observable<void> &Session::downloaderTaskFinished() {
	return downloader().taskFinished();
}

UserId Session::userId() const {
	return _user->bareId();
}

PeerId Session::userPeerId() const {
	return _user->id;
}

bool Session::validateSelf(const MTPUser &user) {
	if (user.type() != mtpc_user || !user.c_user().is_self()) {
		LOG(("API Error: bad self user received."));
		return false;
	} else if (user.c_user().vid().v != userId()) {
		LOG(("Auth Error: wrong self user received."));
		crl::on_main(this, [=] { _account->logOut(); });
		return false;
	}
	return true;
}

void Session::moveSettingsFrom(Settings &&other) {
	_settings.moveFrom(std::move(other));
	if (_settings.hadLegacyCallsPeerToPeerNobody()) {
		api().savePrivacy(
			MTP_inputPrivacyKeyPhoneP2P(),
			QVector<MTPInputPrivacyRule>(
				1,
				MTP_inputPrivacyValueDisallowAll()));
		saveSettingsDelayed();
	}
}

void Session::saveSettingsDelayed(crl::time delay) {
	Expects(this == &Auth());

	_saveSettingsTimer.callOnce(delay);
}

not_null<MTP::Instance*> Session::mtp() {
	return _account->mtp();
}

void Session::localPasscodeChanged() {
	_shouldLockAt = 0;
	_autoLockTimer.cancel();
	checkAutoLock();
}

void Session::termsDeleteNow() {
	api().request(MTPaccount_DeleteAccount(
		MTP_string("Decline ToS update")
	)).send();
}

void Session::checkAutoLock() {
	if (!Global::LocalPasscode()
		|| Core::App().passcodeLocked()) {
		_shouldLockAt = 0;
		_autoLockTimer.cancel();
		return;
	}

	Core::App().checkLocalTime();
	const auto now = crl::now();
	const auto shouldLockInMs = Global::AutoLock() * 1000LL;
	const auto checkTimeMs = now - Core::App().lastNonIdleTime();
	if (checkTimeMs >= shouldLockInMs || (_shouldLockAt > 0 && now > _shouldLockAt + kAutoLockTimeoutLateMs)) {
		_shouldLockAt = 0;
		_autoLockTimer.cancel();
		Core::App().lockByPasscode();
	} else {
		_shouldLockAt = now + (shouldLockInMs - checkTimeMs);
		_autoLockTimer.callOnce(shouldLockInMs - checkTimeMs);
	}
}

void Session::checkAutoLockIn(crl::time time) {
	if (_autoLockTimer.isActive()) {
		auto remain = _autoLockTimer.remainingTime();
		if (remain > 0 && remain <= time) return;
	}
	_autoLockTimer.callOnce(time);
}

bool Session::supportMode() const {
	return (_supportHelper != nullptr);
}

Support::Helper &Session::supportHelper() const {
	Expects(supportMode());

	return *_supportHelper;
}

Support::Templates& Session::supportTemplates() const {
	return supportHelper().templates();
}

} // namespace Main

Main::Session &Auth() {
	return Core::App().activeAccount().session();
}
