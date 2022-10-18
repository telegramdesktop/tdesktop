/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/mtproto_auth_key.h"
#include "mtproto/mtproto_proxy_data.h"
#include "base/timer.h"

class History;

namespace Platform {
class Integration;
} // namespace Platform

namespace Storage {
class Databases;
} // namespace Storage

namespace Window {
class Controller;
} // namespace Window

namespace Window {
namespace Notifications {
class System;
} // namespace Notifications
} // namespace Window

namespace ChatHelpers {
class EmojiKeywords;
} // namespace ChatHelpers

namespace Main {
class Domain;
class Account;
class Session;
} // namespace Main

namespace Ui {
namespace Animations {
class Manager;
} // namespace Animations
namespace Emoji {
class UniversalImages;
} // namespace Emoji
class BoxContent;
} // namespace Ui

namespace MTP {
class Config;
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
struct OpenRequest;
} // namespace View
namespace Player {
class FloatController;
class FloatDelegate;
} // namespace Player
} // namespace Media

namespace Lang {
class Instance;
class Translator;
class CloudManager;
} // namespace Lang

namespace Data {
struct CloudTheme;
class DownloadManager;
} // namespace Data

namespace Stickers {
class EmojiImageLoader;
} // namespace Stickers

namespace Export {
class Manager;
} // namespace Export

namespace Calls {
class Instance;
} // namespace Calls

namespace Core {

class Launcher;
struct LocalUrlHandler;
class Settings;
class Tray;

enum class LaunchState {
	Running,
	QuitRequested,
	QuitProcessed,
};

enum class QuitReason {
	Default,
	QtQuitEvent,
};

class Application final : public QObject {
public:
	struct ProxyChange {
		MTP::ProxyData was;
		MTP::ProxyData now;
	};

	Application(not_null<Launcher*> launcher);
	Application(const Application &other) = delete;
	Application &operator=(const Application &other) = delete;
	~Application();

	[[nodiscard]] Launcher &launcher() const {
		return *_launcher;
	}
	[[nodiscard]] Platform::Integration &platformIntegration() const {
		return *_platformIntegration;
	}

	void run();

	[[nodiscard]] Ui::Animations::Manager &animationManager() const {
		return *_animationsManager;
	}
	[[nodiscard]] Window::Notifications::System &notifications() const {
		Expects(_notifications != nullptr);

		return *_notifications;
	}
	[[nodiscard]] Data::DownloadManager &downloadManager() const {
		return *_downloadManager;
	}
	[[nodiscard]] Tray &tray() const {
		return *_tray;
	}

	// Windows interface.
	bool hasActiveWindow(not_null<Main::Session*> session) const;
	[[nodiscard]] Window::Controller *primaryWindow() const;
	[[nodiscard]] Window::Controller *activeWindow() const;
	[[nodiscard]] Window::Controller *separateWindowForPeer(
		not_null<PeerData*> peer) const;
	Window::Controller *ensureSeparateWindowForPeer(
		not_null<PeerData*> peer,
		MsgId showAtMsgId);
	void closeWindow(not_null<Window::Controller*> window);
	void windowActivated(not_null<Window::Controller*> window);
	bool closeActiveWindow();
	bool minimizeActiveWindow();
	[[nodiscard]] QWidget *getFileDialogParent();
	void notifyFileDialogShown(bool shown);
	void checkSystemDarkMode();
	[[nodiscard]] bool isActiveForTrayMenu() const;
	void closeChatFromWindows(not_null<PeerData*> peer);

	// Media view interface.
	bool hideMediaView();

	[[nodiscard]] QPoint getPointForCallPanelCenter() const;

	void startSettingsAndBackground();
	[[nodiscard]] Settings &settings();
	void saveSettingsDelayed(crl::time delay = kDefaultSaveDelay);
	void saveSettings();

	// Fallback config and proxy.
	[[nodiscard]] MTP::Config &fallbackProductionConfig() const;
	void refreshFallbackProductionConfig(const MTP::Config &config);
	void constructFallbackProductionConfig(const QByteArray &serialized);
	void setCurrentProxy(
		const MTP::ProxyData &proxy,
		MTP::ProxyData::Settings settings);
	[[nodiscard]] rpl::producer<ProxyChange> proxyChanges() const;
	void badMtprotoConfigurationError();

	// Databases.
	[[nodiscard]] Storage::Databases &databases() {
		return *_databases;
	}

	// Domain component.
	[[nodiscard]] Main::Domain &domain() const {
		return *_domain;
	}
	[[nodiscard]] Main::Account &activeAccount() const;
	[[nodiscard]] bool someSessionExists() const;
	[[nodiscard]] Export::Manager &exportManager() const {
		return *_exportManager;
	}
	[[nodiscard]] bool exportPreventsQuit();

	// Main::Session component.
	Main::Session *maybePrimarySession() const;
	[[nodiscard]] int unreadBadge() const;
	[[nodiscard]] bool unreadBadgeMuted() const;
	[[nodiscard]] rpl::producer<> unreadBadgeChanges() const;

	// Media component.
	[[nodiscard]] Media::Audio::Instance &audio() {
		return *_audio;
	}

	// Langpack and emoji keywords.
	[[nodiscard]] Lang::Instance &langpack() {
		return *_langpack;
	}
	[[nodiscard]] Lang::CloudManager *langCloudManager() {
		return _langCloudManager.get();
	}
	[[nodiscard]] bool offerLegacyLangPackSwitch() const;
	[[nodiscard]] bool canApplyLangPackWithoutRestart() const;
	[[nodiscard]] ChatHelpers::EmojiKeywords &emojiKeywords() {
		return *_emojiKeywords;
	}
	[[nodiscard]] auto emojiImageLoader() const
	-> const crl::object_on_queue<Stickers::EmojiImageLoader> & {
		return _emojiImageLoader;
	}

	// Internal links.
	void checkStartUrl();
	bool openLocalUrl(const QString &url, QVariant context);
	bool openInternalUrl(const QString &url, QVariant context);
	[[nodiscard]] QString changelogLink() const;

	// Float player.
	void setDefaultFloatPlayerDelegate(
		not_null<Media::Player::FloatDelegate*> delegate);
	void replaceFloatPlayerDelegate(
		not_null<Media::Player::FloatDelegate*> replacement);
	void restoreFloatPlayerDelegate(
		not_null<Media::Player::FloatDelegate*> replacement);
	[[nodiscard]] rpl::producer<FullMsgId> floatPlayerClosed() const;

	// Calls.
	Calls::Instance &calls() const {
		return *_calls;
	}

	void logout(Main::Account *account = nullptr);
	void logoutWithChecks(Main::Account *account);
	void forceLogOut(
		not_null<Main::Account*> account,
		const TextWithEntities &explanation);
	[[nodiscard]] bool uploadPreventsQuit();
	[[nodiscard]] bool downloadPreventsQuit();
	void checkLocalTime();
	void lockByPasscode();
	void maybeLockByPasscode();
	void unlockPasscode();
	[[nodiscard]] bool passcodeLocked() const;
	rpl::producer<bool> passcodeLockChanges() const;
	rpl::producer<bool> passcodeLockValue() const;

	void checkAutoLock(crl::time lastNonIdleTime = 0);
	void checkAutoLockIn(crl::time time);
	void localPasscodeChanged();

	[[nodiscard]] bool preventsQuit(QuitReason reason);

	[[nodiscard]] crl::time lastNonIdleTime() const;
	void updateNonIdle();

	void registerLeaveSubscription(not_null<QWidget*> widget);
	void unregisterLeaveSubscription(not_null<QWidget*> widget);

	// Sandbox interface.
	void postponeCall(FnMut<void()> &&callable);
	void refreshGlobalProxy();

	void quitPreventFinished();

	void handleAppActivated();
	void handleAppDeactivated();
	[[nodiscard]] rpl::producer<bool> appDeactivatedValue() const;

	void switchDebugMode();
	void switchFreeType();
	void writeInstallBetaVersionsSetting();

	void preventOrInvoke(Fn<void()> &&callback);

	void call_handleObservables();

	// Global runtime variables.
	void setScreenIsLocked(bool locked);
	bool screenIsLocked() const;

	static void RegisterUrlScheme();

protected:
	bool eventFilter(QObject *object, QEvent *event) override;

private:
	static constexpr auto kDefaultSaveDelay = crl::time(1000);

	friend bool IsAppLaunched();
	friend Application &App();

	void clearEmojiSourceImages();
	[[nodiscard]] auto prepareEmojiSourceImages()
		-> std::shared_ptr<Ui::Emoji::UniversalImages>;
	void startLocalStorage();
	void startShortcuts();
	void startDomain();
	void startEmojiImageLoader();
	void startSystemDarkModeViewer();
	void startTray();

	void enumerateWindows(
		Fn<void(not_null<Window::Controller*>)> callback) const;
	void processSecondaryWindow(not_null<Window::Controller*> window);

	friend void QuitAttempt();
	void quitDelayed();
	[[nodiscard]] bool readyToQuit();

	void showOpenGLCrashNotification();
	void clearPasscodeLock();

	bool openCustomUrl(
		const QString &protocol,
		const std::vector<LocalUrlHandler> &handlers,
		const QString &url,
		const QVariant &context);

	static Application *Instance;
	struct InstanceSetter {
		InstanceSetter(not_null<Application*> instance) {
			Expects(Instance == nullptr);

			Instance = instance;
		}
	};
	InstanceSetter _setter = { this };

	const not_null<Launcher*> _launcher;
	rpl::event_stream<ProxyChange> _proxyChanges;

	// Some fields are just moved from the declaration.
	struct Private;
	const std::unique_ptr<Private> _private;
	const std::unique_ptr<Platform::Integration> _platformIntegration;

	const std::unique_ptr<Storage::Databases> _databases;
	const std::unique_ptr<Ui::Animations::Manager> _animationsManager;
	crl::object_on_queue<Stickers::EmojiImageLoader> _emojiImageLoader;
	base::Timer _clearEmojiImageLoaderTimer;
	const std::unique_ptr<Media::Audio::Instance> _audio;
	mutable std::unique_ptr<MTP::Config> _fallbackProductionConfig;

	// Notifications should be destroyed before _audio, after _domain.
	// Mutable because is created in run() after OpenSSL is inited.
	std::unique_ptr<Window::Notifications::System> _notifications;

	const std::unique_ptr<Data::DownloadManager> _downloadManager;
	const std::unique_ptr<Main::Domain> _domain;
	const std::unique_ptr<Export::Manager> _exportManager;
	const std::unique_ptr<Calls::Instance> _calls;
	std::unique_ptr<Window::Controller> _primaryWindow;
	base::flat_map<
		not_null<History*>,
		std::unique_ptr<Window::Controller>> _secondaryWindows;
	Window::Controller *_lastActiveWindow = nullptr;

	std::unique_ptr<Media::View::OverlayWidget> _mediaView;
	const std::unique_ptr<Lang::Instance> _langpack;
	const std::unique_ptr<Lang::CloudManager> _langCloudManager;
	const std::unique_ptr<ChatHelpers::EmojiKeywords> _emojiKeywords;
	std::unique_ptr<Lang::Translator> _translator;
	QPointer<Ui::BoxContent> _badProxyDisableBox;

	const std::unique_ptr<Tray> _tray;

	std::unique_ptr<Media::Player::FloatController> _floatPlayers;
	Media::Player::FloatDelegate *_defaultFloatPlayerDelegate = nullptr;
	Media::Player::FloatDelegate *_replacementFloatPlayerDelegate = nullptr;

	rpl::variable<bool> _passcodeLock;
	bool _screenIsLocked = false;

	crl::time _shouldLockAt = 0;
	base::Timer _autoLockTimer;

	std::optional<base::Timer> _saveSettingsTimer;

	struct LeaveFilter {
		std::vector<QPointer<QWidget>> registered;
		QPointer<QObject> filter;
	};
	base::flat_map<not_null<QWidget*>, LeaveFilter> _leaveFilters;

	rpl::event_stream<Media::View::OpenRequest> _openInMediaViewRequests;

	rpl::lifetime _lifetime;

	crl::time _lastNonIdleTime = 0;

};

[[nodiscard]] bool IsAppLaunched();
[[nodiscard]] Application &App();

[[nodiscard]] LaunchState CurrentLaunchState();
void SetLaunchState(LaunchState state);

void Quit(QuitReason reason = QuitReason::Default);
[[nodiscard]] bool Quitting();

void Restart();

} // namespace Core
