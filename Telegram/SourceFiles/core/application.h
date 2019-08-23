/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/core_settings.h"
#include "mtproto/auth_key.h"
#include "base/observer.h"
#include "base/timer.h"

class MainWindow;
class MainWidget;
class FileUploader;
class Translator;
class BoxContent;

namespace Storage {
class Databases;
} // namespace Storage

namespace Window {
struct TermsLock;
class Controller;
} // namespace Window

namespace ChatHelpers {
class EmojiKeywords;
} // namespace ChatHelpers

namespace App {
void quit();
} // namespace App

namespace Main {
class Account;
} // namespace Main

namespace Ui {
namespace Animations {
class Manager;
} // namespace Animations
} // namespace Ui

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
namespace View {
class OverlayWidget;
} // namespace View
} // namespace Media

namespace Lang {
class Instance;
class Translator;
class CloudManager;
} // namespace Lang

namespace Core {

class Launcher;
struct LocalUrlHandler;

class Application final : public QObject, private base::Subscriber {
public:
	Application(not_null<Launcher*> launcher);
	Application(const Application &other) = delete;
	Application &operator=(const Application &other) = delete;
	~Application();

	not_null<Launcher*> launcher() const {
		return _launcher;
	}

	void run();

	Ui::Animations::Manager &animationManager() const {
		return *_animationsManager;
	}

	// Windows interface.
	Window::Controller *activeWindow() const;
	bool closeActiveWindow();
	bool minimizeActiveWindow();
	QWidget *getFileDialogParent();

	// Media view interface.
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

	[[nodiscard]] Settings &settings() {
		return _settings;
	}
	void moveSettingsFrom(Settings &&other);
	void saveSettingsDelayed(crl::time delay = kDefaultSaveDelay);

	// Dc options and proxy.
	MTP::DcOptions *dcOptions() {
		return _dcOptions.get();
	}
	struct ProxyChange {
		ProxyData was;
		ProxyData now;
	};
	void setCurrentProxy(
		const ProxyData &proxy,
		ProxyData::Settings settings);
	[[nodiscard]] rpl::producer<ProxyChange> proxyChanges() const;
	void badMtprotoConfigurationError();

	// Databases.
	[[nodiscard]] Storage::Databases &databases() {
		return *_databases;
	}

	// Account component.
	[[nodiscard]] Main::Account &activeAccount() const {
		return *_account;
	}
	[[nodiscard]] bool exportPreventsQuit();

	// Main::Session component.
	[[nodiscard]] int unreadBadge() const;
	bool unreadBadgeMuted() const;

	// Media component.
	Media::Audio::Instance &audio() {
		return *_audio;
	}

	// Langpack and emoji keywords.
	Lang::Instance &langpack() {
		return *_langpack;
	}
	Lang::CloudManager *langCloudManager() {
		return _langCloudManager.get();
	}
	ChatHelpers::EmojiKeywords &emojiKeywords() {
		return *_emojiKeywords;
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

	[[nodiscard]] bool locked() const;
	rpl::producer<bool> lockChanges() const;
	rpl::producer<bool> lockValue() const;

	[[nodiscard]] crl::time lastNonIdleTime() const;
	void updateNonIdle();

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

protected:
	bool eventFilter(QObject *object, QEvent *event) override;

private:
	static constexpr auto kDefaultSaveDelay = crl::time(1000);

	friend bool IsAppLaunched();
	friend Application &App();

	void startLocalStorage();
	void startShortcuts();

	void stateChanged(Qt::ApplicationState state);

	friend void App::quit();
	static void QuitAttempt();
	void quitDelayed();

	void clearPasscodeLock();

	static Application *Instance;
	struct InstanceSetter {
		InstanceSetter(not_null<Application*> instance) {
			Expects(Instance == nullptr);

			Instance = instance;
		}
	};
	InstanceSetter _setter = { this };

	not_null<Launcher*> _launcher;
	rpl::event_stream<ProxyChange> _proxyChanges;

	// Some fields are just moved from the declaration.
	struct Private;
	const std::unique_ptr<Private> _private;
	Settings _settings;

	const std::unique_ptr<Storage::Databases> _databases;
	const std::unique_ptr<Ui::Animations::Manager> _animationsManager;
	const std::unique_ptr<MTP::DcOptions> _dcOptions;
	const std::unique_ptr<Main::Account> _account;
	std::unique_ptr<Window::Controller> _window;
	std::unique_ptr<Media::View::OverlayWidget> _mediaView;
	const std::unique_ptr<Lang::Instance> _langpack;
	std::unique_ptr<Lang::CloudManager> _langCloudManager;
	const std::unique_ptr<ChatHelpers::EmojiKeywords> _emojiKeywords;
	std::unique_ptr<Lang::Translator> _translator;
	base::Observable<void> _passcodedChanged;
	QPointer<BoxContent> _badProxyDisableBox;

	const std::unique_ptr<Media::Audio::Instance> _audio;
	const QImage _logo;
	const QImage _logoNoMargin;

	rpl::variable<bool> _passcodeLock;
	rpl::event_stream<bool> _termsLockChanges;
	std::unique_ptr<Window::TermsLock> _termsLock;

	base::DelayedCallTimer _callDelayedTimer;
	base::Timer _saveSettingsTimer;

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

	crl::time _lastNonIdleTime = 0;

};

[[nodiscard]] bool IsAppLaunched();
[[nodiscard]] Application &App();

} // namespace Core
