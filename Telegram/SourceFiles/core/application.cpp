/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/application.h"

#include "data/data_photo.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "base/timer.h"
#include "core/update_checker.h"
#include "core/shortcuts.h"
#include "core/sandbox.h"
#include "core/local_url_handlers.h"
#include "core/launcher.h"
#include "storage/localstorage.h"
#include "platform/platform_specific.h"
#include "mainwindow.h"
#include "dialogs/dialogs_entry.h"
#include "history/history.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "calls/calls_instance.h"
#include "lang/lang_file_parser.h"
#include "lang/lang_translator.h"
#include "lang/lang_cloud_manager.h"
#include "lang/lang_hardcoded.h"
#include "observer_peer.h"
#include "storage/storage_databases.h"
#include "mainwidget.h"
#include "mediaview.h"
#include "mtproto/dc_options.h"
#include "mtproto/mtp_instance.h"
#include "media/player/media_player_instance.h"
#include "media/media_audio.h"
#include "media/media_audio_track.h"
#include "window/notifications_manager.h"
#include "window/themes/window_theme.h"
#include "window/window_lock_widgets.h"
#include "history/history_location_manager.h"
#include "ui/widgets/tooltip.h"
#include "ui/image/image.h"
#include "ui/text_options.h"
#include "ui/emoji_config.h"
#include "storage/serialize_common.h"
#include "window/window_controller.h"
#include "base/qthelp_regex.h"
#include "base/qthelp_url.h"
#include "boxes/connection_box.h"
#include "boxes/confirm_phone_box.h"
#include "boxes/confirm_box.h"
#include "boxes/share_box.h"

namespace Core {
namespace {

constexpr auto kQuitPreventTimeoutMs = 1500;

} // namespace

struct Application::Private {
	UserId authSessionUserId = 0;
	QByteArray authSessionUserSerialized;
	int32 authSessionUserStreamVersion = 0;
	std::unique_ptr<AuthSessionSettings> storedAuthSession;
	MTP::Instance::Config mtpConfig;
	MTP::AuthKeysList mtpKeysToDestroy;
	base::Timer quitTimer;
};

Application::Application(not_null<Launcher*> launcher)
: QObject()
, _launcher(launcher)
, _private(std::make_unique<Private>())
, _databases(std::make_unique<Storage::Databases>())
, _langpack(std::make_unique<Lang::Instance>())
, _audio(std::make_unique<Media::Audio::Instance>())
, _logo(Window::LoadLogo())
, _logoNoMargin(Window::LoadLogoNoMargin()) {
	Expects(!_logo.isNull());
	Expects(!_logoNoMargin.isNull());
}

void Application::run() {
	Fonts::Start();

	ThirdParty::start();
	Global::start();
	refreshGlobalProxy(); // Depends on Global::started().

	startLocalStorage();

	if (Local::oldSettingsVersion() < AppVersion) {
		psNewVersion();
	}

	if (cLaunchMode() == LaunchModeAutoStart && !cAutoStart()) {
		psAutoStart(false, true);
		App::quit();
		return;
	}

	_translator = std::make_unique<Lang::Translator>();
	QCoreApplication::instance()->installTranslator(_translator.get());

	style::startManager();
	anim::startManager();
	Ui::InitTextOptions();
	Ui::Emoji::Init();
	Media::Player::start(_audio.get());

	DEBUG_LOG(("Application Info: inited..."));

	QCoreApplication::instance()->installNativeEventFilter(
		psNativeEventFilter());

	cChangeTimeFormat(QLocale::system().timeFormat(QLocale::ShortFormat));

	DEBUG_LOG(("Application Info: starting app..."));

	// Create mime database, so it won't be slow later.
	QMimeDatabase().mimeTypeForName(qsl("text/plain"));

	_window = std::make_unique<MainWindow>();
	_window->init();

	auto currentGeometry = _window->geometry();
	_mediaView = std::make_unique<MediaView>();
	_window->setGeometry(currentGeometry);

	QCoreApplication::instance()->installEventFilter(this);
	connect(
		static_cast<QGuiApplication*>(QCoreApplication::instance()),
		&QGuiApplication::applicationStateChanged,
		this,
		&Application::stateChanged);

	DEBUG_LOG(("Application Info: window created..."));

	startShortcuts();
	App::initMedia();

	Local::ReadMapState state = Local::readMap(QByteArray());
	if (state == Local::ReadMapPassNeeded) {
		Global::SetLocalPasscode(true);
		Global::RefLocalPasscodeChanged().notify();
		lockByPasscode();
		DEBUG_LOG(("Application Info: passcode needed..."));
	} else {
		DEBUG_LOG(("Application Info: local map read..."));
		startMtp();
		DEBUG_LOG(("Application Info: MTP started..."));
		if (AuthSession::Exists()) {
			_window->setupMain();
		} else {
			_window->setupIntro();
		}
	}
	DEBUG_LOG(("Application Info: showing."));
	_window->firstShow();

	if (cStartToSettings()) {
		_window->showSettings();
	}

	_window->updateIsActive(Global::OnlineFocusTimeout());

	for (const auto &error : Shortcuts::Errors()) {
		LOG(("Shortcuts Error: %1").arg(error));
	}
}

bool Application::hideMediaView() {
	if (_mediaView && !_mediaView->isHidden()) {
		_mediaView->hide();
		if (auto activeWindow = getActiveWindow()) {
			activeWindow->reActivateWindow();
		}
		return true;
	}
	return false;
}

void Application::showPhoto(not_null<const PhotoOpenClickHandler*> link) {
	const auto item = App::histItemById(link->context());
	const auto peer = link->peer();
	return (!item && peer)
		? showPhoto(link->photo(), peer)
		: showPhoto(link->photo(), item);
}

void Application::showPhoto(not_null<PhotoData*> photo, HistoryItem *item) {
	_mediaView->showPhoto(photo, item);
	_mediaView->activateWindow();
	_mediaView->setFocus();
}

void Application::showPhoto(
		not_null<PhotoData*> photo,
		not_null<PeerData*> peer) {
	_mediaView->showPhoto(photo, peer);
	_mediaView->activateWindow();
	_mediaView->setFocus();
}

void Application::showDocument(not_null<DocumentData*> document, HistoryItem *item) {
	if (cUseExternalVideoPlayer() && document->isVideoFile()) {
		QDesktopServices::openUrl(QUrl("file:///" + document->location(false).fname));
	} else {
		_mediaView->showDocument(document, item);
		_mediaView->activateWindow();
		_mediaView->setFocus();
	}
}

PeerData *Application::ui_getPeerForMouseAction() {
	if (_mediaView && !_mediaView->isHidden()) {
		return _mediaView->ui_getPeerForMouseAction();
	} else if (auto main = App::main()) {
		return main->ui_getPeerForMouseAction();
	}
	return nullptr;
}

bool Application::eventFilter(QObject *object, QEvent *e) {
	switch (e->type()) {
	case QEvent::KeyPress:
	case QEvent::MouseButtonPress:
	case QEvent::TouchBegin:
	case QEvent::Wheel: {
		psUserActionDone();
	} break;

	case QEvent::ShortcutOverride: {
		// handle shortcuts ourselves
		return true;
	} break;

	case QEvent::Shortcut: {
		const auto event = static_cast<QShortcutEvent*>(e);
		DEBUG_LOG(("Shortcut event caught: %1"
			).arg(event->key().toString()));
		if (Shortcuts::HandleEvent(event)) {
			return true;
		}
	} break;

	case QEvent::ApplicationActivate: {
		if (object == QCoreApplication::instance()) {
			psUserActionDone();
		}
	} break;

	case QEvent::FileOpen: {
		if (object == QCoreApplication::instance()) {
			const auto event = static_cast<QFileOpenEvent*>(e);
			const auto url = QString::fromUtf8(
				event->url().toEncoded().trimmed());
			if (url.startsWith(qstr("tg://"), Qt::CaseInsensitive)) {
				cSetStartUrl(url.mid(0, 8192));
				checkStartUrl();
			}
			if (StartUrlRequiresActivate(url)) {
				_window->activate();
			}
		}
	} break;
	}

	return QObject::eventFilter(object, e);
}

void Application::setCurrentProxy(
		const ProxyData &proxy,
		ProxyData::Settings settings) {
	const auto key = [&](const ProxyData &proxy) {
		if (proxy.type == ProxyData::Type::Mtproto) {
			return std::make_pair(proxy.host, proxy.port);
		}
		return std::make_pair(QString(), uint32(0));
	};
	const auto previousKey = key(
		(Global::ProxySettings() == ProxyData::Settings::Enabled
			? Global::SelectedProxy()
			: ProxyData()));
	Global::SetSelectedProxy(proxy);
	Global::SetProxySettings(settings);
	refreshGlobalProxy();
	if (_mtproto) {
		_mtproto->restart();
		if (previousKey != key(proxy)) {
			_mtproto->reInitConnection(_mtproto->mainDcId());
		}
	}
	if (_mtprotoForKeysDestroy) {
		_mtprotoForKeysDestroy->restart();
	}
	Global::RefConnectionTypeChanged().notify();
}

void Application::badMtprotoConfigurationError() {
	if (Global::ProxySettings() == ProxyData::Settings::Enabled
		&& !_badProxyDisableBox) {
		const auto disableCallback = [=] {
			setCurrentProxy(
				Global::SelectedProxy(),
				ProxyData::Settings::System);
		};
		_badProxyDisableBox = Ui::show(Box<InformBox>(
			Lang::Hard::ProxyConfigError(),
			disableCallback));
	}
}

void Application::setMtpMainDcId(MTP::DcId mainDcId) {
	Expects(!_mtproto);

	_private->mtpConfig.mainDcId = mainDcId;
}

void Application::setMtpKey(MTP::DcId dcId, const MTP::AuthKey::Data &keyData) {
	Expects(!_mtproto);

	_private->mtpConfig.keys.push_back(std::make_shared<MTP::AuthKey>(MTP::AuthKey::Type::ReadFromFile, dcId, keyData));
}

QByteArray Application::serializeMtpAuthorization() const {
	auto serialize = [this](auto mainDcId, auto &keys, auto &keysToDestroy) {
		auto keysSize = [](auto &list) {
			return sizeof(qint32) + list.size() * (sizeof(qint32) + MTP::AuthKey::Data().size());
		};
		auto writeKeys = [](QDataStream &stream, auto &keys) {
			stream << qint32(keys.size());
			for (auto &key : keys) {
				stream << qint32(key->dcId());
				key->write(stream);
			}
		};

		auto result = QByteArray();
		auto size = sizeof(qint32) + sizeof(qint32); // userId + mainDcId
		size += keysSize(keys) + keysSize(keysToDestroy);
		result.reserve(size);
		{
			QDataStream stream(&result, QIODevice::WriteOnly);
			stream.setVersion(QDataStream::Qt_5_1);

			auto currentUserId = _authSession ? _authSession->userId() : 0;
			stream << qint32(currentUserId) << qint32(mainDcId);
			writeKeys(stream, keys);
			writeKeys(stream, keysToDestroy);

			DEBUG_LOG(("MTP Info: Keys written, userId: %1, dcId: %2").arg(currentUserId).arg(mainDcId));
		}
		return result;
	};
	if (_mtproto) {
		auto keys = _mtproto->getKeysForWrite();
		auto keysToDestroy = _mtprotoForKeysDestroy ? _mtprotoForKeysDestroy->getKeysForWrite() : MTP::AuthKeysList();
		return serialize(_mtproto->mainDcId(), keys, keysToDestroy);
	}
	auto &keys = _private->mtpConfig.keys;
	auto &keysToDestroy = _private->mtpKeysToDestroy;
	return serialize(_private->mtpConfig.mainDcId, keys, keysToDestroy);
}

void Application::setAuthSessionUserId(UserId userId) {
	Expects(!authSession());

	_private->authSessionUserId = userId;
}

void Application::setAuthSessionFromStorage(
		std::unique_ptr<AuthSessionSettings> data,
		QByteArray &&selfSerialized,
		int32 selfStreamVersion) {
	Expects(!authSession());

	DEBUG_LOG(("authSessionUserSerialized set: %1"
		).arg(selfSerialized.size()));

	_private->storedAuthSession = std::move(data);
	_private->authSessionUserSerialized = std::move(selfSerialized);
	_private->authSessionUserStreamVersion = selfStreamVersion;
}

AuthSessionSettings *Application::getAuthSessionSettings() {
	if (_private->authSessionUserId) {
		return _private->storedAuthSession
			? _private->storedAuthSession.get()
			: nullptr;
	} else if (_authSession) {
		return &_authSession->settings();
	}
	return nullptr;
}

void Application::setMtpAuthorization(const QByteArray &serialized) {
	Expects(!_mtproto);

	QDataStream stream(serialized);
	stream.setVersion(QDataStream::Qt_5_1);

	auto userId = Serialize::read<qint32>(stream);
	auto mainDcId = Serialize::read<qint32>(stream);
	if (stream.status() != QDataStream::Ok) {
		LOG(("MTP Error: could not read main fields from serialized mtp authorization."));
		return;
	}

	setAuthSessionUserId(userId);
	_private->mtpConfig.mainDcId = mainDcId;

	auto readKeys = [&stream](auto &keys) {
		auto count = Serialize::read<qint32>(stream);
		if (stream.status() != QDataStream::Ok) {
			LOG(("MTP Error: could not read keys count from serialized mtp authorization."));
			return;
		}
		keys.reserve(count);
		for (auto i = 0; i != count; ++i) {
			auto dcId = Serialize::read<qint32>(stream);
			auto keyData = Serialize::read<MTP::AuthKey::Data>(stream);
			if (stream.status() != QDataStream::Ok) {
				LOG(("MTP Error: could not read key from serialized mtp authorization."));
				return;
			}
			keys.push_back(std::make_shared<MTP::AuthKey>(MTP::AuthKey::Type::ReadFromFile, dcId, keyData));
		}
	};
	readKeys(_private->mtpConfig.keys);
	readKeys(_private->mtpKeysToDestroy);
	LOG(("MTP Info: read keys, current: %1, to destroy: %2").arg(_private->mtpConfig.keys.size()).arg(_private->mtpKeysToDestroy.size()));
}

void Application::startMtp() {
	Expects(!_mtproto);

	_mtproto = std::make_unique<MTP::Instance>(
		_dcOptions.get(),
		MTP::Instance::Mode::Normal,
		base::take(_private->mtpConfig));
	_mtproto->setUserPhone(cLoggedPhoneNumber());
	_private->mtpConfig.mainDcId = _mtproto->mainDcId();

	_mtproto->setStateChangedHandler([](MTP::ShiftedDcId dc, int32 state) {
		if (dc == MTP::maindc()) {
			Global::RefConnectionTypeChanged().notify();
		}
	});
	_mtproto->setSessionResetHandler([](MTP::ShiftedDcId shiftedDcId) {
		if (App::main() && shiftedDcId == MTP::maindc()) {
			App::main()->getDifference();
		}
	});

	if (!_private->mtpKeysToDestroy.empty()) {
		destroyMtpKeys(base::take(_private->mtpKeysToDestroy));
	}

	if (_private->authSessionUserId) {
		DEBUG_LOG(("authSessionUserSerialized.size: %1"
			).arg(_private->authSessionUserSerialized.size()));
		QDataStream peekStream(_private->authSessionUserSerialized);
		const auto phone = Serialize::peekUserPhone(
			_private->authSessionUserStreamVersion,
			peekStream);
		const auto flags = MTPDuser::Flag::f_self | (phone.isEmpty()
			? MTPDuser::Flag()
			: MTPDuser::Flag::f_phone);
		authSessionCreate(MTP_user(
			MTP_flags(flags),
			MTP_int(base::take(_private->authSessionUserId)),
			MTPlong(), // access_hash
			MTPstring(), // first_name
			MTPstring(), // last_name
			MTPstring(), // username
			MTP_string(phone),
			MTPUserProfilePhoto(),
			MTPUserStatus(),
			MTPint(), // bot_info_version
			MTPstring(), // restriction_reason
			MTPstring(), // bot_inline_placeholder
			MTPstring())); // lang_code
		Local::readSelf(
			base::take(_private->authSessionUserSerialized),
			base::take(_private->authSessionUserStreamVersion));
	}
	if (_private->storedAuthSession) {
		if (_authSession) {
			_authSession->moveSettingsFrom(
				std::move(*_private->storedAuthSession));
		}
		_private->storedAuthSession.reset();
	}

	_langCloudManager = std::make_unique<Lang::CloudManager>(
		langpack(),
		mtp());
	if (!UpdaterDisabled()) {
		UpdateChecker().setMtproto(mtp());
	}

	if (_authSession) {
		// Skip all pending self updates so that we won't Local::writeSelf.
		Notify::peerUpdatedSendDelayed();

		Media::Player::mixer()->setVoicePlaybackDoubled(
			Global::VoiceMsgPlaybackDoubled());
	}
}

void Application::destroyMtpKeys(MTP::AuthKeysList &&keys) {
	if (keys.empty()) {
		return;
	}
	if (_mtprotoForKeysDestroy) {
		_mtprotoForKeysDestroy->addKeysForDestroy(std::move(keys));
		Local::writeMtpData();
		return;
	}
	auto destroyConfig = MTP::Instance::Config();
	destroyConfig.mainDcId = MTP::Instance::Config::kNoneMainDc;
	destroyConfig.keys = std::move(keys);
	_mtprotoForKeysDestroy = std::make_unique<MTP::Instance>(_dcOptions.get(), MTP::Instance::Mode::KeysDestroyer, std::move(destroyConfig));
	connect(
		_mtprotoForKeysDestroy.get(),
		&MTP::Instance::allKeysDestroyed,
		[=] { allKeysDestroyed(); });
}

void Application::allKeysDestroyed() {
	LOG(("MTP Info: all keys scheduled for destroy are destroyed."));
	crl::on_main(this, [=] {
		_mtprotoForKeysDestroy = nullptr;
		Local::writeMtpData();
	});
}

void Application::suggestMainDcId(MTP::DcId mainDcId) {
	Assert(_mtproto != nullptr);

	_mtproto->suggestMainDcId(mainDcId);
	if (_private->mtpConfig.mainDcId != MTP::Instance::Config::kNotSetMainDc) {
		_private->mtpConfig.mainDcId = mainDcId;
	}
}

void Application::destroyStaleAuthorizationKeys() {
	Assert(_mtproto != nullptr);

	for (const auto &key : _mtproto->getKeysForWrite()) {
		// Disable this for now.
		if (key->type() == MTP::AuthKey::Type::ReadFromFile) {
			_private->mtpKeysToDestroy = _mtproto->getKeysForWrite();
			LOG(("MTP Info: destroying stale keys, count: %1"
				).arg(_private->mtpKeysToDestroy.size()));
			resetAuthorizationKeys();
			return;
		}
	}
}

void Application::resetAuthorizationKeys() {
	_mtproto = nullptr;
	startMtp();
	Local::writeMtpData();
}

void Application::startLocalStorage() {
	_dcOptions = std::make_unique<MTP::DcOptions>();
	_dcOptions->constructFromBuiltIn();
	Local::start();
	subscribe(_dcOptions->changed(), [this](const MTP::DcOptions::Ids &ids) {
		Local::writeSettings();
		if (auto instance = mtp()) {
			for (auto id : ids) {
				instance->restart(id);
			}
		}
	});
	subscribe(authSessionChanged(), [=] {
		InvokeQueued(this, [=] {
			const auto phone = AuthSession::Exists()
				? Auth().user()->phone()
				: QString();
			if (cLoggedPhoneNumber() != phone) {
				cSetLoggedPhoneNumber(phone);
				if (_mtproto) {
					_mtproto->setUserPhone(phone);
				}
				Local::writeSettings();
			}
			if (_mtproto) {
				_mtproto->requestConfig();
			}
			Platform::SetApplicationIcon(Window::CreateIcon());
			Shortcuts::ToggleSupportShortcuts(
				_authSession && _authSession->supportMode());
		});
	});
}

void Application::forceLogOut(const TextWithEntities &explanation) {
	const auto box = Ui::show(Box<InformBox>(
		explanation,
		lang(lng_passcode_logout)));
	box->setCloseByEscape(false);
	box->setCloseByOutsideClick(false);
	connect(box, &QObject::destroyed, [=] {
		crl::on_main(this, [=] {
			if (AuthSession::Exists()) {
				resetAuthorizationKeys();
				loggedOut();
			}
		});
	});
}

void Application::checkLocalTime() {
	const auto updated = checkms();
	if (App::main()) App::main()->checkLastUpdate(updated);
}

void Application::stateChanged(Qt::ApplicationState state) {
	if (state == Qt::ApplicationActive) {
		handleAppActivated();
	} else {
		handleAppDeactivated();
	}
}

void Application::handleAppActivated() {
	checkLocalTime();
	if (_window) {
		_window->updateIsActive(Global::OnlineFocusTimeout());
	}
}

void Application::handleAppDeactivated() {
	if (_window) {
		_window->updateIsActive(Global::OfflineBlurTimeout());
	}
	Ui::Tooltip::Hide();
}

void Application::call_handleUnreadCounterUpdate() {
	Global::RefUnreadCounterUpdate().notify(true);
}

void Application::call_handleDelayedPeerUpdates() {
	Notify::peerUpdatedSendDelayed();
}

void Application::call_handleObservables() {
	base::HandleObservables();
}

void Application::switchDebugMode() {
	if (Logs::DebugEnabled()) {
		Logs::SetDebugEnabled(false);
		_launcher->writeDebugModeSetting();
		App::restart();
	} else {
		Logs::SetDebugEnabled(true);
		_launcher->writeDebugModeSetting();
		DEBUG_LOG(("Debug logs started."));
		Ui::hideLayer();
	}
}

void Application::switchWorkMode() {
	Global::SetDialogsModeEnabled(!Global::DialogsModeEnabled());
	Global::SetDialogsMode(Dialogs::Mode::All);
	Local::writeUserSettings();
	App::restart();
}

void Application::switchTestMode() {
	if (cTestMode()) {
		QFile(cWorkingDir() + qsl("tdata/withtestmode")).remove();
		cSetTestMode(false);
	} else {
		QFile f(cWorkingDir() + qsl("tdata/withtestmode"));
		if (f.open(QIODevice::WriteOnly)) {
			f.write("1");
			f.close();
		}
		cSetTestMode(true);
	}
	App::restart();
}

void Application::writeInstallBetaVersionsSetting() {
	_launcher->writeInstallBetaVersionsSetting();
}

void Application::authSessionCreate(const MTPUser &user) {
	Expects(_mtproto != nullptr);

	_authSession = std::make_unique<AuthSession>(user);
	_mtproto->setUpdatesHandler(::rpcDone([](
			const mtpPrime *from,
			const mtpPrime *end) {
		if (const auto main = App::main()) {
			main->updateReceived(from, end);
		}
	}));
	_mtproto->setGlobalFailHandler(::rpcFail([=](const RPCError &error) {
		crl::on_main(_authSession.get(), [=] { logOut(); });
		return true;
	}));
	authSessionChanged().notify(true);
}

void Application::authSessionDestroy() {
	_private->storedAuthSession.reset();
	_private->authSessionUserId = 0;
	_private->authSessionUserSerialized = {};
	if (_authSession) {
		unlockTerms();
		_mtproto->clearGlobalHandlers();
		_authSession = nullptr;
		authSessionChanged().notify(true);
		Notify::unreadCounterUpdated();
	}
}

int Application::unreadBadge() const {
	return _authSession ? _authSession->data().unreadBadge() : 0;
}

bool Application::unreadBadgeMuted() const {
	return _authSession ? _authSession->data().unreadBadgeMuted() : false;
}

void Application::setInternalLinkDomain(const QString &domain) const {
	// This domain should start with 'http[s]://' and end with '/'.
	// Like 'https://telegram.me/' or 'https://t.me/'.
	auto validate = [](const auto &domain) {
		const auto prefixes = {
			qstr("https://"),
			qstr("http://"),
		};
		for (const auto &prefix : prefixes) {
			if (domain.startsWith(prefix, Qt::CaseInsensitive)) {
				return domain.endsWith('/');
			}
		}
		return false;
	};
	if (validate(domain) && domain != Global::InternalLinksDomain()) {
		Global::SetInternalLinksDomain(domain);
	}
}

QString Application::createInternalLink(const QString &query) const {
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

QString Application::createInternalLinkFull(const QString &query) const {
	return Global::InternalLinksDomain() + query;
}

void Application::checkStartUrl() {
	if (!cStartUrl().isEmpty() && !locked()) {
		auto url = cStartUrl();
		cSetStartUrl(QString());
		if (!openLocalUrl(url, {})) {
			cSetStartUrl(url);
		}
	}
}

bool Application::openLocalUrl(const QString &url, QVariant context) {
	auto urlTrimmed = url.trimmed();
	if (urlTrimmed.size() > 8192) urlTrimmed = urlTrimmed.mid(0, 8192);

	const auto protocol = qstr("tg://");
	if (!urlTrimmed.startsWith(protocol, Qt::CaseInsensitive) || locked()) {
		return false;
	}
	auto command = urlTrimmed.midRef(protocol.size());

	using namespace qthelp;
	const auto options = RegExOption::CaseInsensitive;
	for (const auto &[expression, handler] : LocalUrlHandlers()) {
		const auto match = regex_match(expression, command, options);
		if (match) {
			return handler(match, context);
		}
	}
	return false;
}

void Application::lockByPasscode() {
	_passcodeLock = true;
	_window->setupPasscodeLock();
}

void Application::unlockPasscode() {
	clearPasscodeLock();
	_window->clearPasscodeLock();
}

void Application::clearPasscodeLock() {
	cSetPasscodeBadTries(0);
	_passcodeLock = false;
}

bool Application::passcodeLocked() const {
	return _passcodeLock.current();
}

rpl::producer<bool> Application::passcodeLockChanges() const {
	return _passcodeLock.changes();
}

rpl::producer<bool> Application::passcodeLockValue() const {
	return _passcodeLock.value();
}

void Application::lockByTerms(const Window::TermsLock &data) {
	if (!_termsLock || *_termsLock != data) {
		_termsLock = std::make_unique<Window::TermsLock>(data);
		_termsLockChanges.fire(true);
	}
}

void Application::unlockTerms() {
	if (_termsLock) {
		_termsLock = nullptr;
		_termsLockChanges.fire(false);
	}
}

std::optional<Window::TermsLock> Application::termsLocked() const {
	return _termsLock ? base::make_optional(*_termsLock) : std::nullopt;
}

rpl::producer<bool> Application::termsLockChanges() const {
	return _termsLockChanges.events();
}

rpl::producer<bool> Application::termsLockValue() const {
	return rpl::single(
		_termsLock != nullptr
	) | rpl::then(termsLockChanges());
}

void Application::termsDeleteNow() {
	MTP::send(MTPaccount_DeleteAccount(MTP_string("Decline ToS update")));
}

bool Application::locked() const {
	return passcodeLocked() || termsLocked();
}

rpl::producer<bool> Application::lockChanges() const {
	return lockValue() | rpl::skip(1);
}

rpl::producer<bool> Application::lockValue() const {
	using namespace rpl::mappers;
	return rpl::combine(
		passcodeLockValue(),
		termsLockValue(),
		_1 || _2);
}

MainWindow *Application::getActiveWindow() const {
	return _window.get();
}

bool Application::closeActiveWindow() {
	if (hideMediaView()) {
		return true;
	}
	if (auto activeWindow = getActiveWindow()) {
		if (!activeWindow->hideNoQuit()) {
			activeWindow->close();
		}
		return true;
	}
	return false;
}

bool Application::minimizeActiveWindow() {
	hideMediaView();
	if (auto activeWindow = getActiveWindow()) {
		if (Global::WorkMode().value() == dbiwmTrayOnly) {
			activeWindow->minimizeToTray();
		} else {
			activeWindow->setWindowState(Qt::WindowMinimized);
		}
		return true;
	}
	return false;
}

QWidget *Application::getFileDialogParent() {
	return (_mediaView && _mediaView->isVisible()) ? (QWidget*)_mediaView.get() : (QWidget*)getActiveWindow();
}

void Application::checkMediaViewActivation() {
	if (_mediaView && !_mediaView->isHidden()) {
		_mediaView->activateWindow();
		QApplication::setActiveWindow(_mediaView.get());
		_mediaView->setFocus();
	}
}

void Application::logOut() {
	if (_mtproto) {
		_mtproto->logout(::rpcDone([=] {
			loggedOut();
		}), ::rpcFail([=] {
			loggedOut();
			return true;
		}));
	} else {
		// We log out because we've forgotten passcode.
		// So we just start mtproto from scratch.
		startMtp();
		loggedOut();
	}
}

void Application::loggedOut() {
	if (Global::LocalPasscode()) {
		Global::SetLocalPasscode(false);
		Global::RefLocalPasscodeChanged().notify();
	}
	clearPasscodeLock();
	Media::Player::mixer()->stopAndClear();
	Global::SetVoiceMsgPlaybackDoubled(false);
	Media::Player::mixer()->setVoicePlaybackDoubled(false);
	if (const auto window = getActiveWindow()) {
		window->tempDirDelete(Local::ClearManagerAll);
		window->setupIntro();
	}
	if (const auto session = authSession()) {
		session->data().clearLocalStorage();
		authSessionDestroy();
	}
	if (_mediaView) {
		hideMediaView();
		_mediaView->clearData();
	}
	Local::reset();

	cSetOtherOnline(0);
	Images::ClearRemote();
}

QPoint Application::getPointForCallPanelCenter() const {
	if (auto activeWindow = getActiveWindow()) {
		Assert(activeWindow->windowHandle() != nullptr);
		if (activeWindow->isActive()) {
			return activeWindow->geometry().center();
		}
		return activeWindow->windowHandle()->screen()->geometry().center();
	}
	return QApplication::desktop()->screenGeometry().center();
}

// macOS Qt bug workaround, sometimes no leaveEvent() gets to the nested widgets.
void Application::registerLeaveSubscription(QWidget *widget) {
#ifdef Q_OS_MAC
	if (auto topLevel = widget->window()) {
		if (topLevel == _window.get()) {
			auto weak = make_weak(widget);
			auto subscription = _window->leaveEvents(
			) | rpl::start_with_next([weak] {
				if (const auto window = weak.data()) {
					QEvent ev(QEvent::Leave);
					QGuiApplication::sendEvent(window, &ev);
				}
			});
			_leaveSubscriptions.emplace_back(weak, std::move(subscription));
		}
	}
#endif // Q_OS_MAC
}

void Application::unregisterLeaveSubscription(QWidget *widget) {
#ifdef Q_OS_MAC
	_leaveSubscriptions = std::move(
		_leaveSubscriptions
	) | ranges::action::remove_if([&](const LeaveSubscription &subscription) {
		auto pointer = subscription.pointer.data();
		return !pointer || (pointer == widget);
	});
#endif // Q_OS_MAC
}

void Application::postponeCall(FnMut<void()> &&callable) {
	Sandbox::Instance().postponeCall(std::move(callable));
}

void Application::refreshGlobalProxy() {
	Sandbox::Instance().refreshGlobalProxy();
}

void Application::activateWindowDelayed(not_null<QWidget*> widget) {
	Sandbox::Instance().activateWindowDelayed(widget);
}

void Application::pauseDelayedWindowActivations() {
	Sandbox::Instance().pauseDelayedWindowActivations();
}

void Application::resumeDelayedWindowActivations() {
	Sandbox::Instance().resumeDelayedWindowActivations();
}

void Application::preventWindowActivation() {
	pauseDelayedWindowActivations();
	postponeCall([=] {
		resumeDelayedWindowActivations();
	});
}

void Application::QuitAttempt() {
	auto prevents = false;
	if (AuthSession::Exists() && !Sandbox::Instance().isSavingSession()) {
		if (Auth().api().isQuitPrevent()) {
			prevents = true;
		}
		if (Auth().calls().isQuitPrevent()) {
			prevents = true;
		}
	}
	if (prevents) {
		App().quitDelayed();
	} else {
		QApplication::quit();
	}
}

void Application::quitPreventFinished() {
	if (App::quitting()) {
		QuitAttempt();
	}
}

void Application::quitDelayed() {
	if (!_private->quitTimer.isActive()) {
		_private->quitTimer.setCallback([] { QApplication::quit(); });
		_private->quitTimer.callOnce(kQuitPreventTimeoutMs);
	}
}

void Application::startShortcuts() {
	Shortcuts::Start();

	Shortcuts::Requests(
	) | rpl::start_with_next([=](not_null<Shortcuts::Request*> request) {
		using Command = Shortcuts::Command;
		request->check(Command::Quit) && request->handle([] {
			App::quit();
			return true;
		});
		request->check(Command::Lock) && request->handle([=] {
			if (!passcodeLocked() && Global::LocalPasscode()) {
				lockByPasscode();
				return true;
			}
			return false;
		});
		request->check(Command::Minimize) && request->handle([=] {
			return minimizeActiveWindow();
		});
		request->check(Command::Close) && request->handle([=] {
			return closeActiveWindow();
		});
	}, _lifetime);
}

Application::~Application() {
	_window.reset();
	_mediaView.reset();

	// Some MTP requests can be cancelled from data clearing.
	authSessionDestroy();

	// The langpack manager should be destroyed before MTProto instance,
	// because it is MTP::Sender and it may have pending requests.
	_langCloudManager.reset();

	_mtproto.reset();
	_mtprotoForKeysDestroy.reset();

	Shortcuts::Finish();

	Ui::Emoji::Clear();

	anim::stopManager();

	stopWebLoadManager();
	App::deinitMedia();

	Window::Theme::Unload();

	Media::Player::finish(_audio.get());
	style::stopManager();

	Local::finish();
	Global::finish();
	ThirdParty::finish();
}

Application &App() {
	return Sandbox::Instance().application();
}

} // namespace Core
