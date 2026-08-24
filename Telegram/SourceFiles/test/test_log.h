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
void Pass(const QString &text);
void Fail(const QString &text, const QString &details = QString());
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
