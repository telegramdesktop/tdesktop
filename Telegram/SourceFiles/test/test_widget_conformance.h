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
// voice recording), which each run focuses and never changes. Its kind
// decides which runs the launch hosts: a private message context needs a
// user's chat, a group one a basic group or a megagroup, a copy any chat.
// |output| is an absolute directory that is absent or empty; empty means
// EvidenceDir() + "conformance". |framework| (the widget-sandbox tag the
// client is on) and |engine| (the campaign's text naming the twidget pin)
// go into the run file as given. |themes| ("light", "dark"), |sets|
// ("auto", "visual") and |ids| (manifest test ids) narrow the selection;
// each empty one selects all, and a name that is none of those is a named
// failure. |pictures| (visual manifest ids) asks for a picture of every
// run of those entries in this launch: its whole card at 2x, in bands of
// at most 300 CSS px, saved under <output>/pictures/ and never judged; an
// id that is no visual entry is a named failure, one this launch runs in
// no theme only a note, and empty asks for none. |token| and |ports| are
// the network controls, as in WidgetAttackAuditArgs: one TCP connection
// and one UDP datagram from this process to 127.0.0.1 on each port,
// carrying the token; an empty token or no port reports the network leg
// unavailable, never a pass.
struct WidgetConformanceArgs {
	QString package;
	PeerId chat = 0;
	QString output;
	QString framework;
	QString engine;
	QStringList themes;
	QStringList sets;
	QStringList ids;
	QStringList pictures;
	QString token;
	std::vector<int> ports;
};

// Appends the runner, after an overlay's session and chats-loaded waits.
// During the call it only reads manifest.json, probe-fixture.json and the
// selected tests' HTML from |package|, selects the runs and registers its
// stages and its Runner::onFinish release. Its first stage, the
// self-check, later reads one file of the same pin outside |package|,
// its sibling ../core/tests/fixtures/paint-policy.json, and makes each
// row of RunWidgetPaintPolicyCheck() (test_widget_paint_policy.h) on its
// bytes one Check; a missing or unreadable fixture is one failed Check
// of that stage, which skips no run. The runs: for each selected
// theme, light first, every selected manifest entry in manifest order, at
// 390 CSS px. An auto entry runs in both themes whatever its manifest
// themes, except the dark start of auto/20-client-behaviour and an entry
// whose message is a preview, which the desktop does not build: those are
// omitted in every launch and stated as such in the run file. A visual
// entry runs only in its manifest themes, as a load check, a preview as a
// private message. Each run's message gets the context of its entry's
// first `message` spec's kind and that spec's state, an entry without one
// the private chat's with no state, and its requests are answered from
// the entry's `bot` table (test_widget_bot.h). A run whose context the
// chat cannot host, by |chat|'s id alone, is omitted from this launch for
// a launch on a fitting chat (`elsewhere` in the run file), never read
// from the account during this call; a group run whose loaded chat is a
// broadcast channel is refused before its injection. A run with a picture
// gets it after its load wait, before its log and removal, each band from
// the list's own paint of the same host at the same card size, and the
// totals stage fails a picture missing a band its card needs. The light
// runs go in the day palette and the dark ones in the night palette,
// switched and restored through test_palette.h with its persisted side
// effects. Package input that is missing or invalid refuses the runs it
// concerns by name before anything is injected. The run file is
// <output>/run.json, following the package's run.schema.json plus
// documented extras; the totals stage writes it, or Runner::onFinish on
// any other exit. It holds only runs that completed; every other selected
// run is named in its "incompleteRuns" with the state it reached, never
// written as a completed run. A run file that cannot be written is a
// failure. Before the first run the fixture puts the primary window in the
// one-column layout on its own screen, activating nothing
// (AppendWidgetFixtureOneColumn()); a window left in another layout fails
// a fixture gate, by which every run skips. The window keeps the new
// geometry, and a maximized or full-screen one stays normal, since
// maximizing it again would activate it.
void AppendWidgetConformance(
	not_null<Runner*> runner,
	WidgetConformanceArgs args);

} // namespace Test
