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
#include "test/test_animation_clock.h"
#include "test/test_log.h"
#include "test/test_log_scan_internal.h"
#include "test/test_post_paint.h"
#include "test/test_probe.h"
#include "test/test_runner.h"
#include "logs.h"
#include "settings.h"

#include <QtCore/QTemporaryDir>

#include <cmath>
#include <limits>

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

// A number of a carried transport line: its printed name, and whether the
// product source shows the client computes its value itself. Every slot
// that is not computed decides like any other plain-line position.
struct TransportSlot {
	QString name;
	bool computed = false;
};

// A DEBUG_LOG line the scan carries: the basename of its source file,
// which base::LogWriteDebug appends as " (<file> : <line>)", and its
// format, copied verbatim with adjacent literals joined. Every placeholder
// is formatted from one integer, %1..%n in text order, one slot each.
struct TransportLine {
	QString file;
	QString format;
	std::vector<TransportSlot> slots;
};

[[nodiscard]] TransportSlot Computed(const QString &name) {
	return { .name = name, .computed = true };
}

[[nodiscard]] TransportSlot Deciding(const QString &name) {
	return { .name = name };
}

// The MTProto transport's own DEBUG_LOG lines whose numbers a bounded
// short-secret hit may be exactly, by coincidence, with no "name=" field to
// declare (the rule of
// 2026/10/13/keep-the-secrecy-scan-from-counting-a-coincidental-transport-number).
// Each format is the one in the named function of its file, and a slot is
// Computed only when that function formats it from a value the client
// computes or keeps itself: a size or count of what it holds or sends, its
// own clock, its routing state (a dc id, a connection state), a request id
// of its own maps, or an id it generated. Routing state may follow server
// configuration or a migrate error; it is still printed from the client's
// own members and maps, never straight from received bytes. A number read
// straight from the server's bytes - an acked or received msg_id, a server
// time, a server salt - stays Deciding even when the client echoes or
// stores it, and so does a slot glued to a letter, whose whole positive
// value is only ever an embedded hit. No slot is formatted from fixture
// input. A line whose literal text drifts from its format is simply not
// carried and keeps the withheld site.
[[nodiscard]] const std::vector<TransportLine> &TransportLines() {
	const auto privateFile = u"session_private.cpp"_q;
	const auto sessionFile = u"session.cpp"_q;
	const auto instanceFile = u"mtp_instance.cpp"_q;
	static const auto result = std::vector<TransportLine>{
		// SessionPrivate::clearOldContainers: the container's msg_id the
		// client assigned in tryToSend (prepareToSend), its crl::now() at
		// sending, crl::now() and base::unixtime::now().
		{
			.file = privateFile,
			.format = u"MTP Info: Removing old container with resending %1, "
				"sent: %2, now: %3, current unixtime: %4"_q,
			.slots = {
				Computed(u"resending"_q),
				Computed(u"sent"_q),
				Computed(u"now"_q),
				Computed(u"unixtime"_q),
			},
		},
		// SessionPrivate::changeSessionId: a base::RandomValue it draws.
		{
			.file = privateFile,
			.format = u"MTP Info: setting server_session: %1"_q,
			.slots = { Computed(u"server_session"_q) },
		},
		// SessionPrivate::tryToSend: _shiftedDcId, getState() and
		// _pingIdToSend, a base::RandomValue drawn in tryToSend.
		{
			.file = privateFile,
			.format = u"MTP Info: tryToSend for dc %1."_q,
			.slots = { Computed(u"dc"_q) },
		},
		{
			.file = privateFile,
			.format = u"MTP Info: not yet connected in dc %1."_q,
			.slots = { Computed(u"dc"_q) },
		},
		{
			.file = privateFile,
			.format = u"MTP Info: not yet with auth key in dc %1."_q,
			.slots = { Computed(u"dc"_q) },
		},
		{
			.file = privateFile,
			.format = u"MTP Info: dc %1 not sending, waiting for Connected "
				"state, state: %2"_q,
			.slots = { Computed(u"dc"_q), Computed(u"state"_q) },
		},
		{
			.file = privateFile,
			.format = u"MTP Info: sending ping, ping_id: %1"_q,
			.slots = { Computed(u"ping_id"_q) },
		},
		{
			.file = privateFile,
			.format = u"MTP Info: sending ping_delay_disconnect, "
				"ping_id: %1"_q,
			.slots = { Computed(u"ping_id"_q) },
		},
		{
			.file = privateFile,
			.format = u"MTP Info: dc %1 sending only service or bind."_q,
			.slots = { Computed(u"dc"_q) },
		},
		{
			.file = privateFile,
			.format = u"MTP Info: dc %1 trying to send after ping, "
				"state: %2"_q,
			.slots = { Computed(u"dc"_q), Computed(u"state"_q) },
		},
		// SessionPrivate::connectToServer: _shiftedDcId and the size of
		// its own _testConnections.
		{
			.file = privateFile,
			.format = u"MTP Info: DC %1 options not found, waiting for "
				"config"_q,
			.slots = { Computed(u"dc"_q) },
		},
		{
			.file = privateFile,
			.format = u"Connection Info: Connecting to %1 with %2 test "
				"connections."_q,
			.slots = { Computed(u"dc"_q), Computed(u"connections"_q) },
		},
		// SessionPrivate::onSentSome: the bytes it sent and the receive
		// delay it computes from them.
		{
			.file = privateFile,
			.format = u"Checking connect for request with size %1 bytes, "
				"delay will be %2"_q,
			.slots = { Computed(u"size"_q), Computed(u"delay"_q) },
		},
		// SessionPrivate::sendPingForce: _shiftedDcId.
		{
			.file = privateFile,
			.format = u"MTP Info: send ping force for dcWithShift %1."_q,
			.slots = { Computed(u"dcWithShift"_q) },
		},
		// SessionPrivate::handleReceived: the server time taken from the
		// received msg_id decides; base::unixtime::now() and the size of
		// its own received-messages queue are the client's.
		{
			.file = privateFile,
			.format = u"MTP Info: bad server time from msg_id: %1, my "
				"time: %2"_q,
			.slots = { Deciding(u"msg_id"_q), Computed(u"time"_q) },
		},
		{
			.file = privateFile,
			.format = u"MTP Info: queueTryToReceive() - need to parse in "
				"another thread, %1 messages."_q,
			.slots = { Computed(u"messages"_q) },
		},
		// SessionPrivate::requestsAcked (both sites of the first format):
		// msgId is an id of the acked list the server sent and decides; a
		// request id comes from the client's own maps (haveSent,
		// _resendingIds, toSend), and the last is a count of its _ackedIds.
		{
			.file = privateFile,
			.format = u"Message Info: ignoring ACK for msgId %1 because "
				"request %2 requires a response"_q,
			.slots = { Deciding(u"msgId"_q), Computed(u"request"_q) },
		},
		{
			.file = privateFile,
			.format = u"Message Info: msgId %1 was found in recent resent, "
				"requestId %2 was not found in prepared to send"_q,
			.slots = { Deciding(u"msgId"_q), Computed(u"requestId"_q) },
		},
		{
			.file = privateFile,
			.format = u"Message Error: for msgId %1 found resent request, "
				"requestId %2, contains requestId %3"_q,
			.slots = {
				Deciding(u"msgId"_q),
				Computed(u"requestId"_q),
				Computed(u"contains"_q),
			},
		},
		{
			.file = privateFile,
			.format = u"Message Info: acked msgId %1 that was prepared to "
				"resend, requestId %2"_q,
			.slots = { Deciding(u"msgId"_q), Computed(u"requestId"_q) },
		},
		{
			.file = privateFile,
			.format = u"Message Info: removing some old acked sent "
				"msgIds %1"_q,
			.slots = { Computed(u"msgIds"_q) },
		},
		// SessionPrivate::updateAuthKey: _shiftedDcId.
		{
			.file = privateFile,
			.format = u"AuthKey Info: Connection updating key from "
				"Session, dc %1"_q,
			.slots = { Computed(u"dc"_q) },
		},
		// SessionPrivate::setCurrentKeyId: the key id AuthKey::countKeyId
		// hashes from the key the client created (applyAuthKey).
		{
			.file = privateFile,
			.format = u"MTP Info: auth key id set to id %1"_q,
			.slots = { Computed(u"id"_q) },
		},
		// SessionPrivate::applyAuthKey: _shiftedDcId.
		{
			.file = privateFile,
			.format = u"MTP Info: auth_key id for dc %1 changed, "
				"restarting..."_q,
			.slots = { Computed(u"dc"_q) },
		},
		// SessionPrivate::tryAcquireKeyCreation: the ids of the keys the
		// client created; the server salts of the key exchange decide.
		{
			.file = privateFile,
			.format = u"AuthKey Info: unbound key creation succeed, ids: "
				"(%1, %2) server salts: (%3, %4)"_q,
			.slots = {
				Computed(u"temporaryKeyId"_q),
				Computed(u"persistentKeyId"_q),
				Deciding(u"temporarySalt"_q),
				Deciding(u"persistentSalt"_q),
			},
		},
		// SessionPrivate::sendSecureRequest: fullSize + 6, and the two
		// int32 halves of the msg_id the client assigned (prepareToSend,
		// or the bind request's own); the low half may be negative.
		{
			.file = privateFile,
			.format = u"MTP Info: sending request, size: %1, num: %2, "
				"time: %3"_q,
			.slots = {
				Computed(u"size"_q),
				Computed(u"num"_q),
				Computed(u"time"_q),
			},
		},
		// Session::watchDcKeyChanges, stop, kill and needToResumeAndSend:
		// _shiftedDcId.
		{
			.file = sessionFile,
			.format = u"AuthKey Info: dcTemporaryKeyChanged in Session %1"_q,
			.slots = { Computed(u"session"_q) },
		},
		{
			.file = sessionFile,
			.format = u"AuthKey Info: calling Connection::updateAuthKey in "
				"Session %1"_q,
			.slots = { Computed(u"session"_q) },
		},
		{
			.file = sessionFile,
			.format = u"Session Info: stopping session dcWithShift %1"_q,
			.slots = { Computed(u"dcWithShift"_q) },
		},
		{
			.file = sessionFile,
			.format = u"Session Info: marked session dcWithShift %1 as "
				"killed"_q,
			.slots = { Computed(u"dcWithShift"_q) },
		},
		{
			.file = sessionFile,
			.format = u"Session Info: resuming session dcWithShift %1"_q,
			.slots = { Computed(u"dcWithShift"_q) },
		},
		// Session::sendAnything: _shiftedDcId and _msSendCall, a crl::now();
		// _msWait is glued to its "ms".
		{
			.file = sessionFile,
			.format = u"MTP Info: dcWithShift %1 can wait for %2ms from "
				"current %3"_q,
			.slots = {
				Computed(u"dcWithShift"_q),
				Deciding(u"wait"_q),
				Computed(u"current"_q),
			},
		},
		{
			.file = sessionFile,
			.format = u"MTP Info: dcWithShift %1 stopped send timer, can "
				"wait for %2ms from current %3"_q,
			.slots = {
				Computed(u"dcWithShift"_q),
				Deciding(u"wait"_q),
				Computed(u"current"_q),
			},
		},
		// Session::sendPrepared: the delay its caller chose and the request
		// id Instance::Private::sendRequest assigned.
		{
			.file = sessionFile,
			.format = u"MTP Info: adding request to toSendMap, "
				"msCanWait %1"_q,
			.slots = { Computed(u"msCanWait"_q) },
		},
		{
			.file = sessionFile,
			.format = u"MTP Info: added, requestId %1"_q,
			.slots = { Computed(u"requestId"_q) },
		},
		// Session::releaseGenericKeyCreationOnDone and
		// notifyDcConnectionInited: _shiftedDcId.
		{
			.file = sessionFile,
			.format = u"AuthKey Info: Persistent key changed while binding "
				"temporary, dcWithShift %1"_q,
			.slots = { Computed(u"dcWithShift"_q) },
		},
		{
			.file = sessionFile,
			.format = u"AuthKey Info: Session key bound, setting, "
				"dcWithShift %1"_q,
			.slots = { Computed(u"dcWithShift"_q) },
		},
		{
			.file = sessionFile,
			.format = u"MTP Info: MTProtoDC::connectionWasInited(), "
				"dcWithShift %1"_q,
			.slots = { Computed(u"dcWithShift"_q) },
		},
		// Instance::Private::cancel, checkDelayedRequests and
		// unregisterRequest: a request id of the client's own maps.
		{
			.file = instanceFile,
			.format = u"MTP Info: Cancel request %1."_q,
			.slots = { Computed(u"request"_q) },
		},
		{
			.file = instanceFile,
			.format = u"MTP Error: could not find request %1"_q,
			.slots = { Computed(u"request"_q) },
		},
		{
			.file = instanceFile,
			.format = u"MTP Info: unregistering request %1."_q,
			.slots = { Computed(u"request"_q) },
		},
		// Instance::Private::dcPersistentKeyChanged: the Dcenter's id.
		{
			.file = instanceFile,
			.format = u"AuthKey Info: writing auth keys, called by dc %1"_q,
			.slots = { Computed(u"dc"_q) },
		},
		// Instance::Private::processCallback: Response::requestId, which
		// SessionPrivate::handleOneReceived takes from its own maps
		// (wasSent).
		{
			.file = instanceFile,
			.format = u"RPC Info: found parser for request %1, trying to "
				"parse response..."_q,
			.slots = { Computed(u"request"_q) },
		},
		{
			.file = instanceFile,
			.format = u"RPC Info: parser not found for %1"_q,
			.slots = { Computed(u"request"_q) },
		},
		// Instance::Private::importDone: the dc of its requests-by-dc map
		// and the waiting request's id.
		{
			.file = instanceFile,
			.format = u"MTP Info: auth import to dc %1 succeeded"_q,
			.slots = { Computed(u"dc"_q) },
		},
		{
			.file = instanceFile,
			.format = u"MTP Info: resending request %1 to dc %2 after "
				"import auth"_q,
			.slots = { Computed(u"request"_q), Computed(u"dc"_q) },
		},
		// Instance::Private::onErrorDefault: the request id and its dc from
		// the client's maps; both dc slots are glued, and the new dc is
		// parsed from the server's error type.
		{
			.file = instanceFile,
			.format = u"MTP Info: changing request %1 from dcWithShift%2 to "
				"dc%3"_q,
			.slots = {
				Computed(u"request"_q),
				Deciding(u"from"_q),
				Deciding(u"to"_q),
			},
		},
		{
			.file = instanceFile,
			.format = u"MTP Info: importing auth to dcWithShift %1"_q,
			.slots = { Computed(u"dcWithShift"_q) },
		},
	};
	return result;
}

// A carried line's public head: its format's text before the first
// placeholder, up to the first character HeadShaped refuses, less trailing
// " .:-/". It is a constant of the format, so it never holds a scanned
// value.
[[nodiscard]] QString TransportHead(const QString &format) {
	static const auto trailing = u" .:-/"_q;
	const auto size = int(format.size());
	auto till = 0;
	while (till < size
		&& format[till] != QChar('%')
		&& HeadShaped(QString(format[till]))) {
		++till;
	}
	while (till > 0 && trailing.contains(format[till - 1])) {
		--till;
	}
	return format.left(till);
}

// The digits a QString::arg of an integer prints, and nothing that
// QChar::isDigit adds.
[[nodiscard]] bool IsAsciiDigit(QChar ch) {
	return (ch.unicode() >= '0') && (ch.unicode() <= '9');
}

// Whether text[from, end) is exactly |line|'s message followed by
// " (<line.file> : <digits>)" and nothing else, each placeholder one
// integer: an optional "-" and at least one ASCII digit. Answers each
// integer's span [first, second), the minus included, in slot order, or
// nothing when the text is not that line or the format is not %1..%n in
// text order with one slot each. Nothing is normalized: whitespace, case,
// punctuation, signs and digits must be exactly as the product prints them.
[[nodiscard]] std::vector<std::pair<int, int>> TransportSpans(
		const TransportLine &line,
		const QString &text,
		int from) {
	const auto &format = line.format;
	const auto size = int(text.size());
	const auto length = int(format.size());
	auto result = std::vector<std::pair<int, int>>();
	auto at = from;
	for (auto i = 0; i < length; ++i) {
		if (format[i] != QChar('%')) {
			if (at >= size || text[at] != format[i]) {
				return {};
			}
			++at;
			continue;
		}
		const auto index = int(result.size()) + 1;
		if (index > 9
			|| i + 1 >= length
			|| format[i + 1].unicode() != '0' + index
			|| (i + 2 < length && IsAsciiDigit(format[i + 2]))) {
			return {};
		}
		++i;
		const auto start = at;
		if (at < size && text[at] == QChar('-')) {
			++at;
		}
		const auto digits = at;
		while (at < size && IsAsciiDigit(text[at])) {
			++at;
		}
		if (at == digits) {
			return {};
		}
		result.emplace_back(start, at);
	}
	const auto suffix = u" ("_q + line.file + u" : "_q;
	if (text.mid(at, suffix.size()) != suffix) {
		return {};
	}
	at += int(suffix.size());
	const auto digits = at;
	while (at < size && IsAsciiDigit(text[at])) {
		++at;
	}
	if (at == digits
		|| at + 1 != size
		|| text[at] != QChar(')')
		|| result.size() != line.slots.size()) {
		return {};
	}
	return result;
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
// exactly the value of a declared computed field or the whole integer of a
// computed slot of a carried transport line.
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
	[[nodiscard]] PlainHit transportHit(
		const QString &text,
		int from,
		int at,
		int till,
		const QString &file) const;
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
		const QString &file,
		Internal::LogScanObserver *observer,
		bool strict);

	void feed(const QString &line);

private:
	const SecrecyMatcher &_matcher;
	const QString _planted;
	const QString _control;
	const QString _file; // public name, printed in plain-line sites
	SecrecyClassReading &_reading;
	Internal::LogScanObserver *_observer = nullptr;
	bool _strict = true;
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
// value of a declared computed field, or the whole integer of a computed
// slot of a carried transport line (transportHit), is reported and does
// not decide.
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
// field (unless it is a carried transport line: transportHit), the hit lies
// in the head, the head is not HeadShaped, or the head (with the printed
// field name) holds any given secret. Only a hit whose printed head and
// field equal a declaration is computed, so a declaration never matches a
// withheld site.
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
		return transportHit(text, from, at, till, file);
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

// The site of a bounded hit over text[at, till) on a plain line with no
// "name=" field, |from| being where MessageFrom put its message. It stays
// the withheld "<file>|?|?" unless |file| is exactly a DebugLogs log_ part
// name, the line opens with exactly a writeDebug entry start (logs.cpp
// _logsEntryStart, ASCII digits, no "(dc:...) ") that ends at |from|, and
// the rest is exactly one carried transport line (TransportSpans). Then
// the same rules as plainHit's hold: a hit in the text before the first
// number, a head that is not HeadShaped or a printed head and slot name
// that hold a given secret withhold the site. Otherwise the site is
// "<file>|<head>|<slot>" when the hit is a slot's whole integer, the minus
// included, and "<file>|<head>|-" for any other position, and the hit is
// computed only for the whole integer of a Computed slot. No declaration
// takes part.
PlainHit SecrecyMatcher::transportHit(
		const QString &text,
		int from,
		int at,
		int till,
		const QString &file) const {
	static const auto partName = QRegularExpression(
		u"\\ADebugLogs/log_[0-9]{2}_[0-9]{2}\\.txt\\z"_q);
	static const auto entryStart = QRegularExpression(
		u"\\A\\[[0-9]{2}:[0-9]{2}:[0-9]{2}\\.[0-9]{3} [0-9]{2,}-[0-9]{7,}\\] "_q);
	const auto withheld = PlainHit{ .site = file + QChar('|') + kSiteWithheld };
	if (!partName.match(file).hasMatch()) {
		return withheld;
	}
	const auto entry = entryStart.match(text);
	if (!entry.hasMatch() || entry.capturedEnd() != from) {
		return withheld;
	}
	for (const auto &line : TransportLines()) {
		const auto spans = TransportSpans(line, text, from);
		if (spans.empty()) {
			continue;
		} else if (at < spans.front().first && till > from) {
			return withheld;
		}
		const auto head = TransportHead(line.format);
		auto slot = (const TransportSlot*)nullptr;
		for (auto i = 0; i != int(spans.size()); ++i) {
			if (spans[i].first == at && spans[i].second == till) {
				slot = &line.slots[i];
				break;
			}
		}
		const auto shown = slot ? (head + QChar(' ') + slot->name) : head;
		if (!HeadShaped(head) || holds(shown)) {
			return withheld;
		}
		return {
			.site = file
				+ QChar('|')
				+ head
				+ QChar('|')
				+ (slot ? slot->name : kSiteNoField),
			.computed = slot && slot->computed,
		};
	}
	return withheld;
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
	const QString &file,
	Internal::LogScanObserver *observer,
	bool strict)
: _matcher(matcher)
, _planted(planted)
, _control(control)
, _file(file)
, _reading(reading)
, _observer(observer)
, _strict(strict) {
}

void LineFeed::feed(const QString &line) {
	++_reading.lines;
	if (_observer && _observer->line) {
		_observer->line(line, IsEntryHeader(line) || IsMainLogStart(line));
	}
	if (IsEntryHeader(line)) {
		_run.clear();
	}
	auto context = _dump.begin(line, _reading);
	context.file = &_file;
	if (_strict) {
		_matcher.line(line, context, _run, _reading);
		_dump.end(line);
	}
	if (!_planted.isEmpty() && line.contains(_planted)) {
		++_reading.plantedHits;
	}
	if (!_control.isEmpty() && line.contains(_control)) {
		++_reading.controlHits;
	}
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
		const QString &file = QString(),
		Internal::LogScanObserver *observer = nullptr,
		bool strict = true) {
	if (observer && observer->beginSource) {
		observer->beginSource(reading.cls);
	}
	auto feed = LineFeed(matcher, planted, control, reading, file, observer, strict);
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
		const QString &name,
		Internal::LogScanObserver *observer = nullptr,
		bool strict = true) {
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
	if (observer && observer->beginSource) {
		observer->beginSource(reading.cls);
	}
	auto feed = LineFeed(matcher, planted, control, reading, name, observer, strict);
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
		SecrecyClassReading &reading,
		Internal::LogScanObserver *observer = nullptr,
		bool strict = true) {
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
			source.name,
			observer,
			strict);
	} else {
		const auto scan = ScanFile(
			source.path,
			source.sliceAtLastBanner,
			matcher,
			planted,
			source.control,
			reading,
			source.name,
			observer,
			strict);
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
constexpr auto kSelfTelemetryValue = -4821.75;
constexpr auto kSelfTelemetryDecimals = 2;
const auto kSelfTelemetryHead = u"NOTE: Selftest telemetry"_q;
// The digit runs of kSelfTelemetryValue's ordinary decimal.
const auto kSelfTelemetrySecrets = std::vector<QString>{
	u"4821"_q,
	u"75"_q,
};

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

// One TelemetryNumber case: the input and the exact text it must print.
struct TelemetryCase {
	double value = 0.;
	int decimals = 0;
	QString expected;
};

// One table for the exact-output and the digit-run checks: zeros of both
// signs, integers up to 2^53 - 1, negatives, fractions with a carry, a
// negative value that rounds to zero, many places, a negative |decimals|
// and the non-finite values. No case sits on an exact rounding half.
[[nodiscard]] std::vector<TelemetryCase> TelemetryCases() {
	const auto notANumber = std::numeric_limits<double>::quiet_NaN();
	const auto infinity = std::numeric_limits<double>::infinity();
	return {
		{ 0., 0, u"0p"_q },
		{ -0., 0, u"0p"_q },
		{ 7., 0, u"7p"_q },
		{ 2147483647., 0, u"2147483647p"_q },
		{ 9007199254740991., 0, u"9007199254740991p"_q },
		{ -1., 0, u"-1p"_q },
		{ -320., 0, u"-320p"_q },
		{ 0.5, 1, u"0p5"_q },
		{ 1.25, 2, u"1p25"_q },
		{ 16.31, 2, u"16p31"_q },
		{ 99.996, 2, u"100p00"_q },
		{ kSelfTelemetryValue, kSelfTelemetryDecimals, u"-4821p75"_q },
		{ -0.004, 2, u"-0p00"_q },
		{ -0.25, 3, u"-0p250"_q },
		{ 123456789.0625, 4, u"123456789p0625"_q },
		{ 3.0625, 12, u"3p062500000000"_q },
		{ 1.75, -1, u"2p"_q },
		{ notANumber, 2, u"nan"_q },
		{ infinity, 2, u"inf"_q },
		{ -infinity, 2, u"-inf"_q },
	};
}

// The one telemetry row shape, as an overlay's Note prints it: a plain
// test-log line whose value is the whole "value" field.
[[nodiscard]] QString TelemetryRow(const QString &value) {
	return kSelfTelemetryHead + u" value="_q + value;
}

// The clean tree plus |row| as one more test-log line, with |secrets| as
// extra synthetic short secrets, so that row is their only candidate.
[[nodiscard]] SelfFixture RowFixture(
		const QString &row,
		const std::vector<QString> &secrets) {
	auto result = CleanFixture();
	for (const auto &secret : secrets) {
		result.secrets.shortSecrets.push_back(secret);
	}
	InsertBeforePlanted(result.testLog, QStringList{ row });
	return result;
}

// The clean tree plus one telemetry row printing |value|, with the digit
// runs of the control value as extra synthetic short secrets, so that row
// is the scan's only candidate.
[[nodiscard]] SelfFixture TelemetryFixture(const QString &value) {
	return RowFixture(TelemetryRow(value), kSelfTelemetrySecrets);
}

// Every distinct non-empty digits-only substring of |text|, in order of
// first appearance: each is a short secret a formatted number could
// coincide with, so each must be held only embedded.
[[nodiscard]] std::vector<QString> DigitRuns(const QString &text) {
	auto result = std::vector<QString>();
	auto seen = QSet<QString>();
	const auto size = int(text.size());
	for (auto from = 0; from != size; ++from) {
		for (auto till = from; till != size && text[till].isDigit(); ++till) {
			const auto run = text.mid(from, till + 1 - from);
			if (!seen.contains(run)) {
				seen.insert(run);
				result.push_back(run);
			}
		}
	}
	return result;
}

// One row through the scan's own matcher, alone: no canary, planted
// control or other line, whose framing would leave some digit secrets
// undecided in a whole ReadSecrecy.
[[nodiscard]] SecrecyClassReading ScanRow(
		const QString &row,
		std::vector<QString> secrets) {
	const auto matcher = SecrecyMatcher({
		.shortSecrets = std::move(secrets),
	});
	auto result = SecrecyClassReading();
	ScanText(row, matcher, {}, {}, result, u"test_log.txt"_q);
	return result;
}

// Overlay telemetry: the digit runs of an ordinary decimal, as synthetic
// short secrets, decide at the row's plain-line site with field "-", and
// the same value as a TelemetryNumber holds them only embedded. Keeps no
// reading, so the earlier stages' rows and readings stay as they were.
void SelfTelemetry() {
	using Sites = std::map<QString, int>;
	const auto ordinary = QString::number(
		kSelfTelemetryValue,
		'f',
		kSelfTelemetryDecimals);
	const auto telemetry = TelemetryNumber(
		kSelfTelemetryValue,
		kSelfTelemetryDecimals);
	const auto site = u"test_log.txt|"_q
		+ kSelfTelemetryHead
		+ u"|"_q
		+ kSiteNoField;
	const auto secrets = int(kSelfTelemetrySecrets.size());
	{
		const auto what = u"secrecy self-test: the digit runs of an "
			"ordinary decimal telemetry value, as synthetic short secrets, "
			"fail the scan at the row's plain-line site with field -"_q;
		const auto run = RunFixture(TelemetryFixture(ordinary));
		CheckPrepared(what, run);
		const auto &r = run.reading;
		const auto &t = ClassOf(r, SecrecyClass::TestLog);
		const auto rows = SecrecyRows(r).join(QChar('\n'));
		Check(
			run.prepared
				&& r.decided
				&& !r.clean
				&& (r.clientWritten() == secrets)
				&& (r.computed() == 0)
				&& (t.bounded == secrets)
				&& (t.boundedOther == secrets)
				&& (t.embedded == 0)
				&& (t.plainSites == Sites{ { site, secrets } })
				&& rows.contains(u"plainSites=["_q
					+ site
					+ u" x"_q
					+ QString::number(secrets)
					+ u"]"_q),
			what,
			SiteDetails(t) + u" | "_q + Summary(r));
	}
	{
		const auto what = u"secrecy self-test: the same value as a "
			"TelemetryNumber holds those secrets only embedded, so a scan "
			"whose only candidate is that row is clean"_q;
		const auto run = RunFixture(TelemetryFixture(telemetry));
		CheckPrepared(what, run);
		const auto &r = run.reading;
		const auto &t = ClassOf(r, SecrecyClass::TestLog);
		Check(
			run.prepared
				&& r.decided
				&& r.clean
				&& (r.clientWritten() == 0)
				&& (r.computed() == 0)
				&& (t.bounded == 0)
				&& (t.embedded == secrets)
				&& t.plainSites.empty(),
			what,
			SiteDetails(t) + u" | "_q + Summary(r));
	}
	const auto cases = TelemetryCases();
	const auto count = int(cases.size());
	{
		// The outputs print in case order: the evidence parses them back.
		auto outputs = QStringList();
		auto failed = QStringList();
		for (auto i = 0; i != count; ++i) {
			const auto &entry = cases[i];
			const auto out = TelemetryNumber(entry.value, entry.decimals);
			outputs.push_back(out);
			if (out != entry.expected) {
				failed.push_back(QString::number(i));
			}
		}
		Check(
			failed.isEmpty(),
			u"secrecy self-test: TelemetryNumber prints every case as "
			"documented"_q,
			u"cases=%1 outputs=[%2] failed=[%3]"_q
				.arg(count)
				.arg(outputs.join(u", "_q))
				.arg(failed.join(u", "_q)));
	}
	{
		// The rule side reads every digit run of each output; the control
		// side proves the same row shape does count a run of an ordinary
		// decimal, so a zero on the rule side is not the row's doing.
		auto finite = 0;
		auto digitRuns = 0;
		auto ruleBounded = 0;
		auto ruleEmbedded = 0;
		auto ordinaryWithBoundedRun = 0;
		auto failing = QStringList();
		for (auto i = 0; i != count; ++i) {
			const auto &entry = cases[i];
			const auto out = TelemetryNumber(entry.value, entry.decimals);
			const auto runs = DigitRuns(out);
			const auto rule = ScanRow(TelemetryRow(out), runs);
			digitRuns += int(runs.size());
			ruleBounded += rule.bounded;
			ruleEmbedded += rule.embedded;
			auto ok = (rule.bounded == 0)
				&& (rule.embedded >= int(runs.size()));
			if (std::isfinite(entry.value)) {
				++finite;
				const auto text = QString::number(
					entry.value,
					'f',
					std::max(entry.decimals, 0));
				const auto plain = ScanRow(
					TelemetryRow(text),
					DigitRuns(text));
				if (plain.bounded >= 1) {
					++ordinaryWithBoundedRun;
				} else {
					ok = false;
				}
			}
			if (!ok) {
				failing.push_back(QString::number(i));
			}
		}
		Check(
			failing.isEmpty() && (finite > 0) && (digitRuns > 0),
			u"secrecy self-test: no digit run of any TelemetryNumber is "
			"bounded, while the ordinary decimal of every finite value holds "
			"a bounded one"_q,
			u"cases=%1 finite=%2 digitRuns=%3 ruleBounded=%4 "
			"ruleEmbedded=%5 ordinaryWithBoundedRun=%6/%2 failing=[%7]"_q
				.arg(count)
				.arg(finite)
				.arg(digitRuns)
				.arg(ruleBounded)
				.arg(ruleEmbedded)
				.arg(ordinaryWithBoundedRun)
				.arg(failing.join(u", "_q)));
	}
}

// Caller text for the converted helper rows, synthetic and letters only,
// with no phrase word.
const auto kSelfHelperSlots = std::vector<QString>{
	u"Selftest helper"_q,
	u"Selftest subject"_q,
	u"Selftest control"_q,
	u"Selftest rows"_q,
};

// The same slots made of SelfPhraseA() words: each slot holds one pair, and
// adjacent slots meet at another pair (cactus dolphin, lobster volcano,
// mango velvet), so any converted number placed directly between two slots
// would change the word-run count.
const auto kSelfHelperPhraseSlots = std::vector<QString>{
	u"walnut giraffe cactus"_q,
	u"dolphin pumpkin lobster"_q,
	u"volcano oyster mango"_q,
	u"velvet kangaroo jaguar"_q,
};

// Synthetic values of the converted rows. None equals a digit run the
// clean fixture or the canaries hold bounded in client-written text; the
// flag 1 is bounded there only in the Recv entry, which never decides.
const auto kSelfHelperSample = PostPaintSample{
	.seq = 4821,
	.paintAt = 37645,
	.lastPaintAt = 37693,
	.covered = 59,
	.lo = 61482,
	.hi = 61537,
};
const auto kSelfHelperStop = PostPaintStop{
	.reason = u"owner-destroyed"_q,
	.seq = 4821,
	.pending = true,
	.samples = 3764,
	.paints = 5937,
	.ignored = 6148,
	.dropped = 7293,
};
const auto kSelfHelperFrame = ClockedFrame{
	.index = 36,
	.requested = 4821,
	.tick = 37645,
	.reachedLo = 3764,
	.reachedHi = 4829,
	.waited = 5937,
	.probeTicks = 41,
	.renderTicks = 53,
};

// The distinct maximal digit runs of |text|, in order of first appearance:
// the numbers an ordinary row prints, each as a synthetic short secret.
[[nodiscard]] std::vector<QString> MaximalDigitRuns(const QString &text) {
	auto result = std::vector<QString>();
	const auto size = int(text.size());
	auto from = 0;
	while (from < size) {
		if (!text[from].isDigit()) {
			++from;
			continue;
		}
		auto till = from + 1;
		while (till < size && text[till].isDigit()) {
			++till;
		}
		const auto run = text.mid(from, till - from);
		if (!ranges::contains(result, run)) {
			result.push_back(run);
		}
		from = till;
	}
	return result;
}

// |text| without each "p" that directly follows a digit and is not
// followed by a letter or digit: the point of a TelemetryNumber with no
// fraction digits, so a requested row reads back as its ordinary text.
[[nodiscard]] QString StripTelemetryPoints(const QString &text) {
	auto result = QString();
	const auto size = int(text.size());
	for (auto i = 0; i != size; ++i) {
		const auto point = (text[i] == QChar('p'))
			&& (i > 0)
			&& text[i - 1].isDigit()
			&& ((i + 1 == size) || !text[i + 1].isLetterOrNumber());
		if (!point) {
			result += text[i];
		}
	}
	return result;
}

// The SelfPhraseA() word runs the scan's own matcher counts in |row|.
[[nodiscard]] int PhraseRuns(const QString &row) {
	const auto matcher = SecrecyMatcher({ .phrases = { SelfPhraseA() } });
	auto reading = SecrecyClassReading();
	ScanText(row, matcher, {}, {}, reading);
	return reading.wordRunsOther;
}

// The walk of the tally and refusal rows: |slots| name it, its subject and
// its control; 4821 items examined, 37 subjects and |controls| controls.
[[nodiscard]] DiscriminatingScan HelperScan(
		const std::vector<QString> &slots,
		int controls) {
	auto result = DiscriminatingScan(slots[0], slots[1], slots[2]);
	result.examined(4821);
	for (auto i = 0; i != 37; ++i) {
		result.matchedSubject();
	}
	for (auto i = 0; i != controls; ++i) {
		result.matchedControl();
	}
	return result;
}

// The refusal of a request named slots[0] that renders nothing: it has an
// action, also a handler when |handler|, and asks for |elapsed|.
[[nodiscard]] QString HelperRequestRefusal(
		const std::vector<QString> &slots,
		bool handler,
		std::vector<crl::time> elapsed,
		NumberFormat format) {
	auto request = ClockedRequest{
		.name = slots[0],
		.action = [] {},
		.elapsed = std::move(elapsed),
		.render = [](const ClockedFrame &) {},
	};
	if (handler) {
		request.handler = std::make_shared<LambdaClickHandler>([] {});
	}
	return ClockedRequestRefusal(request, format);
}

// The window of probe slots[1] holding the rows slots[2] and slots[3].
[[nodiscard]] QString HelperProbeWindow(
		const std::vector<QString> &slots,
		NumberFormat format) {
	return ProbeWindowText(
		slots[1],
		4821,
		5937,
		{ slots[2], slots[3] },
		format);
}

// A paired round trip keyed slots[1] that discarded slots[2], over an
// empty window of probe slots[3].
[[nodiscard]] QString HelperRoundTrip(
		const std::vector<QString> &slots,
		NumberFormat format) {
	const auto trip = RoundTrip{
		.state = RoundTripState::Paired,
		.issueAtMs = 37645,
		.answerAtMs = 37693,
		.roundTripMs = 48,
		.issues = 41,
		.answers = 53,
		.dropped = 52,
		.preIssues = 67,
		.preAnswers = 79,
	};
	return RoundTripText(
		slots[1],
		trip,
		{ slots[2] },
		ProbeWindowText(slots[3], 7293, 8352, {}, format),
		format);
}

// One converted shared-helper row: |line| frames its formatter's output as
// the helper's logging call writes it, for caller text |slots| and numbers
// in |format|, and |ordinary| is today's text for kSelfHelperSlots.
struct HelperRowCase {
	QString label;
	Fn<QString(const std::vector<QString> &slots, NumberFormat format)> line;
	QString ordinary;
};

// Every converted row text. A framing prefix carries no digit: the
// numbers, their field words and their order come only from the helper's
// own formatter.
[[nodiscard]] std::vector<HelperRowCase> HelperRowCases() {
	using Slots = std::vector<QString>;
	return {
		{
			u"geometry"_q,
			[](const Slots &s, NumberFormat f) {
				return GeometryText(s[0], QRect(-4821, 3764, 5937, 6148), f);
			},
			u"GEOMETRY: Selftest helper: x=-4821 y=3764 w=5937 h=6148"_q,
		},
		{
			u"near"_q,
			[](const Slots &s, NumberFormat f) {
				return u"TEST_RESULT: PASS: "_q
					+ CheckNearText(4826, 4821, 37, s[0], f);
			},
			u"TEST_RESULT: PASS: Selftest helper (actual 4826, expected "
			u"4821 ±37)"_q,
		},
		{
			u"sample"_q,
			[](const Slots &s, NumberFormat f) {
				return u"NOTE: "_q
					+ s[0]
					+ u": "_q
					+ PostPaintSampleText(kSelfHelperSample, f);
			},
			u"NOTE: Selftest helper: kind=post seq=4821 paint=37645 "
			"last=37693 covered=59 lo=61482 hi=61537"_q,
		},
		{
			u"stop"_q,
			[](const Slots &s, NumberFormat f) {
				return u"NOTE: "_q
					+ PostPaintStopText(s[0], kSelfHelperStop, f);
			},
			u"NOTE: Selftest helper: kind=stop reason=owner-destroyed "
			"seq=4821 pending=1 samples=3764 paints=5937 ignored=6148 "
			"dropped=7293"_q,
		},
		{
			u"frame"_q,
			[](const Slots &s, NumberFormat f) {
				return u"NOTE: "_q
					+ s[0]
					+ u": "_q
					+ ClockedFrameText(kSelfHelperFrame, f);
			},
			u"NOTE: Selftest helper: kind=frame index=37 requested=4821 "
			"reached=[3764,4829] tick=37645 waited=5937 probeTicks=41 "
			"renderTicks=53"_q,
		},
		{
			u"end gate"_q,
			[](const Slots &s, NumberFormat f) {
				return u"TEST_RESULT: N/A: "_q
					+ ClockedFrameSubject(s[0], kSelfHelperFrame, f)
					+ u" - "_q
					+ ClockedEndGateText(4821, 3764, f);
			},
			u"TEST_RESULT: N/A: Selftest helper: frame 37 at 4821 ms - "
			"requested 4821 ms is at or after the declared animation end "
			"3764 ms: no in-progress frame exists to judge, so nothing was "
			"waited for, ticked or rendered"_q,
		},
		{
			u"reached gate"_q,
			[](const Slots &s, NumberFormat f) {
				return u"TEST_RESULT: N/A: "_q
					+ ClockedFrameSubject(s[0], kSelfHelperFrame, f)
					+ u" - "_q
					+ ClockedReachedGateText(5937, 6148, 6143, f);
			},
			u"TEST_RESULT: N/A: Selftest helper: frame 37 at 4821 ms - the "
			"tick reached [5937,6148] ms, at or after the declared animation "
			"end 6143 ms: the animation may have finished, so no "
			"in-progress frame exists to judge; the frame was not "
			"rendered"_q,
		},
		{
			u"no tick"_q,
			[](const Slots &s, NumberFormat f) {
				return u"TEST_RESULT: FAIL: fixture gate: "_q
					+ ClockedFrameSubject(s[0], kSelfHelperFrame, f)
					+ u" - "_q
					+ ClockedNoTickText(59, 7293, 8352, f);
			},
			u"TEST_RESULT: FAIL: fixture gate: Selftest helper: frame 37 at "
			"4821 ms - manager-did-not-tick: "
			"Core::App().animationManager().update() did not call the probe "
			"animation (probe calls 59 before and after it, 7293 ms after "
			"the action started, waited 8352 ms): the manager skipped the "
			"tick - a schedule callback still pending (_scheduled; under "
			"Manager::SetScheduleWithInvokeQueued(true) it is an "
			"InvokeQueued call the postponed-call drain does not run) or an "
			"update in progress (_updating); the frame was not rendered"_q,
		},
		{
			u"render tick"_q,
			[](const Slots &s, NumberFormat f) {
				return u"TEST_RESULT: FAIL: fixture gate: "_q
					+ ClockedFrameSubject(s[0], kSelfHelperFrame, f)
					+ u" - "_q
					+ ClockedRenderTickText(41, f);
			},
			u"TEST_RESULT: FAIL: fixture gate: Selftest helper: frame 37 at "
			"4821 ms - render-advanced-the-manager: the probe animation was "
			"called 41 time(s) during the render, so the frame may show a "
			"value past its reached elapsed bounds; render synchronously "
			"(Test::GrabWidget / GrabRect), never through repaint() or "
			"processEvents()"_q,
		},
		{
			u"request flags"_q,
			[](const Slots &s, NumberFormat f) {
				return u"TEST_RESULT: FAIL: fixture gate: "_q
					+ s[0]
					+ u": clocked frames refused - "_q
					+ HelperRequestRefusal(s, true, { 4821 }, f);
			},
			u"TEST_RESULT: FAIL: fixture gate: Selftest helper: clocked "
			"frames refused - exactly one of action and handler must be set "
			"(action=1 handler=1)"_q,
		},
		{
			u"request elapsed"_q,
			[](const Slots &s, NumberFormat f) {
				return u"TEST_RESULT: FAIL: fixture gate: "_q
					+ s[0]
					+ u": clocked frames refused - "_q
					+ HelperRequestRefusal(s, false, { 4821, 3764 }, f);
			},
			u"TEST_RESULT: FAIL: fixture gate: Selftest helper: clocked "
			"frames refused - elapsed times must be non-negative and "
			"strictly increasing, read 4821,3764"_q,
		},
		{
			u"probe window"_q,
			[](const Slots &s, NumberFormat f) {
				return u"TEST_RESULT: PASS: "_q
					+ s[0]
					+ u" - "_q
					+ HelperProbeWindow(s, f);
			},
			u"TEST_RESULT: PASS: Selftest helper - probe=Selftest subject "
			"window=[4821,5937) rows=Selftest control; Selftest rows"_q,
		},
		{
			u"probe count"_q,
			[](const Slots &s, NumberFormat f) {
				return u"TEST_RESULT: PASS: "_q
					+ s[0]
					+ u" - "_q
					+ ProbeCountText(6148, 6148, HelperProbeWindow(s, f), f);
			},
			u"TEST_RESULT: PASS: Selftest helper - expected=6148 "
			"actual=6148 probe=Selftest subject window=[4821,5937) "
			"rows=Selftest control; Selftest rows"_q,
		},
		{
			u"round trip"_q,
			[](const Slots &s, NumberFormat f) {
				return u"TEST_RESULT: PASS: "_q
					+ s[0]
					+ u" - "_q
					+ HelperRoundTrip(s, f);
			},
			u"TEST_RESULT: PASS: Selftest helper - key=Selftest subject "
			"state=paired issues=41 answers=53 dropped=52 preIssues=67 "
			"preAnswers=79 issueAtMs=37645 answerAtMs=37693 roundTripMs=48 "
			"discarded=Selftest control probe=Selftest rows "
			"window=[7293,8352) rows=<none>"_q,
		},
		{
			u"scan tally"_q,
			[](const Slots &s, NumberFormat f) {
				return u"NOTE: "_q + HelperScan(s, 59).tallyText(f);
			},
			u"NOTE: Selftest helper: examined=4821 subject(Selftest "
			"subject)=37 control(Selftest control)=59"_q,
		},
		{
			u"scan refusal"_q,
			[](const Slots &s, NumberFormat f) {
				return u"TEST_RESULT: FAIL: "_q
					+ s[0]
					+ u" discriminates - "_q
					+ HelperScan(s, 0).refusalText(f);
			},
			u"TEST_RESULT: FAIL: Selftest helper discriminates - the walk "
			"matched no Selftest control, so its Selftest subject count of "
			"37 over 4821 examined items cannot tell absence from an "
			"enumeration that never reaches the subject"_q,
		},
	};
}

// One case's texts, each built once: both formats for kSelfHelperSlots
// and for kSelfHelperPhraseSlots, and the ordinary text's digit runs.
struct HelperRows {
	QString ordinary;
	QString telemetry;
	QString phraseOrdinary;
	QString phraseTelemetry;
	std::vector<QString> runs;
};

[[nodiscard]] HelperRows BuildHelperRows(const HelperRowCase &entry) {
	auto result = HelperRows{
		.ordinary = entry.line(kSelfHelperSlots, NumberFormat::Ordinary),
		.telemetry = entry.line(kSelfHelperSlots, NumberFormat::Telemetry),
		.phraseOrdinary = entry.line(
			kSelfHelperPhraseSlots,
			NumberFormat::Ordinary),
		.phraseTelemetry = entry.line(
			kSelfHelperPhraseSlots,
			NumberFormat::Telemetry),
	};
	result.runs = MaximalDigitRuns(result.ordinary);
	return result;
}

// Whether |telemetry| differs from |ordinary| only by a "p" after each
// number, and by at least one.
[[nodiscard]] bool PointOnly(
		const QString &telemetry,
		const QString &ordinary) {
	return (telemetry != ordinary)
		&& (StripTelemetryPoints(telemetry) == ordinary);
}

// The shared helpers' numbers: every converted row read through its own
// formatter for fixed synthetic values, in both formats. Never asks the
// process-wide request and keeps no reading, so the earlier stages' rows
// and readings stay as they were.
void SelfHelperNumbers() {
	const auto cases = HelperRowCases();
	const auto count = int(cases.size());
	auto rows = std::vector<HelperRows>();
	for (const auto &entry : cases) {
		rows.push_back(BuildHelperRows(entry));
	}
	{
		auto failing = QStringList();
		for (auto i = 0; i != count; ++i) {
			if (rows[i].ordinary != cases[i].ordinary) {
				failing.push_back(cases[i].label);
			}
		}
		Check(
			(count > 0) && failing.isEmpty(),
			u"secrecy self-test: without the request every converted helper "
			"row prints today's text"_q,
			u"rows=%1 equal=%2 failing=[%3]"_q
				.arg(count)
				.arg(count - int(failing.size()))
				.arg(failing.join(u", "_q)));
	}
	{
		auto failing = QStringList();
		for (auto i = 0; i != count; ++i) {
			const auto &row = rows[i];
			if (!PointOnly(row.telemetry, row.ordinary)
				|| !PointOnly(row.phraseTelemetry, row.phraseOrdinary)) {
				failing.push_back(cases[i].label);
			}
		}
		Check(
			(count > 0) && failing.isEmpty(),
			u"secrecy self-test: under the request every converted helper "
			"row differs from today's text only by a p after each number"_q,
			u"rows=%1 pointOnly=%2 failing=[%3]"_q
				.arg(count)
				.arg(count - int(failing.size()))
				.arg(failing.join(u", "_q)));
	}
	{
		const auto what = u"secrecy self-test: the digit runs of each "
			"converted helper row's ordinary text, as synthetic short "
			"secrets, fail the scan at that row"_q;
		auto secrets = 0;
		auto boundedAlone = 0;
		auto clientWritten = 0;
		auto failing = QStringList();
		for (auto i = 0; i != count; ++i) {
			const auto &row = rows[i];
			const auto runs = int(row.runs.size());
			const auto alone = ScanRow(row.ordinary, row.runs);
			const auto run = RunFixture(RowFixture(row.ordinary, row.runs));
			CheckPrepared(what, run);
			const auto &r = run.reading;
			const auto &t = ClassOf(r, SecrecyClass::TestLog);
			auto sites = 0;
			for (const auto &[site, hits] : t.plainSites) {
				sites += hits;
			}
			secrets += runs;
			boundedAlone += alone.boundedOther;
			clientWritten += r.clientWritten();
			const auto ok = run.prepared
				&& r.decided
				&& !r.clean
				&& (alone.bounded >= runs)
				&& (alone.boundedOther == alone.bounded)
				&& (t.boundedOther == alone.boundedOther)
				&& (sites == t.boundedOther)
				&& (r.clientWritten() == alone.boundedOther);
			if (!ok) {
				failing.push_back(cases[i].label);
			}
		}
		Check(
			(count > 0) && failing.isEmpty(),
			what,
			u"rows=%1 secrets=%2 boundedAlone=%3 clientWritten=%4 "
			"failing=[%5]"_q
				.arg(count)
				.arg(secrets)
				.arg(boundedAlone)
				.arg(clientWritten)
				.arg(failing.join(u", "_q)));
	}
	{
		const auto what = u"secrecy self-test: under the request no "
			"converted helper row holds a bounded digit run, and a scan whose "
			"only candidate is that row is clean"_q;
		auto digitRuns = 0;
		auto bounded = 0;
		auto embedded = 0;
		auto clean = 0;
		auto failing = QStringList();
		for (auto i = 0; i != count; ++i) {
			const auto &row = rows[i];
			const auto all = DigitRuns(row.telemetry);
			const auto alone = ScanRow(row.telemetry, all);
			const auto run = RunFixture(RowFixture(row.telemetry, row.runs));
			CheckPrepared(what, run);
			const auto &r = run.reading;
			const auto &t = ClassOf(r, SecrecyClass::TestLog);
			digitRuns += int(all.size());
			bounded += alone.bounded;
			embedded += alone.embedded;
			const auto scanClean = run.prepared && r.decided && r.clean;
			if (scanClean) {
				++clean;
			}
			const auto ok = (alone.bounded == 0)
				&& (alone.embedded >= int(all.size()))
				&& scanClean
				&& (t.bounded == 0)
				&& (r.clientWritten() == 0);
			if (!ok) {
				failing.push_back(cases[i].label);
			}
		}
		Check(
			(count > 0) && (digitRuns > 0) && failing.isEmpty(),
			what,
			u"rows=%1 digitRuns=%2 bounded=%3 embedded=%4 clean=%5 "
			"failing=[%6]"_q
				.arg(count)
				.arg(digitRuns)
				.arg(bounded)
				.arg(embedded)
				.arg(clean)
				.arg(failing.join(u", "_q)));
	}
	{
		// The premise: the phrase reading can see a split.
		const auto between = [](const QString &number) {
			return PhraseRuns(u"NOTE: "_q
				+ kSelfHelperPhraseSlots[1]
				+ QChar(' ')
				+ number
				+ QChar(' ')
				+ kSelfHelperPhraseSlots[2]);
		};
		const auto ordinary = between(u"4821"_q);
		const auto telemetry = between(TelemetryNumber(4821));
		Check(
			ordinary == telemetry + 1,
			u"secrecy self-test: a TelemetryNumber between two phrase words "
			"ends the run an ordinary number left joined"_q,
			u"ordinary=%1 telemetry=%2"_q.arg(ordinary).arg(telemetry));
	}
	{
		auto runs = QStringList();
		auto ordinary = 0;
		auto telemetry = 0;
		auto failing = QStringList();
		for (auto i = 0; i != count; ++i) {
			const auto ordinaryRuns = PhraseRuns(rows[i].phraseOrdinary);
			const auto telemetryRuns = PhraseRuns(rows[i].phraseTelemetry);
			runs.push_back(u"%1=%2/%3"_q
				.arg(cases[i].label)
				.arg(ordinaryRuns)
				.arg(telemetryRuns));
			ordinary += ordinaryRuns;
			telemetry += telemetryRuns;
			if (ordinaryRuns != telemetryRuns || ordinaryRuns < 1) {
				failing.push_back(cases[i].label);
			}
		}
		Check(
			(count > 0) && failing.isEmpty(),
			u"secrecy self-test: a synthetic phrase in each converted helper "
			"row's caller text gives the same word runs in both formats"_q,
			u"rows=%1 runs=[%2] ordinary=%3 telemetry=%4 failing=[%5]"_q
				.arg(count)
				.arg(runs.join(u", "_q))
				.arg(ordinary)
				.arg(telemetry)
				.arg(failing.join(u", "_q)));
	}
}

// The carried transport lines' synthetic short secret, digits only because
// transport numbers are digits, and the other synthetic values their slots
// and DEBUG_LOG suffix print.
const auto kSelfTransportSecret = u"73914"_q;
const auto kSelfTransportSize = u"52817"_q;
const auto kSelfTransportOther = u"40963"_q;
const auto kSelfTransportTime = u"1768478123"_q;
const auto kSelfTransportRequest = u"68305"_q;
const auto kSelfTransportLine = u"37"_q;
const auto kSelfTransportToken = u"1768478123528170001"_q;
const auto kSelfTransportPart = u"DebugLogs/log_10_00.txt"_q;
const auto kSelfPrivateFile = u"session_private.cpp"_q;
const auto kSelfInstanceFile = u"mtp_instance.cpp"_q;
const auto kSelfSendingHead = u"MTP Info: sending request"_q;
const auto kSelfParserHead = u"RPC Info: found parser for request"_q;
const auto kSelfAckHead = u"Message Info: ignoring ACK for msgId"_q;
const auto kSelfChangingHead = u"MTP Info: changing request"_q;
const auto kSelfKeyHead = u"AuthKey Info: unbound key creation succeed"_q;

// The withholding cases' own secrets, each scanned with the key-creation or
// the queue line: the first three make the printed head, the slot name or
// a phrase across both hold a secret without being bounded in that line,
// the last two are bounded words before its first number. Never printed,
// not even as a label.
const auto kSelfHeadSecret = u"UNBOUND"_q;
const auto kSelfSlotSecret = u"TemporaryKeyId"_q;
const auto kSelfSpanPhrase = std::vector<QString>{
	u"succeed"_q,
	u"temporaryKeyId"_q,
};
const auto kSelfHeadWord = u"creation"_q;
const auto kSelfGapWord = u"thread"_q;

// A DEBUG_LOG line as base::LogWriteDebug and Logs::writeDebug write it into
// a DebugLogs log_ part: "<entry start> <message> (<file> : <line>)".
[[nodiscard]] QString DebugEntry(
		const QString &message,
		const QString &file,
		const QString &line = kSelfTransportLine) {
	return Entry(message + u" ("_q + file + u" : "_q + line + u")"_q);
}

// SessionPrivate::sendSecureRequest's message, built by hand from its
// format, independently of the carried table.
[[nodiscard]] QString SendingMessage(
		const QString &size,
		const QString &num,
		const QString &time) {
	return u"MTP Info: sending request, size: "_q
		+ size
		+ u", num: "_q
		+ num
		+ u", time: "_q
		+ time;
}

[[nodiscard]] QString SendingEntry(
		const QString &num,
		const QString &line = kSelfTransportLine) {
	return DebugEntry(
		SendingMessage(kSelfTransportSize, num, kSelfTransportTime),
		kSelfPrivateFile,
		line);
}

// Instance::Private::processCallback's line.
[[nodiscard]] QString ParserEntry(const QString &request) {
	return DebugEntry(
		u"RPC Info: found parser for request "_q
			+ request
			+ u", trying to parse response..."_q,
		kSelfInstanceFile);
}

// SessionPrivate::requestsAcked's line: a server-sent msgId beside the
// client's own request id.
[[nodiscard]] QString AckEntry(const QString &msgId, const QString &request) {
	return DebugEntry(
		u"Message Info: ignoring ACK for msgId "_q
			+ msgId
			+ u" because request "_q
			+ request
			+ u" requires a response"_q,
		kSelfPrivateFile);
}

// Instance::Private::onErrorDefault's line, both dc slots glued.
[[nodiscard]] QString ChangingEntry(const QString &from, const QString &to) {
	return DebugEntry(
		u"MTP Info: changing request "_q
			+ kSelfTransportRequest
			+ u" from dcWithShift"_q
			+ from
			+ u" to dc"_q
			+ to,
		kSelfInstanceFile);
}

// SessionPrivate::tryAcquireKeyCreation's line with |keyId| as the
// temporary key id.
[[nodiscard]] QString KeyCreationEntry(const QString &keyId) {
	return DebugEntry(
		kSelfKeyHead
			+ u", ids: ("_q
			+ keyId
			+ u", "_q
			+ kSelfTransportSize
			+ u") server salts: ("_q
			+ kSelfTransportOther
			+ u", "_q
			+ kSelfTransportRequest
			+ u")"_q,
		kSelfPrivateFile);
}

// SessionPrivate::handleReceived's queue line.
[[nodiscard]] QString QueueEntry() {
	return DebugEntry(
		u"MTP Info: queueTryToReceive() - need to parse in another thread, "_q
			+ kSelfTransportSize
			+ u" messages."_q,
		kSelfPrivateFile);
}

[[nodiscard]] QString TransportSite(
		const QString &head,
		const QString &field,
		const QString &file = kSelfTransportPart) {
	return file + QChar('|') + head + QChar('|') + field;
}

[[nodiscard]] QString WithheldSite(const QString &file = kSelfTransportPart) {
	return file + QChar('|') + kSiteWithheld;
}

[[nodiscard]] SecrecySecrets TransportSecrets() {
	return { .shortSecrets = { kSelfTransportSecret } };
}

// The clean tree with the synthetic number as one more short secret.
[[nodiscard]] SelfFixture TransportFixture() {
	auto result = CleanFixture();
	result.secrets.shortSecrets.push_back(kSelfTransportSecret);
	return result;
}

// One text alone through the scan's own matcher under the public file name
// |file|: no canary, planted control or other line.
[[nodiscard]] SecrecyClassReading ScanTransportLine(
		const QString &text,
		const SecrecySecrets &secrets,
		const QString &file = kSelfTransportPart,
		std::vector<SecrecyComputedField> declared = {}) {
	const auto matcher = SecrecyMatcher(secrets, std::move(declared));
	auto result = SecrecyClassReading();
	ScanText(text, matcher, {}, {}, result, file);
	return result;
}

// What one isolated scan must read: exactly these computed and deciding
// plain-line sites, this embedded count (-1: not checked) and nothing else.
struct TransportExpect {
	std::map<QString, int> computed;
	std::map<QString, int> plain;
	int embedded = 0;
};

[[nodiscard]] int HitsIn(const std::map<QString, int> &sites) {
	auto result = 0;
	for (const auto &[site, hits] : sites) {
		result += hits;
	}
	return result;
}

[[nodiscard]] bool TransportReads(
		const SecrecyClassReading &c,
		const TransportExpect &expect) {
	const auto computed = HitsIn(expect.computed);
	const auto plain = HitsIn(expect.plain);
	return (c.bounded == computed + plain)
		&& (c.boundedComputed == computed)
		&& (c.boundedOther == plain)
		&& (c.computedSites == expect.computed)
		&& (c.plainSites == expect.plain)
		&& (expect.embedded < 0 || c.embedded == expect.embedded)
		&& !c.boundedSend
		&& !c.boundedRecv
		&& !c.wordRunsOther
		&& !c.tokensOther;
}

// One isolated case: a generic label, never a secret or a scanned value,
// the text, what its scan must read, the public file name and the secrets.
struct TransportCase {
	QString label;
	QString text;
	TransportExpect expect;
	QString file = kSelfTransportPart;
	SecrecySecrets secrets = TransportSecrets();
};

// Scans each case alone and answers the failing ones as "<index>:<label>".
// With |rowsPrivate| a case also fails when its class row (ClassRow) holds
// any of its short secrets or phrase words, case-insensitively.
[[nodiscard]] QStringList TransportFailing(
		const std::vector<TransportCase> &cases,
		bool rowsPrivate) {
	auto result = QStringList();
	for (auto i = 0; i != int(cases.size()); ++i) {
		const auto &entry = cases[i];
		const auto c = ScanTransportLine(entry.text, entry.secrets, entry.file);
		auto ok = TransportReads(c, entry.expect);
		if (rowsPrivate) {
			const auto row = ClassRow(c);
			auto secrets = entry.secrets.shortSecrets;
			for (const auto &phrase : entry.secrets.phrases) {
				secrets.insert(end(secrets), begin(phrase), end(phrase));
			}
			for (const auto &secret : secrets) {
				ok = ok && !row.contains(secret, Qt::CaseInsensitive);
			}
		}
		if (!ok) {
			result.push_back(u"%1:%2"_q.arg(i).arg(entry.label));
		}
	}
	return result;
}

void CheckTransportCases(
		const QString &what,
		const std::vector<TransportCase> &cases,
		bool rowsPrivate = false) {
	const auto failing = TransportFailing(cases, rowsPrivate);
	Check(
		!cases.empty() && failing.isEmpty(),
		what,
		u"cases=%1 failing=[%2]"_q
			.arg(cases.size())
			.arg(failing.join(u", "_q)));
}

// Whether placeholder |index| (0-based) of |format| touches a letter or a
// digit, so that a whole positive value there is only ever embedded.
[[nodiscard]] bool TransportGlued(const QString &format, int index) {
	const auto placeholder = u"%"_q + QString::number(index + 1);
	const auto at = int(format.indexOf(placeholder));
	if (at < 0) {
		return false;
	}
	const auto till = at + int(placeholder.size());
	return (at > 0 && format[at - 1].isLetterOrNumber())
		|| (till < int(format.size()) && format[till].isLetterOrNumber());
}

// Every carried row and slot, mechanics only: the row's message as its
// product call builds it (.arg() by .arg()), with the synthetic number in
// that slot and other synthetic values in the rest, scanned alone in a
// log_ part. The table's shape holds (a known source file, distinct
// formats, a HeadShaped head, distinct safe slot names, no computed glued
// slot), exactly that row matches the line, with no "name=" field and every
// value at its span, and the number reads as the slot's class says: a
// computed slot at its site in computedSites, any other bounded slot at its
// site deciding, a glued slot only embedded. Generated from the table, it
// cannot prove the table's source provenance.
void CheckTransportSweep() {
	const auto &lines = TransportLines();
	const auto files = QStringList{
		u"session_private.cpp"_q,
		u"session.cpp"_q,
		u"mtp_instance.cpp"_q,
	};
	const auto fillers = std::vector<QString>{
		kSelfTransportSize,
		kSelfTransportOther,
		kSelfTransportRequest,
		kSelfTransportTime,
	};
	auto formats = QSet<QString>();
	auto slots = 0;
	auto computed = 0;
	auto received = 0;
	auto glued = 0;
	auto failing = QStringList();
	for (auto i = 0; i != int(lines.size()); ++i) {
		const auto &line = lines[i];
		const auto head = TransportHead(line.format);
		const auto count = int(line.slots.size());
		auto names = QSet<QString>();
		auto shaped = files.contains(line.file)
			&& !formats.contains(line.format)
			&& HeadShaped(head)
			&& (count > 0)
			&& (count <= int(fillers.size()));
		formats.insert(line.format);
		for (auto j = 0; j != count; ++j) {
			const auto &slot = line.slots[j];
			shaped = shaped
				&& (SafeIdentifier(slot.name) == slot.name)
				&& !names.contains(slot.name)
				&& !(slot.computed && TransportGlued(line.format, j));
			names.insert(slot.name);
		}
		if (!shaped) {
			failing.push_back(QString::number(i));
			continue;
		}
		for (auto j = 0; j != count; ++j) {
			auto values = std::vector<QString>();
			auto message = line.format;
			for (auto k = 0; k != count; ++k) {
				values.push_back((k == j) ? kSelfTransportSecret : fillers[k]);
				message = message.arg(values.back());
			}
			const auto text = DebugEntry(message, line.file);
			const auto from = MessageFrom(text);
			const auto spans = TransportSpans(line, text, from);
			auto matching = 0;
			for (const auto &other : lines) {
				if (!TransportSpans(other, text, from).empty()) {
					++matching;
				}
			}
			auto ok = (matching == 1)
				&& PlainFields(text, from).empty()
				&& (int(spans.size()) == count);
			for (auto k = 0; ok && k != count; ++k) {
				const auto &[spanFrom, spanTill] = spans[k];
				ok = (text.mid(spanFrom, spanTill - spanFrom) == values[k]);
			}
			const auto c = ScanTransportLine(text, TransportSecrets());
			const auto site = TransportSite(head, line.slots[j].name);
			if (TransportGlued(line.format, j)) {
				++glued;
				ok = ok && TransportReads(c, { .embedded = 1 });
			} else if (line.slots[j].computed) {
				++computed;
				ok = ok && TransportReads(c, { .computed = { { site, 1 } } });
			} else {
				++received;
				ok = ok && TransportReads(c, { .plain = { { site, 1 } } });
			}
			++slots;
			if (!ok) {
				failing.push_back(u"%1.%2"_q.arg(i).arg(j));
			}
		}
	}
	Check(
		!lines.empty()
			&& (computed > 0)
			&& (received > 0)
			&& (glued > 0)
			&& failing.isEmpty(),
		u"secrecy self-test: every carried transport row matches only its "
		"own line, and the synthetic number in each of its slots reads as "
		"the slot's class says: computed, deciding at its site, or "
		"embedded"_q,
		u"rows=%1 slots=%2 computed=%3 received=%4 glued=%5 failing=[%6]"_q
			.arg(lines.size())
			.arg(slots)
			.arg(computed)
			.arg(received)
			.arg(glued)
			.arg(failing.join(u", "_q)));
}

// What no row of the transport stage may carry: SelfForbidden(), the
// synthetic number, every other synthetic value it prints in a slot, the
// token and the withholding cases' secrets.
[[nodiscard]] QStringList TransportForbidden() {
	auto result = SelfForbidden();
	result += QStringList{
		kSelfTransportSecret,
		kSelfTransportSize,
		kSelfTransportOther,
		kSelfTransportTime,
		kSelfTransportRequest,
		kSelfTransportToken,
		kSelfHeadSecret,
		kSelfSlotSecret,
		kSelfHeadWord,
		kSelfGapWord,
	};
	for (const auto &word : kSelfSpanPhrase) {
		result.push_back(word);
	}
	return result;
}

[[nodiscard]] QStringList TransportLeaksIn(const QStringList &rows) {
	const auto forbidden = TransportForbidden();
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

// The carried transport lines (TransportLines): a bounded hit that is
// exactly the whole integer of a computed slot is reported at its site and
// does not decide, while every other position, line shape, file and rule
// still decides as before. Never asks the process-wide telemetry request
// and keeps no reading, so the earlier stages' rows and readings stay as
// they were.
void SelfTransport() {
	using Sites = std::map<QString, int>;
	const auto secret = kSelfTransportSecret;
	const auto numSite = TransportSite(kSelfSendingHead, u"num"_q);
	const auto requestSite = TransportSite(kSelfParserHead, u"request"_q);
	const auto ackSite = TransportSite(kSelfAckHead, u"msgId"_q);
	const auto dashSite = TransportSite(kSelfSendingHead, kSiteNoField);
	const auto withheld = WithheldSite();
	const auto positives = QStringList{
		SendingEntry(secret),
		ParserEntry(secret),
	};
	const auto message = SendingMessage(
		kSelfTransportSize,
		secret,
		kSelfTransportTime);
	const auto suffix = u" (session_private.cpp : 37)"_q;
	const auto computedRow = u"SECRECY_COMPUTED: total=2 sites=[DebugLog|"_q
		+ numSite
		+ u" x1, DebugLog|"_q
		+ requestSite
		+ u" x1] (reported, non-deciding)"_q;
	auto readings = std::vector<SecrecyReading>();
	auto runs = 0;
	const auto runCounted = [&](const QString &what, const SelfFixture &f) {
		const auto result = RunFixture(f);
		CheckPrepared(what, result);
		++runs;
		if (result.prepared) {
			readings.push_back(result.reading);
		}
		return result;
	};
	const auto tail = [](const SecrecyReading &r) {
		return u" | "_q
			+ Summary(r)
			+ u" computed="_q
			+ QString::number(r.computed());
	};

	auto principal = SelfRun();
	{
		const auto what = u"secrecy self-test: a synthetic number that is "
			"exactly a computed number of a carried transport line is "
			"reported at its site and does not decide, so a scan whose only "
			"candidates are those lines is clean"_q;
		auto fixture = TransportFixture();
		fixture.parts[u"log_10_00.txt"_q] += positives;
		principal = runCounted(what, fixture);
		const auto &r = principal.reading;
		const auto &debug = ClassOf(r, SecrecyClass::DebugLog);
		Check(
			principal.prepared
				&& r.decided
				&& r.clean
				&& (r.clientWritten() == 0)
				&& (r.computed() == 2)
				&& (debug.boundedComputed == 2)
				&& (debug.boundedOther == 0)
				&& debug.plainSites.empty()
				&& (debug.computedSites == Sites{
					{ numSite, 1 },
					{ requestSite, 1 },
				})
				&& SecrecyRows(r).contains(computedRow),
			what,
			SiteDetails(debug) + tail(r));
	}

	struct Tree {
		QString name;
		Fn<void(SelfFixture&)> plant;
		SecrecyClass cls = SecrecyClass::DebugLog;
		Sites plain;
		Sites computed;
		bool written = false; // its rows are also written to the test log
	};
	const auto addLog = [](SelfFixture &f, const QStringList &lines) {
		f.parts[u"log_10_00.txt"_q] += lines;
	};
	const auto trees = std::vector<Tree>{
		{
			u"is the digits of a negative computed value"_q,
			[=](SelfFixture &f) { addLog(f, { SendingEntry(u"-"_q + secret) }); },
			SecrecyClass::DebugLog,
			{ { dashSite, 1 } },
		},
		{
			u"is the line number of the DEBUG_LOG suffix"_q,
			[=](SelfFixture &f) {
				addLog(f, { SendingEntry(kSelfTransportOther, secret) });
			},
			SecrecyClass::DebugLog,
			{ { dashSite, 1 } },
		},
		{
			u"is a server-sent msgId beside the client's own request id"_q,
			[=](SelfFixture &f) {
				addLog(f, { AckEntry(secret, kSelfTransportRequest) });
			},
			SecrecyClass::DebugLog,
			{ { ackSite, 1 } },
			{},
			true,
		},
		{
			u"is that msgId while its head and field are declared "
				"computed"_q,
			[=](SelfFixture &f) {
				addLog(f, { AckEntry(secret, kSelfTransportRequest) });
				f.computed.push_back({
					.head = kSelfAckHead,
					.field = u"msgId"_q,
				});
			},
			SecrecyClass::DebugLog,
			{ { ackSite, 1 } },
		},
		{
			u"sits in a carried message with an added field"_q,
			[=](SelfFixture &f) {
				const auto added = message + u", seq: "_q + kSelfTransportOther;
				addLog(f, { DebugEntry(added, kSelfPrivateFile) });
			},
			SecrecyClass::DebugLog,
			{ { withheld, 1 } },
		},
		{
			u"is free text after a carried line"_q,
			[=](SelfFixture &f) {
				addLog(f, {
					SendingEntry(kSelfTransportOther) + u" retry "_q + secret,
				});
			},
			SecrecyClass::DebugLog,
			{ { withheld, 1 } },
		},
		{
			u"sits in a carried message whose suffix names a harness "
				"file"_q,
			[=](SelfFixture &f) {
				addLog(f, { DebugEntry(message, u"test_secrecy_scan.cpp"_q) });
			},
			SecrecyClass::DebugLog,
			{ { withheld, 1 } },
		},
		{
			u"is a plain-line leak beside two exempted numbers"_q,
			[=](SelfFixture &f) {
				addLog(f, positives);
				addLog(f, { Entry(u"Selftest Info: value "_q + secret) });
			},
			SecrecyClass::DebugLog,
			{ { withheld, 1 } },
			{ { numSite, 1 }, { requestSite, 1 } },
		},
		{
			u"sits in a carried line written to the test log"_q,
			[=](SelfFixture &f) {
				InsertBeforePlanted(f.testLog, { SendingEntry(secret) });
			},
			SecrecyClass::TestLog,
			{ { WithheldSite(u"test_log.txt"_q), 1 } },
		},
		{
			u"sits in a carried line written to the app log"_q,
			[=](SelfFixture &f) { f.appLog.push_back(SendingEntry(secret)); },
			SecrecyClass::AppLog,
			{ { WithheldSite(u"log.txt"_q), 1 } },
		},
		{
			u"sits in a carried line written to an mtp_ part"_q,
			[=](SelfFixture &f) {
				InsertBeforePlanted(
					f.parts[u"mtp_10_00.txt"_q],
					{ SendingEntry(secret) });
			},
			SecrecyClass::MtpLog,
			{ { WithheldSite(u"DebugLogs/mtp_10_00.txt"_q), 1 } },
		},
	};
	auto received = SelfRun();
	for (const auto &tree : trees) {
		const auto what = u"secrecy self-test: the synthetic number still "
			"fails the scan when it "_q
			+ tree.name;
		auto fixture = TransportFixture();
		tree.plant(fixture);
		const auto result = runCounted(what, fixture);
		const auto &r = result.reading;
		const auto &c = ClassOf(r, tree.cls);
		const auto plain = HitsIn(tree.plain);
		const auto computed = HitsIn(tree.computed);
		Check(
			result.prepared
				&& r.decided
				&& !r.clean
				&& (r.clientWritten() == plain)
				&& (r.computed() == computed)
				&& (c.boundedOther == plain)
				&& (c.boundedComputed == computed)
				&& (c.plainSites == tree.plain)
				&& (c.computedSites == tree.computed),
			what,
			SiteDetails(c) + tail(r));
		if (tree.written) {
			received = result;
		}
	}

	const auto earlierName = u"log_09_00.txt"_q;
	const auto earlierSite = TransportSite(
		kSelfSendingHead,
		u"num"_q,
		u"DebugLogs/"_q + earlierName);
	const auto earlierFixture = [&](bool withControl) {
		auto result = TransportFixture();
		auto part = QStringList{ kSelfToday, SendingEntry(secret) };
		if (withControl) {
			part.push_back(Entry(kSelfEarlierControl));
		}
		result.parts[earlierName] = part;
		result.earlier.push_back({
			.name = earlierName,
			.day = kSelfDay,
			.control = kSelfEarlierControl,
		});
		return result;
	};
	{
		const auto what = u"secrecy self-test: in an accepted earlier log_ "
			"part the same computed number is reported and does not "
			"decide"_q;
		const auto result = runCounted(what, earlierFixture(true));
		const auto &r = result.reading;
		const auto &c = ClassOf(r, SecrecyClass::EarlierParts);
		Check(
			result.prepared
				&& r.decided
				&& r.clean
				&& (r.computed() == 1)
				&& (c.files == 1)
				&& (c.controlHits == 1)
				&& c.decided
				&& (c.boundedComputed == 1)
				&& c.plainSites.empty()
				&& (c.computedSites == Sites{ { earlierSite, 1 } }),
			what,
			SiteDetails(c) + tail(r));
	}
	{
		const auto what = u"secrecy self-test: an earlier log_ part whose "
			"only hit is that exempted number still needs its own control "
			"and is refused as undecided without it"_q;
		const auto result = runCounted(what, earlierFixture(false));
		const auto &r = result.reading;
		const auto &c = ClassOf(r, SecrecyClass::EarlierParts);
		Check(
			result.prepared
				&& !r.decided
				&& (c.files == 1)
				&& (c.withoutControl == 1)
				&& !c.decided
				&& c.undecidedReason.contains(
					u"part without its own control"_q)
				&& (c.boundedComputed == 1)
				&& (c.computedSites == Sites{ { earlierSite, 1 } }),
			what,
			SiteDetails(c) + tail(r));
	}

	CheckTransportSweep();

	const auto only = [](const QString &value) {
		return SecrecySecrets{ .shortSecrets = { value } };
	};
	CheckTransportCases(
		u"secrecy self-test: only a carried slot's whole computed integer is "
		"exempted: a positive, zero or whole negative value is, while the "
		"digits under its minus, a server-sent slot beside it and a glued "
		"slot's digits under a minus decide, and a digit run inside a "
		"longer integer or a glued positive value stays embedded"_q,
		{
			{
				.label = u"positive"_q,
				.text = SendingEntry(secret),
				.expect = { .computed = { { numSite, 1 } } },
			},
			{
				.label = u"zero"_q,
				.text = SendingEntry(u"0"_q),
				.expect = { .computed = { { numSite, 1 } }, .embedded = -1 },
				.secrets = only(u"0"_q),
			},
			{
				.label = u"whole negative"_q,
				.text = SendingEntry(u"-"_q + secret),
				.expect = { .computed = { { numSite, 1 } } },
				.secrets = only(u"-"_q + secret),
			},
			{
				.label = u"digits under a minus"_q,
				.text = SendingEntry(u"-"_q + secret),
				.expect = { .plain = { { dashSite, 1 } } },
			},
			{
				.label = u"two computed slots"_q,
				.text = DebugEntry(
					SendingMessage(secret, secret, kSelfTransportTime),
					kSelfPrivateFile),
				.expect = {
					.computed = {
						{ TransportSite(kSelfSendingHead, u"size"_q), 1 },
						{ numSite, 1 },
					},
				},
			},
			{
				.label = u"computed beside server-sent"_q,
				.text = AckEntry(secret, secret),
				.expect = {
					.computed = {
						{ TransportSite(kSelfAckHead, u"request"_q), 1 },
					},
					.plain = { { ackSite, 1 } },
				},
			},
			{
				.label = u"glued digits under a minus"_q,
				.text = ChangingEntry(u"-"_q + secret, u"4"_q),
				.expect = {
					.plain = {
						{ TransportSite(kSelfChangingHead, kSiteNoField), 1 },
					},
				},
			},
			{
				.label = u"glued positive"_q,
				.text = ChangingEntry(secret, u"4"_q),
				.expect = { .embedded = 1 },
			},
			{
				.label = u"glued server-sent"_q,
				.text = ChangingEntry(u"2"_q, secret),
				.expect = { .embedded = 1 },
			},
			{
				.label = u"longer integer"_q,
				.text = SendingEntry(u"1"_q + secret),
				.expect = { .embedded = 1 },
			},
		});

	{
		auto cases = std::vector<TransportCase>{
			{
				.label = u"window part"_q,
				.text = SendingEntry(secret),
				.expect = { .computed = { { numSite, 1 } } },
			},
			{
				.label = u"another quarter"_q,
				.text = SendingEntry(secret),
				.expect = {
					.computed = {
						{
							TransportSite(
								kSelfSendingHead,
								u"num"_q,
								u"DebugLogs/log_23_45.txt"_q),
							1,
						},
					},
				},
				.file = u"DebugLogs/log_23_45.txt"_q,
			},
		};
		const auto others = QStringList{
			u"test_log.txt"_q,
			u"log.txt"_q,
			u"DebugLogs/mtp_10_00.txt"_q,
			QString(),
			u"log_10_00.txt"_q,
			u"DebugLogs/log_10_00.txt.bak"_q,
			u"logs/DebugLogs/log_10_00.txt"_q,
			u"DebugLogs/log_10_0.txt"_q,
			u"DebugLogs/(invalid name)"_q,
			u"DebugLogs/log_10_00.txt\n"_q,
			u"DebugLogs/log_1"_q + QChar(0x0660) + u"_00.txt"_q,
		};
		for (const auto &file : others) {
			cases.push_back({
				.label = u"file#%1"_q.arg(cases.size()),
				.text = SendingEntry(secret),
				.expect = { .plain = { { WithheldSite(file), 1 } } },
				.file = file,
			});
		}
		CheckTransportCases(
			u"secrecy self-test: the identical carried line is exempted only "
			"under a DebugLogs log_ part name and keeps the withheld site "
			"under every other public file name"_q,
			cases);
	}

	{
		const auto shapes = std::vector<std::pair<QString, QString>>{
			{ u"no entry start"_q, message + suffix },
			{
				u"one-digit thread"_q,
				u"[10:05:00.000 1-0000001] "_q + message + suffix,
			},
			{
				u"six-digit index"_q,
				u"[10:05:00.000 01-000001] "_q + message + suffix,
			},
			{
				u"non-ASCII index digit"_q,
				u"[10:05:00.000 01-000000"_q
					+ QChar(0x0661)
					+ u"] "_q
					+ message
					+ suffix,
			},
			{
				u"no closing bracket"_q,
				u"[10:05:00.000 01-0000001 "_q + message + suffix,
			},
			{
				u"mtp_ prefix"_q,
				u"[10:05:00.000 01-0000001] (dc:2_main) "_q + message + suffix,
			},
			{
				u"app log start"_q,
				u"[2026.01.15 10:05:00] "_q + message + suffix,
			},
			{ u"no suffix"_q, Entry(message) },
			{
				u"another product file"_q,
				DebugEntry(message, u"session.cpp"_q),
			},
			{
				u"harness file"_q,
				DebugEntry(message, u"test_secrecy_scan.cpp"_q),
			},
			{
				u"no line number"_q,
				DebugEntry(message, kSelfPrivateFile, QString()),
			},
			{
				u"non-ASCII line digit"_q,
				DebugEntry(message, kSelfPrivateFile, u"3"_q + QChar(0x0667)),
			},
			{
				u"no closing parenthesis"_q,
				Entry(message + u" (session_private.cpp : 37"_q),
			},
			{
				u"no colon"_q,
				Entry(message + u" (session_private.cpp 37)"_q),
			},
			{
				u"no blank before the suffix"_q,
				Entry(message + u"(session_private.cpp : 37)"_q),
			},
			{ u"trailing text"_q, SendingEntry(secret) + u" retry"_q },
			{ u"trailing blank"_q, SendingEntry(secret) + u" "_q },
		};
		auto cases = std::vector<TransportCase>{
			{
				.label = u"valid"_q,
				.text = Entry(message + suffix),
				.expect = { .computed = { { numSite, 1 } } },
			},
		};
		for (const auto &[label, text] : shapes) {
			cases.push_back({
				.label = label,
				.text = text,
				.expect = { .plain = { { withheld, 1 } } },
			});
		}
		CheckTransportCases(
			u"secrecy self-test: a carried message under a malformed entry "
			"start, an mtp_ or app log prefix, with a wrong or truncated "
			"suffix or with trailing text keeps the withheld site and "
			"decides"_q,
			cases);
	}
	{
		const auto &lines = TransportLines();
		const auto sending = ranges::find_if(lines, [](const TransportLine &l) {
			return (l.file == kSelfPrivateFile)
				&& (l.format
					== u"MTP Info: sending request, size: %1, num: %2, "
					"time: %3"_q);
		});
		const auto found = (sending != end(lines));
		const auto spans = [&](const QString &text) {
			return found
				? TransportSpans(*sending, text, MessageFrom(text))
				: std::vector<std::pair<int, int>>();
		};
		const auto whole = SendingEntry(u"-"_q + secret);
		const auto valid = spans(whole);
		const auto validOk = (valid.size() == 3)
			&& (whole.mid(valid[0].first, valid[0].second - valid[0].first)
				== kSelfTransportSize)
			&& (whole.mid(valid[1].first, valid[1].second - valid[1].first)
				== u"-"_q + secret)
			&& (whole.mid(valid[2].first, valid[2].second - valid[2].first)
				== kSelfTransportTime);
		const auto start = u"MTP Info: sending request, size: "_q
			+ kSelfTransportSize;
		const auto truncated = QStringList{
			Entry(start),
			Entry(start + u", num: "_q),
			DebugEntry(start + u", num:"_q, kSelfPrivateFile),
			DebugEntry(
				start + u", num: , time: "_q + kSelfTransportTime,
				kSelfPrivateFile),
			Entry(message + u" (session_private.cpp :"_q),
			Entry(message + u" (session_private.cpp"_q),
			Entry(message + u" ("_q),
			Entry(message),
		};
		auto failing = QStringList();
		for (auto i = 0; i != int(truncated.size()); ++i) {
			if (!spans(truncated[i]).empty()) {
				failing.push_back(QString::number(i));
			}
		}

		// Synthetic formats, never carried: a well-formed one matches its
		// line, and one whose slots or placeholders are malformed matches
		// nothing, whatever the text.
		const auto pairLine = DebugEntry(
			u"Selftest Info: pair "_q
				+ kSelfTransportSize
				+ u" and "_q
				+ kSelfTransportOther,
			kSelfPrivateFile);
		const auto synthetic = [](const QString &format, int count) {
			auto result = TransportLine{
				.file = kSelfPrivateFile,
				.format = format,
			};
			for (auto i = 0; i != count; ++i) {
				result.slots.push_back(Computed(u"n%1"_q.arg(i)));
			}
			return result;
		};
		const auto wellFormedSpans = TransportSpans(
			synthetic(u"Selftest Info: pair %1 and %2"_q, 2),
			pairLine,
			MessageFrom(pairLine));
		const auto wellFormed = (wellFormedSpans.size() == 2);
		const auto malformed = std::vector<std::pair<TransportLine, QString>>{
			{ synthetic(u"Selftest Info: pair %1 and %2"_q, 1), pairLine },
			{ synthetic(u"Selftest Info: pair %1 and %2"_q, 3), pairLine },
			{ synthetic(u"Selftest Info: pair %2 and %1"_q, 2), pairLine },
			{
				synthetic(u"Selftest Info: pair %10 and %2"_q, 2),
				DebugEntry(
					u"Selftest Info: pair "_q
						+ kSelfTransportSize
						+ u"0 and "_q
						+ kSelfTransportOther,
					kSelfPrivateFile),
			},
			{
				synthetic(u"Selftest Info: pair % %1"_q, 1),
				DebugEntry(
					u"Selftest Info: pair % "_q + kSelfTransportSize,
					kSelfPrivateFile),
			},
		};
		for (auto i = 0; i != int(malformed.size()); ++i) {
			const auto &[line, text] = malformed[i];
			if (!TransportSpans(line, text, MessageFrom(text)).empty()) {
				failing.push_back(u"format#%1"_q.arg(i));
			}
		}
		Check(
			found && validOk && wellFormed && failing.isEmpty(),
			u"secrecy self-test: the carried-line matcher finds every whole "
			"integer of a carried line, its minus included, and matches no "
			"line cut before a number or inside its suffix, nor any line "
			"for a format whose slots or placeholders are malformed"_q,
			u"found=%1 valid=%2 wellFormed=%3 truncated=%4 formats=%5 "
			"failing=[%6]"_q
				.arg(B(found))
				.arg(B(validOk))
				.arg(B(wellFormed))
				.arg(truncated.size())
				.arg(malformed.size())
				.arg(failing.join(u", "_q)));
	}
	{
		const auto with = [](
				const QString &size,
				const QString &num,
				const QString &time,
				const QString &line) {
			return DebugEntry(
				SendingMessage(size, num, time),
				kSelfPrivateFile,
				line);
		};
		const auto size = kSelfTransportSize;
		const auto time = kSelfTransportTime;
		const auto line = kSelfTransportLine;
		const auto numbers = std::vector<std::pair<QString, QString>>{
			{ u"plus sign"_q, with(size, u"+"_q + secret, time, line) },
			{ u"decimal"_q, with(size, secret + u".5"_q, time, line) },
			{ u"doubled minus"_q, with(size, u"--"_q + secret, time, line) },
			{
				u"decimal in another slot"_q,
				with(size + u".25"_q, secret, time, line),
			},
			{
				u"exponent in another slot"_q,
				with(size + u"e3"_q, secret, time, line),
			},
			{ u"empty slot"_q, with(QString(), secret, time, line) },
			{ u"sign-only slot"_q, with(u"-"_q, secret, time, line) },
			{
				u"non-ASCII digit in another slot"_q,
				with(QChar(0x0665) + size.mid(1), secret, time, line),
			},
			{
				u"fullwidth digit in another slot"_q,
				with(size, secret, QChar(0xFF11) + time.mid(1), line),
			},
			{
				u"non-ASCII digit in the suffix"_q,
				with(size, secret, time, u"3"_q + QChar(0x0667)),
			},
		};
		auto cases = std::vector<TransportCase>();
		for (const auto &[label, text] : numbers) {
			cases.push_back({
				.label = label,
				.text = text,
				.expect = { .plain = { { withheld, 1 } } },
			});
		}
		CheckTransportCases(
			u"secrecy self-test: a carried message with a malformed number "
			"(a plus sign, a decimal or exponent, a doubled minus, an empty "
			"or sign-only slot, a digit outside ASCII) keeps the withheld "
			"site and decides"_q,
			cases);
	}
	CheckTransportCases(
		u"secrecy self-test: a carried site that would print a given "
		"secret, through its head, its slot name or a phrase across both, "
		"and a hit before its first number are withheld and still decide"_q,
		{
			{
				.label = u"control"_q,
				.text = KeyCreationEntry(secret),
				.expect = {
					.computed = {
						{ TransportSite(kSelfKeyHead, u"temporaryKeyId"_q), 1 },
					},
				},
			},
			{
				.label = u"head secret"_q,
				.text = KeyCreationEntry(secret),
				.expect = { .plain = { { withheld, 1 } } },
				.secrets = { .shortSecrets = { secret, kSelfHeadSecret } },
			},
			{
				.label = u"slot secret"_q,
				.text = KeyCreationEntry(secret),
				.expect = { .plain = { { withheld, 1 } } },
				.secrets = { .shortSecrets = { secret, kSelfSlotSecret } },
			},
			{
				.label = u"phrase across head and slot"_q,
				.text = KeyCreationEntry(secret),
				.expect = { .plain = { { withheld, 1 } } },
				.secrets = {
					.phrases = { kSelfSpanPhrase },
					.shortSecrets = { secret },
				},
			},
			{
				.label = u"hit in the head"_q,
				.text = KeyCreationEntry(kSelfTransportRequest),
				.expect = { .plain = { { withheld, 1 } } },
				.secrets = only(kSelfHeadWord),
			},
			{
				.label = u"hit before the first number"_q,
				.text = QueueEntry(),
				.expect = { .plain = { { withheld, 1 } } },
				.secrets = only(kSelfGapWord),
			},
		},
		true);
	{
		const auto body = u"      message: \""_q
			+ message
			+ suffix
			+ u"\" [STRING]"_q;
		const auto title = u"        title: \""_q
			+ message
			+ suffix
			+ u"\" [STRING]"_q;
		auto failing = QStringList();
		const auto token = ScanTransportLine(
			DebugEntry(
				u"MTP Info: sending ping, ping_id: "_q + kSelfTransportToken,
				kSelfPrivateFile),
			{ .tokens = { kSelfTransportToken } });
		if (token.tokensOther != 1
			|| token.bounded
			|| !token.computedSites.empty()) {
			failing.push_back(u"token"_q);
		}
		const auto words = ScanTransportLine(
			DebugEntry(
				u"MTP Info: sending ping, ping_id: "_q + kSelfTransportSize,
				kSelfPrivateFile),
			{ .phrases = { { u"sending"_q, u"ping"_q } } });
		if (words.wordRunsOther != 1 || words.bounded) {
			failing.push_back(u"word run"_q);
		}
		const auto send = ScanTransportLine(
			SendEntry({
				u"    query: { messages_sendMessage"_q,
				body,
				u"    }"_q,
			}).join(QChar('\n')),
			TransportSecrets());
		const auto sendSite = u"messages_sendMessage/messages_sendMessage"
			".message"_q;
		if (send.boundedSend != 1
			|| send.boundedOther
			|| send.boundedComputed
			|| (send.sendSites != Sites{ { sendSite, 1 } })) {
			failing.push_back(u"Send body"_q);
		}
		const auto recv = ScanTransportLine(
			RecvEntry({
				u"    topics: [ vector<0x0>"_q,
				u"      { forumTopic"_q,
				title,
				u"      }"_q,
				u"    ]"_q,
			}).join(QChar('\n')),
			TransportSecrets());
		const auto recvSite = u"messages_forumTopics/forumTopic.title"_q;
		if (recv.boundedRecv != 1
			|| recv.boundedOther
			|| recv.boundedComputed
			|| (recv.recvSites != Sites{ { recvSite, 1 } })) {
			failing.push_back(u"Recv body"_q);
		}
		const auto declared = ScanTransportLine(
			AckEntry(secret, kSelfTransportRequest),
			TransportSecrets(),
			kSelfTransportPart,
			{ { .head = kSelfAckHead, .field = u"msgId"_q } });
		if (!TransportReads(declared, { .plain = { { ackSite, 1 } } })) {
			failing.push_back(u"declared msgId"_q);
		}
		Check(
			failing.isEmpty(),
			u"secrecy self-test: on a carried transport line a token, a word "
			"run and a declared server-sent slot still decide, and the "
			"line's text in a Send or Recv body keeps that entry's "
			"direction"_q,
			u"cases=5 failing=[%1]"_q.arg(failing.join(u", "_q)));
	}

	{
		auto rows = QStringList();
		for (const auto &reading : readings) {
			rows += SecrecyRows(reading);
		}
		const auto leaks = TransportLeaksIn(rows);
		const auto joined = rows.join(QChar('\n'));
		const auto sitesPrinted = joined.contains(computedRow)
			&& joined.contains(ackSite + u" x1"_q)
			&& joined.contains(dashSite + u" x1"_q)
			&& joined.contains(withheld + u" x1"_q);
		Check(
			(runs > 0)
				&& (int(readings.size()) == runs)
				&& !rows.isEmpty()
				&& leaks.isEmpty()
				&& sitesPrinted,
			u"secrecy self-test: no row of the carried transport readings "
			"carries the synthetic number, another synthetic value or a "
			"scanned line, while they print a computed, a server-sent, a "
			"slotless and a withheld site"_q,
			u"readings=%1/%2 rows=%3 sitesPrinted=%4 hits=[%5]"_q
				.arg(readings.size())
				.arg(runs)
				.arg(rows.size())
				.arg(B(sitesPrinted))
				.arg(leaks.join(u", "_q)));
	}
	{
		const auto path = TestLogPath();
		const auto mark = QFileInfo(path).size();
		auto written = SecrecyRows(principal.reading);
		written += SecrecyRows(received.reading);
		for (const auto &row : written) {
			Note(row);
		}
		const auto appended = AppendedSince(path, mark);
		const auto leaks = TransportLeaksIn({ appended });
		Check(
			principal.prepared
				&& received.prepared
				&& !appended.isEmpty()
				&& appended.contains(computedRow)
				&& appended.contains(ackSite + u" x1"_q)
				&& leaks.isEmpty(),
			u"secrecy self-test: the carried transport rows as written to "
			"test_log.txt carry none of them either"_q,
			u"appended=%1 rows=%2 hits=[%3]"_q
				.arg(B(!appended.isEmpty()))
				.arg(written.size())
				.arg(leaks.join(u", "_q)));
	}
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
	result.selection = LaunchLogSelection::InvalidTimes;
	if (launchStart.isValid() && scanAt.isValid()) {
		const auto reversed = (scanAt < launchStart);
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
		result.selection = reversed
			? LaunchLogSelection::ReversedWindow
			: (part <= scanAt)
			? LaunchLogSelection::CappedWindow
			: LaunchLogSelection::Complete;
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

Internal::LogScanReading Internal::ReadLogClasses(
		const SecrecyLaunchLogs &logs,
		const QString &plantedControl,
		const SecrecySecrets *usable,
		const std::vector<SecrecyComputedField> &computedFields,
		LogScanObserver *observer) {
	auto result = LogScanReading();
	const auto matcher = SecrecyMatcher(
		usable ? *usable : SecrecySecrets(),
		computedFields);

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
				++result.candidates;
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
			ReadSource(
				source,
				matcher,
				plantedControl,
				reading,
				observer,
				usable != nullptr);
		}
		DecideClass(reading);
		result.classes.push_back(std::move(reading));
	}

	return result;
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
	ReadCanaries(usable, result);

	auto scan = Internal::ReadLogClasses(
		logs,
		plantedControl,
		&usable,
		computedFields);
	result.classes = std::move(scan.classes);
	result.accepted = scan.accepted;
	result.foreign = scan.foreign;
	result.missing = scan.missing;
	result.unreadable = scan.unreadable;
	result.candidateIdentities = std::move(scan.candidateIdentities);
	for (const auto &reading : result.classes) {
		if (!reading.decided) {
			result.undecidedClasses.push_back(SecrecyClassName(reading.cls)
				+ u": "_q
				+ reading.undecidedReason);
		}
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
		{ u"secrecy_self_telemetry"_q, [] { SelfTelemetry(); } },
		{ u"secrecy_self_helper_numbers"_q, [] { SelfHelperNumbers(); } },
		{ u"secrecy_self_transport"_q, [] { SelfTransport(); } },
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
