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
#include "core/launcher.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "storage/localstorage.h"
#include "window/window_controller.h"
#include "mainwindow.h"
#include "history/history_location_manager.h"
#include "base/platform/mac/base_confirm_quit.h"
#include "base/platform/mac/base_utilities_mac.h"
#include "base/platform/base_platform_info.h"

#include <QtCore/QDirIterator>
#include <QtGui/QDesktopServices>
#include <QtWidgets/QApplication>

#include <cstdlib>
#include <dlfcn.h>
#include <execinfo.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
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

#ifndef OS_MAC_STORE
namespace {

struct TranslocationState {
	bool translocated = false;
	QString original; // The bundle path macOS made the copy from.
};

// Security.framework exports these since 10.12 without declaring them in
// the public headers; the path check is the fallback when they are not
// resolvable.
using SecTranslocateIsTranslocatedURLFn = Boolean(*)(
	CFURLRef,
	bool*,
	CFErrorRef*);
using SecTranslocateCreateOriginalPathForURLFn = CFURLRef(*)(
	CFURLRef,
	CFErrorRef*);

[[nodiscard]] TranslocationState CheckTranslocationState(
		const QString &bundle) {
	auto result = TranslocationState();
	NSURL *url = [NSURL fileURLWithPath:Platform::Q2NSString(bundle)];
	const auto security = dlopen(
		"/System/Library/Frameworks/Security.framework/Security",
		RTLD_LAZY);
	const auto isTranslocated = security
		? reinterpret_cast<SecTranslocateIsTranslocatedURLFn>(
			dlsym(security, "SecTranslocateIsTranslocatedURL"))
		: nullptr;
	const auto createOriginal = security
		? reinterpret_cast<SecTranslocateCreateOriginalPathForURLFn>(
			dlsym(security, "SecTranslocateCreateOriginalPathForURL"))
		: nullptr;

	auto flag = false;
	if (isTranslocated && isTranslocated((CFURLRef)url, &flag, nullptr)) {
		result.translocated = flag;
	} else {
		result.translocated = bundle.contains(u"/AppTranslocation/"_q);
	}
	if (!result.translocated) {
		return result;
	}
	if (createOriginal) {
		if (const auto original = createOriginal((CFURLRef)url, nullptr)) {
			result.original = Platform::NS2QString([(NSURL*)original path]);
			CFRelease(original);
		}
	}
	if (result.original.isEmpty()) {
		// The translocated bundle lives on a read-only mount of the
		// original location, so the mount source names it.
		struct statfs info;
		if (statfs(QFile::encodeName(bundle).constData(), &info) == 0) {
			auto from = QFile::decodeName(info.f_mntfromname);
			const auto name = QFileInfo(bundle).fileName();
			if (!from.endsWith('/' + name)) {
				from += '/' + name;
			}
			result.original = from;
		}
	}
	return result;
}

// Every item of the bundle carries the attribute, and a single leftover
// would translocate the relaunch again.
[[nodiscard]] bool RemoveQuarantineRecursively(const QString &bundle) {
	constexpr auto kAttribute = "com.apple.quarantine";
	const auto remove = [&](const QString &path) {
		const auto local = QFile::encodeName(path);
		if (removexattr(local.constData(), kAttribute, XATTR_NOFOLLOW) == 0
			|| errno == ENOATTR) {
			return true;
		}
		LOG(("Translocation Error: removexattr failed for '%1': %2"
			).arg(path
			).arg(errno));
		return false;
	};
	if (!remove(bundle)) {
		return false;
	}
	auto iterator = QDirIterator(
		bundle,
		QDir::AllEntries
			| QDir::Hidden
			| QDir::System
			| QDir::NoDotAndDotDot,
		QDirIterator::Subdirectories);
	while (iterator.hasNext()) {
		if (!remove(iterator.next())) {
			return false;
		}
	}
	const auto local = QFile::encodeName(bundle);
	const auto left = getxattr(
		local.constData(),
		kAttribute,
		nullptr,
		0,
		0,
		XATTR_NOFOLLOW);
	return (left < 0) && (errno == ENOATTR);
}

[[nodiscard]] bool RelaunchUntranslocated(const QString &bundle) {
	NSDictionary *conf = @{
		NSWorkspaceLaunchConfigurationArguments: @[
			[NSString stringWithUTF8String:Platform::kUntranslocatedArgument]
		],
	};
	NSError *error = nil;
	const auto launched = [[NSWorkspace sharedWorkspace]
		launchApplicationAtURL:[NSURL fileURLWithPath:Platform::Q2NSString(bundle)]
		options:NSWorkspaceLaunchAsync | NSWorkspaceLaunchNewInstance
		configuration:conf
		error:&error];
	if (!launched) {
		LOG(("Translocation Error: relaunch failed: %1"
			).arg(Platform::NS2QString([error localizedDescription])));
	}
	return launched != nil;
}

// Runs before Qt exists, so this is a bare AppKit alert. No localization
// is loaded at this point either.
void ShowTranslocationError() {
	NSApplication *app = [NSApplication sharedApplication];
	[app setActivationPolicy:NSApplicationActivationPolicyRegular];
	[app activateIgnoringOtherApps:YES];

	NSAlert *alert = [[NSAlert alloc] init];
	alert.alertStyle = NSAlertStyleCritical;
	alert.messageText = @"Telegram Desktop can't start from here";
	alert.informativeText = @"macOS started this copy of Telegram Desktop "
		@"from a read-only temporary location (App Translocation), so it "
		@"can't use its own folder. Please reinstall the app and launch it "
		@"again.";
	[alert addButtonWithTitle:@"Quit"];
	[alert runModal];
	[alert release];
}

} // namespace
#endif // !OS_MAC_STORE

namespace Platform {

void start() {
	objc_start();
}

void finish() {
	objc_finish();
}

// macOS starts a quarantined bundle that was never moved by Finder from
// a random read-only mount, so a portable build loses the
// TelegramForcePortable folder it was shipped with and silently works on
// the default installation's data. When the original location is known
// and is the shipped portable layout, stripping the quarantine attribute
// there is exactly what a Finder move would have done (Gatekeeper has
// already assessed and allowed this launch), and the original can be
// relaunched in place. Anything else is a hard stop: moving the parent
// folder does not help and the user has to reinstall.
bool CheckAppTranslocation() {
#ifdef OS_MAC_STORE
	// Installed by the App Store: never quarantined, never translocated,
	// and the private SecTranslocate symbols must not appear in a store
	// binary.
	return true;
#else // OS_MAC_STORE
	@autoreleasepool {

	const auto bundle = cExeDir() + cExeName();
	if (cExeName().isEmpty()) {
		return true;
	}
	const auto state = CheckTranslocationState(bundle);
	if (!state.translocated) {
		return true;
	}
	LOG(("Translocation Info: running from '%1', original '%2'."
		).arg(bundle
		).arg(state.original));

	const auto relaunched = Core::Launcher::Instance().arguments().contains(
		QString::fromLatin1(kUntranslocatedArgument));
	const auto portable = !state.original.isEmpty()
		&& QFileInfo(state.original + u"/Contents/Info.plist"_q).isFile()
		&& QDir(QFileInfo(state.original).path()
			+ u"/TelegramForcePortable"_q).exists();
	if (!relaunched
		&& portable
		&& RemoveQuarantineRecursively(state.original)
		&& RelaunchUntranslocated(state.original)) {
		LOG(("Translocation Info: relaunched from '%1'."
			).arg(state.original));
		return false;
	}
	ShowTranslocationError();
	return false;

	}
#endif // !OS_MAC_STORE
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

bool HasTouchBar() {
#ifdef Q_PROCESSOR_ARM
	// Apple Silicon Macs have no Touch Bar, except two models.
	auto length = size_t();
	if (sysctlbyname("hw.model", nullptr, &length, nullptr, 0) || !length) {
		return false;
	}
	auto bytes = QByteArray(length, Qt::Uninitialized);
	if (sysctlbyname("hw.model", bytes.data(), &length, nullptr, 0)) {
		return false;
	}
	const auto model = QByteArray(bytes.constData()); // Cut the trailing zero.
	return (model == "MacBookPro17,1") || (model == "Mac14,7");
#else // Q_PROCESSOR_ARM
	return true;
#endif // !Q_PROCESSOR_ARM
}

void ActivateThisProcess() {
	const auto window = Core::IsAppLaunched()
		? Core::App().activeWindow()
		: nullptr;
	objc_activateProgram(window ? window->widget()->winId() : 0);
}

bool ScreenshotProtectionSupported() {
	return true;
}

bool AmbientScreenshotProtectionSupported() {
	return true;
}

void SetWindowScreenshotProtection(not_null<QWidget*> window, bool enabled) {
	const auto handle = window->internalWinId();
	if (!handle) {
		return;
	}
	NSView *view = reinterpret_cast<NSView*>(handle);
	NSWindow *nsWindow = [view window];
	if (!nsWindow) {
		return;
	}
	// Do not read sharingType back to check this: once it has been set to
	// NSWindowSharingNone the getter keeps returning that on macOS 26 even
	// after the window is capturable again. The assignment itself works in
	// both directions and takes effect immediately.
	nsWindow.sharingType = enabled
		? NSWindowSharingNone
		: NSWindowSharingReadOnly;
}

void LaunchMaps(const Data::LocationPoint &point, Fn<void()> fail) {
	if (!QDesktopServices::openUrl(
		u"https://maps.apple.com/?q=Point&z=16&ll=%1,%2"_q.arg(
			point.latAsString(),
			point.lonAsString()))) {
		fail();
	}
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
