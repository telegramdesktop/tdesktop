/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Test {

// Absolute evidence directory: TDESKTOP_TEST_EVIDENCE_DIR when set (the
// workspace test-run helper always sets it), otherwise a test_evidence
// folder in the portable working directory. Created on first use.
[[nodiscard]] QString EvidenceDir();
[[nodiscard]] QString ScreenshotsDir();

// Appends one line to <EvidenceDir()>/test_log.txt and flushes immediately,
// so evidence survives any crash or kill.
void LogRaw(const QString &line);

void Step(const QString &text);
// A PASS and a FAIL line have the same shape: |details| is appended after
// " - " on both verdicts, and an empty |details| produces exactly
// "TEST_RESULT: <verdict>: <text>", with no separator and no empty suffix.
void Pass(const QString &text, const QString &details = QString());
void Fail(const QString &text, const QString &details = QString());

// |details| is an observation - the reading the verdict was made against -
// and it is printed whether the check holds or not, so a green log says what
// each check reached and a passing run can be audited without re-running it.
// It used to be written only on the failing branch, which is what attempt 2
// of 2026/08/26/add-wallet-refresh-readiness-helper was spent on: that
// self-test passed its observations here, printed none of them, and two of
// its acceptance criteria could not be read from its own green log.
// Text that is only true after a failure is therefore conditional at the
// call site - ok ? QString() : u"out of tolerance"_q - which leaves the
// passing line exactly "TEST_RESULT: PASS: <what>".
void Check(bool ok, const QString &what, const QString &details = QString());
void Note(const QString &text);

// PASS/FAIL on |actual| being within |tolerance| of |expected|, logging the
// measured values either way.
void CheckNear(
	int actual,
	int expected,
	int tolerance,
	const QString &what);

void LogGeometry(const QString &name, const QRect &rect);

[[nodiscard]] int FailureCount();

// Writes the TEST_COMPLETE marker the external runner waits for, and
// records when it was written. CompletedAt() is crl::now() at that
// moment and 0 before it.
void Complete();
[[nodiscard]] crl::time CompletedAt();

} // namespace Test
