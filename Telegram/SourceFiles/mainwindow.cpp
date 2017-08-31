/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "mainwindow.h"

#include "dialogs/dialogs_layout.h"
#include "styles/style_dialogs.h"
#include "styles/style_window.h"
#include "styles/style_boxes.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/buttons.h"
#include "base/zlib_help.h"
#include "lang/lang_cloud_manager.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "shortcuts.h"
#include "messenger.h"
#include "application.h"
#include "platform/platform_specific.h"
#include "passcodewidget.h"
#include "intro/introwidget.h"
#include "mainwidget.h"
#include "layerwidget.h"
#include "boxes/confirm_box.h"
#include "boxes/add_contact_box.h"
#include "boxes/connection_box.h"
#include "observer_peer.h"
#include "autoupdater.h"
#include "mediaview.h"
#include "storage/localstorage.h"
#include "apiwrap.h"
#include "settings/settings_widget.h"
#include "platform/platform_notifications_manager.h"
#include "window/notifications_manager.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_theme_warning.h"
#include "window/window_main_menu.h"
#include "base/task_queue.h"
#include "auth_session.h"
#include "window/window_controller.h"

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

ConnectingWidget::ConnectingWidget(QWidget *parent, const QString &text, const QString &reconnect) : TWidget(parent)
, _reconnect(this, QString()) {
	set(text, reconnect);
	connect(_reconnect, SIGNAL(clicked()), this, SLOT(onReconnect()));
}

void ConnectingWidget::set(const QString &text, const QString &reconnect) {
	_text = text;
	_textWidth = st::linkFont->width(_text) + st::linkFont->spacew;
	int32 _reconnectWidth = 0;
	if (reconnect.isEmpty()) {
		_reconnect->hide();
	} else {
		_reconnect->setText(reconnect);
		_reconnect->show();
		_reconnect->move(st::connectingPadding.left() + _textWidth, st::boxRoundShadow.extend.top() + st::connectingPadding.top());
		_reconnectWidth = _reconnect->width();
	}
	resize(st::connectingPadding.left() + _textWidth + _reconnectWidth + st::connectingPadding.right() + st::boxRoundShadow.extend.right(), st::boxRoundShadow.extend.top() + st::connectingPadding.top() + st::normalFont->height + st::connectingPadding.bottom());
	update();
}

void ConnectingWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto sides = RectPart::Top | RectPart::Right;
	Ui::Shadow::paint(p, QRect(0, st::boxRoundShadow.extend.top(), width() - st::boxRoundShadow.extend.right(), height() - st::boxRoundShadow.extend.top()), width(), st::boxRoundShadow, sides);
	auto parts = RectPart::Top | RectPart::TopRight | RectPart::Center | RectPart::Right;
	App::roundRect(p, QRect(-st::boxRadius, st::boxRoundShadow.extend.top(), width() - st::boxRoundShadow.extend.right() + st::boxRadius, height() - st::boxRoundShadow.extend.top() + st::boxRadius), st::boxBg, BoxCorners, nullptr, parts);

	p.setFont(st::normalFont);
	p.setPen(st::windowSubTextFg);
	p.drawText(st::connectingPadding.left(), st::boxRoundShadow.extend.top() + st::connectingPadding.top() + st::normalFont->ascent, _text);
}

void ConnectingWidget::onReconnect() {
	auto throughProxy = (Global::ConnectionType() != dbictAuto);
	if (throughProxy) {
		Ui::show(Box<ConnectionBox>());
	} else {
		MTP::restart();
	}
}

MainWindow::MainWindow() {
	auto logo = Messenger::Instance().logo();
	icon16 = logo.scaledToWidth(16, Qt::SmoothTransformation);
	icon32 = logo.scaledToWidth(32, Qt::SmoothTransformation);
	icon64 = logo.scaledToWidth(64, Qt::SmoothTransformation);

	auto logoNoMargin = Messenger::Instance().logoNoMargin();
	iconbig16 = logoNoMargin.scaledToWidth(16, Qt::SmoothTransformation);
	iconbig32 = logoNoMargin.scaledToWidth(32, Qt::SmoothTransformation);
	iconbig64 = logoNoMargin.scaledToWidth(64, Qt::SmoothTransformation);

	resize(st::windowDefaultWidth, st::windowDefaultHeight);

	setLocale(QLocale(QLocale::English, QLocale::UnitedStates));

	subscribe(Global::RefSelfChanged(), [this] { updateGlobalMenu(); });
	subscribe(Window::Theme::Background(), [this](const Window::Theme::BackgroundUpdate &data) {
		themeUpdated(data);
	});
	subscribe(Messenger::Instance().passcodedChanged(), [this] { updateGlobalMenu(); });

	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
}

void MainWindow::initHook() {
	Platform::MainWindow::initHook();

	QCoreApplication::instance()->installEventFilter(this);
	connect(windowHandle(), &QWindow::activeChanged, this, [this] { checkHistoryActivation(); }, Qt::QueuedConnection);
}

void MainWindow::firstShow() {
#ifdef Q_OS_WIN
	trayIconMenu = new Ui::PopupMenu(nullptr);
	trayIconMenu->deleteOnHide(false);
#else // Q_OS_WIN
	trayIconMenu = new QMenu(this);
#endif // else for Q_OS_WIN

	auto isLinux = (cPlatform() == dbipLinux32 || cPlatform() == dbipLinux64);
	auto notificationActionText = lang(Global::DesktopNotify()
		? lng_disable_notifications_from_tray
		: lng_enable_notifications_from_tray);

	if (isLinux) {
		trayIconMenu->addAction(lang(lng_open_from_tray), this, SLOT(showFromTray()))->setEnabled(true);
		trayIconMenu->addAction(lang(lng_minimize_to_tray), this, SLOT(minimizeToTray()))->setEnabled(true);
		trayIconMenu->addAction(notificationActionText, this, SLOT(toggleDisplayNotifyFromTray()))->setEnabled(true);
		trayIconMenu->addAction(lang(lng_quit_from_tray), this, SLOT(quitFromTray()))->setEnabled(true);
	} else {
		trayIconMenu->addAction(lang(lng_minimize_to_tray), this, SLOT(minimizeToTray()))->setEnabled(true);
		trayIconMenu->addAction(notificationActionText, this, SLOT(toggleDisplayNotifyFromTray()))->setEnabled(true);
		trayIconMenu->addAction(lang(lng_quit_from_tray), this, SLOT(quitFromTray()))->setEnabled(true);
	}
	Global::RefWorkMode().setForced(Global::WorkMode().value(), true);

	psFirstShow();
	updateTrayMenu();
}

void MainWindow::clearWidgetsHook() {
	auto wasMain = (_main != nullptr);
	_passcode.destroyDelayed();
	_main.destroy();
	_intro.destroy();
	if (wasMain) {
		App::clearHistories();
	}
}

QPixmap MainWindow::grabInner() {
	QPixmap result;
	if (_intro) {
		result = myGrab(_intro);
	} else if (_passcode) {
		result = myGrab(_passcode);
	} else if (_main) {
		result = myGrab(_main);
	}
	return result;
}

void MainWindow::clearPasscode() {
	if (!_passcode) return;

	auto bg = grabInner();

	_passcode.destroy();
	if (_intro) {
		_intro->showAnimated(bg, true);
	} else {
		Assert(_main != nullptr);
		_main->showAnimated(bg, true);
		Messenger::Instance().checkStartUrl();
	}
}

void MainWindow::setupPasscode() {
	auto animated = (_main || _intro);
	auto bg = animated ? grabInner() : QPixmap();
	_passcode.create(bodyWidget());
	updateControlsGeometry();

	if (_main) _main->hide();
	Messenger::Instance().hideMediaView();
	Ui::hideSettingsAndLayer(true);
	if (_intro) _intro->hide();
	if (animated) {
		_passcode->showAnimated(bg);
	} else {
		setInnerFocus();
	}
}

void MainWindow::setupIntro() {
	if (_intro && !_intro->isHidden() && !_main) return;

	Ui::hideSettingsAndLayer(true);

	auto animated = (_main || _passcode);
	auto bg = animated ? grabInner() : QPixmap();

	clearWidgets();
	_intro.create(bodyWidget());
	updateControlsGeometry();

	if (animated) {
		_intro->showAnimated(bg);
	} else {
		setInnerFocus();
	}

	fixOrder();

	updateConnectingStatus();

	_delayedServiceMsgs.clear();
	if (_serviceHistoryRequest) {
		MTP::cancel(_serviceHistoryRequest);
		_serviceHistoryRequest = 0;
	}
}

void MainWindow::serviceNotification(const TextWithEntities &message, const MTPMessageMedia &media, int32 date, bool force) {
	if (date <= 0) date = unixtime();
	auto h = (_main && App::userLoaded(ServiceUserId)) ? App::history(ServiceUserId).get() : nullptr;
	if (!h || (!force && h->isEmpty())) {
		_delayedServiceMsgs.push_back(DelayedServiceMsg(message, media, date));
		return sendServiceHistoryRequest();
	}

	_main->insertCheckedServiceNotification(message, media, date);
}

void MainWindow::showDelayedServiceMsgs() {
	for (auto &delayed : base::take(_delayedServiceMsgs)) {
		serviceNotification(delayed.message, delayed.media, delayed.date, true);
	}
}

void MainWindow::sendServiceHistoryRequest() {
	if (!_main || !_main->started() || _delayedServiceMsgs.isEmpty() || _serviceHistoryRequest) return;

	UserData *user = App::userLoaded(ServiceUserId);
	if (!user) {
		auto userFlags = MTPDuser::Flag::f_first_name | MTPDuser::Flag::f_phone | MTPDuser::Flag::f_status | MTPDuser::Flag::f_verified;
		user = App::feedUsers(MTP_vector<MTPUser>(1, MTP_user(MTP_flags(userFlags), MTP_int(ServiceUserId), MTPlong(), MTP_string("Telegram"), MTPstring(), MTPstring(), MTP_string("42777"), MTP_userProfilePhotoEmpty(), MTP_userStatusRecently(), MTPint(), MTPstring(), MTPstring(), MTPstring())));
	}
	_serviceHistoryRequest = MTP::send(MTPmessages_GetHistory(user->input, MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(1), MTP_int(0), MTP_int(0)), _main->rpcDone(&MainWidget::serviceHistoryDone), _main->rpcFail(&MainWidget::serviceHistoryFail));
}

void MainWindow::setupMain(const MTPUser *self) {
	auto animated = (_intro || _passcode);
	auto bg = animated ? grabInner() : QPixmap();

	clearWidgets();

	Assert(AuthSession::Exists());

	_main.create(bodyWidget(), controller());
	_main->show();
	updateControlsGeometry();

	if (animated) {
		_main->showAnimated(bg);
	} else {
		_main->activate();
	}
	_main->start(self);

	fixOrder();

	updateConnectingStatus();
}

void MainWindow::showSettings() {
	if (isHidden()) showFromTray();

	showSpecialLayer(Box<Settings::Widget>());
}

void MainWindow::showSpecialLayer(object_ptr<LayerWidget> layer) {
	if (_passcode) return;

	ensureLayerCreated();
	_layerBg->showSpecialLayer(std::move(layer));
}

void MainWindow::showMainMenu() {
	if (_passcode) return;

	if (isHidden()) showFromTray();

	ensureLayerCreated();
	_layerBg->showMainMenu();
}

void MainWindow::ensureLayerCreated() {
	if (!_layerBg) {
		_layerBg.create(bodyWidget(), controller());
		if (controller()) {
			controller()->enableGifPauseReason(Window::GifPauseReason::Layer);
		}
	}
}

void MainWindow::destroyLayerDelayed() {
	if (_layerBg) {
		_layerBg.destroyDelayed();
		if (controller()) {
			controller()->disableGifPauseReason(Window::GifPauseReason::Layer);
		}
	}
}

void MainWindow::ui_hideSettingsAndLayer(ShowLayerOptions options) {
	if (_layerBg) {
		_layerBg->hideAll();
		if (options & ForceFastShowLayer) {
			destroyLayerDelayed();
		}
	}
}

void MainWindow::mtpStateChanged(int32 dc, int32 state) {
	if (dc == MTP::maindc()) {
		updateConnectingStatus();
		Global::RefConnectionTypeChanged().notify();
	}
}

void MainWindow::updateConnectingStatus() {
	auto state = MTP::dcstate();
	auto throughProxy = (Global::ConnectionType() != dbictAuto);
	if (state == MTP::ConnectingState || state == MTP::DisconnectedState || (state < 0 && state > -600)) {
		if (_main || getms() > 5000 || _connecting) {
			showConnecting(lang(throughProxy ? lng_connecting_to_proxy : lng_connecting), throughProxy ? lang(lng_connecting_settings) : QString());
		}
	} else if (state < 0) {
		showConnecting(lng_reconnecting(lt_count, ((-state) / 1000) + 1), lang(throughProxy ? lng_connecting_settings : lng_reconnecting_try_now));
		QTimer::singleShot((-state) % 1000, this, SLOT(updateConnectingStatus()));
	} else {
		hideConnecting();
	}
}

MainWidget *MainWindow::mainWidget() {
	return _main;
}

PasscodeWidget *MainWindow::passcodeWidget() {
	return _passcode;
}

void MainWindow::ui_showBox(object_ptr<BoxContent> box, ShowLayerOptions options) {
	if (box) {
		ensureLayerCreated();
		if (options & KeepOtherLayers) {
			if (options & ShowAfterOtherLayers) {
				_layerBg->prependBox(std::move(box));
			} else {
				_layerBg->appendBox(std::move(box));
			}
		} else {
			_layerBg->showBox(std::move(box));
		}
		if (options & ForceFastShowLayer) {
			_layerBg->finishAnimation();
		}
	} else {
		if (_layerBg) {
			_layerBg->hideTopLayer();
			if ((options & ForceFastShowLayer) && !_layerBg->layerShown()) {
				destroyLayerDelayed();
			}
		}
		Messenger::Instance().hideMediaView();
	}
}

bool MainWindow::ui_isLayerShown() {
	return _layerBg != nullptr;
}

void MainWindow::ui_showMediaPreview(DocumentData *document) {
	if (!document || ((!document->isAnimation() || !document->loaded()) && !document->sticker())) {
		return;
	}
	if (!_mediaPreview) {
		_mediaPreview.create(bodyWidget(), controller());
		updateControlsGeometry();
	}
	if (_mediaPreview->isHidden()) {
		fixOrder();
	}
	_mediaPreview->showPreview(document);
}

void MainWindow::ui_showMediaPreview(PhotoData *photo) {
	if (!photo) return;
	if (!_mediaPreview) {
		_mediaPreview.create(bodyWidget(), controller());
		updateControlsGeometry();
	}
	if (_mediaPreview->isHidden()) {
		fixOrder();
	}
	_mediaPreview->showPreview(photo);
}

void MainWindow::ui_hideMediaPreview() {
	if (!_mediaPreview) return;
	_mediaPreview->hidePreview();
}

void MainWindow::showConnecting(const QString &text, const QString &reconnect) {
	if (_connecting) {
		_connecting->set(text, reconnect);
	} else {
		_connecting.create(bodyWidget(), text, reconnect);
		_connecting->show();
		updateControlsGeometry();
		fixOrder();
	}
}

void MainWindow::hideConnecting() {
	if (_connecting) {
		_connecting.destroyDelayed();
	}
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

		base::TaskQueue::Main().Put(base::lambda_guarded(this, [this] {
			if (_testingThemeWarning) {
				_testingThemeWarning->showAnimated();
			}
		}));
	} else if (data.type == Type::RevertingTheme || data.type == Type::ApplyingTheme) {
		if (_testingThemeWarning) {
			if (_testingThemeWarning->isHidden()) {
				_testingThemeWarning.destroy();
			} else {
				base::TaskQueue::Main().Put(base::lambda_guarded(this, [this] {
					if (_testingThemeWarning) {
						_testingThemeWarning->hideAnimated();
						_testingThemeWarning = nullptr;
					}
					setInnerFocus();
				}));
			}
		}
	}
}

bool MainWindow::doWeReadServerHistory() {
	updateIsActive(0);
	return isActive() && _main && !Ui::isLayerShown() && _main->doWeReadServerHistory();
}

bool MainWindow::doWeReadMentions() {
	updateIsActive(0);
	return isActive() && _main && !Ui::isLayerShown() && _main->doWeReadMentions();
}

void MainWindow::checkHistoryActivation() {
	if (_main && doWeReadServerHistory()) {
		_main->markActiveHistoryAsRead();
	}
}

bool MainWindow::contentOverlapped(const QRect &globalRect) {
	if (_main && _main->contentOverlapped(globalRect)) return true;
	if (_layerBg && _layerBg->contentOverlapped(globalRect)) return true;
	return false;
}

void MainWindow::setInnerFocus() {
	if (_testingThemeWarning) {
		_testingThemeWarning->setFocus();
	} else if (_layerBg && _layerBg->canSetFocus()) {
		_layerBg->setInnerFocus();
	} else if (_passcode) {
		_passcode->setInnerFocus();
	} else if (_main) {
		_main->setInnerFocus();
	} else if (_intro) {
		_intro->setInnerFocus();
	}
}

bool MainWindow::eventFilter(QObject *object, QEvent *e) {
	switch (e->type()) {
	case QEvent::KeyPress: {
		if (cDebug() && e->type() == QEvent::KeyPress && object == windowHandle()) {
			auto key = static_cast<QKeyEvent*>(e)->key();
			FeedLangTestingKey(key);
		}
	} break;

	case QEvent::MouseMove: {
		if (_main && _main->isIdle()) {
			psUserActionDone();
			_main->checkIdleFinish();
		}
	} break;

	case QEvent::MouseButtonRelease: {
		Ui::hideMediaPreview();
	} break;

	case QEvent::ApplicationActivate: {
		if (object == QCoreApplication::instance()) {
			InvokeQueued(this, [this] {
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
    if (!trayIconMenu || (cPlatform() == dbipWindows && !force)) return;

	auto iconMenu = trayIconMenu;
	auto actions = iconMenu->actions();
	auto isLinux = (cPlatform() == dbipLinux32 || cPlatform() == dbipLinux64);
	if (isLinux) {
		auto minimizeAction = actions.at(1);
		minimizeAction->setDisabled(!isVisible());
	} else {
		updateIsActive(0);
		auto active = isActive();
		auto toggleAction = actions.at(0);
		disconnect(toggleAction, SIGNAL(triggered(bool)), this, SLOT(minimizeToTray()));
		disconnect(toggleAction, SIGNAL(triggered(bool)), this, SLOT(showFromTray()));
		connect(toggleAction, SIGNAL(triggered(bool)), this, active ? SLOT(minimizeToTray()) : SLOT(showFromTray()));
		toggleAction->setText(lang(active ? lng_minimize_to_tray : lng_open_from_tray));

		// On macOS just remove trayIcon menu if the window is not active.
		// So we will activate the window on click instead of showing the menu.
		if (!active && (cPlatform() == dbipMac || cPlatform() == dbipMacOld)) {
			iconMenu = nullptr;
		}
	}
	auto notificationAction = actions.at(isLinux ? 2 : 1);
	auto notificationActionText = lang(Global::DesktopNotify()
		? lng_disable_notifications_from_tray
		: lng_enable_notifications_from_tray);
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

	if (App::self()) {
		Ui::show(Box<AddContactBox>(), KeepOtherLayers);
	}
}

void MainWindow::onShowNewGroup() {
	if (isHidden()) showFromTray();

	if (App::self()) {
		Ui::show(Box<GroupInfoBox>(CreatingGroupGroup, false), KeepOtherLayers);
	}
}

void MainWindow::onShowNewChannel() {
	if (isHidden()) showFromTray();

	if (_main) Ui::show(Box<GroupInfoBox>(CreatingGroupChannel, false), KeepOtherLayers);
}

void MainWindow::onLogout() {
	if (isHidden()) showFromTray();

	Ui::show(Box<ConfirmBox>(lang(lng_sure_logout), lang(lng_settings_logout), st::attentionBoxButton, [] {
		App::logOut();
	}));
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

void MainWindow::noLayerStack(LayerStackWidget *was) {
	if (was == _layerBg) {
		_layerBg = nullptr;
		if (controller()) {
			controller()->disableGifPauseReason(Window::GifPauseReason::Layer);
		}
	}
}

void MainWindow::layerFinishedHide(LayerStackWidget *was) {
	if (was == _layerBg) {
		auto resetFocus = (was == App::wnd()->focusWidget());
		destroyLayerDelayed();
		InvokeQueued(this, [this, resetFocus] {
			if (resetFocus) setInnerFocus();
			checkHistoryActivation();
		});
	}
}

void MainWindow::fixOrder() {
	if (_layerBg) _layerBg->raise();
	if (_mediaPreview) _mediaPreview->raise();
	if (_connecting) _connecting->raise();
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

void MainWindow::toggleTray(QSystemTrayIcon::ActivationReason reason) {
	updateIsActive(0);
	if ((cPlatform() == dbipMac || cPlatform() == dbipMacOld) && isActive()) return;
	if (reason == QSystemTrayIcon::Context) {
		updateTrayMenu(true);
		QTimer::singleShot(1, this, SLOT(psShowTrayMenu()));
	} else {
		if (isActive()) {
			minimizeToTray();
		} else {
			showFromTray(reason);
		}
	}
}

void MainWindow::toggleDisplayNotifyFromTray() {
	if (App::passcoded()) {
		if (!isActive()) showFromTray();
		Ui::show(Box<InformBox>(lang(lng_passcode_need_unblock)));
		return;
	}
	if (!AuthSession::Exists()) {
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
	Auth().notifications().settingsChanged().notify(Window::Notifications::ChangeType::DesktopEnabled);
	if (soundNotifyChanged) {
		Auth().notifications().settingsChanged().notify(Window::Notifications::ChangeType::SoundEnabled);
	}
}

void MainWindow::closeEvent(QCloseEvent *e) {
	if (Sandbox::isSavingSession()) {
		e->accept();
		App::quit();
	} else {
		e->ignore();
		if (!AuthSession::Exists() || !hideNoQuit()) {
			App::quit();
		}
	}
}

void MainWindow::updateControlsGeometry() {
	Platform::MainWindow::updateControlsGeometry();

	auto body = bodyWidget()->rect();
	if (_passcode) _passcode->setGeometry(body);
	if (_main) _main->setGeometry(body);
	if (_intro) _intro->setGeometry(body);
	if (_layerBg) _layerBg->setGeometry(body);
	if (_mediaPreview) _mediaPreview->setGeometry(body);
	if (_connecting) _connecting->moveToLeft(0, body.height() - _connecting->height());
	if (_testingThemeWarning) _testingThemeWarning->setGeometry(body);
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
	return (Local::hasImages() || Local::hasStickers() || Local::hasWebFiles() || Local::hasAudios()) ? TempDirExists : TempDirEmpty;
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

void MainWindow::app_activateClickHandler(ClickHandlerPtr handler, Qt::MouseButton button) {
	handler->onClick(button);
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
	if (App::passcoded()) return;
	Messenger::Instance().hideMediaView();
	Ui::hideSettingsAndLayer(true);
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

PreLaunchWindow *PreLaunchWindowInstance = nullptr;

PreLaunchWindow::PreLaunchWindow(QString title) {
	Fonts::Start();

	auto icon = Window::CreateIcon();
	setWindowIcon(icon);
	setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);

	setWindowTitle(title.isEmpty() ? qsl("Telegram") : title);

	QPalette p(palette());
	p.setColor(QPalette::Background, QColor(255, 255, 255));
	setPalette(p);

	QLabel tmp(this);
	tmp.setText(qsl("Tmp"));
	_size = tmp.sizeHint().height();

	int paddingVertical = (_size / 2);
	int paddingHorizontal = _size;
	int borderRadius = (_size / 5);
	setStyleSheet(qsl("QPushButton { padding: %1px %2px; background-color: #ffffff; border-radius: %3px; }\nQPushButton#confirm:hover, QPushButton#cancel:hover { background-color: #e3f1fa; color: #2f9fea; }\nQPushButton#confirm { color: #2f9fea; }\nQPushButton#cancel { color: #aeaeae; }\nQLineEdit { border: 1px solid #e0e0e0; padding: 5px; }\nQLineEdit:focus { border: 2px solid #37a1de; padding: 4px; }").arg(paddingVertical).arg(paddingHorizontal).arg(borderRadius));
	if (!PreLaunchWindowInstance) {
		PreLaunchWindowInstance = this;
	}
}

void PreLaunchWindow::activate() {
	setWindowState(windowState() & ~Qt::WindowMinimized);
	setVisible(true);
	psActivateProcess();
	activateWindow();
}

PreLaunchWindow *PreLaunchWindow::instance() {
	return PreLaunchWindowInstance;
}

PreLaunchWindow::~PreLaunchWindow() {
	if (PreLaunchWindowInstance == this) {
		PreLaunchWindowInstance = nullptr;
	}
}

PreLaunchLabel::PreLaunchLabel(QWidget *parent) : QLabel(parent) {
	QFont labelFont(font());
	labelFont.setFamily(Fonts::GetOverride(qsl("Open Sans Semibold")));
	labelFont.setPixelSize(static_cast<PreLaunchWindow*>(parent)->basicSize());
	setFont(labelFont);

	QPalette p(palette());
	p.setColor(QPalette::Foreground, QColor(0, 0, 0));
	setPalette(p);
	show();
};

void PreLaunchLabel::setText(const QString &text) {
	QLabel::setText(text);
	updateGeometry();
	resize(sizeHint());
}

PreLaunchInput::PreLaunchInput(QWidget *parent, bool password) : QLineEdit(parent) {
	QFont logFont(font());
	logFont.setFamily(Fonts::GetOverride(qsl("Open Sans")));
	logFont.setPixelSize(static_cast<PreLaunchWindow*>(parent)->basicSize());
	setFont(logFont);

	QPalette p(palette());
	p.setColor(QPalette::Foreground, QColor(0, 0, 0));
	setPalette(p);

	QLineEdit::setTextMargins(0, 0, 0, 0);
	setContentsMargins(0, 0, 0, 0);
	if (password) {
		setEchoMode(QLineEdit::Password);
	}
	show();
};

PreLaunchLog::PreLaunchLog(QWidget *parent) : QTextEdit(parent) {
	QFont logFont(font());
	logFont.setFamily(Fonts::GetOverride(qsl("Open Sans")));
	logFont.setPixelSize(static_cast<PreLaunchWindow*>(parent)->basicSize());
	setFont(logFont);

	QPalette p(palette());
	p.setColor(QPalette::Foreground, QColor(96, 96, 96));
	setPalette(p);

	setReadOnly(true);
	setFrameStyle(QFrame::NoFrame | QFrame::Plain);
	viewport()->setAutoFillBackground(false);
	setContentsMargins(0, 0, 0, 0);
	document()->setDocumentMargin(0);
	show();
};

PreLaunchButton::PreLaunchButton(QWidget *parent, bool confirm) : QPushButton(parent) {
	setFlat(true);

	setObjectName(confirm ? "confirm" : "cancel");

	QFont closeFont(font());
	closeFont.setFamily(Fonts::GetOverride(qsl("Open Sans Semibold")));
	closeFont.setPixelSize(static_cast<PreLaunchWindow*>(parent)->basicSize());
	setFont(closeFont);

	setCursor(Qt::PointingHandCursor);
	show();
};

void PreLaunchButton::setText(const QString &text) {
	QPushButton::setText(text);
	updateGeometry();
	resize(sizeHint());
}

PreLaunchCheckbox::PreLaunchCheckbox(QWidget *parent) : QCheckBox(parent) {
	setTristate(false);
	setCheckState(Qt::Checked);

	QFont closeFont(font());
	closeFont.setFamily(Fonts::GetOverride(qsl("Open Sans Semibold")));
	closeFont.setPixelSize(static_cast<PreLaunchWindow*>(parent)->basicSize());
	setFont(closeFont);

	setCursor(Qt::PointingHandCursor);
	show();
};

void PreLaunchCheckbox::setText(const QString &text) {
	QCheckBox::setText(text);
	updateGeometry();
	resize(sizeHint());
}

NotStartedWindow::NotStartedWindow()
: _label(this)
, _log(this)
, _close(this) {
	_label.setText(qsl("Could not start Telegram Desktop!\nYou can see complete log below:"));

	_log.setPlainText(Logs::full());

	connect(&_close, SIGNAL(clicked()), this, SLOT(close()));
	_close.setText(qsl("CLOSE"));

	QRect scr(QApplication::primaryScreen()->availableGeometry());
	move(scr.x() + (scr.width() / 6), scr.y() + (scr.height() / 6));
	updateControls();
	show();
}

void NotStartedWindow::updateControls() {
	_label.show();
	_log.show();
	_close.show();

	QRect scr(QApplication::primaryScreen()->availableGeometry());
	QSize s(scr.width() / 2, scr.height() / 2);
	if (s == size()) {
		resizeEvent(0);
	} else {
		resize(s);
	}
}

void NotStartedWindow::closeEvent(QCloseEvent *e) {
	deleteLater();
}

void NotStartedWindow::resizeEvent(QResizeEvent *e) {
	int padding = _size;
	_label.setGeometry(padding, padding, width() - 2 * padding, _label.sizeHint().height());
	_log.setGeometry(padding, padding * 2 + _label.sizeHint().height(), width() - 2 * padding, height() - 4 * padding - _label.height() - _close.height());
	_close.setGeometry(width() - padding - _close.width(), height() - padding - _close.height(), _close.width(), _close.height());
}

LastCrashedWindow::LastCrashedWindow()
: _port(80)
, _label(this)
, _pleaseSendReport(this)
, _yourReportName(this)
, _minidump(this)
, _report(this)
, _send(this)
, _sendSkip(this, false)
, _networkSettings(this)
, _continue(this)
, _showReport(this)
, _saveReport(this)
, _getApp(this)
, _includeUsername(this)
, _reportText(QString::fromUtf8(Sandbox::LastCrashDump()))
, _reportShown(false)
, _reportSaved(false)
, _sendingState(Sandbox::LastCrashDump().isEmpty() ? SendingNoReport : SendingUpdateCheck)
, _updating(this)
, _sendingProgress(0)
, _sendingTotal(0)
, _checkReply(0)
, _sendReply(0)
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
, _updatingCheck(this)
, _updatingSkip(this, false)
#endif // !TDESKTOP_DISABLE_AUTOUPDATE
{
	excludeReportUsername();

	if (!cAlphaVersion() && !cBetaVersion()) { // currently accept crash reports only from testers
		_sendingState = SendingNoReport;
	}
	if (_sendingState != SendingNoReport) {
		qint64 dumpsize = 0;
		QString dumpspath = cWorkingDir() + qsl("tdata/dumps");
#if defined Q_OS_MAC && !defined MAC_USE_BREAKPAD
		dumpspath += qsl("/completed");
#endif
		QString possibleDump = getReportField(qstr("minidump"), qstr("Minidump:"));
		if (!possibleDump.isEmpty()) {
			if (!possibleDump.startsWith('/')) {
				possibleDump = dumpspath + '/' + possibleDump;
			}
			if (!possibleDump.endsWith(qstr(".dmp"))) {
				possibleDump += qsl(".dmp");
			}
			QFileInfo possibleInfo(possibleDump);
			if (possibleInfo.exists()) {
				_minidumpName = possibleInfo.fileName();
				_minidumpFull = possibleInfo.absoluteFilePath();
				dumpsize = possibleInfo.size();
			}
		}
		if (_minidumpFull.isEmpty()) {
			QString maxDump, maxDumpFull;
            QDateTime maxDumpModified, workingModified = QFileInfo(cWorkingDir() + qsl("tdata/working")).lastModified();
			QFileInfoList list = QDir(dumpspath).entryInfoList();
            for (int32 i = 0, l = list.size(); i < l; ++i) {
                QString name = list.at(i).fileName();
                if (name.endsWith(qstr(".dmp"))) {
                    QDateTime modified = list.at(i).lastModified();
                    if (maxDump.isEmpty() || qAbs(workingModified.secsTo(modified)) < qAbs(workingModified.secsTo(maxDumpModified))) {
                        maxDump = name;
                        maxDumpModified = modified;
                        maxDumpFull = list.at(i).absoluteFilePath();
                        dumpsize = list.at(i).size();
                    }
                }
            }
            if (!maxDump.isEmpty() && qAbs(workingModified.secsTo(maxDumpModified)) < 10) {
                _minidumpName = maxDump;
                _minidumpFull = maxDumpFull;
            }
        }
		if (_minidumpName.isEmpty()) { // currently don't accept crash reports without dumps from google libraries
			_sendingState = SendingNoReport;
		} else {
			_minidump.setText(qsl("+ %1 (%2 KB)").arg(_minidumpName).arg(dumpsize / 1024));
		}
	}
	if (_sendingState != SendingNoReport) {
		QString version = getReportField(qstr("version"), qstr("Version:"));
		QString current = cBetaVersion() ? qsl("-%1").arg(cBetaVersion()) : QString::number(AppVersion);
		if (version != current) { // currently don't accept crash reports from not current app version
			_sendingState = SendingNoReport;
		}
	}

	_networkSettings.setText(qsl("NETWORK SETTINGS"));
	connect(&_networkSettings, SIGNAL(clicked()), this, SLOT(onNetworkSettings()));

	if (_sendingState == SendingNoReport) {
		_label.setText(qsl("Last time Telegram Desktop was not closed properly."));
	} else {
		_label.setText(qsl("Last time Telegram Desktop crashed :("));
	}

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	_updatingCheck.setText(qsl("TRY AGAIN"));
	connect(&_updatingCheck, SIGNAL(clicked()), this, SLOT(onUpdateRetry()));
	_updatingSkip.setText(qsl("SKIP"));
	connect(&_updatingSkip, SIGNAL(clicked()), this, SLOT(onUpdateSkip()));

	Sandbox::connect(SIGNAL(updateChecking()), this, SLOT(onUpdateChecking()));
	Sandbox::connect(SIGNAL(updateLatest()), this, SLOT(onUpdateLatest()));
	Sandbox::connect(SIGNAL(updateProgress(qint64,qint64)), this, SLOT(onUpdateDownloading(qint64,qint64)));
	Sandbox::connect(SIGNAL(updateFailed()), this, SLOT(onUpdateFailed()));
	Sandbox::connect(SIGNAL(updateReady()), this, SLOT(onUpdateReady()));

	switch (Sandbox::updatingState()) {
	case Application::UpdatingDownload:
		setUpdatingState(UpdatingDownload, true);
		setDownloadProgress(Sandbox::updatingReady(), Sandbox::updatingSize());
	break;
	case Application::UpdatingReady: setUpdatingState(UpdatingReady, true); break;
	default: setUpdatingState(UpdatingCheck, true); break;
	}

	cSetLastUpdateCheck(0);
	Sandbox::startUpdateCheck();
#else // !TDESKTOP_DISABLE_AUTOUPDATE
	_updating.setText(qsl("Please check if there is a new version available."));
	if (_sendingState != SendingNoReport) {
		_sendingState = SendingNone;
	}
#endif // else for !TDESKTOP_DISABLE_AUTOUPDATE

	_pleaseSendReport.setText(qsl("Please send us a crash report."));
	_yourReportName.setText(qsl("Your Report Tag: %1\nYour User Tag: %2").arg(QString(_minidumpName).replace(".dmp", "")).arg(Sandbox::UserTag(), 0, 16));
	_yourReportName.setCursor(style::cur_text);
	_yourReportName.setTextInteractionFlags(Qt::TextSelectableByMouse);

	_includeUsername.setText(qsl("Include username @%1 as your contact info").arg(_reportUsername));

	_report.setPlainText(_reportTextNoUsername);

	_showReport.setText(qsl("VIEW REPORT"));
	connect(&_showReport, SIGNAL(clicked()), this, SLOT(onViewReport()));
	_saveReport.setText(qsl("SAVE TO FILE"));
	connect(&_saveReport, SIGNAL(clicked()), this, SLOT(onSaveReport()));
	_getApp.setText(qsl("GET THE LATEST OFFICIAL VERSION OF TELEGRAM DESKTOP"));
	connect(&_getApp, SIGNAL(clicked()), this, SLOT(onGetApp()));

	_send.setText(qsl("SEND CRASH REPORT"));
	connect(&_send, SIGNAL(clicked()), this, SLOT(onSendReport()));

	_sendSkip.setText(qsl("SKIP"));
	connect(&_sendSkip, SIGNAL(clicked()), this, SLOT(onContinue()));
	_continue.setText(qsl("CONTINUE"));
	connect(&_continue, SIGNAL(clicked()), this, SLOT(onContinue()));

	QRect scr(QApplication::primaryScreen()->availableGeometry());
	move(scr.x() + (scr.width() / 6), scr.y() + (scr.height() / 6));
	updateControls();
	show();
}

void LastCrashedWindow::onViewReport() {
	_reportShown = !_reportShown;
	updateControls();
}

void LastCrashedWindow::onSaveReport() {
	QString to = QFileDialog::getSaveFileName(0, qsl("Telegram Crash Report"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + qsl("/report.telegramcrash"), qsl("Telegram crash report (*.telegramcrash)"));
	if (!to.isEmpty()) {
		QFile file(to);
		if (file.open(QIODevice::WriteOnly)) {
			file.write(getCrashReportRaw());
			_reportSaved = true;
			updateControls();
		}
	}
}

QByteArray LastCrashedWindow::getCrashReportRaw() const {
	QByteArray result(Sandbox::LastCrashDump());
	if (!_reportUsername.isEmpty() && _includeUsername.checkState() != Qt::Checked) {
		result.replace((qsl("Username: ") + _reportUsername).toUtf8(), "Username: _not_included_");
	}
	return result;
}

void LastCrashedWindow::onGetApp() {
	QDesktopServices::openUrl(qsl("https://desktop.telegram.org"));
}

void LastCrashedWindow::excludeReportUsername() {
	QString prefix = qstr("Username:");
	QStringList lines = _reportText.split('\n');
	for (int32 i = 0, l = lines.size(); i < l; ++i) {
		if (lines.at(i).trimmed().startsWith(prefix)) {
			_reportUsername = lines.at(i).trimmed().mid(prefix.size()).trimmed();
			lines.removeAt(i);
			break;
		}
	}
	_reportTextNoUsername = _reportUsername.isEmpty() ? _reportText : lines.join('\n');
}

QString LastCrashedWindow::getReportField(const QLatin1String &name, const QLatin1String &prefix) {
	QStringList lines = _reportText.split('\n');
	for (int32 i = 0, l = lines.size(); i < l; ++i) {
		if (lines.at(i).trimmed().startsWith(prefix)) {
			QString data = lines.at(i).trimmed().mid(prefix.size()).trimmed();

			if (name == qstr("version")) {
				if (data.endsWith(qstr(" beta"))) {
					data = QString::number(-data.replace(QRegularExpression(qsl("[^\\d]")), "").toLongLong());
				} else {
					data = QString::number(data.replace(QRegularExpression(qsl("[^\\d]")), "").toLongLong());
				}
			}

			return data;
		}
	}
	return QString();
}

void LastCrashedWindow::addReportFieldPart(const QLatin1String &name, const QLatin1String &prefix, QHttpMultiPart *multipart) {
	QString data = getReportField(name, prefix);
	if (!data.isEmpty()) {
		QHttpPart reportPart;
		reportPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant(qsl("form-data; name=\"%1\"").arg(name)));
		reportPart.setBody(data.toUtf8());
		multipart->append(reportPart);
	}
}

void LastCrashedWindow::onSendReport() {
	if (_checkReply) {
		_checkReply->deleteLater();
		_checkReply = nullptr;
	}
	if (_sendReply) {
		_sendReply->deleteLater();
		_sendReply = nullptr;
	}
	App::setProxySettings(_sendManager);

	QString apiid = getReportField(qstr("apiid"), qstr("ApiId:")), version = getReportField(qstr("version"), qstr("Version:"));
	_checkReply = _sendManager.get(QNetworkRequest(qsl("https://tdesktop.com/crash.php?act=query_report&apiid=%1&version=%2&dmp=%3&platform=%4").arg(apiid).arg(version).arg(minidumpFileName().isEmpty() ? 0 : 1).arg(cPlatformString())));

	connect(_checkReply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onSendingError(QNetworkReply::NetworkError)));
	connect(_checkReply, SIGNAL(finished()), this, SLOT(onCheckingFinished()));

	_pleaseSendReport.setText(qsl("Sending crash report..."));
	_sendingState = SendingProgress;
	_reportShown = false;
	updateControls();
}

QString LastCrashedWindow::minidumpFileName() {
	QFileInfo dmpFile(_minidumpFull);
	if (dmpFile.exists() && dmpFile.size() > 0 && dmpFile.size() < 20 * 1024 * 1024 &&
		QRegularExpression(qsl("^[a-zA-Z0-9\\-]{1,64}\\.dmp$")).match(dmpFile.fileName()).hasMatch()) {
		return dmpFile.fileName();
	}
	return QString();
}

void LastCrashedWindow::onCheckingFinished() {
	if (!_checkReply || _sendReply) return;

	QByteArray result = _checkReply->readAll().trimmed();
	_checkReply->deleteLater();
	_checkReply = nullptr;

	LOG(("Crash report check for sending done, result: %1").arg(QString::fromUtf8(result)));

	if (result == "Old") {
		_pleaseSendReport.setText(qsl("This report is about some old version of Telegram Desktop."));
		_sendingState = SendingTooOld;
		updateControls();
		return;
	} else if (result == "Unofficial") {
		_pleaseSendReport.setText(qsl("You use some custom version of Telegram Desktop."));
		_sendingState = SendingUnofficial;
		updateControls();
		return;
	} else if (result != "Report") {
		_pleaseSendReport.setText(qsl("Thank you for your report!"));
		_sendingState = SendingDone;
		updateControls();

		SignalHandlers::restart();
		return;
	}

	auto multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

	addReportFieldPart(qstr("platform"), qstr("Platform:"), multipart);
	addReportFieldPart(qstr("version"), qstr("Version:"), multipart);

	QHttpPart reportPart;
	reportPart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/octet-stream"));
	reportPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"report\"; filename=\"report.telegramcrash\""));
	reportPart.setBody(getCrashReportRaw());
	multipart->append(reportPart);

	QString dmpName = minidumpFileName();
	if (!dmpName.isEmpty()) {
		QFile file(_minidumpFull);
		if (file.open(QIODevice::ReadOnly)) {
			QByteArray minidump = file.readAll();
			file.close();

			QString zipName = QString(dmpName).replace(qstr(".dmp"), qstr(".zip"));

			zlib::FileToWrite minidumpZip;

			zip_fileinfo zfi = { { 0, 0, 0, 0, 0, 0 }, 0, 0, 0 };
			QByteArray dmpNameUtf = dmpName.toUtf8();
			minidumpZip.openNewFile(dmpNameUtf.constData(), &zfi, nullptr, 0, nullptr, 0, nullptr, Z_DEFLATED, Z_DEFAULT_COMPRESSION);
			minidumpZip.writeInFile(minidump.constData(), minidump.size());
			minidumpZip.closeFile();
			minidumpZip.close();

			if (minidumpZip.error() == ZIP_OK) {
				QHttpPart dumpPart;
				dumpPart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/octet-stream"));
				dumpPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant(qsl("form-data; name=\"dump\"; filename=\"%1\"").arg(zipName)));
				dumpPart.setBody(minidumpZip.result());
				multipart->append(dumpPart);

				_minidump.setText(qsl("+ %1 (%2 KB)").arg(zipName).arg(minidumpZip.result().size() / 1024));
			}
		}
	}

	_sendReply = _sendManager.post(QNetworkRequest(qsl("https://tdesktop.com/crash.php?act=report")), multipart);
	multipart->setParent(_sendReply);

	connect(_sendReply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(onSendingError(QNetworkReply::NetworkError)));
	connect(_sendReply, SIGNAL(finished()), this, SLOT(onSendingFinished()));
	connect(_sendReply, SIGNAL(uploadProgress(qint64,qint64)), this, SLOT(onSendingProgress(qint64,qint64)));

	updateControls();
}

void LastCrashedWindow::updateControls() {
	int padding = _size, h = padding + _networkSettings.height() + padding;

	_label.show();
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	h += _networkSettings.height() + padding;
	if (_updatingState == UpdatingFail && (_sendingState == SendingNoReport || _sendingState == SendingUpdateCheck)) {
		_networkSettings.show();
		_updatingCheck.show();
		_updatingSkip.show();
		_send.hide();
		_sendSkip.hide();
		_continue.hide();
		_pleaseSendReport.hide();
		_yourReportName.hide();
		_includeUsername.hide();
		_getApp.hide();
		_showReport.hide();
		_report.hide();
		_minidump.hide();
		_saveReport.hide();
		h += padding + _updatingCheck.height() + padding;
	} else {
		if (_updatingState == UpdatingCheck || _sendingState == SendingFail || _sendingState == SendingProgress) {
			_networkSettings.show();
		} else {
			_networkSettings.hide();
		}
		if (_updatingState == UpdatingNone || _updatingState == UpdatingLatest || _updatingState == UpdatingFail) {
			h += padding + _updatingCheck.height() + padding;
			if (_sendingState == SendingNoReport) {
				_pleaseSendReport.hide();
				_yourReportName.hide();
				_includeUsername.hide();
				_getApp.hide();
				_showReport.hide();
				_report.hide();
				_minidump.hide();
				_saveReport.hide();
				_send.hide();
				_sendSkip.hide();
				_continue.show();
			} else {
				h += _showReport.height() + padding + _yourReportName.height() + padding;
				_pleaseSendReport.show();
				_yourReportName.show();
				if (_reportUsername.isEmpty()) {
					_includeUsername.hide();
				} else {
					h += _includeUsername.height() + padding;
					_includeUsername.show();
				}
				if (_sendingState == SendingTooOld || _sendingState == SendingUnofficial) {
					QString verStr = getReportField(qstr("version"), qstr("Version:"));
					qint64 ver = verStr.isEmpty() ? 0 : verStr.toLongLong();
					if (!ver || (ver == AppVersion) || (ver < 0 && (-ver / 1000) == AppVersion)) {
						h += _getApp.height() + padding;
						_getApp.show();
						h -= _yourReportName.height() + padding; // hide report name
						_yourReportName.hide();
						if (!_reportUsername.isEmpty()) {
							h -= _includeUsername.height() + padding;
							_includeUsername.hide();
						}
					} else {
						_getApp.hide();
					}
					_showReport.hide();
					_report.hide();
					_minidump.hide();
					_saveReport.hide();
					_send.hide();
					_sendSkip.hide();
					_continue.show();
				} else {
					_getApp.hide();
					if (_reportShown) {
						h += (_pleaseSendReport.height() * 12.5) + padding + (_minidumpName.isEmpty() ? 0 : (_minidump.height() + padding));
						_report.show();
						if (_minidumpName.isEmpty()) {
							_minidump.hide();
						} else {
							_minidump.show();
						}
						if (_reportSaved || _sendingState == SendingFail || _sendingState == SendingProgress || _sendingState == SendingUploading) {
							_saveReport.hide();
						} else {
							_saveReport.show();
						}
						_showReport.hide();
					} else {
						_report.hide();
						_minidump.hide();
						_saveReport.hide();
						if (_sendingState == SendingFail || _sendingState == SendingProgress || _sendingState == SendingUploading) {
							_showReport.hide();
						} else {
							_showReport.show();
						}
					}
					if (_sendingState == SendingTooMany || _sendingState == SendingDone) {
						_send.hide();
						_sendSkip.hide();
						_continue.show();
					} else {
						if (_sendingState == SendingProgress || _sendingState == SendingUploading) {
							_send.hide();
						} else {
							_send.show();
						}
						_sendSkip.show();
						_continue.hide();
					}
				}
			}
		} else {
			_getApp.hide();
			_pleaseSendReport.hide();
			_yourReportName.hide();
			_includeUsername.hide();
			_showReport.hide();
			_report.hide();
			_minidump.hide();
			_saveReport.hide();
			_send.hide();
			_sendSkip.hide();
			_continue.hide();
		}
		_updatingCheck.hide();
		if (_updatingState == UpdatingCheck || _updatingState == UpdatingDownload) {
			h += padding + _updatingSkip.height() + padding;
			_updatingSkip.show();
		} else {
			_updatingSkip.hide();
		}
	}
#else // !TDESKTOP_DISABLE_AUTOUPDATE
	h += _networkSettings.height() + padding;
	h += padding + _send.height() + padding;
	if (_sendingState == SendingNoReport) {
		_pleaseSendReport.hide();
		_yourReportName.hide();
		_includeUsername.hide();
		_showReport.hide();
		_report.hide();
		_minidump.hide();
		_saveReport.hide();
		_send.hide();
		_sendSkip.hide();
		_continue.show();
		_networkSettings.hide();
	} else {
		h += _showReport.height() + padding + _yourReportName.height() + padding;
		_pleaseSendReport.show();
		_yourReportName.show();
		if (_reportUsername.isEmpty()) {
			_includeUsername.hide();
		} else {
			h += _includeUsername.height() + padding;
			_includeUsername.show();
		}
		if (_reportShown) {
			h += (_pleaseSendReport.height() * 12.5) + padding + (_minidumpName.isEmpty() ? 0 : (_minidump.height() + padding));
			_report.show();
			if (_minidumpName.isEmpty()) {
				_minidump.hide();
			} else {
				_minidump.show();
			}
			_showReport.hide();
			if (_reportSaved || _sendingState == SendingFail || _sendingState == SendingProgress || _sendingState == SendingUploading) {
				_saveReport.hide();
			} else {
				_saveReport.show();
			}
		} else {
			_report.hide();
			_minidump.hide();
			_saveReport.hide();
			if (_sendingState == SendingFail || _sendingState == SendingProgress || _sendingState == SendingUploading) {
				_showReport.hide();
			} else {
				_showReport.show();
			}
		}
		if (_sendingState == SendingDone) {
			_send.hide();
			_sendSkip.hide();
			_continue.show();
			_networkSettings.hide();
		} else {
			if (_sendingState == SendingProgress || _sendingState == SendingUploading) {
				_send.hide();
			} else {
				_send.show();
			}
			_sendSkip.show();
			if (_sendingState == SendingFail) {
				_networkSettings.show();
			} else {
				_networkSettings.hide();
			}
			_continue.hide();
		}
	}

	_getApp.show();
	h += _networkSettings.height() + padding;
#endif // else for !TDESKTOP_DISABLE_AUTOUPDATE

	QRect scr(QApplication::primaryScreen()->availableGeometry());
	QSize s(2 * padding + QFontMetrics(_label.font()).width(qsl("Last time Telegram Desktop was not closed properly.")) + padding + _networkSettings.width(), h);
	if (s == size()) {
		resizeEvent(0);
	} else {
		resize(s);
	}
}

void LastCrashedWindow::onNetworkSettings() {
	auto &p = Sandbox::PreLaunchProxy();
	NetworkSettingsWindow *box = new NetworkSettingsWindow(this, p.host, p.port ? p.port : 80, p.user, p.password);
	connect(box, SIGNAL(saved(QString, quint32, QString, QString)), this, SLOT(onNetworkSettingsSaved(QString, quint32, QString, QString)));
	box->show();
}

void LastCrashedWindow::onNetworkSettingsSaved(QString host, quint32 port, QString username, QString password) {
	Sandbox::RefPreLaunchProxy().host = host;
	Sandbox::RefPreLaunchProxy().port = port ? port : 80;
	Sandbox::RefPreLaunchProxy().user = username;
	Sandbox::RefPreLaunchProxy().password = password;
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	if ((_updatingState == UpdatingFail && (_sendingState == SendingNoReport || _sendingState == SendingUpdateCheck)) || (_updatingState == UpdatingCheck)) {
		Sandbox::stopUpdate();
		cSetLastUpdateCheck(0);
		Sandbox::startUpdateCheck();
	} else
#endif // !TDESKTOP_DISABLE_AUTOUPDATE
	if (_sendingState == SendingFail || _sendingState == SendingProgress) {
		onSendReport();
	}
	activate();
}

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
void LastCrashedWindow::setUpdatingState(UpdatingState state, bool force) {
	if (_updatingState != state || force) {
		_updatingState = state;
		switch (state) {
		case UpdatingLatest:
			_updating.setText(qsl("Latest version is installed."));
			if (_sendingState == SendingNoReport) {
				QTimer::singleShot(0, this, SLOT(onContinue()));
			} else {
				_sendingState = SendingNone;
			}
		break;
		case UpdatingReady:
			if (checkReadyUpdate()) {
				cSetRestartingUpdate(true);
				App::quit();
				return;
			} else {
				setUpdatingState(UpdatingFail);
				return;
			}
		break;
		case UpdatingCheck:
			_updating.setText(qsl("Checking for updates..."));
		break;
		case UpdatingFail:
			_updating.setText(qsl("Update check failed :("));
		break;
		}
		updateControls();
	}
}

void LastCrashedWindow::setDownloadProgress(qint64 ready, qint64 total) {
	qint64 readyTenthMb = (ready * 10 / (1024 * 1024)), totalTenthMb = (total * 10 / (1024 * 1024));
	QString readyStr = QString::number(readyTenthMb / 10) + '.' + QString::number(readyTenthMb % 10);
	QString totalStr = QString::number(totalTenthMb / 10) + '.' + QString::number(totalTenthMb % 10);
	QString res = qsl("Downloading update {ready} / {total} MB..").replace(qstr("{ready}"), readyStr).replace(qstr("{total}"), totalStr);
	if (_newVersionDownload != res) {
		_newVersionDownload = res;
		_updating.setText(_newVersionDownload);
		updateControls();
	}
}

void LastCrashedWindow::onUpdateRetry() {
	cSetLastUpdateCheck(0);
	Sandbox::startUpdateCheck();
}

void LastCrashedWindow::onUpdateSkip() {
	if (_sendingState == SendingNoReport) {
		onContinue();
	} else {
		if (_updatingState == UpdatingCheck || _updatingState == UpdatingDownload) {
			Sandbox::stopUpdate();
			setUpdatingState(UpdatingFail);
		}
		_sendingState = SendingNone;
		updateControls();
	}
}

void LastCrashedWindow::onUpdateChecking() {
	setUpdatingState(UpdatingCheck);
}

void LastCrashedWindow::onUpdateLatest() {
	setUpdatingState(UpdatingLatest);
}

void LastCrashedWindow::onUpdateDownloading(qint64 ready, qint64 total) {
	setUpdatingState(UpdatingDownload);
	setDownloadProgress(ready, total);
}

void LastCrashedWindow::onUpdateReady() {
	setUpdatingState(UpdatingReady);
}

void LastCrashedWindow::onUpdateFailed() {
	setUpdatingState(UpdatingFail);
}
#endif // !TDESKTOP_DISABLE_AUTOUPDATE

void LastCrashedWindow::onContinue() {
	if (SignalHandlers::restart() == SignalHandlers::CantOpen) {
		new NotStartedWindow();
	} else if (!Global::started()) {
		Sandbox::launch();
	}
	close();
}

void LastCrashedWindow::onSendingError(QNetworkReply::NetworkError e) {
	LOG(("Crash report sending error: %1").arg(e));

	_pleaseSendReport.setText(qsl("Sending crash report failed :("));
	_sendingState = SendingFail;
	if (_checkReply) {
		_checkReply->deleteLater();
		_checkReply = nullptr;
	}
	if (_sendReply) {
		_sendReply->deleteLater();
		_sendReply = nullptr;
	}
	updateControls();
}

void LastCrashedWindow::onSendingFinished() {
	if (_sendReply) {
		QByteArray result = _sendReply->readAll();
		LOG(("Crash report sending done, result: %1").arg(QString::fromUtf8(result)));

		_sendReply->deleteLater();
		_sendReply = nullptr;
		_pleaseSendReport.setText(qsl("Thank you for your report!"));
		_sendingState = SendingDone;
		updateControls();

		SignalHandlers::restart();
	}
}

void LastCrashedWindow::onSendingProgress(qint64 uploaded, qint64 total) {
	if (_sendingState != SendingProgress && _sendingState != SendingUploading) return;
	_sendingState = SendingUploading;

	if (total < 0) {
		_pleaseSendReport.setText(qsl("Sending crash report %1 KB...").arg(uploaded / 1024));
	} else {
		_pleaseSendReport.setText(qsl("Sending crash report %1 / %2 KB...").arg(uploaded / 1024).arg(total / 1024));
	}
	updateControls();
}

void LastCrashedWindow::closeEvent(QCloseEvent *e) {
	deleteLater();
}

void LastCrashedWindow::resizeEvent(QResizeEvent *e) {
	int padding = _size;
	_label.move(padding, padding + (_networkSettings.height() - _label.height()) / 2);

	_send.move(width() - padding - _send.width(), height() - padding - _send.height());
	if (_sendingState == SendingProgress || _sendingState == SendingUploading) {
		_sendSkip.move(width() - padding - _sendSkip.width(), height() - padding - _sendSkip.height());
	} else {
		_sendSkip.move(width() - padding - _send.width() - padding - _sendSkip.width(), height() - padding - _sendSkip.height());
	}

	_updating.move(padding, padding * 2 + _networkSettings.height() + (_networkSettings.height() - _updating.height()) / 2);

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	_pleaseSendReport.move(padding, padding * 2 + _networkSettings.height() + _networkSettings.height() + padding + (_showReport.height() - _pleaseSendReport.height()) / 2);
	_showReport.move(padding * 2 + _pleaseSendReport.width(), padding * 2 + _networkSettings.height() + _networkSettings.height() + padding);
	_yourReportName.move(padding, _showReport.y() + _showReport.height() + padding);
	_includeUsername.move(padding, _yourReportName.y() + _yourReportName.height() + padding);
	_getApp.move((width() - _getApp.width()) / 2, _showReport.y() + _showReport.height() + padding);

	if (_sendingState == SendingFail || _sendingState == SendingProgress) {
		_networkSettings.move(padding * 2 + _pleaseSendReport.width(), padding * 2 + _networkSettings.height() + _networkSettings.height() + padding);
	} else {
		_networkSettings.move(padding * 2 + _updating.width(), padding * 2 + _networkSettings.height());
	}

	if (_updatingState == UpdatingCheck || _updatingState == UpdatingDownload) {
		_updatingCheck.move(width() - padding - _updatingCheck.width(), height() - padding - _updatingCheck.height());
		_updatingSkip.move(width() - padding - _updatingSkip.width(), height() - padding - _updatingSkip.height());
	} else {
		_updatingCheck.move(width() - padding - _updatingCheck.width(), height() - padding - _updatingCheck.height());
		_updatingSkip.move(width() - padding - _updatingCheck.width() - padding - _updatingSkip.width(), height() - padding - _updatingSkip.height());
	}
#else // !TDESKTOP_DISABLE_AUTOUPDATE
	_getApp.move((width() - _getApp.width()) / 2, _updating.y() + _updating.height() + padding);

	_pleaseSendReport.move(padding, padding * 2 + _networkSettings.height() + _networkSettings.height() + padding + _getApp.height() + padding + (_showReport.height() - _pleaseSendReport.height()) / 2);
	_showReport.move(padding * 2 + _pleaseSendReport.width(), padding * 2 + _networkSettings.height() + _networkSettings.height() + padding + _getApp.height() + padding);
	_yourReportName.move(padding, _showReport.y() + _showReport.height() + padding);
	_includeUsername.move(padding, _yourReportName.y() + _yourReportName.height() + padding);

	_networkSettings.move(padding * 2 + _pleaseSendReport.width(), padding * 2 + _networkSettings.height() + _networkSettings.height() + padding + _getApp.height() + padding);
#endif // else for !TDESKTOP_DISABLE_AUTOUPDATE
	if (_reportUsername.isEmpty()) {
		_report.setGeometry(padding, _yourReportName.y() + _yourReportName.height() + padding, width() - 2 * padding, _pleaseSendReport.height() * 12.5);
	} else {
		_report.setGeometry(padding, _includeUsername.y() + _includeUsername.height() + padding, width() - 2 * padding, _pleaseSendReport.height() * 12.5);
	}
	_minidump.move(padding, _report.y() + _report.height() + padding);
	_saveReport.move(_showReport.x(), _showReport.y());

	_continue.move(width() - padding - _continue.width(), height() - padding - _continue.height());
}

NetworkSettingsWindow::NetworkSettingsWindow(QWidget *parent, QString host, quint32 port, QString username, QString password)
: PreLaunchWindow(qsl("HTTP Proxy Settings"))
, _hostLabel(this)
, _portLabel(this)
, _usernameLabel(this)
, _passwordLabel(this)
, _hostInput(this)
, _portInput(this)
, _usernameInput(this)
, _passwordInput(this, true)
, _save(this)
, _cancel(this, false)
, _parent(parent) {
	setWindowModality(Qt::ApplicationModal);

	_hostLabel.setText(qsl("Hostname"));
	_portLabel.setText(qsl("Port"));
	_usernameLabel.setText(qsl("Username"));
	_passwordLabel.setText(qsl("Password"));

	_save.setText(qsl("SAVE"));
	connect(&_save, SIGNAL(clicked()), this, SLOT(onSave()));
	_cancel.setText(qsl("CANCEL"));
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(close()));

	_hostInput.setText(host);
	_portInput.setText(QString::number(port));
	_usernameInput.setText(username);
	_passwordInput.setText(password);

	QRect scr(QApplication::primaryScreen()->availableGeometry());
	move(scr.x() + (scr.width() / 6), scr.y() + (scr.height() / 6));
	updateControls();
	show();

	_hostInput.setFocus();
	_hostInput.setCursorPosition(_hostInput.text().size());
}

void NetworkSettingsWindow::resizeEvent(QResizeEvent *e) {
	int padding = _size;
	_hostLabel.move(padding, padding);
	_hostInput.setGeometry(_hostLabel.x(), _hostLabel.y() + _hostLabel.height(), 2 * _hostLabel.width(), _hostInput.height());
	_portLabel.move(padding + _hostInput.width() + padding, padding);
	_portInput.setGeometry(_portLabel.x(), _portLabel.y() + _portLabel.height(), width() - padding - _portLabel.x(), _portInput.height());
	_usernameLabel.move(padding, _hostInput.y() + _hostInput.height() + padding);
	_usernameInput.setGeometry(_usernameLabel.x(), _usernameLabel.y() + _usernameLabel.height(), (width() - 3 * padding) / 2, _usernameInput.height());
	_passwordLabel.move(padding + _usernameInput.width() + padding, _usernameLabel.y());
	_passwordInput.setGeometry(_passwordLabel.x(), _passwordLabel.y() + _passwordLabel.height(), width() - padding - _passwordLabel.x(), _passwordInput.height());

	_save.move(width() - padding - _save.width(), height() - padding - _save.height());
	_cancel.move(_save.x() - padding - _cancel.width(), _save.y());
}

void NetworkSettingsWindow::onSave() {
	QString host = _hostInput.text().trimmed(), port = _portInput.text().trimmed(), username = _usernameInput.text().trimmed(), password = _passwordInput.text().trimmed();
	if (!port.isEmpty() && !port.toUInt()) {
		_portInput.setFocus();
		return;
	} else if (!host.isEmpty() && port.isEmpty()) {
		_portInput.setFocus();
		return;
	}
	emit saved(host, port.toUInt(), username, password);
	close();
}

void NetworkSettingsWindow::closeEvent(QCloseEvent *e) {
}

void NetworkSettingsWindow::updateControls() {
	_hostInput.updateGeometry();
	_hostInput.resize(_hostInput.sizeHint());
	_portInput.updateGeometry();
	_portInput.resize(_portInput.sizeHint());
	_usernameInput.updateGeometry();
	_usernameInput.resize(_usernameInput.sizeHint());
	_passwordInput.updateGeometry();
	_passwordInput.resize(_passwordInput.sizeHint());

	int padding = _size;
	int w = 2 * padding + _hostLabel.width() * 2 + padding + _portLabel.width() * 2 + padding;
	int h = padding + _hostLabel.height() + _hostInput.height() + padding + _usernameLabel.height() + _usernameInput.height() + padding + _save.height() + padding;
	if (w == width() && h == height()) {
		resizeEvent(0);
	} else {
		setGeometry(_parent->x() + (_parent->width() - w) / 2, _parent->y() + (_parent->height() - h) / 2, w, h);
	}
}
