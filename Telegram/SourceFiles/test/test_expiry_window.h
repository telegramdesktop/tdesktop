/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <crl/crl_time.h>

// Typed served-expiry observations, immutable cutoff windows and a boundary
// poll for an overlay that samples product behavior at a deadline derived
// from a served expiry: a temporary link's Copy/Share cutoff, a frozen Share
// closing at its own deadline, a renderer's deadline, a natural expiry, a
// clock resynchronization.
//
// Why it exists. Run 3 of
// 2026/10/17/open-ones-own-qr-box-without-a-username-with-temporary-only-codes
// read a missing dynamic property as 0, passed a fractional-gap check with
// that zero expiry, compared 0==0 as its clock control, and let later
// actions sample one shared mutable cutoff. Here every premise is typed and
// may be absent, every refusal is named before anything can be scheduled,
// and each output freezes its own window. Examples, the refusal table and
// the self-test: docs/helpers/test_expiry_window.md.

namespace Test {

class Runner;

// Milliseconds since the epoch on the server-shifted wall clock
// (base::unixtime's shift applied to the local wall clock). Absent and
// non-positive values are refused, never treated as 0.
struct ServerClockReading {
	std::optional<int64> serverMs;
};

// One QDateTime::currentMSecsSinceEpoch() read plus 1000 times
// base::unixtime's whole-second shift, taken as base::unixtime::now() minus
// time(nullptr), the clock that shift is added to, so Qt's millisecond
// rounding or tick granularity never moves a reading by a second. The shift
// is read between two equal time(nullptr) reads, up to three times when
// that second rolls over, and the reading is absent if it never settles.
// Fractional milliseconds come from the local clock; a resynchronization
// moves it by whole seconds. It is never a product countdown display value
// and never the product's monotonic deadline.
[[nodiscard]] ServerClockReading ReadServerClock();

// The oracle side, recorded where the served value arrives (an RPC reply
// hook): the exact object holding the served value (the box, not an output
// the product closes at its cutoff), the caller's generation counter for
// that served value, the caller's literal link identity (compared exactly,
// never printed), the served expiry (unixtime seconds) and
// ReadServerClock() at receipt.
struct ExpiryObservation {
	QPointer<QObject> owner;
	std::optional<uint64> generation;
	QString link;
	std::optional<TimeId> servedExpires;
	ServerClockReading receipt;
};

// The product side, read now: owner, link and expiry through the product's
// own typed seam (a value the product holds until the link changes, such as
// the box's current link expiry, not a frozen copy it clears at its own
// cutoff), the caller's current generation counter, and a fresh
// ReadServerClock(). Never fill it from the observation: a caller without a
// typed product seam cannot freeze a window. The product's monotonic
// deadline (for example LinkExpiry::deadline) is never an input.
struct ExpiryReading {
	QPointer<QObject> owner;
	std::optional<uint64> generation;
	QString link;
	std::optional<TimeId> productExpires;
	ServerClockReading clock;
};

// Names (ExpiryRefusalName) and meanings:
// - none: accepted;
// - not-evaluated: the default of every result; never accepts;
// - owner-not-live: the observed or window owner was never recorded or is
//   destroyed;
// - wrong-owner: the current owner is another object or null;
// - generation-absent: either side has none;
// - obsolete-generation: the current generation differs;
// - link-absent: either side is empty;
// - wrong-link: the current link differs;
// - served-expiry-absent, served-expiry-not-positive: the observation's
//   served expiry;
// - receipt-absent, receipt-not-positive: the observation's receipt clock;
// - product-expiry-absent, product-expiry-not-positive: the reading's
//   product expiry;
// - product-expiry-differs: the product's value is not the served one;
// - clock-absent, clock-not-positive: the reading's fresh clock;
// - cutoff-not-ahead: the fresh clock is not at least |minimumAhead| and
//   strictly before the cutoff;
// - boundary-unconfirmed: a watch reached its cutoff or boundary before any
//   poll confirmed the full premise.
enum class ExpiryRefusal {
	None,
	NotEvaluated,
	OwnerNotLive,
	WrongOwner,
	GenerationAbsent,
	ObsoleteGeneration,
	LinkAbsent,
	WrongLink,
	ServedExpiryAbsent,
	ServedExpiryNotPositive,
	ReceiptAbsent,
	ReceiptNotPositive,
	ProductExpiryAbsent,
	ProductExpiryNotPositive,
	ProductExpiryDiffers,
	ClockAbsent,
	ClockNotPositive,
	CutoffNotAhead,
	BoundaryUnconfirmed,
};

[[nodiscard]] QString ExpiryRefusalName(ExpiryRefusal refusal);

// |label| is public text naming the window in details, never a link or a
// secret. The cutoff lies |cutoffLead| ms before the served expiry: the
// earliest moment the product may react (3000 for a product's
// kShareCutoff; a few hundred ms for a natural expiry the product acts on
// at the expiry itself, with a watch offset past it). Freezing requires
// the fresh clock to be at least |minimumAhead| ms (inclusive) and strictly
// before the cutoff, so actions above the cutoff have time.
struct ExpiryWindowArgs {
	QString label;
	crl::time cutoffLead = 0;
	crl::time minimumAhead = 0;
};

struct ExpiryFreeze;

// Freezes one window from a served observation and a separate product
// reading. First refusal, in order: owner (owner-not-live, then
// wrong-owner), generation (generation-absent, then obsolete-generation),
// link (link-absent, then wrong-link), served expiry (absent, not
// positive), receipt (absent, not positive), product expiry (absent, not
// positive, differs from served), clock (absent, not positive), then
// cutoff-not-ahead. The details line, every number an ordinary integer and
// a not-positive value printed as its number:
//   window=<label> refusal=<name> owner=<live:Class|not-live>
//   current=<same|other|absent> generation=<n|none>/<n|none>
//   link=<same|other|absent> served=<n|none> receiptServerMs=<n|none>
//   product=<n|none> clockServerMs=<n|none> cutoffLead=<ms>
//   minimumAhead=<ms> cutoffServerMs=<n|none> untilCutoffMs=<n|none>
// where the generations are the observation's and the reading's,
// cutoffServerMs is printed only from a positive served expiry, and
// untilCutoffMs only from it and a positive clock.
[[nodiscard]] ExpiryFreeze FreezeExpiryWindow(
	const ExpiryWindowArgs &args,
	const ExpiryObservation &observation,
	const ExpiryReading &current);

// One frozen Share/render/natural window, immutable once frozen: a refresh
// freezes a new window, and existing windows never change. It exists only
// through FreezeExpiryWindow, so no window skips validation. It holds its
// owner weakly: owner() is null once that object is destroyed, and a new
// object at the same address never revives it. cutoffServerMs() is
// int64(servedExpires()) * 1000 - cutoffLead().
class ExpiryWindow final {
public:
	ExpiryWindow(const ExpiryWindow &other) = delete;
	ExpiryWindow &operator=(const ExpiryWindow &other) = delete;

	[[nodiscard]] const QString &label() const;
	[[nodiscard]] QObject *owner() const;
	[[nodiscard]] uint64 generation() const;
	[[nodiscard]] const QString &link() const;
	[[nodiscard]] TimeId servedExpires() const;
	[[nodiscard]] int64 receiptServerMs() const;
	[[nodiscard]] int64 frozenServerMs() const;
	[[nodiscard]] crl::time cutoffLead() const;
	[[nodiscard]] int64 cutoffServerMs() const;

private:
	friend ExpiryFreeze FreezeExpiryWindow(
		const ExpiryWindowArgs &args,
		const ExpiryObservation &observation,
		const ExpiryReading &current);

	ExpiryWindow(
		const ExpiryWindowArgs &args,
		const ExpiryObservation &observation,
		int64 frozenServerMs);

	const QString _label;
	const QPointer<QObject> _owner;
	const QString _link;
	const uint64 _generation = 0;
	const int64 _receiptServerMs = 0;
	const int64 _frozenServerMs = 0;
	const crl::time _cutoffLead = 0;
	const TimeId _servedExpires = 0;

};

// |window| is non-null exactly when |refusal| is none; |details| is the
// one line documented at FreezeExpiryWindow.
struct ExpiryFreeze {
	std::shared_ptr<const ExpiryWindow> window;
	ExpiryRefusal refusal = ExpiryRefusal::NotEvaluated;
	QString details;

	[[nodiscard]] bool frozen() const {
		return window && (refusal == ExpiryRefusal::None);
	}
};

struct ExpiryPlan {
	std::optional<int64> untilCutoffMs;
	ExpiryRefusal refusal = ExpiryRefusal::NotEvaluated;
	QString details;

	[[nodiscard]] bool accepted() const {
		return untilCutoffMs.has_value() && (refusal == ExpiryRefusal::None);
	}
};

// Plans one sample of a frozen window against a fresh product reading.
// First refusal, in order: owner-not-live (the window's owner is gone),
// wrong-owner, generation-absent, obsolete-generation, link-absent,
// wrong-link, product expiry (absent, not positive, differs from the
// window's served expiry), clock (absent, not positive). The served expiry
// and the receipt are the window's, validated once at freeze. On
// acceptance untilCutoffMs is cutoffServerMs() minus the reading's clock;
// it may be at or below zero past the cutoff, so an action above the
// cutoff requires its own margin. There is no value on refusal, so nothing
// can be scheduled from a refused premise. The details line:
//   window=<label> refusal=<name> owner=<live:Class|not-live>
//   current=<same|other|absent> generation=<window>/<n|none>
//   link=<same|other|absent> served=<window> product=<n|none>
//   clockServerMs=<n|none> cutoffServerMs=<n> untilCutoffMs=<n|none>
[[nodiscard]] ExpiryPlan PlanExpirySample(
	const ExpiryWindow &window,
	const ExpiryReading &current);

enum class ExpiryWatchState {
	Waiting,
	Reached,
	Refused,
};

// The boundary sampler of one frozen window, polled by the caller's
// |until| with a fresh ExpiryReading. Its boundary is the cutoff plus
// |offset|; the offset may be negative (a sample before the cutoff).
// Construction and poll contract:
// (0) built from a freeze that is not frozen(), the watch starts Refused
//     with that freeze's refusal (not-evaluated when it reads none), with a
//     null window and zero polls;
// (a) after Reached or Refused, poll() changes and counts nothing;
// (b) otherwise ++polls; the reading's clock refuses clock-absent /
//     clock-not-positive;
// (c) let |stop| be the earlier of the cutoff and the boundary. Strictly
//     before |stop|, PlanExpirySample must accept, or the watch refuses
//     with its refusal; on acceptance ++confirmations, the reading is kept
//     as the confirmed one and untilBoundaryMs is the boundary minus it;
// (d) at or past |stop|, zero confirmations refuses boundary-unconfirmed;
//     otherwise only the owner is validated (owner-not-live / wrong-owner,
//     so destruction or replacement during the wait never transfers):
//     generation, link and product expiry are the product's reaction,
//     sampled by the caller, not refused here;
// (e) strictly before the boundary it stays Waiting with untilBoundaryMs
//     the boundary minus this reading; at or past it, Reached with
//     reachedServerMs this reading and untilBoundaryMs cleared;
// (f) every poll recomputes from its own reading, so a clock jump in
//     either direction moves the boundary in wall time and never changes
//     the window.
// refusal() is none unless the state is Refused; callers decide by
// state(). boundaryServerMs() is present when a window exists,
// untilBoundaryMs() only while Waiting, from the last poll's clock. The
// details line:
//   window=<label|none> state=<waiting|reached|refused> refusal=<name>
//   offset=<ms> cutoffServerMs=<n|none> boundaryServerMs=<n|none>
//   polls=<n> confirmations=<n> confirmedServerMs=<n|none>
//   untilBoundaryMs=<n|none> reachedServerMs=<n|none> lateMs=<n|none>
//   premise=[<premise>]
// where lateMs is reached minus boundary, and the premise is the last
// poll's PlanExpirySample details before |stop|, or
// "owner=<live:Class|not-live> current=<same|other|absent>
// clockServerMs=<n|none>" for a poll at or past it or with a refused clock
// (empty before the first poll). Main thread only (QPointer).
class ExpiryBoundaryWatch final {
public:
	ExpiryBoundaryWatch(const ExpiryFreeze &freeze, crl::time offset);

	ExpiryWatchState poll(const ExpiryReading &current);

	[[nodiscard]] ExpiryWatchState state() const;
	[[nodiscard]] ExpiryRefusal refusal() const;
	[[nodiscard]] const std::shared_ptr<const ExpiryWindow> &window() const;
	[[nodiscard]] std::optional<int64> boundaryServerMs() const;
	[[nodiscard]] std::optional<int64> untilBoundaryMs() const;
	[[nodiscard]] std::optional<int64> reachedServerMs() const;
	[[nodiscard]] int polls() const;
	[[nodiscard]] int confirmations() const;
	[[nodiscard]] QString details() const;

private:
	void refuse(ExpiryRefusal refusal);

	const std::shared_ptr<const ExpiryWindow> _window;
	const crl::time _offset = 0;
	std::optional<int64> _untilBoundaryMs;
	std::optional<int64> _confirmedServerMs;
	std::optional<int64> _reachedServerMs;
	QString _premise;
	ExpiryWatchState _state = ExpiryWatchState::Waiting;
	ExpiryRefusal _refusal = ExpiryRefusal::None;
	int _polls = 0;
	int _confirmations = 0;

};

// The module measuring itself over literal caller-owned values and plain
// QObject owners: no session, network, account, widget, fixture secret or
// server fixture mutation, and it never calls base::unixtime::update().
// Nine then-only stages "expiry window self-test: <leg>", in order:
// - arm: the fixture gate (three live distinct owners), one distinct
//   non-empty name per refusal, and defaults that never accept (an empty
//   freeze, plan, observation and reading; a watch from an empty freeze);
// - positive observation: a current positive observation freezes a window
//   whose fields are the observation's, keeps its receipt fraction, plans
//   and reaches its boundary, while the product and served values come
//   from separate sources (a differing product value refuses by name);
// - freeze refusals, plan refusals: every refusal by name, absent and zero
//   pairs that never compare equal, the inclusive minimumAhead edge, and
//   watches that are Refused with nothing to wait on;
// - simultaneous windows: coexisting Share, natural and render windows
//   keep their own identities and cutoffs across a refresh, and
//   interleaved watches reach only at their own boundaries;
// - destruction and replacement: a same-address replacement owner, another
//   live owner or a new generation never inherits an old window;
// - boundary: the full premise before the cutoff, only the owner in the
//   product's reaction window, boundary-unconfirmed, a late reach and a
//   negative offset;
// - resynchronization: forward and backward clock jumps recompute the
//   remaining time and keep the window, an absent or zero clock, a moved
//   link or a rebased product expiry refuses, and fractional server time
//   differs where a whole-second countdown display does not;
// - real server clock: the ReadServerClock() composition over literal reads
//   (a mid-second Qt read, one rounded up into the next second, one a tick
//   behind time(nullptr)) keeps the milliseconds and adds exactly the
//   whole-second shift; then one read-only ReadServerClock() lies between
//   the Qt millisecond reads around it, shifted by an independently read
//   whole-second shift.
// A finish release frees the owners. Every Check is expected to PASS, with
// no deliberate FAIL and no N/A row. Reference:
// docs/helpers/test_expiry_window.md.
void AppendExpiryWindowSelfTest(not_null<Runner*> runner);

} // namespace Test
