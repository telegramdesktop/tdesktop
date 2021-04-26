/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "main/main_session.h"

#include "apiwrap.h"
#include "api/api_updates.h"
#include "api/api_send_progress.h"
#include "core/application.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session_settings.h"
#include "mtproto/mtproto_config.h"
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
#include "data/stickers/data_stickers.h"
#include "window/window_session_controller.h"
#include "window/window_lock_widgets.h"
#include "window/themes/window_theme.h"
//#include "platform/platform_specific.h"
#include "base/unixtime.h"
#include "calls/calls_instance.h"
#include "support/support_helper.h"
#include "facades.h"

#ifndef TDESKTOP_DISABLE_SPELLCHECK
#include "chat_helpers/spellchecker_common.h"
#endif // TDESKTOP_DISABLE_SPELLCHECK

namespace Main {
namespace {

constexpr auto kLegacyCallsPeerToPeerNobody = 4;
constexpr auto kTmpPasswordReserveTime = TimeId(10);

[[nodiscard]] QString ValidatedInternalLinksDomain(
		not_null<const Session*> session) {
	// This domain should start with 'http[s]://' and end with '/'.
	// Like 'https://telegram.me/' or 'https://t.me/'.
	const auto &domain = session->serverConfig().internalLinksDomain;
	const auto prefixes = {
		qstr("https://"),
		qstr("http://"),
	};
	for (const auto &prefix : prefixes) {
		if (domain.startsWith(prefix, Qt::CaseInsensitive)) {
			return domain.endsWith('/')
				? domain
				: MTP::ConfigFields().internalLinksDomain;
		}
	}
	return MTP::ConfigFields().internalLinksDomain;
}

} // namespace

Session::Session(
	not_null<Account*> account,
	const MTPUser &user,
	std::unique_ptr<SessionSettings> settings)
: _account(account)
, _settings(std::move(settings))
, _api(std::make_unique<ApiWrap>(this))
, _updates(std::make_unique<Api::Updates>(this))
, _sendProgressManager(std::make_unique<Api::SendProgressManager>(this))
, _downloader(std::make_unique<Storage::DownloadManagerMtproto>(_api.get()))
, _uploader(std::make_unique<Storage::Uploader>(_api.get()))
, _storage(std::make_unique<Storage::Facade>())
, _changes(std::make_unique<Data::Changes>(this))
, _data(std::make_unique<Data::Session>(this))
, _user(_data->processUser(user))
, _emojiStickersPack(std::make_unique<Stickers::EmojiPack>(this))
, _diceStickersPacks(std::make_unique<Stickers::DicePacks>(this))
, _supportHelper(Support::Helper::Create(this))
, _saveSettingsTimer([=] { saveSettings(); }) {
	Expects(_settings != nullptr);

	subscribe(Global::RefConnectionTypeChanged(), [=] {
		_api->refreshTopPromotion();
	});
	_api->refreshTopPromotion();
	_api->requestTermsUpdate();
	_api->requestFullPeer(_user);

	_api->instance().setUserPhone(_user->phone());

	// Load current userpic and keep it loaded.
	_user->loadUserpic();
	changes().peerFlagsValue(
		_user,
		Data::PeerUpdate::Flag::Photo
	) | rpl::start_with_next([=] {
		[[maybe_unused]] const auto image = _user->currentUserpic(
			_selfUserpicView);
	}, lifetime());

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
				_api->instance().setUserPhone(phone);
				if (!phone.isEmpty()) {
					_api->instance().requestConfig();
				}
			}
		}, _lifetime);

		if (_settings->hadLegacyCallsPeerToPeerNobody()) {
			api().savePrivacy(
				MTP_inputPrivacyKeyPhoneP2P(),
				QVector<MTPInputPrivacyRule>(
					1,
					MTP_inputPrivacyValueDisallowAll()));
			saveSettingsDelayed();
		}

		// Storage::Account uses Main::Account::session() in those methods.
		// So they can't be called during Main::Session construction.
		local().readInstalledStickers();
		local().readFeaturedStickers();
		local().readRecentStickers();
		local().readFavedStickers();
		local().readSavedGifs();
		data().stickers().notifyUpdated();
		data().stickers().notifySavedGifsUpdated();
	});

#ifndef TDESKTOP_DISABLE_SPELLCHECK
	Spellchecker::Start(this);
#endif // TDESKTOP_DISABLE_SPELLCHECK

	_api->requestNotifySettings(MTP_inputNotifyUsers());
	_api->requestNotifySettings(MTP_inputNotifyChats());
	_api->requestNotifySettings(MTP_inputNotifyBroadcasts());
}

void Session::setTmpPassword(const QByteArray &password, TimeId validUntil) {
	if (_tmpPassword.isEmpty() || validUntil > _tmpPasswordValidUntil) {
		_tmpPassword = password;
		_tmpPasswordValidUntil = validUntil;
	}
}

QByteArray Session::validTmpPassword() const {
	return (_tmpPasswordValidUntil
		>= base::unixtime::now() + kTmpPasswordReserveTime)
		? _tmpPassword
		: QByteArray();
}

// Can be called only right before ~Session.
void Session::finishLogout() {
	updates().updateOnline();
	unlockTerms();
	data().clear();
	data().clearLocalStorage();
}

Session::~Session() {
	unlockTerms();
	data().clear();
	ClickHandler::clearActive();
	ClickHandler::unpressed();
}

Account &Session::account() const {
	return *_account;
}

Storage::Account &Session::local() const {
	return _account->local();
}

Domain &Session::domain() const {
	return _account->domain();
}

Storage::Domain &Session::domainLocal() const {
	return _account->domainLocal();
}

void Session::notifyDownloaderTaskFinished() {
	downloader().notifyTaskFinished();
}

rpl::producer<> Session::downloaderTaskFinished() const {
	return downloader().taskFinished();
}

uint64 Session::uniqueId() const {
	// See also Account::willHaveSessionUniqueId.
	return userId().bare
		| (mtp().isTestMode() ? 0x0100'0000'0000'0000ULL : 0ULL);
}

UserId Session::userId() const {
	return peerToUser(_user->id);
}

PeerId Session::userPeerId() const {
	return _user->id;
}

bool Session::validateSelf(const MTPUser &user) {
	if (user.type() != mtpc_user || !user.c_user().is_self()) {
		LOG(("API Error: bad self user received."));
		return false;
	} else if (UserId(user.c_user().vid()) != userId()) {
		LOG(("Auth Error: wrong self user received."));
		crl::on_main(this, [=] { _account->logOut(); });
		return false;
	}
	return true;
}

void Session::saveSettings() {
	local().writeSessionSettings();
}

void Session::saveSettingsDelayed(crl::time delay) {
	_saveSettingsTimer.callOnce(delay);
}

void Session::saveSettingsNowIfNeeded() {
	if (_saveSettingsTimer.isActive()) {
		_saveSettingsTimer.cancel();
		saveSettings();
	}
}

MTP::DcId Session::mainDcId() const {
	return _account->mtp().mainDcId();
}

MTP::Instance &Session::mtp() const {
	return _account->mtp();
}

const MTP::ConfigFields &Session::serverConfig() const {
	return _account->mtp().configValues();
}

void Session::lockByTerms(const Window::TermsLock &data) {
	if (!_termsLock || *_termsLock != data) {
		_termsLock = std::make_unique<Window::TermsLock>(data);
		_termsLockChanges.fire(true);
	}
}

void Session::unlockTerms() {
	if (_termsLock) {
		_termsLock = nullptr;
		_termsLockChanges.fire(false);
	}
}

void Session::termsDeleteNow() {
	api().request(MTPaccount_DeleteAccount(
		MTP_string("Decline ToS update")
	)).send();
}

std::optional<Window::TermsLock> Session::termsLocked() const {
	return _termsLock ? base::make_optional(*_termsLock) : std::nullopt;
}

rpl::producer<bool> Session::termsLockChanges() const {
	return _termsLockChanges.events();
}

rpl::producer<bool> Session::termsLockValue() const {
	return rpl::single(
		_termsLock != nullptr
	) | rpl::then(termsLockChanges());
}

QString Session::createInternalLink(const QString &query) const {
	auto result = createInternalLinkFull(query);
	auto prefixes = {
		qstr("https://"),
		qstr("http://"),
	};
	for (auto &prefix : prefixes) {
		if (result.startsWith(prefix, Qt::CaseInsensitive)) {
			return result.mid(prefix.size());
		}
	}
	LOG(("Warning: bad internal url '%1'").arg(result));
	return result;
}

QString Session::createInternalLinkFull(const QString &query) const {
	return ValidatedInternalLinksDomain(this) + query;
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
