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
class Probe final {
public:
	explicit Probe(QString name);

	// Records one row and logs it as "<name>: <row>".
	void record(const QString &row);

	// The end of the history right now. Take one immediately before the
	// action under test.
	[[nodiscard]] int mark() const;

	[[nodiscard]] std::vector<QString> rowsSince(int mark) const;
	[[nodiscard]] int countSince(int mark) const;
	[[nodiscard]] int countSince(int mark, const QString &part) const;
	[[nodiscard]] bool sawSince(int mark, const QString &part) const;

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

	[[nodiscard]] const QString &name() const;

private:
	[[nodiscard]] QString windowDetails(int mark) const;

	QString _name;
	std::vector<QString> _rows;

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
