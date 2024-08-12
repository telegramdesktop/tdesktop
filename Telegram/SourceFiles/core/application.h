/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "mtproto/mtproto_auth_key.h"
#include "mtproto/mtproto_proxy_data.h"
#include "window/window_separate_id.h"

class History;

namespace base {
class BatterySaving;
} // namespace base

namespace Platform {
class Integration;
} // namespace Platform

namespace Storage {
class Databases;
} // namespace Storage

namespace Window {
class Controller;
} // namespace Window

namespace Window::Notifications {
class System;
} // namespace Window::Notifications

namespace ChatHelpers {
class EmojiKeywords;
} // namespace ChatHelpers

namespace Main {
class Domain;
class Account;
class Session;
} // namespace Main

namespace Iv {
class Instance;
class DelegateImpl;
} // namespace Iv

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
class SystemMediaControlsManager;
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

namespace Webrtc {
class Environment;
} // namespace Webrtc

namespace Core {

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

extern const char kOptionSkipUrlSchemeRegister[];

class Application final : public QObject {
public:
	struct ProxyChange {
		MTP::ProxyData was;
		MTP::ProxyData now;
	};

	Application();
	Application(const Application &other) = delete;
	Application &operator=(const Application &other) = delete;
	~Application();

	void run();

	[[nodiscard]] Platform::Integration &platformIntegration() const {
		return *_platformIntegration;
	}
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
	[[nodiscard]] base::BatterySaving &batterySaving() const {
		return *_batterySaving;
	}

	// Windows interface.
	bool hasActiveWindow(not_null<Main::Session*> session) const;
	[[nodiscard]] bool savingPositionFor(
		not_null<Window::Controller*> window) const;
	[[nodiscard]] Window::Controller *findWindow(
		not_null<QWidget*> widget) const;
	[[nodiscard]] Window::Controller *activeWindow() const;
	[[nodiscard]] Window::Controller *activePrimaryWindow() const;
	[[nodiscard]] Window::Controller *separateWindowFor(
		Window::SeparateId id) const;
	Window::Controller *ensureSeparateWindowFor(
		Window::SeparateId id,
		MsgId showAtMsgId = 0);
	[[nodiscard]] Window::Controller *windowFor( // Doesn't auto-switch.
		Window::SeparateId id) const;
	[[nodiscard]] Window::Controller *windowForShowingHistory(
		not_null<PeerData*> peer) const;
	[[nodiscard]] Window::Controller *windowForShowingForum(
		not_null<Data::Forum*> forum) const;
	[[nodiscard]] bool closeNonLastAsync(
		not_null<Window::Controller*> window);
	void closeWindow(not_null<Window::Controller*> window);
	void windowActivated(not_null<Window::Controller*> window);
	bool closeActiveWindow();
	bool minimizeActiveWindow();
	bool toggleActiveWindowFullScreen();
	[[nodiscard]] QWidget *getFileDialogParent();
	void notifyFileDialogShown(bool shown);
	void checkSystemDarkMode();
	[[nodiscard]] bool isActiveForTrayMenu() const;
	void closeChatFromWindows(not_null<PeerData*> peer);
	void checkWindowId(not_null<Window::Controller*> window);
	void activate();

	// Media view interface.
	bool hideMediaView();

	[[nodiscard]] QPoint getPointForCallPanelCenter() const;
	[[nodiscard]] bool isSharingScreen() const;

	void startSettingsAndBackground();
	[[nodiscard]] Settings &settings();
	[[nodiscard]] const Settings &settings() const;
	void saveSettingsDelayed(crl::time delay = kDefaultSaveDelay);
	void saveSettings();

	[[nodiscard]] bool canReadDefaultDownloadPath() const;
	[[nodiscard]] bool canSaveFileWithoutAskingForPath() const;

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
	[[nodiscard]] Webrtc::Environment &mediaDevices() {
		return *_mediaDevices;
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
	void checkSendPaths();
	void checkFileOpen();
	bool openLocalUrl(const QString &url, QVariant context);
	bool openInternalUrl(const QString &url, QVariant context);
	[[nodiscard]] QString changelogLink() const;

	// Float player.
	void floatPlayerToggleGifsPaused(bool paused);
	[[nodiscard]] rpl::producer<FullMsgId> floatPlayerClosed() const;

	// Calls.
	Calls::Instance &calls() const {
		return *_calls;
	}

	// Iv.
	Iv::Instance &iv() const {
		return *_iv;
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
	void refreshApplicationIcon();

	void quitPreventFinished();

	void handleAppActivated();
	void handleAppDeactivated();
	[[nodiscard]] rpl::producer<bool> appDeactivatedValue() const;

	void materializeLocalDrafts();
	[[nodiscard]] rpl::producer<> materializeLocalDraftsRequests() const;

	void switchDebugMode();

	void preventOrInvoke(Fn<void()> &&callback);

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

	void autoRegisterUrlScheme();
	void clearEmojiSourceImages();
	[[nodiscard]] auto prepareEmojiSourceImages()
		-> std::shared_ptr<Ui::Emoji::UniversalImages>;
	void startLocalStorage();
	void startShortcuts();
	void startDomain();
	void startEmojiImageLoader();
	void startSystemDarkModeViewer();
	void startMediaView();
	void startTray();

	void createTray();
	void updateWindowTitles();
	void setLastActiveWindow(Window::Controller *window);
	void showAccount(not_null<Main::Account*> account);
	void enumerateWindows(
		Fn<void(not_null<Window::Controller*>)> callback) const;
	void processCreatedWindow(not_null<Window::Controller*> window);
	void refreshApplicationIcon(Main::Session *session);

	friend void QuitAttempt();
	void quitDelayed();
	[[nodiscard]] bool readyToQuit();

	void showOpenGLCrashNotification();
	void clearPasscodeLock();
	void closeAdditionalWindows();

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

	rpl::event_stream<ProxyChange> _proxyChanges;

	// Some fields are just moved from the declaration.
	struct Private;
	const std::unique_ptr<Private> _private;
	const std::unique_ptr<Platform::Integration> _platformIntegration;
	const std::unique_ptr<base::BatterySaving> _batterySaving;
	const std::unique_ptr<Webrtc::Environment> _mediaDevices;

	const std::unique_ptr<Storage::Databases> _databases;
	const std::unique_ptr<Ui::Animations::Manager> _animationsManager;
	crl::object_on_queue<Stickers::EmojiImageLoader> _emojiImageLoader;
	base::Timer _clearEmojiImageLoaderTimer;
	const std::unique_ptr<Media::Audio::Instance> _audio;
	mutable std::unique_ptr<MTP::Config> _fallbackProductionConfig;

	// Notifications should be destroyed before _audio, after _domain.
	// Mutable because is created in run() after OpenSSL is inited.
	std::unique_ptr<Window::Notifications::System> _notifications;

	using MediaControlsManager = Media::SystemMediaControlsManager;
	std::unique_ptr<MediaControlsManager> _mediaControlsManager;
	const std::unique_ptr<Data::DownloadManager> _downloadManager;
	const std::unique_ptr<Main::Domain> _domain;
	const std::unique_ptr<Export::Manager> _exportManager;
	const std::unique_ptr<Calls::Instance> _calls;
	const std::unique_ptr<Iv::Instance> _iv;
	base::flat_map<
		Window::SeparateId,
		std::unique_ptr<Window::Controller>> _windows;
	base::flat_set<not_null<Window::Controller*>> _closingAsyncWindows;
	std::vector<not_null<Window::Controller*>> _windowStack;
	Window::Controller *_lastActiveWindow = nullptr;
	Window::Controller *_lastActivePrimaryWindow = nullptr;
	Window::Controller *_windowInSettings = nullptr;

	std::unique_ptr<Media::View::OverlayWidget> _mediaView;
	const std::unique_ptr<Lang::Instance> _langpack;
	const std::unique_ptr<Lang::CloudManager> _langCloudManager;
	const std::unique_ptr<ChatHelpers::EmojiKeywords> _emojiKeywords;
	std::unique_ptr<Lang::Translator> _translator;
	QPointer<Ui::BoxContent> _badProxyDisableBox;

	const std::unique_ptr<Tray> _tray;

	std::unique_ptr<Media::Player::FloatController> _floatPlayers;
	rpl::lifetime _floatPlayerDelegateLifetime;
	bool _floatPlayerGifsPaused = false;

	rpl::variable<bool> _passcodeLock;
	bool _screenIsLocked = false;

	crl::time _shouldLockAt = 0;
	base::Timer _autoLockTimer;

	QStringList _filesToOpen;
	base::Timer _fileOpenTimer;

	std::optional<base::Timer> _saveSettingsTimer;

	struct LeaveFilter {
		std::vector<QPointer<QWidget>> registered;
		QPointer<QObject> filter;
	};
	base::flat_map<not_null<QWidget*>, LeaveFilter> _leaveFilters;

	rpl::event_stream<Media::View::OpenRequest> _openInMediaViewRequests;

	rpl::event_stream<> _materializeLocalDraftsRequests;

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
