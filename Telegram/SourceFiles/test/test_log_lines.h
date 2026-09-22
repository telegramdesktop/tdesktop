/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

namespace Test {

class Runner;

// Test::LogRaw is the single writer of <EvidenceDir()>/test_log.txt, and
// every marker the external runner reads is a line grammar, so the writer
// carries an implicit contract - one call is one physical line - that it used
// not to enforce. Both readers of that file split it with Python
// str.splitlines(), which breaks on eleven forms - U+000A, U+000D, the CRLF
// pair, U+000B, U+000C, U+001C, U+001D, U+001E, U+0085, U+2028 and U+2029 -
// and every one of them survives QString::toUtf8() and reaches the file. A
// record handed product text carrying one of them therefore used to become
// several records, which harms a reader twice. parse_test_log
// (.agents/skills/process-inbox/scripts/workspace.py:2279) keeps only lines
// beginning with a known marker, so a multi-line FAIL detail silently lost
// everything after its first line - exactly the part a reader needs. And
// log_marks_complete (workspace.py:2305) accepts any line that right-trims to
// the completion marker, so a middle line spelling it made test-run start its
// grace clock, kill the live application and report a completed run that
// never reached its end. Ui::FlatLabel::accessibilityName() really does read
// a U+000A back out of a multi-line label, and two harness sites already push
// such a read-back into a Check detail, so the hole was latent, not
// imaginary. LogRaw now writes each of those characters as a visible \uXXXX
// escape and still passes separator-free text through byte for byte.
//
// AppendLogLinesSelfTest is that guarantee measuring itself. Every reading it
// takes reads the log file's byte size, makes one LogRaw-family call, and
// reads back exactly the bytes that call appended - by byte offset, in the
// same |then| turn, on the main thread, with nothing else logging in between;
// LogRaw opens, writes, flushes and closes on every call, so the delta is
// exact. It reads them with no QIODevice::Text, so no read-side newline
// translation can hide a stray CR, and splits them with a transcription of
// str.splitlines() written in this module rather than shared with the writer:
// an oracle assembled from the writer's own table would agree with a wrong
// table about a wrong answer, while bytes read back out of the file cannot.
// One Check per reading, and its verdict is the conjunction of four - the
// call appended exactly one physical line, that line equals the marker prefix
// plus the independently escaped payload, no produced line right-trims to the
// completion marker, and the line carries no trailing whitespace.
//
// Three stages. The first writes a separator-free control through LogRaw
// itself and then one Note per separator form, so the guarantee is shown at
// the choke point and shown to be inherited by a family writer; the second
// writes one payload carrying every form, one ending in a separator and one
// that is nothing but separators; the third writes a payload whose middle
// line would be byte-equal to the completion marker. Reverting the escape in
// LogRaw turns every one of these Checks FAIL in the same run, with concrete
// readings: the first stage's U+000A row reads physicalLines=3 with
// line1="NOTE: before", the third reads physicalLines=3
// forgedCompletionLines=1, and the second stage's mixed row reads
// physicalLines=13 on Windows - eleven breaks in the payload, plus the one
// Qt's text-mode write adds by expanding the payload's own LF that follows
// a CR, plus the trailing empty line the record's own terminator closes
// because that payload ends in a separator; where QIODevice::Text does not
// translate, the same row reads 12. The separator-free
// control is what keeps the sweep from passing for an unrelated reason: it
// must read physicalLines=1 under both writers, so a failing per-form row can
// never be blamed on the measurement instead of on the writer.
//
// It asks nothing of the process: no session, no chats list, no network, no
// wallet, no fixture secret, no widget and no primary window. Nothing it
// measures is asynchronous, so each reading begins and ends inside one
// |then|, the module holds no state and there is nothing to tear down. It
// emits no deliberate failure: on a healthy harness its run carries only
// TEST_RESULT: PASS rows, and its before-leg is produced by reverting LogRaw
// and re-running the identical scenario, never by a stage that fails on
// purpose.
//
// The last stage's payload spells the completion marker on purpose. Under a
// healthy writer that payload is escaped and inert, but under the reverted
// writer it is the forgery itself, so it is registered last among these
// stages, and an overlay's own measurements belong before the call that
// appends them - anything registered after it can be killed once the grace
// clock the forgery started runs out.
void AppendLogLinesSelfTest(not_null<Runner*> runner);

} // namespace Test
