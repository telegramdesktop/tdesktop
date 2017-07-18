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
#include "window/main_window.h"

#include "storage/localstorage.h"
#include "styles/style_window.h"
#include "platform/platform_window_title.h"
#include "window/themes/window_theme.h"
#include "window/window_controller.h"
#include "mediaview.h"
#include "messenger.h"
#include "mainwindow.h"

namespace Window {

constexpr auto kInactivePressTimeout = 200;

QImage LoadLogo() {
	return QImage(qsl(":/gui/art/logo_256.png"));
}

QImage LoadLogoNoMargin() {
	return QImage(qsl(":/gui/art/logo_256_no_margin.png"));
}

QIcon CreateOfficialIcon() {
	auto useNoMarginLogo = (cPlatform() == dbipMac);
	if (auto messenger = Messenger::InstancePointer()) {
		return QIcon(App::pixmapFromImageInPlace(useNoMarginLogo ? messenger->logoNoMargin() : messenger->logo()));
	}
	return QIcon(App::pixmapFromImageInPlace(useNoMarginLogo ? LoadLogoNoMargin() : LoadLogo()));
}

QIcon CreateIcon() {
	auto result = CreateOfficialIcon();
	if (cPlatform() == dbipLinux32 || cPlatform() == dbipLinux64) {
		return QIcon::fromTheme("telegram", result);
	}
	return result;
}

MainWindow::MainWindow() : QWidget()
, _positionUpdatedTimer(this)
, _body(this)
, _icon(CreateIcon())
, _titleText(qsl("Telegram")) {
	subscribe(Theme::Background(), [this](const Theme::BackgroundUpdate &data) {
		if (data.paletteChanged()) {
			if (_title) {
				_title->update();
			}
			updatePalette();
		}
	});
	subscribe(Global::RefUnreadCounterUpdate(), [this] { updateUnreadCounter(); });
	subscribe(Global::RefWorkMode(), [this](DBIWorkMode mode) { workmodeUpdated(mode); });
	subscribe(Messenger::Instance().authSessionChanged(), [this] { checkAuthSession(); });
	checkAuthSession();

	_isActiveTimer.setCallback([this] { updateIsActive(0); });
	_inactivePressTimer.setCallback([this] { setInactivePress(false); });
}

bool MainWindow::hideNoQuit() {
	if (_mediaView && !_mediaView->isHidden()) {
		hideMediaview();
		return true;
	}
	if (Global::WorkMode().value() == dbiwmTrayOnly || Global::WorkMode().value() == dbiwmWindowAndTray) {
		if (minimizeToTray()) {
			Ui::showChatsList();
			return true;
		}
	} else if (cPlatform() == dbipMac || cPlatform() == dbipMacOld) {
		closeWithoutDestroy();
		updateIsActive(Global::OfflineBlurTimeout());
		updateGlobalMenu();
		Ui::showChatsList();
		return true;
	}
	return false;
}

void MainWindow::clearWidgets() {
	Ui::hideLayer(true);
	if (_mediaView) {
		hideMediaview();
		_mediaView->rpcClear();
		_mediaView->clearData();
	}
	clearWidgetsHook();
	updateGlobalMenu();
}

void MainWindow::showPhoto(const PhotoOpenClickHandler *lnk, HistoryItem *item) {
	return (!item && lnk->peer()) ? showPhoto(lnk->photo(), lnk->peer()) : showPhoto(lnk->photo(), item);
}

void MainWindow::showPhoto(PhotoData *photo, HistoryItem *item) {
	if (_mediaView->isHidden()) Ui::hideLayer(true);
	_mediaView->showPhoto(photo, item);
	_mediaView->activateWindow();
	_mediaView->setFocus();
}

void MainWindow::showPhoto(PhotoData *photo, PeerData *peer) {
	if (_mediaView->isHidden()) Ui::hideLayer(true);
	_mediaView->showPhoto(photo, peer);
	_mediaView->activateWindow();
	_mediaView->setFocus();
}

void MainWindow::showDocument(DocumentData *doc, HistoryItem *item) {
	if (cUseExternalVideoPlayer() && doc->isVideo()) {
		QDesktopServices::openUrl(QUrl("file:///" + doc->location(false).fname));
	} else {
		if (_mediaView->isHidden()) Ui::hideLayer(true);
		_mediaView->showDocument(doc, item);
		_mediaView->activateWindow();
		_mediaView->setFocus();
	}
}

bool MainWindow::ui_isMediaViewShown() {
	return _mediaView && !_mediaView->isHidden();
}

void MainWindow::updateIsActive(int timeout) {
	if (timeout > 0) {
		return _isActiveTimer.callOnce(timeout);
	}
	_isActive = computeIsActive();
	updateIsActiveHook();
}

bool MainWindow::computeIsActive() const {
	return isActiveWindow() && isVisible() && !(windowState() & Qt::WindowMinimized);
}

void MainWindow::hideMediaview() {
	if (_mediaView && !_mediaView->isHidden()) {
		_mediaView->hide();
#if defined Q_OS_LINUX32 || defined Q_OS_LINUX64
		reActivateWindow();
#endif
	}
}

void MainWindow::onReActivate() {
	if (auto w = App::wnd()) {
		if (auto f = QApplication::focusWidget()) {
			f->clearFocus();
		}
		w->windowHandle()->requestActivate();
		w->activate();
		if (auto f = QApplication::focusWidget()) {
			f->clearFocus();
		}
		w->setInnerFocus();
	}
}

QWidget *MainWindow::filedialogParent() {
	return (_mediaView && _mediaView->isVisible()) ? (QWidget*)_mediaView : (QWidget*)this;
}

void MainWindow::createMediaView() {
	_mediaView.create(nullptr);
}

void MainWindow::updateWindowIcon() {
	setWindowIcon(_icon);
}

void MainWindow::init() {
	initHook();
	updateWindowIcon();

	connect(windowHandle(), &QWindow::activeChanged, this, [this] { handleActiveChanged(); }, Qt::QueuedConnection);
	connect(windowHandle(), &QWindow::windowStateChanged, this, [this](Qt::WindowState state) { handleStateChanged(state); });

	_positionUpdatedTimer->setSingleShot(true);
	connect(_positionUpdatedTimer, SIGNAL(timeout()), this, SLOT(savePositionByTimer()));

	updatePalette();

	if ((_title = Platform::CreateTitleWidget(this))) {
		_title->init();
	}

	initSize();
	updateUnreadCounter();
}

void MainWindow::handleStateChanged(Qt::WindowState state) {
	stateChangedHook(state);
	updateIsActive((state == Qt::WindowMinimized) ? Global::OfflineBlurTimeout() : Global::OnlineFocusTimeout());
	psUserActionDone();
	if (state == Qt::WindowMinimized && Global::WorkMode().value() == dbiwmTrayOnly) {
		minimizeToTray();
	}
	savePosition(state);
}

void MainWindow::handleActiveChanged() {
	if (isActiveWindow() && _mediaView && !_mediaView->isHidden()) {
		_mediaView->activateWindow();
		_mediaView->setFocus();
	}
	App::CallDelayed(1, this, [this] {
		updateTrayMenu();
	});
}

void MainWindow::updatePalette() {
	auto p = palette();
	p.setColor(QPalette::Window, st::windowBg->c);
	setPalette(p);
}

HitTestResult MainWindow::hitTest(const QPoint &p) const {
	auto titleResult = _title ? _title->hitTest(p - _title->geometry().topLeft()) : Window::HitTestResult::None;
	if (titleResult != Window::HitTestResult::None) {
		return titleResult;
	} else if (rect().contains(p)) {
		return Window::HitTestResult::Client;
	}
	return Window::HitTestResult::None;
}

void MainWindow::initSize() {
	setMinimumWidth(st::windowMinWidth);
	setMinimumHeight((_title ? _title->height() : 0) + st::windowMinHeight);

	auto position = cWindowPos();
	DEBUG_LOG(("Window Pos: Initializing first %1, %2, %3, %4 (maximized %5)").arg(position.x).arg(position.y).arg(position.w).arg(position.h).arg(Logs::b(position.maximized)));

	auto avail = QDesktopWidget().availableGeometry();
	bool maximized = false;
	auto geom = QRect(avail.x() + (avail.width() - st::windowDefaultWidth) / 2, avail.y() + (avail.height() - st::windowDefaultHeight) / 2, st::windowDefaultWidth, st::windowDefaultHeight);
	if (position.w && position.h) {
		for (auto screen : QGuiApplication::screens()) {
			if (position.moncrc == screenNameChecksum(screen->name())) {
				auto screenGeometry = screen->geometry();
				DEBUG_LOG(("Window Pos: Screen found, screen geometry: %1, %2, %3, %4").arg(screenGeometry.x()).arg(screenGeometry.y()).arg(screenGeometry.width()).arg(screenGeometry.height()));

				auto w = screenGeometry.width(), h = screenGeometry.height();
				if (w >= st::windowMinWidth && h >= st::windowMinHeight) {
					if (position.x < 0) position.x = 0;
					if (position.y < 0) position.y = 0;
					if (position.w > w) position.w = w;
					if (position.h > h) position.h = h;
					position.x += screenGeometry.x();
					position.y += screenGeometry.y();
					if (position.x + st::windowMinWidth <= screenGeometry.x() + screenGeometry.width() &&
						position.y + st::windowMinHeight <= screenGeometry.y() + screenGeometry.height()) {
						DEBUG_LOG(("Window Pos: Resulting geometry is %1, %2, %3, %4").arg(position.x).arg(position.y).arg(position.w).arg(position.h));
						geom = QRect(position.x, position.y, position.w, position.h);
					}
				}
				break;
			}
		}
		maximized = position.maximized;
	}
	DEBUG_LOG(("Window Pos: Setting first %1, %2, %3, %4").arg(geom.x()).arg(geom.y()).arg(geom.width()).arg(geom.height()));
	setGeometry(geom);
}

void MainWindow::positionUpdated() {
	_positionUpdatedTimer->start(SaveWindowPositionTimeout);
}

bool MainWindow::titleVisible() const {
	return _title && !_title->isHidden();
}

void MainWindow::setTitleVisible(bool visible) {
	if (_title && (_title->isHidden() == visible)) {
		_title->setVisible(visible);
		updateControlsGeometry();
	}
	titleVisibilityChangedHook();
}

int32 MainWindow::screenNameChecksum(const QString &name) const {
	auto bytes = name.toUtf8();
	return hashCrc32(bytes.constData(), bytes.size());
}

void MainWindow::setPositionInited() {
	_positionInited = true;
}

void MainWindow::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void MainWindow::updateControlsGeometry() {
	auto bodyTop = 0;
	auto bodyWidth = width();
	if (_title && !_title->isHidden()) {
		_title->setGeometry(0, bodyTop, width(), _title->height());
		bodyTop += _title->height();
	}
	if (_rightColumn) {
		bodyWidth -= _rightColumn->width();
		_rightColumn->setGeometry(bodyWidth, bodyTop, width() - bodyWidth, height() - bodyTop);
	}
	_body->setGeometry(0, bodyTop, bodyWidth, height() - bodyTop);
}

void MainWindow::updateUnreadCounter() {
	if (!Global::started() || App::quitting()) return;

	auto counter = App::histories().unreadBadge();
	_titleText = (counter > 0) ? qsl("Telegram (%1)").arg(counter) : qsl("Telegram");

	unreadCounterChangedHook();
}

void MainWindow::savePosition(Qt::WindowState state) {
	if (state == Qt::WindowActive) state = windowHandle()->windowState();
	if (state == Qt::WindowMinimized || !positionInited()) return;

	auto savedPosition = cWindowPos();
	auto realPosition = savedPosition;

	if (state == Qt::WindowMaximized) {
		realPosition.maximized = 1;
	} else {
		auto r = geometry();
		realPosition.x = r.x();
		realPosition.y = r.y();
		realPosition.w = r.width() - (_rightColumn ? _rightColumn->width() : 0);
		realPosition.h = r.height();
		realPosition.maximized = 0;
		realPosition.moncrc = 0;
	}
	DEBUG_LOG(("Window Pos: Saving position: %1, %2, %3, %4 (maximized %5)").arg(realPosition.x).arg(realPosition.y).arg(realPosition.w).arg(realPosition.h).arg(Logs::b(realPosition.maximized)));

	auto centerX = realPosition.x + realPosition.w / 2;
	auto centerY = realPosition.y + realPosition.h / 2;
	int minDelta = 0;
	QScreen *chosen = nullptr;
	auto screens = QGuiApplication::screens();
	for (auto screen : QGuiApplication::screens()) {
		auto delta = (screen->geometry().center() - QPoint(centerX, centerY)).manhattanLength();
		if (!chosen || delta < minDelta) {
			minDelta = delta;
			chosen = screen;
		}
	}
	if (chosen) {
		auto screenGeometry = chosen->geometry();
		DEBUG_LOG(("Window Pos: Screen found, geometry: %1, %2, %3, %4").arg(screenGeometry.x()).arg(screenGeometry.y()).arg(screenGeometry.width()).arg(screenGeometry.height()));
		realPosition.x -= screenGeometry.x();
		realPosition.y -= screenGeometry.y();
		realPosition.moncrc = screenNameChecksum(chosen->name());
	}

	if (realPosition.w >= st::windowMinWidth && realPosition.h >= st::windowMinHeight) {
		if (realPosition.x != savedPosition.x
			|| realPosition.y != savedPosition.y
			|| realPosition.w != savedPosition.w
			|| realPosition.h != savedPosition.h
			|| realPosition.moncrc != savedPosition.moncrc
			|| realPosition.maximized != savedPosition.maximized) {
			DEBUG_LOG(("Window Pos: Writing: %1, %2, %3, %4 (maximized %5)").arg(realPosition.x).arg(realPosition.y).arg(realPosition.w).arg(realPosition.h).arg(Logs::b(realPosition.maximized)));
			cSetWindowPos(realPosition);
			Local::writeSettings();
		}
	}
}

bool MainWindow::minimizeToTray() {
	if (App::quitting() || !hasTrayIcon()) return false;

	closeWithoutDestroy();
	updateIsActive(Global::OfflineBlurTimeout());
	updateTrayMenu();
	updateGlobalMenu();
	showTrayTooltip();
	return true;
}

void MainWindow::showRightColumn(object_ptr<TWidget> widget) {
	auto wasWidth = width();
	auto wasRightWidth = _rightColumn ? _rightColumn->width() : 0;
	_rightColumn = std::move(widget);
	if (_rightColumn) {
		_rightColumn->setParent(this);
		_rightColumn->show();
		_rightColumn->setFocus();
	} else if (App::wnd()) {
		App::wnd()->setInnerFocus();
	}
	auto nowRightWidth = _rightColumn ? _rightColumn->width() : 0;
	setMinimumWidth(st::windowMinWidth + nowRightWidth);
	if (!isMaximized()) {
		tryToExtendWidthBy(wasWidth + nowRightWidth - wasRightWidth - width());
	} else {
		updateControlsGeometry();
	}
}

bool MainWindow::canExtendWidthBy(int addToWidth) {
	auto desktop = QDesktopWidget().availableGeometry(this);
	return (width() + addToWidth) <= desktop.width();
}

void MainWindow::tryToExtendWidthBy(int addToWidth) {
	auto desktop = QDesktopWidget().availableGeometry(this);
	auto newWidth = qMin(width() + addToWidth, desktop.width());
	auto newLeft = qMin(x(), desktop.x() + desktop.width() - newWidth);
	if (x() != newLeft || width() != newWidth) {
		setGeometry(newLeft, y(), newWidth, height());
	} else {
		updateControlsGeometry();
	}
}

void MainWindow::documentUpdated(DocumentData *doc) {
	if (!_mediaView || _mediaView->isHidden()) return;
	_mediaView->documentUpdated(doc);
}

void MainWindow::changingMsgId(HistoryItem *row, MsgId newId) {
	if (!_mediaView || _mediaView->isHidden()) return;
	_mediaView->changingMsgId(row, newId);
}

PeerData *MainWindow::ui_getPeerForMouseAction() {
	if (_mediaView && !_mediaView->isHidden()) {
		return _mediaView->ui_getPeerForMouseAction();
	}
	return nullptr;
}

void MainWindow::launchDrag(std::unique_ptr<QMimeData> data) {
	auto weak = QPointer<MainWindow>(this);
	auto drag = std::make_unique<QDrag>(App::wnd());
	drag->setMimeData(data.release());
	drag->exec(Qt::CopyAction);

	// We don't receive mouseReleaseEvent when drag is finished.
	ClickHandler::unpressed();
	if (weak) {
		weak->dragFinished().notify();
	}
}

void MainWindow::checkAuthSession() {
	if (AuthSession::Exists()) {
		_controller = std::make_unique<Window::Controller>(this);
	} else {
		_controller = nullptr;
	}
}

void MainWindow::setInactivePress(bool inactive) {
	_wasInactivePress = inactive;
	if (_wasInactivePress) {
		_inactivePressTimer.callOnce(kInactivePressTimeout);
	} else {
		_inactivePressTimer.cancel();
	}
}

MainWindow::~MainWindow() = default;

} // namespace Window
