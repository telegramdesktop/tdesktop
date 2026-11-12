/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QString>

namespace Test {

class Runner;

// An opt-in hold that keeps a scenario's widget popups open when the
// application deactivates. On a locked Windows console Qt closes every
// shown popup on QEvent::ApplicationDeactivate, inside QApplication::notify
// and before any event filter runs. While the hold is armed in a Windows
// build, Core::Sandbox::notify asks Test::KeepPopupsOnDeactivate()
// (declared in core/sandbox.h, defined in this module) and swallows that
// one event when QApplication::activePopupWidget() is set; the hold counts
// it. The application state change and its signal still reach the product,
// and Escape, a click outside, hideMenu() and every other close still close
// the popup. Off Windows the hold stops nothing. Reference:
// docs/helpers/test_popup_hold.md.
//
// Main thread only. One process has one runner and one hold; its callers
// run serially, and there is no nesting.

// The current arm, or after its release the released arm, until the next
// arm resets it. |stopped| counts the deactivations the hold kept from
// closing a popup while armed. Zero means none arrived while a popup was
// open, not that the hold failed.
struct PopupHoldReading {
	QString label;
	int stopped = 0;
	bool armed = false;
};

// Arms the hold and labels its rows with |label|, one token; a no-op unless
// Test::Active(). Arm before opening the popup: the hold never shows,
// activates or focuses anything. A fresh arm resets the reading and, once
// per process, registers ReleasePopupHold(u"finish"_q) with
// runner->onFinish, so every finish path releases it. Arming while armed
// changes nothing and Notes the already-armed row.
void ArmPopupHold(not_null<Runner*> runner, const QString &label);

// Ends the hold, keeps the reading with armed=false and its count, Notes
// the released row and returns true. When not armed it changes nothing,
// Notes the release-skipped row and returns false. Release after the last
// leg that needs the popup open across turns.
bool ReleasePopupHold(const QString &by);

[[nodiscard]] PopupHoldReading ReadPopupHold();

// The rows, each through Test::Note, in ordinary integers:
// - "POPUP_HOLD: armed label=<label> windows=<0|1>";
// - "POPUP_HOLD: already armed label=<label> requested=<label>
//   stopped=<n>";
// - "POPUP_HOLD: stopped n=<n> label=<label>", one per stopped
//   deactivation;
// - "POPUP_HOLD: released by=<by> label=<label> stopped=<n>";
// - "POPUP_HOLD: release skipped by=<by> - not armed".

} // namespace Test
