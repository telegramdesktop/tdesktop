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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "pspecific_mac_p.h"

#include "window.h"
#include "mainwidget.h"
#include "application.h"

#include "lang.h"

#include <Cocoa/Cocoa.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CFURL.h>

#include <IOKit/hidsystem/ev_keymap.h>

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
}

- (BOOL)applicationShouldHandleReopen:(NSApplication *)theApplication hasVisibleWindows:(BOOL)flag;
- (void)applicationDidBecomeActive:(NSNotification *)aNotification;
- (void)receiveWakeNote:(NSNotification*)note;

@end

ApplicationDelegate *_sharedDelegate = nil;

@implementation ApplicationDelegate {
}

- (BOOL)applicationShouldHandleReopen:(NSApplication *)theApplication hasVisibleWindows:(BOOL)flag {
	if (App::wnd() && App::wnd()->isHidden()) App::wnd()->showFromTray();
	return YES;
}

- (void)applicationDidBecomeActive:(NSNotification *)aNotification {
	if (App::app()) App::app()->checkLocalTime();
}

- (void)receiveWakeNote:(NSNotification*)aNotification {
	if (App::app()) App::app()->checkLocalTime();
}

@end

class QNSString {
public:
    QNSString(const QString &str) : _str([NSString stringWithUTF8String:str.toUtf8().constData()]) {
    }
    NSString *s() {
        return _str;
    }
private:
    NSString *_str;
};

QNSString objc_lang(LangKey key) {
	return QNSString(lang(key));
}
QString objcString(NSString *str) {
	return QString::fromUtf8([str cStringUsingEncoding:NSUTF8StringEncoding]);
}

@interface ObserverHelper : NSObject {
}

- (id) init:(PsMacWindowPrivate *)aWnd;
- (void) activeSpaceDidChange:(NSNotification *)aNotification;
- (void) darkModeChanged:(NSNotification *)aNotification;

@end

@interface NotifyHandler : NSObject<NSUserNotificationCenterDelegate> {
}

- (id) init:(PsMacWindowPrivate *)aWnd;

- (void)userNotificationCenter:(NSUserNotificationCenter *)center didActivateNotification:(NSUserNotification *)notification;

- (BOOL)userNotificationCenter:(NSUserNotificationCenter *)center shouldPresentNotification:(NSUserNotification *)notification;

@end

class PsMacWindowData {
public:

    PsMacWindowData(PsMacWindowPrivate *wnd) :
    wnd(wnd),
    observerHelper([[ObserverHelper alloc] init:wnd]),
    notifyHandler([[NotifyHandler alloc] init:wnd]) {
    }

    void onNotifyClick(NSUserNotification *notification) {
		NSDictionary *dict = [notification userInfo];
		NSNumber *peerObj = [dict objectForKey:@"peer"], *msgObj = [dict objectForKey:@"msgid"];
		unsigned long long peerLong = peerObj ? [peerObj unsignedLongLongValue] : 0;
		int msgId = msgObj ? [msgObj intValue] : 0;
        wnd->notifyClicked(peerLong, msgId);
    }

    void onNotifyReply(NSUserNotification *notification) {
		NSDictionary *dict = [notification userInfo];
		NSNumber *peerObj = [dict objectForKey:@"peer"], *msgObj = [dict objectForKey:@"msgid"];
		unsigned long long peerLong = peerObj ? [peerObj unsignedLongLongValue] : 0;
		int msgId = msgObj ? [msgObj intValue] : 0;
        wnd->notifyReplied(peerLong, msgId, [[[notification response] string] UTF8String]);
    }

    ~PsMacWindowData() {
        [observerHelper release];
        [notifyHandler release];
    }

    PsMacWindowPrivate *wnd;
    ObserverHelper *observerHelper;
    NotifyHandler *notifyHandler;
};

@implementation ObserverHelper {
    PsMacWindowPrivate *wnd;
}

- (id) init:(PsMacWindowPrivate *)aWnd {
    if (self = [super init]) {
        wnd = aWnd;
    }
    return self;
}

- (void) activeSpaceDidChange:(NSNotification *)aNotification {
    wnd->activeSpaceChanged();
}

- (void) darkModeChanged:(NSNotification *)aNotification {
	wnd->darkModeChanged();
}

@end

@implementation NotifyHandler {
    PsMacWindowPrivate *wnd;
}

- (id) init:(PsMacWindowPrivate *)aWnd {
    if (self = [super init]) {
        wnd = aWnd;
    }
    return self;
}

- (void) userNotificationCenter:(NSUserNotificationCenter *)center didActivateNotification:(NSUserNotification *)notification {
    NSNumber *instObj = [[notification userInfo] objectForKey:@"launch"];
	unsigned long long instLong = instObj ? [instObj unsignedLongLongValue] : 0;
	DEBUG_LOG(("Received notification with instance %1").arg(instLong));
	if (instLong != Global::LaunchId()) { // other app instance notification
        return;
    }
    if (notification.activationType == NSUserNotificationActivationTypeReplied) {
        wnd->data->onNotifyReply(notification);
    } else if (notification.activationType == NSUserNotificationActivationTypeContentsClicked) {
        wnd->data->onNotifyClick(notification);
    }
    [center removeDeliveredNotification: notification];
}

- (BOOL)userNotificationCenter:(NSUserNotificationCenter *)center shouldPresentNotification:(NSUserNotification *)notification {
    return YES;
}

@end

PsMacWindowPrivate::PsMacWindowPrivate() : data(new PsMacWindowData(this)) {
    [[[NSWorkspace sharedWorkspace] notificationCenter] addObserver:data->observerHelper selector:@selector(activeSpaceDidChange:) name:NSWorkspaceActiveSpaceDidChangeNotification object:nil];
	[[NSDistributedNotificationCenter defaultCenter] addObserver:data->observerHelper selector:@selector(darkModeChanged:) name:QNSString(strNotificationAboutThemeChange()).s() object:nil];
}

void PsMacWindowPrivate::setWindowBadge(const QString &str) {
    [[NSApp dockTile] setBadgeLabel:QNSString(str).s()];
}

void PsMacWindowPrivate::startBounce() {
    [NSApp requestUserAttention:NSInformationalRequest];
}

void PsMacWindowPrivate::updateDelegate() {
    NSUserNotificationCenter *center = [NSUserNotificationCenter defaultUserNotificationCenter];
    [center setDelegate:data->notifyHandler];
}

void objc_holdOnTop(WId winId) {
    NSWindow *wnd = [reinterpret_cast<NSView *>(winId) window];
    [wnd setHidesOnDeactivate:NO];
}

bool objc_darkMode() {
	NSDictionary *dict = [[NSUserDefaults standardUserDefaults] persistentDomainForName:NSGlobalDomain];
	id style = [dict objectForKey:QNSString(strStyleOfInterface()).s()];
	BOOL darkModeOn = ( style && [style isKindOfClass:[NSString class]] && NSOrderedSame == [style caseInsensitiveCompare:@"dark"] );
	return darkModeOn ? true : false;
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

NSImage *qt_mac_create_nsimage(const QPixmap &pm);

void PsMacWindowPrivate::showNotify(uint64 peer, int32 msgId, const QPixmap &pix, const QString &title, const QString &subtitle, const QString &msg, bool withReply) {
    NSUserNotification *notification = [[NSUserNotification alloc] init];
	NSImage *img = qt_mac_create_nsimage(pix);

	DEBUG_LOG(("Sending notification with userinfo: peer %1, msgId %2 and instance %3").arg(peer).arg(msgId).arg(Global::LaunchId()));
    [notification setUserInfo:[NSDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithUnsignedLongLong:peer],@"peer",[NSNumber numberWithInt:msgId],@"msgid",[NSNumber numberWithUnsignedLongLong:Global::LaunchId()],@"launch",nil]];

	[notification setTitle:QNSString(title).s()];
    [notification setSubtitle:QNSString(subtitle).s()];
    [notification setInformativeText:QNSString(msg).s()];
	if ([notification respondsToSelector:@selector(setContentImage:)]) {
		[notification setContentImage:img];
	}

	if (withReply && [notification respondsToSelector:@selector(setHasReplyButton:)]) {
		[notification setHasReplyButton:YES];
	}

    [notification setSoundName:nil];

    NSUserNotificationCenter *center = [NSUserNotificationCenter defaultUserNotificationCenter];
    [center deliverNotification:notification];

	if (img) [img release];
    [notification release];
}

void PsMacWindowPrivate::enableShadow(WId winId) {
//    [[(NSView*)winId window] setStyleMask:NSBorderlessWindowMask];
//    [[(NSView*)winId window] setHasShadow:YES];
}

bool PsMacWindowPrivate::filterNativeEvent(void *event) {
	NSEvent *e = static_cast<NSEvent*>(event);
	if (e && [e type] == NSSystemDefined && [e subtype] == 8) {
		int keyCode = (([e data1] & 0xFFFF0000) >> 16);
		int keyFlags = ([e data1] & 0x0000FFFF);
		int keyState = (((keyFlags & 0xFF00) >> 8)) == 0xA;
		int keyRepeat = (keyFlags & 0x1);

		switch (keyCode) {
		case NX_KEYTYPE_PLAY:
			if (keyState == 0) { // Play pressed and released
				if (App::main()) App::main()->player()->playPausePressed();
				return true;
			}
			break;

		case NX_KEYTYPE_FAST:
			if (keyState == 0) { // Next pressed and released
				if (App::main()) App::main()->player()->nextPressed();
				return true;
			}
			break;

		case NX_KEYTYPE_REWIND:
			if (keyState == 0) { // Previous pressed and released
				if (App::main()) App::main()->player()->prevPressed();
				return true;
			}
			break;
		}
	}
	return false;
}


void PsMacWindowPrivate::clearNotifies(unsigned long long peer) {
    NSUserNotificationCenter *center = [NSUserNotificationCenter defaultUserNotificationCenter];
    if (peer) {
        NSArray *notifies = [center deliveredNotifications];
        for (id notify in notifies) {
			NSDictionary *dict = [notify userInfo];
			if ([[dict objectForKey:@"peer"] unsignedLongLongValue] == peer && [[dict objectForKey:@"launch"] unsignedLongLongValue] == Global::LaunchId()) {
                [center removeDeliveredNotification:notify];
            }
        }
    } else {
        [center removeAllDeliveredNotifications];
    }
}

void objc_debugShowAlert(const QString &str) {
    [[NSAlert alertWithMessageText:@"Debug Message" defaultButton:@"OK" alternateButton:nil otherButton:nil informativeTextWithFormat:@"%@", QNSString(str).s()] runModal];
}

void objc_outputDebugString(const QString &str) {
    NSLog(@"%@", QNSString(str).s());
}

PsMacWindowPrivate::~PsMacWindowPrivate() {
    delete data;
}

bool objc_idleSupported() {
	int64 idleTime = 0;
	return objc_idleTime(idleTime);
}

bool objc_idleTime(int64 &idleTime) { // taken from https://github.com/trueinteractions/tint/issues/53
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

	idleTime = int64(result);
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
	toOpen = file;
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
	NSMenuItem *item = [menu insertItemWithTitle:objc_lang(lng_mac_choose_program_menu).s() action:@selector(itemChosen:) keyEquivalent:@"" atIndex:index++];
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
		objc_openFile(objcString(toOpen), true);
	}
}

- (void) dealloc {
	if (apps) [apps release];
	[super dealloc];
	if (menu) [menu release];
}

@end

bool objc_showOpenWithMenu(int x, int y, const QString &f) {
	NSString *file = QNSString(f).s();
	@try {
		OpenFileWithInterface *menu = [[OpenFileWithInterface alloc] init:file];
		QRect r = QApplication::desktop()->screenGeometry(QPoint(x, y));
		y = r.y() + r.height() - y;
		return !![menu popupAtX:x andY:y];
	}
	@catch (NSException *exception) {
	}
	@finally {
	}
	return false;
}

void objc_showInFinder(const QString &file, const QString &path) {
    [[NSWorkspace sharedWorkspace] selectFile:QNSString(file).s() inFileViewerRootedAtPath:QNSString(path).s()];
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
        recom = [objc_lang(lng_mac_recommended_apps).s() copy];
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
        if ([cls isEqualToString:QNSString(strNeedToReload()).s()]) {
            [view reloadData];
        } else if ([cls isEqualToString:QNSString(strNeedToRefresh1()).s()] || [cls isEqualToString:QNSString(strNeedToRefresh2()).s()]) {
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
    [self refreshDataInViews:[[panel contentView] subviews]];
    [panel validateVisibleColumns];
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
    NSString *file = QNSString(f).s();
    if (openwith || [[NSWorkspace sharedWorkspace] openFile:file] == NO) {
        @try {
            NSURL *url = [NSURL fileURLWithPath:file];
            NSString *ext = [url pathExtension];
            NSArray *names =[url pathComponents];
            NSString *name = [names count] ? [names lastObject] : @"";
            NSArray *apps = (NSArray*)LSCopyApplicationURLsForURL(CFURLRef(url), kLSRolesAll);

            NSOpenPanel *openPanel = [NSOpenPanel openPanel];

			NSRect fullRect = { { 0., 0. }, { st::macAccessory.width() * 1., st::macAccessory.height() * 1. } };
			NSView *accessory = [[NSView alloc] initWithFrame:fullRect];

            [accessory setAutoresizesSubviews:YES];

            NSPopUpButton *selector = [[NSPopUpButton alloc] init];
            [accessory addSubview:selector];
            [selector addItemWithTitle:objc_lang(lng_mac_recommended_apps).s()];
            [selector addItemWithTitle:objc_lang(lng_mac_all_apps).s()];
            [selector sizeToFit];

            NSTextField *enableLabel = [[NSTextField alloc] init];
            [accessory addSubview:enableLabel];
            [enableLabel setStringValue:objc_lang(lng_mac_enable_filter).s()];
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
            [button setTitle:objc_lang(lng_mac_always_open_with).s()];
            [button sizeToFit];
            NSRect alwaysRect = [button frame];
            alwaysRect.origin.x = (fullRect.size.width - alwaysRect.size.width) / 2;
            alwaysRect.origin.y = selectorFrame.origin.y - alwaysRect.size.height - st::macAlwaysThisAppTop;
            [button setFrame:alwaysRect];
            [button setAutoresizingMask:NSViewMinXMargin|NSViewMaxXMargin];
            NSTextField *goodLabel = [[NSTextField alloc] init];
            [goodLabel setStringValue:QNSString(lng_mac_this_app_can_open(lt_file, objcString(name))).s()];
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
            [badLabel setStringValue:QNSString(lng_mac_not_known_app(lt_file, objcString(name))).s()];
            [badLabel setFont:[goodLabel font]];
            [badLabel setBezeled:NO];
            [badLabel setDrawsBackground:NO];
            [badLabel setEditable:NO];
            [badLabel setSelectable:NO];
            [badLabel sizeToFit];
            NSImageView *badIcon = [[NSImageView alloc] init];
            NSImage *badImage = [NSImage imageNamed:NSImageNameCaution];
            [badIcon setImage:badImage];
            [badIcon setFrame:NSMakeRect(0, 0, st::macCautionIconSize.width(), st::macCautionIconSize.height())];

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
            [openPanel setTitle:objc_lang(lng_mac_choose_app).s()];
            [openPanel setMessage:QNSString(lng_mac_choose_text(lt_file, objcString(name))).s()];

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
								DEBUG_LOG(("App Info: set default handler for '%1' UTI result: %2").arg(objcString(UTI)).arg(result));
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
	#endif
}

BOOL _execUpdater(BOOL update = YES, const QString &crashreport = QString()) {
	NSString *path = @"", *args = @"";
	@try {
		path = [[NSBundle mainBundle] bundlePath];
		if (!path) {
			LOG(("Could not get bundle path!!"));
			return NO;
		}
		path = [path stringByAppendingString:@"/Contents/Frameworks/Updater"];

		NSMutableArray *args = [[NSMutableArray alloc] initWithObjects:@"-workpath", QNSString(cWorkingDir()).s(), @"-procid", nil];
		[args addObject:[NSString stringWithFormat:@"%d", [[NSProcessInfo processInfo] processIdentifier]]];
		if (cRestartingToSettings()) [args addObject:@"-tosettings"];
		if (!update) [args addObject:@"-noupdate"];
		if (cLaunchMode() == LaunchModeAutoStart) [args addObject:@"-autostart"];
		if (cDebug()) [args addObject:@"-debug"];
		if (cStartInTray()) [args addObject:@"-startintray"];
		if (cTestMode()) [args addObject:@"-testmode"];
		if (cDataFile() != qsl("data")) {
			[args addObject:@"-key"];
			[args addObject:QNSString(cDataFile()).s()];
		}
		if (!crashreport.isEmpty()) {
			[args addObject:@"-crashreport"];
			[args addObject:QNSString(crashreport).s()];
		}

		DEBUG_LOG(("Application Info: executing %1 %2").arg(objcString(path)).arg(objcString([args componentsJoinedByString:@" "])));
		Logs::closeMain();
		SignalHandlers::finish();
		if (![NSTask launchedTaskWithLaunchPath:path arguments:args]) {
			DEBUG_LOG(("Task not launched while executing %1 %2").arg(objcString(path)).arg(objcString([args componentsJoinedByString:@" "])));
			return NO;
		}
	}
	@catch (NSException *exception) {
		LOG(("Exception caught while executing %1 %2").arg(objcString(path)).arg(objcString(args)));
		return NO;
	}
	@finally {
	}
	return YES;
}

bool objc_execUpdater() {
	return !!_execUpdater();
}

void objc_execTelegram(const QString &crashreport) {
	_execUpdater(NO, crashreport);
}

void objc_activateProgram(WId winId) {
	[NSApp activateIgnoringOtherApps:YES];
	if (winId) {
		NSWindow *w = [reinterpret_cast<NSView*>(winId) window];
		[w makeKeyAndOrderFront:NSApp];
	}
}

bool objc_moveFile(const QString &from, const QString &to) {
	NSString *f = QNSString(from).s(), *t = QNSString(to).s();
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
	return false;
}

void objc_deleteDir(const QString &dir) {
	[[NSFileManager defaultManager] removeItemAtPath:QNSString(dir).s() error:nil];
}

double objc_appkitVersion() {
	return NSAppKitVersionNumber;
}

QString objc_appDataPath() {
	NSURL *url = [[NSFileManager defaultManager] URLForDirectory:NSApplicationSupportDirectory inDomain:NSUserDomainMask appropriateForURL:nil create:YES error:nil];
	if (url) {
		return QString::fromUtf8([[url path] fileSystemRepresentation]) + '/' + QString::fromWCharArray(AppName) + '/';
	}
	return QString();
}

QString objc_downloadPath() {
	NSURL *url = [[NSFileManager defaultManager] URLForDirectory:NSDownloadsDirectory inDomain:NSUserDomainMask appropriateForURL:nil create:YES error:nil];
	if (url) {
		return QString::fromUtf8([[url path] fileSystemRepresentation]) + '/' + QString::fromWCharArray(AppName) + '/';
	}
	return QString();
}

QString objc_currentCountry() {
	NSLocale *currentLocale = [NSLocale currentLocale];  // get the current locale.
	NSString *countryCode = [currentLocale objectForKey:NSLocaleCountryCode];
	return countryCode ? objcString(countryCode) : QString();
}

QString objc_currentLang() {
	NSLocale *currentLocale = [NSLocale currentLocale];  // get the current locale.
	NSString *currentLang = [currentLocale objectForKey:NSLocaleLanguageCode];
	return currentLang ? objcString(currentLang) : QString();
}

QString objc_convertFileUrl(const QString &url) {
	NSString *nsurl = [[[NSURL URLWithString: [NSString stringWithUTF8String: (qsl("file://") + url).toUtf8().constData()]] filePathURL] path];
	if (!nsurl) return QString();

	return objcString(nsurl);
}

QByteArray objc_downloadPathBookmark(const QString &path) {
	return QByteArray();
}

QByteArray objc_pathBookmark(const QString &path) {
	return QByteArray();
}

void objc_downloadPathEnableAccess(const QByteArray &bookmark) {
}

objc_FileBookmark::objc_FileBookmark(const QByteArray &bookmark) {
}

bool objc_FileBookmark::valid() const {
	return true;
}

bool objc_FileBookmark::enable() const {
	return true;
}

void objc_FileBookmark::disable() const {
}

const QString &objc_FileBookmark::name(const QString &original) const {
	return original;
}

QByteArray objc_FileBookmark::bookmark() const {
	return QByteArray();
}

objc_FileBookmark::~objc_FileBookmark() {
}
