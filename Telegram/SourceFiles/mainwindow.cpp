/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mainwindow.h"

#include "data/data_document.h"
#include "data/data_session.h"
#include "dialogs/dialogs_layout.h"
#include "styles/style_dialogs.h"
#include "styles/style_window.h"
#include "styles/style_boxes.h"
#include "history/history.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/emoji_config.h"
#include "lang/lang_cloud_manager.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "core/shortcuts.h"
#include "core/sandbox.h"
#include "core/application.h"
#include "main/main_session.h"
#include "intro/introwidget.h"
#include "main/main_account.h" // Account::sessionValue.
#include "mainwidget.h"
#include "boxes/confirm_box.h"
#include "boxes/add_contact_box.h"
#include "boxes/connection_box.h"
#include "observer_peer.h"
#include "storage/localstorage.h"
#include "apiwrap.h"
#include "settings/settings_intro.h"
#include "platform/platform_notifications_manager.h"
#include "platform/platform_info.h"
#include "window/layer_widget.h"
#include "window/notifications_manager.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_theme_warning.h"
#include "window/window_lock_widgets.h"
#include "window/window_main_menu.h"
#include "window/window_session_controller.h"

namespace {

// Code for testing languages is F7-F6-F7-F8
void FeedLangTestingKey(int key) {
	static auto codeState = 0;
	if ((codeState == 0 && key == Qt::Key_F7)
		|| (codeState == 1 && key == Qt::Key_F6)
		|| (codeState == 2 && key == Qt::Key_F7)
		|| (codeState == 3 && key == Qt::Key_F8)) {
		++codeState;
	} else {
		codeState = 0;
	}
	if (codeState == 4) {
		codeState = 0;
		Lang::CurrentCloudManager().switchToTestLanguage();
	}
}

} // namespace

MainWindow::MainWindow(not_null<Window::Controller*> controller)
: Platform::MainWindow(controller) {
	auto logo = Core::App().logo();
	icon16 = logo.scaledToWidth(16, Qt::SmoothTransformation);
	icon32 = logo.scaledToWidth(32, Qt::SmoothTransformation);
	icon64 = logo.scaledToWidth(64, Qt::SmoothTransformation);

	auto logoNoMargin = Core::App().logoNoMargin();
	iconbig16 = logoNoMargin.scaledToWidth(16, Qt::SmoothTransformation);
	iconbig32 = logoNoMargin.scaledToWidth(32, Qt::SmoothTransformation);
	iconbig64 = logoNoMargin.scaledToWidth(64, Qt::SmoothTransformation);

	resize(st::windowDefaultWidth, st::windowDefaultHeight);

	setLocale(QLocale(QLocale::English, QLocale::UnitedStates));

	account().sessionValue(
	) | rpl::start_with_next([=](Main::Session *session) {
		updateGlobalMenu();
		if (!session) {
			_mediaPreview.destroy();
		}
	}, lifetime());
	subscribe(Window::Theme::Background(), [this](const Window::Theme::BackgroundUpdate &data) {
		themeUpdated(data);
	});
	Core::App().lockChanges(
	) | rpl::start_with_next([=] {
		updateGlobalMenu();
	}, lifetime());

	Ui::Emoji::Updated(
	) | rpl::start_with_next([=] {
		Ui::ForceFullRepaint(this);
	}, lifetime());

	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
}

void MainWindow::initHook() {
	Platform::MainWindow::initHook();

	QCoreApplication::instance()->installEventFilter(this);

	// Non-queued activeChanged handlers must use QtSignalProducer.
	connect(
		windowHandle(),
		&QWindow::activeChanged,
		this,
		[=] { checkHistoryActivation(); },
		Qt::QueuedConnection);
}

void MainWindow::firstShow() {
#ifdef Q_OS_WIN
	trayIconMenu = new Ui::PopupMenu(nullptr);
	trayIconMenu->deleteOnHide(false);
#else // Q_OS_WIN
	trayIconMenu = new QMenu(this);
#endif // else for Q_OS_WIN

	auto notificationActionText = Global::DesktopNotify()
		? tr::lng_disable_notifications_from_tray(tr::now)
		: tr::lng_enable_notifications_from_tray(tr::now);

	if (Platform::IsLinux()) {
		trayIconMenu->addAction(tr::lng_open_from_tray(tr::now), this, SLOT(showFromTray()));
		trayIconMenu->addAction(tr::lng_minimize_to_tray(tr::now), this, SLOT(minimizeToTray()));
		trayIconMenu->addAction(notificationActionText, this, SLOT(toggleDisplayNotifyFromTray()));
		trayIconMenu->addAction(tr::lng_quit_from_tray(tr::now), this, SLOT(quitFromTray()));
	} else {
		trayIconMenu->addAction(tr::lng_minimize_to_tray(tr::now), this, SLOT(minimizeToTray()));
		trayIconMenu->addAction(notificationActionText, this, SLOT(toggleDisplayNotifyFromTray()));
		trayIconMenu->addAction(tr::lng_quit_from_tray(tr::now), this, SLOT(quitFromTray()));
	}
	Global::RefWorkMode().setForced(Global::WorkMode().value(), true);

	psFirstShow();
	updateTrayMenu();
}

void MainWindow::clearWidgetsHook() {
	destroyLayer();
	_main.destroy();
	_passcodeLock.destroy();
	_intro.destroy();
}

QPixmap MainWindow::grabInner() {
	if (_intro) {
		return Ui::GrabWidget(_intro);
	} else if (_passcodeLock) {
		return Ui::GrabWidget(_passcodeLock);
	} else if (_main) {
		return Ui::GrabWidget(_main);
	}
	return {};
}

void MainWindow::setupPasscodeLock() {
	auto animated = (_main || _intro);
	auto bg = animated ? grabInner() : QPixmap();
	_passcodeLock.create(bodyWidget());
	updateControlsGeometry();

	Core::App().hideMediaView();
	Ui::hideSettingsAndLayer(anim::type::instant);
	if (_main) {
		_main->hide();
	}
	if (_intro) {
		_intro->hide();
	}
	if (animated) {
		_passcodeLock->showAnimated(bg);
	} else {
		setInnerFocus();
	}
}

void MainWindow::clearPasscodeLock() {
	if (!_passcodeLock) return;

	auto bg = grabInner();

	_passcodeLock.destroy();
	if (_intro) {
		_intro->showAnimated(bg, true);
	} else if (_main) {
		_main->showAnimated(bg, true);
		Core::App().checkStartUrl();
	} else if (account().sessionExists()) {
		setupMain();
	} else {
		setupIntro();
	}
}

void MainWindow::setupIntro() {
	auto animated = (_main || _passcodeLock);
	auto bg = animated ? grabInner() : QPixmap();

	clearWidgets();
	_intro.create(bodyWidget(), &account());
	updateControlsGeometry();

	if (animated) {
		_intro->showAnimated(bg);
	} else {
		setInnerFocus();
	}

	fixOrder();
}

void MainWindow::setupMain() {
	Expects(account().sessionExists());

	auto animated = (_intro || _passcodeLock);
	auto bg = animated ? grabInner() : QPixmap();

	clearWidgets();

	_main.create(bodyWidget(), sessionController());
	_main->show();
	updateControlsGeometry();

	if (animated) {
		_main->showAnimated(bg);
	} else {
		_main->activate();
	}
	_main->start();

	fixOrder();
}

void MainWindow::showSettings() {
	if (isHidden()) {
		showFromTray();
	}
	if (_passcodeLock) {
		return;
	}

	if (const auto controller = sessionController()) {
		controller->showSettings();
	} else {
		showSpecialLayer(Box<Settings::LayerWidget>(), anim::type::normal);
	}
}

void MainWindow::showSpecialLayer(
		object_ptr<Window::LayerWidget> layer,
		anim::type animated) {
	if (_passcodeLock) {
		return;
	}

	if (layer) {
		ensureLayerCreated();
		_layer->showSpecialLayer(std::move(layer), animated);
	} else if (_layer) {
		_layer->hideSpecialLayer(animated);
	}
}

bool MainWindow::showSectionInExistingLayer(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	if (_layer) {
		return _layer->showSectionInternal(memento, params);
	}
	return false;
}

void MainWindow::showMainMenu() {
	if (_passcodeLock) return;

	if (isHidden()) showFromTray();

	ensureLayerCreated();
	_layer->showMainMenu(sessionController(), anim::type::normal);
}

void MainWindow::ensureLayerCreated() {
	if (_layer) {
		return;
	}
	_layer = base::make_unique_q<Window::LayerStackWidget>(
		bodyWidget());

	_layer->hideFinishEvents(
	) | rpl::filter([=] {
		return _layer != nullptr; // Last hide finish is sent from destructor.
	}) | rpl::start_with_next([=] {
		destroyLayer();
	}, _layer->lifetime());

	if (const auto controller = sessionController()) {
		controller->enableGifPauseReason(Window::GifPauseReason::Layer);
	}
}

void MainWindow::destroyLayer() {
	if (!_layer) {
		return;
	}

	auto layer = base::take(_layer);
	const auto resetFocus = Ui::InFocusChain(layer);
	if (resetFocus) {
		setFocus();
	}
	layer = nullptr;

	if (const auto controller = sessionController()) {
		controller->disableGifPauseReason(Window::GifPauseReason::Layer);
	}
	if (resetFocus) {
		setInnerFocus();
	}
	InvokeQueued(this, [=] {
		checkHistoryActivation();
	});
}

void MainWindow::ui_hideSettingsAndLayer(anim::type animated) {
	if (animated == anim::type::instant) {
		destroyLayer();
	} else if (_layer) {
		_layer->hideAll(animated);
	}
}

void MainWindow::ui_removeLayerBlackout() {
	if (_layer) {
		_layer->removeBodyCache();
	}
}

MainWidget *MainWindow::mainWidget() {
	return _main;
}

void MainWindow::ui_showBox(
		object_ptr<BoxContent> box,
		LayerOptions options,
		anim::type animated) {
	if (box) {
		ensureLayerCreated();
		_layer->showBox(std::move(box), options, animated);
	} else {
		if (_layer) {
			_layer->hideTopLayer(animated);
			if ((animated == anim::type::instant)
				&& _layer
				&& !_layer->layerShown()) {
				destroyLayer();
			}
		}
		Core::App().hideMediaView();
	}
}

bool MainWindow::ui_isLayerShown() {
	return _layer != nullptr;
}

void MainWindow::showMediaPreview(
		Data::FileOrigin origin,
		not_null<DocumentData*> document) {
	if (!document || ((!document->isAnimation() || !document->loaded()) && !document->sticker())) {
		return;
	}
	if (!_mediaPreview) {
		_mediaPreview.create(bodyWidget(), sessionController());
		updateControlsGeometry();
	}
	if (_mediaPreview->isHidden()) {
		fixOrder();
	}
	_mediaPreview->showPreview(origin, document);
}

void MainWindow::showMediaPreview(
		Data::FileOrigin origin,
		not_null<PhotoData*> photo) {
	if (!photo) {
		return;
	}
	if (!_mediaPreview) {
		_mediaPreview.create(bodyWidget(), sessionController());
		updateControlsGeometry();
	}
	if (_mediaPreview->isHidden()) {
		fixOrder();
	}
	_mediaPreview->showPreview(origin, photo);
}

void MainWindow::hideMediaPreview() {
	if (!_mediaPreview) {
		return;
	}
	_mediaPreview->hidePreview();
}

void MainWindow::themeUpdated(const Window::Theme::BackgroundUpdate &data) {
	using Type = Window::Theme::BackgroundUpdate::Type;

	// We delay animating theme warning because we want all other
	// subscribers to receive paltte changed notification before any
	// animations (that include pixmap caches with old palette values).
	if (data.type == Type::TestingTheme) {
		if (!_testingThemeWarning) {
			_testingThemeWarning.create(bodyWidget());
			_testingThemeWarning->hide();
			_testingThemeWarning->setGeometry(rect());
			_testingThemeWarning->setHiddenCallback([this] { _testingThemeWarning.destroyDelayed(); });
		}
		crl::on_main(this, [=] {
			if (_testingThemeWarning) {
				_testingThemeWarning->showAnimated();
			}
		});
	} else if (data.type == Type::RevertingTheme || data.type == Type::ApplyingTheme) {
		if (_testingThemeWarning) {
			if (_testingThemeWarning->isHidden()) {
				_testingThemeWarning.destroy();
			} else {
				crl::on_main(this, [=] {
					if (_testingThemeWarning) {
						_testingThemeWarning->hideAnimated();
						_testingThemeWarning = nullptr;
					}
					setInnerFocus();
				});
			}
		}
	}
}

bool MainWindow::doWeReadServerHistory() {
	updateIsActive(0);
	return isActive()
		&& !Ui::isLayerShown()
		&& (_main ? _main->doWeReadServerHistory() : false);
}

bool MainWindow::doWeReadMentions() {
	updateIsActive(0);
	return isActive()
		&& !Ui::isLayerShown()
		&& (_main ? _main->doWeReadMentions() : false);
}

void MainWindow::checkHistoryActivation() {
	if (doWeReadServerHistory()) {
		_main->markActiveHistoryAsRead();
	}
}

bool MainWindow::contentOverlapped(const QRect &globalRect) {
	if (_main && _main->contentOverlapped(globalRect)) return true;
	if (_layer && _layer->contentOverlapped(globalRect)) return true;
	return false;
}

void MainWindow::setInnerFocus() {
	if (_testingThemeWarning) {
		_testingThemeWarning->setFocus();
	} else if (_layer && _layer->canSetFocus()) {
		_layer->setInnerFocus();
	} else if (_passcodeLock) {
		_passcodeLock->setInnerFocus();
	} else if (_main) {
		_main->setInnerFocus();
	} else if (_intro) {
		_intro->setInnerFocus();
	}
}

bool MainWindow::eventFilter(QObject *object, QEvent *e) {
	switch (e->type()) {
	case QEvent::KeyPress: {
		if (Logs::DebugEnabled()
			&& (e->type() == QEvent::KeyPress)
			&& object == windowHandle()) {
			auto key = static_cast<QKeyEvent*>(e)->key();
			FeedLangTestingKey(key);
		}
	} break;

	case QEvent::MouseMove: {
		if (_main && _main->isIdle()) {
			Core::App().updateNonIdle();
			_main->checkIdleFinish();
		}
	} break;

	case QEvent::MouseButtonRelease: {
		hideMediaPreview();
	} break;

	case QEvent::ApplicationActivate: {
		if (object == QCoreApplication::instance()) {
			InvokeQueued(this, [=] {
				handleActiveChanged();
			});
		}
	} break;

	case QEvent::WindowStateChange: {
		if (object == this) {
			auto state = (windowState() & Qt::WindowMinimized) ? Qt::WindowMinimized :
				((windowState() & Qt::WindowMaximized) ? Qt::WindowMaximized :
				((windowState() & Qt::WindowFullScreen) ? Qt::WindowFullScreen : Qt::WindowNoState));
			handleStateChanged(state);
		}
	} break;

	case QEvent::Move:
	case QEvent::Resize: {
		if (object == this) {
			positionUpdated();
		}
	} break;
	}

	return Platform::MainWindow::eventFilter(object, e);
}

void MainWindow::updateTrayMenu(bool force) {
    if (!trayIconMenu || (Platform::IsWindows() && !force)) return;

	auto iconMenu = trayIconMenu;
	auto actions = iconMenu->actions();
	if (Platform::IsLinux()) {
		auto minimizeAction = actions.at(1);
		minimizeAction->setDisabled(!isVisible());
	} else {
		updateIsActive(0);
		auto active = isActive();
		auto toggleAction = actions.at(0);
		disconnect(toggleAction, SIGNAL(triggered(bool)), this, SLOT(minimizeToTray()));
		disconnect(toggleAction, SIGNAL(triggered(bool)), this, SLOT(showFromTray()));
		connect(toggleAction, SIGNAL(triggered(bool)), this, active ? SLOT(minimizeToTray()) : SLOT(showFromTray()));
		toggleAction->setText(active
			? tr::lng_minimize_to_tray(tr::now)
			: tr::lng_open_from_tray(tr::now));

		// On macOS just remove trayIcon menu if the window is not active.
		// So we will activate the window on click instead of showing the menu.
		if (!active && Platform::IsMac()) {
			iconMenu = nullptr;
		}
	}
	auto notificationAction = actions.at(Platform::IsLinux() ? 2 : 1);
	auto notificationActionText = Global::DesktopNotify()
		? tr::lng_disable_notifications_from_tray(tr::now)
		: tr::lng_enable_notifications_from_tray(tr::now);
	notificationAction->setText(notificationActionText);

#ifndef Q_OS_WIN
    if (trayIcon && trayIcon->contextMenu() != iconMenu) {
		trayIcon->setContextMenu(iconMenu);
    }
#endif // !Q_OS_WIN

    psTrayMenuUpdated();
}

void MainWindow::onShowAddContact() {
	if (isHidden()) showFromTray();

	if (account().sessionExists()) {
		Ui::show(
			Box<AddContactBox>(&account().session()),
			LayerOption::KeepOther);
	}
}

void MainWindow::onShowNewGroup() {
	if (isHidden()) showFromTray();

	if (account().sessionExists()) {
		Ui::show(
			Box<GroupInfoBox>(
				sessionController(),
				GroupInfoBox::Type::Group),
			LayerOption::KeepOther);
	}
}

void MainWindow::onShowNewChannel() {
	if (isHidden()) showFromTray();

	if (account().sessionExists()) {
		Ui::show(
			Box<GroupInfoBox>(
				sessionController(),
				GroupInfoBox::Type::Channel),
			LayerOption::KeepOther);
	}
}

void MainWindow::onLogout() {
	if (isHidden()) {
		showFromTray();
	}

	const auto callback = [=] {
		if (account().sessionExists()
			&& account().session().data().exportInProgress()) {
			Ui::hideLayer();
			account().session().data().stopExportWithConfirmation([=] {
				account().logOut();
			});
		} else {
			account().logOut();
		}
	};
	Ui::show(Box<ConfirmBox>(
		tr::lng_sure_logout(tr::now),
		tr::lng_settings_logout(tr::now),
		st::attentionBoxButton,
		callback));
}

void MainWindow::quitFromTray() {
	App::quit();
}

void MainWindow::activate() {
	bool wasHidden = !isVisible();
	setWindowState(windowState() & ~Qt::WindowMinimized);
	setVisible(true);
	psActivateProcess();
	activateWindow();
	updateIsActive(Global::OnlineFocusTimeout());
	if (wasHidden) {
		if (_main) {
			_main->windowShown();
		}
	}
}

void MainWindow::noIntro(Intro::Widget *was) {
	if (was == _intro) {
		_intro = nullptr;
	}
}

bool MainWindow::takeThirdSectionFromLayer() {
	return _layer ? _layer->takeToThirdSection() : false;
}

void MainWindow::fixOrder() {
	if (_layer) _layer->raise();
	if (_mediaPreview) _mediaPreview->raise();
	if (_testingThemeWarning) _testingThemeWarning->raise();
}

void MainWindow::showFromTray(QSystemTrayIcon::ActivationReason reason) {
	if (reason != QSystemTrayIcon::Context) {
		App::CallDelayed(1, this, [this] {
			updateTrayMenu();
			updateGlobalMenu();
		});
        activate();
		Notify::unreadCounterUpdated();
	}
}

void MainWindow::handleTrayIconActication(
		QSystemTrayIcon::ActivationReason reason) {
	updateIsActive(0);
	if (Platform::IsMac() && isActive()) {
		return;
	}
	if (reason == QSystemTrayIcon::Context) {
		updateTrayMenu(true);
		QTimer::singleShot(1, this, SLOT(psShowTrayMenu()));
	} else if (!skipTrayClick()) {
		if (isActive()) {
			minimizeToTray();
		} else {
			showFromTray(reason);
		}
		_lastTrayClickTime = crl::now();
	}
}

bool MainWindow::skipTrayClick() const {
	return (_lastTrayClickTime > 0)
		&& (crl::now() - _lastTrayClickTime
			< QApplication::doubleClickInterval());
}

void MainWindow::toggleDisplayNotifyFromTray() {
	if (Core::App().locked()) {
		if (!isActive()) showFromTray();
		Ui::show(Box<InformBox>(tr::lng_passcode_need_unblock(tr::now)));
		return;
	}
	if (!account().sessionExists()) {
		return;
	}

	bool soundNotifyChanged = false;
	Global::SetDesktopNotify(!Global::DesktopNotify());
	if (Global::DesktopNotify()) {
		if (Global::RestoreSoundNotifyFromTray() && !Global::SoundNotify()) {
			Global::SetSoundNotify(true);
			Global::SetRestoreSoundNotifyFromTray(false);
			soundNotifyChanged = true;
		}
	} else {
		if (Global::SoundNotify()) {
			Global::SetSoundNotify(false);
			Global::SetRestoreSoundNotifyFromTray(true);
			soundNotifyChanged = true;
		} else {
			Global::SetRestoreSoundNotifyFromTray(false);
		}
	}
	Local::writeUserSettings();
	account().session().notifications().settingsChanged().notify(
		Window::Notifications::ChangeType::DesktopEnabled);
	if (soundNotifyChanged) {
		account().session().notifications().settingsChanged().notify(
			Window::Notifications::ChangeType::SoundEnabled);
	}
}

void MainWindow::closeEvent(QCloseEvent *e) {
	if (Core::Sandbox::Instance().isSavingSession()) {
		e->accept();
		App::quit();
	} else {
		e->ignore();
		if (!account().sessionExists() || !hideNoQuit()) {
			App::quit();
		}
	}
}

void MainWindow::updateControlsGeometry() {
	Platform::MainWindow::updateControlsGeometry();

	auto body = bodyWidget()->rect();
	if (_passcodeLock) _passcodeLock->setGeometry(body);
	if (_main) _main->setGeometry(body);
	if (_intro) _intro->setGeometry(body);
	if (_layer) _layer->setGeometry(body);
	if (_mediaPreview) _mediaPreview->setGeometry(body);
	if (_testingThemeWarning) _testingThemeWarning->setGeometry(body);

	if (_main) _main->checkMainSectionToLayer();
}

MainWindow::TempDirState MainWindow::tempDirState() {
	if (_clearManager && _clearManager->hasTask(Local::ClearManagerDownloads)) {
		return TempDirRemoving;
	}
	return QDir(cTempDir()).exists() ? TempDirExists : TempDirEmpty;
}

MainWindow::TempDirState MainWindow::localStorageState() {
	if (_clearManager && _clearManager->hasTask(Local::ClearManagerStorage)) {
		return TempDirRemoving;
	}
	return TempDirEmpty;
}

void MainWindow::tempDirDelete(int task) {
	if (_clearManager) {
		if (_clearManager->addTask(task)) {
			return;
		} else {
			_clearManager->stop();
			_clearManager = nullptr;
		}
	}
	_clearManager = new Local::ClearManager();
	_clearManager->addTask(task);
	connect(_clearManager, SIGNAL(succeed(int,void*)), this, SLOT(onClearFinished(int,void*)));
	connect(_clearManager, SIGNAL(failed(int,void*)), this, SLOT(onClearFailed(int,void*)));
	_clearManager->start();
}

void MainWindow::onClearFinished(int task, void *manager) {
	if (manager && manager == _clearManager) {
		_clearManager->stop();
		_clearManager = nullptr;
	}
	emit tempDirCleared(task);
}

void MainWindow::onClearFailed(int task, void *manager) {
	if (manager && manager == _clearManager) {
		_clearManager->stop();
		_clearManager = nullptr;
	}
	emit tempDirClearFailed(task);
}

void MainWindow::placeSmallCounter(QImage &img, int size, int count, style::color bg, const QPoint &shift, style::color color) {
	QPainter p(&img);

	QString cnt = (count < 100) ? QString("%1").arg(count) : QString("..%1").arg(count % 10, 1, 10, QChar('0'));
	int32 cntSize = cnt.size();

	p.setBrush(bg->b);
	p.setPen(Qt::NoPen);
	p.setRenderHint(QPainter::Antialiasing);
	int32 fontSize;
	if (size == 16) {
		fontSize = 8;
	} else if (size == 32) {
		fontSize = (cntSize < 2) ? 12 : 12;
	} else {
		fontSize = (cntSize < 2) ? 22 : 22;
	}
	style::font f = { fontSize, 0, 0 };
	int32 w = f->width(cnt), d, r;
	if (size == 16) {
		d = (cntSize < 2) ? 2 : 1;
		r = (cntSize < 2) ? 4 : 3;
	} else if (size == 32) {
		d = (cntSize < 2) ? 5 : 2;
		r = (cntSize < 2) ? 8 : 7;
	} else {
		d = (cntSize < 2) ? 9 : 4;
		r = (cntSize < 2) ? 16 : 14;
	}
	p.drawRoundedRect(QRect(shift.x() + size - w - d * 2, shift.y() + size - f->height, w + d * 2, f->height), r, r);
	p.setFont(f->f);

	p.setPen(color->p);

	p.drawText(shift.x() + size - w - d, shift.y() + size - f->height + f->ascent, cnt);

}

QImage MainWindow::iconWithCounter(int size, int count, style::color bg, style::color fg, bool smallIcon) {
	bool layer = false;
	if (size < 0) {
		size = -size;
		layer = true;
	}
	if (layer) {
		if (size != 16 && size != 20 && size != 24) size = 32;

		// platform/linux/main_window_linux depends on count used the same
		// way for all the same (count % 1000) values.
		QString cnt = (count < 1000) ? QString("%1").arg(count) : QString("..%1").arg(count % 100, 2, 10, QChar('0'));
		QImage result(size, size, QImage::Format_ARGB32);
		int32 cntSize = cnt.size();
		result.fill(Qt::transparent);
		{
			QPainter p(&result);
			p.setBrush(bg);
			p.setPen(Qt::NoPen);
			p.setRenderHint(QPainter::Antialiasing);
			int32 fontSize;
			if (size == 16) {
				fontSize = (cntSize < 2) ? 11 : ((cntSize < 3) ? 11 : 8);
			} else if (size == 20) {
				fontSize = (cntSize < 2) ? 14 : ((cntSize < 3) ? 13 : 10);
			} else if (size == 24) {
				fontSize = (cntSize < 2) ? 17 : ((cntSize < 3) ? 16 : 12);
			} else {
				fontSize = (cntSize < 2) ? 22 : ((cntSize < 3) ? 20 : 16);
			}
			style::font f = { fontSize, 0, 0 };
			int32 w = f->width(cnt), d, r;
			if (size == 16) {
				d = (cntSize < 2) ? 5 : ((cntSize < 3) ? 2 : 1);
				r = (cntSize < 2) ? 8 : ((cntSize < 3) ? 7 : 3);
			} else if (size == 20) {
				d = (cntSize < 2) ? 6 : ((cntSize < 3) ? 2 : 1);
				r = (cntSize < 2) ? 10 : ((cntSize < 3) ? 9 : 5);
			} else if (size == 24) {
				d = (cntSize < 2) ? 7 : ((cntSize < 3) ? 3 : 1);
				r = (cntSize < 2) ? 12 : ((cntSize < 3) ? 11 : 6);
			} else {
				d = (cntSize < 2) ? 9 : ((cntSize < 3) ? 4 : 2);
				r = (cntSize < 2) ? 16 : ((cntSize < 3) ? 14 : 8);
			}
			p.drawRoundedRect(QRect(size - w - d * 2, size - f->height, w + d * 2, f->height), r, r);
			p.setFont(f);

			p.setPen(fg);

			p.drawText(size - w - d, size - f->height + f->ascent, cnt);
		}
		return result;
	} else {
		if (size != 16 && size != 32) size = 64;
	}

	QImage img(smallIcon ? ((size == 16) ? iconbig16 : (size == 32 ? iconbig32 : iconbig64)) : ((size == 16) ? icon16 : (size == 32 ? icon32 : icon64)));
	if (account().sessionExists() && account().session().supportMode()) {
		Window::ConvertIconToBlack(img);
	}
	if (!count) return img;

	if (smallIcon) {
		placeSmallCounter(img, size, count, bg, QPoint(), fg);
	} else {
		QPainter p(&img);
		p.drawPixmap(size / 2, size / 2, App::pixmapFromImageInPlace(iconWithCounter(-size / 2, count, bg, fg, false)));
	}
	return img;
}

void MainWindow::sendPaths() {
	if (Core::App().locked()) {
		return;
	}
	Core::App().hideMediaView();
	Ui::hideSettingsAndLayer(anim::type::instant);
	if (_main) {
		_main->activate();
	}
}

void MainWindow::updateIsActiveHook() {
	if (_main) _main->updateOnline();
}

MainWindow::~MainWindow() {
	if (_clearManager) {
		_clearManager->stop();
		_clearManager = nullptr;
	}
	delete trayIcon;
	delete trayIconMenu;
}
