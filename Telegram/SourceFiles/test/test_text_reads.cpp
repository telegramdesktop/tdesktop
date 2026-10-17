/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_text_reads.h"

#include "base/unique_qptr.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "ui/widgets/labels.h"

#include "styles/style_widgets.h"

namespace Test {
namespace {

// The distinct whitespace code points of |text| as U+XXXX tokens, in order
// of appearance. QChar::isSpace() is true for the whole Zs category, so the
// tokens cover both classes NormalizeSpaces maps and any other whitespace
// the string happens to carry.
[[nodiscard]] QString SpaceClasses(const QString &text) {
	auto seen = base::flat_set<uint>();
	auto result = QStringList();
	for (const auto ch : text) {
		const auto code = uint(ch.unicode());
		if (ch.isSpace() && seen.emplace(code).second) {
			result.push_back(u"U+%1"_q.arg(
				QString::number(code, 16).toUpper().rightJustified(
					4,
					QChar('0'))));
		}
	}
	return result.isEmpty() ? u"none"_q : result.join(QChar(','));
}

} // namespace

QString NormalizeSpaces(const QString &text) {
	auto result = text;
	result.replace(QChar(0x00A0), QChar(0x0020));
	result.replace(QChar(0x202F), QChar(0x0020));
	return result;
}

void CheckTextReads(
		const QString &read,
		const QString &expected,
		const QString &what) {
	// One multi-argument arg(), never a chain: the compared strings are
	// arbitrary product text and may themselves contain a % sequence, which
	// a chained arg() would take for the next placeholder.
	Check(
		NormalizeSpaces(read) == NormalizeSpaces(expected),
		what,
		u"read=\"%1\" readSpaces=%2 expected=\"%3\" expectedSpaces=%4"_q.arg(
			read,
			SpaceClasses(read),
			expected,
			SpaceClasses(expected)));
}

void AppendTextReadsSelfTest(not_null<Runner*> runner) {
	struct State {
		base::unique_qptr<Ui::FlatLabel> narrow;
		base::unique_qptr<Ui::FlatLabel> nbsp;
		QString readNarrow;
		QString readNbsp;
	};
	// Leaked on purpose, the way the harness's other self-tests leak theirs:
	// the stages outlive this call. The teardown stage destroys both labels,
	// after which the State holds nothing but QStrings.
	const auto state = new State();
	const auto narrowText = u"Sep 2 at 9:21"_q + QChar(0x202F) + u"AM"_q;
	const auto nbspText = u"Sep 2 at 9:21"_q + QChar(0x00A0) + u"AM"_q;
	const auto plainText = u"Sep 2 at 9:21 AM"_q;

	runner->add({
		.name = u"text reads self-test: a label's read-back against the "
			"formatter's string"_q,
		.run = [=] {
			// Parentless and never shown: accessibilityName() returns the
			// parsed text, which the label owns before any layout, paint or
			// grab happens, so this self-test asks the process for no
			// primary window and has no fixture gate to report.
			state->narrow = base::make_unique_q<Ui::FlatLabel>(
				nullptr,
				st::defaultFlatLabel);
			state->nbsp = base::make_unique_q<Ui::FlatLabel>(
				nullptr,
				st::defaultFlatLabel);
			state->narrow->setText(narrowText);
			state->nbsp->setText(nbspText);
			state->readNarrow = state->narrow->accessibilityName();
			state->readNbsp = state->nbsp->accessibilityName();
		},
		.then = [=] {
			Note(u"text reads self-test: byte-exact comparison of the "
				"U+202F label: equal=%1 read=\"%2\" readSpaces=%3 "
				"expected=\"%4\" expectedSpaces=%5 - recorded and not "
				"asserted, so it is neither a PASS nor a FAIL: equal=0 is "
				"the flaw the check below answers, while equal=1 would mean "
				"this host's read-back preserved the class and the "
				"string-only pairs would still prove the helper"_q.arg(
					QString::number(
						(state->readNarrow == narrowText) ? 1 : 0),
					state->readNarrow,
					SpaceClasses(state->readNarrow),
					narrowText,
					SpaceClasses(narrowText)));
			Note(u"text reads self-test: the U+00A0 label reads back "
				"\"%1\" with spaces %2 - the block parser excludes "
				"QChar::Nbsp from the replacement it applies to every "
				"other space class, and this line is that host reading "
				"rather than an assumption"_q.arg(
					state->readNbsp,
					SpaceClasses(state->readNbsp)));
			CheckTextReads(
				state->readNarrow,
				narrowText,
				u"text reads self-test: the label's read-back matches the "
				"formatter-shaped string across the space class"_q);
			CheckTextReads(
				state->readNbsp,
				plainText,
				u"text reads self-test: a U+00A0 read-back matches a "
				"U+0020 expectation"_q);
			const auto normalized = NormalizeSpaces(narrowText);
			Check(
				(normalized == plainText)
					&& (normalized.size() == narrowText.size()),
				u"text reads self-test: NormalizeSpaces maps the space "
				"class and changes nothing else - every digit, letter and "
				"punctuation mark, and the length, are preserved"_q,
				u"input=\"%1\" inputSpaces=%2 output=\"%3\" "
				"outputSpaces=%4 inputLength=%5 outputLength=%6"_q.arg(
					narrowText,
					SpaceClasses(narrowText),
					normalized,
					SpaceClasses(normalized),
					QString::number(narrowText.size()),
					QString::number(normalized.size())));
		},
	});

	runner->add({
		.name = u"text reads self-test: the negative controls"_q,
		.then = [=] {
			Note(u"text reads self-test: the two FAIL rows below are this "
				"self-test's negative controls - the check is expected to "
				"refuse both"_q);
			CheckTextReads(
				plainText,
				u"Sep 2 at 9:22"_q + QChar(0x202F) + u"AM"_q,
				u"text reads self-test negative control: a different "
				"minute is not accepted"_q);
			CheckTextReads(
				plainText,
				u"Sep 3 at 9:21 AM"_q,
				u"text reads self-test negative control: a different digit "
				"sharing the space class is not accepted"_q);
		},
	});

	runner->add({
		.name = u"text reads self-test: teardown"_q,
		.run = [=] {
			// Last on purpose. A timed-out stage or the watchdog skips
			// every stage after it, so anything still held here would
			// outlive the run: both labels are parentless top levels this
			// State alone owns, and releasing the unique_qptrs is what
			// destroys them.
			state->narrow = nullptr;
			state->nbsp = nullptr;
			Note(u"text reads self-test: labels released, alive=%1"_q.arg(
				QString::number((state->narrow ? 1 : 0)
					+ (state->nbsp ? 1 : 0))));
		},
	});
}

} // namespace Test

#endif // _DEBUG
