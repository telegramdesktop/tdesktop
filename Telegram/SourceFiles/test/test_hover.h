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

// Ui::AbstractButton::mousePressEvent calls checkIfOver(e->pos()), which
// latches StateFlag::Over for a point inside the widget, and the only path
// that clears it again is leaveEventHook. Until Test::Click and Test::Drag
// ended with a QEvent::Leave nothing in the harness ever delivered one, so
// a synthetically clicked button stayed hovered for the rest of the
// process: Ui::RoundButton::paintEvent kept painting the style's textBgOver
// over its textBg, and a later Test::DeriveBand under textBg found no row
// whose modal background was the fill it was handed and returned ok=0 with
// an empty rows list - a reading no scenario can tell apart from the widget
// being absent, which is the very class of false negative
// Test::DiscriminatingScan exists to refuse on the probe side. Run 1 of
// 2026/08/27/present-unreadable-address-and-signing-refusal lost eight
// failures across two unrelated checks to it and spent two TEST_FLAW
// classifications before the mechanism was found.
//
// AppendClickHoverSelfTest is that instrument measuring itself. It builds
// its own two-button fixture - one Ui::RoundButton it clicks and then
// drags, and one identically styled button no input helper ever touches -
// and reads both out of the same grab, through test_ink.h, against the two
// fills st::defaultActiveButton names. The control is what makes the
// subject's reading mean something: it separates "the click stopped
// latching hover" from "the measurement changed". Every geometry number
// comes from the live scaled style token, and the stage that opens the
// self-test proves at runtime that the two fills of the theme actually
// running are further apart than kBackgroundSame, so the colour premise is
// falsifiable rather than assumed.
//
// It needs no session, no chats list, no network and no account fixture.
// The only thing it asks of the process is a primary window to parent the
// fixture to, and a missing one is reported as a named fixture gate instead
// of crashing. It appends its own teardown as the last of its five stages,
// and it emits no deliberate failure - every stage is expected to PASS on a
// healthy harness. Its before-leg, the failing reading this repair removed,
// is produced by reverting the two DeliverPointerLeave calls in Test::Click
// and Test::Drag and re-running the identical scenario, never by a stage
// that fails on purpose.
void AppendClickHoverSelfTest(not_null<Runner*> runner);

} // namespace Test
