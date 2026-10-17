/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "mtproto/details/mtproto_serialized_request.h"
#include "test/test_probe.h"
#include "test/test_rpc_fixture.h"

namespace MTP {
class Instance;
} // namespace MTP

namespace Test {

class Runner;

// The controlled Gram wallet fixture's shared request intercept.
//
// Where it came from. Every Gram wallet campaign ran the controlled fixture
// - a Test::GramIntercept hook beside Wallet::GramFixture - from overlay
// code copied from campaign to campaign, and every copy inherited two
// defects. Run 1 of
// 2026/10/02/keep-the-sending-row-one-stable-row-until-the-final-row
// lost its third leg about 55 s after the press with
// worker=[queued=4 started=11 finished=10], and at its teardown the queued
// fixture send's wallet.sendTransfer reached test DC 2 (answered 400
// WALLET_TRANSFER_SEND_FAILED). Four gram-wallet campaigns each repaired the
// stall by hand, each differently: an empty NFT answer (twice), an rpc_error
// for every provider call, and a per-leg drain plus a teardown seal.
//
// Why the engine worker starves. Wallet::Engine runs one serial worker.
// StatuslessHost::execute_statusless posts each provider request to main as
// a toncenter.performApiRequest and blocks that worker until the body, a
// failure, a cancel or the request's own 15 s timeout. A suppressed and
// unanswered provider call therefore holds the worker for the full 15 s,
// and every job queued behind it waits. Each send box queues the comment
// recipient resolution at open (resolveCommentRecipientAttempt, retried
// kCommentRecipientRetries = 3 times kCommentRecipientRetryDelay = 1000 ms
// apart through base::call_delayed, outside the worker queue) and the fee
// preview after typing, and GramFixture::Install's first poll queues the
// collectibles refresh (refresh_nfts). A later press queues behind all of
// them, and Engine::stopClient cannot free a worker blocked in one.
//
// The overlay hook. Paste this as the first statement of
// MTP::details::Session::sendPrepared (mtproto/session.cpp):
//
// #ifdef _DEBUG
// 	if (Test::InterceptWalletRequest(_instance, request)) { // OVERLAY.
// 		return;
// 	}
// #endif // _DEBUG
//
// sendPrepared is the single main-thread funnel into a session's send map.
// Every MTP::Instance::Private first send and resend reaches it - delayed,
// dependent, auth-waiter, migrate, MSG_WAIT_* and connection re-init -
// while SessionPrivate's own resends re-queue only requests that were
// already sent, which a suppressed request never is. Its |request| is the
// very SerializedRequest that storeRequest registered, so its requestId is
// the key MTP::Instance::hasCallback, processCallback and cancel find the
// callbacks by. The hook stays in the overlay: this module adds no
// production seam.
//
// Overlay rules.
// - Arm with the instance the wallet session sends through,
//   arm(&session->mtp()): MtpInterceptRequester's identity() is that
//   pointer.
// - Fix the fixture route when the send is QUEUED. The overlay's
//   Session::startSend captures Test::GramOwns(this) before
//   _engine->run(...) and moves it into the job, so a fixture-queued send
//   always takes the fixture path (placeholder BOC), even when it enters
//   the engine after disarm, and never the real engine path with its own
//   provider calls and broadcast.
// - Report an engine worker that does not exist yet as an idle reading
//   (absent = true, zero counters), never as a missing one: a missing
//   reading refuses (WorkerUnread). Read the counters under the worker
//   mutex.
// - Answer from onRegistered by POSTING (crl::on_main or a zero timer).
//   Never answer inside intercept() or onRegistered: both run inside
//   sendPrepared, in the middle of MTP's request bookkeeping.
// - Optionally guard after the hook: under Test::Active(), a request that
//   IsWalletTransferRequest() matches and still reaches the line after the
//   hook raises Unexpected. A crash sends nothing.
//
// Main thread only. InterceptWalletRequest asserts it, and nothing here
// locks.
//
// Release policy. A held provider call is answered through its real
// registered callback with a controlled rpc_error 400 TIMEOUT
// (DeliverControlledRpcError). Wallet::Api::request hands it on as
// Gram::ApiError{ 400, "TIMEOUT" }, which Wallet::Api::IsTimeoutError
// classifies as the host's own expiry (kTimeout), so the engine job blocked
// on it finishes now instead of after 15 s. AppendWalletEngineDrain turns
// releasing on before each send box, releases everything held, waits for
// readiness and LEAVES RELEASING ON, so the open box's own comment
// resolution and its retries, the fee preview and the press are released
// at once too: the resolution reports Unknown after its retries, a failed
// fee estimate only sets previewError, and the fixture's press action
// injects its own quote. Turn releasing off only for a deliberate hold - a
// teardown leg, or a check whose subject is the box's own provider outcome
// - and answer those calls yourself from onRegistered, then markAnswered();
// the release skips a call already answered. Waiting the provider timeouts
// out is the forbidden technique this replaces: it costs 15 s per call in
// series, and two campaigns recorded it as forbidden.
//
// Readiness, pure. readiness(now, worker, quiet) is Ready only when the
// caller's engine worker reading is present and idle (queued == 0, started
// == finished), no provider call is held (suppressed, unanswered, and its
// requester still waiting), and no provider call was registered, released
// or answered within |quiet| before |now|. 1500 ms outlasts the 1000 ms
// comment-recipient retry delay, which base::call_delayed schedules outside
// the worker queue, where no worker reading can see it. It reads no clock
// of its own, so a self-test can probe both sides of the window exactly.
//
// A drain or press-gate refusal is a FIXTURE GATE: a harness or environment
// TEST_FLAW carrying the worker and held readings, never a product FAIL.
//
// Teardown order, and why. teardown(quiesce) runs:
//   1. seal();
//   2. stop releasing;
//   3. release every held provider call - this frees the worker now, so
//      whatever was queued behind it runs while the seal already holds;
//   4. cancel every other still-registered suppressed request;
//   5. the caller's |quiesce| (Wallet::GramFixture::Quiesce);
//   6. disarm().
// The seal comes BEFORE disarm because Run 1 disarmed first: its queued
// fixture send entered the engine afterwards, the routed wallet.sendTransfer
// was issued on main after armed = false, nothing intercepted it, and it
// reached the network. A sealed intercept keeps suppressing
// wallet.sendTransfer, and a provider POST /api/v2/jsonRPC whose JSON method
// is sendBoc (the product's IsSendBocRequest, the engine's only broadcast
// builder), from ANY MTP instance, armed or not, for the rest of the
// process: the seal is never cleared. Every other constructor passes after
// disarm. teardown() is idempotent: call it from the teardown stage and
// again from Runner::onFinish, where a second call does only what is left;
// it is safe on an intercept that was never armed.

// Longer than the 1000 ms kCommentRecipientRetryDelay.
inline constexpr auto kWalletEngineQuiet = crl::time(1500);
inline constexpr auto kWalletEngineDrainTimeout = crl::time(30000);

// How the intercept reaches one requester's registered requests. The MTP
// implementation is MtpInterceptRequester; the self-test brings its own
// callback registry, which is why this interface exists.
class InterceptRequester {
public:
	virtual ~InterceptRequester() = default;

	// Compared with the identity passed to WalletIntercept::arm().
	[[nodiscard]] virtual const void *identity() const = 0;

	// True while |id| still has a registered callback.
	[[nodiscard]] virtual bool waiting(mtpRequestId id) const = 0;

	// Delivers a boxed rpc_error to |id|'s registered callback, reporting
	// it the way DeliverControlledRpcError does.
	[[nodiscard]] virtual ControlledRpcDelivery answerError(
		mtpRequestId id,
		int code,
		const QString &type) = 0;

	virtual void cancel(mtpRequestId id) = 0;

};

// hasCallback / DeliverControlledRpcError / MTP::Instance::cancel through a
// QPointer to |instance|. identity() is the raw |instance| pointer taken
// here, so it still compares after the instance is gone; answerError then
// returns a Rejected delivery whose diagnosis is "instance gone".
[[nodiscard]] std::shared_ptr<InterceptRequester> MtpInterceptRequester(
	not_null<MTP::Instance*> instance);

// One request the intercept took: kept for every request of the armed
// identity and for every wallet transfer, one record per (requester
// identity, request id). |suppressed| means it never reached the network
// through this funnel, |sealedOnly| that only the seal suppressed it,
// |answered| that it was released, cancelled by teardown or marked answered
// by the campaign.
struct WalletInterceptRecord {
	std::shared_ptr<InterceptRequester> requester;
	mtpRequestId id = 0;
	mtpTypeId type = 0;
	QString endpoint;
	crl::time registeredAt = 0;
	crl::time releasedAt = 0;
	bool suppressed = false;
	bool sealedOnly = false;
	bool provider = false;
	bool post = false;
	bool sendBoc = false;
	bool answered = false;
};

// The overlay's engine worker counters, read under the worker mutex. A
// worker that does not exist yet is absent = true with zero counters, an
// idle reading.
struct WalletEngineWorker {
	int queued = 0;
	int started = 0;
	int finished = 0;
	bool absent = false;
};

// queued == 0 and started == finished.
[[nodiscard]] bool WalletEngineWorkerIdle(const WalletEngineWorker &worker);

// "queued=<n> started=<n> finished=<n> absent=<0|1>".
[[nodiscard]] QString WalletEngineWorkerText(const WalletEngineWorker &worker);

// The first state that applies, in this order.
enum class WalletReadinessState {
	Ready,
	WorkerUnread,
	WorkerBusy,
	ProviderHeld,
	Quiet,
};

[[nodiscard]] QString WalletReadinessStateName(WalletReadinessState state);

// |quietFor| is -1 when no provider activity was ever seen. |observation|
// is never empty: it prints every term - the state, the worker, the held
// calls, quietFor, the quiet window and the released count - whichever one
// decided.
struct WalletReadiness {
	WalletReadinessState state = WalletReadinessState::WorkerUnread;
	int held = 0;
	crl::time quietFor = -1;
	QString observation;

	[[nodiscard]] bool ready() const {
		return state == WalletReadinessState::Ready;
	}
};

// The sealed set: wallet.sendTransfer, or a toncenter.performApiRequest that
// is a POST to /api/v2/jsonRPC whose JSON method is "sendBoc" - the same
// test as the product's IsSendBocRequest. Public so the overlay's guard
// after the hook can reuse it.
[[nodiscard]] bool IsWalletTransferRequest(
	const MTP::details::SerializedRequest &request);

// The registry and the decisions. One process-global instance serves the
// campaign (ControlledWalletIntercept); the self-test builds private ones
// that never touch it. Main thread only.
class WalletIntercept final : public base::has_weak_ptr {
public:
	// |probeName| names the log rows; the global uses the default, and the
	// self-test's private intercepts use their own names, so their
	// deliberate control rows are never read as the global's.
	explicit WalletIntercept(QString probeName = u"wallet-intercept"_q);

	// Suppress and record every request whose requester identity() is
	// |identity|. Does not clear the seal.
	void arm(const void *identity);

	// Idempotent and permanent.
	void seal();

	// Stops suppressing the armed identity's requests and stamps
	// disarmedAt(); a no-op when not armed.
	void disarm();

	[[nodiscard]] bool armed() const;
	[[nodiscard]] bool everArmed() const;
	[[nodiscard]] bool sealed() const;

	// crl::now() at the last disarm, 0 before any.
	[[nodiscard]] crl::time disarmedAt() const;

	// The decision for one request about to enter a send map; true means
	// suppress it. Returns false at once, decoding nothing, while never
	// armed and not sealed. Only toncenter.performApiRequest bodies are
	// copied and decoded; every other constructor is judged by its word.
	// (a) Armed and |requester| is the armed identity: recorded suppressed,
	//     a "registered" row, onRegistered(index) for a new record, and a
	//     provider call is counted as provider activity and, while
	//     releasing, released on a later main-thread turn.
	// (b) A wallet transfer while sealed: recorded suppressed and sealedOnly,
	//     a "sealed ... afterDisarmMs=" row (-1 while armed or never
	//     disarmed).
	// (c) A wallet transfer otherwise: recorded NOT suppressed,
	//     passedTransfers() counts it, a "passed wallet transfer" row. A
	//     campaign that ever sees that row stops.
	// (d) Anything else passes with no record.
	// A resend of a request that already has an unanswered record updates
	// that record instead of adding one, and its row says resend=1.
	[[nodiscard]] bool intercept(
		std::shared_ptr<InterceptRequester> requester,
		const MTP::details::SerializedRequest &request);

	// Called inside intercept() for every new record of the armed identity:
	// POST any answer, never deliver it here.
	void onRegistered(Fn<void(int index)> callback);

	// The campaign answered |index| itself. A provider call answered so
	// counts as provider activity, like a release.
	void markAnswered(int index);

	void setReleasing(bool releasing);
	[[nodiscard]] bool releasing() const;

	// Releases every held provider call now; returns how many.
	int releaseHeld();

	// The records index every "...Since" / |from| read takes.
	[[nodiscard]] int mark() const;

	// Read at once: a later intercept() may move the storage.
	[[nodiscard]] const WalletInterceptRecord &record(int index) const;

	// The newest record of |type| at or after |from| that is suppressed,
	// unanswered and still waiting, or -1.
	[[nodiscard]] int latest(mtpTypeId type, int from = 0) const;

	// A provider call that is suppressed, unanswered, and whose requester
	// still waits for it.
	[[nodiscard]] bool held(int index) const;
	[[nodiscard]] int heldCount(int from = 0, int till = -1) const;

	// "id=<id> endpoint=<path> ageMs=<ms>" per held call, or "none".
	[[nodiscard]] QString heldText(int from = 0, int till = -1) const;

	[[nodiscard]] int releasedCount() const;
	[[nodiscard]] int sealedSuppressionsSince(int mark) const;
	[[nodiscard]] int passedTransfers() const;

	// Pure: reads the records and the last provider activity, never a
	// clock. See the contract above.
	[[nodiscard]] WalletReadiness readiness(
		crl::time now,
		std::optional<WalletEngineWorker> worker,
		crl::time quiet = kWalletEngineQuiet) const;

	// See "Teardown order" above.
	void teardown(Fn<void()> quiesce);

	// Log rows only; read them through a mark().
	[[nodiscard]] Probe &probe();

private:
	void release(int index);
	[[nodiscard]] int find(const void *identity, mtpRequestId id) const;
	[[nodiscard]] QString heldList(int from, int till, crl::time now) const;

	Probe _probe;
	std::vector<WalletInterceptRecord> _records;
	Fn<void(int)> _registered;
	const void *_identity = nullptr;
	crl::time _disarmedAt = 0;
	crl::time _providerActivityAt = 0;
	int _released = 0;
	int _passedTransfers = 0;
	bool _armed = false;
	bool _everArmed = false;
	bool _sealed = false;
	bool _releasing = false;

};

// The campaign's process-global intercept. Leaked like a scenario State, so
// the seal outlives the scenario and nothing is destroyed at static exit.
[[nodiscard]] WalletIntercept &ControlledWalletIntercept();

// The one function the overlay hook calls. Asserts the main thread, returns
// false at once while the global was never armed and is not sealed, and
// otherwise asks ControlledWalletIntercept() through an
// MtpInterceptRequester of |instance|.
[[nodiscard]] bool InterceptWalletRequest(
	not_null<MTP::Instance*> instance,
	const MTP::details::SerializedRequest &request);

// What one drain stage observed, for a later stage to read: the records
// mark when the box may open, how many held calls the drain released, and
// when it started and was found ready.
struct WalletDrainGate {
	int requestMark = 0;
	int releasedInDrain = 0;
	crl::time startedAt = 0;
	crl::time readyAt = 0;
};

// One stage, appended before every send box. |run| turns releasing on and
// releases everything held; |until| is readiness(crl::now(), worker())
// (pure); |then| leaves releasing ON, fills |gate| and checks
// "<name> fixture gate: the engine worker is idle, no provider call is held
// and none was registered or released for 1500 ms before the box" with the
// readiness observation, releasedInDrain and requestMark as details. A null
// |worker| reads as missing and refuses. |skip| is the stage's skipReason.
void AppendWalletEngineDrain(
	not_null<Runner*> runner,
	const QString &name,
	not_null<WalletIntercept*> intercept,
	Fn<std::optional<WalletEngineWorker>()> worker,
	std::shared_ptr<WalletDrainGate> gate = nullptr,
	Fn<QString()> skip = nullptr,
	crl::time timeout = kWalletEngineDrainTimeout);

// The press gate: checks "<name> fixture gate: no earlier leg's provider
// call is held at the press", where earlier means a record before
// |boxMark|, with "earlier=[...] since-box=[...] boxMark= worker=[...]" as
// details, so a held call of the open box itself reads as since-box.
void CheckNoEarlierProviderCallHeld(
	const QString &name,
	const WalletIntercept &intercept,
	int boxMark,
	std::optional<WalletEngineWorker> worker = std::nullopt);

// The intercept measuring itself, on private WalletIntercepts and a
// self-test requester that keeps its own callback registry: it needs no
// network, no account, no session and no MTP::Instance, and it never
// touches ControlledWalletIntercept(). Requests are real serialized TL
// bytes (SerializedRequest::Serialize) with synthetic request ids, so the
// decoder runs on what sendPrepared sees; nothing is handed to an
// instance.
// - S1a, the held control: with the release off, a provider call stays
//   held and waiting, never invoked, after a 150 ms wait.
// - S1b, the subject on the same call: releaseHeld() answers it through
//   its registered callback (registered before, consumed after, invoked
//   once) with TIMEOUT, and Wallet::Api::IsTimeoutError accepts the
//   Gram::ApiError built the way Wallet::Api::request builds it.
// - S1c: while releasing, a new provider call is still waiting in the turn
//   intercept() returned and is answered once on a later turn, beside a
//   wallet.getState that stays waiting and is never invoked.
// - S2, pure readings, each refusal beside its own Ready control: a missing
//   worker reading, a busy worker, a held provider call even ten quiet
//   windows after its registration, a release one millisecond inside the
//   quiet window, and a registration inside it that was cancelled and never
//   released. Then AppendWalletEngineDrain on the live clock over one held
//   call, which must release exactly that call and pass no sooner than
//   kWalletEngineQuiet after it started.
// - S3: teardown's quiesce sees the intercept sealed and still armed, the
//   held provider call answered with TIMEOUT and the other suppressed
//   requests cancelled, and a transfer intercepted from inside quiesce is
//   suppressed. After teardown the intercept is disarmed and sealed: a
//   wallet.sendTransfer is suppressed (row afterDisarmMs >= 0), from any
//   requester, and so is a provider jsonRPC sendBoc, while wallet.getState,
//   a provider GET and a non-sendBoc jsonRPC pass. The control is a second
//   intercept armed and disarmed WITHOUT a seal and handed the same
//   serialized transfer, which it lets through (passedTransfers() == 1) -
//   only as a return value and a record under its own probe name. A second
//   teardown then does only what was left.
// The MTP adapter half - MtpInterceptRequester over a real registered
// callback - is proven live by the campaign: no network-free, account-free
// registration exists in a real MTP::Instance without the overlay hook,
// because Instance::send always reaches sendPrepared. It emits no
// deliberate FAIL, and it holds nothing session-owned, so it has nothing to
// tear down.
void AppendWalletInterceptSelfTest(not_null<Runner*> runner);

} // namespace Test
