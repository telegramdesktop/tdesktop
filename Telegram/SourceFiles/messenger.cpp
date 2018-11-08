/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "messenger.h"

#include "data/data_photo.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "base/timer.h"
#include "core/update_checker.h"
#include "storage/localstorage.h"
#include "platform/platform_specific.h"
#include "mainwindow.h"
#include "dialogs/dialogs_entry.h"
#include "history/history.h"
#include "application.h"
#include "shortcuts.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "calls/calls_instance.h"
#include "lang/lang_file_parser.h"
#include "lang/lang_translator.h"
#include "lang/lang_cloud_manager.h"
#include "lang/lang_hardcoded.h"
#include "core/update_checker.h"
#include "passport/passport_form_controller.h"
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

namespace {

constexpr auto kQuitPreventTimeoutMs = 1500;

Messenger *SingleInstance = nullptr;

} // namespace

Messenger *Messenger::InstancePointer() {
	return SingleInstance;
}

struct Messenger::Private {
	UserId authSessionUserId = 0;
	QByteArray authSessionUserSerialized;
	int32 authSessionUserStreamVersion = 0;
	std::unique_ptr<AuthSessionSettings> storedAuthSession;
	MTP::Instance::Config mtpConfig;
	MTP::AuthKeysList mtpKeysToDestroy;
	base::Timer quitTimer;
};

Messenger::Messenger(not_null<Core::Launcher*> launcher)
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
	Expects(SingleInstance == nullptr);

	SingleInstance = this;

	Fonts::Start();

	ThirdParty::start();
	Global::start();
	Sandbox::refreshGlobalProxy(); // Depends on Global::started().

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
	Media::Player::start();

	DEBUG_LOG(("Application Info: inited..."));

	QCoreApplication::instance()->installNativeEventFilter(psNativeEventFilter());

	cChangeTimeFormat(QLocale::system().timeFormat(QLocale::ShortFormat));

	connect(&killDownloadSessionsTimer, SIGNAL(timeout()), this, SLOT(killDownloadSessions()));

	DEBUG_LOG(("Application Info: starting app..."));

	// Create mime database, so it won't be slow later.
	QMimeDatabase().mimeTypeForName(qsl("text/plain"));

	_window = std::make_unique<MainWindow>();
	_window->init();

	auto currentGeometry = _window->geometry();
	_mediaView = std::make_unique<MediaView>();
	_window->setGeometry(currentGeometry);

	QCoreApplication::instance()->installEventFilter(this);
	Sandbox::connect(SIGNAL(applicationStateChanged(Qt::ApplicationState)), this, SLOT(onAppStateChanged(Qt::ApplicationState)));

	DEBUG_LOG(("Application Info: window created..."));

	Shortcuts::start();

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

	if (!Shortcuts::errors().isEmpty()) {
		const QStringList &errors(Shortcuts::errors());
		for (QStringList::const_iterator i = errors.cbegin(), e = errors.cend(); i != e; ++i) {
			LOG(("Shortcuts Error: %1").arg(*i));
		}
	}
}

bool Messenger::hideMediaView() {
	if (_mediaView && !_mediaView->isHidden()) {
		_mediaView->hide();
		if (auto activeWindow = getActiveWindow()) {
			activeWindow->reActivateWindow();
		}
		return true;
	}
	return false;
}

void Messenger::showPhoto(not_null<const PhotoOpenClickHandler*> link) {
	const auto item = App::histItemById(link->context());
	const auto peer = link->peer();
	return (!item && peer)
		? showPhoto(link->photo(), peer)
		: showPhoto(link->photo(), item);
}

void Messenger::showPhoto(not_null<PhotoData*> photo, HistoryItem *item) {
	if (_mediaView->isHidden()) Ui::hideLayer(anim::type::instant);
	_mediaView->showPhoto(photo, item);
	_mediaView->activateWindow();
	_mediaView->setFocus();
}

void Messenger::showPhoto(
		not_null<PhotoData*> photo,
		not_null<PeerData*> peer) {
	if (_mediaView->isHidden()) Ui::hideLayer(anim::type::instant);
	_mediaView->showPhoto(photo, peer);
	_mediaView->activateWindow();
	_mediaView->setFocus();
}

void Messenger::showDocument(not_null<DocumentData*> document, HistoryItem *item) {
	if (cUseExternalVideoPlayer() && document->isVideoFile()) {
		QDesktopServices::openUrl(QUrl("file:///" + document->location(false).fname));
	} else {
		if (_mediaView->isHidden()) {
			Ui::hideLayer(anim::type::instant);
		}
		_mediaView->showDocument(document, item);
		_mediaView->activateWindow();
		_mediaView->setFocus();
	}
}

PeerData *Messenger::ui_getPeerForMouseAction() {
	if (_mediaView && !_mediaView->isHidden()) {
		return _mediaView->ui_getPeerForMouseAction();
	} else if (auto main = App::main()) {
		return main->ui_getPeerForMouseAction();
	}
	return nullptr;
}

bool Messenger::eventFilter(QObject *object, QEvent *e) {
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
		DEBUG_LOG(("Shortcut event caught: %1").arg(static_cast<QShortcutEvent*>(e)->key().toString()));
		if (Shortcuts::launch(static_cast<QShortcutEvent*>(e)->shortcutId())) {
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
			auto url = QString::fromUtf8(static_cast<QFileOpenEvent*>(e)->url().toEncoded().trimmed());
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

void Messenger::setCurrentProxy(
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
	Sandbox::refreshGlobalProxy();
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

void Messenger::badMtprotoConfigurationError() {
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

void Messenger::setMtpMainDcId(MTP::DcId mainDcId) {
	Expects(!_mtproto);

	_private->mtpConfig.mainDcId = mainDcId;
}

void Messenger::setMtpKey(MTP::DcId dcId, const MTP::AuthKey::Data &keyData) {
	Expects(!_mtproto);

	_private->mtpConfig.keys.push_back(std::make_shared<MTP::AuthKey>(MTP::AuthKey::Type::ReadFromFile, dcId, keyData));
}

QByteArray Messenger::serializeMtpAuthorization() const {
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

void Messenger::setAuthSessionUserId(UserId userId) {
	Expects(!authSession());

	_private->authSessionUserId = userId;
}

void Messenger::setAuthSessionFromStorage(
		std::unique_ptr<AuthSessionSettings> data,
		QByteArray &&selfSerialized,
		int32 selfStreamVersion) {
	Expects(!authSession());

	_private->storedAuthSession = std::move(data);
	_private->authSessionUserSerialized = std::move(selfSerialized);
	_private->authSessionUserStreamVersion = selfStreamVersion;
}

AuthSessionSettings *Messenger::getAuthSessionSettings() {
	if (_private->authSessionUserId) {
		return _private->storedAuthSession
			? _private->storedAuthSession.get()
			: nullptr;
	} else if (_authSession) {
		return &_authSession->settings();
	}
	return nullptr;
}

void Messenger::setMtpAuthorization(const QByteArray &serialized) {
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

void Messenger::startMtp() {
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
			_authSession->settings().moveFrom(
				std::move(*_private->storedAuthSession));
		}
		_private->storedAuthSession.reset();
	}

	_langCloudManager = std::make_unique<Lang::CloudManager>(
		langpack(),
		mtp());
	if (!Core::UpdaterDisabled()) {
		Core::UpdateChecker().setMtproto(mtp());
	}

	if (_authSession) {
		// Skip all pending self updates so that we won't Local::writeSelf.
		Notify::peerUpdatedSendDelayed();

		Media::Player::mixer()->setVoicePlaybackSpeed(
			Global::VoiceMsgPlaybackSpeed());
	}
}

void Messenger::destroyMtpKeys(MTP::AuthKeysList &&keys) {
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
	connect(_mtprotoForKeysDestroy.get(), SIGNAL(allKeysDestroyed()), this, SLOT(onAllKeysDestroyed()));
}

void Messenger::onAllKeysDestroyed() {
	LOG(("MTP Info: all keys scheduled for destroy are destroyed."));
	_mtprotoForKeysDestroy.reset();
	Local::writeMtpData();
}

void Messenger::suggestMainDcId(MTP::DcId mainDcId) {
	Assert(_mtproto != nullptr);

	_mtproto->suggestMainDcId(mainDcId);
	if (_private->mtpConfig.mainDcId != MTP::Instance::Config::kNotSetMainDc) {
		_private->mtpConfig.mainDcId = mainDcId;
	}
}

void Messenger::destroyStaleAuthorizationKeys() {
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

void Messenger::resetAuthorizationKeys() {
	_mtproto.reset();
	startMtp();
	Local::writeMtpData();
}

void Messenger::startLocalStorage() {
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
		});
	});
}

void Messenger::killDownloadSessionsStart(MTP::DcId dcId) {
	if (killDownloadSessionTimes.constFind(dcId) == killDownloadSessionTimes.cend()) {
		killDownloadSessionTimes.insert(dcId, getms() + MTPAckSendWaiting + MTPKillFileSessionTimeout);
	}
	if (!killDownloadSessionsTimer.isActive()) {
		killDownloadSessionsTimer.start(MTPAckSendWaiting + MTPKillFileSessionTimeout + 5);
	}
}

void Messenger::killDownloadSessionsStop(MTP::DcId dcId) {
	killDownloadSessionTimes.remove(dcId);
	if (killDownloadSessionTimes.isEmpty() && killDownloadSessionsTimer.isActive()) {
		killDownloadSessionsTimer.stop();
	}
}

void Messenger::forceLogOut(const TextWithEntities &explanation) {
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

void Messenger::checkLocalTime() {
	const auto updated = checkms();
	if (App::main()) App::main()->checkLastUpdate(updated);
}

void Messenger::onAppStateChanged(Qt::ApplicationState state) {
	if (state == Qt::ApplicationActive) {
		handleAppActivated();
	} else {
		handleAppDeactivated();
	}
}

void Messenger::handleAppActivated() {
	checkLocalTime();
	if (_window) {
		_window->updateIsActive(Global::OnlineFocusTimeout());
	}
}

void Messenger::handleAppDeactivated() {
	if (_window) {
		_window->updateIsActive(Global::OfflineBlurTimeout());
	}
	Ui::Tooltip::Hide();
}

void Messenger::call_handleUnreadCounterUpdate() {
	Global::RefUnreadCounterUpdate().notify(true);
}

void Messenger::call_handleDelayedPeerUpdates() {
	Notify::peerUpdatedSendDelayed();
}

void Messenger::call_handleObservables() {
	base::HandleObservables();
}

void Messenger::killDownloadSessions() {
	auto ms = getms(), left = static_cast<TimeMs>(MTPAckSendWaiting) + MTPKillFileSessionTimeout;
	for (auto i = killDownloadSessionTimes.begin(); i != killDownloadSessionTimes.end(); ) {
		if (i.value() <= ms) {
			for (int j = 0; j < MTP::kDownloadSessionsCount; ++j) {
				MTP::stopSession(MTP::downloadDcId(i.key(), j));
			}
			i = killDownloadSessionTimes.erase(i);
		} else {
			if (i.value() - ms < left) {
				left = i.value() - ms;
			}
			++i;
		}
	}
	if (!killDownloadSessionTimes.isEmpty()) {
		killDownloadSessionsTimer.start(left);
	}
}

void Messenger::onSwitchDebugMode() {
	if (Logs::DebugEnabled()) {
		Logs::SetDebugEnabled(false);
		Sandbox::WriteDebugModeSetting();
		App::restart();
	} else {
		Logs::SetDebugEnabled(true);
		Sandbox::WriteDebugModeSetting();
		DEBUG_LOG(("Debug logs started."));
		Ui::hideLayer();
	}
}

void Messenger::onSwitchWorkMode() {
	Global::SetDialogsModeEnabled(!Global::DialogsModeEnabled());
	Global::SetDialogsMode(Dialogs::Mode::All);
	Local::writeUserSettings();
	App::restart();
}

void Messenger::onSwitchTestMode() {
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

void Messenger::authSessionCreate(const MTPUser &user) {
	Expects(_mtproto != nullptr);

	_authSession = std::make_unique<AuthSession>(user);
	authSessionChanged().notify(true);
}

void Messenger::authSessionDestroy() {
	unlockTerms();

	_authSession.reset();
	_private->storedAuthSession.reset();
	_private->authSessionUserId = 0;
	_private->authSessionUserSerialized = {};
	authSessionChanged().notify(true);
}

void Messenger::setInternalLinkDomain(const QString &domain) const {
	// This domain should start with 'http[s]://' and end with '/', like 'https://t.me/'.
	auto validate = [](auto &domain) {
		auto prefixes = {
			qstr("https://"),
			qstr("http://"),
		};
		for (auto &prefix : prefixes) {
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

QString Messenger::createInternalLink(const QString &query) const {
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

QString Messenger::createInternalLinkFull(const QString &query) const {
	return Global::InternalLinksDomain() + query;
}

void Messenger::checkStartUrl() {
	if (!cStartUrl().isEmpty() && !locked()) {
		auto url = cStartUrl();
		cSetStartUrl(QString());
		if (!openLocalUrl(url, {})) {
			cSetStartUrl(url);
		}
	}
}

bool Messenger::openLocalUrl(const QString &url, QVariant context) {
	auto urlTrimmed = url.trimmed();
	if (urlTrimmed.size() > 8192) urlTrimmed = urlTrimmed.mid(0, 8192);

	const auto protocol = qstr("tg://");
	if (!urlTrimmed.startsWith(protocol, Qt::CaseInsensitive) || locked()) {
		return false;
	}
	auto command = urlTrimmed.midRef(protocol.size());

	const auto showPassportForm = [](const QMap<QString, QString> &params) {
		const auto botId = params.value("bot_id", QString()).toInt();
		const auto scope = params.value("scope", QString());
		const auto callback = params.value("callback_url", QString());
		const auto publicKey = params.value("public_key", QString());
		const auto nonce = params.value(
			Passport::NonceNameByScope(scope),
			QString());
		const auto errors = params.value("errors", QString());
		if (const auto window = App::wnd()) {
			if (const auto controller = window->controller()) {
				controller->showPassportForm(Passport::FormRequest(
					botId,
					scope,
					callback,
					publicKey,
					nonce,
					errors));
				return true;
			}
		}
		return false;
	};

	using namespace qthelp;
	auto matchOptions = RegExOption::CaseInsensitive;
	if (auto joinChatMatch = regex_match(qsl("^join/?\\?invite=([a-zA-Z0-9\\.\\_\\-]+)(&|$)"), command, matchOptions)) {
		if (auto main = App::main()) {
			main->joinGroupByHash(joinChatMatch->captured(1));
			return true;
		}
	} else if (auto stickerSetMatch = regex_match(qsl("^addstickers/?\\?set=([a-zA-Z0-9\\.\\_]+)(&|$)"), command, matchOptions)) {
		if (auto main = App::main()) {
			main->stickersBox(MTP_inputStickerSetShortName(MTP_string(stickerSetMatch->captured(1))));
			return true;
		}
	} else if (auto shareUrlMatch = regex_match(qsl("^msg_url/?\\?(.+)(#|$)"), command, matchOptions)) {
		if (auto main = App::main()) {
			auto params = url_parse_params(shareUrlMatch->captured(1), UrlParamNameTransform::ToLower);
			auto url = params.value(qsl("url"));
			if (!url.isEmpty()) {
				main->shareUrlLayer(url, params.value("text"));
				return true;
			}
		}
	} else if (auto confirmPhoneMatch = regex_match(qsl("^confirmphone/?\\?(.+)(#|$)"), command, matchOptions)) {
		if (auto main = App::main()) {
			auto params = url_parse_params(confirmPhoneMatch->captured(1), UrlParamNameTransform::ToLower);
			auto phone = params.value(qsl("phone"));
			auto hash = params.value(qsl("hash"));
			if (!phone.isEmpty() && !hash.isEmpty()) {
				ConfirmPhoneBox::start(phone, hash);
				return true;
			}
		}
	} else if (auto usernameMatch = regex_match(qsl("^resolve/?\\?(.+)(#|$)"), command, matchOptions)) {
		if (auto main = App::main()) {
			auto params = url_parse_params(usernameMatch->captured(1), UrlParamNameTransform::ToLower);
			auto domain = params.value(qsl("domain"));
			if (domain == qsl("telegrampassport")) {
				return showPassportForm(params);
			} else if (regex_match(qsl("^[a-zA-Z0-9\\.\\_]+$"), domain, matchOptions)) {
				auto start = qsl("start");
				auto startToken = params.value(start);
				if (startToken.isEmpty()) {
					start = qsl("startgroup");
					startToken = params.value(start);
					if (startToken.isEmpty()) {
						start = QString();
					}
				}
				auto post = (start == qsl("startgroup")) ? ShowAtProfileMsgId : ShowAtUnreadMsgId;
				auto postParam = params.value(qsl("post"));
				if (auto postId = postParam.toInt()) {
					post = postId;
				}
				auto gameParam = params.value(qsl("game"));
				if (!gameParam.isEmpty() && regex_match(qsl("^[a-zA-Z0-9\\.\\_]+$"), gameParam, matchOptions)) {
					startToken = gameParam;
					post = ShowAtGameShareMsgId;
				}
				const auto clickFromMessageId = context.value<FullMsgId>();
				main->openPeerByName(
					domain,
					post,
					startToken,
					clickFromMessageId);
				return true;
			}
		}
	} else if (auto shareGameScoreMatch = regex_match(qsl("^share_game_score/?\\?(.+)(#|$)"), command, matchOptions)) {
		if (auto main = App::main()) {
			auto params = url_parse_params(shareGameScoreMatch->captured(1), UrlParamNameTransform::ToLower);
			ShareGameScoreByHash(params.value(qsl("hash")));
			return true;
		}
	} else if (auto socksMatch = regex_match(qsl("^socks/?\\?(.+)(#|$)"), command, matchOptions)) {
		auto params = url_parse_params(socksMatch->captured(1), UrlParamNameTransform::ToLower);
		ProxiesBoxController::ShowApplyConfirmation(ProxyData::Type::Socks5, params);
		return true;
	} else if (auto proxyMatch = regex_match(qsl("^proxy/?\\?(.+)(#|$)"), command, matchOptions)) {
		auto params = url_parse_params(proxyMatch->captured(1), UrlParamNameTransform::ToLower);
		ProxiesBoxController::ShowApplyConfirmation(ProxyData::Type::Mtproto, params);
		return true;
	} else if (auto authMatch = regex_match(qsl("^passport/?\\?(.+)(#|$)"), command, matchOptions)) {
		return showPassportForm(url_parse_params(
			authMatch->captured(1),
			UrlParamNameTransform::ToLower));
	} else if (auto unknownMatch = regex_match(qsl("^([^\\?]+)(\\?|#|$)"), command, matchOptions)) {
		if (_authSession) {
			const auto request = unknownMatch->captured(1);
			const auto callback = [=](const MTPDhelp_deepLinkInfo &result) {
				const auto text = TextWithEntities{
					qs(result.vmessage),
					(result.has_entities()
						? TextUtilities::EntitiesFromMTP(result.ventities.v)
						: EntitiesInText())
				};
				if (result.is_update_app()) {
					const auto box = std::make_shared<QPointer<BoxContent>>();
					const auto callback = [=] {
						Core::UpdateApplication();
						if (*box) (*box)->closeBox();
					};
					*box = Ui::show(Box<ConfirmBox>(
						text,
						lang(lng_menu_update),
						callback));
				} else {
					Ui::show(Box<InformBox>(text));
				}
			};
			_authSession->api().requestDeepLinkInfo(request, callback);
		}
	}
	return false;
}

void Messenger::lockByPasscode() {
	_passcodeLock = true;
	_window->setupPasscodeLock();
}

void Messenger::unlockPasscode() {
	clearPasscodeLock();
	_window->clearPasscodeLock();
}

void Messenger::clearPasscodeLock() {
	cSetPasscodeBadTries(0);
	_passcodeLock = false;
}

bool Messenger::passcodeLocked() const {
	return _passcodeLock.current();
}

rpl::producer<bool> Messenger::passcodeLockChanges() const {
	return _passcodeLock.changes();
}

rpl::producer<bool> Messenger::passcodeLockValue() const {
	return _passcodeLock.value();
}

void Messenger::lockByTerms(const Window::TermsLock &data) {
	if (!_termsLock || *_termsLock != data) {
		_termsLock = std::make_unique<Window::TermsLock>(data);
		_termsLockChanges.fire(true);
	}
}

void Messenger::unlockTerms() {
	if (_termsLock) {
		_termsLock = nullptr;
		_termsLockChanges.fire(false);
	}
}

std::optional<Window::TermsLock> Messenger::termsLocked() const {
	return _termsLock ? base::make_optional(*_termsLock) : std::nullopt;
}

rpl::producer<bool> Messenger::termsLockChanges() const {
	return _termsLockChanges.events();
}

rpl::producer<bool> Messenger::termsLockValue() const {
	return rpl::single(
		_termsLock != nullptr
	) | rpl::then(termsLockChanges());
}

void Messenger::termsDeleteNow() {
	MTP::send(MTPaccount_DeleteAccount(MTP_string("Decline ToS update")));
}

bool Messenger::locked() const {
	return passcodeLocked() || termsLocked();
}

rpl::producer<bool> Messenger::lockChanges() const {
	return lockValue() | rpl::skip(1);
}

rpl::producer<bool> Messenger::lockValue() const {
	using namespace rpl::mappers;
	return rpl::combine(
		passcodeLockValue(),
		termsLockValue(),
		_1 || _2);
}

Messenger::~Messenger() {
	Expects(SingleInstance == this);

	_window.reset();
	_mediaView.reset();

	// Some MTP requests can be cancelled from data clearing.
	App::clearHistories();
	authSessionDestroy();

	// The langpack manager should be destroyed before MTProto instance,
	// because it is MTP::Sender and it may have pending requests.
	_langCloudManager.reset();

	_mtproto.reset();
	_mtprotoForKeysDestroy.reset();

	Shortcuts::finish();

	Ui::Emoji::Clear();

	anim::stopManager();

	stopWebLoadManager();
	App::deinitMedia();

	Window::Theme::Unload();

	Media::Player::finish();
	style::stopManager();

	Local::finish();
	Global::finish();
	ThirdParty::finish();

	SingleInstance = nullptr;
}

MainWindow *Messenger::getActiveWindow() const {
	return _window.get();
}

bool Messenger::closeActiveWindow() {
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

bool Messenger::minimizeActiveWindow() {
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

QWidget *Messenger::getFileDialogParent() {
	return (_mediaView && _mediaView->isVisible()) ? (QWidget*)_mediaView.get() : (QWidget*)getActiveWindow();
}

void Messenger::checkMediaViewActivation() {
	if (_mediaView && !_mediaView->isHidden()) {
		_mediaView->activateWindow();
		Sandbox::setActiveWindow(_mediaView.get());
		_mediaView->setFocus();
	}
}

void Messenger::logOut() {
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

void Messenger::loggedOut() {
	if (Global::LocalPasscode()) {
		Global::SetLocalPasscode(false);
		Global::RefLocalPasscodeChanged().notify();
	}
	clearPasscodeLock();
	Media::Player::mixer()->stopAndClear();
	Global::SetVoiceMsgPlaybackSpeed(1.);
	Media::Player::mixer()->setVoicePlaybackSpeed(1.);
	if (const auto w = getActiveWindow()) {
		w->tempDirDelete(Local::ClearManagerAll);
		w->setupIntro();
	}
	App::histories().clear();
	if (const auto session = authSession()) {
		session->data().cache().close();
		session->data().cache().clear();
	}
	authSessionDestroy();
	if (_mediaView) {
		hideMediaView();
		_mediaView->clearData();
	}
	Local::reset();

	cSetOtherOnline(0);
	Images::ClearRemote();
}

QPoint Messenger::getPointForCallPanelCenter() const {
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
void Messenger::registerLeaveSubscription(QWidget *widget) {
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

void Messenger::unregisterLeaveSubscription(QWidget *widget) {
#ifdef Q_OS_MAC
	_leaveSubscriptions = std::move(
		_leaveSubscriptions
	) | ranges::action::remove_if([&](const LeaveSubscription &subscription) {
		auto pointer = subscription.pointer.data();
		return !pointer || (pointer == widget);
	});
#endif // Q_OS_MAC
}

void Messenger::QuitAttempt() {
	auto prevents = false;
	if (!Sandbox::isSavingSession() && AuthSession::Exists()) {
		if (Auth().api().isQuitPrevent()) {
			prevents = true;
		}
		if (Auth().calls().isQuitPrevent()) {
			prevents = true;
		}
	}
	if (prevents) {
		Instance().quitDelayed();
	} else {
		QCoreApplication::quit();
	}
}

void Messenger::quitPreventFinished() {
	if (App::quitting()) {
		QuitAttempt();
	}
}

void Messenger::quitDelayed() {
	if (!_private->quitTimer.isActive()) {
		_private->quitTimer.setCallback([] { QCoreApplication::quit(); });
		_private->quitTimer.callOnce(kQuitPreventTimeoutMs);
	}
}
