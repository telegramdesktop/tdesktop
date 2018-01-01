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
#pragma once

#include "base/observer.h"
#include "mtproto/auth_key.h"
#include "base/timer.h"

class AuthSession;
class AuthSessionData;
class MainWidget;
class FileUploader;
class Translator;
class MediaView;

namespace Core {
class Launcher;
} // namespace Core

namespace App {
void quit();
} // namespace App

namespace MTP {
class DcOptions;
class Instance;
class AuthKey;
using AuthKeyPtr = std::shared_ptr<AuthKey>;
using AuthKeysList = std::vector<AuthKeyPtr>;
} // namespace MTP

namespace Media {
namespace Audio {
class Instance;
} // namespace Audio
} // namespace Media

namespace Lang {
class Instance;
class Translator;
class CloudManager;
} // namespace Lang

class Messenger final : public QObject, public RPCSender, private base::Subscriber {
	Q_OBJECT

public:
	Messenger(not_null<Core::Launcher*> launcher);

	Messenger(const Messenger &other) = delete;
	Messenger &operator=(const Messenger &other) = delete;

	~Messenger();

	not_null<Core::Launcher*> launcher() const {
		return _launcher;
	}

	// Windows interface.
	MainWindow *getActiveWindow() const;
	bool closeActiveWindow();
	bool minimizeActiveWindow();
	QWidget *getFileDialogParent();
	QWidget *getGlobalShortcutParent() {
		return &_globalShortcutParent;
	}

	// MediaView interface.
	void checkMediaViewActivation();
	bool hideMediaView();
	void showPhoto(not_null<const PhotoOpenClickHandler*> link);
	void showPhoto(not_null<PhotoData*> photo, HistoryItem *item);
	void showPhoto(not_null<PhotoData*> photo, not_null<PeerData*> item);
	void showDocument(not_null<DocumentData*> document, HistoryItem *item);
	PeerData *ui_getPeerForMouseAction();

	QPoint getPointForCallPanelCenter() const;
	QImage logo() const {
		return _logo;
	}
	QImage logoNoMargin() const {
		return _logoNoMargin;
	}

	static Messenger *InstancePointer();
	static Messenger &Instance() {
		auto result = InstancePointer();
		Assert(result != nullptr);
		return *result;
	}

	// MTProto components.
	MTP::DcOptions *dcOptions() {
		return _dcOptions.get();
	}

	// Set from legacy storage.
	void setMtpMainDcId(MTP::DcId mainDcId);
	void setMtpKey(MTP::DcId dcId, const MTP::AuthKey::Data &keyData);
	void setAuthSessionUserId(UserId userId);
	void setAuthSessionFromStorage(std::unique_ptr<AuthSessionData> data);
	AuthSessionData *getAuthSessionData();

	// Serialization.
	QByteArray serializeMtpAuthorization() const;
	void setMtpAuthorization(const QByteArray &serialized);

	void startMtp();
	MTP::Instance *mtp() {
		return _mtproto.get();
	}
	void suggestMainDcId(MTP::DcId mainDcId);
	void destroyStaleAuthorizationKeys();

	// AuthSession component.
	AuthSession *authSession() {
		return _authSession.get();
	}
	Lang::Instance &langpack() {
		return *_langpack;
	}
	Lang::CloudManager *langCloudManager() {
		return _langCloudManager.get();
	}
	void authSessionCreate(UserId userId);
	void authSessionDestroy();
	base::Observable<void> &authSessionChanged() {
		return _authSessionChanged;
	}

	// Media component.
	Media::Audio::Instance &audio() {
		return *_audio;
	}

	// Internal links.
	void setInternalLinkDomain(const QString &domain) const;
	QString createInternalLink(const QString &query) const;
	QString createInternalLinkFull(const QString &query) const;
	void checkStartUrl();
	bool openLocalUrl(const QString &url);

	void uploadProfilePhoto(QImage &&tosend, const PeerId &peerId);
	void regPhotoUpdate(const PeerId &peer, const FullMsgId &msgId);
	bool isPhotoUpdating(const PeerId &peer);
	void cancelPhotoUpdate(const PeerId &peer);

	void selfPhotoCleared(const MTPUserProfilePhoto &result);
	void chatPhotoCleared(PeerId peer, const MTPUpdates &updates);
	void selfPhotoDone(const MTPphotos_Photo &result);
	void chatPhotoDone(PeerId peerId, const MTPUpdates &updates);
	bool peerPhotoFailed(PeerId peerId, const RPCError &e);
	void peerClearPhoto(PeerId peer);

	void writeUserConfigIn(TimeMs ms);

	void killDownloadSessionsStart(MTP::DcId dcId);
	void killDownloadSessionsStop(MTP::DcId dcId);

	void checkLocalTime();
	void setupPasscode();
	void clearPasscode();
	base::Observable<void> &passcodedChanged() {
		return _passcodedChanged;
	}

	void registerLeaveSubscription(QWidget *widget);
	void unregisterLeaveSubscription(QWidget *widget);

	void quitPreventFinished();

	void handleAppActivated();
	void handleAppDeactivated();

	void call_handleHistoryUpdate();
	void call_handleUnreadCounterUpdate();
	void call_handleDelayedPeerUpdates();
	void call_handleObservables();

	void callDelayed(int duration, base::lambda_once<void()> &&lambda) {
		_callDelayedTimer.call(duration, std::move(lambda));
	}

protected:
	bool eventFilter(QObject *object, QEvent *event) override;

signals:
	void peerPhotoDone(PeerId peer);
	void peerPhotoFail(PeerId peer);

public slots:
	void onAllKeysDestroyed();

	void photoUpdated(const FullMsgId &msgId, bool silent, const MTPInputFile &file);

	void onSwitchDebugMode();
	void onSwitchWorkMode();
	void onSwitchTestMode();

	void killDownloadSessions();
	void onAppStateChanged(Qt::ApplicationState state);

private:
	void destroyMtpKeys(MTP::AuthKeysList &&keys);
	void startLocalStorage();

	friend void App::quit();
	static void QuitAttempt();
	void quitDelayed();

	void loggedOut();

	not_null<Core::Launcher*> _launcher;

	QMap<FullMsgId, PeerId> photoUpdates;

	QMap<MTP::DcId, TimeMs> killDownloadSessionTimes;
	SingleTimer killDownloadSessionsTimer;

	// Some fields are just moved from the declaration.
	struct Private;
	const std::unique_ptr<Private> _private;

	QWidget _globalShortcutParent;

	std::unique_ptr<MainWindow> _window;
	std::unique_ptr<MediaView> _mediaView;
	std::unique_ptr<Lang::Instance> _langpack;
	std::unique_ptr<Lang::CloudManager> _langCloudManager;
	std::unique_ptr<Lang::Translator> _translator;
	std::unique_ptr<MTP::DcOptions> _dcOptions;
	std::unique_ptr<MTP::Instance> _mtproto;
	std::unique_ptr<MTP::Instance> _mtprotoForKeysDestroy;
	std::unique_ptr<AuthSession> _authSession;
	base::Observable<void> _authSessionChanged;
	base::Observable<void> _passcodedChanged;

	std::unique_ptr<Media::Audio::Instance> _audio;
	QImage _logo;
	QImage _logoNoMargin;

	base::DelayedCallTimer _callDelayedTimer;

	struct LeaveSubscription {
		LeaveSubscription(
			QPointer<QWidget> pointer,
			rpl::lifetime &&subscription)
		: pointer(pointer), subscription(std::move(subscription)) {
		}

		QPointer<QWidget> pointer;
		rpl::lifetime subscription;
	};
	std::vector<LeaveSubscription> _leaveSubscriptions;

};
