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
#include "base/platform/mac/base_utilities_mac.h"
#include "base/platform/base_platform_info.h"
#include "lang/lang_keys.h"
#include "base/timer.h"
#include "styles/style_window.h"

#include <QtGui/QWindow>
#include <QtWidgets/QApplication>
#if __has_include(<QtCore/QOperatingSystemVersion>)
#include <QtCore/QOperatingSystemVersion>
#endif // __has_include(<QtCore/QOperatingSystemVersion>)
#include <Cocoa/Cocoa.h>
#include <CoreFoundation/CFURL.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hidsystem/ev_keymap.h>
#include <SPMediaKeyTap.h>

namespace {

constexpr auto kIgnoreActivationTimeoutMs = 500;

} // namespace

using Platform::Q2NSString;
using Platform::NS2QString;

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
- (void) applicationDidFinishLaunching:(NSNotification *)aNotification;
- (void) applicationDidBecomeActive:(NSNotification *)aNotification;
- (void) applicationDidResignActive:(NSNotification *)aNotification;
- (void) receiveWakeNote:(NSNotification*)note;

- (void) setWatchingMediaKeys:(bool)watching;
- (bool) isWatchingMediaKeys;
- (void) mediaKeyTap:(SPMediaKeyTap*)keyTap receivedMediaKeyEvent:(NSEvent*)event;

- (void) ignoreApplicationActivationRightNow;

@end // @interface ApplicationDelegate

ApplicationDelegate *_sharedDelegate = nil;

@implementation ApplicationDelegate {
	SPMediaKeyTap *_keyTap;
	bool _watchingMediaKeys;
	bool _ignoreActivation;
	base::Timer _ignoreActivationStop;
}

- (BOOL) applicationShouldHandleReopen:(NSApplication *)theApplication hasVisibleWindows:(BOOL)flag {
	if (App::wnd() && App::wnd()->isHidden()) App::wnd()->showFromTray();
	return YES;
}

- (void) applicationDidFinishLaunching:(NSNotification *)aNotification {
	_keyTap = nullptr;
	_watchingMediaKeys = false;
	_ignoreActivation = false;
	_ignoreActivationStop.setCallback([self] {
		_ignoreActivation = false;
	});
#ifndef OS_MAC_STORE
	if ([SPMediaKeyTap usesGlobalMediaKeyTap]) {
		if (!Platform::IsMac10_14OrGreater()) {
			_keyTap = [[SPMediaKeyTap alloc] initWithDelegate:self];
		} else {
			// In macOS Mojave it requires accessibility features.
			LOG(("Media key monitoring disabled starting with Mojave."));
		}
	} else {
		LOG(("Media key monitoring disabled"));
	}
#endif // else for !OS_MAC_STORE
}

- (void) applicationDidBecomeActive:(NSNotification *)aNotification {
	Core::Sandbox::Instance().customEnterFromEventLoop([&] {
		if (Core::IsAppLaunched() && !_ignoreActivation) {
			Core::App().handleAppActivated();
			if (auto window = App::wnd()) {
				if (window->isHidden()) {
					window->showFromTray();
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
	Core::App().checkLocalTime();

	LOG(("Audio Info: "
		"-receiveWakeNote: received, scheduling detach from audio device"));
	Media::Audio::ScheduleDetachFromDeviceSafe();
}

- (void) setWatchingMediaKeys:(bool)watching {
	if (_watchingMediaKeys != watching) {
		_watchingMediaKeys = watching;
		if (_keyTap) {
#ifndef OS_MAC_STORE
			if (_watchingMediaKeys) {
				[_keyTap startWatchingMediaKeys];
			} else {
				[_keyTap stopWatchingMediaKeys];
			}
#endif // else for !OS_MAC_STORE
		}
	}
}

- (bool) isWatchingMediaKeys {
	return _watchingMediaKeys;
}

- (void) mediaKeyTap:(SPMediaKeyTap*)keyTap receivedMediaKeyEvent:(NSEvent*)e {
	Core::Sandbox::Instance().customEnterFromEventLoop([&] {
		if (e && [e type] == NSSystemDefined && [e subtype] == SPSystemDefinedEventMediaKeys) {
			objc_handleMediaKeyEvent(e);
		}
	});
}

- (void) ignoreApplicationActivationRightNow {
	_ignoreActivation = true;
	_ignoreActivationStop.callOnce(kIgnoreActivationTimeoutMs);
}

@end // @implementation ApplicationDelegate

namespace Platform {

void SetWatchingMediaKeys(bool watching) {
	if (_sharedDelegate) {
		[_sharedDelegate setWatchingMediaKeys:watching];
	}
}

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

bool objc_handleMediaKeyEvent(void *ev) {
	auto e = reinterpret_cast<NSEvent*>(ev);

	int keyCode = (([e data1] & 0xFFFF0000) >> 16);
	int keyFlags = ([e data1] & 0x0000FFFF);
	int keyState = (((keyFlags & 0xFF00) >> 8)) == 0xA;
	int keyRepeat = (keyFlags & 0x1);

	if (!_sharedDelegate || ![_sharedDelegate isWatchingMediaKeys]) {
		return false;
	}

	switch (keyCode) {
	case NX_KEYTYPE_PLAY:
		if (keyState == 0) { // Play pressed and released
			Media::Player::instance()->playPause();
			return true;
		}
		break;

	case NX_KEYTYPE_FAST:
		if (keyState == 0) { // Next pressed and released
			Media::Player::instance()->next();
			return true;
		}
		break;

	case NX_KEYTYPE_REWIND:
		if (keyState == 0) { // Previous pressed and released
			Media::Player::instance()->previous();
			return true;
		}
		break;
	}
	return false;
}

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
