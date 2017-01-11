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
#include "stdafx.h"
#include "pspecific_mac_p.h"

#include "mainwindow.h"
#include "mainwidget.h"
#include "application.h"
#include "localstorage.h"
#include "media/player/media_player_instance.h"
#include "platform/mac/mac_utilities.h"
#include "styles/style_window.h"
#include "lang.h"

#include <Cocoa/Cocoa.h>
#include <CoreFoundation/CFURL.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hidsystem/ev_keymap.h>
#include <SPMediaKeyTap.h>

using Platform::Q2NSString;
using Platform::NSlang;
using Platform::NS2QString;

@interface qVisualize : NSObject {
}

+ (id)str:(const QString &)str;
- (id)initWithString:(const QString &)str;

+ (id)bytearr:(const QByteArray &)arr;
- (id)initWithByteArray:(const QByteArray &)arr;

- (id)debugQuickLookObject;

@end

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

@end

@interface ApplicationDelegate : NSObject<NSApplicationDelegate> {

SPMediaKeyTap *keyTap;
BOOL watchingMediaKeys;

}

- (BOOL)applicationShouldHandleReopen:(NSApplication *)theApplication hasVisibleWindows:(BOOL)flag;
- (void)applicationDidFinishLaunching:(NSNotification *)aNotification;
- (void)applicationDidBecomeActive:(NSNotification *)aNotification;
- (void)receiveWakeNote:(NSNotification*)note;
- (void)setWatchingMediaKeys:(BOOL)watching;
- (BOOL)isWatchingMediaKeys;
- (void)mediaKeyTap:(SPMediaKeyTap*)keyTap receivedMediaKeyEvent:(NSEvent*)event;

@end

ApplicationDelegate *_sharedDelegate = nil;

@implementation ApplicationDelegate {
}

- (BOOL)applicationShouldHandleReopen:(NSApplication *)theApplication hasVisibleWindows:(BOOL)flag {
	if (App::wnd() && App::wnd()->isHidden()) App::wnd()->showFromTray();
	return YES;
}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
	keyTap = nullptr;
	watchingMediaKeys = false;
#ifndef OS_MAC_STORE
	if ([SPMediaKeyTap usesGlobalMediaKeyTap]) {
		keyTap = [[SPMediaKeyTap alloc] initWithDelegate:self];
	} else {
		LOG(("Media key monitoring disabled"));
	}
#endif // else for !OS_MAC_STORE
}

- (void)applicationDidBecomeActive:(NSNotification *)aNotification {
	if (App::app()) App::app()->checkLocalTime();
}

- (void)receiveWakeNote:(NSNotification*)aNotification {
	if (App::app()) App::app()->checkLocalTime();
}

- (void)setWatchingMediaKeys:(BOOL)watching {
	if (watchingMediaKeys != watching) {
		watchingMediaKeys = watching;
		if (keyTap) {
#ifndef OS_MAC_STORE
			if (watchingMediaKeys) {
				[keyTap startWatchingMediaKeys];
			} else {
				[keyTap stopWatchingMediaKeys];
			}
#endif // else for !OS_MAC_STORE
		}
	}
}

- (BOOL)isWatchingMediaKeys {
	return watchingMediaKeys;
}

- (void)mediaKeyTap:(SPMediaKeyTap*)keyTap receivedMediaKeyEvent:(NSEvent*)e {
	if (e && [e type] == NSSystemDefined && [e subtype] == SPSystemDefinedEventMediaKeys) {
		objc_handleMediaKeyEvent(e);
	}
}

@end

namespace Platform {

void SetWatchingMediaKeys(bool watching) {
	if (_sharedDelegate) {
		[_sharedDelegate setWatchingMediaKeys:(watching ? YES : NO)];
	}
}

} // namespace Platform

void objc_holdOnTop(WId winId) {
	NSWindow *wnd = [reinterpret_cast<NSView *>(winId) window];
	[wnd setHidesOnDeactivate:NO];
}

bool objc_darkMode() {
	bool result = false;
	@autoreleasepool {

	NSDictionary *dict = [[NSUserDefaults standardUserDefaults] persistentDomainForName:NSGlobalDomain];
	id style = [dict objectForKey:Q2NSString(strStyleOfInterface())];
	BOOL darkModeOn = (style && [style isKindOfClass:[NSString class]] && NSOrderedSame == [style caseInsensitiveCompare:@"dark"]);
	result = darkModeOn ? true : false;

	}
	return result;
}

void objc_showOverAll(WId winId, bool canFocus) {
	NSWindow *wnd = [reinterpret_cast<NSView *>(winId) window];
	[wnd setLevel:NSPopUpMenuWindowLevel];
	if (!canFocus) {
		[wnd setStyleMask:NSUtilityWindowMask | NSNonactivatingPanelMask];
		[wnd setCollectionBehavior:NSWindowCollectionBehaviorMoveToActiveSpace|NSWindowCollectionBehaviorStationary|NSWindowCollectionBehaviorFullScreenAuxiliary|NSWindowCollectionBehaviorIgnoresCycle];
	}
}

void objc_bringToBack(WId winId) {
	NSWindow *wnd = [reinterpret_cast<NSView *>(winId) window];
	[wnd setLevel:NSModalPanelWindowLevel];
}

void objc_activateWnd(WId winId) {
	NSWindow *wnd = [reinterpret_cast<NSView *>(winId) window];
	[wnd orderFront:wnd];
}

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
			if (Media::Player::exists()) {
				Media::Player::instance()->playPause();
			}
			return true;
		}
		break;

	case NX_KEYTYPE_FAST:
		if (keyState == 0) { // Next pressed and released
			if (Media::Player::exists()) {
				Media::Player::instance()->next();
			}
			return true;
		}
		break;

	case NX_KEYTYPE_REWIND:
		if (keyState == 0) { // Previous pressed and released
			if (Media::Player::exists()) {
				Media::Player::instance()->previous();
			}
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

bool objc_idleSupported() {
	auto idleTime = 0LL;
	return objc_idleTime(idleTime);
}

bool objc_idleTime(TimeMs &idleTime) { // taken from https://github.com/trueinteractions/tint/issues/53
	CFMutableDictionaryRef properties = 0;
	CFTypeRef obj;
	mach_port_t masterPort;
	io_iterator_t iter;
	io_registry_entry_t curObj;

	IOMasterPort(MACH_PORT_NULL, &masterPort);

	/* Get IOHIDSystem */
	IOServiceGetMatchingServices(masterPort, IOServiceMatching("IOHIDSystem"), &iter);
	if (iter == 0) {
		return false;
	} else {
		curObj = IOIteratorNext(iter);
	}
	if (IORegistryEntryCreateCFProperties(curObj, &properties, kCFAllocatorDefault, 0) == KERN_SUCCESS && properties != NULL) {
		obj = CFDictionaryGetValue(properties, CFSTR("HIDIdleTime"));
		CFRetain(obj);
	} else {
		return false;
	}

	uint64 err = ~0L, result = err;
	if (obj) {
		CFTypeID type = CFGetTypeID(obj);

		if (type == CFDataGetTypeID()) {
			CFDataGetBytes((CFDataRef) obj, CFRangeMake(0, sizeof(result)), (UInt8*)&result);
		} else if (type == CFNumberGetTypeID()) {
			CFNumberGetValue((CFNumberRef)obj, kCFNumberSInt64Type, &result);
		} else {
			// error
		}

		CFRelease(obj);

		if (result != err) {
			result /= 1000000; // return as ms
		}
	} else {
		// error
	}

	CFRelease((CFTypeRef)properties);
	IOObjectRelease(curObj);
	IOObjectRelease(iter);
	if (result == err) return false;

	idleTime = static_cast<TimeMs>(result);
	return true;
}

@interface OpenWithApp : NSObject {
	NSString *fullname;
	NSURL *app;
	NSImage *icon;
}
@property (nonatomic, retain) NSString *fullname;
@property (nonatomic, retain) NSURL *app;
@property (nonatomic, retain) NSImage *icon;
@end

@implementation OpenWithApp
@synthesize fullname, app, icon;

- (void) dealloc {
	[fullname release];
	[app release];
	[icon release];
	[super dealloc];
}

@end

@interface OpenFileWithInterface : NSObject {
}

- (id) init:(NSString *)file;
- (BOOL) popupAtX:(int)x andY:(int)y;
- (void) itemChosen:(id)sender;
- (void) dealloc;

@end

@implementation OpenFileWithInterface {
	NSString *toOpen;

	NSURL *defUrl;
	NSString *defBundle, *defName, *defVersion;
	NSImage *defIcon;

	NSMutableArray *apps;

	NSMenu *menu;
}

- (void) fillAppByUrl:(NSURL*)url bundle:(NSString**)bundle name:(NSString**)name version:(NSString**)version icon:(NSImage**)icon {
	NSBundle *b = [NSBundle bundleWithURL:url];
	if (b) {
		NSString *path = [url path];
		*name = [[NSFileManager defaultManager] displayNameAtPath: path];
		if (!*name) *name = (NSString*)[b objectForInfoDictionaryKey:@"CFBundleDisplayName"];
		if (!*name) *name = (NSString*)[b objectForInfoDictionaryKey:@"CFBundleName"];
		if (*name) {
			*bundle = [b bundleIdentifier];
			if (bundle) {
				*version = (NSString*)[b objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
				*icon = [[NSWorkspace sharedWorkspace] iconForFile: path];
				if (*icon && [*icon isValid]) [*icon setSize: CGSizeMake(16., 16.)];
				return;
			}
		}
	}
	*bundle = *name = *version = nil;
	*icon = nil;
}

- (id) init:(NSString*)file {
	toOpen = [file retain];
	if (self = [super init]) {
		NSURL *url = [NSURL fileURLWithPath:file];
		defUrl = [[NSWorkspace sharedWorkspace] URLForApplicationToOpenURL:url];
		if (defUrl) {
			[self fillAppByUrl:defUrl bundle:&defBundle name:&defName version:&defVersion icon:&defIcon];
			if (!defBundle || !defName) {
				defUrl = nil;
			}
		}
		NSArray *appsList = (NSArray*)LSCopyApplicationURLsForURL(CFURLRef(url), kLSRolesAll);
		NSMutableDictionary *data = [NSMutableDictionary dictionaryWithCapacity:16];
		int fullcount = 0;
		for (id app in appsList) {
			if (fullcount > 15) break;

			NSString *bundle = nil, *name = nil, *version = nil;
			NSImage *icon = nil;
			[self fillAppByUrl:(NSURL*)app bundle:&bundle name:&name version:&version icon:&icon];
			if (bundle && name) {
				if ([bundle isEqualToString:defBundle] && [version isEqualToString:defVersion]) continue;
				NSString *key = [[NSArray arrayWithObjects:bundle, name, nil] componentsJoinedByString:@"|"];
				if (!version) version = @"";

				NSMutableDictionary *versions = (NSMutableDictionary*)[data objectForKey:key];
				if (!versions) {
					versions = [NSMutableDictionary dictionaryWithCapacity:2];
					[data setValue:versions forKey:key];
				}
				if (![versions objectForKey:version]) {
					[versions setValue:[NSArray arrayWithObjects:name, icon, app, nil] forKey:version];
					++fullcount;
				}
			}
		}
		if (fullcount || defUrl) {
			apps = [NSMutableArray arrayWithCapacity:fullcount];
			for (id key in data) {
				NSMutableDictionary *val = (NSMutableDictionary*)[data objectForKey:key];
				for (id ver in val) {
					NSArray *app = (NSArray*)[val objectForKey:ver];
					OpenWithApp *a = [[OpenWithApp alloc] init];
					NSString *fullname = (NSString*)[app objectAtIndex:0], *version = (NSString*)ver;
					BOOL showVersion = ([val count] > 1);
					if (!showVersion) {
						NSError *error = NULL;
						NSRegularExpression *regex = [NSRegularExpression regularExpressionWithPattern:@"^\\d+\\.\\d+\\.\\d+(\\.\\d+)?$" options:NSRegularExpressionCaseInsensitive error:&error];
						showVersion = ![regex numberOfMatchesInString:version options:NSMatchingWithoutAnchoringBounds range:{0,[version length]}];
					}
					if (showVersion) fullname = [[NSArray arrayWithObjects:fullname, @" (", version, @")", nil] componentsJoinedByString:@""];
					[a setFullname:fullname];
					[a setIcon:(NSImage*)[app objectAtIndex:1]];
					[a setApp:(NSURL*)[app objectAtIndex:2]];
					[apps addObject:a];
					[a release];
				}
			}
		}
		[apps sortUsingDescriptors:[NSArray arrayWithObject:[NSSortDescriptor sortDescriptorWithKey:@"fullname" ascending:YES]]];
		[appsList release];
		menu = nil;
	}
	return self;
}

- (BOOL) popupAtX:(int)x andY:(int)y {
	if (![apps count] && !defName) return NO;
	menu = [[NSMenu alloc] initWithTitle:@"Open With"];

	int index = 0;
	if (defName) {
		NSMenuItem *item = [menu insertItemWithTitle:[[NSArray arrayWithObjects:defName, @" (default)", nil] componentsJoinedByString:@""] action:@selector(itemChosen:) keyEquivalent:@"" atIndex:index++];
		if (defIcon) [item setImage:defIcon];
		[item setTarget:self];
		[menu insertItem:[NSMenuItem separatorItem] atIndex:index++];
	}
	if ([apps count]) {
		for (id a in apps) {
			OpenWithApp *app = (OpenWithApp*)a;
			NSMenuItem *item = [menu insertItemWithTitle:[a fullname] action:@selector(itemChosen:) keyEquivalent:@"" atIndex:index++];
			if ([app icon]) [item setImage:[app icon]];
			[item setTarget:self];
		}
		[menu insertItem:[NSMenuItem separatorItem] atIndex:index++];
	}
	NSMenuItem *item = [menu insertItemWithTitle:NSlang(lng_mac_choose_program_menu) action:@selector(itemChosen:) keyEquivalent:@"" atIndex:index++];
	[item setTarget:self];

	[menu popUpMenuPositioningItem:nil atLocation:CGPointMake(x, y) inView:nil];

	return YES;
}

- (void) itemChosen:(id)sender {
	NSArray *items = [menu itemArray];
	NSURL *url = nil;
	for (int i = 0, l = [items count]; i < l; ++i) {
		if ([items objectAtIndex:i] == sender) {
			if (defName) i -= 2;
			if (i < 0) {
				url = defUrl;
			} else if (i < int([apps count])) {
				url = [(OpenWithApp*)[apps objectAtIndex:i] app];
			}
			break;
		}
	}
	if (url) {
		[[NSWorkspace sharedWorkspace] openFile:toOpen withApplication:[url path]];
	} else {
		objc_openFile(NS2QString(toOpen), true);
	}
}

- (void) dealloc {
	[toOpen release];
	if (menu) [menu release];
	[super dealloc];
}

@end

bool objc_showOpenWithMenu(int x, int y, const QString &f) {
	@autoreleasepool {

	NSString *file = Q2NSString(f);
	@try {
		OpenFileWithInterface *menu = [[[OpenFileWithInterface alloc] init:file] autorelease];
		QRect r = QApplication::desktop()->screenGeometry(QPoint(x, y));
		y = r.y() + r.height() - y;
		return !![menu popupAtX:x andY:y];
	}
	@catch (NSException *exception) {
	}
	@finally {
	}

	}
	return false;
}

void objc_showInFinder(const QString &file, const QString &path) {
	@autoreleasepool {

	[[NSWorkspace sharedWorkspace] selectFile:Q2NSString(file) inFileViewerRootedAtPath:Q2NSString(path)];

	}
}

@interface NSURL(CompareUrls)

- (BOOL) isEquivalent:(NSURL *)aURL;

@end

@implementation NSURL(CompareUrls)

- (BOOL) isEquivalent:(NSURL *)aURL {
	if ([self isEqual:aURL]) return YES;
	if ([[self scheme] caseInsensitiveCompare:[aURL scheme]] != NSOrderedSame) return NO;
	if ([[self host] caseInsensitiveCompare:[aURL host]] != NSOrderedSame) return NO;
	if ([[self path] compare:[aURL path]] != NSOrderedSame) return NO;
	if ([[self port] compare:[aURL port]] != NSOrderedSame) return NO;
	if ([[self query] compare:[aURL query]] != NSOrderedSame) return NO;
	return YES;
}

@end

@interface ChooseApplicationDelegate : NSObject<NSOpenSavePanelDelegate> {
}

- (id) init:(NSArray *)recommendedApps withPanel:(NSOpenPanel *)creator withSelector:(NSPopUpButton *)menu withGood:(NSTextField *)goodLabel withBad:(NSTextField *)badLabel withIcon:(NSImageView *)badIcon withAccessory:(NSView *)acc;
- (BOOL) panel:(id)sender shouldEnableURL:(NSURL *)url;
- (void) panelSelectionDidChange:(id)sender;
- (void) menuDidClose;
- (void) dealloc;

@end

@implementation ChooseApplicationDelegate {
	BOOL onlyRecommended;
	NSArray *apps;
	NSOpenPanel *panel;
	NSPopUpButton *selector;
	NSTextField *good, *bad;
	NSImageView *icon;
	NSString *recom;
	NSView *accessory;
}

- (id) init:(NSArray *)recommendedApps withPanel:(NSOpenPanel *)creator withSelector:(NSPopUpButton *)menu withGood:(NSTextField *)goodLabel withBad:(NSTextField *)badLabel withIcon:(NSImageView *)badIcon withAccessory:(NSView *)acc {
	if (self = [super init]) {
		onlyRecommended = YES;
		recom = [NSlang(lng_mac_recommended_apps) copy];
		apps = recommendedApps;
		panel = creator;
		selector = menu;
		good = goodLabel;
		bad = badLabel;
		icon = badIcon;
		accessory = acc;
		[selector setAction:@selector(menuDidClose)];
	}
	return self;
}

- (BOOL) isRecommended:(NSURL *)url {
	if (apps) {
		for (id app in apps) {
			if ([(NSURL*)app isEquivalent:url]) {
				return YES;
			}
		}
	}
	return NO;
}

- (BOOL) panel:(id)sender shouldEnableURL:(NSURL *)url {
	NSNumber *isDirectory;
	if ([url getResourceValue:&isDirectory forKey:NSURLIsDirectoryKey error:nil] && isDirectory != nil && [isDirectory boolValue]) {
		if (onlyRecommended) {
			CFStringRef ext = CFURLCopyPathExtension((CFURLRef)url);
			NSNumber *isPackage;
			if ([url getResourceValue:&isPackage forKey:NSURLIsPackageKey error:nil] && isPackage != nil && [isPackage boolValue]) {
				return [self isRecommended:url];
			}
		}
		return YES;
	}
	return NO;
}

- (void) panelSelectionDidChange:(id)sender {
	NSArray *urls = [panel URLs];
	if ([urls count]) {
		if ([self isRecommended:[urls firstObject]]) {
			[bad removeFromSuperview];
			[icon removeFromSuperview];
			[accessory addSubview:good];
		} else {
			[good removeFromSuperview];
			[accessory addSubview:bad];
			[accessory addSubview:icon];
		}
	} else {
		[good removeFromSuperview];
		[bad removeFromSuperview];
		[icon removeFromSuperview];
	}
}

- (void) menuDidClose {
	onlyRecommended = [[[selector selectedItem] title] isEqualToString:recom];
	[self refreshPanelTable];
}

- (BOOL) refreshDataInViews: (NSArray*)subviews {
	for (id view in subviews) {
		NSString *cls = [view className];
		if ([cls isEqualToString:Q2NSString(strNeedToReload())]) {
			[view reloadData];
		} else if ([cls isEqualToString:Q2NSString(strNeedToRefresh1())] || [cls isEqualToString:Q2NSString(strNeedToRefresh2())]) {
			[view reloadData];
			return YES;
		} else {
			NSArray *next = [view subviews];
			if ([next count] && [self refreshDataInViews:next]) {
				return YES;
			}
		}
	}
	return NO;
}


- (void) refreshPanelTable {
	@autoreleasepool {

	[self refreshDataInViews:[[panel contentView] subviews]];
	[panel validateVisibleColumns];

	}
}

- (void) dealloc {
	if (apps) {
		[apps release];
		[recom release];
	}
	[super dealloc];
}

@end

void objc_openFile(const QString &f, bool openwith) {
	@autoreleasepool {

	NSString *file = Q2NSString(f);
	if (openwith || [[NSWorkspace sharedWorkspace] openFile:file] == NO) {
		@try {
			NSURL *url = [NSURL fileURLWithPath:file];
			NSString *ext = [url pathExtension];
			NSArray *names = [url pathComponents];
			NSString *name = [names count] ? [names lastObject] : @"";
			NSArray *apps = (NSArray*)LSCopyApplicationURLsForURL(CFURLRef(url), kLSRolesAll);

			NSOpenPanel *openPanel = [NSOpenPanel openPanel];

			NSRect fullRect = { { 0., 0. }, { st::macAccessoryWidth, st::macAccessoryHeight } };
			NSView *accessory = [[NSView alloc] initWithFrame:fullRect];

			[accessory setAutoresizesSubviews:YES];

			NSPopUpButton *selector = [[NSPopUpButton alloc] init];
			[accessory addSubview:selector];
			[selector addItemWithTitle:NSlang(lng_mac_recommended_apps)];
			[selector addItemWithTitle:NSlang(lng_mac_all_apps)];
			[selector sizeToFit];

			NSTextField *enableLabel = [[NSTextField alloc] init];
			[accessory addSubview:enableLabel];
			[enableLabel setStringValue:NSlang(lng_mac_enable_filter)];
			[enableLabel setFont:[selector font]];
			[enableLabel setBezeled:NO];
			[enableLabel setDrawsBackground:NO];
			[enableLabel setEditable:NO];
			[enableLabel setSelectable:NO];
			[enableLabel sizeToFit];

			NSRect selectorFrame = [selector frame], enableFrame = [enableLabel frame];
			enableFrame.size.width += st::macEnableFilterAdd;
			enableFrame.origin.x = (fullRect.size.width - selectorFrame.size.width - enableFrame.size.width) / 2.;
			selectorFrame.origin.x = (fullRect.size.width - selectorFrame.size.width + enableFrame.size.width) / 2.;
			enableFrame.origin.y = fullRect.size.height - selectorFrame.size.height - st::macEnableFilterTop + (selectorFrame.size.height - enableFrame.size.height) / 2.;
			selectorFrame.origin.y = fullRect.size.height - selectorFrame.size.height - st::macSelectorTop;
			[enableLabel setFrame:enableFrame];
			[enableLabel setAutoresizingMask:NSViewMinXMargin|NSViewMaxXMargin];
			[selector setFrame:selectorFrame];
			[selector setAutoresizingMask:NSViewMinXMargin|NSViewMaxXMargin];

			NSButton *button = [[NSButton alloc] init];
			[accessory addSubview:button];
			[button setButtonType:NSSwitchButton];
			[button setFont:[selector font]];
			[button setTitle:NSlang(lng_mac_always_open_with)];
			[button sizeToFit];
			NSRect alwaysRect = [button frame];
			alwaysRect.origin.x = (fullRect.size.width - alwaysRect.size.width) / 2;
			alwaysRect.origin.y = selectorFrame.origin.y - alwaysRect.size.height - st::macAlwaysThisAppTop;
			[button setFrame:alwaysRect];
			[button setAutoresizingMask:NSViewMinXMargin|NSViewMaxXMargin];
#ifdef OS_MAC_STORE
			[button setHidden:YES];
#endif // OS_MAC_STORE
			NSTextField *goodLabel = [[NSTextField alloc] init];
			[goodLabel setStringValue:Q2NSString(lng_mac_this_app_can_open(lt_file, NS2QString(name)))];
			[goodLabel setFont:[NSFont systemFontOfSize:[NSFont smallSystemFontSize]]];
			[goodLabel setBezeled:NO];
			[goodLabel setDrawsBackground:NO];
			[goodLabel setEditable:NO];
			[goodLabel setSelectable:NO];
			[goodLabel sizeToFit];
			NSRect goodFrame = [goodLabel frame];
			goodFrame.origin.x = (fullRect.size.width - goodFrame.size.width) / 2.;
			goodFrame.origin.y = alwaysRect.origin.y - goodFrame.size.height - st::macAppHintTop;
			[goodLabel setFrame:goodFrame];

			NSTextField *badLabel = [[NSTextField alloc] init];
			[badLabel setStringValue:Q2NSString(lng_mac_not_known_app(lt_file, NS2QString(name)))];
			[badLabel setFont:[goodLabel font]];
			[badLabel setBezeled:NO];
			[badLabel setDrawsBackground:NO];
			[badLabel setEditable:NO];
			[badLabel setSelectable:NO];
			[badLabel sizeToFit];
			NSImageView *badIcon = [[NSImageView alloc] init];
			NSImage *badImage = [NSImage imageNamed:NSImageNameCaution];
			[badIcon setImage:badImage];
			[badIcon setFrame:NSMakeRect(0, 0, st::macCautionIconSize, st::macCautionIconSize)];

			NSRect badFrame = [badLabel frame], badIconFrame = [badIcon frame];
			badFrame.origin.x = (fullRect.size.width - badFrame.size.width + badIconFrame.size.width) / 2.;
			badIconFrame.origin.x = (fullRect.size.width - badFrame.size.width - badIconFrame.size.width) / 2.;
			badFrame.origin.y = alwaysRect.origin.y - badFrame.size.height - st::macAppHintTop;
			badIconFrame.origin.y = badFrame.origin.y;
			[badLabel setFrame:badFrame];
			[badIcon setFrame:badIconFrame];

			[openPanel setAccessoryView:accessory];

			ChooseApplicationDelegate *delegate = [[ChooseApplicationDelegate alloc] init:apps withPanel:openPanel withSelector:selector withGood:goodLabel withBad:badLabel withIcon:badIcon withAccessory:accessory];
			[openPanel setDelegate:delegate];

			[openPanel setCanChooseDirectories:NO];
			[openPanel setCanChooseFiles:YES];
			[openPanel setAllowsMultipleSelection:NO];
			[openPanel setResolvesAliases:YES];
			[openPanel setTitle:NSlang(lng_mac_choose_app)];
			[openPanel setMessage:Q2NSString(lng_mac_choose_text(lt_file, NS2QString(name)))];

			NSArray *appsPaths = [[NSFileManager defaultManager] URLsForDirectory:NSApplicationDirectory inDomains:NSLocalDomainMask];
			if ([appsPaths count]) [openPanel setDirectoryURL:[appsPaths firstObject]];
			[openPanel beginWithCompletionHandler:^(NSInteger result){
				if (result == NSFileHandlingPanelOKButton) {
					if ([[openPanel URLs] count] > 0) {
						NSURL *app = [[openPanel URLs] objectAtIndex:0];
						NSString *path = [app path];
						if ([button state] == NSOnState) {
							NSArray *UTIs = (NSArray *)UTTypeCreateAllIdentifiersForTag(kUTTagClassFilenameExtension,
																						(CFStringRef)ext,
																						nil);
							for (NSString *UTI in UTIs) {
								OSStatus result = LSSetDefaultRoleHandlerForContentType((CFStringRef)UTI,
																						kLSRolesAll,
																						(CFStringRef)[[NSBundle bundleWithPath:path] bundleIdentifier]);
								DEBUG_LOG(("App Info: set default handler for '%1' UTI result: %2").arg(NS2QString(UTI)).arg(result));
							}

							[UTIs release];
						}
						[[NSWorkspace sharedWorkspace] openFile:file withApplication:[app path]];
					}
				}
				[selector release];
				[button release];
				[enableLabel release];
				[goodLabel release];
				[badLabel release];
				[badIcon release];
				[accessory release];
				[delegate release];
			}];
		}
		@catch (NSException *exception) {
			[[NSWorkspace sharedWorkspace] openFile:file];
		}
		@finally {
		}
	}

	}
}

void objc_start() {
	_sharedDelegate = [[ApplicationDelegate alloc] init];
	[[NSApplication sharedApplication] setDelegate:_sharedDelegate];
	[[[NSWorkspace sharedWorkspace] notificationCenter] addObserver: _sharedDelegate
														   selector: @selector(receiveWakeNote:)
															   name: NSWorkspaceDidWakeNotification object: NULL];
}

namespace {
	NSURL *_downloadPathUrl = nil;
}

void objc_finish() {
	[_sharedDelegate release];
	if (_downloadPathUrl) {
		[_downloadPathUrl stopAccessingSecurityScopedResource];
		_downloadPathUrl = nil;
	}
}

void objc_registerCustomScheme() {
#ifndef TDESKTOP_DISABLE_REGISTER_CUSTOM_SCHEME
	OSStatus result = LSSetDefaultHandlerForURLScheme(CFSTR("tg"), (CFStringRef)[[NSBundle mainBundle] bundleIdentifier]);
	DEBUG_LOG(("App Info: set default handler for 'tg' scheme result: %1").arg(result));
#endif // !TDESKTOP_DISABLE_REGISTER_CUSTOM_SCHEME
}

BOOL _execUpdater(BOOL update = YES, const QString &crashreport = QString()) {
	@autoreleasepool {

	NSString *path = @"", *args = @"";
	@try {
		path = [[NSBundle mainBundle] bundlePath];
		if (!path) {
			LOG(("Could not get bundle path!!"));
			return NO;
		}
		path = [path stringByAppendingString:@"/Contents/Frameworks/Updater"];

		NSMutableArray *args = [[NSMutableArray alloc] initWithObjects:@"-workpath", Q2NSString(cWorkingDir()), @"-procid", nil];
		[args addObject:[NSString stringWithFormat:@"%d", [[NSProcessInfo processInfo] processIdentifier]]];
		if (cRestartingToSettings()) [args addObject:@"-tosettings"];
		if (!update) [args addObject:@"-noupdate"];
		if (cLaunchMode() == LaunchModeAutoStart) [args addObject:@"-autostart"];
		if (cDebug()) [args addObject:@"-debug"];
		if (cStartInTray()) [args addObject:@"-startintray"];
		if (cTestMode()) [args addObject:@"-testmode"];
		if (cDataFile() != qsl("data")) {
			[args addObject:@"-key"];
			[args addObject:Q2NSString(cDataFile())];
		}
		if (!crashreport.isEmpty()) {
			[args addObject:@"-crashreport"];
			[args addObject:Q2NSString(crashreport)];
		}

		DEBUG_LOG(("Application Info: executing %1 %2").arg(NS2QString(path)).arg(NS2QString([args componentsJoinedByString:@" "])));
		Logs::closeMain();
		SignalHandlers::finish();
		if (![NSTask launchedTaskWithLaunchPath:path arguments:args]) {
			DEBUG_LOG(("Task not launched while executing %1 %2").arg(NS2QString(path)).arg(NS2QString([args componentsJoinedByString:@" "])));
			return NO;
		}
	}
	@catch (NSException *exception) {
		LOG(("Exception caught while executing %1 %2").arg(NS2QString(path)).arg(NS2QString(args)));
		return NO;
	}
	@finally {
	}

	}
	return YES;
}

bool objc_execUpdater() {
	return !!_execUpdater();
}

void objc_execTelegram(const QString &crashreport) {
#ifndef OS_MAC_STORE
	_execUpdater(NO, crashreport);
#else // OS_MAC_STORE
	@autoreleasepool {

	NSDictionary *conf = [NSDictionary dictionaryWithObject:[NSArray array] forKey:NSWorkspaceLaunchConfigurationArguments];
	[[NSWorkspace sharedWorkspace] launchApplicationAtURL:[NSURL fileURLWithPath:Q2NSString(cExeDir() + cExeName())] options:NSWorkspaceLaunchAsync | NSWorkspaceLaunchNewInstance configuration:conf error:0];

	}
#endif // OS_MAC_STORE
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

void objc_deleteDir(const QString &dir) {
	@autoreleasepool {

	[[NSFileManager defaultManager] removeItemAtPath:Q2NSString(dir) error:nil];

	}
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
		return QString::fromUtf8([[url path] fileSystemRepresentation]) + '/' + str_const_toString(AppName) + '/';
	}
	return QString();
}

QString objc_downloadPath() {
	NSURL *url = [[NSFileManager defaultManager] URLForDirectory:NSDownloadsDirectory inDomain:NSUserDomainMask appropriateForURL:nil create:YES error:nil];
	if (url) {
		return QString::fromUtf8([[url path] fileSystemRepresentation]) + '/' + str_const_toString(AppName) + '/';
	}
	return QString();
}

QString objc_currentCountry() {
	NSLocale *currentLocale = [NSLocale currentLocale];  // get the current locale.
	NSString *countryCode = [currentLocale objectForKey:NSLocaleCountryCode];
	return countryCode ? NS2QString(countryCode) : QString();
}

QString objc_currentLang() {
	NSLocale *currentLocale = [NSLocale currentLocale];  // get the current locale.
	NSString *currentLang = [currentLocale objectForKey:NSLocaleLanguageCode];
	return currentLang ? NS2QString(currentLang) : QString();
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

QByteArray objc_pathBookmark(const QString &path) {
#ifndef OS_MAC_STORE
	return QByteArray();
#else // OS_MAC_STORE
	NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.toUtf8().constData()]];
	if (!url) return QByteArray();

	NSError *error = nil;
	NSData *data = [url bookmarkDataWithOptions:(NSURLBookmarkCreationWithSecurityScope | NSURLBookmarkCreationSecurityScopeAllowOnlyReadAccess) includingResourceValuesForKeys:nil relativeToURL:nil error:&error];
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
		_downloadPathUrl = url;

		Global::SetDownloadPath(NS2QString([_downloadPathUrl path]) + '/');
		if (isStale) {
			NSData *data = [_downloadPathUrl bookmarkDataWithOptions:NSURLBookmarkCreationWithSecurityScope includingResourceValuesForKeys:nil relativeToURL:nil error:&error];
			if (data) {
				Global::SetDownloadPathBookmark(QByteArray::fromNSData(data));
				Local::writeUserSettings();
			}
		}
	}
#endif // OS_MAC_STORE
}

#ifdef OS_MAC_STORE
namespace {
	QMutex _bookmarksMutex;
}

class objc_FileBookmark::objc_FileBookmarkData {
public:
	~objc_FileBookmarkData() {
		if (url) [url release];
	}
	NSURL *url = nil;
	QString name;
	QByteArray bookmark;
	int counter = 0;
};
#endif // OS_MAC_STORE

objc_FileBookmark::objc_FileBookmark(const QByteArray &bookmark) {
#ifdef OS_MAC_STORE
	if (bookmark.isEmpty()) return;

	BOOL isStale = NO;
	NSError *error = nil;
	NSURL *url = [NSURL URLByResolvingBookmarkData:bookmark.toNSData() options:NSURLBookmarkResolutionWithSecurityScope relativeToURL:nil bookmarkDataIsStale:&isStale error:&error];
	if (!url) return;

	if ([url startAccessingSecurityScopedResource]) {
		data = new objc_FileBookmarkData();
		data->url = [url retain];
		data->name = NS2QString([url path]);
		data->bookmark = bookmark;
		[url stopAccessingSecurityScopedResource];
	}
#endif // OS_MAC_STORE
}

bool objc_FileBookmark::valid() const {
	if (enable()) {
		disable();
		return true;
	}
	return false;
}

bool objc_FileBookmark::enable() const {
#ifndef OS_MAC_STORE
	return true;
#else // OS_MAC_STORE
	if (!data) return false;

	QMutexLocker lock(&_bookmarksMutex);
	if (data->counter > 0 || [data->url startAccessingSecurityScopedResource] == YES) {
		++data->counter;
		return true;
	}
	return false;
#endif // OS_MAC_STORE
}

void objc_FileBookmark::disable() const {
#ifdef OS_MAC_STORE
	if (!data) return;

	QMutexLocker lock(&_bookmarksMutex);
	if (data->counter > 0) {
		--data->counter;
		if (!data->counter) {
			[data->url stopAccessingSecurityScopedResource];
		}
	}
#endif // OS_MAC_STORE
}

const QString &objc_FileBookmark::name(const QString &original) const {
#ifndef OS_MAC_STORE
	return original;
#else // OS_MAC_STORE
	return (data && !data->name.isEmpty()) ? data->name : original;
#endif // OS_MAC_STORE
}

QByteArray objc_FileBookmark::bookmark() const {
#ifndef OS_MAC_STORE
	return QByteArray();
#else // OS_MAC_STORE
	return data ? data->bookmark : QByteArray();
#endif // OS_MAC_STORE
}

objc_FileBookmark::~objc_FileBookmark() {
#ifdef OS_MAC_STORE
	if (data && data->counter > 0) {
		LOG(("Did not disable() bookmark, counter: %1").arg(data->counter));
		[data->url stopAccessingSecurityScopedResource];
	}
#endif // OS_MAC_STORE
}
