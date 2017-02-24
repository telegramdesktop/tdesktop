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
#include "stdafx.h"
#include "messenger.h"

#include "localstorage.h"
#include "pspecific.h"
#include "mainwindow.h"
#include "application.h"
#include "shortcuts.h"
#include "auth_session.h"
#include "langloaderplain.h"
#include "observer_peer.h"
#include "fileuploader.h"
#include "mainwidget.h"
#include "mtproto/dc_options.h"
#include "mtproto/mtp_instance.h"
#include "media/player/media_player_instance.h"
#include "window/notifications_manager.h"
#include "window/themes/window_theme.h"
#include "history/history_location_manager.h"
#include "ui/widgets/tooltip.h"
#include "ui/filedialog.h"

namespace {

Messenger *SingleInstance = nullptr;

} // namespace

Messenger *Messenger::InstancePointer() {
	return SingleInstance;
}

struct Messenger::Private {
	MTP::Instance::Config mtpConfig;
};

Messenger::Messenger() : QObject()
, _private(std::make_unique<Private>()) {
	t_assert(SingleInstance == nullptr);
	SingleInstance = this;

	Fonts::start();

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
	Window::Notifications::start();

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
		_window->setupPasscode();
	} else {
		if (AuthSession::Current()) {
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
	t_assert(!_mtproto);
	_private->mtpConfig.mainDcId = mainDcId;
}

void Messenger::setMtpKey(MTP::DcId dcId, const MTP::AuthKey::Data &keyData) {
	t_assert(!_mtproto);
	_private->mtpConfig.keys.insert(std::make_pair(dcId, keyData));
}

QByteArray Messenger::serializeMtpAuthorization() const {
	auto serialize = [this](auto keysCount, auto mainDcId, auto writeKeys) {
		auto result = QByteArray();
		auto size = sizeof(qint32) + sizeof(qint32) + sizeof(qint32); // userId + mainDcId + keys count
		size += keysCount * (sizeof(qint32) + MTP::AuthKey::Data().size());
		result.reserve(size);
		{
			QBuffer buffer(&result);
			if (!buffer.open(QIODevice::WriteOnly)) {
				LOG(("MTP Error: could not open buffer to serialize mtp authorization."));
				return result;
			}
			QDataStream stream(&buffer);
			stream.setVersion(QDataStream::Qt_5_1);

			stream << qint32(AuthSession::CurrentUserId()) << qint32(mainDcId) << qint32(keysCount);
			writeKeys(stream);
		}
		return result;
	};
	if (_mtproto) {
		auto keys = _mtproto->getKeysForWrite();
		return serialize(keys.size(), _mtproto->mainDcId(), [&keys](QDataStream &stream) {
			for (auto &key : keys) {
				stream << qint32(key->getDC());
				key->write(stream);
			}
		});
	}
	auto &keys = _private->mtpConfig.keys;
	return serialize(keys.size(), _private->mtpConfig.mainDcId, [&keys](QDataStream &stream) {
		for (auto &key : keys) {
			stream << qint32(key.first);
			stream.writeRawData(key.second.data(), key.second.size());
		}
	});
}

void Messenger::setMtpAuthorization(const QByteArray &serialized) {
	t_assert(!_mtproto);
	t_assert(!authSession());

	auto readonly = serialized;
	QBuffer buffer(&readonly);
	if (!buffer.open(QIODevice::ReadOnly)) {
		LOG(("MTP Error: could not open serialized mtp authorization for reading."));
		return;
	}
	QDataStream stream(&buffer);
	stream.setVersion(QDataStream::Qt_5_1);

	qint32 userId = 0, mainDcId = 0, count = 0;
	stream >> userId >> mainDcId >> count;
	if (stream.status() != QDataStream::Ok) {
		LOG(("MTP Error: could not read main fields from serialized mtp authorization."));
		return;
	}

	if (userId) {
		authSessionCreate(userId);
	}
	_private->mtpConfig.mainDcId = mainDcId;
	for (auto i = 0; i != count; ++i) {
		qint32 dcId = 0;
		MTP::AuthKey::Data keyData;
		stream >> dcId;
		stream.readRawData(keyData.data(), keyData.size());
		if (stream.status() != QDataStream::Ok) {
			LOG(("MTP Error: could not read key from serialized mtp authorization."));
			return;
		}
		_private->mtpConfig.keys.insert(std::make_pair(dcId, keyData));
	}
}

void Messenger::startMtp() {
	t_assert(!_mtproto);
	_mtproto = std::make_unique<MTP::Instance>(_dcOptions.get(), std::move(_private->mtpConfig));

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
	QCoreApplication::instance()->installTranslator(_translator = new Translator());
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
	if (!AuthSession::Current()) return;

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

void Messenger::killDownloadSessionsStart(int32 dc) {
	if (killDownloadSessionTimes.constFind(dc) == killDownloadSessionTimes.cend()) {
		killDownloadSessionTimes.insert(dc, getms() + MTPAckSendWaiting + MTPKillFileSessionTimeout);
	}
	if (!killDownloadSessionsTimer.isActive()) {
		killDownloadSessionsTimer.start(MTPAckSendWaiting + MTPKillFileSessionTimeout + 5);
	}
}

void Messenger::killDownloadSessionsStop(int32 dc) {
	killDownloadSessionTimes.remove(dc);
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

void Messenger::call_handleFileDialogQueue() {
	while (true) {
		if (!FileDialog::processQuery()) {
			return;
		}
	}
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
			for (int j = 0; j < MTPDownloadSessionsCount; ++j) {
				MTP::stopSession(MTP::dldDcId(i.key(), j));
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
	if (!AuthSession::Current()) return;

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
	_authSession = std::make_unique<AuthSession>(userId);
}

void Messenger::authSessionDestroy() {
	_authSession.reset();
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

	MTPDphoto::Flags photoFlags = 0;
	auto photo = MTP_photo(MTP_flags(photoFlags), MTP_long(id), MTP_long(0), MTP_int(unixtime()), MTP_vector<MTPPhotoSize>(photoSizes));

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
			if ((cAlphaVersion() || cBetaVersion()) && Local::oldMapVersion() < 1000010) {
				versionFeatures = QString::fromUtf8("\xe2\x80\x94 Support for more emoji.\n\xe2\x80\x94 Bug fixes and other minor improvements.");
			} else if (!(cAlphaVersion() || cBetaVersion()) && Local::oldMapVersion() < 1000012) {
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

void Messenger::prepareToDestroy() {
	_window.reset();
	_mtproto.reset();
}

Messenger::~Messenger() {
	t_assert(SingleInstance == this);
	SingleInstance = nullptr;

	Shortcuts::finish();

	App::clearHistories();

	Window::Notifications::finish();

	anim::stopManager();

	stopWebLoadManager();
	App::deinitMedia();
	deinitLocationManager();

	delete base::take(_uploader);
	delete base::take(_translator);

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
