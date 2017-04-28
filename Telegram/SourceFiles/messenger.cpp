/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "messenger.h"

#include "storage/localstorage.h"
#include "platform/platform_specific.h"
#include "mainwindow.h"
#include "application.h"
#include "shortcuts.h"
#include "auth_session.h"
#include "langloaderplain.h"
#include "observer_peer.h"
#include "storage/file_upload.h"
#include "mainwidget.h"
#include "mtproto/dc_options.h"
#include "mtproto/mtp_instance.h"
#include "media/player/media_player_instance.h"
#include "window/notifications_manager.h"
#include "window/themes/window_theme.h"
#include "history/history_location_manager.h"
#include "ui/widgets/tooltip.h"
#include "storage/serialize_common.h"
#include "window/window_controller.h"

namespace {

Messenger *SingleInstance = nullptr;

} // namespace

Messenger *Messenger::InstancePointer() {
	return SingleInstance;
}

struct Messenger::Private {
	UserId authSessionUserId = 0;
	std::unique_ptr<Local::StoredAuthSession> storedAuthSession;
	MTP::Instance::Config mtpConfig;
	MTP::AuthKeysList mtpKeysToDestroy;
};

Messenger::Messenger() : QObject()
, _private(std::make_unique<Private>()) {
	t_assert(SingleInstance == nullptr);
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
	loadLanguage();
	style::startManager();
	anim::startManager();
	historyInit();
	Media::Player::start();

	DEBUG_LOG(("Application Info: inited..."));

	QCoreApplication::instance()->installNativeEventFilter(psNativeEventFilter());

	cChangeTimeFormat(QLocale::system().timeFormat(QLocale::ShortFormat));

	connect(&killDownloadSessionsTimer, SIGNAL(timeout()), this, SLOT(killDownloadSessions()));

	DEBUG_LOG(("Application Info: starting app..."));

	// Create mime database, so it won't be slow later.
	QMimeDatabase().mimeTypeForName(qsl("text/plain"));

	_window = std::make_unique<MainWindow>();
	_window->createWinId();
	_window->init();

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

	if (state != Local::ReadMapPassNeeded) {
		checkMapVersion();
	}

	_window->updateIsActive(Global::OnlineFocusTimeout());

	if (!Shortcuts::errors().isEmpty()) {
		const QStringList &errors(Shortcuts::errors());
		for (QStringList::const_iterator i = errors.cbegin(), e = errors.cend(); i != e; ++i) {
			LOG(("Shortcuts Error: %1").arg(*i));
		}
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
			QBuffer buffer(&result);
			if (!buffer.open(QIODevice::WriteOnly)) {
				LOG(("MTP Error: could not open buffer to serialize mtp authorization."));
				return result;
			}
			QDataStream stream(&buffer);
			stream.setVersion(QDataStream::Qt_5_1);

			auto currentUserId = AuthSession::Exists() ? AuthSession::CurrentUserId() : 0;
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

void Messenger::setAuthSessionFromStorage(std::unique_ptr<Local::StoredAuthSession> data) {
	Expects(!authSession());
	_private->storedAuthSession = std::move(data);
}

AuthSessionData *Messenger::getAuthSessionData() {
	if (_private->authSessionUserId) {
		return _private->storedAuthSession ? &_private->storedAuthSession->data : nullptr;
	} else if (AuthSession::Exists()) {
		return &AuthSession::Current().data();
	}
	return nullptr;
}

void Messenger::setMtpAuthorization(const QByteArray &serialized) {
	Expects(!_mtproto);

	auto readonly = serialized;
	QBuffer buffer(&readonly);
	if (!buffer.open(QIODevice::ReadOnly)) {
		LOG(("MTP Error: could not open serialized mtp authorization for reading."));
		return;
	}
	QDataStream stream(&buffer);
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
			_authSession->data().copyFrom(_private->storedAuthSession->data);
			if (auto window = App::wnd()) {
				t_assert(window->controller() != nullptr);
				window->controller()->dialogsWidthRatio().set(_private->storedAuthSession->dialogsWidthRatio);
			}
		}
		_private->storedAuthSession.reset();
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
	t_assert(_mtproto != nullptr);

	_mtproto->suggestMainDcId(mainDcId);
	if (_private->mtpConfig.mainDcId != MTP::Instance::Config::kNotSetMainDc) {
		_private->mtpConfig.mainDcId = mainDcId;
	}
}

void Messenger::destroyStaleAuthorizationKeys() {
	t_assert(_mtproto != nullptr);

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

void Messenger::loadLanguage() {
	if (cLang() < languageTest) {
		cSetLang(Sandbox::LangSystem());
	}
	if (cLang() == languageTest) {
		if (QFileInfo(cLangFile()).exists()) {
			LangLoaderPlain loader(cLangFile());
			cSetLangErrors(loader.errors());
			if (!cLangErrors().isEmpty()) {
				LOG(("Lang load errors: %1").arg(cLangErrors()));
			} else if (!loader.warnings().isEmpty()) {
				LOG(("Lang load warnings: %1").arg(loader.warnings()));
			}
		} else {
			cSetLang(languageDefault);
		}
	} else if (cLang() > languageDefault && cLang() < languageCount) {
		LangLoaderPlain loader(qsl(":/langs/lang_") + LanguageCodes[cLang()].c_str() + qsl(".strings"));
		if (!loader.errors().isEmpty()) {
			LOG(("Lang load errors: %1").arg(loader.errors()));
		} else if (!loader.warnings().isEmpty()) {
			LOG(("Lang load warnings: %1").arg(loader.warnings()));
		}
	}
	_translator = std::make_unique<Translator>();
	QCoreApplication::instance()->installTranslator(_translator.get());
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
		if (_mtproto) {
			_mtproto->configLoadRequest();
		}
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
	const auto &photo(result.c_photos_photo());
	App::feedPhoto(photo.vphoto);
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

bool Messenger::peerPhotoFail(PeerId peer, const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	LOG(("Application Error: update photo failed %1: %2").arg(error.type()).arg(error.description()));
	cancelPhotoUpdate(peer);
	emit peerPhotoFail(peer);
	return true;
}

void Messenger::peerClearPhoto(PeerId id) {
	if (!AuthSession::Exists()) return;

	if (id == AuthSession::CurrentUserPeerId()) {
		MTP::send(MTPphotos_UpdateProfilePhoto(MTP_inputPhotoEmpty()), rpcDone(&Messenger::selfPhotoCleared), rpcFail(&Messenger::peerPhotoFail, id));
	} else if (peerIsChat(id)) {
		MTP::send(MTPmessages_EditChatPhoto(peerToBareMTPInt(id), MTP_inputChatPhotoEmpty()), rpcDone(&Messenger::chatPhotoCleared, id), rpcFail(&Messenger::peerPhotoFail, id));
	} else if (peerIsChannel(id)) {
		if (auto channel = App::channelLoaded(id)) {
			MTP::send(MTPchannels_EditPhoto(channel->inputChannel, MTP_inputChatPhotoEmpty()), rpcDone(&Messenger::chatPhotoCleared, id), rpcFail(&Messenger::peerPhotoFail, id));
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

void Messenger::call_handleHistoryUpdate() {
	Notify::handlePendingHistoryUpdate();
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
		if (id == AuthSession::CurrentUserPeerId()) {
			MTP::send(MTPphotos_UploadProfilePhoto(file), rpcDone(&Messenger::selfPhotoDone), rpcFail(&Messenger::peerPhotoFail, id));
		} else if (peerIsChat(id)) {
			auto history = App::history(id);
			history->sendRequestId = MTP::send(MTPmessages_EditChatPhoto(history->peer->asChat()->inputChat, MTP_inputChatUploadedPhoto(file)), rpcDone(&Messenger::chatPhotoDone, id), rpcFail(&Messenger::peerPhotoFail, id), 0, 0, history->sendRequestId);
		} else if (peerIsChannel(id)) {
			auto history = App::history(id);
			history->sendRequestId = MTP::send(MTPchannels_EditPhoto(history->peer->asChannel()->inputChannel, MTP_inputChatUploadedPhoto(file)), rpcDone(&Messenger::chatPhotoDone, id), rpcFail(&Messenger::peerPhotoFail, id), 0, 0, history->sendRequestId);
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

FileUploader *Messenger::uploader() {
	if (!_uploader && !App::quitting()) _uploader = new FileUploader();
	return _uploader;
}

void Messenger::uploadProfilePhoto(const QImage &tosend, const PeerId &peerId) {
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

	connect(App::uploader(), SIGNAL(photoReady(const FullMsgId&, bool, const MTPInputFile&)), App::app(), SLOT(photoUpdated(const FullMsgId&, bool, const MTPInputFile&)), Qt::UniqueConnection);

	FullMsgId newId(peerToChannel(peerId), clientMsgId());
	App::app()->regPhotoUpdate(peerId, newId);
	App::uploader()->uploadMedia(newId, ready);
}

void Messenger::checkMapVersion() {
	if (Local::oldMapVersion() < AppVersion) {
		if (Local::oldMapVersion()) {
			QString versionFeatures;
			if ((cAlphaVersion() || cBetaVersion()) && Local::oldMapVersion() < 1000035) {
				versionFeatures = QString::fromUtf8("\xE2\x80\x94 Chat admins can delete other participants' messages.\n\xE2\x80\x94 Bug fixes and other minor improvements.");
			} else if (!(cAlphaVersion() || cBetaVersion()) && Local::oldMapVersion() < 1000029) {
				versionFeatures = langNewVersionText();
			} else {
				versionFeatures = lang(lng_new_version_minor).trimmed();
			}
			if (!versionFeatures.isEmpty()) {
				versionFeatures = lng_new_version_wrap(lt_version, QString::fromLatin1(AppVersionStr.c_str()), lt_changes, versionFeatures, lt_link, qsl("https://desktop.telegram.org/changelog"));
				_window->serviceNotificationLocal(versionFeatures);
			}
		}
	}
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

void Messenger::prepareToDestroy() {
	_window.reset();

	// Some MTP requests can be cancelled from data clearing.
	App::clearHistories();
	authSessionDestroy();

	_mtproto.reset();
	_mtprotoForKeysDestroy.reset();
}

Messenger::~Messenger() {
	t_assert(SingleInstance == this);
	SingleInstance = nullptr;

	Shortcuts::finish();

	anim::stopManager();

	stopWebLoadManager();
	App::deinitMedia();
	deinitLocationManager();

	delete base::take(_uploader);

	Window::Theme::Unload();

	Media::Player::finish();
	style::stopManager();

	Local::finish();
	Global::finish();
	ThirdParty::finish();
}

MainWindow *Messenger::mainWindow() {
	return _window.get();
}

QPoint Messenger::getPointForCallPanelCenter() const {
	Expects(_window != nullptr);
	Expects(_window->windowHandle() != nullptr);
	if (_window->isActive()) {
		return _window->geometry().center();
	}
	return _window->windowHandle()->screen()->geometry().center();
}
