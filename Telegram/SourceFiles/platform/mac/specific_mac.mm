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
#include "mainwindow.h"
#include "history/history_location_manager.h"
#include "base/platform/mac/base_utilities_mac.h"
#include "base/platform/base_platform_info.h"

#include <QtGui/QDesktopServices>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDesktopWidget>

#include <cstdlib>
#include <execinfo.h>
#include <sys/xattr.h>

#include <Cocoa/Cocoa.h>
#include <CoreFoundation/CFURL.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hidsystem/ev_keymap.h>
#include <SPMediaKeyTap.h>
#include <mach-o/dyld.h>
#include <AVFoundation/AVFoundation.h>

namespace {

QStringList _initLogs;

};

namespace {

QRect _monitorRect;
crl::time _monitorLastGot = 0;

} // namespace

QRect psDesktopRect() {
	auto tnow = crl::now();
	if (tnow > _monitorLastGot + 1000 || tnow < _monitorLastGot) {
		_monitorLastGot = tnow;
		_monitorRect = QApplication::desktop()->availableGeometry(App::wnd());
	}
	return _monitorRect;
}

void psWriteDump() {
#ifndef DESKTOP_APP_DISABLE_CRASH_REPORTS
	double v = objc_appkitVersion();
	CrashReports::dump() << "OS-Version: " << v;
#endif // DESKTOP_APP_DISABLE_CRASH_REPORTS
}

QStringList psInitLogs() {
	return _initLogs;
}

void psClearInitLogs() {
	_initLogs = QStringList();
}

void psActivateProcess(uint64 pid) {
	if (!pid) {
		objc_activateProgram(App::wnd() ? App::wnd()->winId() : 0);
	}
}

QString psAppDataPath() {
	return objc_appDataPath();
}

void psDoCleanup() {
	try {
		psAutoStart(false, true);
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
	return qsl("/tmp/") + hash + '-' + cGUIDStr();
#else // OS_MAC_STORE
	return objc_documentsPath() + hash.left(4);
#endif // OS_MAC_STORE
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

std::optional<bool> IsDarkMode() {
	return IsMac10_14OrGreater()
		? std::make_optional(IsDarkMenuBar())
		: std::nullopt;
}

void RegisterCustomScheme(bool force) {
	OSStatus result = LSSetDefaultHandlerForURLScheme(CFSTR("tg"), (CFStringRef)[[NSBundle mainBundle] bundleIdentifier]);
	DEBUG_LOG(("App Info: set default handler for 'tg' scheme result: %1").arg(result));
}

// I do check for availability, just not in the exact way clang is content with
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunguarded-availability"
PermissionStatus GetPermissionStatus(PermissionType type) {
#ifndef OS_MAC_OLD
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
#endif // OS_MAC_OLD
	return PermissionStatus::Granted;
}

void RequestPermission(PermissionType type, Fn<void(PermissionStatus)> resultCallback) {
#ifndef OS_MAC_OLD
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
#endif // OS_MAC_OLD
	resultCallback(PermissionStatus::Granted);
}
#pragma clang diagnostic pop // -Wunguarded-availability

void OpenSystemSettingsForPermission(PermissionType type) {
#ifndef OS_MAC_OLD
	switch (type) {
	case PermissionType::Microphone:
		[[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone"]];
		break;
	case PermissionType::Camera:
		[[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_Camera"]];
		break;
	}
#endif // OS_MAC_OLD
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

Window::ControlsLayout WindowControlsLayout() {
	Window::ControlsLayout controls;
	controls.left = {
		Window::Control::Close,
		Window::Control::Minimize,
		Window::Control::Maximize,
	};

	return controls;
}

} // namespace Platform

void psNewVersion() {
	Platform::RegisterCustomScheme();
}

void psAutoStart(bool start, bool silent) {
}

void psSendToMenu(bool send, bool silent) {
}

void psDownloadPathEnableAccess() {
	objc_downloadPathEnableAccess(Core::App().settings().downloadPathBookmark());
}

QByteArray psDownloadPathBookmark(const QString &path) {
	return objc_downloadPathBookmark(path);
}

bool psLaunchMaps(const Data::LocationPoint &point) {
	return QDesktopServices::openUrl(qsl("https://maps.apple.com/?q=Point&z=16&ll=%1,%2").arg(point.latAsString()).arg(point.lonAsString()));
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

QString strStyleOfInterface() {
	const uint32 letters[] = { 0x3BBB7F05, 0xED4C5EC3, 0xC62C15A3, 0x5D10B283, 0x1BB35729, 0x63FB674D, 0xDBE5C174, 0x401EA195, 0x87B0C82A, 0x311BD596, 0x7063ECFA, 0x4AB90C27, 0xDA587DC4, 0x0B6296F8, 0xAA5603FA, 0xE1140A9F, 0x3D12D094, 0x339B5708, 0x712BA5B1 };
	return Platform::MakeFromLetters(letters);
}

QString strTitleWrapClass() {
	const uint32 letters[] = { 0x066C95DD, 0xA289D425, 0x000EF1A5, 0xB53C76AA, 0x5096391D, 0x212BF5B8, 0xE6BCA526, 0x2A5B8EC6, 0xC1457BDB, 0xA1BEE033, 0xA8ADFA11, 0xFF151585, 0x36EC257D, 0x4D96241D, 0xD0341BAA, 0xDE2908BF, 0xFE7978E8, 0x26875E1D, 0x70DA5557, 0x14C02B69, 0x7EFF7E69, 0x008D7217, 0x5EB01138 };
	return Platform::MakeFromLetters(letters);
}

QString strTitleClass() {
	const uint32 letters[] = { 0x1054BBE5, 0xA39FC333, 0x54B51E1E, 0x24895213, 0x50B71830, 0xBF07478C, 0x10BA5503, 0x5C70D3E6, 0x65079D9D, 0xACAAF939, 0x6A56C3CD };
	return Platform::MakeFromLetters(letters);
}
