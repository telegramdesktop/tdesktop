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
#include "platform/mac/touchbar/mac_touchbar_manager.h"
#include "platform/platform_specific.h"
#include "platform/platform_notifications_manager.h"
#include "base/platform/base_platform_info.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/about_box.h"
#include "lang/lang_keys.h"
#include "base/platform/mac/base_utilities_mac.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/ui_utility.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QTextEdit>
#include <QtGui/QClipboard>
#include <qpa/qwindowsysteminterface.h>

#include <Cocoa/Cocoa.h>
#include <CoreFoundation/CFURL.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hidsystem/ev_keymap.h>

@interface MainWindowObserver : NSObject {
}

- (id) init:(MainWindow::Private*)window;
- (void) activeSpaceDidChange:(NSNotification *)aNotification;
- (void) darkModeChanged:(NSNotification *)aNotification;
- (void) screenIsLocked:(NSNotification *)aNotification;
- (void) screenIsUnlocked:(NSNotification *)aNotification;

@end // @interface MainWindowObserver

namespace Platform {
namespace {

// When we close a window that is fullscreen we first leave the fullscreen
// mode and after that hide the window. This is a timeout for elaving the
// fullscreen mode, after that we'll hide the window no matter what.
constexpr auto kHideAfterFullscreenTimeoutMs = 3000;

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

	bool clipboardHasText();
	~Private();

private:
	not_null<MainWindow*> _public;
	friend class MainWindow;

	NSWindow * __weak _nativeWindow = nil;
	NSView * __weak _nativeView = nil;

	MainWindowObserver *_observer = nullptr;
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
	Core::Sandbox::Instance().customEnterFromEventLoop([&] {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
		QWindowSystemInterface::handleThemeChange();
#else // Qt >= 6.5.0
		Core::App().settings().setSystemDarkMode(Platform::IsDarkMode());
#endif // Qt < 6.5.0
	});
}

- (void) screenIsLocked:(NSNotification *)aNotification {
	Core::App().setScreenIsLocked(true);
}

- (void) screenIsUnlocked:(NSNotification *)aNotification {
	Core::App().setScreenIsLocked(false);
}

@end // @implementation MainWindowObserver

namespace Platform {
namespace {

void SendKeySequence(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
	const auto focused = static_cast<QObject*>(QApplication::focusWidget());
	if (qobject_cast<QLineEdit*>(focused)
		|| qobject_cast<QTextEdit*>(focused)
		|| dynamic_cast<HistoryInner*>(focused)) {
		QKeyEvent pressEvent(QEvent::KeyPress, key, modifiers);
		focused->event(&pressEvent);
		QKeyEvent releaseEvent(QEvent::KeyRelease, key, modifiers);
		focused->event(&releaseEvent);
	}
}

void ForceDisabled(QAction *action, bool disabled) {
	if (action->isEnabled()) {
		if (disabled) action->setDisabled(true);
	} else if (!disabled) {
		action->setDisabled(false);
	}
}

QString strNotificationAboutThemeChange() {
	const uint32 letters[] = { 0x75E86256, 0xD03E11B1, 0x4D92201D, 0xA2144987, 0x99D5B34F, 0x037589C3, 0x38ED2A7C, 0xD2371ABC, 0xDC98BB02, 0x27964E1B, 0x01748AED, 0xE06679F8, 0x761C9580, 0x4F2595BF, 0x6B5FCBF4, 0xE4D9C24E, 0xBA2F6AB5, 0xE6E3FA71, 0xF2CFC255, 0x56A50C19, 0x43AE1239, 0x77CA4254, 0x7D189A89, 0xEA7663EE, 0x84CEB554, 0xA0ADF236, 0x886512D4, 0x7D3FBDAF, 0x85C4BE4F, 0x12C8255E, 0x9AD8BD41, 0xAC154683, 0xB117598B, 0xDFD9F947, 0x63F06C7B, 0x6340DCD6, 0x3AAE6B3E, 0x26CB125A };
	return Platform::MakeFromLetters(letters);
}

QString strNotificationAboutScreenLocked() {
	const uint32 letters[] = { 0x34B47F28, 0x47E95179, 0x73D05C42, 0xB4E2A933, 0x924F22D1, 0x4265D8EA, 0x9E4D2CC2, 0x02E8157B, 0x35BF7525, 0x75901A41, 0xB0400FCC, 0xE801169D, 0x4E04B589, 0xC1CEF054, 0xAB2A7EB0, 0x5C67C4F6, 0xA4E2B954, 0xB35E12D2, 0xD598B22B, 0x4E3B8AAB, 0xBEA5E439, 0xFDA8AA3C, 0x1632DBA8, 0x88FE8965 };
	return Platform::MakeFromLetters(letters);
}

QString strNotificationAboutScreenUnlocked() {
	const uint32 letters[] = { 0xF897900B, 0x19A04630, 0x144DA6DF, 0x643CA7ED, 0x81DDA343, 0x88C6B149, 0x5F9A3A15, 0x31804E13, 0xDF2202B8, 0x9BD1B500, 0x61B92735, 0x7DDF5D43, 0xB74E06C3, 0x16FF1665, 0x9098F702, 0x4461DAF0, 0xA3134FA5, 0x52B01D3C, 0x6BC35769, 0xA7CC945D, 0x8B5327C0, 0x7630B9A0, 0x4E52E3CE, 0xED7765E3, 0xCEB7862D, 0xA06B34F0 };
	return Platform::MakeFromLetters(letters);
}

} // namespace

MainWindow::Private::Private(not_null<MainWindow*> window)
: _public(window)
, _observer([[MainWindowObserver alloc] init:this]) {
	_generalPasteboard = [NSPasteboard generalPasteboard];

	@autoreleasepool {

	[[[NSWorkspace sharedWorkspace] notificationCenter] addObserver:_observer selector:@selector(activeSpaceDidChange:) name:NSWorkspaceActiveSpaceDidChangeNotification object:nil];
	[[NSDistributedNotificationCenter defaultCenter] addObserver:_observer selector:@selector(darkModeChanged:) name:Q2NSString(strNotificationAboutThemeChange()) object:nil];
	[[NSDistributedNotificationCenter defaultCenter] addObserver:_observer selector:@selector(screenIsLocked:) name:Q2NSString(strNotificationAboutScreenLocked()) object:nil];
	[[NSDistributedNotificationCenter defaultCenter] addObserver:_observer selector:@selector(screenIsUnlocked:) name:Q2NSString(strNotificationAboutScreenUnlocked()) object:nil];

	}
}

void MainWindow::Private::setWindowBadge(const QString &str) {
	@autoreleasepool {

	[[NSApp dockTile] setBadgeLabel:Q2NSString(str)];

	}
}

void MainWindow::Private::setNativeWindow(NSWindow *window, NSView *view) {
	_nativeWindow = window;
	_nativeView = view;
	auto inner = [_nativeWindow contentLayoutRect];
	auto full = [_nativeView frame];
	_public->_customTitleHeight = qMax(qRound(full.size.height - inner.size.height), 0);
}

void MainWindow::Private::initTouchBar(
		NSWindow *window,
		not_null<Window::Controller*> controller,
		rpl::producer<bool> canApplyMarkdown) {
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
}

bool MainWindow::Private::clipboardHasText() {
	auto currentChangeCount = static_cast<int>([_generalPasteboard changeCount]);
	if (_generalPasteboardChangeCount != currentChangeCount) {
		_generalPasteboardChangeCount = currentChangeCount;
		_generalPasteboardHasText = !QGuiApplication::clipboard()->text().isEmpty();
	}
	return _generalPasteboardHasText;
}

MainWindow::Private::~Private() {
	[_observer release];
}

MainWindow::MainWindow(not_null<Window::Controller*> controller)
: Window::MainWindow(controller)
, _private(std::make_unique<Private>(this))
, psMainMenu(this) {
	_hideAfterFullScreenTimer.setCallback([this] { hideAndDeactivate(); });
}

void MainWindow::closeWithoutDestroy() {
	NSWindow *nsWindow = [reinterpret_cast<NSView*>(winId()) window];

	auto isFullScreen = (([nsWindow styleMask] & NSWindowStyleMaskFullScreen) == NSWindowStyleMaskFullScreen);
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

void MainWindow::hideAndDeactivate() {
	hide();
}

void MainWindow::unreadCounterChangedHook() {
	updateDockCounter();
}

void MainWindow::updateDockCounter() {
	const auto counter = Core::App().unreadBadge();

	const auto string = !counter
		? QString()
		: (counter < 1000)
		? QString("%1").arg(counter)
		: QString("..%1").arg(counter % 100, 2, 10, QChar('0'));
	_private->setWindowBadge(string);
}

void MainWindow::createGlobalMenu() {
	const auto ensureWindowShown = [=] {
		if (isHidden()) {
			showFromTray();
		}
	};

	auto main = psMainMenu.addMenu(u"Telegram"_q);
	{
		auto callback = [=] {
			ensureWindowShown();
			controller().show(Box<AboutBox>());
		};
		main->addAction(
			tr::lng_mac_menu_about_telegram(
				tr::now,
				lt_telegram,
				u"Telegram"_q),
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
	psUndo = edit->addAction(
		tr::lng_mac_menu_undo(tr::now),
		this,
		[] { SendKeySequence(Qt::Key_Z, Qt::ControlModifier); },
		QKeySequence::Undo);
	psRedo = edit->addAction(
		tr::lng_mac_menu_redo(tr::now),
		this,
		[] {
			SendKeySequence(
				Qt::Key_Z,
				Qt::ControlModifier | Qt::ShiftModifier);
		},
		QKeySequence::Redo);
	edit->addSeparator();
	psCut = edit->addAction(
		tr::lng_mac_menu_cut(tr::now),
		this,
		[] { SendKeySequence(Qt::Key_X, Qt::ControlModifier); },
		QKeySequence::Cut);
	psCopy = edit->addAction(
		tr::lng_mac_menu_copy(tr::now),
		this,
		[] { SendKeySequence(Qt::Key_C, Qt::ControlModifier); },
		QKeySequence::Copy);
	psPaste = edit->addAction(
		tr::lng_mac_menu_paste(tr::now),
		this,
		[] { SendKeySequence(Qt::Key_V, Qt::ControlModifier); },
		QKeySequence::Paste);
	psDelete = edit->addAction(
		tr::lng_mac_menu_delete(tr::now),
		this,
		[] { SendKeySequence(Qt::Key_Delete); },
		QKeySequence(Qt::ControlModifier | Qt::Key_Backspace));

	edit->addSeparator();
	psBold = edit->addAction(
		tr::lng_menu_formatting_bold(tr::now),
		this,
		[] { SendKeySequence(Qt::Key_B, Qt::ControlModifier); },
		QKeySequence::Bold);
	psItalic = edit->addAction(
		tr::lng_menu_formatting_italic(tr::now),
		this,
		[] { SendKeySequence(Qt::Key_I, Qt::ControlModifier); },
		QKeySequence::Italic);
	psUnderline = edit->addAction(
		tr::lng_menu_formatting_underline(tr::now),
		this,
		[] { SendKeySequence(Qt::Key_U, Qt::ControlModifier); },
		QKeySequence::Underline);
	psStrikeOut = edit->addAction(
		tr::lng_menu_formatting_strike_out(tr::now),
		this,
		[] {
			SendKeySequence(
				Qt::Key_X,
				Qt::ControlModifier | Qt::ShiftModifier);
		},
		Ui::kStrikeOutSequence);
	psMonospace = edit->addAction(
		tr::lng_menu_formatting_monospace(tr::now),
		this,
		[] {
			SendKeySequence(
				Qt::Key_M,
				Qt::ControlModifier | Qt::ShiftModifier);
		},
		Ui::kMonospaceSequence);
	psClearFormat = edit->addAction(
		tr::lng_menu_formatting_clear(tr::now),
		this,
		[] {
			SendKeySequence(
				Qt::Key_N,
				Qt::ControlModifier | Qt::ShiftModifier);
		},
		Ui::kClearFormatSequence);

	edit->addSeparator();
	psSelectAll = edit->addAction(
		tr::lng_mac_menu_select_all(tr::now),
		this,
		[] { SendKeySequence(Qt::Key_A, Qt::ControlModifier); },
		QKeySequence::SelectAll);

	edit->addSeparator();
	edit->addAction(
		tr::lng_mac_menu_emoji_and_symbols(tr::now).replace('&', "&&"),
		this,
		[] { [NSApp orderFrontCharacterPalette:nil]; },
		QKeySequence(Qt::MetaModifier | Qt::ControlModifier | Qt::Key_Space));

	QMenu *window = psMainMenu.addMenu(tr::lng_mac_menu_window(tr::now));
	psContacts = window->addAction(tr::lng_mac_menu_contacts(tr::now));
	connect(psContacts, &QAction::triggered, psContacts, crl::guard(this, [=] {
		Expects(sessionController() != nullptr && !controller().locked());

		ensureWindowShown();
		sessionController()->show(PrepareContactsBox(sessionController()));
	}));
	{
		auto callback = [=] {
			Expects(sessionController() != nullptr && !controller().locked());

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
			Expects(sessionController() != nullptr && !controller().locked());

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
			Expects(sessionController() != nullptr && !controller().locked());

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
			if (const auto inputField = dynamic_cast<Ui::InputField*>(
					focused->parentWidget())) {
				canApplyMarkdown = inputField->isMarkdownEnabled();
			}
		}
	} else if (auto list = dynamic_cast<HistoryInner*>(focused)) {
		canCopy = list->canCopySelected();
		canDelete = list->canDeleteSelected();
	}

	_canApplyMarkdown = canApplyMarkdown;

	updateIsActive();
	const auto logged = (sessionController() != nullptr);
	const auto inactive = !logged || controller().locked();
	const auto support = logged
		&& sessionController()->session().supportMode();
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

bool MainWindow::eventFilter(QObject *obj, QEvent *evt) {
	QEvent::Type t = evt->type();
	if (t == QEvent::FocusIn || t == QEvent::FocusOut) {
		if (qobject_cast<QLineEdit*>(obj) || qobject_cast<QTextEdit*>(obj) || dynamic_cast<HistoryInner*>(obj)) {
			updateGlobalMenu();
		}
	}
	return Window::MainWindow::eventFilter(obj, evt);
}

MainWindow::~MainWindow() {
}

} // namespace
