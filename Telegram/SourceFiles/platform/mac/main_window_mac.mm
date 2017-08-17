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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "platform/mac/main_window_mac.h"

#include "styles/style_window.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "application.h"
#include "history/history_widget.h"
#include "history/history_inner_widget.h"
#include "storage/localstorage.h"
#include "window/notifications_manager_default.h"
#include "platform/platform_notifications_manager.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/about_box.h"
#include "lang/lang_keys.h"
#include "platform/mac/mac_utilities.h"

#include <Cocoa/Cocoa.h>
#include <CoreFoundation/CFURL.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hidsystem/ev_keymap.h>
#include <SPMediaKeyTap.h>

namespace {

// When we close a window that is fullscreen we first leave the fullscreen
// mode and after that hide the window. This is a timeout for elaving the
// fullscreen mode, after that we'll hide the window no matter what.
constexpr auto kHideAfterFullscreenTimeoutMs = 3000;

} // namespace

@interface MainWindowObserver : NSObject {
}

- (id) init:(MainWindow::Private*)window;
- (void) activeSpaceDidChange:(NSNotification *)aNotification;
- (void) darkModeChanged:(NSNotification *)aNotification;
- (void) screenIsLocked:(NSNotification *)aNotification;
- (void) screenIsUnlocked:(NSNotification *)aNotification;
- (void) windowWillEnterFullScreen:(NSNotification *)aNotification;
- (void) windowWillExitFullScreen:(NSNotification *)aNotification;

@end // @interface MainWindowObserver

namespace Platform {

class MainWindow::Private {
public:
	Private(MainWindow *window);

	void setWindowBadge(const QString &str);

	void enableShadow(WId winId);

	bool filterNativeEvent(void *event);

	void willEnterFullScreen();
	void willExitFullScreen();

	void initCustomTitle(NSWindow *window, NSView *view);

	bool clipboardHasText();

	~Private();

private:
	MainWindow *_public;
	friend class MainWindow;

	MainWindowObserver *_observer;
	NSPasteboard *_generalPasteboard = nullptr;
	int _generalPasteboardChangeCount = -1;
	bool _generalPasteboardHasText = false;

};

} // namespace Platform

@implementation MainWindowObserver {
	MainWindow::Private *_private;

}

- (id) init:(MainWindow::Private*)window {
	if (self = [super init]) {
		_private = window;
	}
	return self;
}

- (void) activeSpaceDidChange:(NSNotification *)aNotification {
}

- (void) darkModeChanged:(NSNotification *)aNotification {
	Notify::unreadCounterUpdated();
}

- (void) screenIsLocked:(NSNotification *)aNotification {
	Global::SetScreenIsLocked(true);
}

- (void) screenIsUnlocked:(NSNotification *)aNotification {
	Global::SetScreenIsLocked(false);
}

- (void) windowWillEnterFullScreen:(NSNotification *)aNotification {
	_private->willEnterFullScreen();
}

- (void) windowWillExitFullScreen:(NSNotification *)aNotification {
	_private->willExitFullScreen();
}

@end // @implementation MainWindowObserver

namespace Platform {

MainWindow::Private::Private(MainWindow *window)
: _public(window)
, _observer([[MainWindowObserver alloc] init:this]) {
	_generalPasteboard = [NSPasteboard generalPasteboard];

	@autoreleasepool {

	[[[NSWorkspace sharedWorkspace] notificationCenter] addObserver:_observer selector:@selector(activeSpaceDidChange:) name:NSWorkspaceActiveSpaceDidChangeNotification object:nil];
	[[NSDistributedNotificationCenter defaultCenter] addObserver:_observer selector:@selector(darkModeChanged:) name:Q2NSString(strNotificationAboutThemeChange()) object:nil];
	[[NSDistributedNotificationCenter defaultCenter] addObserver:_observer selector:@selector(screenIsLocked:) name:Q2NSString(strNotificationAboutScreenLocked()) object:nil];
	[[NSDistributedNotificationCenter defaultCenter] addObserver:_observer selector:@selector(screenIsUnlocked:) name:Q2NSString(strNotificationAboutScreenUnlocked()) object:nil];

#ifndef OS_MAC_STORE
	// Register defaults for the whitelist of apps that want to use media keys
	[[NSUserDefaults standardUserDefaults] registerDefaults:[NSDictionary dictionaryWithObjectsAndKeys:[SPMediaKeyTap defaultMediaKeyUserBundleIdentifiers], kMediaKeyUsingBundleIdentifiersDefaultsKey, nil]];
#endif // !OS_MAC_STORE

	}
}

void MainWindow::Private::setWindowBadge(const QString &str) {
	@autoreleasepool {

	[[NSApp dockTile] setBadgeLabel:Q2NSString(str)];

	}
}

void MainWindow::Private::initCustomTitle(NSWindow *window, NSView *view) {
	[window setStyleMask:[window styleMask] | NSFullSizeContentViewWindowMask];
	[window setTitlebarAppearsTransparent:YES];
	auto inner = [window contentLayoutRect];
	auto full = [view frame];
	_public->_customTitleHeight = qMax(qRound(full.size.height - inner.size.height), 0);

#ifndef OS_MAC_OLD
	[[NSNotificationCenter defaultCenter] addObserver:_observer selector:@selector(windowWillEnterFullScreen:) name:NSWindowWillEnterFullScreenNotification object:window];
	[[NSNotificationCenter defaultCenter] addObserver:_observer selector:@selector(windowWillExitFullScreen:) name:NSWindowWillExitFullScreenNotification object:window];
#endif // !OS_MAC_OLD
}

bool MainWindow::Private::clipboardHasText() {
	auto currentChangeCount = static_cast<int>([_generalPasteboard changeCount]);
	if (_generalPasteboardChangeCount != currentChangeCount) {
		_generalPasteboardChangeCount = currentChangeCount;
		_generalPasteboardHasText = !Application::clipboard()->text().isEmpty();
	}
	return _generalPasteboardHasText;
}

void MainWindow::Private::willEnterFullScreen() {
	_public->setTitleVisible(false);
}

void MainWindow::Private::willExitFullScreen() {
	_public->setTitleVisible(true);
}

void MainWindow::Private::enableShadow(WId winId) {
//	[[(NSView*)winId window] setStyleMask:NSBorderlessWindowMask];
//	[[(NSView*)winId window] setHasShadow:YES];
}

bool MainWindow::Private::filterNativeEvent(void *event) {
	NSEvent *e = static_cast<NSEvent*>(event);
	if (e && [e type] == NSSystemDefined && [e subtype] == SPSystemDefinedEventMediaKeys) {
#ifndef OS_MAC_STORE
		// If event tap is not installed, handle events that reach the app instead
		if (![SPMediaKeyTap usesGlobalMediaKeyTap]) {
			return objc_handleMediaKeyEvent(e);
		}
#else // !OS_MAC_STORE
		return objc_handleMediaKeyEvent(e);
#endif // else for !OS_MAC_STORE
	}
	return false;
}

MainWindow::Private::~Private() {
	[_observer release];
}

MainWindow::MainWindow()
: _private(std::make_unique<Private>(this)) {
	trayImg = st::macTrayIcon.instance(QColor(0, 0, 0, 180), dbisOne);
	trayImgSel = st::macTrayIcon.instance(QColor(255, 255, 255), dbisOne);

	_hideAfterFullScreenTimer.setCallback([this] { hideAndDeactivate(); });
}

void MainWindow::closeWithoutDestroy() {
	NSWindow *nsWindow = [reinterpret_cast<NSView*>(winId()) window];

	auto isFullScreen = (([nsWindow styleMask] & NSFullScreenWindowMask) == NSFullScreenWindowMask);
	if (isFullScreen) {
		_hideAfterFullScreenTimer.callOnce(kHideAfterFullscreenTimeoutMs);
		[nsWindow toggleFullScreen:nsWindow];
	} else {
		hideAndDeactivate();
	}
}

void MainWindow::stateChangedHook(Qt::WindowState state) {
	if (_hideAfterFullScreenTimer.isActive()) {
		_hideAfterFullScreenTimer.callOnce(0);
	}
}

void MainWindow::initHook() {
	_customTitleHeight = 0;
	if (auto view = reinterpret_cast<NSView*>(winId())) {
		if (auto window = [view window]) {
			if ([window respondsToSelector:@selector(contentLayoutRect)]
				&& [window respondsToSelector:@selector(setTitlebarAppearsTransparent:)]) {
				_private->initCustomTitle(window, view);
			}
		}
	}
}

void MainWindow::updateWindowIcon() {
}

void MainWindow::titleVisibilityChangedHook() {
	updateTitleCounter();
}

void MainWindow::hideAndDeactivate() {
	hide();
}

QImage MainWindow::psTrayIcon(bool selected) const {
	return selected ? trayImgSel : trayImg;
}

void MainWindow::psShowTrayMenu() {
}

void MainWindow::psTrayMenuUpdated() {
}

void MainWindow::psSetupTrayIcon() {
	if (!trayIcon) {
		trayIcon = new QSystemTrayIcon(this);

		QIcon icon(QPixmap::fromImage(psTrayIcon(), Qt::ColorOnly));
		icon.addPixmap(QPixmap::fromImage(psTrayIcon(true), Qt::ColorOnly), QIcon::Selected);

		trayIcon->setIcon(icon);
		trayIcon->setToolTip(str_const_toString(AppName));
		connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(toggleTray(QSystemTrayIcon::ActivationReason)), Qt::UniqueConnection);
		App::wnd()->updateTrayMenu();
	}
	updateIconCounters();

	trayIcon->show();
}

void MainWindow::workmodeUpdated(DBIWorkMode mode) {
	psSetupTrayIcon();
	if (mode == dbiwmWindowOnly) {
		if (trayIcon) {
			trayIcon->setContextMenu(0);
			delete trayIcon;
			trayIcon = nullptr;
		}
	}
}

void _placeCounter(QImage &img, int size, int count, style::color bg, style::color color) {
	if (!count) return;
	auto savedRatio = img.devicePixelRatio();
	img.setDevicePixelRatio(1.);

	{
		Painter p(&img);
		PainterHighQualityEnabler hq(p);

		auto cnt = (count < 100) ? QString("%1").arg(count) : QString("..%1").arg(count % 100, 2, 10, QChar('0'));
		auto cntSize = cnt.size();

		p.setBrush(bg);
		p.setPen(Qt::NoPen);
		int32 fontSize, skip;
		if (size == 22) {
			skip = 1;
			fontSize = 8;
		} else {
			skip = 2;
			fontSize = 16;
		}
		style::font f(fontSize, 0, 0);
		int32 w = f->width(cnt), d, r;
		if (size == 22) {
			d = (cntSize < 2) ? 3 : 2;
			r = (cntSize < 2) ? 6 : 5;
		} else {
			d = (cntSize < 2) ? 6 : 5;
			r = (cntSize < 2) ? 9 : 11;
		}
		p.drawRoundedRect(QRect(size - w - d * 2 - skip, size - f->height - skip, w + d * 2, f->height), r, r);

		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setFont(f);
		p.setPen(color);
		p.drawText(size - w - d - skip, size - f->height + f->ascent - skip, cnt);
	}
	img.setDevicePixelRatio(savedRatio);
}

void MainWindow::updateTitleCounter() {
	setWindowTitle(titleVisible() ? QString() : titleText());
}

void MainWindow::unreadCounterChangedHook() {
	updateTitleCounter();
	updateIconCounters();
}

void MainWindow::updateIconCounters() {
	auto counter = App::histories().unreadBadge();

	QString cnt = (counter < 1000) ? QString("%1").arg(counter) : QString("..%1").arg(counter % 100, 2, 10, QChar('0'));
	_private->setWindowBadge(counter ? cnt : QString());

	if (trayIcon) {
		bool muted = App::histories().unreadOnlyMuted();
		bool dm = objc_darkMode();

		auto &bg = (muted ? st::trayCounterBgMute : st::trayCounterBg);
		QIcon icon;
		QImage img(psTrayIcon(dm)), imgsel(psTrayIcon(true));
		img.detach();
		imgsel.detach();
		int32 size = cRetina() ? 44 : 22;
		_placeCounter(img, size, counter, bg, (dm && muted) ? st::trayCounterFgMacInvert : st::trayCounterFg);
		_placeCounter(imgsel, size, counter, st::trayCounterBgMacInvert, st::trayCounterFgMacInvert);
		icon.addPixmap(App::pixmapFromImageInPlace(std::move(img)));
		icon.addPixmap(App::pixmapFromImageInPlace(std::move(imgsel)), QIcon::Selected);
		trayIcon->setIcon(icon);
	}
}

void MainWindow::psFirstShow() {
	psUpdateMargins();

	bool showShadows = true;

	show();
	_private->enableShadow(winId());
	if (cWindowPos().maximized) {
		DEBUG_LOG(("Window Pos: First show, setting maximized."));
		setWindowState(Qt::WindowMaximized);
	}

	if ((cLaunchMode() == LaunchModeAutoStart && cStartMinimized()) || cStartInTray()) {
		setWindowState(Qt::WindowMinimized);
		if (Global::WorkMode().value() == dbiwmTrayOnly || Global::WorkMode().value() == dbiwmWindowAndTray) {
			hide();
		} else {
			show();
		}
		showShadows = false;
	} else {
		show();
	}

	setPositionInited();

	createGlobalMenu();
}

void MainWindow::createGlobalMenu() {
	auto main = psMainMenu.addMenu(qsl("Telegram"));
	auto about = main->addAction(lng_mac_menu_about_telegram(lt_telegram, qsl("Telegram")));
	connect(about, &QAction::triggered, about, [] {
		if (App::wnd() && App::wnd()->isHidden()) App::wnd()->showFromTray();
		Ui::show(Box<AboutBox>());
	});
	about->setMenuRole(QAction::AboutQtRole);

	main->addSeparator();
	QAction *prefs = main->addAction(lang(lng_mac_menu_preferences), App::wnd(), SLOT(showSettings()), QKeySequence(Qt::ControlModifier | Qt::Key_Comma));
	prefs->setMenuRole(QAction::PreferencesRole);

	QMenu *file = psMainMenu.addMenu(lang(lng_mac_menu_file));
	psLogout = file->addAction(lang(lng_mac_menu_logout), App::wnd(), SLOT(onLogout()));

	QMenu *edit = psMainMenu.addMenu(lang(lng_mac_menu_edit));
	psUndo = edit->addAction(lang(lng_mac_menu_undo), this, SLOT(psMacUndo()), QKeySequence::Undo);
	psRedo = edit->addAction(lang(lng_mac_menu_redo), this, SLOT(psMacRedo()), QKeySequence::Redo);
	edit->addSeparator();
	psCut = edit->addAction(lang(lng_mac_menu_cut), this, SLOT(psMacCut()), QKeySequence::Cut);
	psCopy = edit->addAction(lang(lng_mac_menu_copy), this, SLOT(psMacCopy()), QKeySequence::Copy);
	psPaste = edit->addAction(lang(lng_mac_menu_paste), this, SLOT(psMacPaste()), QKeySequence::Paste);
	psDelete = edit->addAction(lang(lng_mac_menu_delete), this, SLOT(psMacDelete()), QKeySequence(Qt::ControlModifier | Qt::Key_Backspace));
	edit->addSeparator();
	psSelectAll = edit->addAction(lang(lng_mac_menu_select_all), this, SLOT(psMacSelectAll()), QKeySequence::SelectAll);

	QMenu *window = psMainMenu.addMenu(lang(lng_mac_menu_window));
	psContacts = window->addAction(lang(lng_mac_menu_contacts));
	connect(psContacts, &QAction::triggered, psContacts, [] {
		if (App::wnd() && App::wnd()->isHidden()) App::wnd()->showFromTray();

		if (!App::self()) return;
		Ui::show(Box<PeerListBox>(std::make_unique<ContactsBoxController>(), [](not_null<PeerListBox*> box) {
			box->addButton(langFactory(lng_close), [box] { box->closeBox(); });
			box->addLeftButton(langFactory(lng_profile_add_contact), [] { App::wnd()->onShowAddContact(); });
		}));
	});
	psAddContact = window->addAction(lang(lng_mac_menu_add_contact), App::wnd(), SLOT(onShowAddContact()));
	window->addSeparator();
	psNewGroup = window->addAction(lang(lng_mac_menu_new_group), App::wnd(), SLOT(onShowNewGroup()));
	psNewChannel = window->addAction(lang(lng_mac_menu_new_channel), App::wnd(), SLOT(onShowNewChannel()));
	window->addSeparator();
	psShowTelegram = window->addAction(lang(lng_mac_menu_show), App::wnd(), SLOT(showFromTray()));

	updateGlobalMenu();
}

namespace {
	void _sendKeySequence(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
		QWidget *focused = QApplication::focusWidget();
		if (qobject_cast<QLineEdit*>(focused) || qobject_cast<QTextEdit*>(focused) || qobject_cast<HistoryInner*>(focused)) {
			QApplication::postEvent(focused, new QKeyEvent(QEvent::KeyPress, key, modifiers));
			QApplication::postEvent(focused, new QKeyEvent(QEvent::KeyRelease, key, modifiers));
		}
	}
	void _forceDisabled(QAction *action, bool disabled) {
		if (action->isEnabled()) {
			if (disabled) action->setDisabled(true);
		} else if (!disabled) {
			action->setDisabled(false);
		}
	}
}

void MainWindow::psMacUndo() {
	_sendKeySequence(Qt::Key_Z, Qt::ControlModifier);
}

void MainWindow::psMacRedo() {
	_sendKeySequence(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
}

void MainWindow::psMacCut() {
	_sendKeySequence(Qt::Key_X, Qt::ControlModifier);
}

void MainWindow::psMacCopy() {
	_sendKeySequence(Qt::Key_C, Qt::ControlModifier);
}

void MainWindow::psMacPaste() {
	_sendKeySequence(Qt::Key_V, Qt::ControlModifier);
}

void MainWindow::psMacDelete() {
	_sendKeySequence(Qt::Key_Delete);
}

void MainWindow::psMacSelectAll() {
	_sendKeySequence(Qt::Key_A, Qt::ControlModifier);
}

void MainWindow::psInitSysMenu() {
}

void MainWindow::psUpdateMargins() {
}

void MainWindow::updateGlobalMenuHook() {
	if (!App::wnd() || !positionInited()) return;

	auto focused = QApplication::focusWidget();
	bool isLogged = !!App::self(), canUndo = false, canRedo = false, canCut = false, canCopy = false, canPaste = false, canDelete = false, canSelectAll = false;
	auto clipboardHasText = _private->clipboardHasText();
	if (auto edit = qobject_cast<QLineEdit*>(focused)) {
		canCut = canCopy = canDelete = edit->hasSelectedText();
		canSelectAll = !edit->text().isEmpty();
		canUndo = edit->isUndoAvailable();
		canRedo = edit->isRedoAvailable();
		canPaste = clipboardHasText;
	} else if (auto edit = qobject_cast<QTextEdit*>(focused)) {
		canCut = canCopy = canDelete = edit->textCursor().hasSelection();
		canSelectAll = !edit->document()->isEmpty();
		canUndo = edit->document()->isUndoAvailable();
		canRedo = edit->document()->isRedoAvailable();
		canPaste = clipboardHasText;
	} else if (auto list = qobject_cast<HistoryInner*>(focused)) {
		canCopy = list->canCopySelected();
		canDelete = list->canDeleteSelected();
	}
	App::wnd()->updateIsActive(0);
	_forceDisabled(psLogout, !isLogged && !App::passcoded());
	_forceDisabled(psUndo, !canUndo);
	_forceDisabled(psRedo, !canRedo);
	_forceDisabled(psCut, !canCut);
	_forceDisabled(psCopy, !canCopy);
	_forceDisabled(psPaste, !canPaste);
	_forceDisabled(psDelete, !canDelete);
	_forceDisabled(psSelectAll, !canSelectAll);
	_forceDisabled(psContacts, !isLogged || App::passcoded());
	_forceDisabled(psAddContact, !isLogged || App::passcoded());
	_forceDisabled(psNewGroup, !isLogged || App::passcoded());
	_forceDisabled(psNewChannel, !isLogged || App::passcoded());
	_forceDisabled(psShowTelegram, App::wnd()->isActive());
}

bool MainWindow::psFilterNativeEvent(void *event) {
	return _private->filterNativeEvent(event);
}

bool MainWindow::eventFilter(QObject *obj, QEvent *evt) {
	QEvent::Type t = evt->type();
	if (t == QEvent::FocusIn || t == QEvent::FocusOut) {
		if (qobject_cast<QLineEdit*>(obj) || qobject_cast<QTextEdit*>(obj) || qobject_cast<HistoryInner*>(obj)) {
			updateGlobalMenu();
		}
	}
	return Window::MainWindow::eventFilter(obj, evt);
}

MainWindow::~MainWindow() {
}

} // namespace
