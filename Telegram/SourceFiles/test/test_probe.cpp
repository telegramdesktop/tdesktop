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

} // namespace

Probe::Probe(QString name) : _name(std::move(name)) {
}

void Probe::record(const QString &row) {
	_rows.push_back(row);
	Note(_name + u": "_q + row);
}

int Probe::mark() const {
	return int(_rows.size());
}

std::vector<QString> Probe::rowsSince(int mark) const {
	const auto from = std::clamp(mark, 0, int(_rows.size()));
	return std::vector<QString>(begin(_rows) + from, end(_rows));
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

QString Probe::windowDetails(int mark) const {
	const auto from = std::clamp(mark, 0, int(_rows.size()));
	return u"probe=%1 window=[%2,%3) rows=%4"_q
		.arg(_name)
		.arg(from)
		.arg(_rows.size())
		.arg(JoinRows(rowsSince(mark)));
}

void Probe::checkSawSince(
		int mark,
		const QString &part,
		const QString &what) {
	Check(sawSince(mark, part), what, windowDetails(mark));
}

void Probe::checkNoneSince(
		int mark,
		const QString &part,
		const QString &what) {
	Check(!sawSince(mark, part), what, windowDetails(mark));
}

void Probe::checkCountSince(
		int mark,
		const QString &part,
		int expected,
		const QString &what) {
	const auto actual = countSince(mark, part);
	Check(
		actual == expected,
		what,
		u"expected=%1 actual=%2 %3"_q
			.arg(expected)
			.arg(actual)
			.arg(windowDetails(mark)));
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
	Note(u"%1: examined=%2 subject(%3)=%4 control(%5)=%6"_q
		.arg(_name)
		.arg(_examined)
		.arg(_subjectWhat)
		.arg(_subjects)
		.arg(_controlWhat)
		.arg(_controls));
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
		u"the walk matched no %1, so its %2 count of %3 over %4 examined "
		u"items cannot tell absence from an enumeration that never reaches "
		u"the subject"_q
			.arg(_controlWhat)
			.arg(_subjectWhat)
			.arg(_subjects)
			.arg(_examined));
	return discriminates;
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
