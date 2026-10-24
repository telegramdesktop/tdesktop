/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "data/data_peer_id.h"

#include <QtCore/QString>
#include <QtCore/QStringList>

#include <vector>

// The widget conformance runner: widget-sandbox's packages/conformance
// automatic and visual sets as local outgoing widget messages in the
// legacy HistoryInner of a Test chat, through the app's own widget view
// (test_widget_fixture.h), each result read from its host's validated ops
// (test_widget_ops.h), written as the one run file of the desktop renderer
// that the package's tools/report.mjs reads. Reference:
// docs/helpers/test_widget_conformance.md.

namespace Test {

class Runner;

// |package| is the absolute packages/conformance directory of the pinned
// widget-sandbox checkout. |chat| is a chat of the Test session the primary
// window shows, loaded, with its folder known and no unread messages, and
// an ordinary, empty compose field (no draft, edit, forward, rich draft or
// voice recording), which each run focuses and never changes.
// |output| is an absolute directory that is absent or empty; empty means
// EvidenceDir() + "conformance". |framework| (the widget-sandbox tag the
// client is on) and |engine| (the campaign's text naming the twidget pin)
// go into the run file as given. |themes| ("light", "dark"), |sets|
// ("auto", "visual") and |ids| (manifest test ids) narrow the selection;
// each empty one selects all, and a name that is none of those is a named
// failure. |token| and |ports| are the network controls, as in
// WidgetAttackAuditArgs: one TCP connection and one UDP datagram from this
// process to 127.0.0.1 on each port, carrying the token; an empty token or
// no port reports the network leg unavailable, never a pass.
struct WidgetConformanceArgs {
	QString package;
	PeerId chat = 0;
	QString output;
	QString framework;
	QString engine;
	QStringList themes;
	QStringList sets;
	QStringList ids;
	QString token;
	std::vector<int> ports;
};

// Appends the runner, after an overlay's session and chats-loaded waits.
// During the call it only reads manifest.json, probe-fixture.json and the
// selected tests' HTML from |package|, selects the runs and registers its
// stages and its Runner::onFinish release. The runs: for each selected
// theme, light first, every selected manifest entry in manifest order, at
// 390 CSS px. An auto entry runs in both themes whatever its manifest
// themes, except the dark start of auto/20-client-behaviour, which is
// omitted and stated as such in the run file; a visual entry runs only in
// its manifest themes, as a load check. The light runs go in the day
// palette and the dark ones in the night palette, switched and restored
// through test_palette.h with its persisted side effects. Package input
// that is missing or invalid refuses the runs it concerns by name before
// anything is injected. The run file is <output>/run.json, following the
// package's run.schema.json plus documented extras; the totals stage
// writes it, or Runner::onFinish on any other exit. It holds only runs
// that completed; every other selected run is named in its
// "incompleteRuns" with the state it reached, never written as a completed
// run. A run file that cannot be written is a failure.
void AppendWidgetConformance(
	not_null<Runner*> runner,
	WidgetConformanceArgs args);

} // namespace Test
