/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "messenger.h"

#include <rpl/complete.h>
#include "data/data_photo.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "base/timer.h"
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
#include "observer_peer.h"
#include "storage/file_upload.h"
#include "mainwidget.h"
#include "mediaview.h"
#include "mtproto/dc_options.h"
#include "mtproto/mtp_instance.h"
#include "media/player/media_player_instance.h"
#include "media/media_audio_track.h"
#include "window/notifications_manager.h"
#include "window/themes/window_theme.h"
#include "history/history_location_manager.h"
#include "ui/widgets/tooltip.h"
#include "ui/text_options.h"
#include "storage/serialize_common.h"
#include "window/window_controller.h"
#include "base/qthelp_regex.h"
#include "base/qthelp_url.h"
#include "boxes/connection_box.h"
#include "boxes/confirm_phone_box.h"
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
	std::unique_ptr<AuthSessionSettings> storedAuthSession;
	MTP::Instance::Config mtpConfig;
	MTP::AuthKeysList mtpKeysToDestroy;
	base::Timer quitTimer;
};

Messenger::Messenger(not_null<Core::Launcher*> launcher)
: QObject()
, _launcher(launcher)
, _private(std::make_unique<Private>())
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

	startLocalStorage();

	if (Local::oldSettingsVersion() < AppVersion) {
		psNewVersion();
	}

	if (cLaunchMode() == LaunchModeAutoStart && !cAutoStart()) {
		psAutoStart(false, true);
		App::quit();
		return;
	}

	if (cRetina()) {
		cSetConfigScale(dbisOne);
		cSetRealScale(dbisOne);
	}

	_translator = std::make_unique<Lang::Translator>();
	QCoreApplication::instance()->installTranslator(_translator.get());

	style::startManager();
	anim::startManager();
	Ui::InitTextOptions();
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

	initLocationManager();
	App::initMedia();

	Local::ReadMapState state = Local::readMap(QByteArray());
	if (state == Local::ReadMapPassNeeded) {
		Global::SetLocalPasscode(true);
		Global::RefLocalPasscodeChanged().notify();
		DEBUG_LOG(("Application Info: passcode needed..."));
	} else {
		DEBUG_LOG(("Application Info: local map read..."));
		startMtp();
	}

	DEBUG_LOG(("Application Info: MTP started..."));

	DEBUG_LOG(("Application Info: showing."));
	if (state == Local::ReadMapPassNeeded) {
		setupPasscode();
	} else {
		if (AuthSession::Exists()) {
			_window->setupMain();
		} else {
			_window->setupIntro();
		}
	}
	_window->firstShow();

	if (cStartToSettings()) {
		_window->showSettings();
	}

#ifndef TDESKTOP_DISABLE_NETWORK_PROXY
	QNetworkProxyFactory::setUseSystemConfiguration(true);
#endif // !TDESKTOP_DISABLE_NETWORK_PROXY

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
			_window->activate();
		}
	} break;
	}

	return QObject::eventFilter(object, e);
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

void Messenger::setAuthSessionFromStorage(std::unique_ptr<AuthSessionSettings> data) {
	Expects(!authSession());
	_private->storedAuthSession = std::move(data);
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
	_mtproto = std::make_unique<MTP::Instance>(_dcOptions.get(), MTP::Instance::Mode::Normal, base::take(_private->mtpConfig));
	_private->mtpConfig.mainDcId = _mtproto->mainDcId();

	_mtproto->setStateChangedHandler([](MTP::ShiftedDcId shiftedDcId, int32 state) {
		if (App::wnd()) {
			App::wnd()->mtpStateChanged(shiftedDcId, state);
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
		authSessionCreate(base::take(_private->authSessionUserId));
	}
	if (_private->storedAuthSession) {
		if (_authSession) {
			_authSession->settings().moveFrom(
				std::move(*_private->storedAuthSession));
		}
		_private->storedAuthSession.reset();
	}

	_langCloudManager = std::make_unique<Lang::CloudManager>(langpack(), mtp());
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

	auto keys = _mtproto->getKeysForWrite();
	for (auto &key : keys) {
		// Disable this for now.
		if (key->type() == MTP::AuthKey::Type::ReadFromFile) {
			_private->mtpKeysToDestroy = _mtproto->getKeysForWrite();
			_mtproto.reset();
			LOG(("MTP Info: destroying stale keys, count: %1").arg(_private->mtpKeysToDestroy.size()));
			startMtp();
			Local::writeMtpData();
			return;
		}
	}
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
	subscribe(authSessionChanged(), [this] {
		InvokeQueued(this, [this] {
			if (_mtproto) {
				_mtproto->requestConfig();
			}
		});
	});
}

void Messenger::regPhotoUpdate(const PeerId &peer, const FullMsgId &msgId) {
	photoUpdates.insert(msgId, peer);
}

bool Messenger::isPhotoUpdating(const PeerId &peer) {
	for (QMap<FullMsgId, PeerId>::iterator i = photoUpdates.begin(), e = photoUpdates.end(); i != e; ++i) {
		if (i.value() == peer) {
			return true;
		}
	}
	return false;
}

void Messenger::cancelPhotoUpdate(const PeerId &peer) {
	for (QMap<FullMsgId, PeerId>::iterator i = photoUpdates.begin(), e = photoUpdates.end(); i != e;) {
		if (i.value() == peer) {
			i = photoUpdates.erase(i);
		} else {
			++i;
		}
	}
}

void Messenger::selfPhotoCleared(const MTPUserProfilePhoto &result) {
	if (!App::self()) return;
	App::self()->setPhoto(result);
	emit peerPhotoDone(App::self()->id);
}

void Messenger::chatPhotoCleared(PeerId peer, const MTPUpdates &updates) {
	if (App::main()) {
		App::main()->sentUpdatesReceived(updates);
	}
	cancelPhotoUpdate(peer);
	emit peerPhotoDone(peer);
}

void Messenger::selfPhotoDone(const MTPphotos_Photo &result) {
	if (!App::self()) return;
	const auto &photo = result.c_photos_photo();
	Auth().data().photo(photo.vphoto);
	App::feedUsers(photo.vusers);
	cancelPhotoUpdate(App::self()->id);
	emit peerPhotoDone(App::self()->id);
}

void Messenger::chatPhotoDone(PeerId peer, const MTPUpdates &updates) {
	if (App::main()) {
		App::main()->sentUpdatesReceived(updates);
	}
	cancelPhotoUpdate(peer);
	emit peerPhotoDone(peer);
}

bool Messenger::peerPhotoFailed(PeerId peer, const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	LOG(("Application Error: update photo failed %1: %2").arg(error.type()).arg(error.description()));
	cancelPhotoUpdate(peer);
	emit peerPhotoFail(peer);
	return true;
}

void Messenger::peerClearPhoto(PeerId id) {
	if (!AuthSession::Exists()) return;

	if (id == Auth().userPeerId()) {
		MTP::send(MTPphotos_UpdateProfilePhoto(MTP_inputPhotoEmpty()), rpcDone(&Messenger::selfPhotoCleared), rpcFail(&Messenger::peerPhotoFailed, id));
	} else if (peerIsChat(id)) {
		MTP::send(MTPmessages_EditChatPhoto(peerToBareMTPInt(id), MTP_inputChatPhotoEmpty()), rpcDone(&Messenger::chatPhotoCleared, id), rpcFail(&Messenger::peerPhotoFailed, id));
	} else if (peerIsChannel(id)) {
		if (auto channel = App::channelLoaded(id)) {
			MTP::send(MTPchannels_EditPhoto(channel->inputChannel, MTP_inputChatPhotoEmpty()), rpcDone(&Messenger::chatPhotoCleared, id), rpcFail(&Messenger::peerPhotoFailed, id));
		}
	}
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

void Messenger::checkLocalTime() {
	if (App::main()) App::main()->checkLastUpdate(checkms());
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

void Messenger::photoUpdated(const FullMsgId &msgId, bool silent, const MTPInputFile &file) {
	if (!AuthSession::Exists()) return;

	auto i = photoUpdates.find(msgId);
	if (i != photoUpdates.end()) {
		auto id = i.value();
		if (id == Auth().userPeerId()) {
			MTP::send(MTPphotos_UploadProfilePhoto(file), rpcDone(&Messenger::selfPhotoDone), rpcFail(&Messenger::peerPhotoFailed, id));
		} else if (peerIsChat(id)) {
			auto history = App::history(id);
			history->sendRequestId = MTP::send(MTPmessages_EditChatPhoto(history->peer->asChat()->inputChat, MTP_inputChatUploadedPhoto(file)), rpcDone(&Messenger::chatPhotoDone, id), rpcFail(&Messenger::peerPhotoFailed, id), 0, 0, history->sendRequestId);
		} else if (peerIsChannel(id)) {
			auto history = App::history(id);
			history->sendRequestId = MTP::send(MTPchannels_EditPhoto(history->peer->asChannel()->inputChannel, MTP_inputChatUploadedPhoto(file)), rpcDone(&Messenger::chatPhotoDone, id), rpcFail(&Messenger::peerPhotoFailed, id), 0, 0, history->sendRequestId);
		}
	}
}

void Messenger::onSwitchDebugMode() {
	if (cDebug()) {
		QFile(cWorkingDir() + qsl("tdata/withdebug")).remove();
		cSetDebug(false);
		App::restart();
	} else {
		cSetDebug(true);
		DEBUG_LOG(("Debug logs started."));
		QFile f(cWorkingDir() + qsl("tdata/withdebug"));
		if (f.open(QIODevice::WriteOnly)) {
			f.write("1");
			f.close();
		}
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

void Messenger::authSessionCreate(UserId userId) {
	Expects(_mtproto != nullptr);
	_authSession = std::make_unique<AuthSession>(userId);
	authSessionChanged().notify(true);
}

void Messenger::authSessionDestroy() {
	_authSession.reset();
	_private->storedAuthSession.reset();
	_private->authSessionUserId = 0;
	authSessionChanged().notify(true);

	loggedOut();
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
	if (!cStartUrl().isEmpty() && !App::passcoded()) {
		auto url = cStartUrl();
		cSetStartUrl(QString());
		if (!openLocalUrl(url)) {
			cSetStartUrl(url);
		}
	}
}

bool Messenger::openLocalUrl(const QString &url) {
	auto urlTrimmed = url.trimmed();
	if (urlTrimmed.size() > 8192) urlTrimmed = urlTrimmed.mid(0, 8192);

	if (!urlTrimmed.startsWith(qstr("tg://"), Qt::CaseInsensitive) || App::passcoded()) {
		return false;
	}
	auto command = urlTrimmed.midRef(qstr("tg://").size());

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
			if (regex_match(qsl("^[a-zA-Z0-9\\.\\_]+$"), domain, matchOptions)) {
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
				main->openPeerByName(domain, post, startToken);
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
		ConnectionBox::ShowApplyProxyConfirmation(params);
		return true;
	}
	return false;
}

void Messenger::uploadProfilePhoto(QImage &&tosend, const PeerId &peerId) {
	PreparedPhotoThumbs photoThumbs;
	QVector<MTPPhotoSize> photoSizes;

	auto thumb = App::pixmapFromImageInPlace(tosend.scaled(160, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation));
	photoThumbs.insert('a', thumb);
	photoSizes.push_back(MTP_photoSize(MTP_string("a"), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(thumb.width()), MTP_int(thumb.height()), MTP_int(0)));

	auto medium = App::pixmapFromImageInPlace(tosend.scaled(320, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation));
	photoThumbs.insert('b', medium);
	photoSizes.push_back(MTP_photoSize(MTP_string("b"), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(medium.width()), MTP_int(medium.height()), MTP_int(0)));

	auto full = QPixmap::fromImage(tosend, Qt::ColorOnly);
	photoThumbs.insert('c', full);
	photoSizes.push_back(MTP_photoSize(MTP_string("c"), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(full.width()), MTP_int(full.height()), MTP_int(0)));

	QByteArray jpeg;
	QBuffer jpegBuffer(&jpeg);
	full.save(&jpegBuffer, "JPG", 87);

	PhotoId id = rand_value<PhotoId>();

	auto photo = MTP_photo(MTP_flags(0), MTP_long(id), MTP_long(0), MTP_int(unixtime()), MTP_vector<MTPPhotoSize>(photoSizes));

	QString file, filename;
	int32 filesize = 0;
	QByteArray data;

	SendMediaReady ready(SendMediaType::Photo, file, filename, filesize, data, id, id, qsl("jpg"), peerId, photo, photoThumbs, MTP_documentEmpty(MTP_long(0)), jpeg, 0);

	connect(&Auth().uploader(), SIGNAL(photoReady(const FullMsgId&, bool, const MTPInputFile&)), this, SLOT(photoUpdated(const FullMsgId&, bool, const MTPInputFile&)), Qt::UniqueConnection);

	FullMsgId newId(peerToChannel(peerId), clientMsgId());
	regPhotoUpdate(peerId, newId);
	Auth().uploader().uploadMedia(newId, ready);
}

void Messenger::setupPasscode() {
	_window->setupPasscode();
	_passcodedChanged.notify();
}

void Messenger::clearPasscode() {
	cSetPasscodeBadTries(0);
	_window->clearPasscode();
	_passcodedChanged.notify();
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

	anim::stopManager();

	stopWebLoadManager();
	App::deinitMedia();
	deinitLocationManager();

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

void Messenger::loggedOut() {
	if (_mediaView) {
		hideMediaView();
		_mediaView->clearData();
	}
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
