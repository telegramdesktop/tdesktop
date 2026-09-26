/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QString>

namespace Test {

class Runner;

// Maps the no-break space classes U+00A0 NO-BREAK SPACE and U+202F NARROW
// NO-BREAK SPACE to U+0020 SPACE, and changes nothing else: digits,
// letters, punctuation, every other character and the length of the
// string are preserved, because each mapping is one character for one.
[[nodiscard]] QString NormalizeSpaces(const QString &text);

// PASS when |read| and |expected| are equal after NormalizeSpaces on both
// sides, FAIL otherwise, reported through Test::Check - there is no second
// logging path. Both verdicts print both raw strings and the distinct
// whitespace code points of each, as U+XXXX tokens in order of appearance,
// so a passing line still names the space classes it reconciled. Nothing
// else is loosened: a different minute, digit, month or day period is
// still a FAIL.
void CheckTextReads(
	const QString &read,
	const QString &expected,
	const QString &what);

// A text oracle that compares a rendered string against a formatter-shaped
// one fails on a date, a time or a number that reads identically in the
// details, because the two sides carry different space classes.
// langDateTime() (lang/lang_keys.cpp:190-197) embeds
// QLocale().toString(time, QLocale::ShortFormat), and Qt's CLDR data for
// en_US puts U+202F NARROW NO-BREAK SPACE before the AM/PM day period.
// Ui::FlatLabel::setText hands that string to Ui::Text::String::setText,
// whose BlockParser replaces every space-class character except
// QChar::Nbsp with QChar::Space (text_block_parser.cpp:577-579 and
// :638-640), so Ui::FlatLabel::accessibilityName() - which returns that
// parsed text - reads back U+0020 where the formatter wrote U+202F, and a
// byte-exact comparison fails on two strings a reader cannot tell apart.
// The mechanism is the text parser, not the accessibility path and not the
// locale: U+00A0 is the one class the parser leaves alone, so a label fed
// U+00A0 reads U+00A0 back while one fed U+202F reads U+0020.
//
// AppendTextReadsSelfTest is those helpers measuring themselves. It builds
// two parentless Ui::FlatLabels, one fed U+202F and one fed U+00A0, reads
// both back through accessibilityName() - the same accessor the scenarios
// use - and reports the host's actual read-backs as Test::Note lines
// instead of asserting the mechanism. The reproduced byte-exact inequality
// is one of those notes and never a Check, so it is neither a PASS nor a
// FAIL; on a host whose read-back preserved U+202F the note says so, and
// the string-only pairs still prove the helper. The compared strings are
// the self-test's own literals rather than langDateTime()'s output, so no
// stage depends on the host locale, and the sources stay ASCII because
// every space class is written as QChar(0x202F) / QChar(0x00A0).
//
// It needs no primary window, no session, no chats list, no network, no
// wallet, no fixture secret and no funded value: nothing is shown, painted
// or grabbed, and accessibilityName() returns the parsed text independent
// of layout, so there is no fixture gate for it to pass. It appends its
// own teardown last, which destroys both labels.
//
// It is the harness's one self-test that emits deliberate failures. Two of
// its rows are negative controls - a different minute, and a different
// day-of-month digit sharing its counterpart's space class - and both are
// expected to FAIL, because a check that accepted either would be
// permissive rather than space-class-normalizing. A Test::Note immediately
// before them announces that the next two FAIL rows are those controls, so
// a reader of a shared log is not misled, and a run carrying this
// self-test therefore ends with those two failures counted in its
// SCENARIO_RESULT by design.
void AppendTextReadsSelfTest(not_null<Runner*> runner);

} // namespace Test
