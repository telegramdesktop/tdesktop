/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/platform/mac/ui_platform_utility_mac.h"

#include "ui/ui_integration.h"

#include <QtGui/QPainter>
#include <QtGui/QtEvents>
#include <QtGui/QWindow>

#include <Cocoa/Cocoa.h>

extern "C" {
void _dispatch_main_queue_callback_4CF(mach_msg_header_t *msg);
} // extern "C"

namespace Ui {
namespace Platform {

bool IsApplicationActive() {
	return [[NSApplication sharedApplication] isActive];
}

void InitOnTopPanel(not_null<QWidget*> panel) {
	Expects(!panel->windowHandle());

	// Force creating windowHandle() without creating the platform window yet.
	panel->setAttribute(Qt::WA_NativeWindow, true);
	panel->windowHandle()->setProperty("_td_macNonactivatingPanelMask", QVariant(true));
	panel->setAttribute(Qt::WA_NativeWindow, false);

	panel->createWinId();

	auto platformWindow = [reinterpret_cast<NSView*>(panel->winId()) window];
	Assert([platformWindow isKindOfClass:[NSPanel class]]);

	auto platformPanel = static_cast<NSPanel*>(platformWindow);
	[platformPanel setLevel:NSPopUpMenuWindowLevel];
	[platformPanel setCollectionBehavior:NSWindowCollectionBehaviorCanJoinAllSpaces|NSWindowCollectionBehaviorStationary|NSWindowCollectionBehaviorFullScreenAuxiliary|NSWindowCollectionBehaviorIgnoresCycle];
	[platformPanel setFloatingPanel:YES];
	[platformPanel setHidesOnDeactivate:NO];

	Integration::Instance().activationFromTopPanel();
}

void DeInitOnTopPanel(not_null<QWidget*> panel) {
	auto platformWindow = [reinterpret_cast<NSView*>(panel->winId()) window];
	Assert([platformWindow isKindOfClass:[NSPanel class]]);

	auto platformPanel = static_cast<NSPanel*>(platformWindow);
	auto newBehavior = ([platformPanel collectionBehavior] & (~NSWindowCollectionBehaviorCanJoinAllSpaces)) | NSWindowCollectionBehaviorMoveToActiveSpace;
	[platformPanel setCollectionBehavior:newBehavior];
}

void ReInitOnTopPanel(not_null<QWidget*> panel) {
	auto platformWindow = [reinterpret_cast<NSView*>(panel->winId()) window];
	Assert([platformWindow isKindOfClass:[NSPanel class]]);

	auto platformPanel = static_cast<NSPanel*>(platformWindow);
	auto newBehavior = ([platformPanel collectionBehavior] & (~NSWindowCollectionBehaviorMoveToActiveSpace)) | NSWindowCollectionBehaviorCanJoinAllSpaces;
	[platformPanel setCollectionBehavior:newBehavior];
}

void StartTranslucentPaint(QPainter &p, QPaintEvent *e) {
#ifdef OS_MAC_OLD
	p.setCompositionMode(QPainter::CompositionMode_Source);
	p.fillRect(e->rect(), Qt::transparent);
	p.setCompositionMode(QPainter::CompositionMode_SourceOver);
#endif // OS_MAC_OLD
}

void ShowOverAll(not_null<QWidget*> widget, bool canFocus) {
	NSWindow *wnd = [reinterpret_cast<NSView*>(widget->winId()) window];
	[wnd setLevel:NSPopUpMenuWindowLevel];
	if (!canFocus) {
		[wnd setStyleMask:NSUtilityWindowMask | NSNonactivatingPanelMask];
		[wnd setCollectionBehavior:NSWindowCollectionBehaviorMoveToActiveSpace|NSWindowCollectionBehaviorStationary|NSWindowCollectionBehaviorFullScreenAuxiliary|NSWindowCollectionBehaviorIgnoresCycle];
	}
}

void BringToBack(not_null<QWidget*> widget) {
	NSWindow *wnd = [reinterpret_cast<NSView*>(widget->winId()) window];
	[wnd setLevel:NSModalPanelWindowLevel];
}

void DrainMainQueue() {
	_dispatch_main_queue_callback_4CF(nullptr);
}

} // namespace Platform
} // namespace Ui
