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
#include "mediaview.h"
#include "mainwindow.h"

namespace Window {

MainWindow::MainWindow() : QWidget()
, _positionUpdatedTimer(this)
, _body(this)
, _titleText(qsl("Telegram"))
, _isActiveTimer(this) {
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

	_isActiveTimer->setSingleShot(true);
	connect(_isActiveTimer, SIGNAL(timeout()), this, SLOT(updateIsActiveByTimer()));
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
	if (timeout) {
		return _isActiveTimer->start(timeout);
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

void MainWindow::init() {
	initHook();

	_positionUpdatedTimer->setSingleShot(true);
	connect(_positionUpdatedTimer, SIGNAL(timeout()), this, SLOT(savePositionByTimer()));

	updatePalette();

	if ((_title = Platform::CreateTitleWidget(this))) {
		_title->init();
	}

	initSize();
	updateUnreadCounter();
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

	auto pos = cWindowPos();
	auto avail = QDesktopWidget().availableGeometry();
	bool maximized = false;
	auto geom = QRect(avail.x() + (avail.width() - st::windowDefaultWidth) / 2, avail.y() + (avail.height() - st::windowDefaultHeight) / 2, st::windowDefaultWidth, st::windowDefaultHeight);
	if (pos.w && pos.h) {
		for (auto screen : QGuiApplication::screens()) {
			if (pos.moncrc == screenNameChecksum(screen->name())) {
				auto screenGeometry = screen->geometry();
				auto w = screenGeometry.width(), h = screenGeometry.height();
				if (w >= st::windowMinWidth && h >= st::windowMinHeight) {
					if (pos.w > w) pos.w = w;
					if (pos.h > h) pos.h = h;
					pos.x += screenGeometry.x();
					pos.y += screenGeometry.y();
					if (pos.x + st::windowMinWidth <= screenGeometry.x() + screenGeometry.width() &&
						pos.y + st::windowMinHeight <= screenGeometry.y() + screenGeometry.height()) {
						geom = QRect(pos.x, pos.y, pos.w, pos.h);
					}
				}
				break;
			}
		}

		if (pos.y < 0) pos.y = 0;
		maximized = pos.maximized;
	}
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

	auto pos = cWindowPos(), curPos = pos;

	if (state == Qt::WindowMaximized) {
		curPos.maximized = 1;
	} else {
		auto r = geometry();
		curPos.x = r.x();
		curPos.y = r.y();
		curPos.w = r.width() - (_rightColumn ? _rightColumn->width() : 0);
		curPos.h = r.height();
		curPos.maximized = 0;
	}

	int px = curPos.x + curPos.w / 2, py = curPos.y + curPos.h / 2;
	int minDelta = 0;
	QScreen *chosen = 0;
	auto screens = QGuiApplication::screens();
	for (auto screen : QGuiApplication::screens()) {
		auto delta = (screen->geometry().center() - QPoint(px, py)).manhattanLength();
		if (!chosen || delta < minDelta) {
			minDelta = delta;
			chosen = screen;
		}
	}
	if (chosen) {
		curPos.x -= chosen->geometry().x();
		curPos.y -= chosen->geometry().y();
		curPos.moncrc = screenNameChecksum(chosen->name());
	}

	if (curPos.w >= st::windowMinWidth && curPos.h >= st::windowMinHeight) {
		if (curPos.x != pos.x || curPos.y != pos.y || curPos.w != pos.w || curPos.h != pos.h || curPos.moncrc != pos.moncrc || curPos.maximized != pos.maximized) {
			cSetWindowPos(curPos);
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

MainWindow::~MainWindow() = default;

} // namespace Window
