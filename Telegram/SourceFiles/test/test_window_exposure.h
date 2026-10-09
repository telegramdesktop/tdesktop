/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtGui/QWindow>

class QWidget;

namespace Test {

class Runner;

// Keeps a top-level window that other applications' windows cover exposed,
// by stacking alone, so its widgets keep painting while nothing activates
// the application, changes the focus window or takes keyboard focus.
//
// Why it exists. 2026/10/07/animate-the-sending-row-for-a-collectible-transfer
// (work/test.md, Attempt 1): Run 1 launched while the owner used the
// console, the launch was not brought to the front, and the Money window
// stayed under the frontmost application - its fixture gate failed with
// pages=1 exposed=0 and every later stage was N/A. On macOS the window
// server reports a fully covered window occluded,
// QCocoaWindow::windowDidChangeOcclusionState calls
// handleExposeEvent(QRegion()) (both in qcocoawindow.mm),
// QWindow::isExposed() turns false, QWidgetWindow::handleExposeEvent clears
// Qt::WA_Mapped (qwidgetwindow.cpp), and the repaint manager
// holds every sync while the window is unmapped
// (QWidgetRepaintManager::sync()): no paint, so no post-paint sample,
// no paint-stamped product timer and no frame advance. Run 2 set the hint
// below on the panel's QWindow right after showing it, waited for
// isExposed() in a pure until, and the leg ran end to end. Since then a
// default workspace.py test-run launch of an app bundle on macOS does not
// activate the client (launch_method "background" in its report,
// --activate to opt in), so its windows open behind the frontmost
// application's whether or not anyone uses the console. A window covered
// from the moment it is shown reads isExposed() true until the window
// server first reports an occlusion change for it - such as this helper's
// undo dropping it back behind - so exposed=1 is no proof that it is on
// screen; after such a change a covered window reads unexposed and gets no
// paints. A stage that needs paints calls the helper either way; the
// workspace.py test-run --wait-idle gate does not change that.
//
// The mechanism. QWindow::setFlag(Qt::WindowStaysOnTopHint, true) on
// widget->window()->windowHandle(). On a shown macOS window
// QCocoaWindow::setWindowFlags re-applies the style mask and the window
// level - NSModalPanelWindowLevel, above every application's ordinary
// windows - and calls no ordering, key-window or activation API; product
// makes the same call on the shown call window
// (Calls::Window::setPinnedOnTop). Banned here, and in every caller that
// relies on this to stay off the owner's keyboard:
// QWidget::setWindowFlag(s) on a shown widget (it re-parents and hides the
// window, QWidgetPrivate::setWindowFlags), raise() (QCocoaWindow::raise orders
// front and calls [NSApp activateIgnoringOtherApps:]), activateWindow(),
// QWindow::requestActivate() and Window::Controller::activate() - an
// activated test client would receive the owner's keystrokes.
//
// The reading. ReadWindowExposure only reads, so it is legal in a stage's
// pure until. In the value KeepWindowExposed returns, |exposed|, |active|
// and |stayOnTop| are the values from before the call, and |applied| is
// true only when this call set the hint. |applicationState|, |focusWindow|
// and |focusIdentity| are the process-wide readings, filled for a refused
// reading too, so a caller compares them across the call to prove that
// nothing was activated. WindowExposureText prints it as one row, with
// stacking=macos|none saying whether this platform has the mechanism.
//
// Refusals are returned, never logged, and the first token of |refusal|
// names them, checked in this order:
// - null-widget: no widget was given;
// - minimized: stacking cannot expose a minimized window, and
//   Test::KeepMainWindowNotMarkingRead minimizes the primary window on
//   purpose, so the two levers exclude each other;
// - hidden: the widget's own window carries no QWindow handle - it is
//   never retargeted at some other window -, or it isHidden(), or its
//   QWindow is not visible, for example under Qt::WA_DontShowOnScreen.
//
// It never waits. Exposure arrives asynchronously from the window server,
// so the caller calls the helper, waits for ReadWindowExposure(...).exposed
// in a pure until, and only then asks for the repaint it needs with
// update(): a re-exposed window with nothing dirty is only flushed, not
// painted (the exposed-region QWidgetRepaintManager::sync overload).
//
// The undo. RestoreWindowExposure clears the hint only when that reading
// applied it: a call that found the hint already set applies nothing, so
// its undo leaves the product's own hint alone. It is idempotent and safe
// after the window is gone. Undo before teardown and before reading
// anything the product derives from the hint - Calls::Window::pinnedOnTop()
// reads handle->flags(). QWidget::windowFlags() never carries the helper's
// hint, and a later QWidget::create() re-applies the widget's own flags to
// a fresh QWindow, so a recreated native window loses it. While applied the
// window floats above every application's ordinary windows, where a click
// on it would activate the application, so keep it no longer than the
// paints are needed.
//
// Off macOS it is a documented no-op that still returns its reading
// (applied=0, stacking=none), decided by source inspection. Windows and X11
// never turn a covered window unexposed - qwindowswindow.cpp clears its
// Exposed flag only on hide, and the xcb plugin has no occlusion handling -
// so there is nothing to repair, while the flag would restyle the frame
// there (applyWindowFlags and SWP_FRAMECHANGED) or schedule a native
// window recreation at the next show (qxcbwindow.cpp). Wayland marks a
// window whose frame callbacks stop unexposed, but xdg-shell gives clients
// no stacking control, so the helper cannot repair it there.
// RestoreWindowExposure changes nothing off macOS either.
//
// Marking read. isExposed() is a term of MainWindow::markingAsRead()
// (mainwindow.cpp), so on an unlocked console exposing the primary
// window with auto-scroll-inactive-chat on can make it mark messages read
// (see test_marking_read.h).
struct WindowExposure {
	QPointer<QWindow> window;
	QString identity;
	bool exposed = false;
	bool active = false;
	bool stayOnTop = false;
	bool applied = false;
	Qt::ApplicationState applicationState = Qt::ApplicationInactive;
	QPointer<QWindow> focusWindow;
	QString focusIdentity;
	QString refusal;
};

[[nodiscard]] WindowExposure ReadWindowExposure(QWidget *widget);
[[nodiscard]] WindowExposure KeepWindowExposed(QWidget *widget);
void RestoreWindowExposure(const WindowExposure &reading);
[[nodiscard]] QString WindowExposureText(const WindowExposure &reading);

// Needs no session, network or account, and never touches the primary
// window. Two legs - a parentless top-level Ui::RpWidget, a titled window
// at the normal level like the main window, and a Ui::SeparatePanel with
// its stay-on-top hint cleared before its first show - each shown with
// Qt::WA_ShowWithoutActivating and never raised, left by a settle stage
// until it has been shown for 2000 ms, then covered by a harness-owned
// opaque titled top-level window that accepts no focus - a frameless Qt
// top level was measured not to occlude it on macOS. The settle is there
// because a freshly shown window gets one more layer display about half a
// second after it is shown, and Qt counts any layer display as an expose,
// so a target covered sooner reads isExposed() true while the window
// server still reports it occluded; the self-test therefore covers the
// target only after it has settled.
// The control must read the target unexposed under the cover within
// 3000 ms before the helper, or the leg writes a named N/A, never a pass;
// the premise proves the covered target, which nothing asks to repaint,
// stays unexposed and gets no paint for 500 ms; the helper must expose it
// within 3000 ms with no repaint requested, with isActive(), the focus
// window and the application state unchanged and the widget's own flags
// untouched; only then does the self-test ask for one update(), which must
// deliver at least one paint within 3000 ms; the undo must clear the hint
// it set and leave a hint it found alone. Off macOS or on a locked console
// every leg stage is a named N/A, while the refusal stage runs everywhere.
// After a default workspace.py test-run launch, which does not activate the
// client, a target that another application's window covers from the
// moment it is shown cannot be turned unexposed by the harness cover (see
// "Why it exists" above), so that leg is the named N/A, never a pass.
// A per-leg teardown stage and Runner::onFinish, registered at append time,
// destroy every harness window, so none is left over the owner's console on
// any exit path.
void AppendKeepWindowExposedSelfTest(not_null<Runner*> runner);

} // namespace Test
