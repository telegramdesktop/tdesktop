/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "data/data_peer_id.h"
#include "test/test_widget_ops.h"

#include <QtCore/QString>
#include <QtCore/QStringList>

#include <crl/crl_time.h>

#include <optional>
#include <vector>

// The widget stress runner: widget-sandbox's packages/stress kit, one load
// ladder after another, each level a local widget message in a Test chat
// shown through the app's own widget view (test_widget_fixture.h), its host
// read through test_widget_host.h and its validated ops through
// test_widget_ops.h, written as the desktop run file of one pass that the
// package's tools/report.mjs reads. Reference:
// docs/helpers/test_widget_stress.md.

namespace Test {

class Runner;

// CLIENTS.md's two host configurations.
enum class WidgetStressPass {
	// The measure table: compat, the quotas and caps out of the way, applied
	// to each level's host options through test_widget_host.h.
	Measure,
	// The product configuration: Tgw::Runtime's own options, unchanged.
	Production,
};

// |package| is the absolute packages/stress directory of the pinned
// widget-sandbox checkout. |chat| is the Test chat that shows the levels.
// |output| is the absolute path of the pass's run file. A launch whose every
// selected level has an entry without a runner error merges into it: its
// ladders' entries replace theirs, the file keeps manifest order, its first
// date and every note and accepted launch it had; a file it cannot merge
// with (another kit, build, engine, device, pass or configuration) is
// refused by the first key that differs and left unchanged. Any other
// launch writes <output>.launch-<loads>.json beside it instead, never the
// run file. |loads| are manifest load ids, at least one, each known; their
// levels run in manifest order, whatever the order given. |framework| (the
// widget-sandbox tag), |engine| (the text naming the twidget revision) and
// |buildNote| (appended to clientBuild) go into the run file as given,
// |notes| after the runner's own. |warmupMs| and |measureMs| replace
// manifest.run's durations for a smoke: a file with either differing says
// so in its notes, and its requested configuration keeps it from merging
// with a file of the manifest's durations.
struct WidgetStressArgs {
	QString package;
	PeerId chat = 0;
	QString output;
	WidgetStressPass pass = WidgetStressPass::Measure;
	QStringList loads;
	QString framework;
	QString engine;
	QString buildNote;
	QStringList notes;
	std::optional<crl::time> warmupMs;
	std::optional<crl::time> measureMs;
};

// Appends the runner, after an overlay's session and chats-loaded waits;
// reads the package now and changes nothing until its stages run. The
// stages: the fixture's gates; the day palette with the default day theme
// kept over it; the primary window given Qt::WindowStaysOnTopHint on its
// QWindow and moved wholly onto the screen with the highest refresh rate
// (ties: the primary), 64 px inside its
// available area, at most 1000 logical px tall and as wide as a body one
// logical px narrower than columnMinimalWidthLeft + columnMinimalWidthMain
// (plus the filters sidebar when it is shown) needs, so the app shows only
// the chat in its one-column layout, with the text focus cleared and the
// cursor moved off it; gates on that screen, that layout and the hint;
// the list settled and no update of the window for 2000 ms with no widget
// injected, then one screen capture of the window's frame; then each
// selected level in manifest order: a check that no other widget is
// alive, its watched injection, a wait of at most 60 s for loaded or dead,
// the card shown whole and the list's scroll held 3350 ms, the warm-up,
// the measuring window (a harness worker thread reads the console lock and
// the windows over the card every 500 ms inside it), after the window of
// the launch's first level that loaded and showed its card one screen crop
// of that card, its entry, its removal with the host barrier, and
// 1 s before the next level or 20 s after a load's last one. Then the kept
// theme and the palette are restored, the filter and the hint released,
// the fixture closed and the launch published as |args| says. A failed
// gate, a level's runner error or a host barrier that did not pass stops
// the ladder, and such a launch only writes its launch file. Its
// Runner::onFinish, registered at once, releases the update filter, the
// stay-on-top hint (only one it set), the theme and the fixture on every
// path and writes an unpublished launch to its launch file. It never
// changes a display mode. The window keeps its new place and size: the
// account remembers them.
void AppendWidgetStress(not_null<Runner*> runner, WidgetStressArgs args);

// Synchronous and pure: reads no Telegram state and no file. Literal
// readings through the runner's own arithmetic, serialization and merge:
// manifest selection in manifest order and unknown loads refused by name;
// exact-window update intervals 8, 8, 8 and 33 ms giving count 4, p50 8,
// p95 33, max 33 and a null dropped; renderer work groups M2 T1 F3 and
// T1 F2 after a partial first group giving samples 6 and 3 whether their
// frame() attempts replayed or not, the attempts counted 2 replayed and 1
// unreplayed, trailing work kept apart, and an overflowed, late-reserved,
// empty or foreign work log refused with null fields; the in-window
// console-lock and covering-window samples' first failure latched with
// its offset as the level's runner error, their count and cost recorded,
// and a window no sample saw refused; host rates over the snapshots' seconds;
// data-stress values clipped at the window's start and end with the
// largest maxGap between, and foreign, unparsable or backward values
// refused with nulls; a counter reset named and null; a never-loaded death
// entry; a stop the desktop decided itself merged into dead under a
// substantive host reason, with its estimated diedAtMs; the envelope's
// names, its device with the window's size, layout and filters sidebar,
// and requested and applied configurations; integers
// without a fraction; a ladder's replacement keeping manifest order, the
// first date, every note and each launch once; a merge with a different
// device, or a different window, refused naming device; and the launch
// acceptance. The caller turns each row into a Check().
[[nodiscard]] std::vector<WidgetOpsSelfCheckRow> RunWidgetStressSelfCheck();

} // namespace Test
