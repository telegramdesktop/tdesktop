/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QString>

#include <vector>

namespace Test {

// One name per distinct round-trip diagnosis, so a refusal says in the log
// why it refused instead of only that it did, and a reader never has to
// work the reason back out of the tallies.
enum class RoundTripState {
	Paired,
	NoIssue,
	AmbiguousIssue,
	NoAnswer,
	NotLater,
	AmbiguousAnswer,
	OutstandingBeforeMark,
};

[[nodiscard]] QString RoundTripStateName(RoundTripState state);

// One row inside a window with the time it was recorded. The two are one
// value so a row can never be handed another row's time - the defect a
// parallel crl::time vector indexed at mark + i produces silently.
struct TimedRow {
	QString row;
	crl::time at = 0;
};

// The result of one issue -> answer correlation. |observation| is never
// empty: it carries the window bounds, the tallies and the rows the
// verdict was read from, so a caller printing the verdict prints the
// evidence it judged. |preIssues| and |preAnswers| count this key's rows
// before the mark by role, which is what separates an in-window answer that
// may belong to a request already in flight from one that cannot.
// |roundTripMs| is 0 on every state but Paired, and a Paired verdict is only
// ever reached with a strictly positive interval - there is no path that
// fills it from an issue and an answer this verdict did not positively pair.
struct RoundTrip {
	RoundTripState state = RoundTripState::NoIssue;
	crl::time issueAtMs = 0;
	crl::time answerAtMs = 0;
	crl::time roundTripMs = 0;
	int issues = 0;
	int answers = 0;
	int dropped = 0;
	int preIssues = 0;
	int preAnswers = 0;
	QString observation;

	[[nodiscard]] bool paired() const {
		return state == RoundTripState::Paired;
	}
};

// An append-only record of observations, readable only through a window.
//
// A premise that reads every row a run has produced answers from rows an
// earlier stage created, so it stops describing the action under test. Four
// disposable overlays lost a run to exactly that: one read a writer probe
// over the whole history and reported a premise as violated off a row its
// own fixture was required to create, and the same repair exposed a second
// check that had been passing vacuously off that row in both prior runs.
// Bracketing a slice by wall time instead has the same failure — a slow
// neighbouring surface lands its rows inside the bracket.
//
// So there is deliberately no accessor over the whole history. Take mark()
// immediately before the action, pass that mark to every query about it, and
// the window is part of the call rather than something to remember.
//
// A row also carries the time it was recorded. Reading "when did this
// happen" used to mean keeping a std::vector<crl::time> and pushing into it
// beside every record() call, then indexing it at mark + i; one missed
// push_back on one path shifts every later index and the reader silently
// returns another row's time.
//
// And a request is related to its reply by key, never by position. Run 12 of
// 2026/08/26/settle-wallet-lists-gate-and-slice-state paired an issue list
// with an arrival list by index and printed
// "roundTripMs=-618 ... pairs=1 (issues=1 arrivals=2)": a request issued
// before the mark answered inside the window, so the arrival list was
// shifted by one and an orphan was paired with the wrong issue. It was
// emitted as a Note, so nothing failed — the run reported a number that
// decided nothing, which cost the campaign's last normal run plus a two-run
// recovery. So the only way to read a round trip here is
// checkRoundTripSince, which pairs by key, discards and names every answer
// that is not strictly later than its issue, and refuses rather than
// reporting an interval it did not positively pair.
class Probe final {
public:
	explicit Probe(QString name);

	// Records one row and logs it as "<name>: <row>".
	void record(const QString &row);

	// Records one row as the issue for |key|, or as its answer, keeping
	// |key| and the recording time structurally beside the row - never
	// parsed back out of the row text, so a key that contains the row's own
	// separators cannot re-pair anything. Both sides go into this one probe
	// so a single mark() brackets them: that is what makes "the issue was
	// taken before the mark and its answer arrived inside the window" a case
	// the window can actually see, instead of a one-element shift nobody
	// notices. Each logs "<name>: <row> role=issue|answer key=<key>", and
	// windowDetails() prints that same string.
	void recordIssue(const QString &key, const QString &row);
	void recordAnswer(const QString &key, const QString &row);

	// The end of the history right now. Take one immediately before the
	// action under test.
	[[nodiscard]] int mark() const;

	[[nodiscard]] std::vector<QString> rowsSince(int mark) const;
	[[nodiscard]] int countSince(int mark) const;
	[[nodiscard]] int countSince(int mark, const QString &part) const;
	[[nodiscard]] bool sawSince(int mark, const QString &part) const;

	// Plain record() rows inside the window, with the time each was
	// recorded, so measuring when something happened never needs a
	// crl::time vector kept in lockstep with record().
	//
	// A keyed row's time is deliberately never reported here. Two filtered
	// reads - one for role=issue key=K, one for role=answer key=K - would
	// hand back exactly the two index-pairable lists this class exists to
	// prevent, and subtracting the first arrival from the first issue is
	// run 12's technique verbatim on the correlator's own rows. A keyed
	// row's time reaches a caller only as RoundTrip::issueAtMs /
	// answerAtMs, on a verdict the probe positively paired.
	[[nodiscard]] std::vector<TimedRow> timedRowsSince(int mark) const;
	[[nodiscard]] std::vector<TimedRow> timedRowsSince(
		int mark,
		const QString &part) const;

	// PASS/FAIL on what the window holds, logging the window bounds and
	// every row inside it either way, so a failure names the rows it judged
	// instead of only the verdict.
	void checkSawSince(int mark, const QString &part, const QString &what);
	void checkNoneSince(int mark, const QString &part, const QString &what);
	void checkCountSince(
		int mark,
		const QString &part,
		int expected,
		const QString &what);

	// Pairs |key|'s issue with its own answer inside the window: exactly one
	// issue, and exactly one answer strictly later than it. Every answer at
	// or before that issue is a discarded orphan, counted and named in the
	// observation rather than consumed by the issue it did not answer.
	// Anything else refuses. Pure - safe to poll from a stage's |until|.
	//
	// A key whose rows before the mark do not balance is counted as leaving
	// a request outstanding at the mark, so an in-window answer for it may
	// be that older request's rather than this issue's, and the correlation
	// refuses instead of reporting an interval it cannot attribute; a key
	// whose answers can arrive without a recorded issue would need a
	// per-request key for that count to be exact. Only a strictly earlier
	// orphan credits against the count: an answer recorded before the
	// in-window issue cannot be that issue's, so it discharges one
	// outstanding request, while one recorded in the same millisecond may
	// be that issue's own, credits nothing, and leaves the correlation
	// refusing. A probe whose earlier round trips all completed, and the
	// pre-mark-issue-plus-orphan shape, therefore both keep measuring
	// normally.
	[[nodiscard]] RoundTrip roundTripSince(int mark, const QString &key) const;

	// PASS/FAIL on that correlation, logging the tallies, the two times and
	// every row it judged either way. An undecidable pair is a FAIL naming
	// what it counted, never a Note carrying a number a reader would take as
	// a measurement. Returns the verdict it logged.
	RoundTrip checkRoundTripSince(
		int mark,
		const QString &key,
		const QString &what);

	[[nodiscard]] const QString &name() const;

private:
	enum class Role {
		Plain,
		Issue,
		Answer,
	};

	// |text| is the exact string record()/recordIssue()/recordAnswer()
	// logged, so windowDetails() prints byte-identically what the log
	// already carries. |key| and |role| are structural: the correlation
	// reads them, never the text.
	struct Record {
		QString text;
		QString key;
		crl::time at = 0;
		Role role = Role::Plain;
	};

	void push(Role role, const QString &key, const QString &text);

	[[nodiscard]] QString windowDetails(int mark) const;
	[[nodiscard]] QString roundTripDetails(
		int mark,
		const QString &key,
		const RoundTrip &trip,
		const std::vector<QString> &discarded) const;

	QString _name;
	std::vector<Record> _rows;

};

// A walk whose negative result has to be able to discriminate.
//
// An enumeration that structurally cannot reach its subject reports a
// confident zero and measures nothing. Two overlays swept
// Data::Session::enumerateBroadcasts looking for monoforums, which filters
// out megagroups, while ChannelData::monoforum() answers only for
// megagroups: the zero was guaranteed before the run started, and one of
// them shipped it as "N/A, measured".
//
// The cure is a control the same walk must also match. A zero subject count
// means absence only when the walk demonstrably reached a known-present item
// in the same pass, so report() refuses to certify a zero without one.
class DiscriminatingScan final {
public:
	DiscriminatingScan(QString name, QString subjectWhat, QString controlWhat);

	void examined(int count = 1);
	void matchedSubject(const QString &detail = QString());
	void matchedControl(const QString &detail = QString());

	// Logs the tallies, then PASSes when the walk discriminates and FAILs
	// when it does not — naming it as unable to decide rather than letting
	// it report absence. Returns whether the subject count is meaningful.
	bool report();

	[[nodiscard]] int examinedCount() const;
	[[nodiscard]] int subjectCount() const;
	[[nodiscard]] int controlCount() const;

private:
	QString _name;
	QString _subjectWhat;
	QString _controlWhat;
	std::vector<QString> _subjectDetails;
	std::vector<QString> _controlDetails;
	int _examined = 0;
	int _subjects = 0;
	int _controls = 0;

};

} // namespace Test
