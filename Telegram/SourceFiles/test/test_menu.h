/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "test/test_runner.h"

#include <QtCore/QString>

namespace Test {

// A Ui::PopupMenu cannot be waited on through the visibility of the
// Ui::Menu it wraps. PopupMenu::startShowAnimation() ends with
// hideChildren() (ui/widgets/popup_menu.cpp:842-868, the call at :865), and
// the only path that shows them again during a normal open is the
// Ui::PostponeCall the final paintEvent queues once the show animation has
// drawn its last frame (:369-377). showAnimationCallback() merely calls
// update() (:896-898), and the other two showChildren() calls sit inside
// prepareCache()'s grab-and-restore (:789) and the opacity path that
// re-shows a menu after a hide (:890). So the inner menu is hidden for the
// whole show animation and comes back only from a side effect no -testagent
// run is guaranteed to reach. A readiness over menu()->isVisible() then
// waits until focusOutEvent -> hideMenu() -> hideEvent() -> deleteLater()
// takes the popup away underneath it (:617-631), which is how runs 4 and 5
// of 2026/08/28/complete-server-history-details-hash-and-paging ended.
//
// The opposite mistake costs a run just as surely: a one-shot grab taken in
// the turn that called popup() lands on a show-animation frame with almost
// nothing painted on it, which is run 2's blank p1e_menu_peer_nohash.
//
// So the readiness here carries content identity only - the widget is a
// Ui::PopupMenu, it is visible, its geometry is non-empty and it carries
// actions - and the shared PreparedWidgetCapture decides when the frame is
// good, because refusing a blank frame is exactly the check a menu capture
// needs and it already exists. showingContent is read and printed in every
// details line so a log reader sees it, and it is never part of the gate.
//
// Two more facts a caller gets wrong otherwise. Test::PaintingLayerRoot()
// must not be used on a popup: PopupMenu::init() sets
// Qt::WA_NoSystemBackground (:126), so the harness's blank-root refusal
// never applies to it, and the popup is its own top-level window
// (Qt::FramelessWindowHint | Qt::BypassWindowManagerHint | Qt::Popup |
// Qt::NoDropShadowWindowHint, :118-121), so that walk refuses on its first
// hop. And the hidden-children premise above is platform-dependent rather
// than universal: init() takes _useTransparency from
// Platform::TranslucentWindowsSupported() (:128) and startShowAnimation()
// returns before hideChildren() when it is false. useTransparency() is
// public for exactly that reason, every reading below reports it, and the
// self-test asserts it as a named fixture gate instead of passing vacuously
// on a host without translucent windows - on Windows it is an inline
// return true (ui/platform/win/ui_utility_win.h:19-21).
//
// This is its own module rather than part of test_capture.h because
// CapturePopupMenu appends Runner stages, and test_runner.cpp already
// includes test_capture.h: the reverse include would invert the harness's
// layering and make its most-included module runner-aware. The three pure
// readings below would fit test_capture.h's QWidget-only vocabulary
// unchanged; the stage appender is the whole of what decides this.
struct PopupMenuReading {
	bool isMenu = false;
	bool visible = false;
	bool transparent = false;
	bool showingContent = false;
	int width = 0;
	int height = 0;
	int actions = 0;
	QString identity;
};

// One reading, taken once, so a snapshot can be asserted stages after the
// turn it was taken in and so a pass and a refusal print the same fields.
[[nodiscard]] PopupMenuReading ReadPopupMenu(QWidget *widget);

// Content identity only, and never menu()->isVisible(): the popup exists,
// is visible, has non-empty geometry and carries actions.
[[nodiscard]] bool PopupMenuReady(QWidget *widget);

// The same reading as text, for a stage's timeoutDetails and for a Check's
// details. Names a null and a widget that is not a Ui::PopupMenu, and
// otherwise carries showingContent together with the reason it is reported
// and never required.
[[nodiscard]] QString PopupMenuDetails(QWidget *widget);

// Captures an open Ui::PopupMenu, or one that |open| opens. |open| runs in
// its own stage, and only when the resolver does not already answer a ready
// menu; the helper never calls popup() itself, because popup() on an empty
// menu hides and deleteLater()s it (popup_menu.cpp:945-958) and only the
// caller knows the position and the way the product opens its menu.
void CapturePopupMenu(
	not_null<Runner*> runner,
	const QString &name,
	Fn<QWidget*()> resolve,
	Fn<void()> open = {},
	Fn<void(QWidget*, const QImage &)> inspect = {},
	crl::time timeout = kDefaultStageTimeout);

// This helper measuring itself, in six stages. It opens a real
// Ui::PopupMenu of its own over the primary window and, in the turn that
// opened it, shows the two predicates disagreeing: the content-identity
// readiness accepts while the inner menu is still hidden and a one-shot
// prepared grab still refuses. It then captures through both branches of
// CapturePopupMenu - the already-open one and, after closing the menu, the
// one that opens it - and asserts the refusal texts of a null, a widget
// that is no menu, an empty menu and a popup handed to the painting-layer
// root resolver.
//
// It needs no session, no chats list, no network and no account fixture.
// The only thing it asks of the process is a primary window to parent the
// menus to, and a missing one is reported as a named fixture gate instead
// of crashing. It appends its own teardown last, and it emits no deliberate
// failure: every refusal it demonstrates is observed through a value that
// logs nothing - PopupMenuReady returning false, PopupMenuDetails's text,
// PreparedWidgetCapture::prepare()'s pendingReason() - and is asserted as a
// passing Check whose details carry the refused reading verbatim. Its
// before-leg, the wedged stage this helper removes, is produced by gating
// on menu()->isVisible() and re-running the identical scenario, never by a
// stage that fails on purpose.
void AppendPopupMenuCaptureSelfTest(not_null<Runner*> runner);

} // namespace Test
