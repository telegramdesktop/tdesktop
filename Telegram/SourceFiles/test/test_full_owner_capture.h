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

// The full-owner capture helpers measuring themselves on one synthetic
// composite owner: the shared PostPaintCaptureScope and its receipts
// (test_post_paint.h), the strict render path RenderStrict / CompareStrict,
// the checked full-owner crop PreparedWidgetCapture::prepareCrop and the
// typed MappedRect reading (test_capture.h), and the typed panel sizing
// GrowPanelInnerHeight / RestorePanelSizing (test_panel.h).
//
// The fixture is a typed Ui::SeparatePanel this self-test builds with the
// MakePanel() recipe of test_window_exposure.cpp - the stay-on-top hint
// cleared before any native window, shown without activating, never
// showAndActivate(), showInner(), raise() or activateWindow() - and keeps
// exposed with KeepWindowExposed. Its body is six rows square, one row
// being st::defaultActiveButton.height, and holds an opaque composite
// owner: two exact child paint owners in the first row, each of whose
// paints stamps an independent record of its child, generation, sampler
// receipt index and whether a fixture capture was running; a static
// horizontal QLinearGradient in the second row, whose width is chosen from
// the device pixel ratio so a shifted gradient span differs strongly; a
// real Ui::ScrollArea of five rows; and a bottom overlay one row tall. Row 1
// is wholly visible; row 2 maps whole inside the owner while its scroll
// viewport and the overlay cut it.
//
// Stage order. The fixture stage first: its Check is the fixture gate
// (panel shown, owner six rows square, row 1 resolved, row 2 refused by its
// viewport while its mapping fits the owner). Then, composed inside
// Runner::addGated on that gate:
// - group A, Part A, inside its own nested paint gate: A1 an idle control
//   that finds no product receipt over 500 ms, then one update() whose
//   exact first receipt within 3000 ms decides the paint gate; A2 two
//   samplers sharing a scope, one real update on each child; A3 the same
//   composite capture through two samplers without a scope, which passes
//   only when it exposes the false product classification and recursive
//   extra samples; A4 two coalesced real paints and a genuine later one; A5
//   destruction with a sample queued and a replacement generation; then a
//   release stage outside the paint gate;
// - group B, Part B: B1 measures a full-owner and a shifted direct-region
//   render of one gradient crop with an independent pixel count, B2 the
//   mismatch control, B3 the repeated same path, B4 one changed pixel, B5
//   the extent, format, ratio and outside-crop refusals;
// - group C, Part C: C1 a crop past the owner, C2 the viewport-clipped row
//   with its owner-only and owner-as-origin premises, C3 the joint Part C x
//   Part D typed reading (deleted with either part), C4 the pure size-mode
//   classifier, C5 growth through setInnerSize by a measured extra, C6 the
//   same crop and row accepted whole, C7 the normal restore and C8 a second
//   growth restored by the finish-path call; both restores are
//   Runner::addRecovery stages that run whenever a sizing is owed.
// Last comes the teardown, a recovery stage outside the gate.
//
// Groups B and C render in-process and need the shown panel only, never a
// paint; exposure is not part of the fixture gate. Part B's mismatch
// premise is measured: when B1's independent count finds no differing
// pixel, B2 is a named N/A ("premise not demonstrated: ..."), never a pass.
// A locked console never gates: A1 prints the CONSOLE_LOCK and exposure
// readings on both verdicts and receipts alone decide, so a first receipt
// that is missing or does not reconcile is a FAIL and the rest of group A
// is N/A by that name.
//
// It needs no session, account, network or primary window. While it runs
// its panel floats above other applications' windows. It must not be
// packed beside AppendSeparatePanelWalkSelfTest, which must be the only
// owner of a live Ui::SeparatePanel. It emits no deliberate failure: every
// refusal is observed through pure readings and asserted as a passing Check
// whose details carry it verbatim. Runner::onFinish runs the teardown's
// idempotent release on every finish path: samplers first, then
// RestorePanelSizing, then RestoreWindowExposure, then the panel.
// Reference: docs/helpers/test_full_owner_capture.md.
void AppendFullOwnerCaptureSelfTest(not_null<Runner*> runner);

} // namespace Test
