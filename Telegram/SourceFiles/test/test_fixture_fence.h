/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_set.h"
#include "data/data_msg_id.h"
#include "mtproto/details/mtproto_serialized_request.h"
#include "mtproto/mtp_instance.h"
#include "test/test_probe.h"

#include <QtCore/QPointer>

#include <vector>

// Keeps local fixture ids off the network: an overlay registers the message
// ids and synthetic peer ids of the local fixtures it builds, and under
// -testagent a request that names one is refused before the send map and
// recorded. Usage, matching limits, what the refused request's sender
// observes and how a scenario ends it: docs/helpers/test_fixture_fence.md.
//
// The overlay hook, in mtproto/session.cpp. Include this header under
// #ifdef _DEBUG with an "// OVERLAY." mark, and paste this at the start of
// MTP::details::Session::sendPrepared, before its DEBUG_LOG and the send
// map insertion, and AFTER Test::InterceptNotifyRequest and
// Test::InterceptWalletRequest (and the optional wallet guard) when the
// overlay uses them, so it inspects exactly the requests that would
// otherwise enter the send map:
//
// #ifdef _DEBUG
// 	if (Test::FenceFixtureRequest(_instance, request)) { // OVERLAY.
// 		return;
// 	}
// #endif // _DEBUG
//
// The hook never answers or cancels: a refused request stays registered in
// its MTP::Instance and is never written. Main thread only; nothing locks.
// Everything here is defined only in a Debug build; its only caller outside
// test/ is that overlay hunk under _DEBUG.

namespace Test {

class Runner;

enum class FixtureIdKind {
	Message,
	Peer,
};

// |value| is the message id, or the bare peer id the wire carries
// (PeerId::value & PeerId::kChatTypeMask).
struct FixtureIdMatch {
	FixtureIdKind kind = FixtureIdKind::Message;
	uint64 value = 0;

	friend inline constexpr bool operator==(
		FixtureIdMatch,
		FixtureIdMatch) = default;
};

// One refused request. |instance| is the MTP::Instance it was handed to,
// null once that instance is destroyed (and always in the self-test);
// |requestId| is the key MTP::Instance::hasCallback,
// DeliverControlledRpcError and MTP::Instance::cancel take; |method| is the
// generated dumper's outer constructor name, e.g.
// messages_getMessagesReactions; |matches| holds every registered value the
// request named, once each, in dump order.
struct FixtureFenceRefusal {
	QPointer<MTP::Instance> instance;
	mtpRequestId requestId = 0;
	QString method;
	std::vector<FixtureIdMatch> matches;
};

enum class FixtureFenceState {
	Clean,
	Refusals,
	NothingInspected,
};

// NothingInspected when inspect() never ran (the hook is not pasted), else
// Refusals when any request was refused, else Clean. |observation| is
// "state=<clean|refusals|nothing-inspected> inspected=<n> registered=<n>
// refusals=<n> rows=[<row>; ...]" with the counts in HelperNumberFormat()
// and every refusal row, or rows=[none].
struct FixtureFenceVerdict {
	FixtureFenceState state = FixtureFenceState::NothingInspected;
	int inspected = 0;
	int registered = 0;
	int refusals = 0;
	QString observation;

	[[nodiscard]] bool clean() const {
		return state == FixtureFenceState::Clean;
	}
};

// One registry and its decisions. The process uses ProcessFixtureFence();
// the self-test builds private ones that never touch it. Main thread only.
class FixtureFence final {
public:
	// |probeName| names the rows: the process fence uses the default, the
	// self-test's private fences their own.
	explicit FixtureFence(QString probeName = u"fixture-fence"_q);

	// Idempotent and process-long; there is no unregister. Register BEFORE
	// the fixture is published. |id.msg| must be a positive TL int; it
	// matches in any request, whatever peer that names, and |id.peer| only
	// labels the "registered message:<n> peer=<PeerId::value>" row written
	// for a new value.
	void registerMessage(FullMsgId id);

	// |peer| must be nonzero. Its bare id matches whatever peer kind a
	// request names; a new value writes "registered peer:<bare>
	// peer=<PeerId::value>".
	void registerPeer(PeerId peer);

	// The decision for one request about to enter a send map: true means
	// refuse it. Counts every call. With nothing registered it returns
	// false at once. Otherwise only a body word equal to a registered
	// message id, or an adjacent low/high word pair equal to a registered
	// bare peer id, makes it decode the body through the generated TL
	// dumper; a refusal needs a "<n> [INT]" value equal to a registered
	// message id or a "<n> [LONG]" value equal to a registered bare peer
	// id, outside quoted strings. A body the dumper cannot decode
	// completely there is a harness failure (Unexpected), never a pass. A
	// refusal appends one record and one "refused method=<method>
	// request=<id> ids=<kind>:<value>,..." row; a resend is inspected and
	// recorded again.
	[[nodiscard]] bool inspect(
		MTP::Instance *instance,
		const MTP::details::SerializedRequest &request);

	[[nodiscard]] int inspectedCount() const;

	// The end of the refusal records now: take one before the action.
	[[nodiscard]] int mark() const;

	// Refusals recorded at or after |from|, a mark() of this fence.
	[[nodiscard]] int refusalCount(int from = 0) const;

	// Valid until the next inspect(): copy it before calling anything that
	// can send a request.
	[[nodiscard]] const FixtureFenceRefusal &refusal(int index) const;

	// Pure: reads the counters and records, never a clock.
	[[nodiscard]] FixtureFenceVerdict verdict() const;

	// Log rows only; read them through a mark().
	[[nodiscard]] Probe &probe();

private:
	Probe _probe;
	base::flat_set<int32> _messages;
	base::flat_set<uint64> _peers;
	std::vector<FixtureFenceRefusal> _refusals;
	int _inspected = 0;

};

// The process-global fence the overlay hook consults. Leaked, so it lives
// until the process exits.
[[nodiscard]] FixtureFence &ProcessFixtureFence();

// The one function the overlay hook calls. Asserts the main thread and
// returns Test::Active() && ProcessFixtureFence().inspect(...).
[[nodiscard]] bool FenceFixtureRequest(
	not_null<MTP::Instance*> instance,
	const MTP::details::SerializedRequest &request);

// One stage whose |then| writes Check(verdict.clean(), "<what>: the fixture
// fence inspected this run's requests and refused none",
// verdict.observation) over ProcessFixtureFence(): PASS only for Clean, a
// FAIL naming the state for Refusals and for NothingInspected. The window
// is the whole process up to that stage.
void AppendFixtureFenceCheck(not_null<Runner*> runner, const QString &what);

// The fence measuring itself on private fences, over real serialized TL
// bytes (SerializedRequest::Serialize) with synthetic request ids that are
// never handed to an MTP::Instance; it never touches ProcessFixtureFence().
// S1 an empty fence passes a registered-looking poll and getChannels and
// writes no row; S2 a message id is refused in a mixed id vector while its
// neighbour, a channel LONG equal to it and its decimal inside a quoted
// string pass; S3 a peer id is refused while its neighbour and two INT
// halves equal to its words pass; S4 the three verdict states. No network,
// no session, no deliberate FAIL, nothing to tear down.
void AppendFixtureFenceSelfTest(not_null<Runner*> runner);

} // namespace Test
