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

// The attack-corpus audit: every HTML file of widget-sandbox's
// corpus/attacks as a local outgoing widget message in the legacy
// HistoryInner of a Test chat, its hosts' validated ops tapped
// (test_widget_ops.h), one real click when it is live, a 1500 ms watch, and
// loopback network controls before and after the corpus. Rows, fields,
// statuses and limits: docs/helpers/test_widget_attacks.md.

namespace Test {

class Runner;

// |corpus| is the absolute corpus/attacks directory. |chat| is a chat of the
// Test session the primary window shows, loaded, with its folder known and
// no unread messages. |only| names the HTML files to run, in corpus order
// whatever its own order; empty runs them all, and a name that is not a
// corpus HTML file is a named failure. |token| and |ports| are the network
// controls: one TCP connection and one UDP datagram from this process to
// 127.0.0.1 on each port, before the first file and after the last, each
// carrying the token; an empty token or no port reports the network leg
// unavailable (TEST_RESULT: N/A), never a pass.
struct WidgetAttackAuditArgs {
	QString corpus;
	PeerId chat = 0;
	QStringList only;
	QString token;
	std::vector<int> ports;
};

// Appends the audit, after an overlay's session and chats-loaded waits.
// During the call it only lists and reads the corpus files and registers
// stages and its Runner::onFinish release. The stages run the tap and
// verdict self-checks, report the corpus, gate the fixture (Test session,
// the chat, widget messages enabled, no layer, no proxy at a control port,
// the window shown and exposed, the chat in the legacy HistoryInner), send
// the "before" controls, then per file: arm the tap for the message's
// session-qualified id, inject the widget message, show it
// (Test::AppendShowInChatList), wait up to 25 s for load, death or failure,
// show and click it when live, watch 1500 ms, read the runtime log, destroy
// the message and wait up to 10 s until every factory and wrapper of its
// tap finished, then write its ATTACK row. A file whose hosts do not finish
// stops the audit: the remaining files are not run. Then the "after"
// controls, the totals and their checks, the teardown and a settle stage.
// Nothing is sent and no dialog is confirmed: a layer is hidden at once and
// recorded. A failed fixture gate leaves every file not run, still with its
// row; the release, registered first, destroys the local messages, disarms
// the taps, closes the sockets and restores the option, the chat and the
// window's exposure on every exit path.
void AppendWidgetAttackAudit(
	not_null<Runner*> runner,
	WidgetAttackAuditArgs args);

} // namespace Test
