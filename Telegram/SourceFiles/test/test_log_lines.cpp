/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_log_lines.h"

#include "test/test_log.h"
#include "test/test_runner.h"

namespace Test {
namespace {

// The ten code points Python's str.splitlines() breaks on, written out here
// again rather than shared with test_log.cpp's own table. The duplication is
// the point of this module: an oracle assembled from the writer's table would
// agree with a wrong table about a wrong answer, while bytes read back out of
// the file cannot. CR and LF appear individually, so the CRLF pair needs pair
// state nowhere but in SplitPhysicalLines, where the readers' own grammar
// really does count it as a single break.
constexpr auto kSeparators = std::array<ushort, 10>{
	0x000A, 0x000B, 0x000C, 0x000D,
	0x001C, 0x001D, 0x001E,
	0x0085, 0x2028, 0x2029,
};

[[nodiscard]] bool IsSeparator(QChar ch) {
	return ranges::contains(kSeparators, ch.unicode());
}

// The \uXXXX form this module expects to read back, transcribed independently
// of the writer's escaper for the same reason kSeparators is. It also renders
// this module's own diagnostic strings, so a details row stays one physical
// line even under an unmodified writer; applying it to text it produced
// itself changes nothing, because its output carries no separator.
[[nodiscard]] QString Escaped(const QString &text) {
	auto result = QString();
	result.reserve(text.size());
	for (const auto ch : text) {
		if (IsSeparator(ch)) {
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

// Python str.splitlines() as both readers of test_log.txt apply it, and
// therefore the whole oracle. Two rules decide every reading taken here. A CR
// immediately followed by an LF is ONE break, not two, so the CRLF the writer
// leaves on Windows never manufactures an empty line of its own. And text
// ending in a separator yields no trailing empty element, so the terminator
// LogRaw appends is never counted as a line. Empty input yields an empty
// list.
[[nodiscard]] QStringList SplitPhysicalLines(const QString &text) {
	auto result = QStringList();
	auto line = QString();
	const auto count = int(text.size());
	for (auto i = 0; i != count; ++i) {
		const auto ch = text[i];
		if (!IsSeparator(ch)) {
			line.append(ch);
			continue;
		}
		if ((ch == QChar(0x000D))
			&& (i + 1 != count)
			&& (text[i + 1] == QChar(0x000A))) {
			++i;
		}
		result.push_back(base::take(line));
	}
	if (!line.isEmpty()) {
		result.push_back(line);
	}
	return result;
}

[[nodiscard]] QString RightTrimmed(const QString &text) {
	auto size = int(text.size());
	while (size > 0 && text[size - 1].isSpace()) {
		--size;
	}
	return text.left(size);
}

[[nodiscard]] QString LogPath() {
	return EvidenceDir() + u"test_log.txt"_q;
}

// QIODevice::ReadOnly without QIODevice::Text on purpose: the writer opens
// the file in text mode, so on Windows every terminator it produced is a
// CRLF, and a read that translated newlines back would hide a stray CR the
// escape is supposed to have removed. Reading the raw bytes and decoding them
// with QString::fromUtf8 leaves whatever really reached the file visible to
// SplitPhysicalLines.
[[nodiscard]] QString AppendedSince(qint64 from) {
	auto file = QFile(LogPath());
	if (!file.open(QIODevice::ReadOnly)) {
		return QString();
	}
	file.seek(from);
	return QString::fromUtf8(file.readAll());
}

void CheckWritesOneLine(
		const QString &what,
		const QString &prefix,
		const QString &payload,
		Fn<void(const QString &)> write) {
	const auto mark = QFileInfo(LogPath()).size();
	write(payload);
	const auto appendedBytes = QFileInfo(LogPath()).size() - mark;
	const auto lines = SplitPhysicalLines(AppendedSince(mark));
	const auto first = lines.isEmpty() ? QString() : lines.front();
	const auto expected = prefix + Escaped(payload);
	const auto isCompletion = [](const QString &line) {
		return RightTrimmed(line) == u"TEST_COMPLETE"_q;
	};
	const auto forged = int(ranges::count_if(lines, isCompletion));
	const auto rightTrimmedEqualsLine = (RightTrimmed(first) == first);
	// One multi-argument arg(), never a chain: the strings formatted here are
	// this module's own crafted payloads and the bytes read back beside them,
	// and either may carry a % sequence a chained arg() would take for the
	// next placeholder.
	Check(
		(lines.size() == 1)
			&& (first == expected)
			&& !forged
			&& rightTrimmedEqualsLine,
		what,
		u"appendedBytes=%1 physicalLines=%2 forgedCompletionLines=%3 "
		"rightTrimmedEqualsLine=%4 line1=\"%5\" expected=\"%6\""_q.arg(
			QString::number(appendedBytes),
			QString::number(lines.size()),
			QString::number(forged),
			QString::number(rightTrimmedEqualsLine ? 1 : 0),
			Escaped(first),
			Escaped(expected)));
}

} // namespace

void AppendLogLinesSelfTest(not_null<Runner*> runner) {
	struct Form {
		QString name;
		QString separator;
	};
	const auto forms = std::array<Form, 11>{ {
		{ u"U+000A LINE FEED"_q, QString(QChar(0x000A)) },
		{ u"U+000D CARRIAGE RETURN"_q, QString(QChar(0x000D)) },
		{ u"the CRLF pair"_q, QString(QChar(0x000D)) + QChar(0x000A) },
		{ u"U+000B LINE TABULATION"_q, QString(QChar(0x000B)) },
		{ u"U+000C FORM FEED"_q, QString(QChar(0x000C)) },
		{ u"U+001C FILE SEPARATOR"_q, QString(QChar(0x001C)) },
		{ u"U+001D GROUP SEPARATOR"_q, QString(QChar(0x001D)) },
		{ u"U+001E RECORD SEPARATOR"_q, QString(QChar(0x001E)) },
		{ u"U+0085 NEXT LINE"_q, QString(QChar(0x0085)) },
		{ u"U+2028 LINE SEPARATOR"_q, QString(QChar(0x2028)) },
		{ u"U+2029 PARAGRAPH SEPARATOR"_q, QString(QChar(0x2029)) },
	} };

	runner->add({
		.name = u"log lines self-test: a separator-free control and the "
			"eleven separator forms"_q,
		.then = [=] {
			CheckWritesOneLine(
				u"log lines self-test: a separator-free payload reaches the "
				"file unchanged as one physical line"_q,
				QString(),
				u"LOG_LINES_CONTROL: a payload carrying no separator"_q,
				LogRaw);
			for (const auto &form : forms) {
				CheckWritesOneLine(
					u"log lines self-test: a Note carrying %1 writes one "
					"physical line"_q.arg(form.name),
					u"NOTE: "_q,
					u"before"_q
						+ form.separator
						+ u"middle"_q
						+ form.separator
						+ u"after"_q,
					Note);
			}
		},
	});

	runner->add({
		.name = u"log lines self-test: a mixed payload, a trailing separator "
			"and separators only"_q,
		.then = [=] {
			auto mixed = QString();
			auto separatorsOnly = QString();
			for (auto i = 0; i != int(forms.size()); ++i) {
				if (i) {
					mixed += u"part%1"_q.arg(i);
				}
				mixed += forms[i].separator;
				separatorsOnly += forms[i].separator;
			}
			CheckWritesOneLine(
				u"log lines self-test: one payload carrying all eleven "
				"separator forms, leading and trailing, writes one physical "
				"line"_q,
				u"NOTE: "_q,
				mixed,
				Note);
			CheckWritesOneLine(
				u"log lines self-test: a payload ending in a separator writes "
				"one physical line and no blank continuation"_q,
				u"NOTE: "_q,
				u"a payload that ends in a line feed"_q + QChar(0x000A),
				Note);
			CheckWritesOneLine(
				u"log lines self-test: a payload that is nothing but the "
				"eleven separator forms writes one physical line"_q,
				u"NOTE: "_q,
				separatorsOnly,
				Note);
		},
	});

	runner->add({
		.name = u"log lines self-test: a middle line that would be the "
			"completion marker"_q,
		.then = [] {
			CheckWritesOneLine(
				u"log lines self-test: a payload whose middle line spells the "
				"completion marker writes one physical line that no reader "
				"can mistake for a completion"_q,
				u"NOTE: "_q,
				u"before"_q
					+ QChar(0x000A)
					+ u"TEST_COMPLETE"_q
					+ QChar(0x000A)
					+ u"after"_q,
				Note);
		},
	});
}

} // namespace Test

#endif // _DEBUG
