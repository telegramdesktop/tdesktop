/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_secrecy_scan.h"

#include "base/random.h"
#include "test/test_agent.h"
#include "test/test_log.h"
#include "test/test_probe.h"
#include "test/test_runner.h"
#include "logs.h"
#include "settings.h"

#include <QtCore/QTemporaryDir>

namespace Test {
namespace {

constexpr auto kMinTokenLength = 16;
constexpr auto kPartMinutes = 15;
constexpr auto kMaxWindowSteps = 200;
constexpr auto kControlDetails = 3;
constexpr auto kMaxTypeTag = 24;
constexpr auto kWordRunCanaries = 7; // per phrase, see ReadCanaries
constexpr auto kMaxHeadLength = 64;
constexpr auto kMainLogStart = 22; // "[yyyy.MM.dd hh:mm:ss] "

const auto kBanner = u"NEW LOGGING INSTANCE STARTED!!!"_q;
const auto kSiteWithheld = u"?|?"_q;
const auto kSiteNoField = u"-"_q;

// Where a line sits: outside any transport dump, or inside a dump entry the
// client sent or received (session_private.cpp logs "Send: " + DumpToText
// of what it writes and "Recv: " + DumpToText of what it read).
enum class DumpDirection {
	None,
	Send,
	Recv,
};

[[nodiscard]] QString B(bool value) {
	return value ? u"1"_q : u"0"_q;
}

[[nodiscard]] QString NormalizedWord(const QString &word) {
	return word.trimmed().toLower();
}

// DumpToText writes a string as "\"...\" [STRING]" with backslash, quote
// and newline escaped (mtproto_dump_to_text.cpp), so inside a dump the
// secret can only appear in this form.
[[nodiscard]] QString DumpEscaped(QString text) {
	return text
		.replace(QChar('\\'), u"\\\\"_q)
		.replace(QChar('"'), u"\\\""_q)
		.replace(QChar('\n'), u"\\n"_q);
}

[[nodiscard]] bool IsHexDigit(QChar ch) {
	const auto code = ch.unicode();
	return (code >= '0' && code <= '9')
		|| (code >= 'a' && code <= 'f')
		|| (code >= 'A' && code <= 'F');
}

// DumpToText's type tags: " [STRING]", " [12 BYTES]", " [LONG]" and the
// like (mtproto_dump_to_text.cpp). Answers the offset of the closing
// bracket of a tag opening at |open|, or -1.
[[nodiscard]] int TypeTagEnd(const QString &text, int open) {
	static const auto tags = QSet<QString>{
		u"STRING"_q,
		u"BYTES"_q,
		u"INT"_q,
		u"LONG"_q,
		u"DOUBLE"_q,
		u"INT128"_q,
		u"INT256"_q,
		u"GZIPPED"_q,
	};
	const auto size = int(text.size());
	auto close = open + 1;
	while (close < size
		&& (close - open) <= kMaxTypeTag
		&& text[close] != QChar(']')) {
		++close;
	}
	if (close >= size || text[close] != QChar(']')) {
		return -1;
	}
	auto from = open + 1;
	while (from < close && text[from].isDigit()) {
		++from;
	}
	if (from > open + 1) {
		if (from >= close || text[from] != QChar(' ')) {
			return -1;
		}
		++from;
	}
	return tags.contains(text.mid(from, close - from)) ? close : -1;
}

// What joins two words without a space, or sits between them, is blanked
// to as many spaces, so every offset into the line stays valid: the
// DumpToText string escapes (a backslash before n, t, r, a backslash or a
// quote), the test log's \uXXXX line-break escapes (test_log.cpp OneLine)
// and the type tags between a vector's one-per-line elements.
[[nodiscard]] QString WordSeparated(const QString &text) {
	auto result = text;
	const auto size = int(text.size());
	const auto blank = [&](int from, int count) {
		for (auto i = from; i != from + count; ++i) {
			result[i] = QChar(' ');
		}
	};
	for (auto i = 0; i < size; ++i) {
		const auto ch = text[i];
		if (ch == QChar('\\') && (i + 1 < size)) {
			const auto next = text[i + 1].unicode();
			if (next == 'u'
				&& (i + 5 < size)
				&& IsHexDigit(text[i + 2])
				&& IsHexDigit(text[i + 3])
				&& IsHexDigit(text[i + 4])
				&& IsHexDigit(text[i + 5])) {
				blank(i, 6);
				i += 5;
			} else if (next == 'n'
				|| next == 't'
				|| next == 'r'
				|| next == '\\'
				|| next == '"') {
				blank(i, 2);
				++i;
			}
		} else if (ch == QChar('[')) {
			const auto close = TypeTagEnd(text, i);
			if (close > i) {
				blank(i, close - i + 1);
				i = close;
			}
		}
	}
	return result;
}

[[nodiscard]] bool AllDigits(const QString &text, int from, int till) {
	for (auto i = from; i != till; ++i) {
		if (!text[i].isDigit()) {
			return false;
		}
	}
	return true;
}

// A log entry starts with "[hh:mm:ss.zzz tid-index] " (logs.cpp
// _logsEntryStart).
[[nodiscard]] bool IsEntryHeader(const QString &line) {
	if (line.size() < 15 || line[0] != QChar('[') || line[13] != QChar(' ')) {
		return false;
	}
	for (const auto index : { 1, 2, 4, 5, 7, 8, 10, 11, 12 }) {
		if (!line[index].isDigit()) {
			return false;
		}
	}
	return (line[3] == QChar(':'))
		&& (line[6] == QChar(':'))
		&& (line[9] == QChar('.'));
}

// An mtp entry is "<entry start> (dc:<dc>) <text>" (logs.cpp writeMtp); a
// dump entry's text starts with exactly "Send: " or "Recv: "
// (session_private.cpp). |dumpFrom| is where the dump text itself starts.
[[nodiscard]] DumpDirection HeaderDirection(
		const QString &line,
		int *dumpFrom) {
	const auto dc = int(line.indexOf(u" (dc:"_q));
	if (dc < 0) {
		return DumpDirection::None;
	}
	const auto close = int(line.indexOf(u") "_q, dc));
	if (close < 0) {
		return DumpDirection::None;
	}
	const auto text = line.mid(close + 2, 6);
	const auto result = (text == u"Send: "_q)
		? DumpDirection::Send
		: (text == u"Recv: "_q)
		? DumpDirection::Recv
		: DumpDirection::None;
	if (result != DumpDirection::None) {
		*dumpFrom = close + 8;
	}
	return result;
}

[[nodiscard]] bool IdentifierChar(QChar ch) {
	return ch.isLetterOrNumber() || (ch == QChar('_'));
}

// A public schema identifier as DumpToText prints it ("{ name", "name: ");
// anything else is never printed.
[[nodiscard]] QString SafeIdentifier(const QString &name) {
	if (name.isEmpty() || name.size() > 64 || !name[0].isLetter()) {
		return u"?"_q;
	}
	for (const auto ch : name) {
		if ((ch.unicode() > 0x7F) || !IdentifierChar(ch)) {
			return u"?"_q;
		}
	}
	return name;
}

// Walks DumpToText's own syntax (mtproto_dump_to_text.cpp and the generated
// scheme-dump_to_text.cpp): an object opens as "{ name" and closes with
// "}", a field is "  name: value", a string is "\"...\" [STRING]" with "\\"
// and "\"" escaped and never spans lines. Over text[from, till) it keeps the
// open-object stack, the quote state at |till| and the last field name.
struct DumpWalk {
	bool quoted = false;
	QString field;
};

DumpWalk WalkDump(
		const QString &text,
		int from,
		int till,
		std::vector<QString> &stack,
		bool wantField = false) {
	auto result = DumpWalk();
	for (auto i = from; i < till; ++i) {
		const auto ch = text[i];
		if (result.quoted) {
			if (ch == QChar('\\')) {
				++i;
			} else if (ch == QChar('"')) {
				result.quoted = false;
			}
			continue;
		}
		if (ch == QChar('"')) {
			result.quoted = true;
		} else if (ch == QChar('{')) {
			auto start = i + 1;
			if (start < till && text[start] == QChar(' ')) {
				++start;
			}
			auto end = start;
			while (end < till && IdentifierChar(text[end])) {
				++end;
			}
			stack.push_back(text.mid(start, end - start));
			result.field.clear();
			i = end - 1;
		} else if (ch == QChar('}')) {
			if (!stack.empty()) {
				stack.pop_back();
			}
			result.field.clear();
		} else if (wantField
			&& (ch == QChar(':'))
			&& (i + 1 < till)
			&& (text[i + 1] == QChar(' '))) {
			auto start = i;
			while (start > from && IdentifierChar(text[start - 1])) {
				--start;
			}
			result.field = text.mid(start, i - start);
		}
	}
	return result;
}

// Outside dumps: inside a double-quoted segment when an odd number of
// unescaped quotes precede the position.
[[nodiscard]] bool InsideQuotes(const QString &text, int at) {
	auto inside = false;
	for (auto i = 0; i < at; ++i) {
		if (inside && text[i] == QChar('\\')) {
			++i;
		} else if (text[i] == QChar('"')) {
			inside = !inside;
		}
	}
	return inside;
}

// "<top>/<inner>.<field>": the first object of the chain that is not
// transport framing, the innermost open object and the field the hit
// belongs to on its line ("-" when the line prints no "name: ").
[[nodiscard]] QString SiteText(
		const std::vector<QString> &chain,
		const QString &field) {
	static const auto framing = QSet<QString>{
		u"core_message"_q,
		u"msg_container"_q,
		u"rpc_result"_q,
		u"invokeAfterMsg"_q,
		u"invokeAfterMsgs"_q,
		u"initConnection"_q,
		u"invokeWithLayer"_q,
		u"invokeWithoutUpdates"_q,
		u"invokeWithMessagesRange"_q,
		u"invokeWithTakeout"_q,
	};
	auto top = QString();
	for (const auto &name : chain) {
		if (!framing.contains(name)) {
			top = name;
			break;
		}
	}
	if (top.isEmpty() && !chain.empty()) {
		top = chain.back();
	}
	return u"%1/%2.%3"_q
		.arg(top.isEmpty() ? u"?"_q : SafeIdentifier(top))
		.arg(chain.empty() ? u"?"_q : SafeIdentifier(chain.back()))
		.arg(field.isEmpty() ? u"-"_q : SafeIdentifier(field));
}

struct LineContext {
	DumpDirection direction = DumpDirection::None;
	const std::vector<QString> *stack = nullptr;
	int dumpFrom = 0;
	const QString *file = nullptr; // the public name of the scanned file
};

// Per file: a dump entry runs from its header to the next entry header;
// the open-object stack is carried across its lines.
struct DumpTracker {
	DumpDirection direction = DumpDirection::None;
	std::vector<QString> stack;
	int dumpFrom = 0;

	LineContext begin(const QString &line, SecrecyClassReading &reading);
	void end(const QString &line);
};

LineContext DumpTracker::begin(
		const QString &line,
		SecrecyClassReading &reading) {
	dumpFrom = 0;
	if (IsEntryHeader(line)) {
		stack.clear();
		direction = HeaderDirection(line, &dumpFrom);
		if (direction == DumpDirection::Send) {
			++reading.sendHeaders;
		} else if (direction == DumpDirection::Recv) {
			++reading.recvHeaders;
		}
	}
	return { direction, &stack, dumpFrom };
}

void DumpTracker::end(const QString &line) {
	if (direction != DumpDirection::None) {
		WalkDump(line, dumpFrom, int(line.size()), stack);
	}
}

[[nodiscard]] bool InDump(const LineContext &context, int at) {
	return (context.direction != DumpDirection::None)
		&& context.stack
		&& (at >= context.dumpFrom);
}

[[nodiscard]] QString SiteAt(
		const QString &text,
		const LineContext &context,
		int at,
		bool *quoted = nullptr) {
	auto chain = *context.stack;
	const auto walk = WalkDump(text, context.dumpFrom, at, chain, true);
	if (quoted) {
		*quoted = walk.quoted;
	}
	return SiteText(chain, walk.field);
}

// The app log's "[yyyy.MM.dd hh:mm:ss] " (logs.cpp writeMain).
[[nodiscard]] bool IsMainLogStart(const QString &line) {
	if (line.size() < kMainLogStart
		|| line[0] != QChar('[')
		|| line[20] != QChar(']')
		|| line[21] != QChar(' ')) {
		return false;
	}
	for (const auto index : {
		1, 2, 3, 4, 6, 7, 9, 10, 12, 13, 15, 16, 18, 19,
	}) {
		if (!line[index].isDigit()) {
			return false;
		}
	}
	return (line[5] == QChar('.'))
		&& (line[8] == QChar('.'))
		&& (line[11] == QChar(' '))
		&& (line[14] == QChar(':'))
		&& (line[17] == QChar(':'));
}

// Where the message of a plain line starts, per writer (logs.cpp): after
// the app log's timestamp (writeMain), after a DebugLogs entry start
// (writeDebug) and, in an mtp_ part, also after its "(dc:<dc>) "
// (writeMtp). 0 for a test-log row, which has no prefix, and for a
// continuation line of a multi-line entry.
[[nodiscard]] int MessageFrom(const QString &line) {
	if (IsMainLogStart(line)) {
		return kMainLogStart;
	} else if (!IsEntryHeader(line)) {
		return 0;
	}
	const auto close = int(line.indexOf(u"] "_q, 14));
	if (close < 0) {
		return 0;
	}
	auto result = close + 2;
	if (line.mid(result, 4) == u"(dc:"_q) {
		const auto dc = int(line.indexOf(u") "_q, result));
		if (dc > 0) {
			result = dc + 2;
		}
	}
	return result;
}

// A "name=value" field of a plain line. The name is an ASCII identifier
// (SafeIdentifier) at the message start or right after a blank, outside a
// double-quoted segment; the value runs to the next blank, less a trailing
// run of ";,.:" and then one enclosing pair of quotes.
struct PlainField {
	QString name;
	int nameFrom = 0;
	int valueFrom = 0;
	int valueTill = 0;
};

[[nodiscard]] bool IsBlank(QChar ch) {
	return (ch == QChar(' ')) || (ch == QChar('\t'));
}

// One left-to-right pass over the message from |from|. Scanning resumes at
// the blank that ended a value, so a "name=" inside a value, quoted or not,
// is never a field.
[[nodiscard]] std::vector<PlainField> PlainFields(
		const QString &text,
		int from) {
	static const auto trailing = u";,.:"_q;
	auto result = std::vector<PlainField>();
	const auto size = int(text.size());
	auto i = from;
	while (i < size) {
		if (IsBlank(text[i]) || (i > from && !IsBlank(text[i - 1]))) {
			++i;
			continue;
		}
		auto end = i;
		while (end < size && IdentifierChar(text[end])) {
			++end;
		}
		const auto name = text.mid(i, end - i);
		if (end == i
			|| end >= size
			|| text[end] != QChar('=')
			|| SafeIdentifier(name) != name
			|| InsideQuotes(text, i)) {
			i = std::max(end, i + 1);
			continue;
		}
		auto till = end + 1;
		while (till < size && !IsBlank(text[till])) {
			++till;
		}
		auto valueFrom = end + 1;
		auto valueTill = till;
		while (valueTill > valueFrom && trailing.contains(text[valueTill - 1])) {
			--valueTill;
		}
		if (valueTill - valueFrom >= 2
			&& text[valueFrom] == QChar('"')
			&& text[valueTill - 1] == QChar('"')) {
			++valueFrom;
			--valueTill;
		}
		result.push_back({
			.name = name,
			.nameFrom = i,
			.valueFrom = valueFrom,
			.valueTill = valueTill,
		});
		i = till;
	}
	return result;
}

// A message head prints only in this identifier-like shape, so no quote,
// bracket, "=", "|" or escape can make it carry more than product text.
[[nodiscard]] bool HeadShaped(const QString &head) {
	static const auto extra = u" _.:-/"_q;
	if (head.isEmpty() || head.size() > kMaxHeadLength) {
		return false;
	}
	for (const auto ch : head) {
		if ((ch.unicode() > 0x7F)
			|| !(ch.isLetterOrNumber() || extra.contains(ch))) {
			return false;
		}
	}
	return true;
}

// The secrets a reading can decide on: phrases of at least two normalized
// words, non-empty short secrets and tokens of at least kMinTokenLength
// characters, each once. What was refused is counted into |reading|.
[[nodiscard]] SecrecySecrets UsableSecrets(
		const SecrecySecrets &secrets,
		SecrecyReading *reading) {
	auto result = SecrecySecrets();
	auto phrasesTooShort = 0;
	auto tokensTooShort = 0;
	for (const auto &phrase : secrets.phrases) {
		auto words = std::vector<QString>();
		for (const auto &word : phrase) {
			const auto normalized = NormalizedWord(word);
			if (!normalized.isEmpty()) {
				words.push_back(normalized);
			}
		}
		if (words.size() >= 2) {
			result.phrases.push_back(std::move(words));
		} else if (!words.empty()) {
			++phrasesTooShort;
		}
	}
	auto seen = QSet<QString>();
	for (const auto &secret : secrets.shortSecrets) {
		if (!secret.isEmpty() && !seen.contains(secret)) {
			seen.insert(secret);
			result.shortSecrets.push_back(secret);
		}
	}
	seen.clear();
	for (const auto &token : secrets.tokens) {
		if (token.isEmpty() || seen.contains(token)) {
			continue;
		}
		seen.insert(token);
		if (token.size() < kMinTokenLength) {
			++tokensTooShort;
		} else {
			result.tokens.push_back(token);
		}
	}
	if (reading) {
		reading->phrasesGiven = int(result.phrases.size());
		reading->shortGiven = int(result.shortSecrets.size());
		reading->tokensGiven = int(result.tokens.size());
		reading->phrasesTooShort = phrasesTooShort;
		reading->tokensTooShort = tokensTooShort;
	}
	return result;
}

// A plain-line bounded short-secret hit: its public site, and whether it is
// exactly the value of a declared computed field.
struct PlainHit {
	QString site;
	bool computed = false;
};

class SecrecyMatcher final {
public:
	explicit SecrecyMatcher(
		const SecrecySecrets &usable,
		std::vector<SecrecyComputedField> computed = {});

	// |run| is the last phrase word of the current run, carried from line
	// to line of one file or text by the caller.
	void line(
		const QString &text,
		const LineContext &context,
		QString &run,
		SecrecyClassReading &reading) const;

private:
	void countWordRuns(
		const QString &text,
		const LineContext &context,
		QString &run,
		SecrecyClassReading &reading) const;
	void countShort(
		const QString &text,
		const QString &needle,
		const LineContext &context,
		bool dumpOnly,
		SecrecyClassReading &reading) const;
	void countToken(
		const QString &text,
		const QString &token,
		const LineContext &context,
		SecrecyClassReading &reading) const;
	[[nodiscard]] PlainHit plainHit(
		const QString &text,
		const LineContext &context,
		int at,
		int till) const;
	[[nodiscard]] bool holds(const QString &text) const;

	QSet<QString> _words;
	QSet<QString> _pairs;
	int _longest = 0;
	std::vector<QString> _shortSecrets;
	std::vector<QString> _escaped; // empty where it equals the raw form
	std::vector<QString> _tokens;
	std::vector<SecrecyComputedField> _computed;

};

// Feeds the lines of one file or text through a matcher, tracking the dump
// context and counting the known-present controls.
class LineFeed final {
public:
	LineFeed(
		const SecrecyMatcher &matcher,
		const QString &planted,
		const QString &control,
		SecrecyClassReading &reading,
		const QString &file);

	void feed(const QString &line);

private:
	const SecrecyMatcher &_matcher;
	const QString _planted;
	const QString _control;
	const QString _file; // public name, printed in plain-line sites
	SecrecyClassReading &_reading;
	DumpTracker _dump;
	QString _run;

};

SecrecyMatcher::SecrecyMatcher(
	const SecrecySecrets &usable,
	std::vector<SecrecyComputedField> computed)
: _shortSecrets(usable.shortSecrets)
, _tokens(usable.tokens)
, _computed(std::move(computed)) {
	for (const auto &words : usable.phrases) {
		auto previous = QString();
		for (const auto &word : words) {
			const auto normalized = NormalizedWord(word);
			_words.insert(normalized);
			_longest = std::max(_longest, int(normalized.size()));
			if (!previous.isEmpty()) {
				_pairs.insert(previous + QChar(' ') + normalized);
			}
			previous = normalized;
		}
	}
	for (const auto &secret : _shortSecrets) {
		const auto escaped = DumpEscaped(secret);
		_escaped.push_back((escaped != secret) ? escaped : QString());
	}
}

void SecrecyMatcher::line(
		const QString &text,
		const LineContext &context,
		QString &run,
		SecrecyClassReading &reading) const {
	if (!_pairs.isEmpty()) {
		countWordRuns(text, context, run, reading);
	}
	for (auto i = 0; i != int(_shortSecrets.size()); ++i) {
		countShort(text, _shortSecrets[i], context, false, reading);
		if (!_escaped[i].isEmpty()) {
			countShort(text, _escaped[i], context, true, reading);
		}
	}
	for (const auto &token : _tokens) {
		countToken(text, token, context, reading);
	}
}

// A word run is an ordered adjacent pair of one phrase's words,
// case-insensitive. Words are the letter-and-digit runs of the line once
// WordSeparated has blanked escapes and type tags, so whitespace, commas,
// quotes, brackets and line breaks all separate them, and the run carries
// across the lines of one file: a phrase one word per line is still a run.
// A number is neutral (a list index), any other word ends the run, and
// every entry header ends it (LineFeed). A token longer than every phrase
// word matches nothing, so it is not lowered.
void SecrecyMatcher::countWordRuns(
		const QString &raw,
		const LineContext &context,
		QString &run,
		SecrecyClassReading &reading) const {
	const auto text = WordSeparated(raw);
	const auto size = int(text.size());
	auto i = 0;
	while (i < size) {
		while (i < size && !text[i].isLetterOrNumber()) {
			++i;
		}
		const auto from = i;
		while (i < size && text[i].isLetterOrNumber()) {
			++i;
		}
		const auto length = i - from;
		if (!length) {
			break;
		} else if (AllDigits(text, from, i)) {
			continue;
		} else if (length <= _longest) {
			const auto normalized = text.mid(from, length).toLower();
			if (_words.contains(normalized)) {
				if (!run.isEmpty()
					&& _pairs.contains(run + QChar(' ') + normalized)) {
					if (!InDump(context, from)) {
						++reading.wordRunsOther;
					} else if (context.direction == DumpDirection::Send) {
						++reading.wordRunsSend;
					} else {
						++reading.wordRunsRecv;
					}
				}
				run = normalized;
				continue;
			}
		}
		run.clear();
	}
}

// A short secret's occurrence is bounded when the characters right before
// and right after it are no letter or digit (or the line's edges), and
// embedded otherwise. A bounded one sits in a line the client composed
// (Other), in a dump entry the client sent (Send) or in one it received
// (Recv); only Other and Send decide. |dumpOnly| is the escaped form, which
// only a dump can carry. A plain-line hit is reported at its
// "<file>|<head>|<field>" site (plainHit), and one that is exactly the
// value of a declared computed field is reported and does not decide.
void SecrecyMatcher::countShort(
		const QString &text,
		const QString &needle,
		const LineContext &context,
		bool dumpOnly,
		SecrecyClassReading &reading) const {
	const auto size = int(text.size());
	const auto length = int(needle.size());
	auto from = 0;
	while (true) {
		const auto at = int(text.indexOf(needle, from));
		if (at < 0) {
			break;
		}
		from = at + 1;
		const auto inDump = InDump(context, at);
		if (dumpOnly && !inDump) {
			continue;
		}
		const auto till = at + length;
		const auto before = (at == 0) || !text[at - 1].isLetterOrNumber();
		const auto after = (till >= size) || !text[till].isLetterOrNumber();
		if (!before || !after) {
			++reading.embedded;
			continue;
		}
		++reading.bounded;
		if (!inDump) {
			++(InsideQuotes(text, at)
				? reading.boundedQuoted
				: reading.boundedUnquoted);
			const auto hit = plainHit(text, context, at, till);
			if (hit.computed) {
				++reading.boundedComputed;
				++reading.computedSites[hit.site];
			} else {
				++reading.boundedOther;
				++reading.plainSites[hit.site];
			}
			continue;
		}
		auto quoted = false;
		const auto site = SiteAt(text, context, at, &quoted);
		++(quoted ? reading.boundedQuoted : reading.boundedUnquoted);
		if (context.direction == DumpDirection::Send) {
			++reading.boundedSend;
			++reading.sendSites[site];
		} else {
			++reading.boundedRecv;
			++reading.recvSites[site];
		}
	}
}

// Every substring occurrence of a token counts: a record id or a secret ref
// is long and random enough that a coincidence is not a concern.
void SecrecyMatcher::countToken(
		const QString &text,
		const QString &token,
		const LineContext &context,
		SecrecyClassReading &reading) const {
	auto from = 0;
	while (true) {
		const auto at = int(text.indexOf(token, from));
		if (at < 0) {
			break;
		}
		from = at + 1;
		if (!InDump(context, at)) {
			++reading.tokensOther;
			continue;
		}
		const auto site = SiteAt(text, context, at);
		if (context.direction == DumpDirection::Send) {
			++reading.tokensSend;
			++reading.sendSites[site];
		} else {
			++reading.tokensRecv;
			++reading.recvSites[site];
		}
	}
}

// The site of a plain-line hit over text[at, till): the file, the message
// head (the text from the message start to the first field's name) and the
// name of the field whose whole value the hit is, or "-". The whole site is
// withheld as "<file>|?|?", for every reason alike, when the line has no
// field, the hit lies in the head, the head is not HeadShaped, or the head
// (with the printed field name) holds any given secret. Only a hit whose
// printed head and field equal a declaration is computed, so a declaration
// never matches a withheld site.
PlainHit SecrecyMatcher::plainHit(
		const QString &text,
		const LineContext &context,
		int at,
		int till) const {
	const auto file = context.file ? *context.file : QString();
	const auto withheld = PlainHit{ .site = file + QChar('|') + kSiteWithheld };
	const auto from = MessageFrom(text);
	const auto fields = PlainFields(text, from);
	if (fields.empty()) {
		return withheld;
	}
	const auto headTill = fields.front().nameFrom;
	if (at < headTill && till > from) {
		return withheld;
	}
	const auto head = text.mid(from, headTill - from).trimmed();
	auto name = QString();
	for (const auto &field : fields) {
		if (field.valueFrom == at && field.valueTill == till) {
			name = field.name;
			break;
		}
	}
	const auto shown = name.isEmpty() ? head : (head + QChar(' ') + name);
	if (!HeadShaped(head) || holds(shown)) {
		return withheld;
	}
	const auto computed = !name.isEmpty()
		&& ranges::any_of(_computed, [&](const SecrecyComputedField &field) {
			return (field.head == head) && (field.field == name);
		});
	return {
		.site = file
			+ QChar('|')
			+ head
			+ QChar('|')
			+ (name.isEmpty() ? kSiteNoField : name),
		.computed = computed,
	};
}

// Whether printed text would carry secret material: any given short secret
// or token, case-insensitively (stricter than the case-sensitive match, so
// no case variant prints), or an adjacent pair of one phrase's words.
bool SecrecyMatcher::holds(const QString &text) const {
	const auto contains = [&](const std::vector<QString> &list) {
		return ranges::any_of(list, [&](const QString &secret) {
			return text.contains(secret, Qt::CaseInsensitive);
		});
	};
	if (contains(_shortSecrets) || contains(_tokens)) {
		return true;
	} else if (_pairs.isEmpty()) {
		return false;
	}
	auto run = QString();
	auto reading = SecrecyClassReading();
	countWordRuns(text, LineContext(), run, reading);
	return (reading.wordRunsOther > 0);
}

LineFeed::LineFeed(
	const SecrecyMatcher &matcher,
	const QString &planted,
	const QString &control,
	SecrecyClassReading &reading,
	const QString &file)
: _matcher(matcher)
, _planted(planted)
, _control(control)
, _file(file)
, _reading(reading) {
}

void LineFeed::feed(const QString &line) {
	++_reading.lines;
	if (IsEntryHeader(line)) {
		_run.clear();
	}
	auto context = _dump.begin(line, _reading);
	context.file = &_file;
	_matcher.line(line, context, _run, _reading);
	if (!_planted.isEmpty() && line.contains(_planted)) {
		++_reading.plantedHits;
	}
	if (!_control.isEmpty() && line.contains(_control)) {
		++_reading.controlHits;
	}
	_dump.end(line);
}

[[nodiscard]] QString ChopLineEnd(QString line) {
	while (!line.isEmpty()
		&& (line.back() == QChar('\n') || line.back() == QChar('\r'))) {
		line.chop(1);
	}
	return line;
}

void ScanText(
		const QString &text,
		const SecrecyMatcher &matcher,
		const QString &planted,
		const QString &control,
		SecrecyClassReading &reading,
		const QString &file = QString()) {
	auto feed = LineFeed(matcher, planted, control, reading, file);
	const auto size = int(text.size());
	auto from = 0;
	while (from < size) {
		auto till = int(text.indexOf(QChar('\n'), from));
		if (till < 0) {
			till = size;
		}
		feed.feed(ChopLineEnd(text.mid(from, till - from)));
		from = till + 1;
	}
}

struct FileScan {
	bool opened = false;
	int slicedFrom = 0; // 1-based line of the last banner, 0 when whole
};

// Raw bytes, no QIODevice::Text, streamed line by line: an mtp part can be
// large. With |slice| a first pass finds the last banner reopen() wrote
// when this launch appended to a same-day part (logs.cpp), and the second
// pass scans from it, so an earlier launch's lines are not charged here.
// |name| is the file's public name, printed in plain-line sites.
FileScan ScanFile(
		const QString &path,
		bool slice,
		const SecrecyMatcher &matcher,
		const QString &planted,
		const QString &control,
		SecrecyClassReading &reading,
		const QString &name) {
	auto result = FileScan();
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return result;
	}
	result.opened = true;
	if (slice) {
		auto index = 0;
		while (!file.atEnd()) {
			++index;
			const auto line = QString::fromUtf8(file.readLine());
			if (line.trimmed() == kBanner) {
				result.slicedFrom = index;
			}
		}
		file.seek(0);
	}
	auto feed = LineFeed(matcher, planted, control, reading, name);
	auto index = 0;
	while (!file.atEnd()) {
		++index;
		const auto bytes = file.readLine();
		if (index < result.slicedFrom) {
			continue;
		}
		feed.feed(ChopLineEnd(QString::fromUtf8(bytes)));
	}
	return result;
}

// A part is a given day's only when its first line is that day's index; a
// same-name file of another day is foreign and is not scanned.
[[nodiscard]] PartIdentity ReadPartIdentity(
		const QString &path,
		const QString &dayIndex) {
	if (!QFile::exists(path)) {
		return PartIdentity::Missing;
	}
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return PartIdentity::Unreadable;
	}
	const auto first = QString::fromUtf8(file.readLine()).trimmed();
	return (first == dayIndex)
		? PartIdentity::Accepted
		: PartIdentity::Foreign;
}

[[nodiscard]] bool ValidPartName(const QString &name) {
	static const auto re = QRegularExpression(
		u"^(log|mtp)_\\d\\d_\\d\\d\\.txt$"_q);
	return re.match(name).hasMatch();
}

[[nodiscard]] bool IsMtpName(const QString &name) {
	return name.mid(name.lastIndexOf(QChar('/')) + 1).startsWith(u"mtp_"_q);
}

[[nodiscard]] bool IsWindowClass(SecrecyClass cls) {
	return (cls == SecrecyClass::DebugLog) || (cls == SecrecyClass::MtpLog);
}

[[nodiscard]] QString CanaryDump(
		const QString &direction,
		const QString &secret) {
	return u"[00:00:00.000 00-0000000] (dc:2_main) "_q
		+ direction
		+ u": { core_message\n"
		"  body: { account_password\n"
		"    hint: \""_q
		+ DumpEscaped(secret)
		+ u"\" [STRING]\n"
		"  }\n"
		"} (dc:2,key:0,session:0)"_q;
}

// A Send entry dumping a vector of strings one element per line.
[[nodiscard]] QString CanaryVectorDump(
		const QString &first,
		const QString &second) {
	return u"[00:00:00.000 00-0000000] (dc:2_main) Send: { core_message\n"
		"  body: { wallet_canary\n"
		"    words: [ vector<0xb5286e24> (2)\n"
		"      \""_q
		+ DumpEscaped(first)
		+ u"\" [STRING],\n"
		"      \""_q
		+ DumpEscaped(second)
		+ u"\" [STRING],\n"
		"    ]\n"
		"  }\n"
		"} (dc:2,key:0,session:0)"_q;
}

// Negative-control canaries, built in memory and never written. Each runs
// through the same matcher class, LineFeed and dump walk as the files, but
// over a matcher of that one secret, so another secret it happens to
// contain (a ref contains its record id) cannot change its count.
void ReadCanaries(const SecrecySecrets &usable, SecrecyReading &result) {
	const auto site = u"account_password/account_password.hint"_q;
	for (const auto &words : usable.phrases) {
		const auto matcher = SecrecyMatcher({ .phrases = { words } });
		const auto last = int(words.size()) - 2;
		const auto pair = [&](int first, const QString &separator) {
			return words[first] + separator + words[first + 1];
		};
		struct WordCanary {
			QString text;
			bool send = false;
		};
		const auto canaries = std::vector<WordCanary>{
			{ pair(0, u" "_q) },
			{ pair(last, u" "_q) },
			{ u"[00:00:00.000 00-0000000] words:\n"_q + pair(0, u"\n"_q) },
			{ u"NOTE: words: "_q + pair(last, u"\\u000A"_q) },
			{ u"words=["_q + pair(0, u","_q) + u"]"_q },
			{ CanaryDump(u"Send"_q, pair(last, u"\n"_q)), true },
			{ CanaryVectorDump(words[0], words[1]), true },
		};
		for (const auto &canary : canaries) {
			auto reading = SecrecyClassReading();
			ScanText(canary.text, matcher, {}, {}, reading);
			++result.canaryWordRunsExpected;
			const auto other = canary.send ? 0 : 1;
			const auto send = canary.send ? 1 : 0;
			if (reading.wordRunsOther == other
				&& reading.wordRunsSend == send
				&& !reading.wordRunsRecv) {
				++result.canaryWordRuns;
			}
		}
	}
	for (const auto &secret : usable.shortSecrets) {
		const auto matcher = SecrecyMatcher({ .shortSecrets = { secret } });
		++result.canaryShortExpected;

		auto bounded = SecrecyClassReading();
		ScanText(u"canary="_q + secret, matcher, {}, {}, bounded);
		if (bounded.bounded == 1 && bounded.boundedOther == 1) {
			++result.canaryBounded;
		}

		auto wrap = QChar('x');
		for (const auto ch : { 'x', 'q', 'z', 'j', 'k' }) {
			if (!secret.contains(QChar(ch), Qt::CaseInsensitive)) {
				wrap = QChar(ch);
				break;
			}
		}
		auto embedded = SecrecyClassReading();
		ScanText(QString(wrap) + secret + wrap, matcher, {}, {}, embedded);
		if (embedded.embedded == 1 && embedded.bounded == 0) {
			++result.canaryEmbedded;
		}

		auto send = SecrecyClassReading();
		ScanText(CanaryDump(u"Send"_q, secret), matcher, {}, {}, send);
		if (send.boundedSend == 1
			&& send.boundedRecv == 0
			&& send.bounded == 1
			&& send.sendSites.size() == 1
			&& send.sendSites.contains(site)) {
			++result.canarySend;
		}

		auto recv = SecrecyClassReading();
		ScanText(CanaryDump(u"Recv"_q, secret), matcher, {}, {}, recv);
		if (recv.boundedRecv == 1
			&& recv.boundedSend == 0
			&& recv.bounded == 1
			&& recv.recvSites.size() == 1
			&& recv.recvSites.contains(site)) {
			++result.canaryRecv;
		}
	}
	for (const auto &token : usable.tokens) {
		const auto matcher = SecrecyMatcher({ .tokens = { token } });
		++result.canaryTokenExpected;
		auto canary = SecrecyClassReading();
		ScanText(u"ref="_q + token, matcher, {}, {}, canary);
		if (canary.tokensOther == 1
			&& !canary.tokensSend
			&& !canary.tokensRecv) {
			++result.canaryToken;
		}
	}
	const auto shortOk = [&](int passed) {
		return (passed == result.canaryShortExpected);
	};
	result.canariesHold
		= (result.canaryWordRuns == result.canaryWordRunsExpected)
		&& shortOk(result.canaryBounded)
		&& shortOk(result.canaryEmbedded)
		&& shortOk(result.canarySend)
		&& shortOk(result.canaryRecv)
		&& (result.canaryToken == result.canaryTokenExpected);
}

void ReadSource(
		const SecrecySource &source,
		const SecrecyMatcher &matcher,
		const QString &planted,
		SecrecyClassReading &reading) {
	if (source.invalidName) {
		++reading.invalidNames;
		return;
	}
	switch (source.identity) {
	case PartIdentity::Missing: ++reading.missing; return;
	case PartIdentity::Unreadable: ++reading.unreadable; return;
	case PartIdentity::Foreign: ++reading.foreign; return;
	case PartIdentity::Accepted: break;
	}
	if (!source.control.isEmpty()) {
		reading.namedControl = true;
	}
	const auto controlsBefore = reading.controlHits;
	auto slicedFrom = 0;
	if (source.fromText) {
		if (source.text.isEmpty()) {
			++reading.missing;
			return;
		}
		reading.fromLogger = true;
		ScanText(
			source.text,
			matcher,
			planted,
			source.control,
			reading,
			source.name);
	} else {
		const auto scan = ScanFile(
			source.path,
			source.sliceAtLastBanner,
			matcher,
			planted,
			source.control,
			reading,
			source.name);
		if (!scan.opened) {
			++reading.unreadable;
			return;
		}
		slicedFrom = scan.slicedFrom;
	}
	++reading.files;
	reading.names.push_back(source.name);
	reading.slicedFrom.push_back(slicedFrom);
	if (IsMtpName(source.name)) {
		++reading.mtpFiles;
	}
	if (reading.cls == SecrecyClass::EarlierParts
		&& (source.control.isEmpty()
			|| reading.controlHits == controlsBefore)) {
		++reading.withoutControl;
	}
}

void DecideClass(SecrecyClassReading &reading) {
	auto reasons = QStringList();
	if (IsWindowClass(reading.cls) && !reading.candidates) {
		reasons.push_back(u"the window yielded no candidates"_q);
	}
	if (!reading.files) {
		reasons.push_back(u"no accepted file"_q);
	}
	if (reading.unreadable) {
		reasons.push_back(u"unreadable part"_q);
	}
	if (reading.cls == SecrecyClass::EarlierParts) {
		if (reading.invalidNames) {
			reasons.push_back(u"invalid part name"_q);
		}
		if (reading.missing) {
			reasons.push_back(u"missing part"_q);
		}
		if (reading.foreign) {
			reasons.push_back(u"foreign part"_q);
		}
		if (reading.withoutControl) {
			reasons.push_back(u"part without its own control"_q);
		}
	} else if (reading.files) {
		if (!reading.plantedHits) {
			reasons.push_back(u"no planted control"_q);
		}
		if (reading.namedControl && !reading.controlHits) {
			reasons.push_back(u"named control absent"_q);
		}
	}
	if (reading.mtpFiles
		&& !(reading.sendHeaders > 0 && reading.recvHeaders > 0)) {
		reasons.push_back(u"mtp part without headers of both directions"_q);
	}
	reading.decided = reasons.isEmpty();
	reading.undecidedReason = reasons.join(u"; "_q);
}

[[nodiscard]] QString SitesText(const std::map<QString, int> &sites) {
	auto list = QStringList();
	for (const auto &[site, hits] : sites) {
		list.push_back(site + u" x"_q + QString::number(hits));
	}
	return list.join(u", "_q);
}

[[nodiscard]] QString IntsText(const std::vector<int> &values) {
	auto list = QStringList();
	for (const auto value : values) {
		list.push_back(QString::number(value));
	}
	return list.join(u", "_q);
}

[[nodiscard]] QString TimeText(const QDateTime &value) {
	return value.isValid()
		? value.toString(u"yyyy-MM-dd hh:mm:ss"_q)
		: u"invalid"_q;
}

[[nodiscard]] QString ClassRow(const SecrecyClassReading &c) {
	const auto pairs = std::vector<std::pair<QString, int>>{
		{ u"candidates"_q, c.candidates },
		{ u"files"_q, c.files },
		{ u"foreign"_q, c.foreign },
		{ u"unreadable"_q, c.unreadable },
		{ u"missing"_q, c.missing },
		{ u"invalidNames"_q, c.invalidNames },
		{ u"withoutControl"_q, c.withoutControl },
		{ u"mtpFiles"_q, c.mtpFiles },
		{ u"lines"_q, c.lines },
		{ u"wordRunsOther"_q, c.wordRunsOther },
		{ u"wordRunsSend"_q, c.wordRunsSend },
		{ u"wordRunsRecv"_q, c.wordRunsRecv },
		{ u"bounded"_q, c.bounded },
		{ u"embedded"_q, c.embedded },
		{ u"boundedOther"_q, c.boundedOther },
		{ u"boundedSend"_q, c.boundedSend },
		{ u"boundedRecv"_q, c.boundedRecv },
		{ u"boundedComputed"_q, c.boundedComputed },
		{ u"boundedQuoted"_q, c.boundedQuoted },
		{ u"boundedUnquoted"_q, c.boundedUnquoted },
		{ u"tokensOther"_q, c.tokensOther },
		{ u"tokensSend"_q, c.tokensSend },
		{ u"tokensRecv"_q, c.tokensRecv },
		{ u"sendHeaders"_q, c.sendHeaders },
		{ u"recvHeaders"_q, c.recvHeaders },
		{ u"plantedHits"_q, c.plantedHits },
		{ u"controlHits"_q, c.controlHits },
		{ u"clientWritten"_q, c.clientWritten() },
		{ u"received"_q, c.received() },
	};
	auto parts = QStringList();
	parts.push_back(u"SECRECY_CLASS: "_q + SecrecyClassName(c.cls));
	parts.push_back(u"source="_q
		+ (c.fromLogger ? u"logger"_q : u"files"_q));
	parts.push_back(u"decided="_q + B(c.decided));
	parts.push_back(u"undecidedReason=["_q + c.undecidedReason + u"]"_q);
	for (const auto &[key, value] : pairs) {
		parts.push_back(key + QChar('=') + QString::number(value));
	}
	parts.push_back(u"namedControl="_q + B(c.namedControl));
	parts.push_back(u"sendSites=["_q + SitesText(c.sendSites) + u"]"_q);
	parts.push_back(u"plainSites=["_q + SitesText(c.plainSites) + u"]"_q);
	parts.push_back(u"names=["_q + c.names.join(u", "_q) + u"]"_q);
	parts.push_back(u"slicedFrom=["_q + IntsText(c.slicedFrom) + u"]"_q);
	return parts.join(QChar(' '));
}

[[nodiscard]] QString AppendedSince(const QString &path, qint64 from) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return QString();
	}
	file.seek(from);
	return QString::fromUtf8(file.readAll());
}

[[nodiscard]] QString TestLogPath() {
	return EvidenceDir() + u"test_log.txt"_q;
}

[[nodiscard]] int HitTotal(const SecrecyClassReading &c) {
	return c.wordRunsOther
		+ c.wordRunsSend
		+ c.wordRunsRecv
		+ c.bounded
		+ c.tokensOther
		+ c.tokensSend
		+ c.tokensRecv;
}

void ReportClass(const SecrecyClassReading &c) {
	auto scan = DiscriminatingScan(
		u"SECRECY: "_q + SecrecyClassName(c.cls),
		u"client-written fixture secret"_q,
		u"the planted control or the named control"_q);
	scan.examined(c.lines);
	const auto subjects = std::vector<std::pair<QString, int>>{
		{ u"word-run/plain"_q, c.wordRunsOther },
		{ u"word-run/Send"_q, c.wordRunsSend },
		{ u"token/plain"_q, c.tokensOther },
	};
	for (const auto &[detail, count] : subjects) {
		for (auto i = 0; i != count; ++i) {
			scan.matchedSubject(detail);
		}
	}
	for (const auto &[site, count] : c.plainSites) {
		for (auto i = 0; i != count; ++i) {
			scan.matchedSubject(u"bounded/plain:"_q + site);
		}
	}
	for (const auto &[site, count] : c.sendSites) {
		for (auto i = 0; i != count; ++i) {
			scan.matchedSubject(u"Send:"_q + site);
		}
	}
	auto details = 0;
	const auto controls = std::vector<std::pair<QString, int>>{
		{ u"planted"_q, c.plantedHits },
		{ u"named"_q, c.controlHits },
	};
	for (const auto &[detail, count] : controls) {
		for (auto i = 0; i != count; ++i) {
			scan.matchedControl((details++ < kControlDetails)
				? detail
				: QString());
		}
	}
	scan.report();
}

// ---- self-test -------------------------------------------------------------

const auto kSelfPassword = u"k9Tq2"_q;
const auto kSelfToken = u"3f6c1a2e-9b7d-4c58-a1e0-5d2f8b9c7e41"_q;
const auto kSelfShortToken = u"tok4567890"_q;
const auto kSelfNonce = u"secrecy-control-selftest00000001"_q;
const auto kSelfFiller = u"FILLER_LINE_MARKER"_q;
const auto kSelfEarlierControl = u"EARLIER_CONTROL_A"_q;
const auto kSelfDay = QDate(2026, 1, 15);
const auto kSelfToday = u"20260115"_q;
const auto kSelfYesterday = u"20260114"_q;
const auto kSelfBannerRule = QString(64, QChar('-'));
const auto kSelfComputedHead = u"Selftest Info: computed state"_q;
const auto kSelfComputedField = u"digest"_q;
const auto kSelfOtherValue = u"OTHER_VALUE_MARKER"_q;

[[nodiscard]] std::vector<QString> SelfPhraseA() {
	return {
		u"walnut"_q, u"giraffe"_q, u"pumpkin"_q, u"lobster"_q,
		u"volcano"_q, u"oyster"_q, u"cactus"_q, u"dolphin"_q,
		u"kangaroo"_q, u"jaguar"_q, u"mango"_q, u"velvet"_q,
	};
}

[[nodiscard]] std::vector<QString> SelfPhraseB() {
	return {
		u"orchard"_q, u"hazard"_q, u"gorilla"_q, u"harvest"_q,
		u"lizard"_q, u"noodle"_q, u"raccoon"_q, u"tomato"_q,
		u"zebra"_q, u"puppy"_q, u"squirrel"_q, u"donkey"_q,
	};
}

[[nodiscard]] SecrecySecrets SelfSecrets() {
	return {
		.phrases = { SelfPhraseA(), SelfPhraseB() },
		.shortSecrets = { kSelfPassword },
		.tokens = { kSelfToken },
	};
}

// Every string a row of a synthetic reading must never contain: the
// secrets, the words, their adjacent pairs, a scanned line's filler and
// the value of a field other than the one a hit is.
[[nodiscard]] QStringList SelfForbidden() {
	auto result = QStringList{
		kSelfPassword,
		kSelfToken,
		kSelfShortToken,
		kSelfFiller,
		kSelfOtherValue,
	};
	for (const auto &phrase : { SelfPhraseA(), SelfPhraseB() }) {
		for (auto i = 0; i != int(phrase.size()); ++i) {
			result.push_back(phrase[i]);
			if (i + 1 < int(phrase.size())) {
				result.push_back(phrase[i] + QChar(' ') + phrase[i + 1]);
			}
		}
	}
	return result;
}

[[nodiscard]] QStringList SelfLeaksIn(const QStringList &rows) {
	const auto forbidden = SelfForbidden();
	auto result = QStringList();
	for (const auto &row : rows) {
		for (auto i = 0; i != int(forbidden.size()); ++i) {
			if (row.contains(forbidden[i], Qt::CaseInsensitive)) {
				// The index only: the forbidden string itself is a secret.
				result.push_back(u"forbidden#%1"_q.arg(i));
			}
		}
	}
	return result;
}

[[nodiscard]] QString Entry(const QString &text) {
	return u"[10:05:00.000 01-0000001] "_q + text;
}

[[nodiscard]] QString MtpEntry(const QString &text) {
	return u"[10:05:02.000 03-0000002] (dc:2_main) "_q + text;
}

[[nodiscard]] QStringList SendEntry(const QStringList &body) {
	auto result = QStringList{
		MtpEntry(u"Send: { core_message"_q),
		u"  body: { invokeWithLayer"_q,
	};
	result += body;
	result += QStringList{ u"  }"_q, u"} (dc:2,key:0,session:0)"_q };
	return result;
}

[[nodiscard]] QStringList RecvEntry(const QStringList &body) {
	auto result = QStringList{
		MtpEntry(u"Recv: { rpc_result"_q),
		u"  req_msg_id: 1 [LONG]"_q,
		u"  result: { messages_forumTopics"_q,
	};
	result += body;
	result += QStringList{ u"  }"_q, u"} (dc:2,key:0,session:0)"_q };
	return result;
}

[[nodiscard]] QString PlantedEntry() {
	return u"Test Info: secrecy control "_q + kSelfNonce;
}

// A product-shaped diagnostic line with the synthetic head: "<head>
// span=<span> digest=<digest>; mode=<other value>.<tail>", so one value is
// followed by ";" and one by ".".
[[nodiscard]] QString ComputedLine(
		const QString &span,
		const QString &digest,
		const QString &tail = QString()) {
	return kSelfComputedHead
		+ u" span="_q
		+ span
		+ u" digest="_q
		+ digest
		+ u"; mode="_q
		+ kSelfOtherValue
		+ u"."_q
		+ tail;
}

[[nodiscard]] SecrecyComputedField SelfDeclaration() {
	return { .head = kSelfComputedHead, .field = kSelfComputedField };
}

// Keeps a part's planted control its last line.
void InsertBeforePlanted(QStringList &lines, const QStringList &add) {
	const auto planted = lines.back();
	lines.pop_back();
	lines += add;
	lines.push_back(planted);
}

struct SelfFixture {
	bool testLogFile = true;
	QStringList testLog;
	QStringList appLog;
	std::map<QString, QStringList> parts; // DebugLogs file name -> lines
	std::vector<SecrecyEarlierPart> earlier;
	SecrecySecrets secrets;
	std::vector<SecrecyComputedField> computed; // declared computed fields
};

[[nodiscard]] SelfFixture CleanFixture() {
	auto result = SelfFixture();
	result.testLog = QStringList{
		u"NOTE: "_q + kSelfFiller + u" a harness row"_q,
		u"NOTE: SECRECY_CONTROL: "_q + kSelfNonce,
	};
	result.appLog = QStringList{
		u"[2026.01.15 10:05:00] "_q + kSelfFiller + u" app line"_q,
		u"[2026.01.15 10:05:01] "_q + PlantedEntry(),
	};
	result.parts[u"log_10_00.txt"_q] = QStringList{
		kSelfToday,
		Entry(kSelfFiller + u" debug line"_q),
		Entry(PlantedEntry()),
	};
	auto mtp = QStringList{ kSelfToday };
	mtp += SendEntry({ u"    query: { wallet_getState"_q, u"    }"_q });
	mtp += RecvEntry({
		u"    topics: [ vector<0x0>"_q,
		u"      { forumTopic"_q,
		u"        title: \"Public topic\" [STRING]"_q,
		u"      }"_q,
		u"    ]"_q,
	});
	mtp.push_back(MtpEntry(PlantedEntry()));
	result.parts[u"mtp_10_00.txt"_q] = mtp;
	result.secrets = SelfSecrets();
	return result;
}

// The computed-field trap: one product-shaped line whose "digest" value is
// the synthetic password, mirrored into the app log and its DebugLogs part
// as LOG writes it, with or without the declaration of that field.
[[nodiscard]] SelfFixture MirroredFixture(bool declared) {
	auto result = CleanFixture();
	const auto line = ComputedLine(u"17"_q, kSelfPassword);
	result.appLog.push_back(u"[2026.01.15 10:05:02] "_q + line);
	result.parts[u"log_10_00.txt"_q].push_back(Entry(line));
	if (declared) {
		result.computed.push_back(SelfDeclaration());
	}
	return result;
}

// The declared mirrored line plus six log_ lines whose sites must each be
// withheld: the hit in the head, a line with no field, a head whose last
// word and the field name form a phrase pair, a field name that is the
// password itself (its value's site is withheld; the name's own hit is no
// field's value and prints the head with "-"), a head holding the token,
// and a head holding the password upper-cased, which the case-sensitive
// match does not count but the case-insensitive print rule withholds.
[[nodiscard]] SelfFixture SitesFixture() {
	auto result = MirroredFixture(true);
	const auto a = SelfPhraseA();
	const auto pw = kSelfPassword;
	result.parts[u"log_10_00.txt"_q] += QStringList{
		Entry(u"Selftest Info: computed "_q + pw + u" digest=7"_q),
		Entry(kSelfComputedHead + QChar(' ') + pw),
		Entry(u"Selftest Info: "_q
			+ a[6]
			+ QChar(' ')
			+ a[7]
			+ QChar('=')
			+ pw),
		Entry(kSelfComputedHead + QChar(' ') + pw + QChar('=') + pw),
		Entry(u"Selftest Info: "_q + kSelfToken + u" digest="_q + pw),
		Entry(u"Selftest Info: "_q + pw.toUpper() + u" digest="_q + pw),
	};
	return result;
}

[[nodiscard]] bool WriteLines(const QString &path, const QStringList &lines) {
	auto file = QFile(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	const auto bytes = (lines.join(QChar('\n')) + QChar('\n')).toUtf8();
	return (file.write(bytes) == bytes.size());
}

struct SelfRun {
	bool prepared = false;
	SecrecyLaunchLogs logs;
	SecrecyReading reading;
};

// Builds the fixture in a fresh QTemporaryDir, which is removed again when
// this returns, and reads it with a fixed window: 10:05 to 10:20 on the
// synthetic day, so the candidates are the 10:00 and 10:15 parts.
[[nodiscard]] SelfRun RunFixture(const SelfFixture &fixture) {
	auto result = SelfRun();
	const auto dir = QTemporaryDir();
	if (!dir.isValid()) {
		return result;
	}
	const auto evidence = dir.path() + u"/evidence/"_q;
	const auto working = dir.path() + u"/work/"_q;
	if (!QDir().mkpath(evidence) || !QDir().mkpath(working + u"DebugLogs"_q)) {
		return result;
	}
	auto ok = true;
	if (fixture.testLogFile) {
		ok = WriteLines(evidence + u"test_log.txt"_q, fixture.testLog) && ok;
	}
	for (const auto &[name, lines] : fixture.parts) {
		ok = WriteLines(working + u"DebugLogs/"_q + name, lines) && ok;
	}
	if (!ok) {
		return result;
	}
	const auto appLog = fixture.appLog.isEmpty()
		? QString()
		: (fixture.appLog.join(QChar('\n')) + QChar('\n'));
	result.logs = SelectLaunchLogs(
		working,
		evidence,
		appLog,
		QDateTime(kSelfDay, QTime(10, 5)),
		QDateTime(kSelfDay, QTime(10, 20)),
		fixture.earlier);
	result.reading = ReadSecrecy(
		fixture.secrets,
		result.logs,
		kSelfNonce,
		fixture.computed);
	result.prepared = true;
	return result;
}

[[nodiscard]] const SecrecyClassReading &ClassOf(
		const SecrecyReading &reading,
		SecrecyClass cls) {
	static const auto empty = SecrecyClassReading();
	const auto found = reading.find(cls);
	return found ? *found : empty;
}

[[nodiscard]] QString Summary(const SecrecyReading &reading) {
	return u"decided=%1 clean=%2 clientWritten=%3 received=%4 "
		"canariesHold=%5 undecidedClasses=[%6] reasons=[%7]"_q
		.arg(B(reading.decided))
		.arg(B(reading.clean))
		.arg(reading.clientWritten())
		.arg(reading.received())
		.arg(B(reading.canariesHold))
		.arg(reading.undecidedClasses.join(u", "_q))
		.arg(reading.undecidedReasons.join(u"; "_q));
}

[[nodiscard]] QString Counts(const SecrecyClassReading &c) {
	return u"%1: files=%2 foreign=%3 missing=%4 invalidNames=%5 "
		"wordRunsOther=%6 wordRunsSend=%7 wordRunsRecv=%8 bounded=%9 "
		"embedded=%10 boundedOther=%11 boundedSend=%12 boundedRecv=%13 "
		"tokensOther=%14 tokensSend=%15 tokensRecv=%16 sendHeaders=%17 "
		"recvHeaders=%18 plantedHits=%19 controlHits=%20 decided=%21 "
		"reason=[%22] slicedFrom=[%23]"_q
		.arg(SecrecyClassName(c.cls))
		.arg(c.files)
		.arg(c.foreign)
		.arg(c.missing)
		.arg(c.invalidNames)
		.arg(c.wordRunsOther)
		.arg(c.wordRunsSend)
		.arg(c.wordRunsRecv)
		.arg(c.bounded)
		.arg(c.embedded)
		.arg(c.boundedOther)
		.arg(c.boundedSend)
		.arg(c.boundedRecv)
		.arg(c.tokensOther)
		.arg(c.tokensSend)
		.arg(c.tokensRecv)
		.arg(c.sendHeaders)
		.arg(c.recvHeaders)
		.arg(c.plantedHits)
		.arg(c.controlHits)
		.arg(B(c.decided))
		.arg(c.undecidedReason)
		.arg(IntsText(c.slicedFrom));
}

[[nodiscard]] QString SiteDetails(const SecrecyClassReading &c) {
	return Counts(c)
		+ u" boundedComputed="_q
		+ QString::number(c.boundedComputed)
		+ u" plainSites=["_q
		+ SitesText(c.plainSites)
		+ u"] computedSites=["_q
		+ SitesText(c.computedSites)
		+ u"] sendSites=["_q
		+ SitesText(c.sendSites)
		+ u"]"_q;
}

struct SelfState {
	std::vector<SecrecyReading> readings;
};

void Keep(
		const std::shared_ptr<SelfState> &state,
		const SelfRun &run) {
	if (run.prepared) {
		state->readings.push_back(run.reading);
	}
}

void CheckPrepared(const QString &what, const SelfRun &run) {
	if (!run.prepared) {
		Check(false, what, u"the synthetic log tree could not be written"_q);
	}
}

void SelfCanaries(const std::shared_ptr<SelfState> &state) {
	const auto what = u"secrecy self-test: canaries hold and a clean tree "
		"reads decided and clean"_q;
	const auto run = RunFixture(CleanFixture());
	CheckPrepared(what, run);
	Keep(state, run);
	const auto &r = run.reading;
	Check(
		run.prepared
			&& r.canariesHold
			&& (r.canaryWordRunsExpected == 2 * kWordRunCanaries)
			&& (r.canaryShortExpected == 1)
			&& (r.canaryTokenExpected == 1)
			&& r.decided
			&& r.clean
			&& (r.classes.size() == 4),
		what,
		u"canaries wordRuns=%1/%2 bounded=%3 embedded=%4 send=%5 recv=%6 "
		"of %7 token=%8/%9 %10"_q
			.arg(r.canaryWordRuns)
			.arg(r.canaryWordRunsExpected)
			.arg(r.canaryBounded)
			.arg(r.canaryEmbedded)
			.arg(r.canarySend)
			.arg(r.canaryRecv)
			.arg(r.canaryShortExpected)
			.arg(r.canaryToken)
			.arg(r.canaryTokenExpected)
			.arg(Summary(r)));
}

void SelfEmbedded(const std::shared_ptr<SelfState> &state) {
	const auto what = u"secrecy self-test: an embedded short secret is not "
		"counted as bounded"_q;
	auto fixture = CleanFixture();
	auto &part = fixture.parts[u"log_10_00.txt"_q];
	part.push_back(Entry(kSelfFiller + u" x"_q + kSelfPassword + u"x"_q));
	part.push_back(Entry(u"id12"_q + kSelfPassword + u"34"_q));
	const auto run = RunFixture(fixture);
	CheckPrepared(what, run);
	Keep(state, run);
	const auto &c = ClassOf(run.reading, SecrecyClass::DebugLog);
	Check(
		run.prepared
			&& (c.embedded >= 2)
			&& (c.bounded == 0)
			&& run.reading.decided
			&& run.reading.clean,
		what,
		Counts(c) + u" | "_q + Summary(run.reading));
}

void SelfRecv(const std::shared_ptr<SelfState> &state) {
	const auto what = u"secrecy self-test: a bounded short secret in a Recv "
		"entry is reported at its site and does not decide"_q;
	auto fixture = CleanFixture();
	auto &part = fixture.parts[u"mtp_10_00.txt"_q];
	const auto planted = part.back();
	part.pop_back();
	part += RecvEntry({
		u"    topics: [ vector<0x0>"_q,
		u"      { forumTopic"_q,
		u"        title: \""_q + kSelfPassword + u"\" [STRING]"_q,
		u"      }"_q,
		u"    ]"_q,
	});
	part.push_back(planted);
	const auto run = RunFixture(fixture);
	CheckPrepared(what, run);
	Keep(state, run);
	const auto &c = ClassOf(run.reading, SecrecyClass::MtpLog);
	const auto site = u"messages_forumTopics/forumTopic.title"_q;
	Check(
		run.prepared
			&& (c.boundedRecv == 1)
			&& (c.recvSites.size() == 1)
			&& c.recvSites.contains(site)
			&& (c.boundedSend == 0)
			&& run.reading.decided
			&& run.reading.clean,
		what,
		Counts(c)
			+ u" recvSites=["_q
			+ SitesText(c.recvSites)
			+ u"] | "_q
			+ Summary(run.reading));
}

void SelfClientWritten(const std::shared_ptr<SelfState> &state) {
	struct Leak {
		QString name;
		SecrecyClass cls;
		Fn<void(SelfFixture&)> plant;
		Fn<int(const SecrecyClassReading&)> counter;
		int expected = 1;
	};
	const auto pw = kSelfPassword;
	const auto a = SelfPhraseA();
	const auto b = SelfPhraseB();
	const auto leaks = std::vector<Leak>{
		{
			u"a bounded short secret in a Send entry"_q,
			SecrecyClass::MtpLog,
			[=](SelfFixture &f) {
				InsertBeforePlanted(f.parts[u"mtp_10_00.txt"_q], SendEntry({
					u"    query: { account_password"_q,
					u"      hint: \""_q + pw + u"\" [STRING]"_q,
					u"    }"_q,
				}));
			},
			[](const SecrecyClassReading &c) { return c.boundedSend; },
		},
		{
			u"a bounded short secret in a plain log_ line"_q,
			SecrecyClass::DebugLog,
			[=](SelfFixture &f) {
				f.parts[u"log_10_00.txt"_q].push_back(
					Entry(u"value: "_q + pw));
			},
			[](const SecrecyClassReading &c) { return c.boundedOther; },
		},
		{
			u"a word run in the app log"_q,
			SecrecyClass::AppLog,
			[=](SelfFixture &f) {
				f.appLog.push_back(u"[2026.01.15 10:05:02] words: "_q
					+ a[0]
					+ QChar(' ')
					+ a[1]);
			},
			[](const SecrecyClassReading &c) { return c.wordRunsOther; },
		},
		{
			u"a word run in a Send entry"_q,
			SecrecyClass::MtpLog,
			[=](SelfFixture &f) {
				InsertBeforePlanted(f.parts[u"mtp_10_00.txt"_q], SendEntry({
					u"    query: { messages_sendMessage"_q,
					u"      message: \""_q
						+ a[3]
						+ QChar(' ')
						+ a[4]
						+ u"\" [STRING]"_q,
					u"    }"_q,
				}));
			},
			[](const SecrecyClassReading &c) { return c.wordRunsSend; },
		},
		{
			u"a token in a plain line"_q,
			SecrecyClass::DebugLog,
			[=](SelfFixture &f) {
				f.parts[u"log_10_00.txt"_q].push_back(
					Entry(u"ref="_q + kSelfToken));
			},
			[](const SecrecyClassReading &c) { return c.tokensOther; },
		},
		{
			u"newline-joined words in a multi-line app log entry"_q,
			SecrecyClass::AppLog,
			[=](SelfFixture &f) {
				f.appLog += QStringList{
					u"[2026.01.15 10:05:02] words:"_q,
					a[0],
					a[1],
				};
			},
			[](const SecrecyClassReading &c) { return c.wordRunsOther; },
		},
		{
			u"a newline-joined phrase through Note into test_log"_q,
			SecrecyClass::TestLog,
			[=](SelfFixture &f) {
				// test_log.cpp OneLine writes each line break as \u000A.
				auto joined = QStringList(b.begin(), b.end());
				f.testLog.push_back(u"NOTE: phrase: "_q
					+ joined.join(u"\\u000A"_q));
			},
			[](const SecrecyClassReading &c) { return c.wordRunsOther; },
			int(b.size()) - 1,
		},
		{
			u"a comma-joined pair"_q,
			SecrecyClass::DebugLog,
			[=](SelfFixture &f) {
				f.parts[u"log_10_00.txt"_q].push_back(
					Entry(u"words=["_q + a[8] + QChar(',') + a[9] + u"]"_q));
			},
			[](const SecrecyClassReading &c) { return c.wordRunsOther; },
		},
		{
			u"words joined by an escaped line break in a Send string"_q,
			SecrecyClass::MtpLog,
			[=](SelfFixture &f) {
				InsertBeforePlanted(f.parts[u"mtp_10_00.txt"_q], SendEntry({
					u"    query: { messages_sendMessage"_q,
					u"      message: \""_q
						+ DumpEscaped(a[10] + QChar('\n') + a[11])
						+ u"\" [STRING]"_q,
					u"    }"_q,
				}));
			},
			[](const SecrecyClassReading &c) { return c.wordRunsSend; },
		},
		{
			u"a vector of words dumped one element per line in a Send "
				"entry"_q,
			SecrecyClass::MtpLog,
			[=](SelfFixture &f) {
				InsertBeforePlanted(f.parts[u"mtp_10_00.txt"_q], SendEntry({
					u"    query: { messages_sendMessage"_q,
					u"      entities: [ vector<0x1cb5c415> (2)"_q,
					u"        \""_q + a[1] + u"\" [STRING],"_q,
					u"        \""_q + a[2] + u"\" [STRING],"_q,
					u"      ]"_q,
					u"    }"_q,
				}));
			},
			[](const SecrecyClassReading &c) { return c.wordRunsSend; },
		},
	};
	for (const auto &leak : leaks) {
		const auto what = u"secrecy self-test: "_q
			+ leak.name
			+ u" fails the scan"_q;
		auto fixture = CleanFixture();
		leak.plant(fixture);
		const auto run = RunFixture(fixture);
		CheckPrepared(what, run);
		Keep(state, run);
		const auto &c = ClassOf(run.reading, leak.cls);
		Check(
			run.prepared
				&& run.reading.decided
				&& !run.reading.clean
				&& (leak.counter(c) == leak.expected)
				&& (c.clientWritten() == leak.expected)
				&& (run.reading.clientWritten() == leak.expected),
			what,
			Counts(c) + u" | "_q + Summary(run.reading));
	}
}

// The plain-line site report and declared computed fields, over the one
// synthetic head and its declared "digest" field.
void SelfSites(const std::shared_ptr<SelfState> &state) {
	using Sites = std::map<QString, int>;
	const auto pw = kSelfPassword;
	const auto a = SelfPhraseA();
	const auto head = u"|"_q + kSelfComputedHead + u"|"_q;
	const auto part = u"DebugLogs/log_10_00.txt"_q;
	const auto appSite = u"log.txt"_q + head + kSelfComputedField;
	const auto partSite = part + head + kSelfComputedField;
	const auto runKept = [&](const QString &what, const SelfFixture &fixture) {
		const auto result = RunFixture(fixture);
		CheckPrepared(what, result);
		Keep(state, result);
		return result;
	};
	const auto tail = [](const SecrecyReading &r) {
		return u" | "_q
			+ Summary(r)
			+ u" computed="_q
			+ QString::number(r.computed());
	};
	{
		const auto what = u"secrecy self-test: a bounded short secret that "
			"is an undeclared field's value fails the scan, and its site row "
			"names the class, the file, the message head and the field"_q;
		const auto result = runKept(what, MirroredFixture(false));
		const auto &r = result.reading;
		const auto &app = ClassOf(r, SecrecyClass::AppLog);
		const auto &debug = ClassOf(r, SecrecyClass::DebugLog);
		const auto rows = SecrecyRows(r).join(QChar('\n'));
		Check(
			result.prepared
				&& r.decided
				&& !r.clean
				&& (r.clientWritten() == 2)
				&& (r.computed() == 0)
				&& (app.boundedOther == 1)
				&& (app.plainSites == Sites{ { appSite, 1 } })
				&& (debug.boundedOther == 1)
				&& (debug.plainSites == Sites{ { partSite, 1 } })
				&& rows.contains(u"plainSites=["_q + appSite + u" x1]"_q)
				&& rows.contains(u"plainSites=["_q + partSite + u" x1]"_q),
			what,
			SiteDetails(app) + u" | "_q + SiteDetails(debug) + tail(r));
	}
	{
		const auto what = u"secrecy self-test: the same value as a declared "
			"computed field's value is reported and does not decide, so a "
			"scan whose only hit is that one is clean"_q;
		const auto result = runKept(what, MirroredFixture(true));
		const auto &r = result.reading;
		const auto &app = ClassOf(r, SecrecyClass::AppLog);
		const auto &debug = ClassOf(r, SecrecyClass::DebugLog);
		const auto reported = [](
				const SecrecyClassReading &c,
				const QString &site) {
			return (c.boundedOther == 0)
				&& (c.boundedComputed == 1)
				&& c.plainSites.empty()
				&& (c.computedSites == Sites{ { site, 1 } });
		};
		const auto row = u"SECRECY_COMPUTED: total=2 sites=[AppLog|"_q
			+ appSite
			+ u" x1, DebugLog|"_q
			+ partSite
			+ u" x1] (reported, non-deciding)"_q;
		Check(
			result.prepared
				&& r.decided
				&& r.clean
				&& (r.clientWritten() == 0)
				&& (r.computed() == 2)
				&& reported(app, appSite)
				&& reported(debug, partSite)
				&& SecrecyRows(r).contains(row),
			what,
			SiteDetails(app) + u" | "_q + SiteDetails(debug) + tail(r));
	}
	{
		const auto what = u"secrecy self-test: on the declared line the same "
			"value in another, undeclared field, in free text or in part of "
			"the declared field's value still fails the scan"_q;
		auto fixture = CleanFixture();
		fixture.computed.push_back(SelfDeclaration());
		fixture.parts[u"log_10_00.txt"_q] += QStringList{
			Entry(ComputedLine(pw, pw)),
			Entry(ComputedLine(u"17"_q, pw, u" retry "_q + pw)),
			Entry(ComputedLine(u"17"_q, u"v-"_q + pw)),
		};
		const auto result = runKept(what, fixture);
		const auto &r = result.reading;
		const auto &debug = ClassOf(r, SecrecyClass::DebugLog);
		const auto plain = Sites{
			{ part + head + kSiteNoField, 2 },
			{ part + head + u"span"_q, 1 },
		};
		Check(
			result.prepared
				&& r.decided
				&& !r.clean
				&& (r.clientWritten() == 3)
				&& (r.computed() == 2)
				&& (debug.plainSites == plain)
				&& (debug.computedSites == Sites{ { partSite, 2 } }),
			what,
			SiteDetails(debug) + tail(r));
	}
	{
		const auto what = u"secrecy self-test: a site that would print the "
			"hit or another secret, or a line with no field, is withheld and "
			"still fails the scan"_q;
		const auto result = runKept(what, SitesFixture());
		const auto &r = result.reading;
		const auto &app = ClassOf(r, SecrecyClass::AppLog);
		const auto &debug = ClassOf(r, SecrecyClass::DebugLog);
		const auto plain = Sites{
			{ part + u"|"_q + kSiteWithheld, 6 },
			{ part + head + kSiteNoField, 1 },
		};
		const auto leaks = SelfLeaksIn(SecrecyRows(r));
		Check(
			result.prepared
				&& r.decided
				&& !r.clean
				&& (r.computed() == 2)
				&& (r.clientWritten() == 9)
				&& (debug.boundedOther == 7)
				&& (debug.boundedComputed == 1)
				&& (debug.wordRunsOther == 1)
				&& (debug.tokensOther == 1)
				&& (debug.plainSites == plain)
				&& leaks.isEmpty(),
			what,
			SiteDetails(app)
				+ u" | "_q
				+ SiteDetails(debug)
				+ tail(r)
				+ u" hits=["_q
				+ leaks.join(u", "_q)
				+ u"]"_q);
	}
	struct Exempt {
		QString name;
		SecrecyClass cls = SecrecyClass::TestLog;
		Fn<void(SelfFixture&)> plant;
		Fn<int(const SecrecyClassReading&)> counter;
		QString sendSite; // the Send site the hit must be reported at
	};
	const auto exempts = std::vector<Exempt>{
		{
			u"word run"_q,
			SecrecyClass::DebugLog,
			[=](SelfFixture &f) {
				f.parts[u"log_10_00.txt"_q].push_back(Entry(ComputedLine(
					u"17"_q,
					a[4] + QChar(',') + a[5])));
			},
			[](const SecrecyClassReading &c) { return c.wordRunsOther; },
		},
		{
			u"token"_q,
			SecrecyClass::DebugLog,
			[=](SelfFixture &f) {
				f.parts[u"log_10_00.txt"_q].push_back(
					Entry(ComputedLine(u"17"_q, kSelfToken)));
			},
			[](const SecrecyClassReading &c) { return c.tokensOther; },
		},
		{
			u"Send-entry hit"_q,
			SecrecyClass::MtpLog,
			[=](SelfFixture &f) {
				InsertBeforePlanted(f.parts[u"mtp_10_00.txt"_q], SendEntry({
					u"    query: { messages_sendMessage"_q,
					u"      message: \""_q
						+ ComputedLine(u"17"_q, pw)
						+ u"\" [STRING]"_q,
					u"    }"_q,
				}));
			},
			[](const SecrecyClassReading &c) { return c.boundedSend; },
			u"messages_sendMessage/messages_sendMessage.message"_q,
		},
	};
	for (const auto &entry : exempts) {
		const auto what = u"secrecy self-test: a declaration exempts no "_q
			+ entry.name
			+ u", even in the declared field"_q;
		auto fixture = CleanFixture();
		fixture.computed.push_back(SelfDeclaration());
		entry.plant(fixture);
		const auto result = runKept(what, fixture);
		const auto &r = result.reading;
		const auto &c = ClassOf(r, entry.cls);
		Check(
			result.prepared
				&& r.decided
				&& !r.clean
				&& (r.clientWritten() == 1)
				&& (r.computed() == 0)
				&& (entry.counter(c) == 1)
				&& (entry.sendSite.isEmpty()
					|| c.sendSites.contains(entry.sendSite)),
			what,
			SiteDetails(c) + tail(r));
	}
}

void SelfUndecided(const std::shared_ptr<SelfState> &state) {
	struct Case {
		QString name;
		std::optional<SecrecyClass> cls; // nullopt: a scan-level reason
		QString reason;
		Fn<void(SelfFixture&)> change;
	};
	const auto cases = std::vector<Case>{
		{
			u"no test log file"_q,
			SecrecyClass::TestLog,
			u"no accepted file"_q,
			[](SelfFixture &f) { f.testLogFile = false; },
		},
		{
			u"an app log without the planted control"_q,
			SecrecyClass::AppLog,
			u"no planted control"_q,
			[](SelfFixture &f) { f.appLog.pop_back(); },
		},
		{
			u"an mtp_ part with Send headers only"_q,
			SecrecyClass::MtpLog,
			u"both directions"_q,
			[](SelfFixture &f) {
				auto mtp = QStringList{ kSelfToday };
				mtp += SendEntry({
					u"    query: { wallet_getState"_q,
					u"    }"_q,
				});
				mtp.push_back(MtpEntry(PlantedEntry()));
				f.parts[u"mtp_10_00.txt"_q] = mtp;
			},
		},
		{
			u"no secrets at all"_q,
			std::nullopt,
			u"no secret given"_q,
			[](SelfFixture &f) { f.secrets = SecrecySecrets(); },
		},
		{
			u"a 10-character token"_q,
			std::nullopt,
			u"shorter than 16 characters"_q,
			[](SelfFixture &f) { f.secrets.tokens.push_back(kSelfShortToken); },
		},
		{
			u"an earlier part with a bad name"_q,
			SecrecyClass::EarlierParts,
			u"invalid part name"_q,
			[](SelfFixture &f) {
				f.earlier.push_back({
					.name = u"log_9_00.txt"_q,
					.day = kSelfDay,
					.control = kSelfEarlierControl,
				});
			},
		},
		{
			u"an earlier part that is missing"_q,
			SecrecyClass::EarlierParts,
			u"missing part"_q,
			[](SelfFixture &f) {
				f.earlier.push_back({
					.name = u"log_08_45.txt"_q,
					.day = kSelfDay,
					.control = kSelfEarlierControl,
				});
			},
		},
	};
	for (const auto &entry : cases) {
		const auto what = u"secrecy self-test: "_q
			+ entry.name
			+ u" is refused as undecided"_q;
		auto fixture = CleanFixture();
		entry.change(fixture);
		const auto run = RunFixture(fixture);
		CheckPrepared(what, run);
		Keep(state, run);
		const auto &r = run.reading;
		auto named = false;
		auto details = QString();
		if (entry.cls) {
			const auto &c = ClassOf(r, *entry.cls);
			named = !c.decided && c.undecidedReason.contains(entry.reason);
			details = Counts(c);
		} else {
			named = r.undecidedReasons.join(u"; "_q).contains(entry.reason);
		}
		Check(
			run.prepared && !r.decided && !r.clean && named,
			what,
			details + u" | "_q + Summary(r));
	}
}

[[nodiscard]] QStringList LeakLines() {
	const auto a = SelfPhraseA();
	return {
		Entry(u"leak: "_q + kSelfPassword),
		Entry(u"ref="_q + kSelfToken),
		Entry(u"words: "_q + a[6] + QChar(' ') + a[7]),
	};
}

[[nodiscard]] SelfFixture IdentityFixture(
		bool nextQuarterToday,
		bool withBanner) {
	auto result = CleanFixture();
	auto appended = QStringList{ kSelfToday };
	appended += LeakLines();
	if (withBanner) {
		appended += QStringList{ kSelfBannerRule, kBanner, kSelfBannerRule };
	}
	appended.push_back(Entry(kSelfFiller + u" debug line"_q));
	appended.push_back(Entry(PlantedEntry()));
	result.parts[u"log_10_00.txt"_q] = appended;

	const auto nextDay = nextQuarterToday ? kSelfToday : kSelfYesterday;
	result.parts[u"log_10_15.txt"_q] = QStringList{ nextDay } + LeakLines();
	auto mtp = QStringList{ nextDay };
	mtp += SendEntry({
		u"    query: { account_password"_q,
		u"      hint: \""_q + kSelfPassword + u"\" [STRING]"_q,
		u"    }"_q,
	});
	result.parts[u"mtp_10_15.txt"_q] = mtp;

	result.parts[u"log_11_00.txt"_q] = QStringList{ kSelfToday } + LeakLines();
	return result;
}

void SelfIdentity(const std::shared_ptr<SelfState> &state) {
	{
		const auto what = u"secrecy self-test: a same-name part of another "
			"day is foreign and not scanned, a part outside the window is "
			"no candidate, and an appended part is read from its last "
			"banner"_q;
		const auto run = RunFixture(IdentityFixture(false, true));
		CheckPrepared(what, run);
		Keep(state, run);
		const auto &r = run.reading;
		const auto &debug = ClassOf(r, SecrecyClass::DebugLog);
		const auto &mtp = ClassOf(r, SecrecyClass::MtpLog);
		const auto names = (debug.names + mtp.names).join(u", "_q);
		const auto sliced = (debug.slicedFrom.size() == 1)
			&& (debug.slicedFrom.front() > 1);
		Check(
			run.prepared
				&& (r.candidates == 4)
				&& (debug.foreign == 1)
				&& (mtp.foreign == 1)
				&& (debug.files == 1)
				&& (mtp.files == 1)
				&& !names.contains(u"11_00"_q)
				&& r.candidateIdentities.contains(
					u"DebugLogs/log_10_15.txt=foreign"_q)
				&& sliced
				&& r.decided
				&& r.clean,
			what,
			u"candidates=%1 identities=[%2] | %3 | %4 | %5"_q
				.arg(r.candidates)
				.arg(r.candidateIdentities.join(u", "_q))
				.arg(Counts(debug))
				.arg(Counts(mtp))
				.arg(Summary(r)));
	}
	{
		const auto what = u"secrecy self-test (control): the same next "
			"quarter parts carrying this day's index are scanned and their "
			"leaks count"_q;
		const auto run = RunFixture(IdentityFixture(true, true));
		CheckPrepared(what, run);
		Keep(state, run);
		const auto &r = run.reading;
		const auto &debug = ClassOf(r, SecrecyClass::DebugLog);
		const auto &mtp = ClassOf(r, SecrecyClass::MtpLog);
		Check(
			run.prepared
				&& (debug.files == 2)
				&& (debug.clientWritten() == 3)
				&& (mtp.boundedSend == 1)
				&& !r.clean,
			what,
			Counts(debug) + u" | "_q + Counts(mtp) + u" | "_q + Summary(r));
	}
	{
		const auto what = u"secrecy self-test (control): the appended part "
			"without a banner is scanned whole and its earlier lines "
			"count"_q;
		const auto run = RunFixture(IdentityFixture(false, false));
		CheckPrepared(what, run);
		Keep(state, run);
		const auto &r = run.reading;
		const auto &debug = ClassOf(r, SecrecyClass::DebugLog);
		Check(
			run.prepared
				&& (debug.slicedFrom.size() == 1)
				&& (debug.slicedFrom.front() == 0)
				&& (debug.clientWritten() == 3)
				&& !r.clean,
			what,
			Counts(debug) + u" | "_q + Summary(r));
	}
	const auto earlierAccepted = SecrecyEarlierPart{
		.name = u"log_09_00.txt"_q,
		.day = kSelfDay,
		.control = kSelfEarlierControl,
	};
	const auto earlierFixture = [&](bool withForeign) {
		auto result = CleanFixture();
		result.parts[u"log_09_00.txt"_q] = QStringList{
			kSelfToday,
			Entry(kSelfFiller + u" earlier launch"_q),
			Entry(kSelfEarlierControl),
		};
		result.earlier.push_back(earlierAccepted);
		if (withForeign) {
			result.parts[u"log_09_15.txt"_q] = QStringList{ kSelfYesterday }
				+ LeakLines();
			result.earlier.push_back({
				.name = u"log_09_15.txt"_q,
				.day = kSelfDay,
				.control = kSelfEarlierControl,
			});
		}
		return result;
	};
	{
		const auto what = u"secrecy self-test: a named earlier part with its "
			"day's index and its own control is accepted"_q;
		const auto run = RunFixture(earlierFixture(false));
		CheckPrepared(what, run);
		Keep(state, run);
		const auto &c = ClassOf(run.reading, SecrecyClass::EarlierParts);
		Check(
			run.prepared
				&& (run.reading.classes.size() == 5)
				&& (c.files == 1)
				&& (c.controlHits == 1)
				&& c.decided
				&& run.reading.decided
				&& run.reading.clean,
			what,
			Counts(c) + u" | "_q + Summary(run.reading));
	}
	{
		const auto what = u"secrecy self-test: a named earlier part of "
			"another day is foreign and makes the class undecided"_q;
		const auto run = RunFixture(earlierFixture(true));
		CheckPrepared(what, run);
		Keep(state, run);
		const auto &c = ClassOf(run.reading, SecrecyClass::EarlierParts);
		Check(
			run.prepared
				&& (c.files == 1)
				&& (c.foreign == 1)
				&& !c.decided
				&& c.undecidedReason.contains(u"foreign part"_q)
				&& (c.clientWritten() == 0)
				&& !run.reading.decided,
			what,
			Counts(c) + u" | "_q + Summary(run.reading));
	}
}

void SelfPrintsNothing(const std::shared_ptr<SelfState> &state) {
	auto rows = QStringList();
	auto readings = 0;
	for (const auto &reading : state->readings) {
		rows += SecrecyRows(reading);
		++readings;
	}
	const auto memoryLeaks = SelfLeaksIn(rows);
	// The leak check must have examined rows that do print sites: the
	// computed row, the withheld sites and a printed head with "-".
	const auto joined = rows.join(QChar('\n'));
	const auto computedRow = u"SECRECY_COMPUTED: total=2 "
		"sites=[AppLog|log.txt|"_q;
	const auto sitesPrinted = joined.contains(computedRow)
		&& joined.contains(u"|?|? x6"_q)
		&& joined.contains(u"|"_q + kSelfComputedHead + u"|- x1"_q);
	Check(
		(readings >= 15)
			&& !rows.isEmpty()
			&& memoryLeaks.isEmpty()
			&& sitesPrinted,
		u"secrecy self-test: no row of a clean, dirty or undecided reading "
		"carries a synthetic secret, word, pair, token or scanned line"_q,
		u"readings=%1 rows=%2 sitesPrinted=%3 hits=[%4]"_q
			.arg(readings)
			.arg(rows.size())
			.arg(B(sitesPrinted))
			.arg(memoryLeaks.join(u", "_q)));

	const auto prepared = u"secrecy self-test: rows written to the test log"_q;
	const auto run = RunFixture(CleanFixture());
	CheckPrepared(prepared, run);
	const auto sites = RunFixture(SitesFixture());
	CheckPrepared(prepared, sites);
	const auto path = TestLogPath();
	const auto mark = QFileInfo(path).size();
	auto written = SecrecyRows(run.reading);
	written += SecrecyRows(sites.reading);
	for (const auto &row : written) {
		Note(row);
	}
	const auto appended = AppendedSince(path, mark);
	const auto fileLeaks = SelfLeaksIn({ appended });
	Check(
		run.prepared
			&& sites.prepared
			&& !appended.isEmpty()
			&& appended.contains(u"SECRECY_SCAN:"_q)
			&& appended.contains(u"SECRECY_COMPUTED: total=2 "_q)
			&& appended.contains(u"|?|? x6"_q)
			&& fileLeaks.isEmpty(),
		u"secrecy self-test: the rows as written to test_log.txt carry no "
		"synthetic secret either"_q,
		u"appendedBytes=%1 rows=%2 hits=[%3]"_q
			.arg(appended.toUtf8().size())
			.arg(written.size())
			.arg(fileLeaks.join(u", "_q)));
}

} // namespace

QString SecrecyClassName(SecrecyClass value) {
	switch (value) {
	case SecrecyClass::TestLog: return u"TestLog"_q;
	case SecrecyClass::AppLog: return u"AppLog"_q;
	case SecrecyClass::DebugLog: return u"DebugLog"_q;
	case SecrecyClass::MtpLog: return u"MtpLog"_q;
	case SecrecyClass::EarlierParts: return u"EarlierParts"_q;
	}
	return u"?"_q;
}

QString PartIdentityName(PartIdentity value) {
	switch (value) {
	case PartIdentity::Missing: return u"missing"_q;
	case PartIdentity::Unreadable: return u"unreadable"_q;
	case PartIdentity::Foreign: return u"foreign"_q;
	case PartIdentity::Accepted: return u"accepted"_q;
	}
	return u"?"_q;
}

int SecrecyClassReading::clientWritten() const {
	return wordRunsOther
		+ wordRunsSend
		+ boundedOther
		+ boundedSend
		+ tokensOther
		+ tokensSend;
}

int SecrecyClassReading::received() const {
	return wordRunsRecv + boundedRecv + tokensRecv;
}

const SecrecyClassReading *SecrecyReading::find(SecrecyClass cls) const {
	for (const auto &reading : classes) {
		if (reading.cls == cls) {
			return &reading;
		}
	}
	return nullptr;
}

int SecrecyReading::clientWritten() const {
	auto result = 0;
	for (const auto &reading : classes) {
		result += reading.clientWritten();
	}
	return result;
}

int SecrecyReading::received() const {
	auto result = 0;
	for (const auto &reading : classes) {
		result += reading.received();
	}
	return result;
}

int SecrecyReading::computed() const {
	auto result = 0;
	for (const auto &reading : classes) {
		result += reading.boundedComputed;
	}
	return result;
}

SecrecyLaunchLogs SelectLaunchLogs(
		const QString &workingDir,
		const QString &evidenceDir,
		const QString &appLogText,
		const QDateTime &launchStart,
		const QDateTime &scanAt,
		const std::vector<SecrecyEarlierPart> &earlier) {
	auto result = SecrecyLaunchLogs{
		.launchStart = launchStart,
		.scanAt = scanAt,
	};
	{
		const auto path = evidenceDir + u"test_log.txt"_q;
		result.sources.push_back({
			.cls = SecrecyClass::TestLog,
			.name = u"test_log.txt"_q,
			.path = path,
			.identity = (QFile::exists(path)
				? PartIdentity::Accepted
				: PartIdentity::Missing),
		});
	}
	result.sources.push_back({
		.cls = SecrecyClass::AppLog,
		.name = u"log.txt"_q,
		.text = appLogText,
		.fromText = true,
		.identity = (appLogText.isEmpty()
			? PartIdentity::Missing
			: PartIdentity::Accepted),
	});
	if (launchStart.isValid() && scanAt.isValid()) {
		const auto time = launchStart.time();
		auto part = QDateTime(
			launchStart.date(),
			QTime(time.hour(), (time.minute() / kPartMinutes) * kPartMinutes));
		for (auto step = 0
			; (part <= scanAt) && (step < kMaxWindowSteps)
			; ++step, part = part.addSecs(kPartMinutes * 60)) {
			const auto postfix = u"_%1_%2.txt"_q
				.arg(part.time().hour(), 2, 10, QChar('0'))
				.arg(part.time().minute(), 2, 10, QChar('0'));
			const auto day = part.date().toString(u"yyyyMMdd"_q);
			for (const auto &[kind, cls] : {
				std::pair{ u"log"_q, SecrecyClass::DebugLog },
				std::pair{ u"mtp"_q, SecrecyClass::MtpLog },
			}) {
				const auto name = u"DebugLogs/"_q + kind + postfix;
				const auto path = workingDir + name;
				result.sources.push_back({
					.cls = cls,
					.name = name,
					.path = path,
					.sliceAtLastBanner = true,
					.identity = ReadPartIdentity(path, day),
				});
				++result.candidates;
			}
		}
	}
	for (const auto &part : earlier) {
		auto source = SecrecySource{
			.cls = SecrecyClass::EarlierParts,
			.control = part.control,
		};
		if (!ValidPartName(part.name) || !part.day.isValid()) {
			// The refused name is the caller's; it is not echoed.
			source.name = u"DebugLogs/(invalid name)"_q;
			source.invalidName = true;
			source.identity = PartIdentity::Missing;
		} else {
			source.name = u"DebugLogs/"_q + part.name;
			source.path = workingDir + source.name;
			source.identity = ReadPartIdentity(
				source.path,
				part.day.toString(u"yyyyMMdd"_q));
		}
		result.sources.push_back(std::move(source));
	}
	return result;
}

SecrecyLaunchLogs SelectThisLaunchLogs(
		const std::vector<SecrecyEarlierPart> &earlier) {
	// crl::now() counts from lib_crl's static initializer (crl_time.cpp),
	// which runs before the logger starts, so now - crl::now() is the
	// process start. No last-write time takes part: on Windows a file the
	// process still holds open keeps its old one, which is how "modified
	// since the scenario started" selected none of this launch's logs.
	const auto now = QDateTime::currentDateTime();
	const auto launchStart = now.addMSecs(-crl::now());
	return SelectLaunchLogs(
		cWorkingDir(),
		EvidenceDir(),
		Logs::full(),
		launchStart,
		now,
		earlier);
}

SecrecyReading ReadSecrecy(
		const SecrecySecrets &secrets,
		const SecrecyLaunchLogs &logs,
		const QString &plantedControl,
		const std::vector<SecrecyComputedField> &computedFields) {
	auto result = SecrecyReading();
	result.launchStart = logs.launchStart;
	result.scanAt = logs.scanAt;
	result.candidates = logs.candidates;

	const auto usable = UsableSecrets(secrets, &result);
	const auto matcher = SecrecyMatcher(usable, computedFields);
	ReadCanaries(usable, result);

	auto order = std::vector<SecrecyClass>{
		SecrecyClass::TestLog,
		SecrecyClass::AppLog,
		SecrecyClass::DebugLog,
		SecrecyClass::MtpLog,
	};
	const auto hasEarlier = ranges::any_of(logs.sources, [](const auto &s) {
		return (s.cls == SecrecyClass::EarlierParts);
	});
	if (hasEarlier) {
		order.push_back(SecrecyClass::EarlierParts);
	}
	for (const auto cls : order) {
		auto reading = SecrecyClassReading{ .cls = cls };
		for (const auto &source : logs.sources) {
			if (source.cls != cls) {
				continue;
			}
			++reading.candidates;
			if (IsWindowClass(cls)) {
				result.candidateIdentities.push_back(source.name
					+ QChar('=')
					+ PartIdentityName(source.identity));
				switch (source.identity) {
				case PartIdentity::Missing: ++result.missing; break;
				case PartIdentity::Unreadable: ++result.unreadable; break;
				case PartIdentity::Foreign: ++result.foreign; break;
				case PartIdentity::Accepted: ++result.accepted; break;
				}
			}
			ReadSource(source, matcher, plantedControl, reading);
		}
		DecideClass(reading);
		if (!reading.decided) {
			result.undecidedClasses.push_back(SecrecyClassName(cls)
				+ u": "_q
				+ reading.undecidedReason);
		}
		result.classes.push_back(std::move(reading));
	}

	if (!result.phrasesGiven && !result.shortGiven && !result.tokensGiven) {
		result.undecidedReasons.push_back(u"no secret given"_q);
	}
	if (result.tokensTooShort) {
		result.undecidedReasons.push_back(u"%1 token(s) shorter than 16 "
			"characters: not a token; hand it as a short secret"_q
			.arg(result.tokensTooShort));
	}
	if (result.phrasesTooShort) {
		result.undecidedReasons.push_back(
			u"%1 phrase(s) shorter than 2 words"_q.arg(result.phrasesTooShort));
	}
	if (!result.canariesHold) {
		result.undecidedReasons.push_back(u"a canary failed"_q);
	}
	result.decided = result.undecidedReasons.isEmpty()
		&& result.undecidedClasses.isEmpty();
	result.clean = result.decided && !result.clientWritten();
	return result;
}

QStringList SecrecyRows(const SecrecyReading &reading) {
	auto result = QStringList();
	auto classes = QStringList();
	auto files = QStringList();
	for (const auto &c : reading.classes) {
		classes.push_back(SecrecyClassName(c.cls));
		files += c.names;
	}
	result.push_back(u"SECRECY_SCAN: decided=%1 clean=%2 classes=[%3] "
		"undecided=[%4] reasons=[%5] canaries=[wordRuns=%6/%7 bounded=%8/%9 "
		"embedded=%10/%9 send=%11/%9 recv=%12/%9 tokens=%13/%14 hold=%15] "
		"secrets=[phrases=%16 short=%17 tokens=%18 tokensTooShort=%19 "
		"phrasesTooShort=%20] window=[start=%21 scan=%22 candidates=%23 "
		"accepted=%24 foreign=%25 missing=%26 unreadable=%27] "
		"clientWritten=%28 received=%29 files=[%30]"_q
		.arg(B(reading.decided))
		.arg(B(reading.clean))
		.arg(classes.join(u", "_q))
		.arg(reading.undecidedClasses.join(u"; "_q))
		.arg(reading.undecidedReasons.join(u"; "_q))
		.arg(reading.canaryWordRuns)
		.arg(reading.canaryWordRunsExpected)
		.arg(reading.canaryBounded)
		.arg(reading.canaryShortExpected)
		.arg(reading.canaryEmbedded)
		.arg(reading.canarySend)
		.arg(reading.canaryRecv)
		.arg(reading.canaryToken)
		.arg(reading.canaryTokenExpected)
		.arg(B(reading.canariesHold))
		.arg(reading.phrasesGiven)
		.arg(reading.shortGiven)
		.arg(reading.tokensGiven)
		.arg(reading.tokensTooShort)
		.arg(reading.phrasesTooShort)
		.arg(TimeText(reading.launchStart))
		.arg(TimeText(reading.scanAt))
		.arg(reading.candidates)
		.arg(reading.accepted)
		.arg(reading.foreign)
		.arg(reading.missing)
		.arg(reading.unreadable)
		.arg(reading.clientWritten())
		.arg(reading.received())
		.arg(files.join(u", "_q)));
	result.push_back(u"SECRECY_WINDOW: candidates=["_q
		+ reading.candidateIdentities.join(u", "_q)
		+ u"]"_q);
	auto recvSites = std::map<QString, int>();
	auto computedSites = std::map<QString, int>();
	auto wordRunsRecv = 0;
	auto boundedRecv = 0;
	auto tokensRecv = 0;
	for (const auto &c : reading.classes) {
		result.push_back(ClassRow(c));
		for (const auto &[site, hits] : c.recvSites) {
			recvSites[site] += hits;
		}
		for (const auto &[site, hits] : c.computedSites) {
			computedSites[SecrecyClassName(c.cls) + QChar('|') + site] += hits;
		}
		wordRunsRecv += c.wordRunsRecv;
		boundedRecv += c.boundedRecv;
		tokensRecv += c.tokensRecv;
	}
	result.push_back(u"SECRECY_RECEIVED: total=%1 wordRunsRecv=%2 "
		"boundedRecv=%3 tokensRecv=%4 recvSites=[%5] (reported, "
		"non-deciding)"_q
		.arg(reading.received())
		.arg(wordRunsRecv)
		.arg(boundedRecv)
		.arg(tokensRecv)
		.arg(SitesText(recvSites)));
	result.push_back(u"SECRECY_COMPUTED: total=%1 sites=[%2] (reported, "
		"non-deciding)"_q
		.arg(reading.computed())
		.arg(SitesText(computedSites)));
	return result;
}

bool CheckSecrecy(const SecrecyScanArgs &args, const QString &what) {
	if (!Active()) {
		return false;
	}
	const auto nonce = u"secrecy-control-"_q
		+ QString::number(base::RandomValue<uint64>(), 16).rightJustified(
			16,
			QChar('0'));
	Note(u"SECRECY_CONTROL: "_q + nonce);
	LOG(("Test Info: secrecy control %1").arg(nonce));
	MTP_LOG(0, ("Test Info: secrecy control %1").arg(nonce));

	const auto path = TestLogPath();
	const auto mark = QFileInfo(path).size();
	auto logs = SelectThisLaunchLogs(args.earlierParts);
	for (auto &source : logs.sources) {
		source.control = [&] {
			switch (source.cls) {
			case SecrecyClass::TestLog: return args.testLogControl;
			case SecrecyClass::AppLog: return args.appLogControl;
			case SecrecyClass::DebugLog: return args.debugLogControl;
			case SecrecyClass::MtpLog: return args.mtpLogControl;
			case SecrecyClass::EarlierParts: return source.control;
			}
			return source.control;
		}();
	}
	const auto reading
		= ReadSecrecy(args.secrets, logs, nonce, args.computedFields);
	for (const auto &c : reading.classes) {
		ReportClass(c);
	}
	for (const auto &row : SecrecyRows(reading)) {
		Note(row);
	}
	Check(
		reading.decided,
		what + u": every class decided"_q,
		u"classes=%1 undecidedClasses=[%2] reasons=[%3] canariesHold=%4"_q
			.arg(reading.classes.size())
			.arg(reading.undecidedClasses.join(u"; "_q))
			.arg(reading.undecidedReasons.join(u"; "_q))
			.arg(B(reading.canariesHold)));
	Check(
		reading.clean,
		what + u": no client-written fixture secret"_q,
		u"clientWritten=%1 received=%2 computed=%3 decided=%4"_q
			.arg(reading.clientWritten())
			.arg(reading.received())
			.arg(reading.computed())
			.arg(B(reading.decided)));

	// The same matcher over exactly the bytes this call appended, raw, so
	// the scan's own rows are proven to carry no secret.
	const auto usable = UsableSecrets(args.secrets, nullptr);
	const auto matcher = SecrecyMatcher(usable);
	auto own = SecrecyClassReading();
	ScanText(AppendedSince(path, mark), matcher, {}, {}, own);
	Check(
		own.lines > 0 && !HitTotal(own),
		what + u": the scan's own rows carry no fixture secret"_q,
		u"lines=%1 wordRuns=%2 bounded=%3 embedded=%4 tokens=%5"_q
			.arg(own.lines)
			.arg(own.wordRunsOther + own.wordRunsSend + own.wordRunsRecv)
			.arg(own.bounded)
			.arg(own.embedded)
			.arg(own.tokensOther + own.tokensSend + own.tokensRecv));
	return reading.decided && reading.clean;
}

void AppendSecrecyScanSelfTest(not_null<Runner*> runner) {
	const auto state = std::make_shared<SelfState>();
	const auto stages = std::vector<std::pair<QString, Fn<void()>>>{
		{ u"secrecy_self_canaries"_q, [=] { SelfCanaries(state); } },
		{ u"secrecy_self_embedded"_q, [=] { SelfEmbedded(state); } },
		{ u"secrecy_self_recv"_q, [=] { SelfRecv(state); } },
		{
			u"secrecy_self_client_written"_q,
			[=] { SelfClientWritten(state); },
		},
		{ u"secrecy_self_sites"_q, [=] { SelfSites(state); } },
		{ u"secrecy_self_undecided"_q, [=] { SelfUndecided(state); } },
		{ u"secrecy_self_identity"_q, [=] { SelfIdentity(state); } },
		{
			u"secrecy_self_prints_nothing"_q,
			[=] { SelfPrintsNothing(state); },
		},
	};
	for (const auto &[name, then] : stages) {
		runner->add({
			.name = name,
			.then = then,
			.timeout = kDefaultStageTimeout,
		});
	}
}

} // namespace Test

#endif // _DEBUG
