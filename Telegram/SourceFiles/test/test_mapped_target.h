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

// Test::ReadMappedTarget measuring itself on the shape that paid for it:
// a painted owner taller than its Ui::ElasticScroll, an inner origin, and
// a painted child whose origin-local rectangle is fully inside the owner
// while a negative inner offset still leaves part of it outside the
// scroll. HistoryInner-local containment of a Gram card passed for that
// same reason; CaptureMappedRect then refused the mapped HistoryWidget
// rect. ElasticScroll::viewport() is Dummy and returns the inner widget,
// so treating that pointer as the clipper would recreate the bug.
//
// The fixture is not a chat. It parents a painted Ui::RpWidget to the
// primary window, hosts an ElasticScroll with tall content and a
// distinctive child at a nonzero origin offset, and a sibling
// Ui::ScrollArea so the QAbstractScrollArea::viewport() branch runs.
// At scrollTop 0 the elastic child is only partly inside the scroll and
// still inside the owner: MappedTargetReady is false and
// PreparedWidgetCapture::prepare(owner, origin, rect) stores that
// refusal without logging a FAIL. After ElasticScroll::scrollToY of the
// same local rect, the same origin/owner/rect are ready, inner y is
// nonzero, and CaptureMappedTarget saves the full requested frame.
// Overlap-only, empty, hidden, and MisframedDetails on an outside owner
// rect are quoted as passing Checks. A stray nonpainting widget whose
// geometry fits is still refused as a capture root. PaintingLayerRoot
// does not resolve the fixture.
//
// It needs no session, no chats list, no network and no account fixture.
// A missing primary window is a named fixture gate. It emits no
// deliberate failure. Its before-leg is produced by reverting the
// complete-target helper and re-running this scenario, never by a stage
// that fails on purpose.
void AppendMappedTargetSelfTest(not_null<Runner*> runner);

} // namespace Test
