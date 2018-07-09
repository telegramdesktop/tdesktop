/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/main_window.h"

#include "storage/localstorage.h"
#include "platform/platform_window_title.h"
#include "history/history.h"
#include "window/themes/window_theme.h"
#include "window/window_controller.h"
#include "window/window_lock_widgets.h"
#include "boxes/confirm_box.h"
#include "core/click_handler_types.h"
#include "lang/lang_keys.h"
#include "mediaview.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "messenger.h"
#include "mainwindow.h"
#include "styles/style_window.h"
#include "styles/style_boxes.h"

namespace Window {

constexpr auto kInactivePressTimeout = TimeMs(200);
constexpr auto kSaveWindowPositionTimeout = TimeMs(1000);

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

MainWindow::MainWindow()
: _positionUpdatedTimer([=] { savePosition(); })
, _body(this)
, _icon(CreateIcon())
, _titleText(qsl("Telegram")) {
	subscribe(Theme::Background(), [this](const Theme::BackgroundUpdate &data) {
		if (data.paletteChanged()) {
			updatePalette();
		}
	});
	subscribe(Global::RefUnreadCounterUpdate(), [this] { updateUnreadCounter(); });
	subscribe(Global::RefWorkMode(), [this](DBIWorkMode mode) { workmodeUpdated(mode); });
	subscribe(Messenger::Instance().authSessionChanged(), [this] { checkAuthSession(); });
	checkAuthSession();

	Messenger::Instance().termsLockValue(
	) | rpl::start_with_next([=] {
		checkLockByTerms();
	}, lifetime());

	_isActiveTimer.setCallback([this] { updateIsActive(0); });
	_inactivePressTimer.setCallback([this] { setInactivePress(false); });
}

void MainWindow::checkLockByTerms() {
	const auto data = Messenger::Instance().termsLocked();
	if (!data || !AuthSession::Exists()) {
		if (_termsBox) {
			_termsBox->closeBox();
		}
		return;
	}
	Ui::hideSettingsAndLayer(anim::type::instant);
	const auto box = Ui::show(Box<TermsBox>(
		*data,
		langFactory(lng_terms_agree),
		langFactory(lng_terms_decline)));

	box->setCloseByEscape(false);
	box->setCloseByOutsideClick(false);

	const auto id = data->id;
	box->agreeClicks(
	) | rpl::start_with_next([=] {
		const auto mention = box ? box->lastClickedMention() : QString();
		if (AuthSession::Exists()) {
			Auth().api().acceptTerms(id);
			if (!mention.isEmpty()) {
				MentionClickHandler(mention).onClick({});
			}
		}
		Messenger::Instance().unlockTerms();
	}, box->lifetime());

	box->cancelClicks(
	) | rpl::start_with_next([=] {
		showTermsDecline();
	}, box->lifetime());

	connect(box, &QObject::destroyed, [=] {
		crl::on_main(this, [=] { checkLockByTerms(); });
	});

	_termsBox = box;
}

void MainWindow::showTermsDecline() {
	const auto box = Ui::show(
		Box<Window::TermsBox>(
			TextWithEntities{ lang(lng_terms_update_sorry) },
			langFactory(lng_terms_decline_and_delete),
			langFactory(lng_terms_back),
			true),
		LayerOption::KeepOther);

	box->agreeClicks(
	) | rpl::start_with_next([=] {
		if (box) {
			box->closeBox();
		}
		showTermsDelete();
	}, box->lifetime());

	box->cancelClicks(
	) | rpl::start_with_next([=] {
		if (box) {
			box->closeBox();
		}
	}, box->lifetime());
}

void MainWindow::showTermsDelete() {
	const auto box = std::make_shared<QPointer<BoxContent>>();
	*box = Ui::show(
		Box<ConfirmBox>(
			lang(lng_terms_delete_warning),
			lang(lng_terms_delete_now),
			st::attentionBoxButton,
			[=] { Messenger::Instance().termsDeleteNow(); },
			[=] { if (*box) (*box)->closeBox(); }),
		LayerOption::KeepOther);
}

bool MainWindow::hideNoQuit() {
	if (App::quitting()) {
		return false;
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
	Ui::hideLayer(anim::type::instant);
	clearWidgetsHook();
	updateGlobalMenu();
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

void MainWindow::updateWindowIcon() {
	setWindowIcon(_icon);
}

void MainWindow::init() {
	Expects(!windowHandle());

	createWinId();

	initHook();
	updateWindowIcon();

	connect(windowHandle(), &QWindow::activeChanged, this, [this] { handleActiveChanged(); }, Qt::QueuedConnection);
	connect(windowHandle(), &QWindow::windowStateChanged, this, [this](Qt::WindowState state) { handleStateChanged(state); });

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
	if (isActiveWindow()) {
		Messenger::Instance().checkMediaViewActivation();
	}
	App::CallDelayed(1, this, [this] {
		updateTrayMenu();
		handleActiveChangedHook();
	});
}

void MainWindow::updatePalette() {
	Ui::ForceFullRepaint(this);

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
	_positionUpdatedTimer.callOnce(kSaveWindowPositionTimeout);
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

rpl::producer<> MainWindow::leaveEvents() const {
	return _leaveEvents.events();
}

void MainWindow::leaveEventHook(QEvent *e) {
	_leaveEvents.fire({});
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

void MainWindow::reActivateWindow() {
#if defined Q_OS_LINUX32 || defined Q_OS_LINUX64
	const auto reActivate = [=] {
		if (const auto w = App::wnd()) {
			if (auto f = QApplication::focusWidget()) {
				f->clearFocus();
			}
			windowHandle()->requestActivate();
			w->activate();
			if (auto f = QApplication::focusWidget()) {
				f->clearFocus();
			}
			w->setInnerFocus();
		}
	};
	crl::on_main(this, reActivate);
	App::CallDelayed(200, this, reActivate);
#endif // Q_OS_LINUX32 || Q_OS_LINUX64
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

int MainWindow::maximalExtendBy() const {
	auto desktop = QDesktopWidget().availableGeometry(this);
	return std::max(desktop.width() - geometry().width(), 0);
}

bool MainWindow::canExtendNoMove(int extendBy) const {
	auto desktop = QDesktopWidget().availableGeometry(this);
	auto inner = geometry();
	auto innerRight = (inner.x() + inner.width() + extendBy);
	auto desktopRight = (desktop.x() + desktop.width());
	return innerRight <= desktopRight;
}

int MainWindow::tryToExtendWidthBy(int addToWidth) {
	auto desktop = QDesktopWidget().availableGeometry(this);
	auto inner = geometry();
	accumulate_min(
		addToWidth,
		std::max(desktop.width() - inner.width(), 0));
	auto newWidth = inner.width() + addToWidth;
	auto newLeft = std::min(
		inner.x(),
		desktop.x() + desktop.width() - newWidth);
	if (inner.x() != newLeft || inner.width() != newWidth) {
		setGeometry(newLeft, inner.y(), newWidth, inner.height());
	} else {
		updateControlsGeometry();
	}
	return addToWidth;
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
