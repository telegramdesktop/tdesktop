/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#import <Cocoa/Cocoa.h>

NSString *appName = @"Telegram.app";
NSString *appDir = nil;
NSString *workDir = nil;

#ifdef _DEBUG
BOOL _debug = YES;
#else
BOOL _debug = NO;
#endif

NSFileHandle *_logFile = nil;
void openLog() {
	if (!_debug || _logFile) return;
	NSString *logDir = [workDir stringByAppendingString:@"DebugLogs"];
	if (![[NSFileManager defaultManager] createDirectoryAtPath:logDir withIntermediateDirectories:YES attributes:nil error:nil]) {
		return;
	}

	NSDateFormatter *fmt = [[NSDateFormatter alloc] initWithDateFormat:@"DebugLogs/%Y%m%d %H%M%S_upd.txt" allowNaturalLanguage:NO];
	NSString *logPath = [workDir stringByAppendingString:[fmt stringFromDate:[NSDate date]]];
	[[NSFileManager defaultManager] createFileAtPath:logPath contents:nil attributes:nil];
	_logFile = [NSFileHandle fileHandleForWritingAtPath:logPath];
}

void closeLog() {
	if (!_logFile) return;

	[_logFile closeFile];
}

void writeLog(NSString *msg) {
	if (!_logFile) return;

	[_logFile writeData:[[msg stringByAppendingString:@"\n"] dataUsingEncoding:NSUTF8StringEncoding]];
	[_logFile synchronizeFile];
}

void delFolder() {
	[[NSFileManager defaultManager] removeItemAtPath:[workDir stringByAppendingString:@"tupdates/ready"] error:nil];
	rmdir([[workDir stringByAppendingString:@"tupdates"] fileSystemRepresentation]);
}

int main(int argc, const char * argv[]) {
	NSString *path = [[NSBundle mainBundle] bundlePath];
	if (!path) {
		return -1;
	}
	NSRange range = [path rangeOfString:appName options:NSBackwardsSearch];
	if (range.location == NSNotFound) {
		return -1;
	}
	appDir = [path substringToIndex:range.location > 0 ? range.location : 0];

	openLog();
	pid_t procId = 0;
	BOOL update = YES, toSettings = NO, autoStart = NO;
	NSString *key = nil;
	for (int i = 0; i < argc; ++i) {
		if ([@"-workpath" isEqualToString:[NSString stringWithUTF8String:argv[i]]]) {
			if (++i < argc) {
				workDir = [NSString stringWithUTF8String:argv[i]];
			}
		} else if ([@"-procid" isEqualToString:[NSString stringWithUTF8String:argv[i]]]) {
			if (++i < argc) {
				NSNumberFormatter *formatter = [[NSNumberFormatter alloc] init];
				[formatter setNumberStyle:NSNumberFormatterDecimalStyle];
				procId = [[formatter numberFromString:[NSString stringWithUTF8String:argv[i]]] intValue];
			}
		} else if ([@"-noupdate" isEqualToString:[NSString stringWithUTF8String:argv[i]]]) {
			update = NO;
		} else if ([@"-tosettings" isEqualToString:[NSString stringWithUTF8String:argv[i]]]) {
			toSettings = YES;
		} else if ([@"-autostart" isEqualToString:[NSString stringWithUTF8String:argv[i]]]) {
			autoStart = YES;
		} else if ([@"-debug" isEqualToString:[NSString stringWithUTF8String:argv[i]]]) {
			_debug = YES;
		} else if ([@"-key" isEqualToString:[NSString stringWithUTF8String:argv[i]]]) {
			if (++i < argc) key = [NSString stringWithUTF8String:argv[i]];
		}
	}
	if (!workDir) workDir = appDir;
	openLog();
	NSMutableArray *argsArr = [[NSMutableArray alloc] initWithCapacity:argc];
	for (int i = 0; i < argc; ++i) {
		[argsArr addObject:[NSString stringWithUTF8String:argv[i]]];
	}
	writeLog([[NSArray arrayWithObjects:@"Arguments: '", [argsArr componentsJoinedByString:@"' '"], @"'..", nil] componentsJoinedByString:@""]);
	if (key) writeLog([@"Key: " stringByAppendingString:key]);
	if (toSettings) writeLog(@"To Settings!");

	if (procId) {
		NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier:procId];
		for (int i = 0; i < 5 && app != nil && ![app isTerminated]; ++i) {
			usleep(200000);
			app = [NSRunningApplication runningApplicationWithProcessIdentifier:procId];
		}
		if (app) [app forceTerminate];
		app = [NSRunningApplication runningApplicationWithProcessIdentifier:procId];
		for (int i = 0; i < 5 && app != nil && ![app isTerminated]; ++i) {
			usleep(200000);
			app = [NSRunningApplication runningApplicationWithProcessIdentifier:procId];
		}
	}

	if (update) {
		writeLog(@"Starting update files iteration!");

		NSFileManager *fileManager = [NSFileManager defaultManager];
		NSString *srcDir = [workDir stringByAppendingString:@"tupdates/ready/"];
		NSArray *keys = [NSArray arrayWithObject:NSURLIsDirectoryKey];
		NSDirectoryEnumerator *enumerator = [fileManager
											 enumeratorAtURL:[NSURL fileURLWithPath:[workDir stringByAppendingString:@"tupdates/ready"]]
											 includingPropertiesForKeys:keys
											 options:0
											 errorHandler:^(NSURL *url, NSError *error) {
												 return NO;
											 }];
		for (NSURL *url in enumerator) {
			NSString *srcPath = [url path];
			writeLog([@"Handling file " stringByAppendingString:srcPath]);
			NSRange r = [srcPath rangeOfString:srcDir];
			if (r.location != 0) {
				writeLog([@"Bad file found, no base path " stringByAppendingString:srcPath]);
				delFolder();
				break;
			}
			NSString *pathPart = [srcPath substringFromIndex:r.length];
			if ([pathPart rangeOfString:appName].location != 0) {
				writeLog([@"Skipping not app file " stringByAppendingString:srcPath]);
				continue;
			}
			NSString *dstPath = [appDir stringByAppendingString:pathPart];
			NSError *error;
			NSNumber *isDirectory = nil;
			writeLog([[NSArray arrayWithObjects: @"Copying file ", srcPath, @" to ", dstPath, nil] componentsJoinedByString:@""]);
			if (![url getResourceValue:&isDirectory forKey:NSURLIsDirectoryKey error:&error]) {
				writeLog([@"Failed to get IsDirectory for file " stringByAppendingString:[url path]]);
				delFolder();
				break;
			}
			if ([isDirectory boolValue]) {
				if (![fileManager createDirectoryAtPath:dstPath withIntermediateDirectories:YES attributes:nil error:nil]) {
					writeLog([@"Failed to force path for directory " stringByAppendingString:dstPath]);
					delFolder();
					break;
				}
			} else if ([fileManager fileExistsAtPath:dstPath]) {
				if (![[NSData dataWithContentsOfFile:srcPath] writeToFile:dstPath atomically:YES]) {
					writeLog([@"Failed to edit file " stringByAppendingString:dstPath]);
					delFolder();
					break;
				}
			} else {
				if (![fileManager copyItemAtPath:srcPath toPath:dstPath error:nil]) {
					writeLog([@"Failed to copy file to " stringByAppendingString:dstPath]);
					delFolder();
					break;
				}
			}
		}
		delFolder();
	}

	NSString *appPath = [[NSArray arrayWithObjects:appDir, appName, nil] componentsJoinedByString:@""];
	NSMutableArray *args = [[NSMutableArray alloc] initWithObjects:@"-noupdate", nil];
	if (toSettings) [args addObject:@"-tosettings"];
	if (_debug) [args addObject:@"-debug"];
	if (autoStart) [args addObject:@"-autostart"];
	if (key) {
		[args addObject:@"-key"];
		[args addObject:key];
	}
	writeLog([[NSArray arrayWithObjects:@"Running application '", appPath, @"' with args '", [args componentsJoinedByString:@"' '"], @"'..", nil] componentsJoinedByString:@""]);
	NSError *error = nil;
	NSRunningApplication *result = [[NSWorkspace sharedWorkspace]
					launchApplicationAtURL:[NSURL fileURLWithPath:appPath]
					options:NSWorkspaceLaunchDefault
					configuration:[NSDictionary
								   dictionaryWithObject:args
								   forKey:NSWorkspaceLaunchConfigurationArguments]
					error:&error];
	if (!result) {
		writeLog([@"Could not run application, error: " stringByAppendingString:error ? [error localizedDescription] : @"(nil)"]);
	}
	closeLog();
	return result ? 0 : -1;
}

