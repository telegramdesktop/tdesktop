/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/specific_mac_p.h"

#include "mainwindow.h"
#include "mainwidget.h"
#include "core/sandbox.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/crash_reports.h"
#include "storage/localstorage.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "window/window_controller.h"
#include "base/platform/mac/base_utilities_mac.h"
#include "base/platform/base_platform_info.h"
#include "lang/lang_keys.h"
#include "base/timer.h"
#include "styles/style_window.h"
#include "platform/platform_specific.h"

#include <QtGui/QWindow>
#include <QtWidgets/QApplication>
#if __has_include(<QtCore/QOperatingSystemVersion>)
#include <QtCore/QOperatingSystemVersion>
#endif // __has_include(<QtCore/QOperatingSystemVersion>)
#include <Cocoa/Cocoa.h>
#include <CoreFoundation/CFURL.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hidsystem/ev_keymap.h>

using Platform::Q2NSString;
using Platform::NS2QString;

namespace {

constexpr auto kIgnoreActivationTimeoutMs = 500;

NSMenuItem *CreateMenuItem(
		QString title,
		rpl::lifetime &lifetime,
		Fn<void()> callback,
		bool enabled = true) {
	id block = [^{
		Core::Sandbox::Instance().customEnterFromEventLoop(callback);
	} copy];

	NSMenuItem *item = [[NSMenuItem alloc]
		initWithTitle:Q2NSString(title)
		action:@selector(invoke)
		keyEquivalent:@""];
	[item setTarget:block];
	[item setEnabled:enabled];

	lifetime.add([=] {
		[block release];
	});
	return [item autorelease];
}

} // namespace

@interface RpMenu : NSMenu {
}

- (rpl::lifetime &) lifetime;

@end // @interface Menu

@implementation RpMenu {
	rpl::lifetime _lifetime;
}

- (rpl::lifetime &) lifetime {
	return _lifetime;
}

@end // @implementation Menu

@interface qVisualize : NSObject {
}

+ (id)str:(const QString &)str;
- (id)initWithString:(const QString &)str;

+ (id)bytearr:(const QByteArray &)arr;
- (id)initWithByteArray:(const QByteArray &)arr;

- (id)debugQuickLookObject;

@end // @interface qVisualize

@implementation qVisualize {
	NSString *value;

}

+ (id)bytearr:(const QByteArray &)arr {
	return [[qVisualize alloc] initWithByteArray:arr];
}
- (id)initWithByteArray:(const QByteArray &)arr {
	if (self = [super init]) {
		value = [NSString stringWithUTF8String:arr.constData()];
	}
	return self;
}

+ (id)str:(const QString &)str {
	return [[qVisualize alloc] initWithString:str];
}
- (id)initWithString:(const QString &)str {
	if (self = [super init]) {
		value = [NSString stringWithUTF8String:str.toUtf8().constData()];
	}
	return self;
}

- (id)debugQuickLookObject {
	return value;
}

@end // @implementation qVisualize

@interface ApplicationDelegate : NSObject<NSApplicationDelegate> {
}

- (BOOL) applicationShouldHandleReopen:(NSApplication *)theApplication hasVisibleWindows:(BOOL)flag;
- (void) applicationDidBecomeActive:(NSNotification *)aNotification;
- (void) applicationDidResignActive:(NSNotification *)aNotification;
- (void) receiveWakeNote:(NSNotification*)note;

- (void) ignoreApplicationActivationRightNow;

- (NSMenu *) applicationDockMenu:(NSApplication *)sender;

@end // @interface ApplicationDelegate

ApplicationDelegate *_sharedDelegate = nil;

@implementation ApplicationDelegate {
	bool _ignoreActivation;
	base::Timer _ignoreActivationStop;
}

- (BOOL) applicationShouldHandleReopen:(NSApplication *)theApplication hasVisibleWindows:(BOOL)flag {
	if (const auto window = Core::App().activeWindow()) {
		if (window->widget()->isHidden()) {
			window->widget()->showFromTray();
		}
	}
	return YES;
}

- (void) applicationDidFinishLaunching:(NSNotification *)aNotification {
	_ignoreActivation = false;
	_ignoreActivationStop.setCallback([self] {
		_ignoreActivation = false;
	});
}

- (void) applicationDidBecomeActive:(NSNotification *)aNotification {
	Core::Sandbox::Instance().customEnterFromEventLoop([&] {
		if (Core::IsAppLaunched() && !_ignoreActivation) {
			Core::App().handleAppActivated();
			if (auto window = Core::App().activeWindow()) {
				if (window->widget()->isHidden()) {
					window->widget()->showFromTray();
				}
			}
		}
	});
}

- (void) applicationDidResignActive:(NSNotification *)aNotification {
}

- (void) receiveWakeNote:(NSNotification*)aNotification {
	if (!Core::IsAppLaunched()) {
		return;
	}
	Core::Sandbox::Instance().customEnterFromEventLoop([&] {
		Core::App().checkLocalTime();

		LOG(("Audio Info: "
			"-receiveWakeNote: received, scheduling detach from audio device"));
		Media::Audio::ScheduleDetachFromDeviceSafe();

		Core::App().settings().setSystemDarkMode(Platform::IsDarkMode());
	});
}

- (void) ignoreApplicationActivationRightNow {
	_ignoreActivation = true;
	_ignoreActivationStop.callOnce(kIgnoreActivationTimeoutMs);
}

- (NSMenu *) applicationDockMenu:(NSApplication *)sender {
	RpMenu* dockMenu = [[[RpMenu alloc] initWithTitle: @""] autorelease];
	[dockMenu setAutoenablesItems:false];

	auto notifyCallback = [] {
		auto &settings = Core::App().settings();
		settings.setDesktopNotify(!settings.desktopNotify());
	};
	[dockMenu addItem:CreateMenuItem(
		Core::App().settings().desktopNotify()
			? tr::lng_disable_notifications_from_tray(tr::now)
			: tr::lng_enable_notifications_from_tray(tr::now),
		[dockMenu lifetime],
		std::move(notifyCallback))];

	using namespace Media::Player;
	const auto state = instance()->getState(instance()->getActiveType());
	if (!IsStoppedOrStopping(state.state)) {
		[dockMenu addItem:[NSMenuItem separatorItem]];
		[dockMenu addItem:CreateMenuItem(
			tr::lng_mac_menu_player_previous(tr::now),
			[dockMenu lifetime],
			[] { instance()->previous(); },
			instance()->previousAvailable(instance()->getActiveType()))];
		[dockMenu addItem:CreateMenuItem(
			IsPausedOrPausing(state.state)
				? tr::lng_mac_menu_player_resume(tr::now)
				: tr::lng_mac_menu_player_pause(tr::now),
			[dockMenu lifetime],
			[] { instance()->playPause(); })];
		[dockMenu addItem:CreateMenuItem(
			tr::lng_mac_menu_player_next(tr::now),
			[dockMenu lifetime],
			[] { instance()->next(); },
			instance()->nextAvailable(instance()->getActiveType()))];
	}

	return dockMenu;
}

@end // @implementation ApplicationDelegate

namespace Platform {

void SetApplicationIcon(const QIcon &icon) {
	NSImage *image = nil;
	if (!icon.isNull()) {
		auto pixmap = icon.pixmap(1024, 1024);
		pixmap.setDevicePixelRatio(cRetinaFactor());
		image = Q2NSImage(pixmap.toImage());
	}
	[[NSApplication sharedApplication] setApplicationIconImage:image];
}

} // namespace Platform

void objc_debugShowAlert(const QString &str) {
	@autoreleasepool {

	[[NSAlert alertWithMessageText:@"Debug Message" defaultButton:@"OK" alternateButton:nil otherButton:nil informativeTextWithFormat:@"%@", Q2NSString(str)] runModal];

	}
}

void objc_outputDebugString(const QString &str) {
	@autoreleasepool {

	NSLog(@"%@", Q2NSString(str));

	}
}

void objc_start() {
#if QT_VERSION >= QT_VERSION_CHECK(5, 9, 0)
	// Patch: Fix macOS regression. On 10.14.4, it crashes on GPU switches.
	// See https://bugreports.qt.io/browse/QTCREATORBUG-22215
	const auto version = QOperatingSystemVersion::current();
	if (version.majorVersion() == 10
		&& version.minorVersion() == 14
		&& version.microVersion() == 4) {
		qputenv("QT_MAC_PRO_WEBENGINE_WORKAROUND", "1");
	}
#endif // Qt 5.9.0

	_sharedDelegate = [[ApplicationDelegate alloc] init];
	[[NSApplication sharedApplication] setDelegate:_sharedDelegate];
	[[[NSWorkspace sharedWorkspace] notificationCenter] addObserver: _sharedDelegate
														   selector: @selector(receiveWakeNote:)
															   name: NSWorkspaceDidWakeNotification object: NULL];
}

void objc_ignoreApplicationActivationRightNow() {
	if (_sharedDelegate) {
		[_sharedDelegate ignoreApplicationActivationRightNow];
	}
}

namespace {
	NSURL *_downloadPathUrl = nil;
}

void objc_finish() {
	[_sharedDelegate release];
	_sharedDelegate = nil;
	if (_downloadPathUrl) {
		[_downloadPathUrl stopAccessingSecurityScopedResource];
		_downloadPathUrl = nil;
	}
}

void objc_activateProgram(WId winId) {
	[NSApp activateIgnoringOtherApps:YES];
	if (winId) {
		NSWindow *w = [reinterpret_cast<NSView*>(winId) window];
		[w makeKeyAndOrderFront:NSApp];
	}
}

bool objc_moveFile(const QString &from, const QString &to) {
	@autoreleasepool {

	NSString *f = Q2NSString(from), *t = Q2NSString(to);
	if ([[NSFileManager defaultManager] fileExistsAtPath:t]) {
		NSData *data = [NSData dataWithContentsOfFile:f];
		if (data) {
			if ([data writeToFile:t atomically:YES]) {
				if ([[NSFileManager defaultManager] removeItemAtPath:f error:nil]) {
					return true;
				}
			}
		}
	} else {
		if ([[NSFileManager defaultManager] moveItemAtPath:f toPath:t error:nil]) {
			return true;
		}
	}

	}
	return false;
}

double objc_appkitVersion() {
	return NSAppKitVersionNumber;
}

QString objc_documentsPath() {
	NSURL *url = [[NSFileManager defaultManager] URLForDirectory:NSDocumentDirectory inDomain:NSUserDomainMask appropriateForURL:nil create:YES error:nil];
	if (url) {
		return QString::fromUtf8([[url path] fileSystemRepresentation]) + '/';
	}
	return QString();
}

QString objc_appDataPath() {
	NSURL *url = [[NSFileManager defaultManager] URLForDirectory:NSApplicationSupportDirectory inDomain:NSUserDomainMask appropriateForURL:nil create:YES error:nil];
	if (url) {
		return QString::fromUtf8([[url path] fileSystemRepresentation]) + '/' + AppName.utf16() + '/';
	}
	return QString();
}

QByteArray objc_downloadPathBookmark(const QString &path) {
#ifndef OS_MAC_STORE
	return QByteArray();
#else // OS_MAC_STORE
	NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.toUtf8().constData()] isDirectory:YES];
	if (!url) return QByteArray();

	NSError *error = nil;
	NSData *data = [url bookmarkDataWithOptions:NSURLBookmarkCreationWithSecurityScope includingResourceValuesForKeys:nil relativeToURL:nil error:&error];
	return data ? QByteArray::fromNSData(data) : QByteArray();
#endif // OS_MAC_STORE
}

void objc_downloadPathEnableAccess(const QByteArray &bookmark) {
#ifdef OS_MAC_STORE
	if (bookmark.isEmpty()) return;

	BOOL isStale = NO;
	NSError *error = nil;
	NSURL *url = [NSURL URLByResolvingBookmarkData:bookmark.toNSData() options:NSURLBookmarkResolutionWithSecurityScope relativeToURL:nil bookmarkDataIsStale:&isStale error:&error];
	if (!url) return;

	if ([url startAccessingSecurityScopedResource]) {
		if (_downloadPathUrl) {
			[_downloadPathUrl stopAccessingSecurityScopedResource];
		}
		_downloadPathUrl = [url retain];

		Core::App().settings().setDownloadPath(NS2QString([_downloadPathUrl path]) + '/');
		if (isStale) {
			NSData *data = [_downloadPathUrl bookmarkDataWithOptions:NSURLBookmarkCreationWithSecurityScope includingResourceValuesForKeys:nil relativeToURL:nil error:&error];
			if (data) {
				Core::App().settings().setDownloadPathBookmark(QByteArray::fromNSData(data));
				Local::writeSettings();
			}
		}
	}
#endif // OS_MAC_STORE
}
