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

#include <AppKit/AppKit.h>
#include <Cocoa/Cocoa.h>
#include <IOKit/IOKitLib.h>

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
    
    NSDictionary *uinfo = [[NSDictionary alloc] initWithObjectsAndKeys:[NSNumber numberWithUnsignedLongLong:peer],@"peer",nil];
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

void _debugShowAlert(const char *utf8str) {
    NSString *text = [[NSString alloc] initWithUTF8String: utf8str];
    NSAlert *alert = [NSAlert alertWithMessageText:@"Debug Message" defaultButton:@"OK" alternateButton:nil otherButton:nil informativeTextWithFormat:@"%@", text];
    [alert runModal];
    [alert release];
    [text release];
}

void _outputDebugString(const char *utf8str) {
    NSString *text = [[NSString alloc] initWithUTF8String:utf8str];
    NSLog(@"%@", text);
    [text release];
}

PsMacWindowPrivate::~PsMacWindowPrivate() {
    delete data;
}

int64 _idleTime() { // taken from https://github.com/trueinteractions/tint/issues/53
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
