/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#include "stdafx.h"
#include "style.h"
#include "lang.h"

#include "window.h"
#include "application.h"

#include "pspecific.h"
#include "title.h"
#include "intro/intro.h"
#include "mainwidget.h"
#include "layerwidget.h"
#include "settingswidget.h"

ConnectingWidget::ConnectingWidget(QWidget *parent, const QString &text, const QString &reconnect) : QWidget(parent), _shadow(st::boxShadow), _reconnect(this, QString()) {
	set(text, reconnect);
	connect(&_reconnect, SIGNAL(clicked()), this, SLOT(onReconnect()));
}

void ConnectingWidget::set(const QString &text, const QString &reconnect) {
	_text = text;
	_textWidth = st::linkFont->m.width(_text) + st::linkFont->spacew;
	int32 _reconnectWidth = 0;
	if (reconnect.isEmpty()) {
		_reconnect.hide();
	} else {
		_reconnect.setText(reconnect);
		_reconnect.show();
		_reconnect.move(st::connectingPadding.left() + _textWidth, st::boxShadow.pxHeight() + st::connectingPadding.top());
		_reconnectWidth = _reconnect.width();
	}
	resize(st::connectingPadding.left() + _textWidth + _reconnectWidth + st::connectingPadding.right() + st::boxShadow.pxWidth(), st::boxShadow.pxHeight() + st::connectingPadding.top() + st::linkFont->height + st::connectingPadding.bottom());
	update();
}
void ConnectingWidget::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	_shadow.paint(p, QRect(0, st::boxShadow.pxHeight(), width() - st::boxShadow.pxWidth(), height() - st::boxShadow.pxHeight()), QPoint(0, 0), BoxShadow::Top | BoxShadow::Right);
	p.fillRect(0, st::boxShadow.pxHeight(), width() - st::boxShadow.pxWidth(), height() - st::boxShadow.pxHeight(), st::connectingBG->b);
	p.setFont(st::linkFont->f);
	p.setPen(st::connectingColor->p);
	p.drawText(st::connectingPadding.left(), st::boxShadow.pxHeight() + st::connectingPadding.top() + st::linkFont->ascent, _text);
}

void ConnectingWidget::onReconnect() {
	MTP::restart();
}

TempDirDeleter::TempDirDeleter(QThread *thread) {
	moveToThread(thread);
	connect(thread, SIGNAL(started()), this, SLOT(onStart()));
}

void TempDirDeleter::onStart() {
	if (QDir(cTempDir()).removeRecursively()) {
		emit succeed();
	} else {
		emit failed();
	}
}

Window::Window(QWidget *parent)	: PsMainWindow(parent),
	intro(0), main(0), settings(0), layer(0), layerBG(0), _topWidget(0),
	_connecting(0), _tempDeleter(0), _tempDeleterThread(0), myIcon(QPixmap::fromImage(icon256)), dragging(false), _inactivePress(false) {

	if (objectName().isEmpty())
		setObjectName(qsl("MainWindow"));
	resize(st::wndDefWidth, st::wndDefHeight);
	setWindowOpacity(1);
	setLocale(QLocale(QLocale::English, QLocale::UnitedStates));
	centralwidget = new QWidget(this);
	centralwidget->setObjectName(qsl("centralwidget"));
	setCentralWidget(centralwidget);

	QMetaObject::connectSlotsByName(this);

	_inactiveTimer.setSingleShot(true);
	connect(&_inactiveTimer, SIGNAL(timeout()), this, SLOT(onInactiveTimer()));
}

void Window::inactivePress(bool inactive) {
	_inactivePress = inactive;
	if (_inactivePress) {
		_inactiveTimer.start(200);
	} else {
		_inactiveTimer.stop();
	}
}

bool Window::inactivePress() const {
	return _inactivePress;
}

void Window::onInactiveTimer() {
	inactivePress(false);
}

void Window::init() {
	psInitFrameless();
	setWindowIcon(myIcon);

	App::app()->installEventFilter(this);
    connect(windowHandle(), SIGNAL(activeChanged()), this, SLOT(checkHistoryActivation()));

	QPalette p(palette());
	p.setColor(QPalette::Window, st::wndBG->c);
	setPalette(p);

	title = new TitleWidget(this);

	psInitSize();
	psUpdateWorkmode();
}

void Window::clearWidgets() {
	layerHidden();
	if (settings) {
		anim::stop(settings);
		settings->hide();
		settings->deleteLater();
		settings = 0;
	}
	if (main) {
		anim::stop(main);
		main->hide();
		main->deleteLater();
		main = 0;
	}
	if (intro) {
		anim::stop(intro);
		intro->hide();
		intro->deleteLater();
		intro = 0;
	}
}

void Window::setupIntro(bool anim) {
	if (intro && (intro->animating() || intro->isVisible()) && !main) return;

	QPixmap bg = myGrab(this, QRect(0, st::titleHeight, width(), height() - st::titleHeight));

	clearWidgets();
	intro = new IntroWidget(this);
	intro->move(0, st::titleHeight);
	if (anim) {
		intro->animShow(bg);
	}

	fixOrder();

	updateTitleStatus();
}

void Window::getNotifySetting(const MTPInputNotifyPeer &peer, uint32 msWait) {
	MTP::send(MTPaccount_GetNotifySettings(peer), main->rpcDone(&MainWidget::gotNotifySetting, peer), main->rpcFail(&MainWidget::failNotifySetting, peer), 0, msWait);
}

void Window::setupMain(bool anim) {
	QPixmap bg = myGrab(this, QRect(0, st::titleHeight, width(), height() - st::titleHeight));
	clearWidgets();
	main = new MainWidget(this);
	main->move(0, st::titleHeight);
	if (anim) {
		main->animShow(bg);
	} else {
		MTP::send(MTPusers_GetUsers(MTP_vector<MTPInputUser>(QVector<MTPInputUser>(1, MTP_inputUserSelf()))), main->rpcDone(&MainWidget::startFull));
		main->activate();
	}

	fixOrder();

	updateTitleStatus();
}

void Window::showSettings() {
	App::wnd()->hideLayer();
	if (settings) {
		return hideSettings();
	}
	QPixmap bg = myGrab(this, QRect(0, st::titleHeight, width(), height() - st::titleHeight));

	if (intro) {
		anim::stop(intro);
		intro->hide();
	} else if (main) {
		anim::stop(main);
		main->hide();
	}
	settings = new Settings(this);
	settings->animShow(bg);

	fixOrder();
}

void Window::hideSettings(bool fast) {
	if (!settings) return;

	if (fast) {
		anim::stop(settings);
		settings->hide();
		settings->deleteLater();
		settings = 0;
		if (intro) {
			intro->show();
		} else {
			main->show();
		}
	} else {
		QPixmap bg = myGrab(this, QRect(0, st::titleHeight, width(), height() - st::titleHeight));

		anim::stop(settings);
		settings->hide();
		settings->deleteLater();
		settings = 0;
		if (intro) {
			intro->animShow(bg, true);
		} else {
			main->animShow(bg, true);
		}
	}

	fixOrder();
}

void Window::startMain(const MTPUser &user) {
	if (main) main->start(user);
	title->resizeEvent(0);
}

void Window::mtpStateChanged(int32 dc, int32 state) {
	if (dc == MTP::maindc()) {
		updateTitleStatus();
		if (settings) settings->updateConnectionType();
	}
}

void Window::updateTitleStatus() {
	int32 state = MTP::dcstate();
	if (state == MTProtoConnection::Connecting || state == MTProtoConnection::Disconnected || (state < 0 && state > -600)) {
		if (main || getms() > 5000 || _connecting) {
			showConnecting(lang(lng_connecting));
		}
	} else if (state < 0) {
		showConnecting(lang(lng_reconnecting).arg((-state) / 1000), lang(lng_reconnecting_try_now));
		QTimer::singleShot((-state) % 1000, this, SLOT(updateTitleStatus()));
	} else {
		hideConnecting();
	}
}

IntroWidget *Window::introWidget() {
	return intro;
}

MainWidget *Window::mainWidget() {
	return main;
}

Settings *Window::settingsWidget() {
	return settings;
}

void Window::showPhoto(const PhotoLink *lnk, HistoryItem *item) {
	return showPhoto(lnk->photo(), item);
}


void Window::showPhoto(PhotoData *photo, HistoryItem *item) {
	layerHidden();
	layer = new LayerWidget(this, photo, item);
}

PhotoData *Window::photoShown() {
	return layer ? layer->photoShown() : 0;
}

/*
void Window::showVideo(const VideoOpenLink *lnk, HistoryItem *item) {
	layerHidden();
	VideoData *video = App::video(lnk->video());
	layer = new LayerWidget(this, video, item);
}
/**/
void Window::showLayer(LayeredWidget *w) {
	layerHidden();
	layerBG = new BackgroundWidget(this, w);
}

void Window::showConnecting(const QString &text, const QString &reconnect) {
	if (_connecting) {
		_connecting->set(text, reconnect);
	} else {
		_connecting = new ConnectingWidget(this, text, reconnect);
		resizeEvent(0);
	}
}

void Window::hideConnecting() {
	if (_connecting) {
		_connecting->deleteLater();
		_connecting = 0;
	}
}

void Window::replaceLayer(LayeredWidget *w) {
	if (layer) layer->deleteLater();
	layer = 0;
	if (layerBG) {
		layerBG->replaceInner(w);
	} else {
		layerBG = new BackgroundWidget(this, w);
	}
}

void Window::hideLayer() {
	if (layerBG) {
		layerBG->onClose();
	}
}

bool Window::layerShown() {
	return !!layerBG || !!_topWidget;
}

bool Window::historyIsActive(int state) const {
    return psIsActive(state) && main && main->historyIsActive() && (!settings || !settings->isVisible());
}

void Window::checkHistoryActivation(int state) {
	if (main && MTP::authedId() && historyIsActive(state)) {
		main->historyWasRead();
	}
}

void Window::layerHidden() {
	if (layer) layer->deleteLater();
	layer = 0;
	if (layerBG) layerBG->deleteLater();
	layerBG = 0;
	if (main) main->setInnerFocus();
}

QRect Window::clientRect() const {
	return QRect(0, st::titleHeight, width(), height() - st::titleHeight);
}

QRect Window::photoRect() const {
	if (settings) {
		return settings->geometry();
	} else if (main) {
		QRect r(main->historyRect());
		r.moveLeft(r.left() + main->x());
		r.moveTop(r.top() + main->y());
		return r;
	}
	return QRect(0, 0, 0, 0);
}

void Window::wStartDrag(QMouseEvent *e) {
	dragStart = e->globalPos() - frameGeometry().topLeft();
	dragging = true;
}

void Window::paintEvent(QPaintEvent *e) {
}

HitTestType Window::hitTest(const QPoint &p) const {
	int x(p.x()), y(p.y()), w(width()), h(height());
	
	const int32 raw = psResizeRowWidth();
	if (!windowState().testFlag(Qt::WindowMaximized)) {
		if (y < raw) {
			if (x < raw) {
				return HitTestTopLeft;
			} else if (x > w - raw - 1) {
				return HitTestTopRight;
			}
			return HitTestTop;
		} else if (y > h - raw - 1) {
			if (x < raw) {
				return HitTestBottomLeft;
			} else if (x > w - raw - 1) {
				return HitTestBottomRight;
			}
			return HitTestBottom;
		} else if (x < raw) {
			return HitTestLeft;
		} else if (x > w - raw - 1) {
			return HitTestRight;
		}
	}
	HitTestType titleTest = title->hitTest(p - title->geometry().topLeft());
	if (titleTest && (!layer || titleTest != HitTestCaption)) {
		return titleTest;
	} else if (x >= 0 && y >= 0 && x < w && y < h) {
		return HitTestClient;
	}
	return HitTestNone;
}

bool Window::getPhotoCoords(PhotoData *photo, int32 &x, int32 &y, int32 &w) const {
	if (main && main->getPhotoCoords(photo, x, y, w)) {
		x += main->x();
		y += main->y();
		return true;
	} else if (settings && settings->getPhotoCoords(photo, x, y, w)) {
		x += main->x();
		y += main->y();
		return true;
	}
	return false;
}

bool Window::getVideoCoords(VideoData *video, int32 &x, int32 &y, int32 &w) const {
	if (main && main->getVideoCoords(video, x, y, w)) {
		x += main->x();
		y += main->y();
		return true;
	}
	return false;
}

QRect Window::iconRect() const {
	return QRect(st::titleIconPos + title->geometry().topLeft(), st::titleIconRect.pxSize());
}

bool Window::eventFilter(QObject *obj, QEvent *evt) {
	if (obj == App::app() && (evt->type() == QEvent::ApplicationActivate)) {
        QTimer::singleShot(1, this, SLOT(checkHistoryActivation()));
	} else if (obj == this && evt->type() == QEvent::WindowStateChange) {
		Qt::WindowState state = (windowState() & Qt::WindowMinimized) ? Qt::WindowMinimized : ((windowState() & Qt::WindowMaximized) ? Qt::WindowMaximized : ((windowState() & Qt::WindowFullScreen) ? Qt::WindowFullScreen : Qt::WindowNoState));
		psStateChanged(state);
		if (App::main()) {
			App::main()->mainStateChanged(state);
		}
	} else if (obj == this && (evt->type() == QEvent::Move || evt->type() == QEvent::Resize)) {
		psUpdatedPosition();
	}
	return PsMainWindow::eventFilter(obj, evt);
}

void Window::mouseMoveEvent(QMouseEvent *e) {
	if (e->buttons() & Qt::LeftButton) {
		if (dragging) {
			if (windowState().testFlag(Qt::WindowMaximized)) {
				setWindowState(windowState() & ~Qt::WindowMaximized);
				
				dragStart = e->globalPos() - frameGeometry().topLeft();
			} else {
				move(e->globalPos() - dragStart);
			}
		}
	} else if (dragging) {
		dragging = false;
	}
}

void Window::mouseReleaseEvent(QMouseEvent *e) {
	dragging = false;
}

bool Window::minimizeToTray() {
	if (App::quiting() || !trayIcon) return false;

	hide();
	if (!cSeenTrayTooltip()) {
		trayIcon->showMessage(QString::fromStdWString(AppName), lang(lng_tray_icon_text), QSystemTrayIcon::Information, 10000);
		cSetSeenTrayTooltip(true);
		App::writeConfig();
	}
	if (App::main()) App::main()->setOnline(windowState());
	return true;
}

void Window::setupTrayIcon() {
	if (!trayIcon) {
		if (trayIconMenu) trayIconMenu->deleteLater();
		trayIconMenu = new QMenu(this);
		trayIconMenu->setFont(QFont("Tahoma"));
		QAction *a;
		a = trayIconMenu->addAction(lang(lng_open_from_tray), this, SLOT(showFromTray()));
		a->setEnabled(true);
		a = trayIconMenu->addAction(lang(lng_quit_from_tray), this, SLOT(quitFromTray()));
		a->setEnabled(true);

		if (trayIcon) trayIcon->deleteLater();
		trayIcon = new QSystemTrayIcon(this);
		trayIcon->setIcon(this->windowIcon());
		trayIcon->setContextMenu(trayIconMenu);
		trayIcon->setToolTip(QString::fromStdWString(AppName));

		connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(toggleTray(QSystemTrayIcon::ActivationReason)));
		connect(trayIcon, SIGNAL(messageClicked()), this, SLOT(showFromTray()));
	}
	psUpdateCounter();
	trayIcon->show();
}	

void Window::quitFromTray() {
	App::quit();
}

void Window::activate() {
	bool wasHidden = !isVisible();
	setWindowState(windowState() & ~Qt::WindowMinimized);
	setVisible(true);
	activateWindow();
	if (wasHidden) {
		if (main) {
			main->windowShown();
		}
	}
}

void Window::noIntro(IntroWidget *was) {
	if (was == intro) {
		intro = 0;
	}
}

void Window::noSettings(Settings *was) {
	if (was == settings) {
		settings = 0;
	}
	checkHistoryActivation();
}

void Window::noMain(MainWidget *was) {
	if (was == main) {
		main = 0;
	}
}

void Window::noLayer(LayerWidget *was) {
	if (was == layer) {
		layer = 0;
	}
	fixOrder();
}

void Window::noBox(BackgroundWidget *was) {
	if (was == layerBG) {
		layerBG = 0;
	}
}

void Window::fixOrder() {
	title->raise();
	if (layer) layer->raise();
	if (layerBG) layerBG->raise();
	if (_topWidget) _topWidget->raise();
	if (_connecting) _connecting->raise();
}

void Window::topWidget(QWidget *w) {
	_topWidget = w;
}

void Window::noTopWidget(QWidget *w) {
	if (_topWidget == w) {
		_topWidget = 0;
	}
}

void Window::showFromTray(QSystemTrayIcon::ActivationReason reason) {
	if (reason != QSystemTrayIcon::Context) {
        activate();
		setWindowIcon(myIcon);
		psUpdateCounter();
		if (App::main()) App::main()->setOnline(windowState());
	}
}

void Window::toggleTray(QSystemTrayIcon::ActivationReason reason) {
	if (reason != QSystemTrayIcon::Context) {
		if (psIsActive()) {
			minimizeToTray();
		} else {
			showFromTray(reason);
		}
	}
}

void Window::closeEvent(QCloseEvent *e) {
	if (MTP::authedId() && minimizeToTray()) {
		e->ignore();
	} else {
		App::quit();
	}
}

TitleWidget *Window::getTitle() {
	return title;
}

void Window::resizeEvent(QResizeEvent *e) {
	title->setGeometry(QRect(0, 0, width(), st::titleHeight + st::titleShadow));
	if (layer) layer->resize(width(), height());
	if (layerBG) layerBG->resize(width(), height());
	if (_connecting) _connecting->setGeometry(0, height() - _connecting->height(), _connecting->width(), _connecting->height());
	emit resized(QSize(width(), height() - st::titleHeight));
}

Window::TempDirState Window::tempDirState() {
	if (_tempDeleter) {
		return TempDirRemoving;
	}
	return QDir(cTempDir()).exists() ? TempDirExists : TempDirEmpty;
}

void Window::tempDirDelete() {
	if (_tempDeleter) return;
	_tempDeleterThread = new QThread();
	_tempDeleter = new TempDirDeleter(_tempDeleterThread);
	connect(_tempDeleter, SIGNAL(succeed()), this, SLOT(onTempDirCleared()));
	connect(_tempDeleter, SIGNAL(failed()), this, SLOT(onTempDirClearFailed()));
	_tempDeleterThread->start();
}

void Window::onTempDirCleared() {
	_tempDeleter->deleteLater();
	_tempDeleter = 0;
	_tempDeleterThread->deleteLater();
	_tempDeleterThread = 0;
	emit tempDirCleared();
}

void Window::onTempDirClearFailed() {
	_tempDeleter->deleteLater();
	_tempDeleter = 0;
	_tempDeleterThread->deleteLater();
	_tempDeleterThread = 0;
	emit tempDirClearFailed();
}

Window::~Window() {
	delete _tempDeleter;
	delete _tempDeleterThread;
	delete _connecting;
	delete trayIcon;
	delete trayIconMenu;
	delete intro;
	delete main;
	delete layer;
	delete settings;
}
