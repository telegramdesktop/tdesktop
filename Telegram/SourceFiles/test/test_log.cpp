/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_log.h"

#include "settings.h"

namespace Test {
namespace {

auto FailuresCount = 0;
auto SkippedCountValue = 0;
auto CompletedAtValue = crl::time(0);

[[nodiscard]] QString EnsuredDir(const QString &path) {
	QDir().mkpath(path);
	return path.endsWith('/') ? path : (path + '/');
}

// Exactly the characters Python's str.splitlines() breaks on, which is the
// line grammar both readers of test_log.txt use - parse_test_log and
// log_marks_complete in the external runner. CR and LF are listed
// individually, so a CRLF pair needs no pair state and no lookahead: it is
// simply its two code points, each matched on its own. All ten are single
// UTF-16 code units in the BMP, so matching per character is total and a
// surrogate half can never be mistaken for one of them.
constexpr auto kLineSeparators = std::array<ushort, 10>{
	0x000A, 0x000B, 0x000C, 0x000D,
	0x001C, 0x001D, 0x001E,
	0x0085, 0x2028, 0x2029,
};

[[nodiscard]] bool IsLineSeparator(QChar ch) {
	return ranges::contains(kLineSeparators, ch.unicode());
}

// Makes one write one physical line: each separator is replaced by a visible
// \uXXXX token naming its code point, so a record whose text carried a break
// stays one row and stays readable past the break. Every token ends in a hex
// digit, never in whitespace, so a reader's line.rstrip() == "TEST_COMPLETE"
// and Qt's Windows QIODevice::Text translation - which scans for '\n' alone -
// are both undisturbed. The escape character is deliberately not doubled, so
// separator-free text comes back unchanged, byte for byte, and an author's
// own backslash is never rewritten.
[[nodiscard]] QString OneLine(const QString &line) {
	if (ranges::none_of(line, IsLineSeparator)) {
		return line;
	}
	auto result = QString();
	result.reserve(line.size() + 8);
	for (const auto ch : line) {
		if (IsLineSeparator(ch)) {
			result.append(u"\\u%1"_q.arg(
				QString::number(uint(ch.unicode()), 16)
					.toUpper()
					.rightJustified(4, QChar('0'))));
		} else {
			result.append(ch);
		}
	}
	return result;
}

} // namespace

QString EvidenceDir() {
	static const auto result = [] {
		const auto value = qEnvironmentVariable("TDESKTOP_TEST_EVIDENCE_DIR");
		const auto path = value.isEmpty()
			? (cWorkingDir() + u"test_evidence"_q)
			: value;
		return EnsuredDir(QFileInfo(path).absoluteFilePath());
	}();
	return result;
}

QString ScreenshotsDir() {
	static const auto result = EnsuredDir(EvidenceDir() + u"screenshots"_q);
	return result;
}

void LogRaw(const QString &line) {
	auto file = QFile(EvidenceDir() + u"test_log.txt"_q);
	if (!file.open(QIODevice::Append | QIODevice::Text)) {
		return;
	}
	file.write((OneLine(line) + u"\n"_q).toUtf8());
	file.flush();
}

void Step(const QString &text) {
	LogRaw(u"TEST_STEP: %1"_q.arg(text));
}

void Pass(const QString &text, const QString &details) {
	LogRaw(details.isEmpty()
		? u"TEST_RESULT: PASS: %1"_q.arg(text)
		: u"TEST_RESULT: PASS: %1 - %2"_q.arg(text, details));
}

void Fail(const QString &text, const QString &details) {
	++FailuresCount;
	LogRaw(details.isEmpty()
		? u"TEST_RESULT: FAIL: %1"_q.arg(text)
		: u"TEST_RESULT: FAIL: %1 - %2"_q.arg(text, details));
}

void Skipped(const QString &text, const QString &details) {
	++SkippedCountValue;
	LogRaw(details.isEmpty()
		? u"TEST_RESULT: N/A: %1"_q.arg(text)
		: u"TEST_RESULT: N/A: %1 - %2"_q.arg(text, details));
}

void Check(bool ok, const QString &what, const QString &details) {
	if (ok) {
		Pass(what, details);
	} else {
		Fail(what, details);
	}
}

void Note(const QString &text) {
	LogRaw(u"NOTE: %1"_q.arg(text));
}

void CheckNear(
		int actual,
		int expected,
		int tolerance,
		const QString &what) {
	const auto ok = (std::abs(actual - expected) <= tolerance);
	Check(
		ok,
		u"%1 (actual %2, expected %3 ±%4)"_q.arg(
			what,
			QString::number(actual),
			QString::number(expected),
			QString::number(tolerance)),
		ok ? QString() : u"out of tolerance"_q);
}

void LogGeometry(const QString &name, const QRect &rect) {
	LogRaw(u"GEOMETRY: %1: x=%2 y=%3 w=%4 h=%5"_q.arg(
		name,
		QString::number(rect.x()),
		QString::number(rect.y()),
		QString::number(rect.width()),
		QString::number(rect.height())));
}

int FailureCount() {
	return FailuresCount;
}

int SkippedCount() {
	return SkippedCountValue;
}

void Complete() {
	CompletedAtValue = crl::now();
	LogRaw(u"TEST_COMPLETE"_q);
}

crl::time CompletedAt() {
	return CompletedAtValue;
}

} // namespace Test

#endif // _DEBUG
