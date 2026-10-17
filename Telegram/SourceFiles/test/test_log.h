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
// so evidence survives any crash or kill. Exactly one physical line per
// call, whatever |line| carries: every character Python's str.splitlines()
// breaks on - which is the grammar the external runner's readers use -
// is written as a visible \uXXXX escape, so a record whose text had a
// break stays one row rather than several, and no middle line can be
// byte-equal to the completion marker. Text with no separator is written
// byte for byte, and the escape adds no trailing whitespace.
void LogRaw(const QString &line);

void Step(const QString &text);
// A PASS and a FAIL line have the same shape: |details| is appended after
// " - " on both verdicts, and an empty |details| produces exactly
// "TEST_RESULT: <verdict>: <text>", with no separator and no empty suffix.
void Pass(const QString &text, const QString &details = QString());
void Fail(const QString &text, const QString &details = QString());

// A stage or check that did not apply: its gate read false, so nothing was
// measured. Same grammar as Pass and Fail - "TEST_RESULT: N/A: <what>" with
// " - <details>" appended when details exist - counted separately from both
// and never a failure, because a scenario's verdict stays decided by its
// failure count alone. |details| carries the gate's reason, so a reader can
// tell a deliberate skip from a missing measurement.
void Skipped(const QString &what, const QString &details = QString());

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

// The number format for telemetry an overlay prints into a scanned log,
// from
// 2026/10/08/give-overlay-telemetry-a-number-format-the-secrecy-scan-cannot-count.
// An ordinary decimal leaves each digit run between "=", "-", "." or a
// blank, so the run is bounded and can equal a short secret by
// coincidence. It then decides the scan at a "<file>|<head>|-" plain-line
// site (or at the field's name when the number is a field's whole value),
// which no computed-field declaration may exempt. This prints "-" for a
// value below zero, the integer digits, "p" and exactly |decimals|
// fraction digits (a negative |decimals| counts as 0), rounding as
// QString::number(.., 'f', ..) does: 42 -> "42p", 16.31 with 2 ->
// "16p31", -4821.75 with 2 -> "-4821p75". NaN and the infinities print
// "nan", "inf" and "-inf". Every digit run touches the "p", and an
// occurrence counts only with no letter or digit right before and after
// it, so no digit run of a formatted number is ever counted. A judge
// script reads it as float(v.replace("p", ".")).
//
// Format only numbers an overlay, or a converted helper under the request,
// measured or computed, never a fixture secret or a number read from one:
// the format hides digits from the scan, so it would hide that leak too.
// Choose |decimals| per quantity, never from a secret. A TelemetryNumber is
// a word to the phrase matcher: it ends a word run where a bare number is
// neutral. So never print one as a list index or between words copied from
// product or fixture text, and keep such text out of telemetry rows.
[[nodiscard]] QString TelemetryNumber(double value, int decimals = 0);

// How a converted shared helper prints the numbers it measures, counts or
// times. Ordinary is today's decimal ("4821", "-12"); Telemetry is
// TelemetryNumber(value) with |decimals| 0 ("4821p", "-12p"). A converted
// helper formats each row through a pure formatter taking a NumberFormat,
// and its logging path passes HelperNumberFormat().
enum class NumberFormat {
	Ordinary,
	Telemetry,
};

// One explicit, process-wide, one-way ask that the converted shared
// helpers print the numbers they themselves measure, count or time as
// TelemetryNumbers. It takes no argument and reads nothing, so it cannot
// depend on a secret. Call it unconditionally at the campaign's first
// stage, before any row it may scan, never under a condition and never
// from code that read a fixture secret. The first call logs exactly
// "TELEMETRY_NUMBERS: requested" (a judge splits test_log.txt at that
// line); a later call does nothing and logs nothing. Every converted row
// formatted after it prints TelemetryNumbers, one formatted before it keeps
// ordinary integers, and there is no undo: a text a helper formatted before
// the ask and a caller prints after it, such as a kept
// RoundTrip::observation or ClockedRun::refusal, stays ordinary.
//
// The converted rows: GEOMETRY: (LogGeometry); the CheckNear verdict; the
// PostPaintSampler kind=post sample and kind=stop rows; the
// RunClockedFrames kind=frame row, its "frame <k> at <ms> ms" subject, its
// gate and refusal reasons and its request refusal; the Probe window
// bounds, checkCountSince's counts and the round-trip tallies and times;
// the DiscriminatingScan examined= tally row and its refusal (the secrecy
// scan's "SECRECY: <class>" reports among them).
// Caller text and detail lists, PostPaintSampler::details(), the sampler's
// empty-window note, the scan's SECRECY_* rows and the runner's rows keep
// ordinary integers. Grammars in both formats, the caller-number rule and
// the failure route: Telegram/SourceFiles/test/README.md, the test_log.h
// (TelemetryNumber, RequestTelemetryNumbers) catalog row and "Proving no
// fixture secret reached the logs".
void RequestTelemetryNumbers();

// Ordinary until RequestTelemetryNumbers() ran, Telemetry after.
[[nodiscard]] NumberFormat HelperNumberFormat();

// QString::number(value) under Ordinary, TelemetryNumber(double(value))
// under Telemetry.
[[nodiscard]] QString HelperNumber(qint64 value, NumberFormat format);

// text.arg(value) - the integer overload, exactly as before - under
// Ordinary, and text.arg(TelemetryNumber(double(value))) under Telemetry.
// For an .arg chain that already substituted caller text: the two arg
// overloads differ on a %L<n> marker that text may carry (the integer one
// localizes it, the string one does not), so only this keeps such a chain
// byte-identical without the request.
[[nodiscard]] QString ArgNumber(
	const QString &text,
	qint64 value,
	NumberFormat format);

// CheckNear's check text, "<what> (actual <a>, expected <e> ±<t>)", with
// its numbers in |format|. Pure, so a self-test reads both formats.
[[nodiscard]] QString CheckNearText(
	int actual,
	int expected,
	int tolerance,
	const QString &what,
	NumberFormat format);

// PASS/FAIL on |actual| being within |tolerance| of |expected|, logging the
// measured values either way:
// "TEST_RESULT: PASS: <what> (actual <a>, expected <e> ±<t>)", and on FAIL
// the same with " - out of tolerance" appended. <a>, <e> and <t> print as
// ordinary integers ("actual 4826, expected 4821 ±37"), or as
// TelemetryNumbers after RequestTelemetryNumbers() ("actual 4826p, expected
// 4821p ±37p"). So under the request the caller's |actual|, |expected| and
// |tolerance| print in a format that hides digits from the secrecy scan:
// they must never be a fixture secret or a number read from one.
void CheckNear(
	int actual,
	int expected,
	int tolerance,
	const QString &what);

// LogGeometry's whole row, "GEOMETRY: <name>: x=<x> y=<y> w=<w> h=<h>",
// with its numbers in |format|. Pure, so a self-test reads both formats.
[[nodiscard]] QString GeometryText(
	const QString &name,
	const QRect &rect,
	NumberFormat format);

// GEOMETRY: <name>: x=<x> y=<y> w=<w> h=<h>, Ordinary or Telemetry numbers.
void LogGeometry(const QString &name, const QRect &rect);

[[nodiscard]] int FailureCount();
[[nodiscard]] int SkippedCount();

// Writes the TEST_COMPLETE marker the external runner waits for, and
// records when it was written. CompletedAt() is crl::now() at that
// moment and 0 before it.
void Complete();
[[nodiscard]] crl::time CompletedAt();

} // namespace Test
