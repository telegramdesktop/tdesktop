/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/main_window_mac.h"

#include "data/data_session.h"
#include "styles/style_window.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "core/application.h"
#include "core/sandbox.h"
#include "main/main_session.h"
#include "history/history.h"
#include "history/history_widget.h"
#include "history/history_inner_widget.h"
#include "main/main_account.h"
#include "main/main_domain.h" // Domain::activeSessionValue
#include "media/player/media_player_instance.h"
#include "media/audio/media_audio.h"
#include "storage/localstorage.h"
#include "window/notifications_manager_default.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "window/themes/window_theme.h"
#include "platform/mac/touchbar/mac_touchbar_manager.h"
#include "platform/platform_specific.h"
#include "platform/platform_notifications_manager.h"
#include "base/platform/base_platform_info.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/about_box.h"
#include "lang/lang_keys.h"
#include "base/platform/mac/base_utilities_mac.h"
#include "ui/widgets/input_fields.h"
#include "facades.h"
#include "app.h"

#include <QtWidgets/QApplication>
#include <QtGui/QClipboard>

#include <Cocoa/Cocoa.h>
#include <CoreFoundation/CFURL.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hidsystem/ev_keymap.h>
#include <SPMediaKeyTap.h>

@interface MainWindowObserver : NSObject {
}

- (id) init:(MainWindow::Private*)window;
- (void) activeSpaceDidChange:(NSNotification *)aNotification;
- (void) darkModeChanged:(NSNotification *)aNotification;
- (void) screenIsLocked:(NSNotification *)aNotification;
- (void) screenIsUnlocked:(NSNotification *)aNotification;
- (void) windowWillEnterFullScreen:(NSNotification *)aNotification;
- (void) windowWillExitFullScreen:(NSNotification *)aNotification;
- (void) windowDidExitFullScreen:(NSNotification *)aNotification;

@end // @interface MainWindowObserver

namespace Platform {
namespace {

// When we close a window that is fullscreen we first leave the fullscreen
// mode and after that hide the window. This is a timeout for elaving the
// fullscreen mode, after that we'll hide the window no matter what.
constexpr auto kHideAfterFullscreenTimeoutMs = 3000;

id FindClassInSubviews(NSView *parent, NSString *className) {
	for (NSView *child in [parent subviews]) {
		if ([child isKindOfClass:NSClassFromString(className)]) {
			return child;
		} else if (id inchild = FindClassInSubviews(child, className)) {
			return inchild;
		}
	}
	return nil;
}

#ifndef OS_MAC_OLD

class LayerCreationChecker : public QObject {
public:
	LayerCreationChecker(NSView * __weak view, Fn<void()> callback)
	: _weakView(view)
	, _callback(std::move(callback)) {
		QCoreApplication::instance()->installEventFilter(this);
	}

protected:
	bool eventFilter(QObject *object, QEvent *event) override {
		if (!_weakView || [_weakView layer] != nullptr) {
			_callback();
		}
		return QObject::eventFilter(object, event);
	}

private:
	NSView * __weak _weakView = nil;
	Fn<void()> _callback;

};

#endif // OS_MAC_OLD

class EventFilter : public QAbstractNativeEventFilter {
public:
	EventFilter(not_null<MainWindow*> window) : _window(window) {
	}

	bool nativeEventFilter(
			const QByteArray &eventType,
			void *message,
			long *result) {
		return Core::Sandbox::Instance().customEnterFromEventLoop([&] {
			return _window->psFilterNativeEvent(message);
		});
	}

private:
	not_null<MainWindow*> _window;

};

[[nodiscard]] QImage TrayIconBack(bool darkMode) {
	static const auto WithColor = [](QColor color) {
		return st::macTrayIcon.instance(color, 100);
	};
	static const auto DarkModeResult = WithColor({ 255, 255, 255 });
	static const auto LightModeResult = WithColor({ 0, 0, 0, 180 });
	auto result = darkMode ? DarkModeResult : LightModeResult;
	result.detach();
	return result;
}

} // namespace

class MainWindow::Private {
public:
	explicit Private(not_null<MainWindow*> window);

	void setNativeWindow(NSWindow *window, NSView *view);
	void initTouchBar(
		NSWindow *window,
		not_null<Window::Controller*> controller,
		rpl::producer<bool> canApplyMarkdown);
	void setWindowBadge(const QString &str);
	void setWindowTitle(const QString &str);
	void updateNativeTitle();

	void enableShadow(WId winId);

	bool filterNativeEvent(void *event);

	void willEnterFullScreen();
	void willExitFullScreen();
	void didExitFullScreen();

	bool clipboardHasText();
	~Private();

private:
	void initCustomTitle();
	void refreshWeakTitleReferences();
	void enforceCorrectStyleMask();

	not_null<MainWindow*> _public;
	friend class MainWindow;

#ifdef OS_MAC_OLD
	NSWindow *_nativeWindow = nil;
	NSView *_nativeView = nil;
#else // OS_MAC_OLD
	NSWindow * __weak _nativeWindow = nil;
	NSView * __weak _nativeView = nil;
	id __weak _nativeTitleWrapWeak = nil;
	id __weak _nativeTitleWeak = nil;
	std::unique_ptr<LayerCreationChecker> _layerCreationChecker;
#endif // !OS_MAC_OLD
	bool _useNativeTitle = false;
	bool _inFullScreen = false;

	MainWindowObserver *_observer = nullptr;
	NSPasteboard *_generalPasteboard = nullptr;
	int _generalPasteboardChangeCount = -1;
	bool _generalPasteboardHasText = false;

	EventFilter _nativeEventFilter;

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
	Core::Sandbox::Instance().customEnterFromEventLoop([&] {
		Core::App().settings().setSystemDarkMode(Platform::IsDarkMode());
	});
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

- (void) windowDidExitFullScreen:(NSNotification *)aNotification {
	_private->didExitFullScreen();
}

@end // @implementation MainWindowObserver

namespace Platform {
namespace {

void SendKeySequence(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
	const auto focused = QApplication::focusWidget();
	if (qobject_cast<QLineEdit*>(focused) || qobject_cast<QTextEdit*>(focused) || qobject_cast<HistoryInner*>(focused)) {
		QApplication::postEvent(focused, new QKeyEvent(QEvent::KeyPress, key, modifiers));
		QApplication::postEvent(focused, new QKeyEvent(QEvent::KeyRelease, key, modifiers));
	}
}

void ForceDisabled(QAction *action, bool disabled) {
	if (action->isEnabled()) {
		if (disabled) action->setDisabled(true);
	} else if (!disabled) {
		action->setDisabled(false);
	}
}

} // namespace

MainWindow::Private::Private(not_null<MainWindow*> window)
: _public(window)
, _observer([[MainWindowObserver alloc] init:this])
, _nativeEventFilter(window) {
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

void MainWindow::Private::setWindowTitle(const QString &str) {
	_public->setWindowTitle(str);
	updateNativeTitle();
}

void MainWindow::Private::setNativeWindow(NSWindow *window, NSView *view) {
	_nativeWindow = window;
	_nativeView = view;
	initCustomTitle();
}

void MainWindow::Private::initTouchBar(
		NSWindow *window,
		not_null<Window::Controller*> controller,
		rpl::producer<bool> canApplyMarkdown) {
#ifndef OS_OSX
	if (!IsMac10_13OrGreater()) {
		return;
	}
	[NSApplication sharedApplication]
		.automaticCustomizeTouchBarMenuItemEnabled = true;

	[window
		performSelectorOnMainThread:@selector(setTouchBar:)
		withObject:[[[RootTouchBar alloc]
			init:std::move(canApplyMarkdown)
			controller:controller
			domain:(&Core::App().domain())] autorelease]
		waitUntilDone:true];
#endif
}

void MainWindow::Private::initCustomTitle() {
#ifndef OS_MAC_OLD
	if (![_nativeWindow respondsToSelector:@selector(contentLayoutRect)]
		|| ![_nativeWindow respondsToSelector:@selector(setTitlebarAppearsTransparent:)]) {
		return;
	}
	[_nativeWindow setTitlebarAppearsTransparent:YES];

	[[NSNotificationCenter defaultCenter] addObserver:_observer selector:@selector(windowWillEnterFullScreen:) name:NSWindowWillEnterFullScreenNotification object:_nativeWindow];
	[[NSNotificationCenter defaultCenter] addObserver:_observer selector:@selector(windowWillExitFullScreen:) name:NSWindowWillExitFullScreenNotification object:_nativeWindow];
	[[NSNotificationCenter defaultCenter] addObserver:_observer selector:@selector(windowDidExitFullScreen:) name:NSWindowDidExitFullScreenNotification object:_nativeWindow];

	// Qt has bug with layer-backed widgets containing QOpenGLWidgets.
	// See https://bugreports.qt.io/browse/QTBUG-64494
	// Emulate custom title instead (code below).
	//
	// Tried to backport a fix, testing.
	[_nativeWindow setStyleMask:[_nativeWindow styleMask] | NSFullSizeContentViewWindowMask];
	auto inner = [_nativeWindow contentLayoutRect];
	auto full = [_nativeView frame];
	_public->_customTitleHeight = qMax(qRound(full.size.height - inner.size.height), 0);

	// Qt still has some bug with layer-backed widgets containing QOpenGLWidgets.
	// See https://github.com/telegramdesktop/tdesktop/issues/4150
	// Tried to workaround it by catching the first moment we have CALayer created
	// and explicitly setting contentsScale to window->backingScaleFactor there.
	_layerCreationChecker = std::make_unique<LayerCreationChecker>(_nativeView, [=] {
		if (_nativeView && _nativeWindow) {
			if (CALayer *layer = [_nativeView layer]) {
				LOG(("Window Info: Setting layer scale factor to: %1").arg([_nativeWindow backingScaleFactor]));
				[layer setContentsScale: [_nativeWindow backingScaleFactor]];
				_layerCreationChecker = nullptr;
			}
		} else {
			_layerCreationChecker = nullptr;
		}
	});

	// Disabled for now.
	//_useNativeTitle = true;
	//setWindowTitle(qsl("Telegram"));
#endif // !OS_MAC_OLD
}

void MainWindow::Private::refreshWeakTitleReferences() {
	if (!_nativeWindow) {
		return;
	}

#ifndef OS_MAC_OLD
	@autoreleasepool {

	if (NSView *parent = [[_nativeWindow contentView] superview]) {
		if (id titleWrap = FindClassInSubviews(parent, Q2NSString(strTitleWrapClass()))) {
			if ([titleWrap respondsToSelector:@selector(setBackgroundColor:)]) {
				if (id title = FindClassInSubviews(titleWrap, Q2NSString(strTitleClass()))) {
					if ([title respondsToSelector:@selector(setAttributedStringValue:)]) {
						_nativeTitleWrapWeak = titleWrap;
						_nativeTitleWeak = title;
					}
				}
			}
		}
	}

	}
#endif // !OS_MAC_OLD
}

void MainWindow::Private::updateNativeTitle() {
	if (!_useNativeTitle) {
		return;
	}
#ifndef OS_MAC_OLD
	if (!_nativeTitleWrapWeak || !_nativeTitleWeak) {
		refreshWeakTitleReferences();
	}
	if (_nativeTitleWrapWeak && _nativeTitleWeak) {
		@autoreleasepool {

		auto convertColor = [](QColor color) {
			return [NSColor colorWithDeviceRed:color.redF() green:color.greenF() blue:color.blueF() alpha:color.alphaF()];
		};
		auto adjustFg = [](const style::color &st) {
			// Weird thing with NSTextField taking NSAttributedString with
			// NSForegroundColorAttributeName set to colorWithDeviceRed:green:blue
			// with components all equal to 128 - it ignores it and prints black text!
			auto color = st->c;
			return (color.red() == 128 && color.green() == 128 && color.blue() == 128)
				? QColor(129, 129, 129, color.alpha())
				: color;
		};

		auto active = _public->isActiveWindow();
		auto bgColor = (active ? st::titleBgActive : st::titleBg)->c;
		auto fgColor = adjustFg(active ? st::titleFgActive : st::titleFg);

		auto bgConverted = convertColor(bgColor);
		auto fgConverted = convertColor(fgColor);
		[_nativeTitleWrapWeak setBackgroundColor:bgConverted];

		auto title = Q2NSString(_public->windowTitle());
		NSDictionary *attributes = _inFullScreen
			? nil
			: [NSDictionary dictionaryWithObjectsAndKeys: fgConverted, NSForegroundColorAttributeName, bgConverted, NSBackgroundColorAttributeName, nil];
		NSAttributedString *string = [[NSAttributedString alloc] initWithString:title attributes:attributes];
		[_nativeTitleWeak setAttributedStringValue:string];

		}
	}
#endif // !OS_MAC_OLD
}

bool MainWindow::Private::clipboardHasText() {
	auto currentChangeCount = static_cast<int>([_generalPasteboard changeCount]);
	if (_generalPasteboardChangeCount != currentChangeCount) {
		_generalPasteboardChangeCount = currentChangeCount;
		_generalPasteboardHasText = !QGuiApplication::clipboard()->text().isEmpty();
	}
	return _generalPasteboardHasText;
}

void MainWindow::Private::willEnterFullScreen() {
	_inFullScreen = true;
	_public->setTitleVisible(false);
}

void MainWindow::Private::willExitFullScreen() {
	_inFullScreen = false;
	_public->setTitleVisible(true);
	enforceCorrectStyleMask();
}

void MainWindow::Private::didExitFullScreen() {
	enforceCorrectStyleMask();
}

void MainWindow::Private::enforceCorrectStyleMask() {
	if (_nativeWindow && _public->_customTitleHeight > 0) {
		[_nativeWindow setStyleMask:[_nativeWindow styleMask] | NSFullSizeContentViewWindowMask];
	}
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

MainWindow::MainWindow(not_null<Window::Controller*> controller)
: Window::MainWindow(controller)
, _private(std::make_unique<Private>(this)) {
	QCoreApplication::instance()->installNativeEventFilter(
		&_private->_nativeEventFilter);

#ifndef OS_MAC_OLD
	auto forceOpenGL = std::make_unique<QOpenGLWidget>(this);
#endif // !OS_MAC_OLD

	_hideAfterFullScreenTimer.setCallback([this] { hideAndDeactivate(); });

	subscribe(Window::Theme::Background(), [this](const Window::Theme::BackgroundUpdate &data) {
		if (data.paletteChanged()) {
			_private->updateNativeTitle();
		}
	});
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

void MainWindow::handleActiveChangedHook() {
	InvokeQueued(this, [this] { _private->updateNativeTitle(); });

	// On macOS just remove trayIcon menu if the window is not active.
	// So we will activate the window on click instead of showing the menu.
	if (isActiveForTrayMenu()) {
		if (trayIcon
			&& trayIconMenu
			&& trayIcon->contextMenu() != trayIconMenu) {
			trayIcon->setContextMenu(trayIconMenu);
		}
	} else if (trayIcon) {
		trayIcon->setContextMenu(nullptr);
	}
}

void MainWindow::initHook() {
	_customTitleHeight = 0;
	if (auto view = reinterpret_cast<NSView*>(winId())) {
		if (auto window = [view window]) {
			_private->setNativeWindow(window, view);
			_private->initTouchBar(
				window,
				&controller(),
				_canApplyMarkdown.changes());
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

void MainWindow::psShowTrayMenu() {
}

void MainWindow::psTrayMenuUpdated() {
}

void MainWindow::psSetupTrayIcon() {
	if (!trayIcon) {
		trayIcon = new QSystemTrayIcon(this);
		trayIcon->setIcon(generateIconForTray(
			Core::App().unreadBadge(),
			Core::App().unreadBadgeMuted()));
		if (isActiveForTrayMenu()) {
			trayIcon->setContextMenu(trayIconMenu);
		} else {
			trayIcon->setContextMenu(nullptr);
		}
		attachToTrayIcon(trayIcon);
	} else {
		updateIconCounters();
	}

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
	_private->setWindowTitle(titleVisible() ? QString() : titleText());
}

void MainWindow::unreadCounterChangedHook() {
	updateTitleCounter();
	updateIconCounters();
}

void MainWindow::updateIconCounters() {
	const auto counter = Core::App().unreadBadge();
	const auto muted = Core::App().unreadBadgeMuted();

	const auto string = !counter
		? QString()
		: (counter < 1000)
		? QString("%1").arg(counter)
		: QString("..%1").arg(counter % 100, 2, 10, QChar('0'));
	_private->setWindowBadge(string);

	if (trayIcon) {
		trayIcon->setIcon(generateIconForTray(counter, muted));
	}
}

QIcon MainWindow::generateIconForTray(int counter, bool muted) const {
	auto result = QIcon();
	auto lightMode = TrayIconBack(false);
	auto darkMode = TrayIconBack(true);
	auto lightModeActive = darkMode;
	auto darkModeActive = darkMode;
	lightModeActive.detach();
	darkModeActive.detach();
	const auto size = 22 * cIntRetinaFactor();
	const auto &bg = (muted ? st::trayCounterBgMute : st::trayCounterBg);
	_placeCounter(lightMode, size, counter, bg, st::trayCounterFg);
	_placeCounter(darkMode, size, counter, bg, muted ? st::trayCounterFgMacInvert : st::trayCounterFg);
	_placeCounter(lightModeActive, size, counter, st::trayCounterBgMacInvert, st::trayCounterFgMacInvert);
	_placeCounter(darkModeActive, size, counter, st::trayCounterBgMacInvert, st::trayCounterFgMacInvert);
	result.addPixmap(App::pixmapFromImageInPlace(std::move(lightMode)), QIcon::Normal, QIcon::Off);
	result.addPixmap(App::pixmapFromImageInPlace(std::move(darkMode)), QIcon::Normal, QIcon::On);
	result.addPixmap(App::pixmapFromImageInPlace(std::move(lightModeActive)), QIcon::Active, QIcon::Off);
	result.addPixmap(App::pixmapFromImageInPlace(std::move(darkModeActive)), QIcon::Active, QIcon::On);
	return result;
}

void MainWindow::initShadows() {
	_private->enableShadow(winId());
}

void MainWindow::createGlobalMenu() {
	const auto ensureWindowShown = [=] {
		if (isHidden()) {
			showFromTray();
		}
	};

	auto main = psMainMenu.addMenu(qsl("Telegram"));
	{
		auto callback = [=] {
			ensureWindowShown();
			controller().show(Box<AboutBox>());
		};
		main->addAction(
			tr::lng_mac_menu_about_telegram(
				tr::now,
				lt_telegram,
				qsl("Telegram")),
			std::move(callback))
		->setMenuRole(QAction::AboutQtRole);
	}

	main->addSeparator();
	{
		auto callback = [=] {
			ensureWindowShown();
			controller().showSettings();
		};
		main->addAction(
			tr::lng_mac_menu_preferences(tr::now),
			this,
			std::move(callback),
			QKeySequence(Qt::ControlModifier | Qt::Key_Comma))
		->setMenuRole(QAction::PreferencesRole);
	}

	QMenu *file = psMainMenu.addMenu(tr::lng_mac_menu_file(tr::now));
	{
		auto callback = [=] {
			ensureWindowShown();
			controller().showLogoutConfirmation();
		};
		psLogout = file->addAction(
			tr::lng_mac_menu_logout(tr::now),
			this,
			std::move(callback));
	}

	QMenu *edit = psMainMenu.addMenu(tr::lng_mac_menu_edit(tr::now));
	psUndo = edit->addAction(tr::lng_mac_menu_undo(tr::now), this, SLOT(psMacUndo()), QKeySequence::Undo);
	psRedo = edit->addAction(tr::lng_mac_menu_redo(tr::now), this, SLOT(psMacRedo()), QKeySequence::Redo);
	edit->addSeparator();
	psCut = edit->addAction(tr::lng_mac_menu_cut(tr::now), this, SLOT(psMacCut()), QKeySequence::Cut);
	psCopy = edit->addAction(tr::lng_mac_menu_copy(tr::now), this, SLOT(psMacCopy()), QKeySequence::Copy);
	psPaste = edit->addAction(tr::lng_mac_menu_paste(tr::now), this, SLOT(psMacPaste()), QKeySequence::Paste);
	psDelete = edit->addAction(tr::lng_mac_menu_delete(tr::now), this, SLOT(psMacDelete()), QKeySequence(Qt::ControlModifier | Qt::Key_Backspace));

	edit->addSeparator();
	psBold = edit->addAction(tr::lng_menu_formatting_bold(tr::now), this, SLOT(psMacBold()), QKeySequence::Bold);
	psItalic = edit->addAction(tr::lng_menu_formatting_italic(tr::now), this, SLOT(psMacItalic()), QKeySequence::Italic);
	psUnderline = edit->addAction(tr::lng_menu_formatting_underline(tr::now), this, SLOT(psMacUnderline()), QKeySequence::Underline);
	psStrikeOut = edit->addAction(tr::lng_menu_formatting_strike_out(tr::now), this, SLOT(psMacStrikeOut()), Ui::kStrikeOutSequence);
	psMonospace = edit->addAction(tr::lng_menu_formatting_monospace(tr::now), this, SLOT(psMacMonospace()), Ui::kMonospaceSequence);
	psClearFormat = edit->addAction(tr::lng_menu_formatting_clear(tr::now), this, SLOT(psMacClearFormat()), Ui::kClearFormatSequence);

	edit->addSeparator();
	psSelectAll = edit->addAction(tr::lng_mac_menu_select_all(tr::now), this, SLOT(psMacSelectAll()), QKeySequence::SelectAll);

	edit->addSeparator();
	edit->addAction(tr::lng_mac_menu_emoji_and_symbols(tr::now).replace('&', "&&"), this, SLOT(psMacEmojiAndSymbols()), QKeySequence(Qt::MetaModifier | Qt::ControlModifier | Qt::Key_Space));

	QMenu *window = psMainMenu.addMenu(tr::lng_mac_menu_window(tr::now));
	psContacts = window->addAction(tr::lng_mac_menu_contacts(tr::now));
	connect(psContacts, &QAction::triggered, psContacts, crl::guard(this, [=] {
		if (isHidden()) {
			showFromTray();
		}
		if (!sessionController()) {
			return;
		}
		Ui::show(PrepareContactsBox(sessionController()));
	}));
	{
		auto callback = [=] {
			Expects(sessionController() != nullptr);
			ensureWindowShown();
			sessionController()->showAddContact();
		};
		psAddContact = window->addAction(
			tr::lng_mac_menu_add_contact(tr::now),
			this,
			std::move(callback));
	}
	window->addSeparator();
	{
		auto callback = [=] {
			Expects(sessionController() != nullptr);
			ensureWindowShown();
			sessionController()->showNewGroup();
		};
		psNewGroup = window->addAction(
			tr::lng_mac_menu_new_group(tr::now),
			this,
			std::move(callback));
	}
	{
		auto callback = [=] {
			Expects(sessionController() != nullptr);
			ensureWindowShown();
			sessionController()->showNewChannel();
		};
		psNewChannel = window->addAction(
			tr::lng_mac_menu_new_channel(tr::now),
			this,
			std::move(callback));
	}
	window->addSeparator();
	psShowTelegram = window->addAction(
		tr::lng_mac_menu_show(tr::now),
		this,
		[=] { showFromTray(); });

	updateGlobalMenu();
}

void MainWindow::psMacUndo() {
	SendKeySequence(Qt::Key_Z, Qt::ControlModifier);
}

void MainWindow::psMacRedo() {
	SendKeySequence(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
}

void MainWindow::psMacCut() {
	SendKeySequence(Qt::Key_X, Qt::ControlModifier);
}

void MainWindow::psMacCopy() {
	SendKeySequence(Qt::Key_C, Qt::ControlModifier);
}

void MainWindow::psMacPaste() {
	SendKeySequence(Qt::Key_V, Qt::ControlModifier);
}

void MainWindow::psMacDelete() {
	SendKeySequence(Qt::Key_Delete);
}

void MainWindow::psMacSelectAll() {
	SendKeySequence(Qt::Key_A, Qt::ControlModifier);
}

void MainWindow::psMacEmojiAndSymbols() {
	[NSApp orderFrontCharacterPalette:nil];
}

void MainWindow::psMacBold() {
	SendKeySequence(Qt::Key_B, Qt::ControlModifier);
}

void MainWindow::psMacItalic() {
	SendKeySequence(Qt::Key_I, Qt::ControlModifier);
}

void MainWindow::psMacUnderline() {
	SendKeySequence(Qt::Key_U, Qt::ControlModifier);
}

void MainWindow::psMacStrikeOut() {
	SendKeySequence(Qt::Key_X, Qt::ControlModifier | Qt::ShiftModifier);
}

void MainWindow::psMacMonospace() {
	SendKeySequence(Qt::Key_M, Qt::ControlModifier | Qt::ShiftModifier);
}

void MainWindow::psMacClearFormat() {
	SendKeySequence(Qt::Key_N, Qt::ControlModifier | Qt::ShiftModifier);
}

void MainWindow::updateGlobalMenuHook() {
	if (!positionInited()) {
		return;
	}

	auto focused = QApplication::focusWidget();
	bool canUndo = false, canRedo = false, canCut = false, canCopy = false, canPaste = false, canDelete = false, canSelectAll = false;
	auto clipboardHasText = _private->clipboardHasText();
	auto canApplyMarkdown = false;
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
		if (canCopy) {
			if (const auto inputField = qobject_cast<Ui::InputField*>(
					focused->parentWidget())) {
				canApplyMarkdown = inputField->isMarkdownEnabled();
			}
		}
	} else if (auto list = qobject_cast<HistoryInner*>(focused)) {
		canCopy = list->canCopySelected();
		canDelete = list->canDeleteSelected();
	}

	_canApplyMarkdown = canApplyMarkdown;

	updateIsActive();
	const auto logged = (sessionController() != nullptr);
	const auto inactive = !logged || controller().locked();
	const auto support = logged && account().session().supportMode();
	ForceDisabled(psLogout, !logged && !Core::App().passcodeLocked());
	ForceDisabled(psUndo, !canUndo);
	ForceDisabled(psRedo, !canRedo);
	ForceDisabled(psCut, !canCut);
	ForceDisabled(psCopy, !canCopy);
	ForceDisabled(psPaste, !canPaste);
	ForceDisabled(psDelete, !canDelete);
	ForceDisabled(psSelectAll, !canSelectAll);
	ForceDisabled(psContacts, inactive || support);
	ForceDisabled(psAddContact, inactive);
	ForceDisabled(psNewGroup, inactive || support);
	ForceDisabled(psNewChannel, inactive || support);
	ForceDisabled(psShowTelegram, isActive());

	ForceDisabled(psBold, !canApplyMarkdown);
	ForceDisabled(psItalic, !canApplyMarkdown);
	ForceDisabled(psUnderline, !canApplyMarkdown);
	ForceDisabled(psStrikeOut, !canApplyMarkdown);
	ForceDisabled(psMonospace, !canApplyMarkdown);
	ForceDisabled(psClearFormat, !canApplyMarkdown);
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
