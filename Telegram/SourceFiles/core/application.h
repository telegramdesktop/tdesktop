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

namespace Core {

class Launcher;
struct LocalUrlHandler;

class Application final
	: public QObject
	, public RPCSender
	, private base::Subscriber {
public:
	Application(not_null<Launcher*> launcher);

	Application(const Application &other) = delete;
	Application &operator=(const Application &other) = delete;

	not_null<Launcher*> launcher() const {
		return _launcher;
	}

	void run();

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

	// MTProto components.
	MTP::DcOptions *dcOptions() {
		return _dcOptions.get();
	}
	void setCurrentProxy(
		const ProxyData &proxy,
		ProxyData::Settings settings);
	void badMtprotoConfigurationError();

	// Set from legacy storage.
	void setMtpMainDcId(MTP::DcId mainDcId);
	void setMtpKey(MTP::DcId dcId, const MTP::AuthKey::Data &keyData);
	void setAuthSessionUserId(UserId userId);
	void setAuthSessionFromStorage(
		std::unique_ptr<AuthSessionSettings> data,
		QByteArray &&selfSerialized,
		int32 selfStreamVersion);
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
	void authSessionCreate(const MTPUser &user);
	base::Observable<void> &authSessionChanged() {
		return _authSessionChanged;
	}
	int unreadBadge() const;
	bool unreadBadgeMuted() const;
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

	void forceLogOut(const TextWithEntities &explanation);
	void checkLocalTime();
	void lockByPasscode();
	void unlockPasscode();
	[[nodiscard]] bool passcodeLocked() const;
	rpl::producer<bool> passcodeLockChanges() const;
	rpl::producer<bool> passcodeLockValue() const;

	void lockByTerms(const Window::TermsLock &data);
	void unlockTerms();
	[[nodiscard]] std::optional<Window::TermsLock> termsLocked() const;
	rpl::producer<bool> termsLockChanges() const;
	rpl::producer<bool> termsLockValue() const;
	void termsDeleteNow();

	[[nodiscard]] bool locked() const;
	rpl::producer<bool> lockChanges() const;
	rpl::producer<bool> lockValue() const;

	void registerLeaveSubscription(QWidget *widget);
	void unregisterLeaveSubscription(QWidget *widget);

	// Sandbox interface.
	void postponeCall(FnMut<void()> &&callable);
	void refreshGlobalProxy();
	void activateWindowDelayed(not_null<QWidget*> widget);
	void pauseDelayedWindowActivations();
	void resumeDelayedWindowActivations();
	void preventWindowActivation();

	void quitPreventFinished();

	void handleAppActivated();
	void handleAppDeactivated();

	void switchDebugMode();
	void switchWorkMode();
	void switchTestMode();
	void writeInstallBetaVersionsSetting();

	void call_handleUnreadCounterUpdate();
	void call_handleDelayedPeerUpdates();
	void call_handleObservables();

	void callDelayed(int duration, FnMut<void()> &&lambda) {
		_callDelayedTimer.call(duration, std::move(lambda));
	}

	~Application();

protected:
	bool eventFilter(QObject *object, QEvent *event) override;

private:
	void destroyMtpKeys(MTP::AuthKeysList &&keys);
	void allKeysDestroyed();

	void startLocalStorage();
	void startShortcuts();

	void stateChanged(Qt::ApplicationState state);

	friend void App::quit();
	static void QuitAttempt();
	void quitDelayed();

	void resetAuthorizationKeys();
	void authSessionDestroy();
	void clearPasscodeLock();
	void loggedOut();

	not_null<Launcher*> _launcher;

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

	rpl::lifetime _lifetime;

};

Application &App();

} // namespace Core
