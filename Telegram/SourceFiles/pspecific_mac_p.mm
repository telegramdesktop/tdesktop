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
#include "stdafx.h"
#include "pspecific_mac_p.h"

#include "lang.h"

#include <Cocoa/Cocoa.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CFURL.h>

class QNSString {
public:
    QNSString(const QString &str) : _str([[NSString alloc] initWithUTF8String:str.toUtf8().constData()]) {
    }
    QNSString &operator=(const QNSString &other) {
        if (this != &other) {
            [_str release];
            _str = [other._str copy];
        }
        return *this;
    }
    QNSString(const QNSString &other) : _str([other._str copy]) {
    }
    ~QNSString() {
        [_str release];
    }

    NSString *s() {
        return _str;
    }
private:
    NSString *_str;
};

typedef QMap<LangKey, QNSString> ObjcLang;
ObjcLang objcLang;

QNSString objc_lang(LangKey key) {
    ObjcLang::const_iterator i = objcLang.constFind(key);
    if (i == objcLang.cend()) {
        i = objcLang.insert(key, lang(key));
    }
    return i.value();
}

@interface ObserverHelper : NSObject {
}

- (id) init:(PsMacWindowPrivate *)aWnd;
- (void) activeSpaceDidChange:(NSNotification *)aNotification;

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
        NSNumber *peerObj = [[notification userInfo] objectForKey:@"peer"];
        unsigned long long peerLong = [peerObj unsignedLongLongValue];
        wnd->notifyClicked(peerLong);
    }
    
    void onNotifyReply(NSUserNotification *notification) {
        NSNumber *peerObj = [[notification userInfo] objectForKey:@"peer"];
        unsigned long long peerLong = [peerObj unsignedLongLongValue];
        wnd->notifyReplied(peerLong, [[[notification response] string] UTF8String]);
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
    NSNumber *instObj = [[notification userInfo] objectForKey:@"inst"];
    unsigned long long instLong = [instObj unsignedLongLongValue];
    if (instLong != cInstance()) { // other app instance notification
        return;
    }
    if (notification.activationType == NSUserNotificationActivationTypeReplied){
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
    NSUserNotificationCenter *center = [NSUserNotificationCenter defaultUserNotificationCenter];
    [center setDelegate:data->notifyHandler];
}

void PsMacWindowPrivate::setWindowBadge(const char *utf8str) {
    NSString *badgeString = [[NSString alloc] initWithUTF8String:utf8str];
    [[NSApp dockTile] setBadgeLabel:badgeString];
    [badgeString release];
}

void PsMacWindowPrivate::startBounce() {
    [NSApp requestUserAttention:NSInformationalRequest];
}

void PsMacWindowPrivate::holdOnTop(WId winId) {
    NSWindow *wnd = [reinterpret_cast<NSView *>(winId) window];
    [wnd setHidesOnDeactivate:NO];
}

void PsMacWindowPrivate::showOverAll(WId winId) {
    NSWindow *wnd = [reinterpret_cast<NSView *>(winId) window];
    [wnd setLevel:NSFloatingWindowLevel];
    [wnd setStyleMask:NSUtilityWindowMask | NSNonactivatingPanelMask];
    [wnd setCollectionBehavior:NSWindowCollectionBehaviorCanJoinAllSpaces|NSWindowCollectionBehaviorFullScreenAuxiliary|NSWindowCollectionBehaviorIgnoresCycle];
}

void PsMacWindowPrivate::activateWnd(WId winId) {
    NSWindow *wnd = [reinterpret_cast<NSView *>(winId) window];
    [wnd orderFront:wnd];
}

void PsMacWindowPrivate::showNotify(unsigned long long peer, const char *utf8title, const char *utf8subtitle, const char *utf8msg) {
    NSUserNotification *notification = [[NSUserNotification alloc] init];
    
    NSDictionary *uinfo = [[NSDictionary alloc] initWithObjectsAndKeys:[NSNumber numberWithUnsignedLongLong:peer],@"peer",[NSNumber numberWithUnsignedLongLong:cInstance()],@"inst",nil];
    [notification setUserInfo:uinfo];
    [uinfo release];

    NSString *title = [[NSString alloc] initWithUTF8String:utf8title];
    [notification setTitle:title];
    [title release];
    
    NSString *subtitle = [[NSString alloc] initWithUTF8String:utf8subtitle];
    [notification setSubtitle:subtitle];
    [subtitle release];

    NSString *msg = [[NSString alloc] initWithUTF8String:utf8msg];
    [notification setInformativeText:msg];
    [msg release];
    
    [notification setHasReplyButton:YES];

    [notification setSoundName:nil];
    
    NSUserNotificationCenter *center = [NSUserNotificationCenter defaultUserNotificationCenter];
    [center deliverNotification:notification];
    
    [notification release];
}

void PsMacWindowPrivate::enableShadow(WId winId) {
//    [[(NSView*)winId window] setStyleMask:NSBorderlessWindowMask];
//    [[(NSView*)winId window] setHasShadow:YES];
}

void PsMacWindowPrivate::clearNotifies(unsigned long long peer) {
    NSUserNotificationCenter *center = [NSUserNotificationCenter defaultUserNotificationCenter];
    if (peer) {
        NSArray *notifies = [center deliveredNotifications];
        for (id notify in notifies) {
            if ([[[notify userInfo] objectForKey:@"peer"] unsignedLongLongValue] == peer) {
                [center removeDeliveredNotification:notify];
            }
        }
    } else {
        [center removeAllDeliveredNotifications];
    }
}

void objc_debugShowAlert(const char *utf8str) {
    NSString *text = [[NSString alloc] initWithUTF8String: utf8str];
    NSAlert *alert = [NSAlert alertWithMessageText:@"Debug Message" defaultButton:@"OK" alternateButton:nil otherButton:nil informativeTextWithFormat:@"%@", text];
    [alert runModal];
    [alert release];
    [text release];
}

void objc_outputDebugString(const char *utf8str) {
    NSString *text = [[NSString alloc] initWithUTF8String:utf8str];
    NSLog(@"%@", text);
    [text release];
}

PsMacWindowPrivate::~PsMacWindowPrivate() {
    delete data;
}

int64 objc_idleTime() { // taken from https://github.com/trueinteractions/tint/issues/53
    CFMutableDictionaryRef properties = 0;
    CFTypeRef obj;
    mach_port_t masterPort;
    io_iterator_t iter;
    io_registry_entry_t curObj;
    
    IOMasterPort(MACH_PORT_NULL, &masterPort);
    
    /* Get IOHIDSystem */
    IOServiceGetMatchingServices(masterPort, IOServiceMatching("IOHIDSystem"), &iter);
    if (iter == 0) {
        return -1;
    } else {
        curObj = IOIteratorNext(iter);
    }
    if (IORegistryEntryCreateCFProperties(curObj, &properties, kCFAllocatorDefault, 0) == KERN_SUCCESS && properties != NULL) {
        obj = CFDictionaryGetValue(properties, CFSTR("HIDIdleTime"));
        CFRetain(obj);
    } else {
        return -1;
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
    return (result == err) ? -1 : int64(result);
}

void objc_showInFinder(const char *utf8file, const char *utf8path) {
    NSString *file = [[NSString alloc] initWithUTF8String:utf8file], *path = [[NSString alloc] initWithUTF8String:utf8path];
    [[NSWorkspace sharedWorkspace] selectFile:file inFileViewerRootedAtPath:path];
    [file release];
    [path release];
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
        if ([cls isEqualToString:@"FI_TBrowserTableView"]) {
            [view reloadData];
        } else if ([cls isEqualToString:@"FI_TListView"] || [cls isEqualToString:@"FI_TIconView"]) {
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

void objc_openFile(const char *utf8file, bool openwith) {
    NSString *file = [[NSString alloc] initWithUTF8String:utf8file];
    if (openwith || [[NSWorkspace sharedWorkspace] openFile:file] == NO) {
        @try {
            NSURL *url = [NSURL fileURLWithPath:file];
            NSString *ext = [url pathExtension];
            NSArray *names =[url pathComponents];
            NSString *name = [names count] ? [names lastObject] : @"";
            NSArray *apps = (NSArray*)LSCopyApplicationURLsForURL(CFURLRef(url), kLSRolesAll);
            
            NSOpenPanel *openPanel = [NSOpenPanel openPanel];
            
            NSView *accessory = [[NSView alloc] init];
            
            [openPanel setAccessoryView:accessory];
            NSRect fullRect = [[accessory superview] frame];
            fullRect.origin = NSMakePoint(0, 0);
            fullRect.size.height = st::macAccessoryHeight;
            [accessory setFrame:fullRect];
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
            [goodLabel setStringValue:[objc_lang(lng_mac_this_app_can_open).s() stringByReplacingOccurrencesOfString:@"{file}" withString:name]];
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
            [badLabel setStringValue:[objc_lang(lng_mac_not_known_app).s() stringByReplacingOccurrencesOfString:@"{file}" withString:name]];
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
            
            ChooseApplicationDelegate *delegate = [[ChooseApplicationDelegate alloc] init:apps withPanel:openPanel withSelector:selector withGood:goodLabel withBad:badLabel withIcon:badIcon withAccessory:accessory];
            [openPanel setDelegate:delegate];
            
            [openPanel setCanChooseDirectories:NO];
            [openPanel setCanChooseFiles:YES];
            [openPanel setAllowsMultipleSelection:NO];
            [openPanel setResolvesAliases:YES];
            [openPanel setTitle:objc_lang(lng_mac_choose_app).s()];
            [openPanel setMessage:[objc_lang(lng_mac_choose_text).s() stringByReplacingOccurrencesOfString:@"{file}" withString:name]];
            
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
                                LSSetDefaultRoleHandlerForContentType((CFStringRef)UTI,
                                                                      kLSRolesEditor,
                                                                      (CFStringRef)[[NSBundle bundleWithPath:path] bundleIdentifier]);
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
    [file release];
}

void objc_finish() {
    if (!objcLang.isEmpty()) {
        objcLang.clear();
    }
}
