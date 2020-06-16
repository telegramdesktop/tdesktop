/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "main/main_session.h"

#include "apiwrap.h"
#include "api/api_updates.h"
#include "core/application.h"
#include "core/changelogs.h"
#include "main/main_account.h"
#include "chat_helpers/stickers_emoji_pack.h"
#include "chat_helpers/stickers_dice_pack.h"
#include "storage/file_download.h"
#include "storage/download_manager_mtproto.h"
#include "storage/file_upload.h"
#include "storage/storage_account.h"
#include "storage/storage_facade.h"
#include "storage/storage_account.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "data/data_user.h"
#include "window/notifications_manager.h"
#include "window/window_session_controller.h"
#include "window/themes/window_theme.h"
//#include "platform/platform_specific.h"
#include "calls/calls_instance.h"
#include "support/support_helper.h"
#include "facades.h"

#ifndef TDESKTOP_DISABLE_SPELLCHECK
#include "chat_helpers/spellchecker_common.h"
#endif // TDESKTOP_DISABLE_SPELLCHECK

namespace Main {
namespace {

constexpr auto kLegacyCallsPeerToPeerNobody = 4;

} // namespace

Session::Session(
	not_null<Main::Account*> account,
	const MTPUser &user,
	Settings &&settings)
: _account(account)
, _settings(std::move(settings))
, _saveSettingsTimer([=] { local().writeSettings(); })
, _api(std::make_unique<ApiWrap>(this))
, _updates(std::make_unique<Api::Updates>(this))
, _calls(std::make_unique<Calls::Instance>(this))
, _downloader(std::make_unique<Storage::DownloadManagerMtproto>(_api.get()))
, _uploader(std::make_unique<Storage::Uploader>(_api.get()))
, _storage(std::make_unique<Storage::Facade>())
, _notifications(std::make_unique<Window::Notifications::System>(this))
, _changes(std::make_unique<Data::Changes>(this))
, _data(std::make_unique<Data::Session>(this))
, _user(_data->processUser(user))
, _emojiStickersPack(std::make_unique<Stickers::EmojiPack>(this))
, _diceStickersPacks(std::make_unique<Stickers::DicePacks>(this))
, _changelogs(Core::Changelogs::Create(this))
, _supportHelper(Support::Helper::Create(this)) {
	Core::App().lockChanges(
	) | rpl::start_with_next([=] {
		notifications().updateAll();
	}, _lifetime);

	subscribe(Global::RefConnectionTypeChanged(), [=] {
		_api->refreshTopPromotion();
	});
	_api->refreshTopPromotion();
	_api->requestTermsUpdate();
	_api->requestFullPeer(_user);

	_api->instance()->setUserPhone(_user->phone());

	crl::on_main(this, [=] {
		using Flag = Data::PeerUpdate::Flag;
		changes().peerUpdates(
			_user,
			Flag::Name
			| Flag::Username
			| Flag::Photo
			| Flag::About
			| Flag::PhoneNumber
		) | rpl::start_with_next([=](const Data::PeerUpdate &update) {
			local().writeSelf();

			if (update.flags & Flag::PhoneNumber) {
				const auto phone = _user->phone();
				_api->instance()->setUserPhone(phone);
				if (!phone.isEmpty()) {
					_api->instance()->requestConfig();
				}
			}
		}, _lifetime);

		if (_settings.hadLegacyCallsPeerToPeerNobody()) {
			api().savePrivacy(
				MTP_inputPrivacyKeyPhoneP2P(),
				QVector<MTPInputPrivacyRule>(
					1,
					MTP_inputPrivacyValueDisallowAll()));
			saveSettingsDelayed();
		}
	});

#ifndef TDESKTOP_DISABLE_SPELLCHECK
	Spellchecker::Start(this);
#endif // TDESKTOP_DISABLE_SPELLCHECK
}

Session::~Session() {
	ClickHandler::clearActive();
	ClickHandler::unpressed();
}

Main::Account &Session::account() const {
	return *_account;
}

Storage::Account &Session::local() const {
	return _account->local();
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

void Session::saveSettingsDelayed(crl::time delay) {
	_saveSettingsTimer.callOnce(delay);
}

MTP::DcId Session::mainDcId() const {
	return _account->mtp()->mainDcId();
}

not_null<MTP::Instance*> Session::mtp() const {
	return _account->mtp();
}

void Session::termsDeleteNow() {
	api().request(MTPaccount_DeleteAccount(
		MTP_string("Decline ToS update")
	)).send();
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

void Session::saveSettingsNowIfNeeded() {
	if (_saveSettingsTimer.isActive()) {
		_saveSettingsTimer.cancel();
		local().writeSettings();
	}
}

void Session::addWindow(not_null<Window::SessionController*> controller) {
	_windows.emplace(controller);
	controller->lifetime().add([=] {
		_windows.remove(controller);
	});
	updates().addActiveChat(controller->activeChatChanges(
	) | rpl::map([=](const Dialogs::Key &chat) {
		return chat.peer();
	}) | rpl::distinct_until_changed());
}

auto Session::windows() const
-> const base::flat_set<not_null<Window::SessionController*>> & {
	return _windows;
}

} // namespace Main
