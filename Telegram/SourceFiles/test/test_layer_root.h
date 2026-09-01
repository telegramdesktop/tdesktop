/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

namespace Test {

class Runner;

// Which boxes the harness refuses as a render root is not the question a
// reader assumes it is. Ui::BoxContent's constructor sets
// Qt::WA_OpaquePaintEvent (ui/layers/box_content.h:117-119), and the
// blank-root refusal short-circuits to an empty string for any widget
// carrying it, so a plain Ui::GenericBox paints its own background and is
// accepted as its own render root. setNoContentMargin(true) clears the
// attribute again (:224-230) - 53 call sites under Telegram/SourceFiles/ do
// exactly that - and only then does the box paint nothing of its own and get
// every frame it offers refused. Run 1 of
// 2026/08/28/complete-server-history-details-hash-and-paging handed such a
// box to Runner::captureAndInspect and could only end in a stage timeout,
// although the refusal it printed already named the Ui::BoxLayerWidget to
// grab instead. Test::PaintingLayerRoot is that answer, and this module is
// the resolver measuring itself.
//
// It shows two real boxes through Window::Controller::show with
// anim::type::instant, one plain and one that called
// setNoContentMargin(true), and reads both through the same
// PreparedWidgetCapture every scenario captures through. The plain box is the
// control: it is accepted, and that is what stops the refusal quoted against
// the second box from reading as something the fixture manufactured. It then
// captures the box through the Ui::BoxLayerWidget the resolver answers, crops
// the box's own mapped rect out of that accepted frame to show the root
// really holds the box's pixels rather than merely being some ancestor that
// paints, and refuses a null, a stray widget and the window itself by name so
// the resolver is shown to discriminate instead of answering everything.
//
// It needs no session, no chats list, no network and no account fixture. The
// only thing it asks of the process is a primary window to show a layer in,
// and a missing one is reported as a named fixture gate instead of crashing.
// It appends its own teardown as the last of its five stages, and it emits no
// deliberate failure: every refusal it demonstrates is observed through
// PreparedWidgetCapture::prepare(), which stores its reason and logs nothing,
// and is asserted as a passing Check whose details carry the refusal verbatim.
// Its before-leg, the stage timeout this repair removed, is produced by
// reverting the resolver and re-running the identical scenario, never by a
// stage that fails on purpose.
void AppendPaintingLayerRootSelfTest(not_null<Runner*> runner);

} // namespace Test
