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
// Three stages then measure the typed MappedRect path on a painted child
// translated by a nonzero x and y inside the owner's header band, a child
// hanging over the owner's right edge and a parentless widget that is never
// shown. "typed coordinate owners map one translated child identically":
// MappedRect::Local of the child's rect and MappedRect::InParent of its
// geometry resolve to the same full bounds, equal to an independent
// Ui::MapFrom; the untyped reading of the geometry with the child as origin
// resolves at the doubled position (the defect shape is present), while the
// typed Local of that geometry and InParent of the child's rect are refused
// as a coordinate mismatch naming the requested rect, the bounds, the
// origin, the painted child and the owner, and the typed prepare stores the
// same refusal. "typed refusals": with the ElasticScroll back at the top,
// the overhanging child and the clipped elastic child are refused with
// their full requested size mapped, the parentless widget is refused as
// detached (InParent) and as the wrong owner (Local) like the stray band,
// a hidden child's InParent rect is refused, and the typed prepare still
// refuses the uniform band as blank and the stray as a nonpainting root.
// "typed painted crop": the typed prepare accepts the child's complete
// extent from both spaces with byte-equal images, and the typed
// CaptureMappedTarget saves mapped_target_typed_local. Every refusal is
// stored and asserted as a passing Check.
//
// It needs no session, no chats list, no network and no account fixture.
// A missing primary window is a named fixture gate. It emits no
// deliberate failure. Its before-leg is produced by reverting the
// complete-target helper and re-running this scenario, never by a stage
// that fails on purpose.
void AppendMappedTargetSelfTest(not_null<Runner*> runner);

} // namespace Test
