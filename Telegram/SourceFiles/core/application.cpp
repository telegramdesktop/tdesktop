/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/application.h"

#include "data/data_photo.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "base/timer.h"
#include "base/concurrent_timer.h"
#include "base/qt_signal_producer.h"
#include "base/unixtime.h"
#include "core/update_checker.h"
#include "core/shortcuts.h"
#include "core/sandbox.h"
#include "core/local_url_handlers.h"
#include "core/launcher.h"
#include "core/ui_integration.h"
#include "core/core_settings.h"
#include "chat_helpers/emoji_keywords.h"
#include "chat_helpers/stickers_emoji_image_loader.h"
#include "base/platform/base_platform_info.h"
#include "base/platform/base_platform_last_input.h"
#include "platform/platform_specific.h"
#include "mainwindow.h"
#include "dialogs/dialogs_entry.h"
#include "history/history.h"
#include "apiwrap.h"
#include "api/api_updates.h"
#include "calls/calls_instance.h"
#include "lang/lang_file_parser.h"
#include "lang/lang_translator.h"
#include "lang/lang_cloud_manager.h"
#include "lang/lang_hardcoded.h"
#include "lang/lang_instance.h"
#include "mainwidget.h"
#include "core/file_utilities.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "media/view/media_view_overlay_widget.h"
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
#include "ui/image/image.h"
#include "ui/text/text_options.h"
#include "ui/emoji_config.h"
#include "ui/effects/animations.h"
#include "storage/serialize_common.h"
#include "storage/storage_domain.h"
#include "storage/storage_databases.h"
#include "storage/localstorage.h"
#include "export/export_manager.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "base/qthelp_regex.h"
#include "base/qthelp_url.h"
#include "boxes/connection_box.h"
#include "boxes/confirm_phone_box.h"
#include "boxes/confirm_box.h"
#include "boxes/share_box.h"
#include "facades.h"
#include "app.h"

#include <QtWidgets/QDesktopWidget>
#include <QtCore/QMimeDatabase>
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>

namespace Core {
namespace {

constexpr auto kQuitPreventTimeoutMs = crl::time(1500);
constexpr auto kAutoLockTimeoutLateMs = crl::time(3000);
constexpr auto kClearEmojiImageSourceTimeout = 10 * crl::time(1000);

} // namespace

Application *Application::Instance = nullptr;

struct Application::Private {
	base::Timer quitTimer;
	UiIntegration uiIntegration;
};

Application::Application(not_null<Launcher*> launcher)
: QObject()
, _launcher(launcher)
, _private(std::make_unique<Private>())
, _databases(std::make_unique<Storage::Databases>())
, _animationsManager(std::make_unique<Ui::Animations::Manager>())
, _clearEmojiImageLoaderTimer([=] { clearEmojiSourceImages(); })
, _audio(std::make_unique<Media::Audio::Instance>())
, _fallbackProductionConfig(
	std::make_unique<MTP::Config>(MTP::Environment::Production))
, _domain(std::make_unique<Main::Domain>(cDataFile()))
, _exportManager(std::make_unique<Export::Manager>())
, _calls(std::make_unique<Calls::Instance>())
, _langpack(std::make_unique<Lang::Instance>())
, _langCloudManager(std::make_unique<Lang::CloudManager>(langpack()))
, _emojiKeywords(std::make_unique<ChatHelpers::EmojiKeywords>())
, _logo(Window::LoadLogo())
, _logoNoMargin(Window::LoadLogoNoMargin())
, _autoLockTimer([=] { checkAutoLock(); }) {
	Expects(!_logo.isNull());
	Expects(!_logoNoMargin.isNull());

	Ui::Integration::Set(&_private->uiIntegration);

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

	_domain->activeValue(
	) | rpl::filter(rpl::mappers::_1 != nullptr
	) | rpl::take(1) | rpl::start_with_next([=] {
		if (_window) {
			// Global::DesktopNotify is used in updateTrayMenu.
			// This should be called when user settings are read.
			// Right now after they are read the startMtp() is called.
			_window->widget()->updateTrayMenu();
		}
	}, _lifetime);
}

Application::~Application() {
	// Depend on activeWindow() for now :(
	Shortcuts::Finish();

	_window = nullptr;
	_mediaView = nullptr;
	_notifications->clearAllFast();
	_domain->finish();

	Local::finish();

	Shortcuts::Finish();

	Ui::Emoji::Clear();
	Media::Clip::Finish();

	App::deinitMedia();

	Window::Theme::Uninitialize();

	Media::Player::finish(_audio.get());
	style::stopManager();

	Global::finish();
	ThirdParty::finish();

	Instance = nullptr;
}

void Application::run() {
	style::internal::StartFonts();

	ThirdParty::start();
	Global::start();
	refreshGlobalProxy(); // Depends on Global::start().

	// Depends on OpenSSL on macOS, so on ThirdParty::start().
	// Depends on notifications settings.
	_notifications = std::make_unique<Window::Notifications::System>();

	startLocalStorage();
	ValidateScale();

	if (Local::oldSettingsVersion() < AppVersion) {
		psNewVersion();
	}

	if (cAutoStart() && !Platform::AutostartSupported()) {
		cSetAutoStart(false);
	}

	if (cLaunchMode() == LaunchModeAutoStart && !cAutoStart()) {
		psAutoStart(false, true);
		App::quit();
		return;
	}

	Core::App().settings().setWindowControlsLayout(Platform::WindowControlsLayout());

	_translator = std::make_unique<Lang::Translator>();
	QCoreApplication::instance()->installTranslator(_translator.get());

	style::startManager(cScale());
	Ui::InitTextOptions();
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

	cChangeTimeFormat(QLocale::system().timeFormat(QLocale::ShortFormat));

	DEBUG_LOG(("Application Info: starting app..."));

	// Create mime database, so it won't be slow later.
	QMimeDatabase().mimeTypeForName(qsl("text/plain"));

	_window = std::make_unique<Window::Controller>();

	_domain->activeChanges(
	) | rpl::start_with_next([=](not_null<Main::Account*> account) {
		_window->showAccount(account);
	}, _window->widget()->lifetime());

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

	// Depend on activeWindow() for now :(
	startShortcuts();
	App::initMedia();
	startDomain();

	_window->widget()->show();

	const auto currentGeometry = _window->widget()->geometry();
	_mediaView = std::make_unique<Media::View::OverlayWidget>();
	_window->widget()->setGeometry(currentGeometry);

	DEBUG_LOG(("Application Info: showing."));
	_window->finishFirstShow();

	if (!_window->locked() && cStartToSettings()) {
		_window->showSettings();
	}

	_window->updateIsActiveFocus();

	for (const auto &error : Shortcuts::Errors()) {
		LOG(("Shortcuts Error: %1").arg(error));
	}
}

void Application::startDomain() {
	const auto state = _domain->start(QByteArray());
	if (state != Storage::StartResult::IncorrectPasscodeLegacy) {
		// In case of non-legacy passcoded app all global settings are ready.
		startSettingsAndBackground();
	}
	if (state != Storage::StartResult::Success) {
		Global::SetLocalPasscode(true);
		Global::RefLocalPasscodeChanged().notify();
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
	const auto maybeDarkMode = _settings.systemDarkMode();
	const auto darkModeEnabled = _settings.systemDarkModeEnabled();
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
		_settings.setSystemDarkModeEnabled(false);
	}
	rpl::merge(
		_settings.systemDarkModeChanges() | rpl::to_empty,
		_settings.systemDarkModeEnabledChanges() | rpl::to_empty
	) | rpl::start_with_next([=] {
		checkSystemDarkMode();
	}, _lifetime);
}

auto Application::prepareEmojiSourceImages()
-> std::shared_ptr<Ui::Emoji::UniversalImages> {
	const auto &images = Ui::Emoji::SourceImages();
	if (_settings.largeEmoji()) {
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

void Application::showPhoto(not_null<const PhotoOpenClickHandler*> link) {
	const auto photo = link->photo();
	const auto peer = link->peer();
	const auto item = photo->owner().message(link->context());
	return (!item && peer)
		? showPhoto(photo, peer)
		: showPhoto(photo, item);
}

void Application::showPhoto(not_null<PhotoData*> photo, HistoryItem *item) {
	Expects(_mediaView != nullptr);

	_mediaView->showPhoto(photo, item);
	_mediaView->activateWindow();
	_mediaView->setFocus();
}

void Application::showPhoto(
		not_null<PhotoData*> photo,
		not_null<PeerData*> peer) {
	Expects(_mediaView != nullptr);

	_mediaView->showPhoto(photo, peer);
	_mediaView->activateWindow();
	_mediaView->setFocus();
}

void Application::showDocument(not_null<DocumentData*> document, HistoryItem *item) {
	Expects(_mediaView != nullptr);

	if (cUseExternalVideoPlayer()
		&& document->isVideoFile()
		&& !document->filepath().isEmpty()) {
		File::Launch(document->location(false).fname);
	} else {
		_mediaView->showDocument(document, item);
		_mediaView->activateWindow();
		_mediaView->setFocus();
	}
}

void Application::showTheme(
		not_null<DocumentData*> document,
		const Data::CloudTheme &cloud) {
	Expects(_mediaView != nullptr);

	_mediaView->showTheme(document, cloud);
	_mediaView->activateWindow();
	_mediaView->setFocus();
}

PeerData *Application::ui_getPeerForMouseAction() {
	if (_mediaView && !_mediaView->isHidden()) {
		return _mediaView->ui_getPeerForMouseAction();
	} else if (const auto m = App::main()) { // multi good
		return m->ui_getPeerForMouseAction();
	}
	return nullptr;
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
				_window->activate();
			}
		}
	} break;
	}

	return QObject::eventFilter(object, e);
}

void Application::saveSettingsDelayed(crl::time delay) {
	_saveSettingsTimer.callOnce(delay);
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
	const auto current = [&] {
		return (Global::ProxySettings() == MTP::ProxyData::Settings::Enabled)
			? Global::SelectedProxy()
			: MTP::ProxyData();
	};
	const auto was = current();
	Global::SetSelectedProxy(proxy);
	Global::SetProxySettings(settings);
	const auto now = current();
	refreshGlobalProxy();
	_proxyChanges.fire({ was, now });
	Global::RefConnectionTypeChanged().notify();
}

auto Application::proxyChanges() const -> rpl::producer<ProxyChange> {
	return _proxyChanges.events();
}

void Application::badMtprotoConfigurationError() {
	if (Global::ProxySettings() == MTP::ProxyData::Settings::Enabled
		&& !_badProxyDisableBox) {
		const auto disableCallback = [=] {
			setCurrentProxy(
				Global::SelectedProxy(),
				MTP::ProxyData::Settings::System);
		};
		_badProxyDisableBox = Ui::show(Box<InformBox>(
			Lang::Hard::ProxyConfigError(),
			disableCallback));
	}
}

void Application::startLocalStorage() {
	Local::start();
	_saveSettingsTimer.setCallback([=] { saveSettings(); });
}

void Application::startEmojiImageLoader() {
	_emojiImageLoader.with([
		source = prepareEmojiSourceImages(),
		large = _settings.largeEmoji()
	](Stickers::EmojiImageLoader &loader) mutable {
		loader.init(std::move(source), large);
	});

	_settings.largeEmojiChanges(
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

void Application::forceLogOut(
		not_null<Main::Account*> account,
		const TextWithEntities &explanation) {
	const auto box = Ui::show(Box<InformBox>(
		explanation,
		tr::lng_passcode_logout(tr::now)));
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
	if (const auto session = maybeActiveSession()) {
		session->updates().checkLastUpdate(adjusted);
	}
}

void Application::handleAppActivated() {
	checkLocalTime();
	if (_window) {
		_window->updateIsActiveFocus();
	}
}

void Application::handleAppDeactivated() {
	if (_window) {
		_window->updateIsActiveBlur();
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
		App::restart();
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
	App::restart();
}

void Application::writeInstallBetaVersionsSetting() {
	_launcher->writeInstallBetaVersionsSetting();
}

Main::Account &Application::activeAccount() const {
	return _domain->active();
}

Main::Session *Application::maybeActiveSession() const {
	return _domain->started() ? activeAccount().maybeSession() : nullptr;
}

bool Application::exportPreventsQuit() {
	if (_exportManager->inProgress()) {
		_exportManager->stopWithConfirmation([] {
			App::quit();
		});
		return true;
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
	if (!cStartUrl().isEmpty() && _window && !_window->locked()) {
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
	const auto command = urlTrimmed.midRef(protocol.size(), 8192);
	const auto controller = _window ? _window->sessionController() : nullptr;

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
	_window->preventOrInvoke(std::move(callback));
}

void Application::lockByPasscode() {
	preventOrInvoke([=] {
		if (_window) {
			_passcodeLock = true;
			_window->setupPasscodeLock();
		}
	});
}

void Application::unlockPasscode() {
	clearPasscodeLock();
	if (_window) {
		_window->clearPasscodeLock();
	}
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
	if (const auto session = maybeActiveSession()) {
		session->updates().checkIdleFinish();
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

void Application::checkAutoLock() {
	if (!Global::LocalPasscode()
		|| passcodeLocked()
		|| !someSessionExists()) {
		_shouldLockAt = 0;
		_autoLockTimer.cancel();
		return;
	}

	checkLocalTime();
	const auto now = crl::now();
	const auto shouldLockInMs = _settings.autoLock() * 1000LL;
	const auto checkTimeMs = now - lastNonIdleTime();
	if (checkTimeMs >= shouldLockInMs || (_shouldLockAt > 0 && now > _shouldLockAt + kAutoLockTimeoutLateMs)) {
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
	checkAutoLock();
}

bool Application::hasActiveWindow(not_null<Main::Session*> session) const {
	if (App::quitting() || !_window) {
		return false;
	} else if (_calls->hasActivePanel(session)) {
		return true;
	} else if (const auto controller = _window->sessionController()) {
		if (&controller->session() == session
			&& _window->widget()->isActive()) {
			return true;
		}
	}
	return false;
}

void Application::saveCurrentDraftsToHistories() {
	if (!_window) {
		return;
	} else if (const auto controller = _window->sessionController()) {
		controller->content()->saveFieldToHistoryLocalDraft();
	}
}

Window::Controller *Application::activeWindow() const {
	return _window.get();
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
	return (_mediaView && _mediaView->isVisible())
		? (QWidget*)_mediaView.get()
		: activeWindow()
		? (QWidget*)activeWindow()->widget()
		: nullptr;
}

void Application::notifyFileDialogShown(bool shown) {
	if (_mediaView) {
		_mediaView->notifyFileDialogShown(shown);
	}
}

QWidget *Application::getModalParent() {
	return (Platform::IsWayland() && activeWindow())
		? activeWindow()->widget().get()
		: nullptr;
}


void Application::checkMediaViewActivation() {
	if (_mediaView && !_mediaView->isHidden()) {
		_mediaView->activateWindow();
		QApplication::setActiveWindow(_mediaView.get());
		_mediaView->setFocus();
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
	if (const auto topLevel = widget->window()) {
		if (topLevel == _window->widget()) {
			auto weak = Ui::MakeWeak(widget);
			auto subscription = _window->widget()->leaveEvents(
			) | rpl::start_with_next([weak] {
				if (const auto window = weak.data()) {
					QEvent ev(QEvent::Leave);
					QGuiApplication::sendEvent(window, &ev);
				}
			});
			_leaveSubscriptions.emplace_back(weak, std::move(subscription));
		}
	}
#endif // Q_OS_MAC
}

void Application::unregisterLeaveSubscription(not_null<QWidget*> widget) {
#ifdef Q_OS_MAC
	_leaveSubscriptions = std::move(
		_leaveSubscriptions
	) | ranges::action::remove_if([&](const LeaveSubscription &subscription) {
		auto pointer = subscription.pointer.data();
		return !pointer || (pointer == widget);
	});
#endif // Q_OS_MAC
}

void Application::postponeCall(FnMut<void()> &&callable) {
	Sandbox::Instance().postponeCall(std::move(callable));
}

void Application::refreshGlobalProxy() {
	Sandbox::Instance().refreshGlobalProxy();
}

void Application::QuitAttempt() {
	if (!IsAppLaunched()
		|| Sandbox::Instance().isSavingSession()
		|| App().readyToQuit()) {
		QApplication::quit();
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
	if (App::quitting()) {
		QuitAttempt();
	}
}

void Application::quitDelayed() {
	if (!_private->quitTimer.isActive()) {
		_private->quitTimer.setCallback([] { QApplication::quit(); });
		_private->quitTimer.callOnce(kQuitPreventTimeoutMs);
	}
}

void Application::startShortcuts() {
	Shortcuts::Start();

	_domain->activeSessionChanges(
	) | rpl::start_with_next([=](Main::Session *session) {
		const auto support = session && session->supportMode();
		Shortcuts::ToggleSupportShortcuts(support);
		Platform::SetApplicationIcon(Window::CreateIcon(session));
	}, _lifetime);

	Shortcuts::Requests(
	) | rpl::start_with_next([=](not_null<Shortcuts::Request*> request) {
		using Command = Shortcuts::Command;
		request->check(Command::Quit) && request->handle([] {
			App::quit();
			return true;
		});
		request->check(Command::Lock) && request->handle([=] {
			if (!passcodeLocked() && Global::LocalPasscode()) {
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

bool IsAppLaunched() {
	return (Application::Instance != nullptr);
}

Application &App() {
	Expects(Application::Instance != nullptr);

	return *Application::Instance;
}

} // namespace Core
