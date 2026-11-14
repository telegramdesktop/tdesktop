/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "data/data_msg_id.h"
#include "test/test_widget_ops.h"
#include "twidget/sdk_policy.h"

#include <crl/crl_time.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// A played widget bot: the requests of an armed widget message answered
// from a table at the runtime's request seam, in place of Tgw::Bridge, the
// server and the message's bot. Reference: docs/helpers/test_widget_bot.md.

namespace Test {

// The request seam Tgw::Runtime::botRequested() calls on the main thread
// after its own refusal and right before Tgw::Bridge::request(), for the
// message |id|: its HistoryItem::globalId(), the FullMsgId and its
// session's uniqueId(). |wallet| is the request's WidgetRequest::wallet,
// whether it asked for the wallet. Returns false, without calling |respond|
// or recording anything, unless Active() and |id| is armed by
// ArmWidgetBot(). For an armed id it records the request, |wallet| as
// given, in the id's record, then calls |respond| exactly once,
// synchronously, with an answer (never a failure), and returns true.
// |wallet| never changes the answer. |respond| is never kept or copied.
// The Release branch of this module defines this function alone,
// returning false.
[[nodiscard]] bool PlayWidgetBot(
	GlobalMsgId id,
	const std::string &data,
	const Fn<void(const twidget::WidgetRequestOutcome&)> &respond,
	bool wallet = false);

// Everything below is Debug-only: declared here and deliberately left
// undefined in a Release build, because no production code reads it.

// A request's exact data -> the `result` its answer carries, where "{n}"
// becomes the count of requests with that data for the message so far, this
// one included, "{from}" "Conformance" and "{time}" "12:00"; nullopt is an
// answer without result. Data not in the table is answered without result.
using WidgetBotTable = std::map<std::string, std::optional<std::string>>;

// One armed message's record: its table and every request it played.
// Main thread only. Opaque; read it with ReadWidgetBot().
class WidgetBotRecord;

// One played request.
struct WidgetBotRequest {
	std::string data;
	// The answer's result, its templates filled; nullopt when it had none.
	std::optional<std::string> result;
	// Since the record was armed.
	crl::time ms = 0;
	// The record's requests with this exact data, this one included.
	int n = 0;
	// Whether |data| is in the table.
	bool listed = false;
	// Whether the request asked for the wallet (WidgetRequest::wallet).
	bool wallet = false;
};

// Main thread. Arms |id|, the session-qualified id the message's
// HistoryItem::globalId() returns, with |table| and returns its fresh,
// empty record. The record belongs to the message: every request of every
// host built for it lands there, and its counts never restart while it is
// armed. A request made before the arming went to the product's bridge
// and is not in it, so arm before the message's first host can start.
// Arming an armed id replaces the armed record with a fresh one; the
// replaced record stays readable by whoever holds it. A message of another
// session with the same FullMsgId stays unarmed.
[[nodiscard]] std::shared_ptr<WidgetBotRecord> ArmWidgetBot(
	GlobalMsgId id,
	WidgetBotTable table);

// Main thread. Later requests for |id| are not played; the record keeps
// what it played and stays readable. Pass the id that was armed, as kept by
// the caller, not one rebuilt later; no other id is touched.
void DisarmWidgetBot(GlobalMsgId id);

// Main thread. Every request the record played, in order.
[[nodiscard]] std::vector<WidgetBotRequest> ReadWidgetBot(
	const WidgetBotRecord &record);

// Main thread, synchronous, needs Active(). Drives PlayWidgetBot() under
// message ids of peer 0, which no real message has in any session: unarmed
// and other-session identity (false, the responder never called, nothing
// recorded), a listed answer with its templates and counts, an answer
// without result for a listed entry with none and for unlisted data,
// records in order, a request that asked for the wallet recorded as such
// beside the same data without it, and reading after a disarm, which plays
// no more.
// Disarms its ids before it returns. The caller turns each row into a
// Check().
[[nodiscard]] std::vector<WidgetOpsSelfCheckRow> RunWidgetBotSelfCheck();

} // namespace Test
