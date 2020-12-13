/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/core_settings.h"
#include "mtproto/mtproto_auth_key.h"
#include "mtproto/mtproto_proxy_data.h"
#include "base/observer.h"
#include "base/timer.h"

class MainWindow;
class MainWidget;
class FileUploader;
class Translator;

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

namespace App {
void quit();
} // namespace App

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

class Application final : public QObject, private base::Subscriber {
public:
	struct ProxyChange {
		MTP::ProxyData was;
		MTP::ProxyData now;
	};

	Application(not_null<Launcher*> launcher);
	Application(const Application &other) = delete;
	Application &operator=(const Application &other) = delete;
	~Application();

	[[nodiscard]] not_null<Launcher*> launcher() const {
		return _launcher;
	}

	void run();

	[[nodiscard]] Ui::Animations::Manager &animationManager() const {
		return *_animationsManager;
	}
	[[nodiscard]] Window::Notifications::System &notifications() const {
		Expects(_notifications != nullptr);

		return *_notifications;
	}

	// Windows interface.
	bool hasActiveWindow(not_null<Main::Session*> session) const;
	void saveCurrentDraftsToHistories();
	[[nodiscard]] Window::Controller *activeWindow() const;
	bool closeActiveWindow();
	bool minimizeActiveWindow();
	[[nodiscard]] QWidget *getFileDialogParent();
	void notifyFileDialogShown(bool shown);
	[[nodiscard]] QWidget *getModalParent();
	void checkSystemDarkMode();

	// Media view interface.
	void checkMediaViewActivation();
	bool hideMediaView();
	void showPhoto(not_null<const PhotoOpenClickHandler*> link);
	void showPhoto(not_null<PhotoData*> photo, HistoryItem *item);
	void showPhoto(not_null<PhotoData*> photo, not_null<PeerData*> item);
	void showDocument(not_null<DocumentData*> document, HistoryItem *item);
	void showTheme(
		not_null<DocumentData*> document,
		const Data::CloudTheme &cloud);
	[[nodiscard]] PeerData *ui_getPeerForMouseAction();

	[[nodiscard]] QPoint getPointForCallPanelCenter() const;
	[[nodiscard]] QImage logo() const {
		return _logo;
	}
	[[nodiscard]] QImage logoNoMargin() const {
		return _logoNoMargin;
	}

	void startSettingsAndBackground();
	[[nodiscard]] Settings &settings() {
		return _settings;
	}
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
	Main::Session *maybeActiveSession() const;
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
	void forceLogOut(
		not_null<Main::Account*> account,
		const TextWithEntities &explanation);
	void checkLocalTime();
	void lockByPasscode();
	void unlockPasscode();
	[[nodiscard]] bool passcodeLocked() const;
	rpl::producer<bool> passcodeLockChanges() const;
	rpl::producer<bool> passcodeLockValue() const;

	void checkAutoLock();
	void checkAutoLockIn(crl::time time);
	void localPasscodeChanged();

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

	void stateChanged(Qt::ApplicationState state);

	friend void App::quit();
	static void QuitAttempt();
	void quitDelayed();
	[[nodiscard]] bool readyToQuit();

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

	not_null<Launcher*> _launcher;
	rpl::event_stream<ProxyChange> _proxyChanges;

	// Some fields are just moved from the declaration.
	struct Private;
	const std::unique_ptr<Private> _private;
	Settings _settings;

	const std::unique_ptr<Storage::Databases> _databases;
	const std::unique_ptr<Ui::Animations::Manager> _animationsManager;
	crl::object_on_queue<Stickers::EmojiImageLoader> _emojiImageLoader;
	base::Timer _clearEmojiImageLoaderTimer;
	const std::unique_ptr<Media::Audio::Instance> _audio;
	mutable std::unique_ptr<MTP::Config> _fallbackProductionConfig;

	// Notifications should be destroyed before _audio, after _domain.
	// Mutable because is created in run() after OpenSSL is inited.
	std::unique_ptr<Window::Notifications::System> _notifications;

	const std::unique_ptr<Main::Domain> _domain;
	const std::unique_ptr<Export::Manager> _exportManager;
	const std::unique_ptr<Calls::Instance> _calls;
	std::unique_ptr<Window::Controller> _window;
	std::unique_ptr<Media::View::OverlayWidget> _mediaView;
	const std::unique_ptr<Lang::Instance> _langpack;
	const std::unique_ptr<Lang::CloudManager> _langCloudManager;
	const std::unique_ptr<ChatHelpers::EmojiKeywords> _emojiKeywords;
	std::unique_ptr<Lang::Translator> _translator;
	base::Observable<void> _passcodedChanged;
	QPointer<Ui::BoxContent> _badProxyDisableBox;

	std::unique_ptr<Media::Player::FloatController> _floatPlayers;
	Media::Player::FloatDelegate *_defaultFloatPlayerDelegate = nullptr;
	Media::Player::FloatDelegate *_replacementFloatPlayerDelegate = nullptr;

	const QImage _logo;
	const QImage _logoNoMargin;

	rpl::variable<bool> _passcodeLock;

	crl::time _shouldLockAt = 0;
	base::Timer _autoLockTimer;

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
