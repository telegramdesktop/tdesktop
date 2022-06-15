/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/application.h"

#include "data/data_abstract_structure.h"
#include "data/data_photo.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_download_manager.h"
#include "base/timer.h"
#include "base/event_filter.h"
#include "base/concurrent_timer.h"
#include "base/qt_signal_producer.h"
#include "base/unixtime.h"
#include "core/core_settings.h"
#include "core/update_checker.h"
#include "core/shortcuts.h"
#include "core/sandbox.h"
#include "core/local_url_handlers.h"
#include "core/launcher.h"
#include "core/ui_integration.h"
#include "chat_helpers/emoji_keywords.h"
#include "chat_helpers/stickers_emoji_image_loader.h"
#include "base/qt/qt_common_adapters.h"
#include "base/platform/base_platform_url_scheme.h"
#include "base/platform/base_platform_last_input.h"
#include "base/platform/base_platform_info.h"
#include "platform/platform_specific.h"
#include "platform/platform_integration.h"
#include "mainwindow.h"
#include "dialogs/dialogs_entry.h"
#include "history/history.h"
#include "apiwrap.h"
#include "api/api_updates.h"
#include "calls/calls_instance.h"
#include "countries/countries_manager.h"
#include "lang/lang_file_parser.h"
#include "lang/lang_translator.h"
#include "lang/lang_cloud_manager.h"
#include "lang/lang_hardcoded.h"
#include "lang/lang_instance.h"
#include "inline_bots/bot_attach_web_view.h"
#include "mainwidget.h"
#include "tray.h"
#include "core/file_utilities.h"
#include "core/click_handler_types.h" // ClickHandlerContext.
#include "core/crash_reports.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "media/view/media_view_overlay_widget.h"
#include "media/view/media_view_open_common.h"
#include "mtproto/mtproto_dc_options.h"
#include "mtproto/mtproto_config.h"
#include "mtproto/mtp_instance.h"
#include "media/audio/media_audio.h"
#include "media/audio/media_audio_track.h"
#include "media/player/media_player_instance.h"
#include "media/player/media_player_float.h"
#include "media/clip/media_clip_reader.h" // For Media::Clip::Finish().
#include "window/notifications_manager.h"
#include "window/themes/window_theme.h"
#include "window/window_lock_widgets.h"
#include "history/history_location_manager.h"
#include "ui/widgets/tooltip.h"
#include "ui/gl/gl_detection.h"
#include "ui/image/image.h"
#include "ui/text/text_options.h"
#include "ui/emoji_config.h"
#include "ui/effects/animations.h"
#include "ui/cached_round_corners.h"
#include "storage/serialize_common.h"
#include "storage/storage_domain.h"
#include "storage/storage_databases.h"
#include "storage/localstorage.h"
#include "payments/payments_checkout_process.h"
#include "export/export_manager.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "base/qthelp_regex.h"
#include "base/qthelp_url.h"
#include "boxes/connection_box.h"
#include "boxes/premium_limits_box.h"
#include "ui/boxes/confirm_box.h"

#include <QtCore/QMimeDatabase>
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>

namespace Core {
namespace {

constexpr auto kQuitPreventTimeoutMs = crl::time(1500);
constexpr auto kAutoLockTimeoutLateMs = crl::time(3000);
constexpr auto kClearEmojiImageSourceTimeout = 10 * crl::time(1000);

LaunchState GlobalLaunchState/* = LaunchState::Running*/;

void SetCrashAnnotationsGL() {
#ifdef Q_OS_WIN
	CrashReports::SetAnnotation("OpenGL ANGLE", [] {
		if (Core::App().settings().disableOpenGL()) {
			return "Disabled";
		} else switch (Ui::GL::CurrentANGLE()) {
		case Ui::GL::ANGLE::Auto: return "Auto";
		case Ui::GL::ANGLE::D3D11: return "Direct3D 11";
		case Ui::GL::ANGLE::D3D9: return "Direct3D 9";
		case Ui::GL::ANGLE::D3D11on12: return "D3D11on12";
		case Ui::GL::ANGLE::OpenGL: return "OpenGL";
		}
		Unexpected("Ui::GL::CurrentANGLE value in SetupANGLE.");
	}());
#else // Q_OS_WIN
	CrashReports::SetAnnotation(
		"OpenGL",
		Core::App().settings().disableOpenGL() ? "Disabled" : "Enabled");
#endif // Q_OS_WIN
}

} // namespace

Application *Application::Instance = nullptr;

struct Application::Private {
	base::Timer quitTimer;
	UiIntegration uiIntegration;
	Settings settings;
};

Application::Application(not_null<Launcher*> launcher)
: QObject()
, _launcher(launcher)
, _private(std::make_unique<Private>())
, _platformIntegration(Platform::Integration::Create())
, _databases(std::make_unique<Storage::Databases>())
, _animationsManager(std::make_unique<Ui::Animations::Manager>())
, _clearEmojiImageLoaderTimer([=] { clearEmojiSourceImages(); })
, _audio(std::make_unique<Media::Audio::Instance>())
, _fallbackProductionConfig(
	std::make_unique<MTP::Config>(MTP::Environment::Production))
, _downloadManager(std::make_unique<Data::DownloadManager>())
, _domain(std::make_unique<Main::Domain>(cDataFile()))
, _exportManager(std::make_unique<Export::Manager>())
, _calls(std::make_unique<Calls::Instance>())
, _langpack(std::make_unique<Lang::Instance>())
, _langCloudManager(std::make_unique<Lang::CloudManager>(langpack()))
, _emojiKeywords(std::make_unique<ChatHelpers::EmojiKeywords>())
, _tray(std::make_unique<Tray>())
, _autoLockTimer([=] { checkAutoLock(); }) {
	Ui::Integration::Set(&_private->uiIntegration);

	_platformIntegration->init();

	passcodeLockChanges(
	) | rpl::start_with_next([=] {
		_shouldLockAt = 0;
	}, _lifetime);

	passcodeLockChanges(
	) | rpl::start_with_next([=] {
		_notifications->updateAll();
	}, _lifetime);

	_domain->activeSessionChanges(
	) | rpl::start_with_next([=](Main::Session *session) {
		if (session && !UpdaterDisabled()) { // #TODO multi someSessionValue
			UpdateChecker().setMtproto(session);
		}
	}, _lifetime);
}

Application::~Application() {
	if (_saveSettingsTimer && _saveSettingsTimer->isActive()) {
		Local::writeSettings();
	}

	// Depend on primaryWindow() for now :(
	Shortcuts::Finish();

	_secondaryWindows.clear();
	_primaryWindow = nullptr;
	_mediaView = nullptr;
	_notifications->clearAllFast();

	// We must manually destroy all windows before going further.
	// DestroyWindow on Windows (at least with an active WebView) enters
	// event loop and invoke scheduled crl::on_main callbacks.
	//
	// For example Domain::removeRedundantAccounts() is called from
	// Domain::finish() and there is a violation on Ensures(started()).
	Payments::CheckoutProcess::ClearAll();
	InlineBots::AttachWebView::ClearAll();

	_domain->finish();

	Local::finish();

	Shortcuts::Finish();

	Ui::Emoji::Clear();
	Media::Clip::Finish();

	Ui::FinishCachedCorners();
	Data::clearGlobalStructures();

	Window::Theme::Uninitialize();

	Media::Player::finish(_audio.get());
	style::stopManager();

	ThirdParty::finish();

	Instance = nullptr;
}

void Application::run() {
	style::internal::StartFonts();

	ThirdParty::start();

	// Depends on OpenSSL on macOS, so on ThirdParty::start().
	// Depends on notifications settings.
	_notifications = std::make_unique<Window::Notifications::System>();

	startLocalStorage();
	ValidateScale();

	refreshGlobalProxy(); // Depends on app settings being read.

	if (Local::oldSettingsVersion() < AppVersion) {
		RegisterUrlScheme();
		psNewVersion();
	}

	if (cAutoStart() && !Platform::AutostartSupported()) {
		cSetAutoStart(false);
	}

	if (cLaunchMode() == LaunchModeAutoStart && Platform::AutostartSkip()) {
		Platform::AutostartToggle(false);
		Quit();
		return;
	}

	_translator = std::make_unique<Lang::Translator>();
	QCoreApplication::instance()->installTranslator(_translator.get());

	style::startManager(cScale());
	Ui::InitTextOptions();
	Ui::StartCachedCorners();
	Ui::Emoji::Init();
	startEmojiImageLoader();
	startSystemDarkModeViewer();
	Media::Player::start(_audio.get());

	style::ShortAnimationPlaying(
	) | rpl::start_with_next([=](bool playing) {
		if (playing) {
			MTP::details::pause();
		} else {
			MTP::details::unpause();
		}
	}, _lifetime);

	DEBUG_LOG(("Application Info: inited..."));

	cChangeDateFormat(QLocale::system().dateFormat(QLocale::ShortFormat));
	cChangeTimeFormat(QLocale::system().timeFormat(QLocale::ShortFormat));

	DEBUG_LOG(("Application Info: starting app..."));

	// Create mime database, so it won't be slow later.
	QMimeDatabase().mimeTypeForName(qsl("text/plain"));

	_primaryWindow = std::make_unique<Window::Controller>();
	_lastActiveWindow = _primaryWindow.get();

	_domain->activeChanges(
	) | rpl::start_with_next([=](not_null<Main::Account*> account) {
		_primaryWindow->showAccount(account);
	}, _primaryWindow->widget()->lifetime());

	(
		_domain->activeValue(
		) | rpl::to_empty | rpl::filter([=] {
			return _domain->started();
		}) | rpl::take(1)
	) | rpl::then(
		_domain->accountsChanges()
	) | rpl::map([=] {
		return (_domain->accounts().size() > Main::Domain::kMaxAccounts)
			? _domain->activeChanges()
			: rpl::never<not_null<Main::Account*>>();
	}) | rpl::flatten_latest(
	) | rpl::start_with_next([=](not_null<Main::Account*> account) {
		const auto ordered = _domain->orderedAccounts();
		const auto it = ranges::find(ordered, account);
		if (it != end(ordered)) {
			const auto index = std::distance(begin(ordered), it);
			if ((index + 1) > _domain->maxAccounts()) {
				_primaryWindow->show(Box(
					AccountsLimitBox,
					&account->session()));
			}
		}
	}, _primaryWindow->widget()->lifetime());

	QCoreApplication::instance()->installEventFilter(this);

	appDeactivatedValue(
	) | rpl::start_with_next([=](bool deactivated) {
		if (deactivated) {
			handleAppDeactivated();
		} else {
			handleAppActivated();
		}
	}, _lifetime);

	DEBUG_LOG(("Application Info: window created..."));

	// Depend on primaryWindow() for now :(
	startShortcuts();
	startDomain();

	startTray();

	_primaryWindow->widget()->show();

	const auto currentGeometry = _primaryWindow->widget()->geometry();
	_mediaView = std::make_unique<Media::View::OverlayWidget>();
	_primaryWindow->widget()->Ui::RpWidget::setGeometry(currentGeometry);

	DEBUG_LOG(("Application Info: showing."));
	_primaryWindow->finishFirstShow();

	if (!_primaryWindow->locked() && cStartToSettings()) {
		_primaryWindow->showSettings();
	}

	_primaryWindow->updateIsActiveFocus();

	for (const auto &error : Shortcuts::Errors()) {
		LOG(("Shortcuts Error: %1").arg(error));
	}

	SetCrashAnnotationsGL();
	if (!Platform::IsMac() && Ui::GL::LastCrashCheckFailed()) {
		showOpenGLCrashNotification();
	}

	_openInMediaViewRequests.events(
	) | rpl::start_with_next([=](Media::View::OpenRequest &&request) {
		if (_mediaView) {
			_mediaView->show(std::move(request));
		}
	}, _lifetime);
	_primaryWindow->openInMediaViewRequests(
	) | rpl::start_to_stream(
		_openInMediaViewRequests,
		_primaryWindow->lifetime());

	{
		const auto countries = std::make_shared<Countries::Manager>(
			_domain.get());
		countries->lifetime().add([=] {
			[[maybe_unused]] const auto countriesCopy = countries;
		});
	}
}

void Application::showOpenGLCrashNotification() {
	const auto enable = [=] {
		Ui::GL::ForceDisable(false);
		Ui::GL::CrashCheckFinish();
		Core::App().settings().setDisableOpenGL(false);
		Local::writeSettings();
		Restart();
	};
	const auto keepDisabled = [=] {
		Ui::GL::ForceDisable(true);
		Ui::GL::CrashCheckFinish();
		Core::App().settings().setDisableOpenGL(true);
		Local::writeSettings();
	};
	_primaryWindow->show(Ui::MakeConfirmBox({
		.text = ""
		"There may be a problem with your graphics drivers and OpenGL. "
		"Try updating your drivers.\n\n"
		"OpenGL has been disabled. You can try to enable it again "
		"or keep it disabled if crashes continue.",
		.confirmed = enable,
		.cancelled = keepDisabled,
		.confirmText = "Enable",
		.cancelText = "Keep Disabled",
	}));
}

void Application::startDomain() {
	const auto state = _domain->start(QByteArray());
	if (state != Storage::StartResult::IncorrectPasscodeLegacy) {
		// In case of non-legacy passcoded app all global settings are ready.
		startSettingsAndBackground();
	}
	if (state != Storage::StartResult::Success) {
		lockByPasscode();
		DEBUG_LOG(("Application Info: passcode needed..."));
	}
}

void Application::startSettingsAndBackground() {
	Local::rewriteSettingsIfNeeded();
	Window::Theme::Background()->start();
	checkSystemDarkMode();
}

void Application::checkSystemDarkMode() {
	const auto maybeDarkMode = settings().systemDarkMode();
	const auto darkModeEnabled = settings().systemDarkModeEnabled();
	const auto needToSwitch = darkModeEnabled
		&& maybeDarkMode
		&& (*maybeDarkMode != Window::Theme::IsNightMode());
	if (needToSwitch) {
		Window::Theme::ToggleNightMode();
		Window::Theme::KeepApplied();
	}
}

void Application::startSystemDarkModeViewer() {
	if (Window::Theme::Background()->editingTheme()) {
		settings().setSystemDarkModeEnabled(false);
	}
	rpl::merge(
		settings().systemDarkModeChanges() | rpl::to_empty,
		settings().systemDarkModeEnabledChanges() | rpl::to_empty
	) | rpl::start_with_next([=] {
		checkSystemDarkMode();
	}, _lifetime);
}

void Application::enumerateWindows(Fn<void(
		not_null<Window::Controller*>)> callback) const {
	if (_primaryWindow) {
		callback(_primaryWindow.get());
	}
	for (const auto &window : ranges::views::values(_secondaryWindows)) {
		callback(window.get());
	}
}

void Application::processSecondaryWindow(
		not_null<Window::Controller*> window) {
	window->openInMediaViewRequests(
	) | rpl::start_to_stream(_openInMediaViewRequests, window->lifetime());
}

void Application::startTray() {
	using WindowRaw = not_null<Window::Controller*>;
	_tray->create();
	_tray->aboutToShowRequests(
	) | rpl::start_with_next([=] {
		enumerateWindows([&](WindowRaw w) { w->updateIsActive(); });
		_tray->updateMenuText();
	}, _primaryWindow->widget()->lifetime());

	_tray->showFromTrayRequests(
	) | rpl::start_with_next([=] {
		const auto last = _lastActiveWindow;
		enumerateWindows([&](WindowRaw w) { w->widget()->showFromTray(); });
		if (last) {
			last->widget()->showFromTray();
		}
	}, _primaryWindow->widget()->lifetime());

	_tray->hideToTrayRequests(
	) | rpl::start_with_next([=] {
		enumerateWindows([&](WindowRaw w) { w->widget()->minimizeToTray(); });
	}, _primaryWindow->widget()->lifetime());
}

auto Application::prepareEmojiSourceImages()
-> std::shared_ptr<Ui::Emoji::UniversalImages> {
	const auto &images = Ui::Emoji::SourceImages();
	if (settings().largeEmoji()) {
		return images;
	}
	Ui::Emoji::ClearSourceImages(images);
	return std::make_shared<Ui::Emoji::UniversalImages>(images->id());
}

void Application::clearEmojiSourceImages() {
	_emojiImageLoader.with([](Stickers::EmojiImageLoader &loader) {
		crl::on_main([images = loader.releaseImages()]{
			Ui::Emoji::ClearSourceImages(images);
		});
	});
}

bool Application::isActiveForTrayMenu() const {
	if (_primaryWindow && _primaryWindow->widget()->isActiveForTrayMenu()) {
		return true;
	}
	return ranges::any_of(ranges::views::values(_secondaryWindows), [=](
			const std::unique_ptr<Window::Controller> &controller) {
		return controller->widget()->isActiveForTrayMenu();
	});
}

bool Application::hideMediaView() {
	if (_mediaView && !_mediaView->isHidden()) {
		_mediaView->hide();
		if (const auto window = activeWindow()) {
			window->reActivate();
		}
		return true;
	}
	return false;
}

bool Application::eventFilter(QObject *object, QEvent *e) {
	switch (e->type()) {
	case QEvent::KeyPress:
	case QEvent::MouseButtonPress:
	case QEvent::TouchBegin:
	case QEvent::Wheel: {
		updateNonIdle();
	} break;

	case QEvent::ShortcutOverride: {
		// handle shortcuts ourselves
		return true;
	} break;

	case QEvent::Shortcut: {
		const auto event = static_cast<QShortcutEvent*>(e);
		DEBUG_LOG(("Shortcut event caught: %1"
			).arg(event->key().toString()));
		if (Shortcuts::HandleEvent(event)) {
			return true;
		}
	} break;

	case QEvent::ApplicationActivate: {
		if (object == QCoreApplication::instance()) {
			updateNonIdle();
		}
	} break;

	case QEvent::FileOpen: {
		if (object == QCoreApplication::instance()) {
			const auto event = static_cast<QFileOpenEvent*>(e);
			const auto url = QString::fromUtf8(
				event->url().toEncoded().trimmed());
			if (url.startsWith(qstr("tg://"), Qt::CaseInsensitive)) {
				cSetStartUrl(url.mid(0, 8192));
				checkStartUrl();
			}
			if (StartUrlRequiresActivate(url)) {
				_primaryWindow->activate();
			}
		}
	} break;
	}

	return QObject::eventFilter(object, e);
}

Settings &Application::settings() {
	return _private->settings;
}

void Application::saveSettingsDelayed(crl::time delay) {
	if (_saveSettingsTimer) {
		_saveSettingsTimer->callOnce(delay);
	}
}

void Application::saveSettings() {
	Local::writeSettings();
}

MTP::Config &Application::fallbackProductionConfig() const {
	if (!_fallbackProductionConfig) {
		_fallbackProductionConfig = std::make_unique<MTP::Config>(
			MTP::Environment::Production);
	}
	return *_fallbackProductionConfig;
}

void Application::refreshFallbackProductionConfig(
		const MTP::Config &config) {
	if (config.environment() == MTP::Environment::Production) {
		_fallbackProductionConfig = std::make_unique<MTP::Config>(config);
	}
}

void Application::constructFallbackProductionConfig(
		const QByteArray &serialized) {
	if (auto config = MTP::Config::FromSerialized(serialized)) {
		if (config->environment() == MTP::Environment::Production) {
			_fallbackProductionConfig = std::move(config);
		}
	}
}

void Application::setCurrentProxy(
		const MTP::ProxyData &proxy,
		MTP::ProxyData::Settings settings) {
	auto &my = _private->settings.proxy();
	const auto current = [&] {
		return my.isEnabled() ? my.selected() : MTP::ProxyData();
	};
	const auto was = current();
	my.setSelected(proxy);
	my.setSettings(settings);
	const auto now = current();
	refreshGlobalProxy();
	_proxyChanges.fire({ was, now });
	my.connectionTypeChangesNotify();
}

auto Application::proxyChanges() const -> rpl::producer<ProxyChange> {
	return _proxyChanges.events();
}

void Application::badMtprotoConfigurationError() {
	if (settings().proxy().isEnabled() && !_badProxyDisableBox) {
		const auto disableCallback = [=] {
			setCurrentProxy(
				settings().proxy().selected(),
				MTP::ProxyData::Settings::System);
		};
		_badProxyDisableBox = Ui::show(
			Ui::MakeInformBox(Lang::Hard::ProxyConfigError()));
		_badProxyDisableBox->boxClosing(
		) | rpl::start_with_next(
			disableCallback,
			_badProxyDisableBox->lifetime());
	}
}

void Application::startLocalStorage() {
	Local::start();
	_saveSettingsTimer.emplace([=] { saveSettings(); });
	settings().saveDelayedRequests() | rpl::start_with_next([=] {
		saveSettingsDelayed();
	}, _lifetime);
}

void Application::startEmojiImageLoader() {
	_emojiImageLoader.with([
		source = prepareEmojiSourceImages(),
		large = settings().largeEmoji()
	](Stickers::EmojiImageLoader &loader) mutable {
		loader.init(std::move(source), large);
	});

	settings().largeEmojiChanges(
	) | rpl::start_with_next([=](bool large) {
		if (large) {
			_clearEmojiImageLoaderTimer.cancel();
		} else {
			_clearEmojiImageLoaderTimer.callOnce(
				kClearEmojiImageSourceTimeout);
		}
	}, _lifetime);

	Ui::Emoji::Updated(
	) | rpl::start_with_next([=] {
		_emojiImageLoader.with([
			source = prepareEmojiSourceImages()
		](Stickers::EmojiImageLoader &loader) mutable {
			loader.switchTo(std::move(source));
		});
	}, _lifetime);
}

void Application::setScreenIsLocked(bool locked) {
	_screenIsLocked = locked;
}

bool Application::screenIsLocked() const {
	return _screenIsLocked;
}

void Application::setDefaultFloatPlayerDelegate(
		not_null<Media::Player::FloatDelegate*> delegate) {
	Expects(!_defaultFloatPlayerDelegate == !_floatPlayers);

	_defaultFloatPlayerDelegate = delegate;
	_replacementFloatPlayerDelegate = nullptr;
	if (_floatPlayers) {
		_floatPlayers->replaceDelegate(delegate);
	} else {
		_floatPlayers = std::make_unique<Media::Player::FloatController>(
			delegate);
	}
}

void Application::replaceFloatPlayerDelegate(
		not_null<Media::Player::FloatDelegate*> replacement) {
	Expects(_floatPlayers != nullptr);

	_replacementFloatPlayerDelegate = replacement;
	_floatPlayers->replaceDelegate(replacement);
}

void Application::restoreFloatPlayerDelegate(
		not_null<Media::Player::FloatDelegate*> replacement) {
	Expects(_floatPlayers != nullptr);

	if (_replacementFloatPlayerDelegate == replacement) {
		_replacementFloatPlayerDelegate = nullptr;
		_floatPlayers->replaceDelegate(_defaultFloatPlayerDelegate);
	}
}

rpl::producer<FullMsgId> Application::floatPlayerClosed() const {
	Expects(_floatPlayers != nullptr);

	return _floatPlayers->closeEvents();
}

void Application::logout(Main::Account *account) {
	if (account) {
		account->logOut();
	} else {
		_domain->resetWithForgottenPasscode();
	}
}

void Application::logoutWithChecks(Main::Account *account) {
	const auto weak = base::make_weak(account);
	const auto retry = [=] {
		if (const auto account = weak.get()) {
			logoutWithChecks(account);
		}
	};
	if (!account || !account->sessionExists()) {
		logout(account);
	} else if (_exportManager->inProgress(&account->session())) {
		_exportManager->stopWithConfirmation(retry);
	} else if (account->session().uploadsInProgress()) {
		account->session().uploadsStopWithConfirmation(retry);
	} else if (_downloadManager->loadingInProgress(&account->session())) {
		_downloadManager->loadingStopWithConfirmation(
			retry,
			&account->session());
	} else {
		logout(account);
	}
}

void Application::forceLogOut(
		not_null<Main::Account*> account,
		const TextWithEntities &explanation) {
	const auto box = Ui::show(Ui::MakeConfirmBox({
		.text = explanation,
		.confirmText = tr::lng_passcode_logout(tr::now),
		.inform = true,
	}));
	box->setCloseByEscape(false);
	box->setCloseByOutsideClick(false);
	const auto weak = base::make_weak(account.get());
	connect(box, &QObject::destroyed, [=] {
		crl::on_main(weak, [=] {
			account->forcedLogOut();
		});
	});
}

void Application::checkLocalTime() {
	const auto adjusted = crl::adjust_time();
	if (adjusted) {
		base::Timer::Adjust();
		base::ConcurrentTimerEnvironment::Adjust();
		base::unixtime::http_invalidate();
	}
	if (const auto session = maybePrimarySession()) {
		session->updates().checkLastUpdate(adjusted);
	}
}

void Application::handleAppActivated() {
	checkLocalTime();
	if (_primaryWindow) {
		_primaryWindow->updateIsActiveFocus();
	}
}

void Application::handleAppDeactivated() {
	if (_primaryWindow) {
		_primaryWindow->updateIsActiveBlur();
	}
	const auto session = _lastActiveWindow
		? _lastActiveWindow->maybeSession()
		: nullptr;
	if (session) {
		session->updates().updateOnline();
	}
	Ui::Tooltip::Hide();
}

rpl::producer<bool> Application::appDeactivatedValue() const {
	const auto &app =
		static_cast<QGuiApplication*>(QCoreApplication::instance());
	return rpl::single(
		app->applicationState()
	) | rpl::then(
		base::qt_signal_producer(
			app,
			&QGuiApplication::applicationStateChanged
	)) | rpl::map([=](Qt::ApplicationState state) {
		return (state != Qt::ApplicationActive);
	});
}

void Application::call_handleObservables() {
	base::HandleObservables();
}

void Application::switchDebugMode() {
	if (Logs::DebugEnabled()) {
		Logs::SetDebugEnabled(false);
		_launcher->writeDebugModeSetting();
		Restart();
	} else {
		Logs::SetDebugEnabled(true);
		_launcher->writeDebugModeSetting();
		DEBUG_LOG(("Debug logs started."));
		Ui::hideLayer();
	}
}

void Application::switchFreeType() {
	if (cUseFreeType()) {
		QFile(cWorkingDir() + qsl("tdata/withfreetype")).remove();
		cSetUseFreeType(false);
	} else {
		QFile f(cWorkingDir() + qsl("tdata/withfreetype"));
		if (f.open(QIODevice::WriteOnly)) {
			f.write("1");
			f.close();
		}
		cSetUseFreeType(true);
	}
	Restart();
}

void Application::writeInstallBetaVersionsSetting() {
	_launcher->writeInstallBetaVersionsSetting();
}

Main::Account &Application::activeAccount() const {
	return _domain->active();
}

Main::Session *Application::maybePrimarySession() const {
	return _domain->started() ? activeAccount().maybeSession() : nullptr;
}

bool Application::exportPreventsQuit() {
	if (_exportManager->inProgress()) {
		_exportManager->stopWithConfirmation([] {
			Quit();
		});
		return true;
	}
	return false;
}

bool Application::uploadPreventsQuit() {
	if (!_domain->started()) {
		return false;
	}
	for (const auto &[index, account] : _domain->accounts()) {
		if (!account->sessionExists()) {
			continue;
		}
		if (account->session().uploadsInProgress()) {
			account->session().uploadsStopWithConfirmation([=] {
				for (const auto &[index, account] : _domain->accounts()) {
					if (account->sessionExists()) {
						account->session().uploadsStop();
					}
				}
				Quit();
			});
			return true;
		}
	}
	return false;
}

bool Application::downloadPreventsQuit() {
	if (_downloadManager->loadingInProgress()) {
		_downloadManager->loadingStopWithConfirmation([=] { Quit(); });
		return true;
	}
	return false;
}

bool Application::preventsQuit(QuitReason reason) {
	if (exportPreventsQuit()
		|| uploadPreventsQuit()
		|| downloadPreventsQuit()) {
		return true;
	} else if (const auto window = activeWindow()) {
		if (window->widget()->isActive()) {
			return window->widget()->preventsQuit(reason);
		}
	}
	return false;
}

int Application::unreadBadge() const {
	return _domain->unreadBadge();
}

bool Application::unreadBadgeMuted() const {
	return _domain->unreadBadgeMuted();
}

rpl::producer<> Application::unreadBadgeChanges() const {
	return _domain->unreadBadgeChanges();
}

bool Application::offerLegacyLangPackSwitch() const {
	return (_domain->accounts().size() == 1)
		&& activeAccount().sessionExists();
}

bool Application::canApplyLangPackWithoutRestart() const {
	for (const auto &[index, account] : _domain->accounts()) {
		if (account->sessionExists()) {
			return false;
		}
	}
	return true;
}

void Application::checkStartUrl() {
	if (!cStartUrl().isEmpty() && _primaryWindow && !_primaryWindow->locked()) {
		const auto url = cStartUrl();
		cSetStartUrl(QString());
		if (!openLocalUrl(url, {})) {
			cSetStartUrl(url);
		}
	}
}

bool Application::openLocalUrl(const QString &url, QVariant context) {
	return openCustomUrl("tg://", LocalUrlHandlers(), url, context);
}

bool Application::openInternalUrl(const QString &url, QVariant context) {
	return openCustomUrl("internal:", InternalUrlHandlers(), url, context);
}

QString Application::changelogLink() const {
	const auto base = u"https://desktop.telegram.org/changelog"_q;
	const auto languages = {
		"id",
		"de",
		"fr",
		"nl",
		"pl",
		"tr",
		"uk",
		"fa",
		"ru",
		"ms",
		"es",
		"it",
		"uz",
		"pt-br",
		"be",
		"ar",
		"ko",
	};
	const auto current = _langpack->id().replace("-raw", "");
	if (current.isEmpty()) {
		return base;
	}
	for (const auto language : languages) {
		if (current == language || current.split(u'-')[0] == language) {
			return base + "?setln=" + language;
		}
	}
	return base;
}

bool Application::openCustomUrl(
		const QString &protocol,
		const std::vector<LocalUrlHandler> &handlers,
		const QString &url,
		const QVariant &context) {
	const auto urlTrimmed = url.trimmed();
	if (!urlTrimmed.startsWith(protocol, Qt::CaseInsensitive)
		|| passcodeLocked()) {
		return false;
	}
	const auto command = base::StringViewMid(urlTrimmed, protocol.size(), 8192);
	const auto my = context.value<ClickHandlerContext>();
	const auto controller = my.sessionWindow.get()
		? my.sessionWindow.get()
		: _primaryWindow
		? _primaryWindow->sessionController()
		: nullptr;

	using namespace qthelp;
	const auto options = RegExOption::CaseInsensitive;
	for (const auto &[expression, handler] : handlers) {
		const auto match = regex_match(expression, command, options);
		if (match) {
			return handler(controller, match, context);
		}
	}
	return false;

}

void Application::preventOrInvoke(Fn<void()> &&callback) {
	_primaryWindow->preventOrInvoke(std::move(callback));
}

void Application::lockByPasscode() {
	preventOrInvoke([=] {
		enumerateWindows([&](not_null<Window::Controller*> w) {
			_passcodeLock = true;
			w->setupPasscodeLock();
		});
	});
}

void Application::unlockPasscode() {
	clearPasscodeLock();
	enumerateWindows([&](not_null<Window::Controller*> w) {
		w->clearPasscodeLock();
	});
}

void Application::clearPasscodeLock() {
	cSetPasscodeBadTries(0);
	_passcodeLock = false;
}

bool Application::passcodeLocked() const {
	return _passcodeLock.current();
}

void Application::updateNonIdle() {
	_lastNonIdleTime = crl::now();
	if (const auto session = maybePrimarySession()) {
		session->updates().checkIdleFinish(_lastNonIdleTime);
	}
}

crl::time Application::lastNonIdleTime() const {
	return std::max(
		base::Platform::LastUserInputTime().value_or(0),
		_lastNonIdleTime);
}

rpl::producer<bool> Application::passcodeLockChanges() const {
	return _passcodeLock.changes();
}

rpl::producer<bool> Application::passcodeLockValue() const {
	return _passcodeLock.value();
}

bool Application::someSessionExists() const {
	for (const auto &[index, account] : _domain->accounts()) {
		if (account->sessionExists()) {
			return true;
		}
	}
	return false;
}

void Application::checkAutoLock(crl::time lastNonIdleTime) {
	if (!_domain->local().hasLocalPasscode()
		|| passcodeLocked()
		|| !someSessionExists()) {
		_shouldLockAt = 0;
		_autoLockTimer.cancel();
		return;
	} else if (!lastNonIdleTime) {
		lastNonIdleTime = this->lastNonIdleTime();
	}

	checkLocalTime();
	const auto now = crl::now();
	const auto shouldLockInMs = settings().autoLock() * 1000LL;
	const auto checkTimeMs = now - lastNonIdleTime;
	if (checkTimeMs >= shouldLockInMs
		|| (_shouldLockAt > 0
			&& now > _shouldLockAt + kAutoLockTimeoutLateMs)) {
		_shouldLockAt = 0;
		_autoLockTimer.cancel();
		lockByPasscode();
	} else {
		_shouldLockAt = now + (shouldLockInMs - checkTimeMs);
		_autoLockTimer.callOnce(shouldLockInMs - checkTimeMs);
	}
}

void Application::checkAutoLockIn(crl::time time) {
	if (_autoLockTimer.isActive()) {
		auto remain = _autoLockTimer.remainingTime();
		if (remain > 0 && remain <= time) return;
	}
	_autoLockTimer.callOnce(time);
}

void Application::localPasscodeChanged() {
	_shouldLockAt = 0;
	_autoLockTimer.cancel();
	checkAutoLock(crl::now());
}

bool Application::hasActiveWindow(not_null<Main::Session*> session) const {
	if (Quitting() || !_primaryWindow) {
		return false;
	} else if (_calls->hasActivePanel(session)) {
		return true;
	} else if (const auto window = _lastActiveWindow) {
		return (window->account().maybeSession() == session)
			&& window->widget()->isActive();
	}
	return false;
}

void Application::saveCurrentDraftsToHistories() {
	if (!_primaryWindow) {
		return;
	} else if (const auto controller = _primaryWindow->sessionController()) {
		controller->content()->saveFieldToHistoryLocalDraft();
	}
}

Window::Controller *Application::primaryWindow() const {
	return _primaryWindow.get();
}

Window::Controller *Application::separateWindowForPeer(
		not_null<PeerData*> peer) const {
	for (const auto &[history, window] : _secondaryWindows) {
		if (history->peer == peer) {
			return window.get();
		}
	}
	return nullptr;
}

Window::Controller *Application::ensureSeparateWindowForPeer(
		not_null<PeerData*> peer,
		MsgId showAtMsgId) {
	const auto activate = [&](not_null<Window::Controller*> window) {
		window->activate();
		return window;
	};

	if (const auto existing = separateWindowForPeer(peer)) {
		existing->sessionController()->showPeerHistory(
			peer,
			Window::SectionShow::Way::ClearStack,
			showAtMsgId);
		return activate(existing);
	}
	const auto result = _secondaryWindows.emplace(
		peer->owner().history(peer),
		std::make_unique<Window::Controller>(peer, showAtMsgId)
	).first->second.get();
	processSecondaryWindow(result);
	result->widget()->show();
	result->finishFirstShow();
	return activate(result);
}

Window::Controller *Application::activeWindow() const {
	return _lastActiveWindow;
}

void Application::closeWindow(not_null<Window::Controller*> window) {
	for (auto i = begin(_secondaryWindows); i != end(_secondaryWindows);) {
		if (i->second.get() == window) {
			if (_lastActiveWindow == window) {
				_lastActiveWindow = _primaryWindow.get();
			}
			i = _secondaryWindows.erase(i);
		} else {
			++i;
		}
	}
}

void Application::closeChatFromWindows(not_null<PeerData*> peer) {
	for (const auto &[history, window] : _secondaryWindows) {
		if (!window) {
			continue;
		}
		if (history->peer == peer) {
			closeWindow(window.get());
		} else if (const auto session = window->sessionController()) {
			if (session->activeChatCurrent().peer() == peer) {
				session->showPeerHistory(
					window->singlePeer()->id,
					Window::SectionShow::Way::ClearStack);
			}
		}
	}
	if (_primaryWindow && _primaryWindow->sessionController()) {
		const auto primary = _primaryWindow->sessionController();
		if ((primary->activeChatCurrent().peer() == peer)
			&& (&primary->session() == &peer->session())) {
			// showChatsList
			primary->showPeerHistory(
				PeerId(0),
				Window::SectionShow::Way::ClearStack);
		}
	}
}

void Application::windowActivated(not_null<Window::Controller*> window) {
	const auto was = _lastActiveWindow;
	const auto now = window;
	_lastActiveWindow = window;

	const auto wasSession = was ? was->maybeSession() : nullptr;
	const auto nowSession = now->maybeSession();
	if (wasSession != nowSession) {
		if (wasSession) {
			wasSession->updates().updateOnline();
		}
		if (nowSession) {
			nowSession->updates().updateOnline();
		}
	}
	if (_mediaView && !_mediaView->isHidden()) {
		_mediaView->activate();
	}
}

bool Application::closeActiveWindow() {
	if (hideMediaView()) {
		return true;
	}
	if (!calls().closeCurrentActiveCall()) {
		if (const auto window = activeWindow()) {
			if (window->widget()->isVisible()
				&& window->widget()->isActive()) {
				window->close();
				return true;
			}
		}
	}
	return false;
}

bool Application::minimizeActiveWindow() {
	hideMediaView();
	if (!calls().minimizeCurrentActiveCall()) {
		if (const auto window = activeWindow()) {
			window->minimize();
			return true;
		}
	}
	return false;
}

QWidget *Application::getFileDialogParent() {
	if (const auto view = _mediaView.get(); view && !view->isHidden()) {
		return view->widget();
	} else if (const auto active = activeWindow()) {
		return active->widget();
	}
	return nullptr;
}

void Application::notifyFileDialogShown(bool shown) {
	if (_mediaView) {
		_mediaView->notifyFileDialogShown(shown);
	}
}

QPoint Application::getPointForCallPanelCenter() const {
	if (const auto window = activeWindow()) {
		return window->getPointForCallPanelCenter();
	}
	return QGuiApplication::primaryScreen()->geometry().center();
}

// macOS Qt bug workaround, sometimes no leaveEvent() gets to the nested widgets.
void Application::registerLeaveSubscription(not_null<QWidget*> widget) {
#ifdef Q_OS_MAC
	if (const auto window = widget->window()) {
		auto i = _leaveFilters.find(window);
		if (i == end(_leaveFilters)) {
			const auto check = [=](not_null<QEvent*> e) {
				if (e->type() == QEvent::Leave) {
					if (const auto taken = _leaveFilters.take(window)) {
						for (const auto &weak : taken->registered) {
							if (const auto widget = weak.data()) {
								QEvent ev(QEvent::Leave);
								QCoreApplication::sendEvent(widget, &ev);
							}
						}
						delete taken->filter.data();
					}
				}
				return base::EventFilterResult::Continue;
			};
			const auto filter = base::install_event_filter(window, check);
			QObject::connect(filter, &QObject::destroyed, [=] {
				_leaveFilters.remove(window);
			});
			i = _leaveFilters.emplace(
				window,
				LeaveFilter{ .filter = filter.get() }).first;
		}
		i->second.registered.push_back(widget.get());
	}
#endif // Q_OS_MAC
}

void Application::unregisterLeaveSubscription(not_null<QWidget*> widget) {
#ifdef Q_OS_MAC
	if (const auto topLevel = widget->window()) {
		const auto i = _leaveFilters.find(topLevel);
		if (i != end(_leaveFilters)) {
			i->second.registered = std::move(
				i->second.registered
			) | ranges::actions::remove_if([&](QPointer<QWidget> widget) {
				const auto pointer = widget.data();
				return !pointer || (pointer == widget);
			});
		}
	}
#endif // Q_OS_MAC
}

void Application::postponeCall(FnMut<void()> &&callable) {
	Sandbox::Instance().postponeCall(std::move(callable));
}

void Application::refreshGlobalProxy() {
	Sandbox::Instance().refreshGlobalProxy();
}

void QuitAttempt() {
	const auto savingSession = Sandbox::Instance().isSavingSession();
	if (!IsAppLaunched()
		|| savingSession
		|| App().readyToQuit()) {
		Sandbox::QuitWhenStarted();
	}
}

bool Application::readyToQuit() {
	auto prevented = false;
	if (_calls->isQuitPrevent()) {
		prevented = true;
	}
	if (_domain->started()) {
		for (const auto &[index, account] : _domain->accounts()) {
			if (const auto session = account->maybeSession()) {
				if (session->updates().isQuitPrevent()) {
					prevented = true;
				}
				if (session->api().isQuitPrevent()) {
					prevented = true;
				}
			}
		}
	}
	if (prevented) {
		quitDelayed();
		return false;
	}
	return true;
}

void Application::quitPreventFinished() {
	if (Quitting()) {
		QuitAttempt();
	}
}

void Application::quitDelayed() {
	if (_primaryWindow) {
		_primaryWindow->widget()->hide();
	}
	for (const auto &[history, window] : _secondaryWindows) {
		window->widget()->hide();
	}
	if (!_private->quitTimer.isActive()) {
		_private->quitTimer.setCallback([] { Sandbox::QuitWhenStarted(); });
		_private->quitTimer.callOnce(kQuitPreventTimeoutMs);
	}
}

void Application::startShortcuts() {
	Shortcuts::Start();

	_domain->activeSessionChanges(
	) | rpl::start_with_next([=](Main::Session *session) {
		const auto support = session && session->supportMode();
		Shortcuts::ToggleSupportShortcuts(support);
		Platform::SetApplicationIcon(Window::CreateIcon(
			session,
			Platform::IsMac()));
	}, _lifetime);

	Shortcuts::Requests(
	) | rpl::start_with_next([=](not_null<Shortcuts::Request*> request) {
		using Command = Shortcuts::Command;
		request->check(Command::Quit) && request->handle([] {
			Quit();
			return true;
		});
		request->check(Command::Lock) && request->handle([=] {
			if (!passcodeLocked() && _domain->local().hasLocalPasscode()) {
				lockByPasscode();
				return true;
			}
			return false;
		});
		request->check(Command::Minimize) && request->handle([=] {
			return minimizeActiveWindow();
		});
		request->check(Command::Close) && request->handle([=] {
			return closeActiveWindow();
		});
	}, _lifetime);
}

void Application::RegisterUrlScheme() {
	base::Platform::RegisterUrlScheme(base::Platform::UrlSchemeDescriptor{
		.executable = cExeDir() + cExeName(),
		.arguments = qsl("-workdir \"%1\"").arg(cWorkingDir()),
		.protocol = qsl("tg"),
		.protocolName = qsl("Telegram Link"),
		.shortAppName = qsl("tdesktop"),
		.longAppName = QCoreApplication::applicationName(),
		.displayAppName = AppName.utf16(),
		.displayAppDescription = AppName.utf16(),
	});
}

bool IsAppLaunched() {
	return (Application::Instance != nullptr);
}

Application &App() {
	Expects(Application::Instance != nullptr);

	return *Application::Instance;
}

void Quit(QuitReason reason) {
   if (Quitting()) {
	   return;
   } else if (IsAppLaunched() && App().preventsQuit(reason)) {
	   return;
   }
   SetLaunchState(LaunchState::QuitRequested);

   QuitAttempt();
}

bool Quitting() {
   return GlobalLaunchState != LaunchState::Running;
}

LaunchState CurrentLaunchState() {
   return GlobalLaunchState;
}

void SetLaunchState(LaunchState state) {
   GlobalLaunchState = state;
}

void Restart() {
   const auto updateReady = !UpdaterDisabled()
	   && (UpdateChecker().state() == UpdateChecker::State::Ready);
   if (updateReady) {
	   cSetRestartingUpdate(true);
   } else {
	   cSetRestarting(true);
	   cSetRestartingToSettings(true);
   }
   Quit();
}

} // namespace Core
