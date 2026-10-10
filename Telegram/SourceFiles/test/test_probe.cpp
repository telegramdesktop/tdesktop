/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_probe.h"

#include "test/test_log.h"

namespace Test {
namespace {

[[nodiscard]] QString JoinRows(const std::vector<QString> &rows) {
	if (rows.empty()) {
		return u"<none>"_q;
	}
	auto result = QString();
	for (const auto &row : rows) {
		if (!result.isEmpty()) {
			result += u"; "_q;
		}
		result += row;
	}
	return result;
}

[[nodiscard]] QString RoleSuffix(const QString &roleName, const QString &key) {
	return u" role=%1 key=%2"_q.arg(roleName, key);
}

} // namespace

QString RoundTripStateName(RoundTripState state) {
	switch (state) {
	case RoundTripState::Paired:
		return u"paired"_q;
	case RoundTripState::NoIssue:
		return u"no-issue"_q;
	case RoundTripState::AmbiguousIssue:
		return u"ambiguous-issue"_q;
	case RoundTripState::NoAnswer:
		return u"no-answer"_q;
	case RoundTripState::NotLater:
		return u"not-later"_q;
	case RoundTripState::AmbiguousAnswer:
		return u"ambiguous-answer"_q;
	case RoundTripState::OutstandingBeforeMark:
		return u"outstanding-before-mark"_q;
	}
	return u"missing"_q;
}

QString ProbeWindowText(
		const QString &name,
		int from,
		int till,
		const std::vector<QString> &rows,
		NumberFormat format) {
	auto text = u"probe=%1 window=[%2,%3) rows=%4"_q.arg(name);
	text = ArgNumber(text, from, format);
	text = ArgNumber(text, till, format);
	return text.arg(JoinRows(rows));
}

QString ProbeCountText(
		int expected,
		int actual,
		const QString &window,
		NumberFormat format) {
	return u"expected=%1 actual=%2 %3"_q
		.arg(HelperNumber(expected, format))
		.arg(HelperNumber(actual, format))
		.arg(window);
}

QString RoundTripText(
		const QString &key,
		const RoundTrip &trip,
		const std::vector<QString> &discarded,
		const QString &window,
		NumberFormat format) {
	const auto tallies = u"issues=%1 answers=%2 dropped=%3 "
		"preIssues=%4 preAnswers=%5"_q
			.arg(HelperNumber(trip.issues, format))
			.arg(HelperNumber(trip.answers, format))
			.arg(HelperNumber(trip.dropped, format))
			.arg(HelperNumber(trip.preIssues, format))
			.arg(HelperNumber(trip.preAnswers, format));
	return u"key=%1 state=%2 %3 issueAtMs=%4 answerAtMs=%5 roundTripMs=%6 "
		"discarded=%7 %8"_q.arg(
			key,
			RoundTripStateName(trip.state),
			tallies,
			HelperNumber(trip.issueAtMs, format),
			HelperNumber(trip.answerAtMs, format),
			HelperNumber(trip.roundTripMs, format),
			JoinRows(discarded),
			window);
}

Probe::Probe(QString name) : _name(std::move(name)) {
}

void Probe::push(Role role, const QString &key, const QString &text) {
	_rows.push_back({ text, key, crl::now(), role });
	Note(_name + u": "_q + text);
}

void Probe::record(const QString &row) {
	push(Role::Plain, QString(), row);
}

void Probe::recordIssue(const QString &key, const QString &row) {
	push(Role::Issue, key, row + RoleSuffix(u"issue"_q, key));
}

void Probe::recordAnswer(const QString &key, const QString &row) {
	push(Role::Answer, key, row + RoleSuffix(u"answer"_q, key));
}

int Probe::mark() const {
	return int(_rows.size());
}

std::vector<QString> Probe::rowsSince(int mark) const {
	const auto from = std::clamp(mark, 0, int(_rows.size()));
	auto result = std::vector<QString>();
	for (auto i = from; i != int(_rows.size()); ++i) {
		result.push_back(_rows[i].text);
	}
	return result;
}

int Probe::countSince(int mark) const {
	return int(_rows.size()) - std::clamp(mark, 0, int(_rows.size()));
}

int Probe::countSince(int mark, const QString &part) const {
	auto result = 0;
	for (const auto &row : rowsSince(mark)) {
		if (row.contains(part)) {
			++result;
		}
	}
	return result;
}

bool Probe::sawSince(int mark, const QString &part) const {
	return countSince(mark, part) > 0;
}

std::vector<TimedRow> Probe::timedRowsSince(int mark) const {
	return timedRowsSince(mark, QString());
}

std::vector<TimedRow> Probe::timedRowsSince(
		int mark,
		const QString &part) const {
	const auto from = std::clamp(mark, 0, int(_rows.size()));
	auto result = std::vector<TimedRow>();
	for (auto i = from; i != int(_rows.size()); ++i) {
		const auto &entry = _rows[i];
		if (entry.role != Role::Plain) {
			continue;
		} else if (part.isEmpty() || entry.text.contains(part)) {
			result.push_back({ entry.text, entry.at });
		}
	}
	return result;
}

QString Probe::windowDetails(int mark, NumberFormat format) const {
	const auto from = std::clamp(mark, 0, int(_rows.size()));
	return ProbeWindowText(
		_name,
		from,
		int(_rows.size()),
		rowsSince(mark),
		format);
}

void Probe::checkSawSince(
		int mark,
		const QString &part,
		const QString &what) {
	const auto format = HelperNumberFormat();
	Check(sawSince(mark, part), what, windowDetails(mark, format));
}

void Probe::checkNoneSince(
		int mark,
		const QString &part,
		const QString &what) {
	const auto format = HelperNumberFormat();
	Check(!sawSince(mark, part), what, windowDetails(mark, format));
}

void Probe::checkCountSince(
		int mark,
		const QString &part,
		int expected,
		const QString &what) {
	const auto actual = countSince(mark, part);
	const auto format = HelperNumberFormat();
	Check(
		actual == expected,
		what,
		ProbeCountText(expected, actual, windowDetails(mark, format), format));
}

RoundTrip Probe::roundTripSince(int mark, const QString &key) const {
	const auto from = std::clamp(mark, 0, int(_rows.size()));
	auto trip = RoundTrip();
	auto answers = std::vector<TimedRow>();
	auto issueAt = crl::time(0);
	for (auto i = 0; i != from; ++i) {
		const auto &entry = _rows[i];
		if (entry.role == Role::Plain || entry.key != key) {
			continue;
		} else if (entry.role == Role::Issue) {
			++trip.preIssues;
		} else {
			++trip.preAnswers;
		}
	}
	for (auto i = from; i != int(_rows.size()); ++i) {
		const auto &entry = _rows[i];
		if (entry.role == Role::Plain || entry.key != key) {
			continue;
		} else if (entry.role == Role::Issue) {
			if (!trip.issues) {
				issueAt = entry.at;
			}
			++trip.issues;
		} else {
			answers.push_back({ entry.text, entry.at });
		}
	}
	trip.answers = int(answers.size());
	auto pairedIndex = -1;
	if (!trip.issues) {
		trip.state = RoundTripState::NoIssue;
	} else if (trip.issues > 1) {
		trip.state = RoundTripState::AmbiguousIssue;
	} else if (!trip.answers) {
		trip.state = RoundTripState::NoAnswer;
	} else {
		auto later = std::vector<int>();
		auto earlier = 0;
		for (auto i = 0; i != trip.answers; ++i) {
			if (answers[i].at > issueAt) {
				later.push_back(i);
			} else if (answers[i].at < issueAt) {
				++earlier;
			}
		}
		const auto outstanding = trip.preIssues
			- trip.preAnswers
			- earlier;
		if (later.empty()) {
			trip.state = RoundTripState::NotLater;
		} else if (outstanding > 0) {
			trip.state = RoundTripState::OutstandingBeforeMark;
		} else if (later.size() > 1) {
			trip.state = RoundTripState::AmbiguousAnswer;
		} else {
			pairedIndex = later.front();
			trip.state = RoundTripState::Paired;
			trip.issueAtMs = issueAt;
			trip.answerAtMs = answers[pairedIndex].at;
			trip.roundTripMs = trip.answerAtMs - trip.issueAtMs;
		}
	}
	auto discarded = std::vector<QString>();
	for (auto i = 0; i != trip.answers; ++i) {
		if (i != pairedIndex) {
			discarded.push_back(answers[i].row);
		}
	}
	trip.dropped = int(discarded.size());
	const auto format = HelperNumberFormat();
	trip.observation = RoundTripText(
		key,
		trip,
		discarded,
		windowDetails(mark, format),
		format);
	return trip;
}

RoundTrip Probe::checkRoundTripSince(
		int mark,
		const QString &key,
		const QString &what) {
	const auto trip = roundTripSince(mark, key);
	Check(trip.paired(), what, trip.observation);
	return trip;
}

const QString &Probe::name() const {
	return _name;
}

DiscriminatingScan::DiscriminatingScan(
	QString name,
	QString subjectWhat,
	QString controlWhat)
: _name(std::move(name))
, _subjectWhat(std::move(subjectWhat))
, _controlWhat(std::move(controlWhat)) {
}

void DiscriminatingScan::examined(int count) {
	_examined += count;
}

void DiscriminatingScan::matchedSubject(const QString &detail) {
	++_subjects;
	if (!detail.isEmpty()) {
		_subjectDetails.push_back(detail);
	}
}

void DiscriminatingScan::matchedControl(const QString &detail) {
	++_controls;
	if (!detail.isEmpty()) {
		_controlDetails.push_back(detail);
	}
}

bool DiscriminatingScan::report() {
	const auto format = HelperNumberFormat();
	Note(tallyText(format));
	if (!_subjectDetails.empty()) {
		Note(_name + u" subjects: "_q + JoinRows(_subjectDetails));
	}
	if (!_controlDetails.empty()) {
		Note(_name + u" controls: "_q + JoinRows(_controlDetails));
	}
	const auto discriminates = (_controls > 0);
	Check(
		discriminates,
		_name + u" discriminates"_q,
		discriminates ? QString() : refusalText(format));
	return discriminates;
}

QString DiscriminatingScan::tallyText(NumberFormat format) const {
	auto text = u"%1: examined=%2 subject(%3)=%4 control(%5)=%6"_q.arg(_name);
	text = ArgNumber(text, _examined, format).arg(_subjectWhat);
	text = ArgNumber(text, _subjects, format).arg(_controlWhat);
	return ArgNumber(text, _controls, format);
}

QString DiscriminatingScan::refusalText(NumberFormat format) const {
	auto text
		= u"the walk matched no %1, so its %2 count of %3 over %4 examined "
		u"items cannot tell absence from an enumeration that never reaches "
		u"the subject"_q
			.arg(_controlWhat)
			.arg(_subjectWhat);
	text = ArgNumber(text, _subjects, format);
	return ArgNumber(text, _examined, format);
}

int DiscriminatingScan::examinedCount() const {
	return _examined;
}

int DiscriminatingScan::subjectCount() const {
	return _subjects;
}

int DiscriminatingScan::controlCount() const {
	return _controls;
}

} // namespace Test

#endif // _DEBUG
