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
#include "base/unixtime.h"
#include "core/update_checker.h"
#include "core/shortcuts.h"
#include "core/sandbox.h"
#include "core/local_url_handlers.h"
#include "core/launcher.h"
#include "chat_helpers/emoji_keywords.h"
#include "storage/localstorage.h"
#include "platform/platform_specific.h"
#include "mainwindow.h"
#include "dialogs/dialogs_entry.h"
#include "history/history.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "calls/calls_instance.h"
#include "lang/lang_file_parser.h"
#include "lang/lang_translator.h"
#include "lang/lang_cloud_manager.h"
#include "lang/lang_hardcoded.h"
#include "observer_peer.h"
#include "storage/storage_databases.h"
#include "mainwidget.h"
#include "main/main_account.h"
#include "media/view/media_view_overlay_widget.h"
#include "mtproto/dc_options.h"
#include "mtproto/mtp_instance.h"
#include "media/audio/media_audio.h"
#include "media/audio/media_audio_track.h"
#include "media/player/media_player_instance.h"
#include "media/clip/media_clip_reader.h" // For Media::Clip::Finish().
#include "window/notifications_manager.h"
#include "window/themes/window_theme.h"
#include "window/window_lock_widgets.h"
#include "history/history_location_manager.h"
#include "ui/widgets/tooltip.h"
#include "ui/image/image.h"
#include "ui/text_options.h"
#include "ui/emoji_config.h"
#include "ui/effects/animations.h"
#include "storage/serialize_common.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "base/qthelp_regex.h"
#include "base/qthelp_url.h"
#include "boxes/connection_box.h"
#include "boxes/confirm_phone_box.h"
#include "boxes/confirm_box.h"
#include "boxes/share_box.h"

namespace Core {
namespace {

constexpr auto kQuitPreventTimeoutMs = 1500;

} // namespace

Application *Application::Instance = nullptr;

struct Application::Private {
	base::Timer quitTimer;
};

Application::Application(not_null<Launcher*> launcher)
: QObject()
, _launcher(launcher)
, _private(std::make_unique<Private>())
, _databases(std::make_unique<Storage::Databases>())
, _animationsManager(std::make_unique<Ui::Animations::Manager>())
, _dcOptions(std::make_unique<MTP::DcOptions>())
, _account(std::make_unique<Main::Account>(cDataFile()))
, _langpack(std::make_unique<Lang::Instance>())
, _emojiKeywords(std::make_unique<ChatHelpers::EmojiKeywords>())
, _audio(std::make_unique<Media::Audio::Instance>())
, _logo(Window::LoadLogo())
, _logoNoMargin(Window::LoadLogoNoMargin()) {
	Expects(!_logo.isNull());
	Expects(!_logoNoMargin.isNull());

	activeAccount().sessionChanges(
	) | rpl::start_with_next([=] {
		if (_mediaView) {
			hideMediaView();
			_mediaView->clearData();
		}
	}, _lifetime);

	activeAccount().mtpChanges(
	) | rpl::filter([=](MTP::Instance *instance) {
		return instance != nullptr;
	}) | rpl::start_with_next([=](not_null<MTP::Instance*> mtp) {
		_langCloudManager = std::make_unique<Lang::CloudManager>(
			langpack(),
			mtp);
		if (!UpdaterDisabled()) {
			UpdateChecker().setMtproto(mtp.get());
		}
	}, _lifetime);
}

Application::~Application() {
	_window.reset();
	_mediaView.reset();

	// This can call writeMap() that serializes Main::Session.
	// In case it gets called after destroySession() we get missing data.
	Local::finish();

	// Some MTP requests can be cancelled from data clearing.
	unlockTerms();
	activeAccount().destroySession();

	// The langpack manager should be destroyed before MTProto instance,
	// because it is MTP::Sender and it may have pending requests.
	_langCloudManager.reset();

	activeAccount().clearMtp();

	Shortcuts::Finish();

	Ui::Emoji::Clear();
	Media::Clip::Finish();

	stopWebLoadManager();
	App::deinitMedia();

	Window::Theme::Unload();

	Media::Player::finish(_audio.get());
	style::stopManager();

	Global::finish();
	ThirdParty::finish();

	Instance = nullptr;
}

void Application::run() {
	Fonts::Start();

	ThirdParty::start();
	Global::start();
	refreshGlobalProxy(); // Depends on Global::started().

	startLocalStorage();

	if (Local::oldSettingsVersion() < AppVersion) {
		psNewVersion();
	}

	if (cLaunchMode() == LaunchModeAutoStart && !cAutoStart()) {
		psAutoStart(false, true);
		App::quit();
		return;
	}

	_translator = std::make_unique<Lang::Translator>();
	QCoreApplication::instance()->installTranslator(_translator.get());

	style::startManager();
	Ui::InitTextOptions();
	Ui::Emoji::Init();
	Media::Player::start(_audio.get());

	DEBUG_LOG(("Application Info: inited..."));

	cChangeTimeFormat(QLocale::system().timeFormat(QLocale::ShortFormat));

	DEBUG_LOG(("Application Info: starting app..."));

	// Create mime database, so it won't be slow later.
	QMimeDatabase().mimeTypeForName(qsl("text/plain"));

	_window = std::make_unique<Window::Controller>(&activeAccount());

	const auto currentGeometry = _window->widget()->geometry();
	_mediaView = std::make_unique<Media::View::OverlayWidget>();
	_window->widget()->setGeometry(currentGeometry);

	QCoreApplication::instance()->installEventFilter(this);
	connect(
		static_cast<QGuiApplication*>(QCoreApplication::instance()),
		&QGuiApplication::applicationStateChanged,
		this,
		&Application::stateChanged);

	DEBUG_LOG(("Application Info: window created..."));

	startShortcuts();
	App::initMedia();

	Local::ReadMapState state = Local::readMap(QByteArray());
	if (state == Local::ReadMapPassNeeded) {
		Global::SetLocalPasscode(true);
		Global::RefLocalPasscodeChanged().notify();
		lockByPasscode();
		DEBUG_LOG(("Application Info: passcode needed..."));
	} else {
		DEBUG_LOG(("Application Info: local map read..."));
		activeAccount().startMtp();
		DEBUG_LOG(("Application Info: MTP started..."));
		if (activeAccount().sessionExists()) {
			_window->setupMain();
		} else {
			_window->setupIntro();
		}
	}
	DEBUG_LOG(("Application Info: showing."));
	_window->firstShow();

	if (!locked() && cStartToSettings()) {
		_window->showSettings();
	}

	_window->updateIsActive(Global::OnlineFocusTimeout());

	for (const auto &error : Shortcuts::Errors()) {
		LOG(("Shortcuts Error: %1").arg(error));
	}
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
	_mediaView->showPhoto(photo, item);
	_mediaView->activateWindow();
	_mediaView->setFocus();
}

void Application::showPhoto(
		not_null<PhotoData*> photo,
		not_null<PeerData*> peer) {
	_mediaView->showPhoto(photo, peer);
	_mediaView->activateWindow();
	_mediaView->setFocus();
}

void Application::showDocument(not_null<DocumentData*> document, HistoryItem *item) {
	if (cUseExternalVideoPlayer()
		&& document->isVideoFile()
		&& document->loaded()) {
		QDesktopServices::openUrl(QUrl("file:///" + document->location(false).fname));
	} else {
		_mediaView->showDocument(document, item);
		_mediaView->activateWindow();
		_mediaView->setFocus();
	}
}

PeerData *Application::ui_getPeerForMouseAction() {
	if (_mediaView && !_mediaView->isHidden()) {
		return _mediaView->ui_getPeerForMouseAction();
	} else if (auto main = App::main()) {
		return main->ui_getPeerForMouseAction();
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

void Application::setCurrentProxy(
		const ProxyData &proxy,
		ProxyData::Settings settings) {
	const auto current = [&] {
		return (Global::ProxySettings() == ProxyData::Settings::Enabled)
			? Global::SelectedProxy()
			: ProxyData();
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
	if (Global::ProxySettings() == ProxyData::Settings::Enabled
		&& !_badProxyDisableBox) {
		const auto disableCallback = [=] {
			setCurrentProxy(
				Global::SelectedProxy(),
				ProxyData::Settings::System);
		};
		_badProxyDisableBox = Ui::show(Box<InformBox>(
			Lang::Hard::ProxyConfigError(),
			disableCallback));
	}
}

void Application::startLocalStorage() {
	Local::start();
	subscribe(_dcOptions->changed(), [this](const MTP::DcOptions::Ids &ids) {
		Local::writeSettings();
		if (const auto instance = activeAccount().mtp()) {
			for (const auto id : ids) {
				instance->restart(id);
			}
		}
	});
	_saveSettingsTimer.setCallback([=] { Local::writeSettings(); });
}

void Application::forceLogOut(const TextWithEntities &explanation) {
	const auto box = Ui::show(Box<InformBox>(
		explanation,
		tr::lng_passcode_logout(tr::now)));
	box->setCloseByEscape(false);
	box->setCloseByOutsideClick(false);
	connect(box, &QObject::destroyed, [=] {
		crl::on_main(this, [=] {
			activeAccount().forcedLogOut();
		});
	});
}

void Application::checkLocalTime() {
	if (crl::adjust_time()) {
		base::Timer::Adjust();
		base::ConcurrentTimerEnvironment::Adjust();
		base::unixtime::http_invalidate();
		if (App::main()) App::main()->checkLastUpdate(true);
	} else {
		if (App::main()) App::main()->checkLastUpdate(false);
	}
}

void Application::stateChanged(Qt::ApplicationState state) {
	if (state == Qt::ApplicationActive) {
		handleAppActivated();
	} else {
		handleAppDeactivated();
	}
}

void Application::handleAppActivated() {
	checkLocalTime();
	if (_window) {
		_window->updateIsActive(Global::OnlineFocusTimeout());
	}
}

void Application::handleAppDeactivated() {
	if (_window) {
		_window->updateIsActive(Global::OfflineBlurTimeout());
	}
	Ui::Tooltip::Hide();
}

void Application::call_handleUnreadCounterUpdate() {
	Global::RefUnreadCounterUpdate().notify(true);
}

void Application::call_handleDelayedPeerUpdates() {
	Notify::peerUpdatedSendDelayed();
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

void Application::switchWorkMode() {
	Global::SetDialogsModeEnabled(!Global::DialogsModeEnabled());
	Global::SetDialogsMode(Dialogs::Mode::All);
	Local::writeUserSettings();
	App::restart();
}

void Application::switchTestMode() {
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

void Application::writeInstallBetaVersionsSetting() {
	_launcher->writeInstallBetaVersionsSetting();
}

bool Application::exportPreventsQuit() {
	if (!activeAccount().sessionExists()
		|| !activeAccount().session().data().exportInProgress()) {
		return false;
	}
	activeAccount().session().data().stopExportWithConfirmation([] {
		App::quit();
	});
	return true;
}

int Application::unreadBadge() const {
	return activeAccount().sessionExists()
		? activeAccount().session().data().unreadBadge()
		: 0;
}

bool Application::unreadBadgeMuted() const {
	return activeAccount().sessionExists()
		? activeAccount().session().data().unreadBadgeMuted()
		: false;
}

void Application::setInternalLinkDomain(const QString &domain) const {
	// This domain should start with 'http[s]://' and end with '/'.
	// Like 'https://telegram.me/' or 'https://t.me/'.
	auto validate = [](const auto &domain) {
		const auto prefixes = {
			qstr("https://"),
			qstr("http://"),
		};
		for (const auto &prefix : prefixes) {
			if (domain.startsWith(prefix, Qt::CaseInsensitive)) {
				return domain.endsWith('/');
			}
		}
		return false;
	};
	if (validate(domain) && domain != Global::InternalLinksDomain()) {
		Global::SetInternalLinksDomain(domain);
	}
}

QString Application::createInternalLink(const QString &query) const {
	auto result = createInternalLinkFull(query);
	auto prefixes = {
		qstr("https://"),
		qstr("http://"),
	};
	for (auto &prefix : prefixes) {
		if (result.startsWith(prefix, Qt::CaseInsensitive)) {
			return result.mid(prefix.size());
		}
	}
	LOG(("Warning: bad internal url '%1'").arg(result));
	return result;
}

QString Application::createInternalLinkFull(const QString &query) const {
	return Global::InternalLinksDomain() + query;
}

void Application::checkStartUrl() {
	if (!cStartUrl().isEmpty() && !locked()) {
		const auto url = cStartUrl();
		cSetStartUrl(QString());
		if (!openLocalUrl(url, {})) {
			cSetStartUrl(url);
		}
	}
}

bool Application::openLocalUrl(const QString &url, QVariant context) {
	auto urlTrimmed = url.trimmed();
	if (urlTrimmed.size() > 8192) urlTrimmed = urlTrimmed.mid(0, 8192);

	const auto protocol = qstr("tg://");
	if (!urlTrimmed.startsWith(protocol, Qt::CaseInsensitive) || locked()) {
		return false;
	}
	auto command = urlTrimmed.midRef(protocol.size());

	const auto session = activeAccount().sessionExists()
		? &activeAccount().session()
		: nullptr;
	using namespace qthelp;
	const auto options = RegExOption::CaseInsensitive;
	for (const auto &[expression, handler] : LocalUrlHandlers()) {
		const auto match = regex_match(expression, command, options);
		if (match) {
			return handler(session, match, context);
		}
	}
	return false;
}

void Application::lockByPasscode() {
	_passcodeLock = true;
	_window->setupPasscodeLock();
}

void Application::unlockPasscode() {
	clearPasscodeLock();
	if (!activeAccount().mtp()) {
		// We unlocked initial passcode, so we just start mtproto.
		activeAccount().startMtp();
	}
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
}

crl::time Application::lastNonIdleTime() const {
	return std::max(
		Platform::LastUserInputTime().value_or(0),
		_lastNonIdleTime);
}

rpl::producer<bool> Application::passcodeLockChanges() const {
	return _passcodeLock.changes();
}

rpl::producer<bool> Application::passcodeLockValue() const {
	return _passcodeLock.value();
}

void Application::lockByTerms(const Window::TermsLock &data) {
	if (!_termsLock || *_termsLock != data) {
		_termsLock = std::make_unique<Window::TermsLock>(data);
		_termsLockChanges.fire(true);
	}
}

void Application::unlockTerms() {
	if (_termsLock) {
		_termsLock = nullptr;
		_termsLockChanges.fire(false);
	}
}

std::optional<Window::TermsLock> Application::termsLocked() const {
	return _termsLock ? base::make_optional(*_termsLock) : std::nullopt;
}

rpl::producer<bool> Application::termsLockChanges() const {
	return _termsLockChanges.events();
}

rpl::producer<bool> Application::termsLockValue() const {
	return rpl::single(
		_termsLock != nullptr
	) | rpl::then(termsLockChanges());
}

bool Application::locked() const {
	return passcodeLocked() || termsLocked();
}

rpl::producer<bool> Application::lockChanges() const {
	return lockValue() | rpl::skip(1);
}

rpl::producer<bool> Application::lockValue() const {
	using namespace rpl::mappers;
	return rpl::combine(
		passcodeLockValue(),
		termsLockValue(),
		_1 || _2);
}

Window::Controller *Application::activeWindow() const {
	return _window.get();
}

bool Application::closeActiveWindow() {
	if (hideMediaView()) {
		return true;
	}
	if (const auto window = activeWindow()) {
		window->close();
		return true;
	}
	return false;
}

bool Application::minimizeActiveWindow() {
	hideMediaView();
	if (const auto window = activeWindow()) {
		window->minimize();
		return true;
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
	return QApplication::desktop()->screenGeometry().center();
}

// macOS Qt bug workaround, sometimes no leaveEvent() gets to the nested widgets.
void Application::registerLeaveSubscription(QWidget *widget) {
#ifdef Q_OS_MAC
	if (const auto topLevel = widget->window()) {
		if (topLevel == _window->widget()) {
			auto weak = make_weak(widget);
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

void Application::unregisterLeaveSubscription(QWidget *widget) {
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

void Application::activateWindowDelayed(not_null<QWidget*> widget) {
	Sandbox::Instance().activateWindowDelayed(widget);
}

void Application::pauseDelayedWindowActivations() {
	Sandbox::Instance().pauseDelayedWindowActivations();
}

void Application::resumeDelayedWindowActivations() {
	Sandbox::Instance().resumeDelayedWindowActivations();
}

void Application::preventWindowActivation() {
	pauseDelayedWindowActivations();
	postponeCall([=] {
		resumeDelayedWindowActivations();
	});
}

void Application::QuitAttempt() {
	auto prevents = false;
	if (IsAppLaunched()
		&& App().activeAccount().sessionExists()
		&& !Sandbox::Instance().isSavingSession()) {
		if (const auto mainwidget = App::main()) {
			if (mainwidget->isQuitPrevent()) {
				prevents = true;
			}
		}
		if (App().activeAccount().session().api().isQuitPrevent()) {
			prevents = true;
		}
		if (App().activeAccount().session().calls().isQuitPrevent()) {
			prevents = true;
		}
	}
	if (prevents) {
		App().quitDelayed();
	} else {
		QApplication::quit();
	}
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
