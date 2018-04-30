/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mainwindow.h"

#include "data/data_document.h"
#include "dialogs/dialogs_layout.h"
#include "styles/style_dialogs.h"
#include "styles/style_window.h"
#include "styles/style_boxes.h"
#include "history/history.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "lang/lang_cloud_manager.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "shortcuts.h"
#include "messenger.h"
#include "application.h"
#include "passcodewidget.h"
#include "intro/introwidget.h"
#include "mainwidget.h"
#include "boxes/confirm_box.h"
#include "boxes/add_contact_box.h"
#include "boxes/connection_box.h"
#include "observer_peer.h"
#include "mediaview.h"
#include "storage/localstorage.h"
#include "apiwrap.h"
#include "settings/settings_widget.h"
#include "platform/platform_notifications_manager.h"
#include "window/layer_widget.h"
#include "window/notifications_manager.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_theme_warning.h"
#include "window/window_main_menu.h"
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
	if (Global::UseProxy()) {
		//Ui::show(Box<ConnectionBox>());
		Ui::show(ProxiesBoxController::CreateOwningBox());
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
		trayIconMenu->addAction(lang(lng_open_from_tray), this, SLOT(showFromTray()));
		trayIconMenu->addAction(lang(lng_minimize_to_tray), this, SLOT(minimizeToTray()));
		trayIconMenu->addAction(notificationActionText, this, SLOT(toggleDisplayNotifyFromTray()));
		trayIconMenu->addAction(lang(lng_quit_from_tray), this, SLOT(quitFromTray()));
	} else {
		trayIconMenu->addAction(lang(lng_minimize_to_tray), this, SLOT(minimizeToTray()));
		trayIconMenu->addAction(notificationActionText, this, SLOT(toggleDisplayNotifyFromTray()));
		trayIconMenu->addAction(lang(lng_quit_from_tray), this, SLOT(quitFromTray()));
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
		result = Ui::GrabWidget(_intro);
	} else if (_passcode) {
		result = Ui::GrabWidget(_passcode);
	} else if (_main) {
		result = Ui::GrabWidget(_main);
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
	Ui::hideSettingsAndLayer(anim::type::instant);
	if (_intro) _intro->hide();
	if (animated) {
		_passcode->showAnimated(bg);
	} else {
		setInnerFocus();
	}
}

void MainWindow::setupIntro() {
	if (_intro && !_intro->isHidden() && !_main) {
		return;
	}

	Ui::hideSettingsAndLayer(anim::type::instant);

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

	auto user = App::userLoaded(ServiceUserId);
	if (!user) {
		auto userFlags = MTPDuser::Flag::f_first_name | MTPDuser::Flag::f_phone | MTPDuser::Flag::f_status | MTPDuser::Flag::f_verified;
		user = App::feedUsers(MTP_vector<MTPUser>(1, MTP_user(
			MTP_flags(userFlags),
			MTP_int(ServiceUserId),
			MTPlong(),
			MTP_string("Telegram"),
			MTPstring(),
			MTPstring(),
			MTP_string("42777"),
			MTP_userProfilePhotoEmpty(),
			MTP_userStatusRecently(),
			MTPint(),
			MTPstring(),
			MTPstring(),
			MTPstring())));
	}
	auto offsetId = 0;
	auto offsetDate = 0;
	auto addOffset = 0;
	auto limit = 1;
	auto maxId = 0;
	auto minId = 0;
	auto historyHash = 0;
	_serviceHistoryRequest = MTP::send(
		MTPmessages_GetHistory(
			user->input,
			MTP_int(offsetId),
			MTP_int(offsetDate),
			MTP_int(addOffset),
			MTP_int(limit),
			MTP_int(maxId),
			MTP_int(minId),
			MTP_int(historyHash)),
		_main->rpcDone(&MainWidget::serviceHistoryDone),
		_main->rpcFail(&MainWidget::serviceHistoryFail));
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

	controller()->showSpecialLayer(Box<Settings::Widget>());
}

void MainWindow::showSpecialLayer(
		object_ptr<Window::LayerWidget> layer,
		anim::type animated) {
	if (_passcode) return;

	if (layer) {
		ensureLayerCreated();
		_layerBg->showSpecialLayer(std::move(layer), animated);
	} else if (_layerBg) {
		_layerBg->hideSpecialLayer(animated);
	}
}

bool MainWindow::showSectionInExistingLayer(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	if (_layerBg) {
		return _layerBg->showSectionInternal(memento, params);
	}
	return false;
}

void MainWindow::showMainMenu() {
	if (_passcode) return;

	if (isHidden()) showFromTray();

	ensureLayerCreated();
	_layerBg->showMainMenu(anim::type::normal);
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

void MainWindow::ui_hideSettingsAndLayer(anim::type animated) {
	if (_layerBg) {
		_layerBg->hideAll(animated);
		if (animated == anim::type::instant) {
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
	const auto state = MTP::dcstate();
	const auto throughProxy = Global::UseProxy();
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

void MainWindow::ui_showBox(
		object_ptr<BoxContent> box,
		LayerOptions options,
		anim::type animated) {
	if (box) {
		ensureLayerCreated();
		if (options & LayerOption::KeepOther) {
			if (options & LayerOption::ShowAfterOther) {
				_layerBg->prependBox(std::move(box), animated);
			} else {
				_layerBg->appendBox(std::move(box), animated);
			}
		} else {
			_layerBg->showBox(std::move(box), animated);
		}
	} else {
		if (_layerBg) {
			_layerBg->hideTopLayer(animated);
			if ((animated == anim::type::instant)
				&& _layerBg
				&& !_layerBg->layerShown()) {
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
		_connecting->show();
	} else {
		_connecting.create(bodyWidget(), text, reconnect);
		_connecting->show();
		updateControlsGeometry();
		fixOrder();
	}
}

void MainWindow::hideConnecting() {
	if (_connecting) {
		_connecting->hide();
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
		Ui::show(Box<AddContactBox>(), LayerOption::KeepOther);
	}
}

void MainWindow::onShowNewGroup() {
	if (isHidden()) showFromTray();

	if (App::self()) {
		Ui::show(
			Box<GroupInfoBox>(CreatingGroupGroup, false),
			LayerOption::KeepOther);
	}
}

void MainWindow::onShowNewChannel() {
	if (isHidden()) showFromTray();

	if (_main) {
		Ui::show(
			Box<GroupInfoBox>(CreatingGroupChannel, false),
			LayerOption::KeepOther);
	}
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

void MainWindow::noLayerStack(Window::LayerStackWidget *was) {
	if (was == _layerBg) {
		_layerBg = nullptr;
		if (controller()) {
			controller()->disableGifPauseReason(
				Window::GifPauseReason::Layer);
		}
	}
}

void MainWindow::layerFinishedHide(Window::LayerStackWidget *was) {
	if (was == _layerBg) {
		auto resetFocus = Ui::InFocusChain(was);
		if (resetFocus) setFocus();
		destroyLayerDelayed();
		if (resetFocus) setInnerFocus();
		InvokeQueued(this, [this] {
			checkHistoryActivation();
		});
	}
}

bool MainWindow::takeThirdSectionFromLayer() {
	return _layerBg ? _layerBg->takeToThirdSection() : false;
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
	} else if (!skipTrayClick()) {
		if (isActive()) {
			minimizeToTray();
		} else {
			showFromTray(reason);
		}
		_lastTrayClickTime = getms();
	}
}

bool MainWindow::skipTrayClick() const {
	return (_lastTrayClickTime > 0)
		&& (getms() - _lastTrayClickTime
			< QApplication::doubleClickInterval());
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
