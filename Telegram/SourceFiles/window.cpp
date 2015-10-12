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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "style.h"
#include "lang.h"

#include "window.h"
#include "application.h"

#include "pspecific.h"
#include "title.h"
#include "passcodewidget.h"
#include "intro/intro.h"
#include "mainwidget.h"
#include "layerwidget.h"
#include "settingswidget.h"
#include "boxes/confirmbox.h"
#include "boxes/contactsbox.h"
#include "boxes/addcontactbox.h"

#include "mediaview.h"
#include "localstorage.h"

ConnectingWidget::ConnectingWidget(QWidget *parent, const QString &text, const QString &reconnect) : QWidget(parent), _shadow(st::boxShadow), _reconnect(this, QString()) {
	set(text, reconnect);
	connect(&_reconnect, SIGNAL(clicked()), this, SLOT(onReconnect()));
}

void ConnectingWidget::set(const QString &text, const QString &reconnect) {
	_text = text;
	_textWidth = st::linkFont->width(_text) + st::linkFont->spacew;
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

	_shadow.paint(p, QRect(0, st::boxShadow.pxHeight(), width() - st::boxShadow.pxWidth(), height() - st::boxShadow.pxHeight()), 0, BoxShadow::Top | BoxShadow::Right);
	p.fillRect(0, st::boxShadow.pxHeight(), width() - st::boxShadow.pxWidth(), height() - st::boxShadow.pxHeight(), st::connectingBG->b);
	p.setFont(st::linkFont->f);
	p.setPen(st::connectingColor->p);
	p.drawText(st::connectingPadding.left(), st::boxShadow.pxHeight() + st::connectingPadding.top() + st::linkFont->ascent, _text);
}

void ConnectingWidget::onReconnect() {
	MTP::restart();
}

NotifyWindow::NotifyWindow(HistoryItem *msg, int32 x, int32 y, int32 fwdCount) : history(msg->history()), item(msg), fwdCount(fwdCount)
#ifdef Q_OS_WIN
, started(GetTickCount())
#endif
, close(this, st::notifyClose)
, alphaDuration(st::notifyFastAnim)
, posDuration(st::notifyFastAnim)
, hiding(false)
, _index(0)
, aOpacity(0)
, aOpacityFunc(st::notifyFastAnimFunc)
, aY(y + st::notifyHeight + st::notifyDeltaY) {

	updateNotifyDisplay();

	hideTimer.setSingleShot(true);
	connect(&hideTimer, SIGNAL(timeout()), this, SLOT(hideByTimer()));

	inputTimer.setSingleShot(true);
	connect(&inputTimer, SIGNAL(timeout()), this, SLOT(checkLastInput()));

	connect(&close, SIGNAL(clicked()), this, SLOT(unlinkHistoryAndNotify()));
	close.setAcceptBoth(true);
	close.move(st::notifyWidth - st::notifyClose.width - st::notifyClosePos.x(), st::notifyClosePos.y());
	close.show();

	aY.start(y);
	setGeometry(x, aY.current(), st::notifyWidth, st::notifyHeight);

	aOpacity.start(1);
    setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint | Qt::X11BypassWindowManagerHint);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);

	show();

	setWindowOpacity(aOpacity.current());

	alphaDuration = posDuration = st::notifyFastAnim;
	anim::start(this);

	checkLastInput();
}

void NotifyWindow::checkLastInput() {
#ifdef Q_OS_WIN
	LASTINPUTINFO lii;
	lii.cbSize = sizeof(LASTINPUTINFO);
	BOOL res = GetLastInputInfo(&lii);
	if (!res || lii.dwTime >= started) {
		hideTimer.start(st::notifyWaitLongHide);
	} else {
		inputTimer.start(300);
	}
#else
    // TODO
	if (true) {
		hideTimer.start(st::notifyWaitLongHide);
	} else {
		inputTimer.start(300);
	}
#endif
}

void NotifyWindow::moveTo(int32 x, int32 y, int32 index) {
	if (index >= 0) {
		_index = index;
	}
	move(x, aY.current());
	aY.start(y);
	aOpacity.restart();
	posDuration = st::notifyFastAnim;
	anim::start(this);
}

void NotifyWindow::updateNotifyDisplay() {
	if (!item) return;

	int32 w = st::notifyWidth, h = st::notifyHeight;
	QImage img(w * cIntRetinaFactor(), h * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	if (cRetina()) img.setDevicePixelRatio(cRetinaFactor());
	img.fill(st::notifyBG->c);

	{
		Painter p(&img);
		p.fillRect(0, 0, w - st::notifyBorderWidth, st::notifyBorderWidth, st::notifyBorder->b);
		p.fillRect(w - st::notifyBorderWidth, 0, st::notifyBorderWidth, h - st::notifyBorderWidth, st::notifyBorder->b);
		p.fillRect(st::notifyBorderWidth, h - st::notifyBorderWidth, w - st::notifyBorderWidth, st::notifyBorderWidth, st::notifyBorder->b);
		p.fillRect(0, st::notifyBorderWidth, st::notifyBorderWidth, h - st::notifyBorderWidth, st::notifyBorder->b);

		if (!App::passcoded() && cNotifyView() <= dbinvShowName) {
			if (history->peer->photo->loaded()) {
				p.drawPixmap(st::notifyPhotoPos.x(), st::notifyPhotoPos.y(), history->peer->photo->pix(st::notifyPhotoSize));
			} else {
				MTP::clearLoaderPriorities();
				peerPhoto = history->peer->photo;
				peerPhoto->load(true, true);
			}
		} else {
			static QPixmap icon = QPixmap::fromImage(App::wnd()->iconLarge().scaled(st::notifyPhotoSize, st::notifyPhotoSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation), Qt::ColorOnly);
			p.drawPixmap(st::notifyPhotoPos.x(), st::notifyPhotoPos.y(), icon);
		}

		int32 itemWidth = w - st::notifyPhotoPos.x() - st::notifyPhotoSize - st::notifyTextLeft - st::notifyClosePos.x() - st::notifyClose.width;

		QRect rectForName(st::notifyPhotoPos.x() + st::notifyPhotoSize + st::notifyTextLeft, st::notifyTextTop, itemWidth, st::msgNameFont->height);
		if (!App::passcoded() && cNotifyView() <= dbinvShowName) {
			if (history->peer->isChat()) {
				p.drawPixmap(QPoint(rectForName.left() + st::dlgChatImgPos.x(), rectForName.top() + st::dlgChatImgPos.y()), App::sprite(), st::dlgChatImg);
				rectForName.setLeft(rectForName.left() + st::dlgImgSkip);
			} else if (history->peer->isChannel()) {
				p.drawPixmap(QPoint(rectForName.left() + st::dlgChannelImgPos.x(), rectForName.top() + st::dlgChannelImgPos.y()), App::sprite(), st::dlgChannelImg);
				rectForName.setLeft(rectForName.left() + st::dlgImgSkip);
			}
		}

		QDateTime now(QDateTime::currentDateTime()), lastTime(item->date);
		QDate nowDate(now.date()), lastDate(lastTime.date());
		QString dt = lastTime.toString(cTimeFormat());
		int32 dtWidth = st::dlgHistFont->width(dt);
		rectForName.setWidth(rectForName.width() - dtWidth - st::dlgDateSkip);
		p.setFont(st::dlgDateFont->f);
		p.setPen(st::dlgDateColor->p);
		p.drawText(rectForName.left() + rectForName.width() + st::dlgDateSkip, rectForName.top() + st::dlgHistFont->ascent, dt);

		if (!App::passcoded() && cNotifyView() <= dbinvShowPreview) {
			const HistoryItem *textCachedFor = 0;
			Text itemTextCache(itemWidth);
			QRect r(st::notifyPhotoPos.x() + st::notifyPhotoSize + st::notifyTextLeft, st::notifyItemTop + st::msgNameFont->height, itemWidth, 2 * st::dlgFont->height);
			if (fwdCount < 2) {
				bool active = false;
				item->drawInDialog(p, r, active, textCachedFor, itemTextCache);
			} else {
				p.setFont(st::dlgHistFont->f);
				if (item->displayFromName() && !item->fromChannel()) {
					itemTextCache.setText(st::dlgHistFont, item->from()->name);
					p.setPen(st::dlgSystemColor->p);
					itemTextCache.drawElided(p, r.left(), r.top(), r.width(), st::dlgHistFont->height);
					r.setTop(r.top() + st::dlgHistFont->height);
				}
				p.setPen(st::dlgTextColor->p);
				p.drawText(r.left(), r.top() + st::dlgHistFont->ascent, lng_forward_messages(lt_count, fwdCount));
			}
		} else {
			static QString notifyText = st::dlgHistFont->elided(lang(lng_notification_preview), itemWidth);
			p.setPen(st::dlgSystemColor->p);
			p.drawText(st::notifyPhotoPos.x() + st::notifyPhotoSize + st::notifyTextLeft, st::notifyItemTop + st::msgNameFont->height + st::dlgHistFont->ascent, notifyText);
		}

		p.setPen(st::dlgNameColor->p);
		if (!App::passcoded() && cNotifyView() <= dbinvShowName) {
			history->peer->dialogName().drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());
		} else {
			p.setFont(st::msgNameFont->f);
			static QString notifyTitle = st::msgNameFont->elided(qsl("Telegram Desktop"), rectForName.width());
			p.drawText(rectForName.left(), rectForName.top() + st::msgNameFont->ascent, notifyTitle);
		}
	}

	pm = QPixmap::fromImage(img, Qt::ColorOnly);
	update();
}

void NotifyWindow::updatePeerPhoto() {
	if (!peerPhoto->isNull() && peerPhoto->loaded()) {
		QImage img(pm.toImage());
		{
			QPainter p(&img);
			p.drawPixmap(st::notifyPhotoPos.x(), st::notifyPhotoPos.y(), peerPhoto->pix(st::notifyPhotoSize));
		}
		peerPhoto = ImagePtr();
		pm = QPixmap::fromImage(img, Qt::ColorOnly);
		update();
	}
}

void NotifyWindow::itemRemoved(HistoryItem *del) {
	if (item == del) {
		item = 0;
		unlinkHistoryAndNotify();
	}
}

void NotifyWindow::unlinkHistoryAndNotify() {
	unlinkHistory();
	App::wnd()->notifyShowNext();
}

void NotifyWindow::unlinkHistory(History *hist) {
	if (!hist || hist == history) {
		animHide(st::notifyFastAnim, st::notifyFastAnimFunc);
		history = 0;
		item = 0;
	}
}

void NotifyWindow::enterEvent(QEvent *e) {
	if (!history) return;
	if (App::wnd()) App::wnd()->notifyStopHiding();
}

void NotifyWindow::leaveEvent(QEvent *e) {
	if (!history) return;
	App::wnd()->notifyStartHiding();
}

void NotifyWindow::startHiding() {
	hideTimer.start(st::notifyWaitShortHide);
}

void NotifyWindow::mousePressEvent(QMouseEvent *e) {
	if (!history) return;
	PeerId peer = history->peer->id;

	if (e->button() == Qt::RightButton) {
		unlinkHistoryAndNotify();
	} else if (history) {
		App::wnd()->showFromTray();
		if (App::passcoded()) {
			App::wnd()->setInnerFocus();
			App::wnd()->notifyClear();
		} else {
			App::wnd()->hideSettings();
			App::main()->showPeerHistory(peer, (!history->peer->isUser() && item && item->notifyByFrom() && item->id > 0) ? item->id : ShowAtUnreadMsgId);
		}
		e->ignore();
	}
}

void NotifyWindow::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	p.drawPixmap(0, 0, pm);
}

void NotifyWindow::animHide(float64 duration, anim::transition func) {
	if (!history) return;
	alphaDuration = duration;
	aOpacityFunc = func;
	aOpacity.start(0);
	aY.restart();
	hiding = true;
	anim::start(this);
}

void NotifyWindow::stopHiding() {
	if (!history) return;
	alphaDuration = st::notifyFastAnim;
	aOpacityFunc = st::notifyFastAnimFunc;
	aOpacity.start(1);
	aY.restart();
	hiding = false;
	hideTimer.stop();
	anim::start(this);
}

void NotifyWindow::hideByTimer() {
	if (!history) return;
	animHide(st::notifySlowHide, st::notifySlowHideFunc);
}

bool NotifyWindow::animStep(float64 ms) {
	float64 dtAlpha = ms / alphaDuration, dtPos = ms / posDuration;
	if (dtAlpha >= 1) {
		aOpacity.finish();
		if (hiding) {
			deleteLater();
		}
	} else {
		aOpacity.update(dtAlpha, aOpacityFunc);
	}
	setWindowOpacity(aOpacity.current());
	if (dtPos >= 1) {
		aY.finish();
	} else {
		aY.update(dtPos, anim::linear);
	}
	move(x(), aY.current());
	update();
	return (dtAlpha < 1 || (!hiding && dtPos < 1));
}

NotifyWindow::~NotifyWindow() {
	if (App::wnd()) App::wnd()->notifyShowNext(this);
}

Window::Window(QWidget *parent) : PsMainWindow(parent), _serviceHistoryRequest(0), title(0),
_passcode(0), intro(0), main(0), settings(0), layerBg(0), _isActive(false),
_connecting(0), _clearManager(0), dragging(false), _inactivePress(false), _shouldLockAt(0), _mediaView(0) {

	icon16 = icon256.scaledToWidth(16, Qt::SmoothTransformation);
	icon32 = icon256.scaledToWidth(32, Qt::SmoothTransformation);
	icon64 = icon256.scaledToWidth(64, Qt::SmoothTransformation);
	iconbig16 = iconbig256.scaledToWidth(16, Qt::SmoothTransformation);
	iconbig32 = iconbig256.scaledToWidth(32, Qt::SmoothTransformation);
	iconbig64 = iconbig256.scaledToWidth(64, Qt::SmoothTransformation);

	if (objectName().isEmpty()) {
		setObjectName(qsl("MainWindow"));
	}
	resize(st::wndDefWidth, st::wndDefHeight);

	setLocale(QLocale(QLocale::English, QLocale::UnitedStates));
	centralwidget = new QWidget(this);
	centralwidget->setObjectName(qsl("centralwidget"));
	setCentralWidget(centralwidget);

	QMetaObject::connectSlotsByName(this);

	_inactiveTimer.setSingleShot(true);
	connect(&_inactiveTimer, SIGNAL(timeout()), this, SLOT(onInactiveTimer()));

	connect(&notifyWaitTimer, SIGNAL(timeout()), this, SLOT(notifyFire()));

	_isActiveTimer.setSingleShot(true);
	connect(&_isActiveTimer, SIGNAL(timeout()), this, SLOT(updateIsActive()));

	connect(&_autoLockTimer, SIGNAL(timeout()), this, SLOT(checkAutoLock()));

	connect(this, SIGNAL(imageLoaded()), this, SLOT(notifyUpdateAllPhotos()));

	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
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

void Window::stateChanged(Qt::WindowState state) {
	psUserActionDone();

	updateIsActive((state == Qt::WindowMinimized) ? cOfflineBlurTimeout() : cOnlineFocusTimeout());

	psUpdateSysMenu(state);
	if (state == Qt::WindowMinimized && cWorkMode() == dbiwmTrayOnly) {
		App::wnd()->minimizeToTray();
	}
	psSavePosition(state);
}

void Window::init() {
	psInitFrameless();
	setWindowIcon(wndIcon);

	App::app()->installEventFilter(this);
	connect(windowHandle(), SIGNAL(windowStateChanged(Qt::WindowState)), this, SLOT(stateChanged(Qt::WindowState)));
	connect(windowHandle(), SIGNAL(activeChanged()), this, SLOT(checkHistoryActivation()), Qt::QueuedConnection);

	QPalette p(palette());
	p.setColor(QPalette::Window, st::wndBG->c);
	setPalette(p);

	title = new TitleWidget(this);

    psInitSize();
}

void Window::firstShow() {
#ifdef Q_OS_WIN
	trayIconMenu = new ContextMenu(this);
#else
	trayIconMenu = new QMenu(this);
	trayIconMenu->setFont(QFont("Tahoma"));
#endif
	QString notificationItem = lang(cDesktopNotify() 
		? lng_disable_notifications_from_tray : lng_enable_notifications_from_tray);
	
	if (cPlatform() == dbipWindows || cPlatform() == dbipMac) {
		trayIconMenu->addAction(notificationItem, this, SLOT(toggleDisplayNotifyFromTray()))->setEnabled(true);
		trayIconMenu->addAction(lang(lng_minimize_to_tray), this, SLOT(minimizeToTray()))->setEnabled(true);
		trayIconMenu->addAction(lang(lng_quit_from_tray), this, SLOT(quitFromTray()))->setEnabled(true);
	} else {
		trayIconMenu->addAction(notificationItem, this, SLOT(toggleDisplayNotifyFromTray()))->setEnabled(true);
		trayIconMenu->addAction(lang(lng_open_from_tray), this, SLOT(showFromTray()))->setEnabled(true);
		trayIconMenu->addAction(lang(lng_minimize_to_tray), this, SLOT(minimizeToTray()))->setEnabled(true);
		trayIconMenu->addAction(lang(lng_quit_from_tray), this, SLOT(quitFromTray()))->setEnabled(true);
	}
	psUpdateWorkmode();

	psFirstShow();
	updateTrayMenu();
}

QWidget *Window::filedialogParent() {
	return (_mediaView && _mediaView->isVisible()) ? (QWidget*)_mediaView : (QWidget*)this;
}

void Window::clearWidgets() {
	hideLayer(true);
	if (_passcode) {
		_passcode->hide();
		_passcode->deleteLater();
		_passcode = 0;
	}
	if (settings) {
		anim::stop(settings);
		settings->hide();
		settings->deleteLater();
		settings->rpcInvalidate();
		settings = 0;
	}
	if (main) {
		anim::stop(main);
		main->hide();
		main->deleteLater();
		main->rpcInvalidate();
		main = 0;
	}
	if (intro) {
		anim::stop(intro);
		intro->hide();
		intro->deleteLater();
		intro->rpcInvalidate();
		intro = 0;
	}
	title->updateBackButton();
	updateGlobalMenu();
}

void Window::clearPasscode() {
	if (!_passcode) return;

	QPixmap bg = myGrab(this, QRect(0, st::titleHeight, width(), height() - st::titleHeight));

	anim::stop(_passcode);
	_passcode->hide();
	_passcode->deleteLater();
	_passcode = 0;
	if (intro) {
		intro->animShow(bg, true);
	} else if (settings) {
		settings->animShow(bg, true);
	} else {
		main->animShow(bg, true);
	}
	notifyUpdateAll();
	title->updateBackButton();
	updateGlobalMenu();
}

void Window::setupPasscode(bool anim) {
	QPixmap bg = myGrab(this, QRect(0, st::titleHeight, width(), height() - st::titleHeight));
	if (_passcode) {
		anim::stop(_passcode);
		_passcode->hide();
		_passcode->deleteLater();
	}
	_passcode = new PasscodeWidget(this);
	_passcode->move(0, st::titleHeight);
	if (main) main->hide();
	if (settings) settings->hide();
	if (intro) intro->hide();
	if (anim) {
		_passcode->animShow(bg);
	} else {
		setInnerFocus();
	}
	_shouldLockAt = 0;
	notifyUpdateAll();
	title->updateBackButton();
	updateGlobalMenu();
}

void Window::checkAutoLockIn(int msec) {
	if (_autoLockTimer.isActive()) {
		int remain = _autoLockTimer.remainingTime();
		if (remain > 0 && remain <= msec) return;
	}
	_autoLockTimer.start(msec);
}

void Window::checkAutoLock() {
	if (!cHasPasscode() || App::passcoded()) return;

	App::app()->checkLocalTime();
	uint64 ms = getms(true), idle = psIdleTime(), should = cAutoLock() * 1000ULL;
	if (idle >= should || (_shouldLockAt > 0 && ms > _shouldLockAt + 3000ULL)) {
		setupPasscode(true);
	} else {
		_shouldLockAt = ms + (should - idle);
		_autoLockTimer.start(should - idle);
	}
}

void Window::setupIntro(bool anim) {
	cSetContactsReceived(false);
	cSetDialogsReceived(false);
	if (intro && (intro->animating() || intro->isVisible()) && !main) return;

	QPixmap bg = anim ? myGrab(this, QRect(0, st::titleHeight, width(), height() - st::titleHeight)) : QPixmap();

	clearWidgets();
	intro = new IntroWidget(this);
	intro->move(0, st::titleHeight);
	if (anim) {
		intro->animShow(bg);
	}

	fixOrder();

	updateTitleStatus();

	_delayedServiceMsgs.clear();
	if (_serviceHistoryRequest) {
		MTP::cancel(_serviceHistoryRequest);
		_serviceHistoryRequest = 0;
	}
}

void Window::getNotifySetting(const MTPInputNotifyPeer &peer, uint32 msWait) {
	MTP::send(MTPaccount_GetNotifySettings(peer), main->rpcDone(&MainWidget::gotNotifySetting, peer), main->rpcFail(&MainWidget::failNotifySetting, peer), 0, msWait);
}

void Window::serviceNotification(const QString &msg, const MTPMessageMedia &media, bool force) {
	History *h = (main && App::userLoaded(ServiceUserId)) ? App::history(ServiceUserId) : 0;
	if (!h || (!force && h->isEmpty())) {
		_delayedServiceMsgs.push_back(DelayedServiceMsg(msg, media));
		return sendServiceHistoryRequest();
	}

	main->serviceNotification(msg, media);
}

void Window::showDelayedServiceMsgs() {
	QVector<DelayedServiceMsg> toAdd = _delayedServiceMsgs;
	_delayedServiceMsgs.clear();
	for (QVector<DelayedServiceMsg>::const_iterator i = toAdd.cbegin(), e = toAdd.cend(); i != e; ++i) {
		serviceNotification(i->first, i->second, true);
	}
}

void Window::sendServiceHistoryRequest() {
	if (!main || !main->started() || _delayedServiceMsgs.isEmpty() || _serviceHistoryRequest) return;

	UserData *user = App::userLoaded(ServiceUserId);
	if (!user) {
		int32 userFlags = MTPDuser::flag_first_name | MTPDuser::flag_phone | MTPDuser::flag_status;
		user = App::feedUsers(MTP_vector<MTPUser>(1, MTP_user(MTP_int(userFlags), MTP_int(ServiceUserId), MTPlong(), MTP_string("Telegram"), MTPstring(), MTPstring(), MTP_string("42777"), MTP_userProfilePhotoEmpty(), MTP_userStatusRecently(), MTPint())));
	}
	_serviceHistoryRequest = MTP::send(MTPmessages_GetHistory(user->input, MTP_int(0), MTP_int(0), MTP_int(1), MTP_int(0), MTP_int(0)), main->rpcDone(&MainWidget::serviceHistoryDone), main->rpcFail(&MainWidget::serviceHistoryFail));
}

void Window::setupMain(bool anim, const MTPUser *self) {
	Local::readStickers();

	QPixmap bg = anim ? myGrab(this, QRect(0, st::titleHeight, width(), height() - st::titleHeight)) : QPixmap();
	clearWidgets();
	main = new MainWidget(this);
	main->move(0, st::titleHeight);
	if (anim) {
		main->animShow(bg);
	} else {
		main->activate();
	}
	if (self) {
		main->start(*self);
	} else {
		MTP::send(MTPusers_GetUsers(MTP_vector<MTPInputUser>(1, MTP_inputUserSelf())), main->rpcDone(&MainWidget::startFull));
	}
	title->resizeEvent(0);

	fixOrder();

	updateTitleStatus();

	_mediaView = new MediaView();
}

void Window::updateCounter() {
	psUpdateCounter();
	title->updateCounter();
}

void Window::showSettings() {
	if (_passcode) return;

	if (isHidden()) showFromTray();

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
	settings = new SettingsWidget(this);
	settings->animShow(bg);
	title->updateBackButton();

	fixOrder();
}

void Window::hideSettings(bool fast) {
	if (!settings || _passcode) return;

	if (fast) {
		anim::stop(settings);
		settings->hide();
		settings->deleteLater();
		settings->rpcInvalidate();
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
		settings->rpcInvalidate();
		settings = 0;
		if (intro) {
			intro->animShow(bg, true);
		} else {
			main->animShow(bg, true);
		}
	}
	title->updateBackButton();

	fixOrder();
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
		showConnecting(lng_reconnecting(lt_count, ((-state) / 1000) + 1), lang(lng_reconnecting_try_now));
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

SettingsWidget *Window::settingsWidget() {
	return settings;
}

PasscodeWidget *Window::passcodeWidget() {
	return _passcode;
}

void Window::showPhoto(const PhotoLink *lnk, HistoryItem *item) {
	return lnk->peer() ? showPhoto(lnk->photo(), lnk->peer()) : showPhoto(lnk->photo(), item);
}

void Window::showPhoto(PhotoData *photo, HistoryItem *item) {
	hideLayer(true);
	_mediaView->showPhoto(photo, item);
	_mediaView->activateWindow();
	_mediaView->setFocus();
}

void Window::showPhoto(PhotoData *photo, PeerData *peer) {
	hideLayer(true);
	_mediaView->showPhoto(photo, peer);
	_mediaView->activateWindow();
	_mediaView->setFocus();
}

void Window::showDocument(DocumentData *doc, HistoryItem *item) {
	hideLayer(true);
	_mediaView->showDocument(doc, item);
	_mediaView->activateWindow();
	_mediaView->setFocus();
}

void Window::showLayer(LayeredWidget *w, bool forceFast) {
	bool fast = forceFast || layerShown();
	hideLayer(true);
	layerBg = new BackgroundWidget(this, w);
	if (fast) {
		layerBg->showFast();
	}
}

void Window::replaceLayer(LayeredWidget *w) {
	if (layerBg) {
		layerBg->replaceInner(w);
	} else {
		layerBg = new BackgroundWidget(this, w);
	}
}

void Window::showConnecting(const QString &text, const QString &reconnect) {
	if (_connecting) {
		_connecting->set(text, reconnect);
	} else {
		_connecting = new ConnectingWidget(this, text, reconnect);
		_connecting->show();
		resizeEvent(0);
		fixOrder();
	}
	if (settings) settings->update();
}

bool Window::connectingVisible() const {
	return _connecting && !_connecting->isHidden();
}

void Window::hideConnecting() {
	if (_connecting) {
		_connecting->deleteLater();
		_connecting = 0;
	}
	if (settings) settings->update();
}

void Window::hideLayer(bool fast) {
	if (layerBg) {
		layerBg->onClose();
		if (fast) {
			layerBg->hide();
			layerBg->deleteLater();
			layerBg = 0;
		}
	}
	hideMediaview();
}

bool Window::hideInnerLayer() {
	if (layerBg) {
		return layerBg->onInnerClose();
	}
	return true;
}

bool Window::layerShown() {
	return !!layerBg;
}

bool Window::historyIsActive() const {
    return isActive(false) && main && main->historyIsActive() && (!settings || !settings->isVisible());
}

void Window::checkHistoryActivation() {
	if (main && MTP::authedId() && historyIsActive()) {
		main->historyWasRead();
	}
    QTimer::singleShot(1, this, SLOT(updateTrayMenu()));
}

void Window::layerHidden() {
	if (layerBg) {
		layerBg->hide();
		layerBg->deleteLater();
	}
	layerBg = 0;
	hideMediaview();
	setInnerFocus();
}

void Window::hideMediaview() {
    if (_mediaView && !_mediaView->isHidden()) {
        _mediaView->hide();
#if defined Q_OS_LINUX32 || defined Q_OS_LINUX64
        if (App::wnd()) {
            App::wnd()->activateWindow();
        }
#endif
    }
}

bool Window::contentOverlapped(const QRect &globalRect) {
	if (main && main->contentOverlapped(globalRect)) return true;
	if (layerBg && layerBg->contentOverlapped(globalRect)) return true;
	return false;
}

void Window::setInnerFocus() {
	if (layerBg && layerBg->canSetFocus()) {
		layerBg->setInnerFocus();
	} else if (_passcode) {
		_passcode->setInnerFocus();
	} else if (settings) {
		settings->setInnerFocus();
	} else if (main) {
		main->setInnerFocus();
	}
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
	if (titleTest) {
		return titleTest;
	} else if (x >= 0 && y >= 0 && x < w && y < h) {
		return HitTestClient;
	}
	return HitTestNone;
}

QRect Window::iconRect() const {
	return QRect(st::titleIconPos + title->geometry().topLeft(), st::titleIconImg.pxSize());
}

bool Window::eventFilter(QObject *obj, QEvent *evt) {
	QEvent::Type t = evt->type();
	if (t == QEvent::MouseButtonPress || t == QEvent::KeyPress || t == QEvent::TouchBegin) {
		psUserActionDone();
	} else if (t == QEvent::MouseMove) {
		if (main && main->isIdle()) {
			psUserActionDone();
			main->checkIdleFinish();
		}
	}
	if (obj == App::app()) {
		if (t == QEvent::ApplicationActivate) {
			psUserActionDone();
			QTimer::singleShot(1, this, SLOT(checkHistoryActivation()));
		} else if (t == QEvent::FileOpen) {
			QString url = static_cast<QFileOpenEvent*>(evt)->url().toEncoded();
			if (!url.trimmed().midRef(0, 5).compare(qsl("tg://"), Qt::CaseInsensitive)) {
				cSetStartUrl(url);
				if (!cStartUrl().isEmpty() && App::main() && App::self()) {
					App::main()->openLocalUrl(cStartUrl());
					cSetStartUrl(QString());
				}
			}
			activate();
		}
	} else if (obj == this) {
		if (t == QEvent::WindowStateChange) {
			Qt::WindowState state = (windowState() & Qt::WindowMinimized) ? Qt::WindowMinimized : ((windowState() & Qt::WindowMaximized) ? Qt::WindowMaximized : ((windowState() & Qt::WindowFullScreen) ? Qt::WindowFullScreen : Qt::WindowNoState));
			stateChanged(state);
		} else if (t == QEvent::Move || t == QEvent::Resize) {
			psUpdatedPosition();
		}
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
    if (App::quiting() || !psHasTrayIcon()) return false;

	hide();
    if (cPlatform() == dbipWindows && trayIcon && !cSeenTrayTooltip()) {
		trayIcon->showMessage(QString::fromStdWString(AppName), lang(lng_tray_icon_text), QSystemTrayIcon::Information, 10000);
		cSetSeenTrayTooltip(true);
		Local::writeSettings();
	}
	updateIsActive(cOfflineBlurTimeout());
	updateTrayMenu();
	updateGlobalMenu();
	return true;
}

void Window::updateTrayMenu(bool force) {
    if (!trayIconMenu || (cPlatform() == dbipWindows && !force)) return;

    bool active = isActive(false);
    QString notificationItem = lang(cDesktopNotify()
        ? lng_disable_notifications_from_tray : lng_enable_notifications_from_tray);

    QAction *first = trayIconMenu->actions().at(0);
    first->setText(notificationItem);
    if (cPlatform() == dbipWindows || cPlatform() == dbipMac) {
        QAction *second = trayIconMenu->actions().at(1);
        second->setText(lang(active ? lng_minimize_to_tray : lng_open_from_tray));
        disconnect(second, SIGNAL(triggered(bool)), 0, 0);
        connect(second, SIGNAL(triggered(bool)), this, active ? SLOT(minimizeToTray()) : SLOT(showFromTray()));
    } else {
        QAction *third = trayIconMenu->actions().at(2);
        third->setDisabled(!isVisible());
    }
#ifndef Q_OS_WIN
    if (trayIcon) {
        trayIcon->setContextMenu((active || cPlatform() != dbipMac) ? trayIconMenu : 0);
    }
#endif

    psTrayMenuUpdated();
}

void Window::onShowAddContact() {
	if (isHidden()) showFromTray();

	if (main) main->showAddContact();
}

void Window::onShowNewGroup() {
	if (isHidden()) showFromTray();

	if (main) replaceLayer(new GroupInfoBox(CreatingGroupGroup, false));
}

void Window::onShowNewChannel() {
	if (isHidden()) showFromTray();

	if (main) replaceLayer(new GroupInfoBox(CreatingGroupChannel, false));
}

void Window::onLogout() {
	if (isHidden()) showFromTray();

	ConfirmBox *box = new ConfirmBox(lang(lng_sure_logout), lang(lng_settings_logout), st::attentionBoxButton);
	connect(box, SIGNAL(confirmed()), this, SLOT(onLogoutSure()));
	App::wnd()->showLayer(box);
}

void Window::onLogoutSure() {
	App::logOut();
}

void Window::updateGlobalMenu() {
#ifdef Q_OS_MAC
	psMacUpdateMenu();
#endif
}

void Window::quitFromTray() {
	App::quit();
}

void Window::activate() {
	bool wasHidden = !isVisible();
	setWindowState(windowState() & ~Qt::WindowMinimized);
	setVisible(true);
	psActivateProcess();
	activateWindow();
	updateIsActive(cOnlineFocusTimeout());
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

void Window::noSettings(SettingsWidget *was) {
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

void Window::noBox(BackgroundWidget *was) {
	if (was == layerBg) {
		layerBg = 0;
	}
}

void Window::layerFinishedHide(BackgroundWidget *was) {
	if (was == layerBg) {
		QTimer::singleShot(0, this, SLOT(layerHidden()));
	}
}

void Window::fixOrder() {
	title->raise();
	if (layerBg) layerBg->raise();
	if (_connecting) _connecting->raise();
}

void Window::showFromTray(QSystemTrayIcon::ActivationReason reason) {
	if (reason != QSystemTrayIcon::Context) {
        QTimer::singleShot(1, this, SLOT(updateTrayMenu()));
        QTimer::singleShot(1, this, SLOT(updateGlobalMenu()));
        activate();
		updateCounter();
	}
}

void Window::toggleTray(QSystemTrayIcon::ActivationReason reason) {
	if (cPlatform() == dbipMac && isActive(false)) return;
	if (reason == QSystemTrayIcon::Context) {
		updateTrayMenu(true);
		QTimer::singleShot(1, this, SLOT(psShowTrayMenu()));
	} else {
		if (isActive(false)) {
			minimizeToTray();
		} else {
			showFromTray(reason);
		}
	}
}

void Window::toggleDisplayNotifyFromTray() {
	cSetDesktopNotify(!cDesktopNotify());
	if (settings) {
		settings->updateDisplayNotify();
	} else {
		if (!cDesktopNotify()) {
			notifyClear();
		}
		Local::writeUserSettings();
		updateTrayMenu();
	}
}

void Window::closeEvent(QCloseEvent *e) {
	if (MTP::authedId() && !App::app()->isSavingSession() && minimizeToTray()) {
		e->ignore();
	} else {
		App::quit();
	}
}

TitleWidget *Window::getTitle() {
	return title;
}

void Window::resizeEvent(QResizeEvent *e) {
	if (!title) return;

	bool wideMode = (width() >= st::wideModeWidth);
	if (wideMode != cWideMode()) {
		cSetWideMode(wideMode);
		updateWideMode();
	}
	title->setGeometry(0, 0, width(), st::titleHeight);
	if (layerBg) layerBg->resize(width(), height());
	if (_connecting) _connecting->setGeometry(0, height() - _connecting->height(), _connecting->width(), _connecting->height());
	emit resized(QSize(width(), height() - st::titleHeight));
}

void Window::updateWideMode() {
	title->updateWideMode();
	if (main) main->updateWideMode();
	if (settings) settings->updateWideMode();
	if (intro) intro->updateWideMode();
	if (layerBg) layerBg->updateWideMode();
}

bool Window::needBackButton() {
	return !!settings;
}

Window::TempDirState Window::tempDirState() {
	if (_clearManager && _clearManager->hasTask(Local::ClearManagerDownloads)) {
		return TempDirRemoving;
	}
	return QDir(cTempDir()).exists() ? TempDirExists : TempDirEmpty;
}

Window::TempDirState Window::localStorageState() {
	if (_clearManager && _clearManager->hasTask(Local::ClearManagerStorage)) {
		return TempDirRemoving;
	}
	return (Local::hasImages() || Local::hasStickers() || Local::hasAudios()) ? TempDirExists : TempDirEmpty;
}

void Window::tempDirDelete(int task) {
	if (_clearManager) {
		if (_clearManager->addTask(task)) {
			return;
		} else {
			_clearManager->deleteLater();
			_clearManager = 0;
		}
	}
	_clearManager = new Local::ClearManager();
	_clearManager->addTask(task);
	connect(_clearManager, SIGNAL(succeed(int,void*)), this, SLOT(onClearFinished(int,void*)));
	connect(_clearManager, SIGNAL(failed(int,void*)), this, SLOT(onClearFailed(int,void*)));
	_clearManager->start();
}

void Window::onClearFinished(int task, void *manager) {
	if (manager && manager == _clearManager) {
		_clearManager->deleteLater();
		_clearManager = 0;
	}
	emit tempDirCleared(task);
}

void Window::onClearFailed(int task, void *manager) {
	if (manager && manager == _clearManager) {
		_clearManager->deleteLater();
		_clearManager = 0;
	}
	emit tempDirClearFailed(task);
}

void Window::quit() {
	delete _mediaView;
	_mediaView = 0;
	delete main;
	main = 0;
	notifyClearFast();
}

void Window::notifySchedule(History *history, HistoryItem *item) {
	if (App::quiting() || !history->currentNotification() || !main) return;

	PeerData *notifyByFrom = (!history->peer->isUser() && item->notifyByFrom()) ? item->from() : 0;

	bool haveSetting = (history->peer->notify != UnknownNotifySettings);
	if (haveSetting) {
		if (history->peer->notify != EmptyNotifySettings && history->peer->notify->mute > unixtime()) {
			if (notifyByFrom) {
				haveSetting = (item->from()->notify != UnknownNotifySettings);
				if (haveSetting) {
					if (notifyByFrom->notify != EmptyNotifySettings && notifyByFrom->notify->mute > unixtime()) {
						history->popNotification(item);
						return;
					}
				} else {
					App::wnd()->getNotifySetting(MTP_inputNotifyPeer(notifyByFrom->input));
				}
			} else {
				history->popNotification(item);
				return;
			}
		}
	} else {
		if (notifyByFrom && notifyByFrom->notify == UnknownNotifySettings) {
			App::wnd()->getNotifySetting(MTP_inputNotifyPeer(notifyByFrom->input), 10);
		}
		App::wnd()->getNotifySetting(MTP_inputNotifyPeer(history->peer->input));
	}

	HistoryForwarded *fwd = item->toHistoryForwarded();
	int delay = fwd ? 500 : 100, t = unixtime();
	uint64 ms = getms(true);
	bool isOnline = main->lastWasOnline(), otherNotOld = ((cOtherOnline() * uint64(1000)) + cOnlineCloudTimeout() > t * uint64(1000));
	bool otherLaterThanMe = (cOtherOnline() * uint64(1000) + (ms - main->lastSetOnline()) > t * uint64(1000));
	if (!isOnline && otherNotOld && otherLaterThanMe) {
		delay = cNotifyCloudDelay();
	} else if (cOtherOnline() >= t) {
		delay = cNotifyDefaultDelay();
	}

	uint64 when = getms(true) + delay;
	notifyWhenAlerts[history].insert(when, notifyByFrom);
	if (cDesktopNotify() && !psSkipDesktopNotify()) {
		NotifyWhenMaps::iterator i = notifyWhenMaps.find(history);
		if (i == notifyWhenMaps.end()) {
			i = notifyWhenMaps.insert(history, NotifyWhenMap());
		}
		if (i.value().constFind(item->id) == i.value().cend()) {
			i.value().insert(item->id, when);
		}
		NotifyWaiters *addTo = haveSetting ? &notifyWaiters : &notifySettingWaiters;
		NotifyWaiters::const_iterator it = addTo->constFind(history);
		if (it == addTo->cend() || it->when > when) {
			addTo->insert(history, NotifyWaiter(item->id, when, notifyByFrom));
		}
	}
	if (haveSetting) {
		if (!notifyWaitTimer.isActive() || notifyWaitTimer.remainingTime() > delay) {
			notifyWaitTimer.start(delay);
		}
	}
}

void Window::notifyFire() {
	notifyShowNext();
}

void Window::notifyClear(History *history) {
	if (!history) {
		for (NotifyWindows::const_iterator i = notifyWindows.cbegin(), e = notifyWindows.cend(); i != e; ++i) {
			(*i)->unlinkHistory();
		}
		psClearNotifies();
		for (NotifyWhenMaps::const_iterator i = notifyWhenMaps.cbegin(), e = notifyWhenMaps.cend(); i != e; ++i) {
			i.key()->clearNotifications();
		}
		notifyWaiters.clear();
		notifySettingWaiters.clear();
		notifyWhenMaps.clear();
		return;
	}
	notifyWaiters.remove(history);
	notifySettingWaiters.remove(history);
	for (NotifyWindows::const_iterator i = notifyWindows.cbegin(), e = notifyWindows.cend(); i != e; ++i) {
		(*i)->unlinkHistory(history);
	}
	psClearNotifies(history->peer->id);
	notifyWhenMaps.remove(history);
	notifyWhenAlerts.remove(history);
	notifyShowNext();
}

void Window::notifyClearFast() {
	notifyWaiters.clear();
	notifySettingWaiters.clear();
	for (NotifyWindows::const_iterator i = notifyWindows.cbegin(), e = notifyWindows.cend(); i != e; ++i) {
		(*i)->deleteLater();
	}
	psClearNotifies();
	notifyWindows.clear();
	notifyWhenMaps.clear();
	notifyWhenAlerts.clear();
}

void Window::notifySettingGot() {
	int32 t = unixtime();
	for (NotifyWaiters::iterator i = notifySettingWaiters.begin(); i != notifySettingWaiters.end();) {
		History *history = i.key();
		if (history->peer->notify == UnknownNotifySettings) {
			++i;
		} else {
			if (history->peer->notify == EmptyNotifySettings || history->peer->notify->mute <= t) {
				notifyWaiters.insert(i.key(), i.value());
			} else if (PeerData *from = i.value().notifyByFrom) {
				if (from->notify == UnknownNotifySettings) {
					++i;
					continue;
				} else if (from->notify == EmptyNotifySettings || from->notify->mute <= t) {
					notifyWaiters.insert(i.key(), i.value());
				}
			}
			i = notifySettingWaiters.erase(i);
		}
	}
	notifyWaitTimer.stop();
	notifyShowNext();
}

void Window::notifyShowNext(NotifyWindow *remove) {
	if (App::quiting()) return;

	int32 count = NotifyWindowsCount;
	if (remove) {
		for (NotifyWindows::iterator i = notifyWindows.begin(), e = notifyWindows.end(); i != e; ++i) {
			if ((*i) == remove) {
				notifyWindows.erase(i);
				break;
			}
		}
	}

	uint64 ms = getms(true), nextAlert = 0;
	bool alert = false;
	int32 now = unixtime();
	for (NotifyWhenAlerts::iterator i = notifyWhenAlerts.begin(); i != notifyWhenAlerts.end();) {
		while (!i.value().isEmpty() && i.value().begin().key() <= ms) {
			NotifySettingsPtr n = i.key()->peer->notify, f = i.value().begin().value() ? i.value().begin().value()->notify : UnknownNotifySettings;
			while (!i.value().isEmpty() && i.value().begin().key() <= ms + 500) { // not more than one sound in 500ms from one peer - grouping
				i.value().erase(i.value().begin());
			}
			if (n == EmptyNotifySettings || (n != UnknownNotifySettings && n->mute <= now)) {
				alert = true;
			} else if (f == EmptyNotifySettings || (f != UnknownNotifySettings && f->mute <= now)) { // notify by from()
				alert = true;
			}
		}
		if (i.value().isEmpty()) {
			i = notifyWhenAlerts.erase(i);
		} else {
			if (!nextAlert || nextAlert > i.value().begin().key()) {
				nextAlert = i.value().begin().key();
			}
			++i;
		}
	}
	if (alert) {
		psFlash();
		App::playSound();
	}

    if (cCustomNotifies()) {
        for (NotifyWindows::const_iterator i = notifyWindows.cbegin(), e = notifyWindows.cend(); i != e; ++i) {
            int32 ind = (*i)->index();
            if (ind < 0) continue;
            --count;
        }
    }
	if (count <= 0 || notifyWaiters.isEmpty() || !cDesktopNotify() || psSkipDesktopNotify()) {
		if (nextAlert) {
			notifyWaitTimer.start(nextAlert - ms);
		}
		return;
	}

	QRect r = psDesktopRect();
	int32 x = r.x() + r.width() - st::notifyWidth - st::notifyDeltaX, y = r.y() + r.height() - st::notifyHeight - st::notifyDeltaY;
	while (count > 0) {
		uint64 next = 0;
		HistoryItem *notifyItem = 0;
		History *notifyHistory = 0;
		NotifyWaiters::iterator notifyWaiter = notifyWaiters.end();
		for (NotifyWaiters::iterator i = notifyWaiters.begin(); i != notifyWaiters.end();) {
			History *history = i.key();
			if (history->currentNotification() && history->currentNotification()->id != i.value().msg) {
				NotifyWhenMaps::iterator j = notifyWhenMaps.find(history);
				if (j == notifyWhenMaps.end()) {
					history->clearNotifications();
					i = notifyWaiters.erase(i);
					notifyWaiter = notifyHistory ? notifyWaiters.find(notifyHistory) : notifyWaiters.end();
					continue;
				}
				do {
					NotifyWhenMap::const_iterator k = j.value().constFind(history->currentNotification()->id);
					if (k != j.value().cend()) {
						i.value().msg = k.key();
						i.value().when = k.value();
						break;
					}
					history->skipNotification();
				} while (history->currentNotification());
			}
			if (!history->currentNotification()) {
				notifyWhenMaps.remove(history);
				i = notifyWaiters.erase(i);
				notifyWaiter = notifyHistory ? notifyWaiters.find(notifyHistory) : notifyWaiters.end();
				continue;
			}
			uint64 when = i.value().when;
			if (!notifyItem || next > when) {
				next = when;
				notifyItem = history->currentNotification();
				notifyHistory = history;
				notifyWaiter = i;
			}
			++i;
		}
		if (notifyItem) {
			if (next > ms) {
				if (nextAlert && nextAlert < next) {
					next = nextAlert;
					nextAlert = 0;
				}
				notifyWaitTimer.start(next - ms);
				break;
			} else {
				HistoryForwarded *fwd = notifyItem->toHistoryForwarded(); // forwarded notify grouping
				int32 fwdCount = 1;

				uint64 ms = getms(true);
				History *history = notifyItem->history();
				NotifyWhenMaps::iterator j = notifyWhenMaps.find(history);
				if (j == notifyWhenMaps.cend()) {
					history->clearNotifications();
				} else {
					HistoryItem *nextNotify = 0;
					do {
						history->skipNotification();
						if (!history->hasNotification()) {
							break;
						}

						j.value().remove((fwd ? fwd : notifyItem)->id);
						do {
							NotifyWhenMap::const_iterator k = j.value().constFind(history->currentNotification()->id);
							if (k != j.value().cend()) {
								nextNotify = history->currentNotification();
								notifyWaiter.value().msg = k.key();
								notifyWaiter.value().when = k.value();
								break;
							}
							history->skipNotification();
						} while (history->hasNotification());
						if (nextNotify) {
							if (fwd) {
								HistoryForwarded *nextFwd = nextNotify->toHistoryForwarded();
								if (nextFwd && fwd->from() == nextFwd->from() && qAbs(int64(nextFwd->date.toTime_t()) - int64(fwd->date.toTime_t())) < 2) {
									fwd = nextFwd;
									++fwdCount;
								} else {
									nextNotify = 0;
								}
							} else {
								nextNotify = 0;
							}
						}
					} while (nextNotify);
				}

				if (cCustomNotifies()) {
					NotifyWindow *notify = new NotifyWindow(notifyItem, x, y, fwdCount);
					notifyWindows.push_back(notify);
					psNotifyShown(notify);
					--count;
				} else {
					psPlatformNotify(notifyItem, fwdCount);
				}

				if (!history->hasNotification()) {
					if (notifyWaiter != notifyWaiters.cend()) notifyWaiters.erase(notifyWaiter);
					notifyWhenMaps.remove(history);
					continue;
				}
			}
		} else {
			break;
		}
	}
	if (nextAlert) {
		notifyWaitTimer.start(nextAlert - ms);
	}

	count = NotifyWindowsCount - count;
	for (NotifyWindows::const_iterator i = notifyWindows.cbegin(), e = notifyWindows.cend(); i != e; ++i) {
		int32 ind = (*i)->index();
		if (ind < 0) continue;
		--count;
		(*i)->moveTo(x, y - count * (st::notifyHeight + st::notifyDeltaY));
	}
}

void Window::notifyItemRemoved(HistoryItem *item) {
	if (cCustomNotifies()) {
		for (NotifyWindows::const_iterator i = notifyWindows.cbegin(), e = notifyWindows.cend(); i != e; ++i) {
			(*i)->itemRemoved(item);
		}
	}
}

void Window::notifyStopHiding() {
    if (cCustomNotifies()) {
        for (NotifyWindows::const_iterator i = notifyWindows.cbegin(), e = notifyWindows.cend(); i != e; ++i) {
            (*i)->stopHiding();
        }
    }
}

void Window::notifyStartHiding() {
    if (cCustomNotifies()) {
        for (NotifyWindows::const_iterator i = notifyWindows.cbegin(), e = notifyWindows.cend(); i != e; ++i) {
            (*i)->startHiding();
        }
    }
}

void Window::notifyUpdateAllPhotos() {
    if (cCustomNotifies()) {
        for (NotifyWindows::const_iterator i = notifyWindows.cbegin(), e = notifyWindows.cend(); i != e; ++i) {
            (*i)->updatePeerPhoto();
        }
    }
	if (_mediaView && !_mediaView->isHidden()) _mediaView->updateControls();
}

void Window::notifyUpdateAll() {
	if (cCustomNotifies()) {
		for (NotifyWindows::const_iterator i = notifyWindows.cbegin(), e = notifyWindows.cend(); i != e; ++i) {
			(*i)->updateNotifyDisplay();
		}
	}
	psClearNotifies();
}

void Window::notifyActivateAll() {
	if (cCustomNotifies()) {
        for (NotifyWindows::const_iterator i = notifyWindows.cbegin(), e = notifyWindows.cend(); i != e; ++i) {
            psActivateNotify(*i);
        }
    }
}

QImage Window::iconLarge() const {
	return iconbig256;
}

void Window::placeSmallCounter(QImage &img, int size, int count, style::color bg, const QPoint &shift, style::color color) {
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
	style::font f(fontSize);
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

QImage Window::iconWithCounter(int size, int count, style::color bg, bool smallIcon) {
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
		result.fill(st::transparent->c);
		{
			QPainter p(&result);
			p.setBrush(bg->b);
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
			style::font f(fontSize);
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
			p.setFont(f->f);

			p.setPen(st::counterColor->p);

			p.drawText(size - w - d, size - f->height + f->ascent, cnt);
		}
		return result;
	} else {
		if (size != 16 && size != 32) size = 64;
	}

	QImage img(smallIcon ? ((size == 16) ? iconbig16 : (size == 32 ? iconbig32 : iconbig64)) : ((size == 16) ? icon16 : (size == 32 ? icon32 : icon64)));
	if (!count) return img;

	if (smallIcon) {
		placeSmallCounter(img, size, count, bg, QPoint(), st::counterColor);
	} else {
		QPainter p(&img);
		p.drawPixmap(size / 2, size / 2, QPixmap::fromImage(iconWithCounter(-size / 2, count, bg, false), Qt::ColorOnly));
	}
	return img;
}

void Window::sendPaths() {
	if (App::passcoded()) return;
	hideMediaview();
	if (settings) {
		hideSettings();
	} else {
		if (layerShown()) {
			hideLayer();
		}
		if (main && !main->animating()) {
			main->activate();
		}
	}
}

void Window::mediaOverviewUpdated(PeerData *peer, MediaOverviewType type) {
	if (main) main->mediaOverviewUpdated(peer, type);
	if (!_mediaView || _mediaView->isHidden()) return;
	_mediaView->mediaOverviewUpdated(peer, type);
}

void Window::documentUpdated(DocumentData *doc) {
	if (!_mediaView || _mediaView->isHidden()) return;
	_mediaView->documentUpdated(doc);
}

void Window::changingMsgId(HistoryItem *row, MsgId newId) {
	if (main) main->changingMsgId(row, newId);
	if (!_mediaView || _mediaView->isHidden()) return;
	_mediaView->changingMsgId(row, newId);
}

bool Window::isActive(bool cached) const {
	if (cached) return _isActive;
	return isActiveWindow() && isVisible() && !(windowState() & Qt::WindowMinimized);
}

void Window::updateIsActive(int timeout) {
	if (timeout) return _isActiveTimer.start(timeout);
	_isActive = isActive(false);
	if (main) main->updateOnline();
}

Window::~Window() {
    notifyClearFast();
	delete _clearManager;
	delete _connecting;
	delete _mediaView;
	delete trayIcon;
	delete trayIconMenu;
	delete intro;
	delete main;
	delete settings;
}
