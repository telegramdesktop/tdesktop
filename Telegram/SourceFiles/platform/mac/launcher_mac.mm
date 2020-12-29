/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/launcher_mac.h"

#include "core/crash_reports.h"
#include "core/update_checker.h"
#include "base/platform/base_platform_file_utilities.h"
#include "base/platform/mac/base_utilities_mac.h"

#include <Cocoa/Cocoa.h>
#include <CoreFoundation/CFURL.h>
#include <sys/sysctl.h>

namespace Platform {

Launcher::Launcher(int argc, char *argv[])
: Core::Launcher(argc, argv) {
}

void Launcher::initHook() {
#ifndef OS_MAC_OLD
	// macOS Retina display support is working fine, others are not.
	QCoreApplication::setAttribute(Qt::AA_DisableHighDpiScaling, false);
#endif // OS_MAC_OLD
}

bool Launcher::launchUpdater(UpdaterLaunch action) {
	if (cExeName().isEmpty()) {
		return false;
	}
	@autoreleasepool {

#ifdef OS_MAC_STORE
	// In AppStore version we don't have Updater.
	// We just relaunch our app.
	if (action == UpdaterLaunch::JustRelaunch) {
		NSDictionary *conf = [NSDictionary dictionaryWithObject:[NSArray array] forKey:NSWorkspaceLaunchConfigurationArguments];
		[[NSWorkspace sharedWorkspace] launchApplicationAtURL:[NSURL fileURLWithPath:Q2NSString(cExeDir() + cExeName())] options:NSWorkspaceLaunchAsync | NSWorkspaceLaunchNewInstance configuration:conf error:0];
		return true;
	}
#endif // OS_MAC_STORE

	NSString *path = @"", *args = @"";
	@try {
		path = [[NSBundle mainBundle] bundlePath];
		if (!path) {
			LOG(("Could not get bundle path!!"));
			return false;
		}
		path = [path stringByAppendingString:@"/Contents/Frameworks/Updater"];
		base::Platform::RemoveQuarantine(QFile::decodeName([path fileSystemRepresentation]));

		NSMutableArray *args = [[NSMutableArray alloc] initWithObjects:@"-workpath", Q2NSString(cWorkingDir()), @"-procid", nil];
		[args addObject:[NSString stringWithFormat:@"%d", [[NSProcessInfo processInfo] processIdentifier]]];
		if (cRestartingToSettings()) [args addObject:@"-tosettings"];
		if (action == UpdaterLaunch::JustRelaunch) [args addObject:@"-noupdate"];
		if (cLaunchMode() == LaunchModeAutoStart) [args addObject:@"-autostart"];
		if (Logs::DebugEnabled()) [args addObject:@"-debug"];
		if (cStartInTray()) [args addObject:@"-startintray"];
		if (cUseFreeType()) [args addObject:@"-freetype"];
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
		if (Core::UpdaterDisabled()) [args addObject:@"-externalupdater"];
#endif // !TDESKTOP_DISABLE_AUTOUPDATE
		if (cDataFile() != qsl("data")) {
			[args addObject:@"-key"];
			[args addObject:Q2NSString(cDataFile())];
		}
		if (customWorkingDir()) {
			[args addObject:@"-workdir_custom"];
		}

		DEBUG_LOG(("Application Info: executing %1 %2").arg(NS2QString(path)).arg(NS2QString([args componentsJoinedByString:@" "])));
		Logs::closeMain();
		CrashReports::Finish();
		if (![NSTask launchedTaskWithLaunchPath:path arguments:args]) {
			DEBUG_LOG(("Task not launched while executing %1 %2").arg(NS2QString(path)).arg(NS2QString([args componentsJoinedByString:@" "])));
			return false;
		}
	}
	@catch (NSException *exception) {
		LOG(("Exception caught while executing %1 %2").arg(NS2QString(path)).arg(NS2QString(args)));
		return false;
	}
	@finally {
	}

	}
	return true;
}

} // namespace Platform
