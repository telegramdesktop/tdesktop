/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/specific_mac.h"

#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "history/history_widget.h"
#include "core/crash_reports.h"
#include "core/sandbox.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "storage/localstorage.h"
#include "window/window_controller.h"
#include "mainwindow.h"
#include "history/history_location_manager.h"
#include "base/platform/mac/base_confirm_quit.h"
#include "base/platform/mac/base_utilities_mac.h"
#include "base/platform/base_platform_info.h"

#include <QtGui/QDesktopServices>
#include <QtWidgets/QApplication>

#include <cstdlib>
#include <execinfo.h>
#include <sys/xattr.h>

#include <Cocoa/Cocoa.h>
#include <CoreFoundation/CFURL.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hidsystem/ev_keymap.h>
#include <mach-o/dyld.h>
#include <AVFoundation/AVFoundation.h>

namespace {

[[nodiscard]] QImage ImageFromNS(NSImage *icon) {
	CGImageRef image = [icon CGImageForProposedRect:NULL context:nil hints:nil];

	const int width = CGImageGetWidth(image);
	const int height = CGImageGetHeight(image);
	auto result = QImage(width, height, QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);

	CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
	CGBitmapInfo info = CGBitmapInfo(kCGImageAlphaPremultipliedFirst) | kCGBitmapByteOrder32Host;
	CGContextRef context = CGBitmapContextCreate(
		result.bits(),
		width,
		height,
		8,
		result.bytesPerLine(),
		space,
		info);

	CGRect rect = CGRectMake(0, 0, width, height);
	CGContextDrawImage(context, rect, image);

	CFRelease(space);
	CFRelease(context);

	return result;
}

[[nodiscard]] QImage ResolveBundleIconDefault() {
	NSString *path = [[NSBundle mainBundle] bundlePath];
	NSString *icon = [path stringByAppendingString:@"/Contents/Resources/Icon.icns"];
	NSImage *image = [[NSImage alloc] initWithContentsOfFile:icon];
	if (!image) {
		return Window::Logo();
	}

	auto result = ImageFromNS(image);
	[image release];
	return result;
}

} // namespace

QString psAppDataPath() {
	return objc_appDataPath();
}

void psDoCleanup() {
	try {
		Platform::AutostartToggle(false);
		psSendToMenu(false, true);
	} catch (...) {
	}
}

int psCleanup() {
	psDoCleanup();
	return 0;
}

void psDoFixPrevious() {
}

int psFixPrevious() {
	psDoFixPrevious();
	return 0;
}

namespace Platform {

void start() {
	objc_start();
}

void finish() {
	objc_finish();
}

QString SingleInstanceLocalServerName(const QString &hash) {
#ifndef OS_MAC_STORE
	return u"/tmp/"_q + hash + '-' + cGUIDStr();
#else // OS_MAC_STORE
	return objc_documentsPath() + hash.left(4);
#endif // OS_MAC_STORE
}

#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
namespace {

QString strStyleOfInterface() {
	const uint32 letters[] = { 0x3BBB7F05, 0xED4C5EC3, 0xC62C15A3, 0x5D10B283, 0x1BB35729, 0x63FB674D, 0xDBE5C174, 0x401EA195, 0x87B0C82A, 0x311BD596, 0x7063ECFA, 0x4AB90C27, 0xDA587DC4, 0x0B6296F8, 0xAA5603FA, 0xE1140A9F, 0x3D12D094, 0x339B5708, 0x712BA5B1 };
	return Platform::MakeFromLetters(letters);
}

bool IsDarkMenuBar() {
	bool result = false;
	@autoreleasepool {

	NSDictionary *dict = [[NSUserDefaults standardUserDefaults] persistentDomainForName:NSGlobalDomain];
	id style = [dict objectForKey:Q2NSString(strStyleOfInterface())];
	BOOL darkModeOn = (style && [style isKindOfClass:[NSString class]] && NSOrderedSame == [style caseInsensitiveCompare:@"dark"]);
	result = darkModeOn ? true : false;

	}
	return result;
}

} // namespace

std::optional<bool> IsDarkMode() {
	return IsMac10_14OrGreater()
		? std::make_optional(IsDarkMenuBar())
		: std::nullopt;
}
#endif // Qt < 6.5.0

void WriteCrashDumpDetails() {
#ifndef TDESKTOP_DISABLE_CRASH_REPORTS
	double v = objc_appkitVersion();
	CrashReports::dump() << "OS-Version: " << v;
#endif // TDESKTOP_DISABLE_CRASH_REPORTS
}

// I do check for availability, just not in the exact way clang is content with
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunguarded-availability"
PermissionStatus GetPermissionStatus(PermissionType type) {
	switch (type) {
	case PermissionType::Microphone:
	case PermissionType::Camera:
		const auto nativeType = (type == PermissionType::Microphone)
			? AVMediaTypeAudio
			: AVMediaTypeVideo;
		if ([AVCaptureDevice respondsToSelector: @selector(authorizationStatusForMediaType:)]) { // Available starting with 10.14
			switch ([AVCaptureDevice authorizationStatusForMediaType:nativeType]) {
				case AVAuthorizationStatusNotDetermined:
					return PermissionStatus::CanRequest;
				case AVAuthorizationStatusAuthorized:
					return PermissionStatus::Granted;
				case AVAuthorizationStatusDenied:
				case AVAuthorizationStatusRestricted:
					return PermissionStatus::Denied;
			}
		}
		break;
	}
	return PermissionStatus::Granted;
}

void RequestPermission(PermissionType type, Fn<void(PermissionStatus)> resultCallback) {
	switch (type) {
	case PermissionType::Microphone:
	case PermissionType::Camera:
		const auto nativeType = (type == PermissionType::Microphone)
			? AVMediaTypeAudio
			: AVMediaTypeVideo;
		if ([AVCaptureDevice respondsToSelector: @selector(requestAccessForMediaType:completionHandler:)]) { // Available starting with 10.14
			[AVCaptureDevice requestAccessForMediaType:nativeType completionHandler:^(BOOL granted) {
				crl::on_main([=] {
					resultCallback(granted ? PermissionStatus::Granted : PermissionStatus::Denied);
				});
			}];
		}
		break;
	}
	resultCallback(PermissionStatus::Granted);
}
#pragma clang diagnostic pop // -Wunguarded-availability

void OpenSystemSettingsForPermission(PermissionType type) {
	switch (type) {
	case PermissionType::Microphone:
		[[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone"]];
		break;
	case PermissionType::Camera:
		[[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_Camera"]];
		break;
	}
}

bool OpenSystemSettings(SystemSettingsType type) {
	switch (type) {
	case SystemSettingsType::Audio:
		[[NSWorkspace sharedWorkspace] openFile:@"/System/Library/PreferencePanes/Sound.prefPane"];
		break;
	}
	return true;
}

void IgnoreApplicationActivationRightNow() {
	objc_ignoreApplicationActivationRightNow();
}

void AutostartToggle(bool enabled, Fn<void(bool)> done) {
	if (done) {
		done(false);
	}
}

bool AutostartSkip() {
	return !cAutoStart();
}

void NewVersionLaunched(int oldVersion) {
}

QImage DefaultApplicationIcon() {
	static auto result = ResolveBundleIconDefault();
	return result;
}

bool PreventsQuit(Core::QuitReason reason) {
	// Thanks Chromium, see
	// chromium.org/developers/design-documents/confirm-to-quit-experiment
	return (reason == Core::QuitReason::QtQuitEvent)
		&& Core::App().settings().macWarnBeforeQuit()
		&& ([[NSApp currentEvent] type] == NSEventTypeKeyDown)
		&& !ConfirmQuit::RunModal(
			tr::lng_mac_hold_to_quit(
				tr::now,
				lt_text,
				ConfirmQuit::QuitKeysString()));
}

void ActivateThisProcess() {
	const auto window = Core::App().activeWindow();
	objc_activateProgram(window ? window->widget()->winId() : 0);
}

} // namespace Platform

void psSendToMenu(bool send, bool silent) {
}

void psDownloadPathEnableAccess() {
	objc_downloadPathEnableAccess(Core::App().settings().downloadPathBookmark());
}

QByteArray psDownloadPathBookmark(const QString &path) {
	return objc_downloadPathBookmark(path);
}

bool psLaunchMaps(const Data::LocationPoint &point) {
	return QDesktopServices::openUrl(u"https://maps.apple.com/?q=Point&z=16&ll=%1,%2"_q.arg(point.latAsString()).arg(point.lonAsString()));
}
