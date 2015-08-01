/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the plugins of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

/****************************************************************************
 **
 ** Copyright (c) 2007-2008, Apple, Inc.
 **
 ** All rights reserved.
 **
 ** Redistribution and use in source and binary forms, with or without
 ** modification, are permitted provided that the following conditions are met:
 **
 **   * Redistributions of source code must retain the above copyright notice,
 **     this list of conditions and the following disclaimer.
 **
 **   * Redistributions in binary form must reproduce the above copyright notice,
 **     this list of conditions and the following disclaimer in the documentation
 **     and/or other materials provided with the distribution.
 **
 **   * Neither the name of Apple, Inc. nor the names of its contributors
 **     may be used to endorse or promote products derived from this software
 **     without specific prior written permission.
 **
 ** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 ** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 ** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 ** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 ** CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 ** EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 ** PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 ** PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 ** LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 ** NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 ** SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **
 ****************************************************************************/


#import "qcocoaapplicationdelegate.h"
#import "qnswindowdelegate.h"
#import "qcocoamenuloader.h"
#include "qcocoaintegration.h"
#include <qevent.h>
#include <qurl.h>
#include <qdebug.h>
#include <qguiapplication.h>
#include <private/qguiapplication_p.h>
#include "qt_mac_p.h"
#include <qpa/qwindowsysteminterface.h>

QT_USE_NAMESPACE

static QCocoaApplicationDelegate *sharedCocoaApplicationDelegate = nil;

static void cleanupCocoaApplicationDelegate()
{
    [sharedCocoaApplicationDelegate release];
}

@implementation QCocoaApplicationDelegate

- (id)init
{
    self = [super init];
    if (self) {
        inLaunch = true;
        [[NSNotificationCenter defaultCenter]
                addObserver:self
                   selector:@selector(updateScreens:)
                       name:NSApplicationDidChangeScreenParametersNotification
                     object:NSApp];
    }
    return self;
}

- (void)updateScreens:(NSNotification *)notification
{
    Q_UNUSED(notification);
    if (QCocoaIntegration *ci = QCocoaIntegration::instance())
        ci->updateScreens();
}

- (void)dealloc
{
    sharedCocoaApplicationDelegate = nil;
    [dockMenu release];
    [qtMenuLoader release];
    if (reflectionDelegate) {
        [[NSApplication sharedApplication] setDelegate:reflectionDelegate];
        [reflectionDelegate release];
    }
    [[NSNotificationCenter defaultCenter] removeObserver:self];

    [super dealloc];
}

+ (id)allocWithZone:(NSZone *)zone
{
    @synchronized(self) {
        if (sharedCocoaApplicationDelegate == nil) {
            sharedCocoaApplicationDelegate = [super allocWithZone:zone];
            return sharedCocoaApplicationDelegate;
            qAddPostRoutine(cleanupCocoaApplicationDelegate);
        }
    }
    return nil;
}

+ (QCocoaApplicationDelegate *)sharedDelegate
{
    @synchronized(self) {
        if (sharedCocoaApplicationDelegate == nil)
            [[self alloc] init];
    }
    return [[sharedCocoaApplicationDelegate retain] autorelease];
}

- (void)setDockMenu:(NSMenu*)newMenu
{
    [newMenu retain];
    [dockMenu release];
    dockMenu = newMenu;
}

- (NSMenu *)applicationDockMenu:(NSApplication *)sender
{
    Q_UNUSED(sender);
    return [[dockMenu retain] autorelease];
}

- (void)setMenuLoader:(QCocoaMenuLoader *)menuLoader
{
    [menuLoader retain];
    [qtMenuLoader release];
    qtMenuLoader = menuLoader;
}

- (QCocoaMenuLoader *)menuLoader
{
    return [[qtMenuLoader retain] autorelease];
}

- (BOOL) canQuit
{
    [[NSApp mainMenu] cancelTracking];

    bool handle_quit = true;
    NSMenuItem *quitMenuItem = [[[QCocoaApplicationDelegate sharedDelegate] menuLoader] quitMenuItem];
    if (!QGuiApplicationPrivate::instance()->modalWindowList.isEmpty()
        && [quitMenuItem isEnabled]) {
        int visible = 0;
        const QWindowList tlws = QGuiApplication::topLevelWindows();
        for (int i = 0; i < tlws.size(); ++i) {
            if (tlws.at(i)->isVisible())
                ++visible;
        }
        handle_quit = (visible <= 1);
    }

    if (handle_quit) {
        QCloseEvent ev;
        QGuiApplication::sendEvent(qGuiApp, &ev);
        if (ev.isAccepted()) {
            return YES;
        }
    }

    return NO;
}

// This function will only be called when NSApp is actually running.
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender
{
    // The reflection delegate gets precedence
    if (reflectionDelegate) {
        if ([reflectionDelegate respondsToSelector:@selector(applicationShouldTerminate:)])
            return [reflectionDelegate applicationShouldTerminate:sender];
//        return NSTerminateNow;
    }

    if ([self canQuit]) {
        if (!startedQuit) {
            startedQuit = true;
            // Close open windows. This is done in order to deliver de-expose
            // events while the event loop is still running.
            const QWindowList topLevels = QGuiApplication::topLevelWindows();
            for (int i = 0; i < topLevels.size(); ++i) {
                QWindow *topLevelWindow = topLevels.at(i);
                // Widgets have alreay received a CloseEvent from the QApplication
                // QCloseEvent handler. (see canQuit above). Prevent running the
                // CloseEvent logic twice, call close() directly.
                if (topLevelWindow->inherits("QWidgetWindow"))
                    topLevelWindow->close();
                else
                    QWindowSystemInterface::handleCloseEvent(topLevelWindow);
            }
            QWindowSystemInterface::flushWindowSystemEvents();

            QGuiApplication::exit(0);
            startedQuit = false;
        }
    }

    if (QGuiApplicationPrivate::instance()->threadData->eventLoops.isEmpty()) {
        // INVARIANT: No event loop is executing. This probably
        // means that Qt is used as a plugin, or as a part of a native
        // Cocoa application. In any case it should be fine to
        // terminate now:
        return NSTerminateNow;
    }

    return NSTerminateCancel;
}

- (void) applicationWillFinishLaunching:(NSNotification *)notification
{
    Q_UNUSED(notification);

    /*
        From the Cocoa documentation: "A good place to install event handlers
        is in the applicationWillFinishLaunching: method of the application
        delegate. At that point, the Application Kit has installed its default
        event handlers, so if you install a handler for one of the same events,
        it will replace the Application Kit version."
    */

    /*
        If Qt is used as a plugin, we let the 3rd party application handle
        events like quit and open file events. Otherwise, if we install our own
        handlers, we easily end up breaking functionality the 3rd party
        application depends on.
     */
    NSAppleEventManager *eventManager = [NSAppleEventManager sharedAppleEventManager];
    [eventManager setEventHandler:self
                      andSelector:@selector(appleEventQuit:withReplyEvent:)
                    forEventClass:kCoreEventClass
                       andEventID:kAEQuitApplication];
    [eventManager setEventHandler:self
                      andSelector:@selector(getUrl:withReplyEvent:)
                    forEventClass:kInternetEventClass
                       andEventID:kAEGetURL];
}

// called by QCocoaIntegration's destructor before resetting the application delegate to nil
- (void) removeAppleEventHandlers
{
    NSAppleEventManager *eventManager = [NSAppleEventManager sharedAppleEventManager];
    [eventManager removeEventHandlerForEventClass:kCoreEventClass andEventID:kAEQuitApplication];
    [eventManager removeEventHandlerForEventClass:kInternetEventClass andEventID:kAEGetURL];
}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
    Q_UNUSED(aNotification);
    inLaunch = false;
    // qt_release_apple_event_handler();


    // Insert code here to initialize your application
}



- (void)application:(NSApplication *)sender openFiles:(NSArray *)filenames
{
    Q_UNUSED(filenames);
    Q_UNUSED(sender);

    for (NSString *fileName in filenames) {
        QString qtFileName = QCFString::toQString(fileName);
        if (inLaunch) {
            // We need to be careful because Cocoa will be nice enough to take
            // command line arguments and send them to us as events. Given the history
            // of Qt Applications, this will result in behavior people don't want, as
            // they might be doing the opening themselves with the command line parsing.
            if (qApp->arguments().contains(qtFileName))
                continue;
        }
        QWindowSystemInterface::handleFileOpenEvent(qtFileName);
    }

    if (reflectionDelegate &&
        [reflectionDelegate respondsToSelector:@selector(application:openFiles:)])
        [reflectionDelegate application:sender openFiles:filenames];

}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender
{
    // If we have a reflection delegate, that will get to call the shots.
    if (reflectionDelegate
        && [reflectionDelegate respondsToSelector:
                            @selector(applicationShouldTerminateAfterLastWindowClosed:)])
        return [reflectionDelegate applicationShouldTerminateAfterLastWindowClosed:sender];
    return NO; // Someday qApp->quitOnLastWindowClosed(); when QApp and NSApp work closer together.
}


- (void)applicationDidBecomeActive:(NSNotification *)notification
{
    if (reflectionDelegate
        && [reflectionDelegate respondsToSelector:@selector(applicationDidBecomeActive:)])
        [reflectionDelegate applicationDidBecomeActive:notification];

/*
    onApplicationChangedActivation(true);

    if (!QWidget::mouseGrabber()){
        // Update enter/leave immidiatly, don't wait for a move event. But only
        // if no grab exists (even if the grab points to this widget, it seems, ref X11)
        QPoint qlocal, qglobal;
        QWidget *widgetUnderMouse = 0;
        qt_mac_getTargetForMouseEvent(0, QEvent::Enter, qlocal, qglobal, 0, &widgetUnderMouse);
        QApplicationPrivate::dispatchEnterLeave(widgetUnderMouse, 0);
        qt_last_mouse_receiver = widgetUnderMouse;
        qt_last_native_mouse_receiver = widgetUnderMouse ?
            (widgetUnderMouse->internalWinId() ? widgetUnderMouse : widgetUnderMouse->nativeParentWidget()) : 0;
    }
*/
}

- (void)applicationDidResignActive:(NSNotification *)notification
{
    if (reflectionDelegate
        && [reflectionDelegate respondsToSelector:@selector(applicationDidResignActive:)])
        [reflectionDelegate applicationDidResignActive:notification];

/*
    onApplicationChangedActivation(false);

    if (!QWidget::mouseGrabber())
        QApplicationPrivate::dispatchEnterLeave(0, qt_last_mouse_receiver);
    qt_last_mouse_receiver = 0;
    qt_last_native_mouse_receiver = 0;
    qt_button_down = 0;
*/
}

- (void)setReflectionDelegate:(NSObject <NSApplicationDelegate> *)oldDelegate
{
    [oldDelegate retain];
    [reflectionDelegate release];
    reflectionDelegate = oldDelegate;
}

- (NSMethodSignature *)methodSignatureForSelector:(SEL)aSelector
{
    NSMethodSignature *result = [super methodSignatureForSelector:aSelector];
    if (!result && reflectionDelegate) {
        result = [reflectionDelegate methodSignatureForSelector:aSelector];
    }
    return result;
}

- (BOOL)respondsToSelector:(SEL)aSelector
{
    BOOL result = [super respondsToSelector:aSelector];
    if (!result && reflectionDelegate)
        result = [reflectionDelegate respondsToSelector:aSelector];
    return result;
}

- (void)forwardInvocation:(NSInvocation *)invocation
{
    SEL invocationSelector = [invocation selector];
    if (reflectionDelegate && [reflectionDelegate respondsToSelector:invocationSelector])
        [invocation invokeWithTarget:reflectionDelegate];
    else
        [self doesNotRecognizeSelector:invocationSelector];
}

- (void)getUrl:(NSAppleEventDescriptor *)event withReplyEvent:(NSAppleEventDescriptor *)replyEvent
{
    Q_UNUSED(replyEvent);
    NSString *urlString = [[event paramDescriptorForKeyword:keyDirectObject] stringValue];
    QWindowSystemInterface::handleFileOpenEvent(QUrl::fromNSURL([NSURL URLWithString:urlString]));
}

- (void)appleEventQuit:(NSAppleEventDescriptor *)event withReplyEvent:(NSAppleEventDescriptor *)replyEvent
{
    Q_UNUSED(event);
    Q_UNUSED(replyEvent);
    [NSApp terminate:self];
}

- (void)qtDispatcherToQAction:(id)sender
{
    Q_UNUSED(sender);
    [qtMenuLoader qtDispatcherToQPAMenuItem:sender];
}

@end
