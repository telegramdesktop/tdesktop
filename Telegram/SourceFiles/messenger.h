/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/observer.h"
#include "mtproto/auth_key.h"
#include "base/timer.h"

class AuthSession;
class AuthSessionSettings;
class MainWidget;
class FileUploader;
class Translator;
class MediaView;
class BoxContent;

namespace Storage {
class Databases;
} // namespace Storage

namespace Core {
class Launcher;
} // namespace Core

namespace Window {
struct TermsLock;
} // namespace Window

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
	void setCurrentProxy(const ProxyData &proxy, bool enabled);
	void badMtprotoConfigurationError();

	// Set from legacy storage.
	void setMtpMainDcId(MTP::DcId mainDcId);
	void setMtpKey(MTP::DcId dcId, const MTP::AuthKey::Data &keyData);
	void setAuthSessionUserId(UserId userId);
	void setAuthSessionFromStorage(
		std::unique_ptr<AuthSessionSettings> data);
	AuthSessionSettings *getAuthSessionSettings();

	// Serialization.
	QByteArray serializeMtpAuthorization() const;
	void setMtpAuthorization(const QByteArray &serialized);

	void startMtp();
	MTP::Instance *mtp() {
		return _mtproto.get();
	}
	void suggestMainDcId(MTP::DcId mainDcId);
	void destroyStaleAuthorizationKeys();

	// Databases
	Storage::Databases &databases() {
		return *_databases;
	}

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
	base::Observable<void> &authSessionChanged() {
		return _authSessionChanged;
	}
	void logOut();

	// Media component.
	Media::Audio::Instance &audio() {
		return *_audio;
	}

	// Internal links.
	void setInternalLinkDomain(const QString &domain) const;
	QString createInternalLink(const QString &query) const;
	QString createInternalLinkFull(const QString &query) const;
	void checkStartUrl();
	bool openLocalUrl(const QString &url, QVariant context);

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

	void forceLogOut(const TextWithEntities &explanation);
	void checkLocalTime();
	void lockByPasscode();
	void unlockPasscode();
	[[nodiscard]] bool passcodeLocked() const;
	rpl::producer<bool> passcodeLockChanges() const;
	rpl::producer<bool> passcodeLockValue() const;

	void lockByTerms(const Window::TermsLock &data);
	void unlockTerms();
	[[nodiscard]] base::optional<Window::TermsLock> termsLocked() const;
	rpl::producer<bool> termsLockChanges() const;
	rpl::producer<bool> termsLockValue() const;
	void termsDeleteNow();

	[[nodiscard]] bool locked() const;
	rpl::producer<bool> lockChanges() const;
	rpl::producer<bool> lockValue() const;

	void registerLeaveSubscription(QWidget *widget);
	void unregisterLeaveSubscription(QWidget *widget);

	void quitPreventFinished();

	void handleAppActivated();
	void handleAppDeactivated();

	void call_handleUnreadCounterUpdate();
	void call_handleDelayedPeerUpdates();
	void call_handleObservables();

	void callDelayed(int duration, FnMut<void()> &&lambda) {
		_callDelayedTimer.call(duration, std::move(lambda));
	}

protected:
	bool eventFilter(QObject *object, QEvent *event) override;

signals:
	void peerPhotoDone(PeerId peer);
	void peerPhotoFail(PeerId peer);

public slots:
	void onAllKeysDestroyed();

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

	void photoUpdated(const FullMsgId &msgId, const MTPInputFile &file);
	void resetAuthorizationKeys();
	void authSessionDestroy();
	void clearPasscodeLock();
	void loggedOut();

	not_null<Core::Launcher*> _launcher;

	QMap<FullMsgId, PeerId> photoUpdates;

	QMap<MTP::DcId, TimeMs> killDownloadSessionTimes;
	SingleTimer killDownloadSessionsTimer;

	// Some fields are just moved from the declaration.
	struct Private;
	const std::unique_ptr<Private> _private;

	QWidget _globalShortcutParent;

	std::unique_ptr<Storage::Databases> _databases;
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
	QPointer<BoxContent> _badProxyDisableBox;

	// While profile photo uploading is not moved to apiwrap.
	rpl::lifetime _uploaderSubscription;

	std::unique_ptr<Media::Audio::Instance> _audio;
	QImage _logo;
	QImage _logoNoMargin;

	rpl::variable<bool> _passcodeLock;
	rpl::event_stream<bool> _termsLockChanges;
	std::unique_ptr<Window::TermsLock> _termsLock;

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
