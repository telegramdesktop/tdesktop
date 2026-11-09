/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_widget_conformance_bridge.h"

#include "test/test_widget_fixture.h"

#include <QtCore/QChar>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonParseError>
#include <QtCore/QJsonValue>
#include <QtCore/QLocale>
#include <QtCore/QRegularExpression>
#include <QtCore/QRegularExpressionMatch>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace Test {
namespace {

// Clip() in tgw_runtime.cpp cuts each field of a summary line to this many
// characters, its "..." included.
constexpr auto kClipLength = 240;

// WidgetHost keeps only its first kMaxRecordedCalls bridge calls
// (widget_host.cpp): a count this high may have lost later calls.
constexpr auto kHostCallCap = 1000;

// The methods sdk::checkBridgeCall() accepts, the only names a recorded
// call can have, and the levels the sandbox's console logs at.
constexpr auto kMethods = std::array{
	"ready",
	"openLink",
	"openTelegramLink",
	"haptic",
};
constexpr auto kLevels = std::array{
	"log",
	"info",
	"warn",
	"error",
	"debug",
};

// The excerpt of a line a refusal quotes.
constexpr auto kExcerpt = 80;

struct ConsoleHeader {
	int count = -1;
	bool dropped = false;
};

struct ConsoleEntry {
	QString level;
	QString text;
};

[[nodiscard]] QString NumberText(qint64 value) {
	return QString::number(value);
}

[[nodiscard]] QString CompactJson(const QJsonObject &object) {
	return QString::fromUtf8(
		QJsonDocument(object).toJson(QJsonDocument::Compact));
}

// The N of a line that is exactly of |pattern|'s form, or -1.
[[nodiscard]] int MatchedCount(
		const QRegularExpression &pattern,
		const QString &line) {
	const auto match = pattern.match(line);
	return match.hasMatch() ? match.captured(1).toInt() : -1;
}

[[nodiscard]] int BridgeHeaderCount(const QString &line) {
	static const auto pattern = QRegularExpression(
		u"^Bridge calls: (\\d{1,9})\\.$"_q);
	return MatchedCount(pattern, line);
}

[[nodiscard]] int OmittedCount(const QString &line) {
	static const auto pattern = QRegularExpression(
		u"^\\.\\.\\. (\\d{1,9}) earlier entries not shown\\.$"_q);
	return MatchedCount(pattern, line);
}

[[nodiscard]] ConsoleHeader ReadConsoleHeader(const QString &line) {
	static const auto pattern = QRegularExpression(
		u"^Console: (\\d{1,9}) entries(, the host dropped all later "
		u"ones)?\\.$"_q);
	const auto match = pattern.match(line);
	if (!match.hasMatch()) {
		return ConsoleHeader();
	}
	return {
		.count = match.captured(1).toInt(),
		.dropped = !match.captured(2).isEmpty(),
	};
}

template <std::size_t Size>
[[nodiscard]] bool Listed(
		const std::array<const char*, Size> &list,
		const QString &name) {
	return std::any_of(begin(list), end(list), [&](const char *entry) {
		return (name == QString::fromLatin1(entry));
	});
}

// The console level a line starts an entry at ("<level>: "), or empty.
[[nodiscard]] QString LevelOf(const QString &line) {
	for (const auto level : kLevels) {
		const auto name = QString::fromLatin1(level);
		if (line.startsWith(name + u": "_q)) {
			return name;
		}
	}
	return QString();
}

// A field Clip() cut: it ends in its "..." at the clip length, or one
// shorter when a surrogate pair would have been split.
[[nodiscard]] bool Clipped(const QString &text) {
	return text.endsWith(u"..."_q) && (text.size() >= kClipLength - 1);
}

// Adds the refusal |name| once; a repeat keeps the first detail.
void Refuse(
		ConformanceSummary &summary,
		const QString &name,
		const QString &detail) {
	const auto known = std::any_of(
		begin(summary.refusals),
		end(summary.refusals),
		[&](const ConformanceRefusal &refusal) {
			return (refusal.name == name);
		});
	if (!known) {
		summary.refusals.push_back({ .name = name, .detail = detail });
	}
}

// One line after the bridge header and its omission line: a call, or the
// refusal that names why it is none.
void ReadCallLine(
		const QString &line,
		const QString &where,
		ConformanceSummary &summary,
		std::vector<ConformanceBridgeCall> &calls) {
	const auto separator = line.indexOf(u": "_q);
	if (separator <= 0) {
		Refuse(
			summary,
			u"bridge-section-ambiguous"_q,
			where + u" is not a \"<method>: <args>\" call line"_q);
		return;
	}
	const auto method = line.left(separator);
	if (!Listed(kMethods, method)) {
		Refuse(
			summary,
			u"bridge-unknown-method"_q,
			where + u" names a method the host accepts no call of"_q);
		return;
	}
	const auto args = line.mid(separator + 2);
	auto parse = QJsonParseError();
	const auto document = QJsonDocument::fromJson(args.toUtf8(), &parse);
	const auto parsed = (parse.error == QJsonParseError::NoError);
	if (parsed && document.isArray()) {
		calls.push_back({ .method = method, .args = document.array() });
	} else if (!parsed && Clipped(args)) {
		Refuse(
			summary,
			u"bridge-args-clipped"_q,
			where + u": the formatter cut the arguments at %1 characters, "
				u"so they are no JSON array"_q.arg(NumberText(kClipLength)));
	} else {
		Refuse(
			summary,
			u"bridge-args-not-array"_q,
			where + u": the arguments are not a JSON array (%1)"_q.arg(
				parsed
					? u"another JSON value"_q
					: parse.errorString()));
	}
}

// The bridge section, from its genuine header at line |header| to the end.
void ParseBridge(
		const QStringList &lines,
		int header,
		ConformanceSummary &summary) {
	const auto size = int(lines.size());
	const auto before = summary.refusals.size();
	summary.bridgeCount = BridgeHeaderCount(lines[header]);
	auto index = header + 1;
	if ((index < size) && (OmittedCount(lines[index]) >= 0)) {
		summary.bridgeOmitted = OmittedCount(lines[index]);
		++index;
	}
	auto calls = std::vector<ConformanceBridgeCall>();
	for (; index < size; ++index) {
		++summary.bridgeLines;
		ReadCallLine(
			lines[index],
			u"line %1 of the bridge section [%2]"_q.arg(
				NumberText(index - header),
				lines[index].left(kExcerpt)),
			summary,
			calls);
	}
	if (summary.bridgeLines + summary.bridgeOmitted != summary.bridgeCount) {
		Refuse(
			summary,
			u"bridge-count-mismatch"_q,
			u"the header counts %1 calls, the section shows %2 lines and "
				u"says %3 earlier ones are not shown"_q.arg(
					NumberText(summary.bridgeCount),
					NumberText(summary.bridgeLines),
					NumberText(summary.bridgeOmitted)));
	}
	if (summary.bridgeOmitted > 0) {
		Refuse(
			summary,
			u"bridge-tail-omitted"_q,
			u"the formatter left out the %1 earliest of %2 calls"_q.arg(
				NumberText(summary.bridgeOmitted),
				NumberText(summary.bridgeCount)));
	}
	if (summary.bridgeCount >= kHostCallCap) {
		Refuse(
			summary,
			u"bridge-host-saturated"_q,
			u"%1 calls reach the host's record cap of %2: any later call "
				u"was not recorded"_q.arg(
					NumberText(summary.bridgeCount),
					NumberText(kHostCallCap)));
	}
	if (summary.refusals.size() == before) {
		summary.bridge = std::move(calls);
	}
}

// The console section: the one line of its header's form before the
// bridge section's header at line |bridge|, up to that header.
void ParseConsole(
		const QStringList &lines,
		int bridge,
		ConformanceSummary &summary) {
	auto header = -1;
	for (auto i = 0; i != bridge; ++i) {
		if (ReadConsoleHeader(lines[i]).count >= 0) {
			++summary.consoleHeaders;
			header = i;
		}
	}
	if (summary.consoleHeaders != 1) {
		Refuse(
			summary,
			u"console-ambiguous"_q,
			u"%1 lines before the bridge section have the console header's "
				u"form, so none can be taken for it"_q.arg(
					NumberText(summary.consoleHeaders)));
		return;
	}
	const auto parsed = ReadConsoleHeader(lines[header]);
	summary.consoleCount = parsed.count;
	summary.consoleDropped = parsed.dropped;
	auto index = header + 1;
	if ((index < bridge) && (OmittedCount(lines[index]) >= 0)) {
		summary.consoleOmitted = OmittedCount(lines[index]);
		++index;
	}
	auto entries = std::vector<ConsoleEntry>();
	auto orphan = false;
	for (; index < bridge; ++index) {
		const auto &line = lines[index];
		const auto level = LevelOf(line);
		if (!level.isEmpty()) {
			entries.push_back({ level, line.mid(level.size() + 2) });
			continue;
		}
		++summary.consoleContinuations;
		if (entries.empty()) {
			orphan = true;
		} else {
			entries.back().text += QChar('\n') + line;
		}
	}
	summary.consoleEntries = int(entries.size());
	const auto shown = summary.consoleCount - summary.consoleOmitted;
	if (orphan || (summary.consoleEntries != shown)) {
		Refuse(
			summary,
			u"console-ambiguous"_q,
			u"%1 lines start an entry at a console level and %2 continue "
				u"one, but the header shows %3 of %4 entries: a continued "
				u"entry's line that reads as an entry cannot be told from "
				u"one"_q.arg(
					NumberText(summary.consoleEntries),
					NumberText(summary.consoleContinuations),
					NumberText(shown),
					NumberText(summary.consoleCount)));
		return;
	}
	auto errors = QStringList();
	for (const auto &entry : entries) {
		if ((entry.level == u"error"_q)
			&& entry.text.startsWith(u"Uncaught "_q)) {
			errors.push_back(entry.text);
			if (Clipped(entry.text)) {
				++summary.errorsClipped;
			}
		}
	}
	summary.errors = std::move(errors);
}

[[nodiscard]] QJsonArray CallsArray(
		const std::vector<ConformanceBridgeCall> &calls) {
	auto result = QJsonArray();
	for (const auto &call : calls) {
		result.push_back(QJsonObject{
			{ u"method"_q, call.method },
			{ u"args"_q, call.args },
		});
	}
	return result;
}

// ---------------------------------------------------------------------
// What the run file and the totals may claim.

// JavaScript's truthiness of a manifest field, as report.mjs tests it.
[[nodiscard]] bool Truthy(const QJsonValue &value) {
	switch (value.type()) {
	case QJsonValue::Bool: return value.toBool();
	case QJsonValue::Double: {
		const auto number = value.toDouble();
		return (number != 0.) && !std::isnan(number);
	}
	case QJsonValue::String: return !value.toString().isEmpty();
	case QJsonValue::Array:
	case QJsonValue::Object: return true;
	case QJsonValue::Null:
	case QJsonValue::Undefined: return false;
	}
	return false;
}

// |value| as JavaScript's String() writes it, for a finite height: an
// integer without a fraction, otherwise the shortest round trip.
[[nodiscard]] QString JsNumber(double value) {
	if ((std::floor(value) == value) && (std::abs(value) < 1e15)) {
		return QString::number(qint64(value));
	}
	return QString::number(value, 'g', QLocale::FloatingPointShortest);
}

[[nodiscard]] QJsonObject HostJudgement(
		const QJsonObject &check,
		const QString &status,
		const QString &detail) {
	return QJsonObject{
		{ u"id"_q, check.value(u"id"_q) },
		{ u"name"_q, check.value(u"name"_q) },
		{ u"status"_q, status },
		{ u"detail"_q, detail },
	};
}

// One check of report.mjs's hostChecks(), in its order and with its texts,
// for a run whose bridge calls are unknown: a check counting them is not
// observed, |unknown| says why, and no count of them is ever given.
[[nodiscard]] QJsonObject JudgeHostCheck(
		const QJsonObject &check,
		const ConformanceObservation &observation,
		const QString &unknown) {
	if (Truthy(check.value(u"gesture"_q)) && observation.syntheticInput) {
		return HostJudgement(
			check,
			u"skip"_q,
			u"synthetic input grants no user gesture"_q);
	} else if (Truthy(check.value(u"bridge"_q))) {
		return HostJudgement(check, u"skip"_q, unknown);
	}
	auto ok = true;
	auto detail = QStringList();
	const auto height = check.value(u"height"_q);
	if (Truthy(height)) {
		if (!observation.height) {
			return HostJudgement(check, u"skip"_q, u"height not observed"_q);
		}
		const auto value = *observation.height;
		const auto min = height.toObject().value(u"min"_q);
		const auto max = height.toObject().value(u"max"_q);
		ok = ok
			&& (min.isUndefined() || (value >= min.toDouble()))
			&& (max.isUndefined() || (value <= max.toDouble()));
		detail.push_back(u"height "_q + JsNumber(value));
	}
	if (check.value(u"focus"_q).toString() == u"chat"_q) {
		if (!observation.chatFocused) {
			return HostJudgement(
				check,
				u"skip"_q,
				u"chat focus not observed"_q);
		}
		const auto focused = *observation.chatFocused;
		ok = ok && focused;
		detail.push_back(focused
			? u"chat input kept the focus"_q
			: (u"focus moved to "_q + (observation.focusHolder.isEmpty()
				? u"the widget"_q
				: observation.focusHolder)));
	}
	const auto status = Truthy(check.value(u"xfail"_q))
		? (ok ? u"xpass"_q : u"xfail"_q)
		: (ok ? u"pass"_q : u"fail"_q);
	return HostJudgement(check, status, detail.join(u"; "_q));
}

// Why |run|'s tap reading cannot show that every host message of the run
// was scanned, or empty.
[[nodiscard]] QString TapUncovered(const ConformanceCoverage &run) {
	const auto &reading = run.reading;
	if (!run.tapped) {
		return u"no tap was armed for it"_q;
	} else if (reading.factoriesSelected <= 0) {
		return u"its tap wrapped no host"_q;
	} else if (!run.final) {
		return u"its tap reading is not final (factories %1 of %2 released, "
			u"wrappers %3 of %4 destroyed)"_q.arg(
				NumberText(reading.factoriesReleased),
				NumberText(reading.factoriesSelected),
				NumberText(reading.wrappersDestroyed),
				NumberText(reading.wrappersBuilt));
	}
	return QString();
}

// Why |run|'s tap reading cannot show that its widget leaked no URL into
// the validated ops, or empty.
[[nodiscard]] QString LeakUncovered(const ConformanceCoverage &run) {
	const auto tap = TapUncovered(run);
	if (!tap.isEmpty()) {
		return tap;
	} else if (run.reading.scanGaps > 0) {
		return u"%1 of its host messages were not scanned"_q.arg(
			NumberText(run.reading.scanGaps));
	} else if (run.reading.batches <= 0) {
		return u"no validated ops batch of it was scanned"_q;
	}
	return QString();
}

[[nodiscard]] QString BridgeUncovered(const ConformanceCoverage &run) {
	return run.bridge
		? QString()
		: run.bridgeRefusals.isEmpty()
		? u"its runtime summary was not read"_q
		: (u"its bridge reading was refused: "_q
			+ run.bridgeRefusals.join(u", "_q));
}

[[nodiscard]] int TelegramLinks(const ConformanceCoverage &run) {
	if (!run.bridge) {
		return 0;
	}
	return int(std::count_if(
		begin(*run.bridge),
		end(*run.bridge),
		[](const ConformanceBridgeCall &call) {
			return (call.method == u"openTelegramLink"_q);
		}));
}

void Cover(
		ConformanceAbsence &absence,
		const QString &key,
		const QString &uncovered,
		int found) {
	++absence.selected;
	absence.found += found;
	if (!uncovered.isEmpty()) {
		absence.uncovered.push_back(key + u": "_q + uncovered);
	}
}

void Certify(ConformanceAbsence &absence, bool discriminates) {
	absence.certified = discriminates
		&& (absence.selected > 0)
		&& absence.uncovered.isEmpty()
		&& !absence.found;
}

// ---------------------------------------------------------------------
// Self-checks.

// Clip() of tgw_runtime.cpp, for the plain text of these controls.
[[nodiscard]] QString ClipLikeSummary(QString text) {
	if (text.size() <= kClipLength) {
		return text;
	}
	text.truncate(kClipLength - 3);
	return text + u"..."_q;
}

// A summary in Summarize()'s order: the state lines, no diagnostic kinds,
// the console header and |console|, the bridge header and |bridge|.
[[nodiscard]] QString LiteralSummary(
		int consoleCount,
		const QStringList &console,
		int bridgeCount,
		const QStringList &bridge) {
	auto lines = QStringList{
		u"Loaded: yes, rendered: yes, nodes: 12, height: 120."_q,
		u"Timing: renderer ready 4 ms, loaded 21 ms, rendered 30 ms, "
			u"first content 31 ms."_q,
		u"Engine: 9 tasks, 3 ms of JS, 640 KB of memory, 0 throttled "
			u"tasks."_q,
		u"Renderer: 40 ops, 2 queries in 0 ms (0 cached), 0 missed "
			u"mutations."_q,
		u"Diagnostics: 0 kinds."_q,
		u"Console: %1 entries."_q.arg(NumberText(consoleCount)),
	};
	lines.append(console);
	lines.push_back(u"Bridge calls: %1."_q.arg(NumberText(bridgeCount)));
	lines.append(bridge);
	return lines.join(QChar('\n'));
}

[[nodiscard]] QStringList RefusalNames(const ConformanceSummary &summary) {
	auto result = QStringList();
	for (const auto &refusal : summary.refusals) {
		result.push_back(refusal.name);
	}
	result.sort();
	return result;
}

[[nodiscard]] bool RefusedExactly(
		const ConformanceSummary &summary,
		QStringList names) {
	names.sort();
	return (RefusalNames(summary) == names);
}

// The reading with its calls, errors and refusal details, as one text.
[[nodiscard]] QString ReadingText(const ConformanceSummary &summary) {
	auto object = ConformanceSummaryObject(summary);
	auto details = QJsonArray();
	for (const auto &refusal : summary.refusals) {
		details.push_back(refusal.name + u": "_q + refusal.detail);
	}
	object.insert(u"refusalDetails"_q, details);
	object.insert(u"calls"_q, summary.bridge
		? QJsonValue(CallsArray(*summary.bridge))
		: QJsonValue(QJsonValue::Null));
	object.insert(u"errorTexts"_q, summary.errors
		? QJsonValue(QJsonArray::fromStringList(*summary.errors))
		: QJsonValue(QJsonValue::Null));
	return CompactJson(object);
}

[[nodiscard]] WidgetOpsSelfCheckRow CheckRow(
		bool passed,
		const QString &name,
		const std::vector<ConformanceSummary> &readings) {
	auto details = QStringList();
	for (const auto &reading : readings) {
		details.push_back(ReadingText(reading));
	}
	return {
		.name = u"summary reader self-check: "_q + name,
		.details = details.join(QChar(' ')),
		.passed = passed,
	};
}

[[nodiscard]] WidgetOpsSelfCheckRow CheckAccepted() {
	const auto uncaught = u"Uncaught TypeError: not a function\n"
		u"    at step (widget.html:3)"_q;
	const auto summary = ParseConformanceSummary(LiteralSummary(
		3,
		{
			u"log: started"_q,
			u"error: "_q + uncaught,
			u"error: logged, never thrown"_q,
		},
		3,
		{
			u"ready: []"_q,
			u"openLink: [\"https://example.org/conformance/tap\"]"_q,
			u"haptic: [\"impact\",\"light\"]"_q,
		}));
	const auto calls = QJsonArray{
		QJsonObject{
			{ u"method"_q, u"ready"_q },
			{ u"args"_q, QJsonArray() },
		},
		QJsonObject{
			{ u"method"_q, u"openLink"_q },
			{
				u"args"_q,
				QJsonArray{ u"https://example.org/conformance/tap"_q },
			},
		},
		QJsonObject{
			{ u"method"_q, u"haptic"_q },
			{ u"args"_q, QJsonArray{ u"impact"_q, u"light"_q } },
		},
	};
	const auto passed = summary.refusals.empty()
		&& summary.bridge
		&& (CallsArray(*summary.bridge) == calls)
		&& summary.errors
		&& (*summary.errors == QStringList{ uncaught })
		&& (summary.consoleContinuations == 1);
	return CheckRow(
		passed,
		u"accepted calls keep their order and their arguments as JSON "
			u"arrays, and the one uncaught error is read whole, its stack "
			u"line included, while a logged error is none"_q,
		{ summary });
}

[[nodiscard]] WidgetOpsSelfCheckRow CheckArguments() {
	const auto url = u"https://example.org/"_q + QString(300, QChar('a'));
	const auto clipped = ParseConformanceSummary(LiteralSummary(
		0,
		{},
		2,
		{
			u"ready: []"_q,
			u"openLink: "_q + ClipLikeSummary(u"[\""_q + url + u"\"]"_q),
		}));
	const auto objects = ParseConformanceSummary(LiteralSummary(
		0,
		{},
		2,
		{
			u"openLink: {\"url\":\"https://example.org/x\"}"_q,
			u"haptic: \"impact\""_q,
		}));
	const auto passed = RefusedExactly(clipped, { u"bridge-args-clipped"_q })
		&& !clipped.bridge
		&& RefusedExactly(objects, { u"bridge-args-not-array"_q })
		&& !objects.bridge;
	return CheckRow(
		passed,
		u"arguments the formatter clipped and arguments that are no JSON "
			u"array are refused by name, and no call of their section is "
			u"kept"_q,
		{ clipped, objects });
}

[[nodiscard]] WidgetOpsSelfCheckRow CheckSection() {
	const auto haptic = u"haptic: [\"impact\",\"light\"]"_q;
	const auto missing = ParseConformanceSummary(
		LiteralSummary(0, {}, 0, {}).section(QChar('\n'), 0, -2));
	const auto mismatch = ParseConformanceSummary(
		LiteralSummary(0, {}, 2, { u"ready: []"_q }));
	const auto omitted = ParseConformanceSummary(LiteralSummary(
		0,
		{},
		30,
		{ u"... 28 earlier entries not shown."_q, haptic, haptic }));
	const auto saturated = ParseConformanceSummary(LiteralSummary(
		0,
		{},
		1000,
		{ u"... 998 earlier entries not shown."_q, haptic, haptic }));
	const auto unknown = ParseConformanceSummary(
		LiteralSummary(0, {}, 1, { u"share: []"_q }));
	const auto passed = RefusedExactly(
			missing,
			{ u"bridge-section-missing"_q, u"console-ambiguous"_q })
		&& RefusedExactly(mismatch, { u"bridge-count-mismatch"_q })
		&& RefusedExactly(omitted, { u"bridge-tail-omitted"_q })
		&& RefusedExactly(
			saturated,
			{ u"bridge-tail-omitted"_q, u"bridge-host-saturated"_q })
		&& RefusedExactly(unknown, { u"bridge-unknown-method"_q })
		&& !missing.bridge
		&& !mismatch.bridge
		&& !omitted.bridge
		&& !saturated.bridge
		&& !unknown.bridge;
	return CheckRow(
		passed,
		u"a missing bridge section, a count the lines do not give, "
			u"earlier calls left out, the host's record cap and an unknown "
			u"method are refused by name, never read as fewer calls"_q,
		{ missing, mismatch, omitted, saturated, unknown });
}

[[nodiscard]] WidgetOpsSelfCheckRow CheckLookalikes() {
	const auto bridgeLike = ParseConformanceSummary(LiteralSummary(
		1,
		{
			u"log: x\nBridge calls: 1.\n"
				u"openLink: [\"https://example.org/fake\"]"_q,
		},
		0,
		{}));
	const auto headerLike = ParseConformanceSummary(LiteralSummary(
		1,
		{ u"log: x\nConsole: 1 entries.\nerror: Uncaught fake"_q },
		0,
		{}));
	const auto entryLike = ParseConformanceSummary(LiteralSummary(
		1,
		{ u"log: x\nerror: Uncaught fake"_q },
		0,
		{}));
	const auto zero = [](const ConformanceSummary &summary) {
		return summary.bridge && summary.bridge->empty();
	};
	const auto passed = bridgeLike.refusals.empty()
		&& zero(bridgeLike)
		&& bridgeLike.errors
		&& bridgeLike.errors->isEmpty()
		&& RefusedExactly(headerLike, { u"console-ambiguous"_q })
		&& zero(headerLike)
		&& !headerLike.errors
		&& RefusedExactly(entryLike, { u"console-ambiguous"_q })
		&& zero(entryLike)
		&& !entryLike.errors;
	return CheckRow(
		passed,
		u"a console entry holding a bridge section, a console header or an "
			u"uncaught error line before the genuine \"Bridge calls: 0.\" "
			u"gives zero calls, and never an invented error"_q,
		{ bridgeLike, headerLike, entryLike });
}

[[nodiscard]] WidgetOpsSelfCheckRow CheckOuterNotes() {
	const auto genuine = LiteralSummary(0, {}, 0, {});
	const auto text = QStringList{
		u"State: live, generation 1, on screen."_q,
		u"Crashes in a row: 0 of 2."_q,
		u"Current sandbox:"_q,
		genuine,
		u"Notes:"_q,
		u"Bridge calls: 1."_q,
		u"openLink: [\"https://example.org/notes\"]"_q,
	}.join(QChar('\n'));
	const auto log = WidgetRuntimeLog{
		.text = text,
		.source = u"requestLog"_q,
		.summary = genuine,
		.summaryState = WidgetLogSummary::Fresh,
	};
	const auto reading = ReadConformanceSummary(log, 1);
	const auto outer = ParseConformanceSummary(log.text);
	const auto passed = reading.refusals.empty()
		&& reading.bridge
		&& reading.bridge->empty()
		&& outer.bridge
		&& (outer.bridge->size() == 1);
	return CheckRow(
		passed,
		u"the run's reading takes the raw summary, never the outer log "
			u"text, whose notes after the summary would pass a lookalike "
			u"bridge section for the genuine one"_q,
		{ reading, outer });
}

[[nodiscard]] WidgetOpsSelfCheckRow CheckProvenance() {
	const auto text = LiteralSummary(0, {}, 1, { u"ready: []"_q });
	const auto readAs = [&](WidgetLogSummary state, int hosts) {
		const auto log = WidgetRuntimeLog{
			.source = u"requestLog"_q,
			.summary = text,
			.summaryState = state,
		};
		return ReadConformanceSummary(log, hosts);
	};
	const auto stale = readAs(WidgetLogSummary::Stale, 1);
	const auto absent = readAs(WidgetLogSummary::Absent, 1);
	const auto several = readAs(WidgetLogSummary::Fresh, 2);
	const auto last = readAs(WidgetLogSummary::Final, 1);
	const auto unread = [](const ConformanceSummary &summary) {
		return !summary.bridge && !summary.errors;
	};
	const auto passed = RefusedExactly(stale, { u"summary-stale"_q })
		&& unread(stale)
		&& RefusedExactly(absent, { u"summary-unavailable"_q })
		&& unread(absent)
		&& RefusedExactly(several, { u"summary-multiple-hosts"_q })
		&& unread(several)
		&& last.refusals.empty()
		&& last.bridge
		&& (last.bridge->size() == 1);
	return CheckRow(
		passed,
		u"a stale or absent summary, and the summary of a run that had "
			u"more than one host, keep no call and no error; a final one of "
			u"the only host is read"_q,
		{ stale, absent, several, last });
}

// The host checks of the pinned manifest's auto/16-sdk-taps-a,
// auto/16-sdk-taps-c, auto/20-client-behaviour and auto/21-no-keyboard, as
// v0.1.2 has them.
constexpr auto kManifestHost = R"json([
	{ "id": "tw.open-link",
		"name": "openLink from a tap reaches the host once",
		"bridge": { "method": "openLink",
			"arg": "https://example.org/conformance/tap", "count": 1 },
		"gesture": true },
	{ "id": "input.user-activation",
		"name": "openLink from a timer is blocked",
		"bridge": { "method": "openLink",
			"arg": "https://example.org/conformance/timer", "count": 0 } },
	{ "id": "html.el.a-external",
		"name": "tapping an external <a href> goes to openLink",
		"bridge": { "method": "openLink",
			"arg": "https://example.org/conformance/anchor", "count": 1 },
		"gesture": true },
	{ "id": "input.prevent-default",
		"name": "preventDefault on a link tap: no openLink",
		"bridge": { "method": "openLink",
			"arg": "https://example.org/conformance/prevented", "count": 0 } },
	{ "id": "limit.haptic-rate",
		"name": "a burst of 20 haptics reaches the client as 5",
		"bridge": { "method": "haptic", "count": 5 } },
	{ "id": "behaviour.auto-height",
		"name": "the bubble grows with the widget and is clipped at the cap (600px)",
		"height": { "min": 599, "max": 601 } },
	{ "id": "js.focus",
		"name": "focus() from the widget leaves the chat input focused",
		"focus": "chat" },
	{ "id": "input.key-gesture",
		"name": "a key press does not unlock openLink",
		"bridge": { "method": "openLink",
			"arg": "https://example.org/conformance/key", "count": 0 },
		"gesture": true }
])json";

// Host checks no manifest entry has, for hostChecks()'s other branches.
constexpr auto kControlHost = R"json([
	{ "id": "control.xfail-fails", "name": "an expected failure, failing",
		"height": { "min": 1000 }, "xfail": true },
	{ "id": "control.xfail-passes", "name": "an expected failure, passing",
		"height": { "min": 1 }, "xfail": true },
	{ "id": "control.plain", "name": "a check of nothing" },
	{ "id": "control.cap", "name": "a height under a cap",
		"height": { "max": 650 } }
])json";

[[nodiscard]] QJsonArray JsonArray(const char *json) {
	return QJsonDocument::fromJson(QByteArray(json)).array();
}

[[nodiscard]] QJsonArray ControlHost() {
	auto result = JsonArray(kManifestHost);
	for (const auto &check : JsonArray(kControlHost)) {
		result.push_back(check);
	}
	return result;
}

[[nodiscard]] ConformanceSummary ReadLiteral(
		WidgetLogSummary state,
		int hosts,
		const QString &summary) {
	return ReadConformanceSummary(WidgetRuntimeLog{
		.source = u"requestLog"_q,
		.summary = summary,
		.summaryState = state,
	}, hosts);
}

// The run's observation from |summary|, its card 600 CSS px tall and the
// chat's input field still focused.
[[nodiscard]] ConformanceObservation ObservedFrom(
		const ConformanceSummary &summary) {
	return {
		.bridge = summary.bridge,
		.bridgeRefusals = ConformanceBridgeRefusals(summary),
		.height = 600.,
		.chatFocused = true,
	};
}

[[nodiscard]] QString FieldsText(const ConformanceHostFields &fields) {
	auto object = QJsonObject{ { u"observed"_q, fields.observed } };
	if (fields.host) {
		object.insert(u"host"_q, *fields.host);
	}
	return CompactJson(object);
}

[[nodiscard]] bool JudgedAs(
		const ConformanceHostFields &fields,
		const QString &id,
		const QString &status,
		const QString &detail) {
	for (const auto &value : fields.host.value_or(QJsonArray())) {
		const auto entry = value.toObject();
		if (entry.value(u"id"_q).toString() == id) {
			return (entry.value(u"status"_q).toString() == status)
				&& (entry.value(u"detail"_q).toString() == detail);
		}
	}
	return false;
}

// No observed.bridge, and |host| judged entry by entry in its order: each
// check counting bridge calls skipped as not observed, naming every one of
// |why| and giving no count; the height and focus ones of the 600 CSS px
// card that kept the focus passed.
[[nodiscard]] bool UnknownJudged(
		const ConformanceHostFields &fields,
		const QJsonArray &host,
		const QStringList &why) {
	if (fields.observed.contains(u"bridge"_q)
		|| !fields.host
		|| (fields.host->size() != host.size())) {
		return false;
	}
	for (auto i = 0; i != int(host.size()); ++i) {
		const auto check = host[i].toObject();
		const auto entry = (*fields.host)[i].toObject();
		if ((entry.value(u"id"_q) != check.value(u"id"_q))
			|| (entry.value(u"name"_q) != check.value(u"name"_q))) {
			return false;
		} else if (!Truthy(check.value(u"bridge"_q))) {
			continue;
		}
		const auto detail = entry.value(u"detail"_q).toString();
		if ((entry.value(u"status"_q).toString() != u"skip"_q)
			|| !detail.startsWith(u"bridge calls not observed ("_q)
			|| detail.contains(QChar(0x00D7))) {
			return false;
		}
		for (const auto &name : why) {
			if (!detail.contains(name)) {
				return false;
			}
		}
	}
	return JudgedAs(
			fields,
			u"behaviour.auto-height"_q,
			u"pass"_q,
			u"height 600"_q)
		&& JudgedAs(
			fields,
			u"js.focus"_q,
			u"pass"_q,
			u"chat input kept the focus"_q);
}

[[nodiscard]] WidgetOpsSelfCheckRow ObservationRow(
		bool passed,
		const QString &name,
		const QStringList &details) {
	return {
		.name = u"observation self-check: "_q + name,
		.details = details.join(QChar(' ')),
		.passed = passed,
	};
}

[[nodiscard]] WidgetOpsSelfCheckRow CheckUnknownBridge() {
	const auto host = JsonArray(kManifestHost);
	const auto haptic = u"haptic: [\"impact\",\"light\"]"_q;
	const auto url = u"https://example.org/"_q + QString(300, QChar('a'));
	const auto one = LiteralSummary(0, {}, 1, { u"ready: []"_q });
	struct Case {
		ConformanceSummary summary;
		QStringList why;
	};
	const auto cases = std::vector<Case>{
		{
			ReadLiteral(WidgetLogSummary::Absent, 1, QString()),
			{ u"summary-unavailable"_q },
		},
		{
			ReadLiteral(WidgetLogSummary::Stale, 1, one),
			{ u"summary-stale"_q },
		},
		{
			ReadLiteral(WidgetLogSummary::Fresh, 2, one),
			{ u"summary-multiple-hosts"_q },
		},
		{
			ReadLiteral(WidgetLogSummary::Fresh, 1, LiteralSummary(
				0,
				{},
				1,
				{
					u"openLink: "_q
						+ ClipLikeSummary(u"[\""_q + url + u"\"]"_q),
				})),
			{ u"bridge-args-clipped"_q },
		},
		{
			ReadLiteral(WidgetLogSummary::Fresh, 1, LiteralSummary(
				0,
				{},
				30,
				{ u"... 28 earlier entries not shown."_q, haptic, haptic })),
			{ u"bridge-tail-omitted"_q },
		},
		{
			ReadLiteral(WidgetLogSummary::Final, 1, LiteralSummary(
				0,
				{},
				0,
				{}).section(QChar('\n'), 0, -2)),
			{ u"bridge-section-missing"_q },
		},
	};
	auto passed = true;
	auto details = QStringList();
	for (const auto &entry : cases) {
		const auto observation = ObservedFrom(entry.summary);
		const auto fields = ConformanceHostFieldsOf(observation, host);
		passed = passed
			&& (observation.bridgeRefusals == entry.why)
			&& UnknownJudged(fields, host, entry.why);
		details.push_back(FieldsText(fields));
	}
	auto unread = ObservedFrom(ConformanceSummary());
	unread.bridgeRefusals = QStringList();
	const auto never = ConformanceHostFieldsOf(unread, host);
	passed = passed && UnknownJudged(never, host, { u"not read"_q });
	details.push_back(FieldsText(never));
	return ObservationRow(
		passed,
		u"a run whose bridge reading was refused (no summary, a stale one, "
			u"more than one host, clipped arguments, earlier calls left out, "
			u"no bridge section) or never read gets no observed.bridge and "
			u"every host check judged: each counting bridge calls skipped as "
			u"not observed, naming why, with no call count"_q,
		details);
}

[[nodiscard]] WidgetOpsSelfCheckRow CheckIndependentHost() {
	const auto host = ControlHost();
	const auto stale = ReadLiteral(WidgetLogSummary::Stale, 1, QString());
	auto moved = ObservedFrom(stale);
	moved.height = 700.;
	moved.chatFocused = false;
	moved.focusHolder = u"the compose field's send button"_q;
	auto unmeasured = ObservedFrom(stale);
	unmeasured.height = std::nullopt;
	unmeasured.chatFocused = std::nullopt;
	auto fraction = ObservedFrom(stale);
	fraction.height = 599.5;
	fraction.chatFocused = false;
	auto synthetic = ObservedFrom(stale);
	synthetic.syntheticInput = true;
	const auto a = ConformanceHostFieldsOf(moved, host);
	const auto b = ConformanceHostFieldsOf(unmeasured, host);
	const auto c = ConformanceHostFieldsOf(fraction, host);
	const auto d = ConformanceHostFieldsOf(synthetic, host);
	const auto passed = JudgedAs(
			a,
			u"behaviour.auto-height"_q,
			u"fail"_q,
			u"height 700"_q)
		&& JudgedAs(
			a,
			u"js.focus"_q,
			u"fail"_q,
			u"focus moved to the compose field's send button"_q)
		&& JudgedAs(a, u"control.xfail-fails"_q, u"xfail"_q, u"height 700"_q)
		&& JudgedAs(a, u"control.xfail-passes"_q, u"xpass"_q, u"height 700"_q)
		&& JudgedAs(a, u"control.plain"_q, u"pass"_q, QString())
		&& JudgedAs(a, u"control.cap"_q, u"fail"_q, u"height 700"_q)
		&& JudgedAs(
			b,
			u"behaviour.auto-height"_q,
			u"skip"_q,
			u"height not observed"_q)
		&& JudgedAs(
			b,
			u"js.focus"_q,
			u"skip"_q,
			u"chat focus not observed"_q)
		&& JudgedAs(b, u"control.plain"_q, u"pass"_q, QString())
		&& JudgedAs(
			c,
			u"behaviour.auto-height"_q,
			u"pass"_q,
			u"height 599.5"_q)
		&& JudgedAs(
			c,
			u"js.focus"_q,
			u"fail"_q,
			u"focus moved to the widget"_q)
		&& JudgedAs(c, u"control.cap"_q, u"pass"_q, u"height 599.5"_q)
		&& JudgedAs(
			d,
			u"tw.open-link"_q,
			u"skip"_q,
			u"synthetic input grants no user gesture"_q)
		&& JudgedAs(
			d,
			u"input.user-activation"_q,
			u"skip"_q,
			u"bridge calls not observed (runner refusal: summary-stale)"_q);
	return ObservationRow(
		passed,
		u"the checks of a run with unknown bridge calls that read no call "
			u"are judged as report.mjs's hostChecks() judges them: a height "
			u"in and out of bounds and unmeasured, the focus kept, moved with "
			u"and without a holder and unmeasured, expected failures, a check "
			u"of nothing and synthetic input"_q,
		{ FieldsText(a), FieldsText(b), FieldsText(c), FieldsText(d) });
}

[[nodiscard]] WidgetOpsSelfCheckRow CheckMeasuredBridge() {
	const auto host = JsonArray(kManifestHost);
	const auto tap = u"https://example.org/conformance/tap"_q;
	const auto anchor = u"https://example.org/conformance/anchor"_q;
	const auto empty = ConformanceHostFieldsOf(
		ObservedFrom(ReadLiteral(
			WidgetLogSummary::Fresh,
			1,
			LiteralSummary(0, {}, 0, {}))),
		host);
	const auto calls = ConformanceHostFieldsOf(
		ObservedFrom(ReadLiteral(
			WidgetLogSummary::Final,
			1,
			LiteralSummary(0, {}, 4, {
				u"ready: []"_q,
				u"openLink: [\""_q + tap + u"\"]"_q,
				u"openLink: [\""_q + anchor + u"\"]"_q,
				u"haptic: [\"impact\",\"light\"]"_q,
			}))),
		host);
	const auto call = [](const QString &method, QJsonArray args) {
		return QJsonObject{
			{ u"method"_q, method },
			{ u"args"_q, args },
		};
	};
	const auto expected = QJsonArray{
		call(u"ready"_q, QJsonArray()),
		call(u"openLink"_q, QJsonArray{ tap }),
		call(u"openLink"_q, QJsonArray{ anchor }),
		call(u"haptic"_q, QJsonArray{ u"impact"_q, u"light"_q }),
	};
	const auto observed = [](const ConformanceHostFields &fields) {
		return !fields.host
			&& (fields.observed.value(u"height"_q).toDouble() == 600.)
			&& (fields.observed.value(u"chatFocused"_q) == QJsonValue(true))
			&& (fields.observed.value(u"syntheticInput"_q)
				== QJsonValue(false));
	};
	const auto passed = observed(empty)
		&& (empty.observed.value(u"bridge"_q) == QJsonValue(QJsonArray()))
		&& observed(calls)
		&& (calls.observed.value(u"bridge"_q) == QJsonValue(expected));
	return ObservationRow(
		passed,
		u"a measured bridge reading, no call or exact calls, is written as "
			u"observed.bridge, the calls in order with array arguments, and "
			u"the host checks are left to report.mjs"_q,
		{ FieldsText(empty), FieldsText(calls) });
}

// A run read in full: a final reading of one host with three scanned
// batches, and a bridge reading of no call.
[[nodiscard]] ConformanceCoverage CoveredRun(const QString &key) {
	auto result = ConformanceCoverage{
		.key = key,
		.bridge = std::vector<ConformanceBridgeCall>(),
		.tapped = true,
		.final = true,
	};
	auto &reading = result.reading;
	reading.factoriesSelected = 1;
	reading.factoriesCompleted = 1;
	reading.factoriesReleased = 1;
	reading.wrappersBuilt = 1;
	reading.wrappersDestroyed = 1;
	reading.batches = 3;
	reading.ops = 40;
	return result;
}

// Each of |subjects| beside a run read in full: |claim|'s absence is not
// certified, and that subject alone is named uncovered.
[[nodiscard]] bool EachUncovered(
		const std::vector<ConformanceCoverage> &subjects,
		ConformanceAbsence ConformanceAbsences::*claim,
		QStringList &details) {
	auto result = true;
	for (const auto &subject : subjects) {
		const auto absences = JudgeConformanceAbsences(
			{ CoveredRun(u"good"_q), subject },
			2);
		const auto &judged = absences.*claim;
		details.push_back(CompactJson(ConformanceAbsenceObject(judged)));
		result = result
			&& !judged.certified
			&& (judged.uncovered.size() == 1)
			&& judged.uncovered.front().startsWith(subject.key + u": "_q);
	}
	return result;
}

[[nodiscard]] WidgetOpsSelfCheckRow CheckScanCoverage() {
	const auto missing = ConformanceCoverage{ .key = u"missing"_q };
	auto hostless = CoveredRun(u"hostless"_q);
	hostless.reading = WidgetOpsReading();
	auto empty = CoveredRun(u"zero-batch"_q);
	empty.reading.batches = 0;
	empty.reading.ops = 0;
	auto unfinished = CoveredRun(u"non-final"_q);
	unfinished.final = false;
	unfinished.reading.factoriesReleased = 0;
	auto gapped = CoveredRun(u"gapped"_q);
	gapped.reading.scanGaps = 1;
	auto leaking = CoveredRun(u"leaking"_q);
	leaking.reading.leaks = 1;
	auto details = QStringList();
	const auto leaks = &ConformanceAbsences::leaks;
	const auto gaps = &ConformanceAbsences::gaps;
	const auto unseen = EachUncovered(
			{ missing, hostless, empty, unfinished, gapped },
			leaks,
			details)
		&& EachUncovered({ missing, hostless, unfinished }, gaps, details);
	const auto good = CoveredRun(u"good"_q);
	const auto covered = JudgeConformanceAbsences(
		{ good, CoveredRun(u"other"_q) },
		2);
	const auto blind = JudgeConformanceAbsences({ good }, 0);
	const auto none = JudgeConformanceAbsences({}, 2);
	const auto quiet = JudgeConformanceAbsences({ good, empty }, 2);
	const auto found = JudgeConformanceAbsences(
		{ good, leaking, gapped },
		2);
	for (const auto &judged : { covered, blind, none, quiet, found }) {
		details.push_back(CompactJson(QJsonObject{
			{ u"leaks"_q, ConformanceAbsenceObject(judged.leaks) },
			{ u"gaps"_q, ConformanceAbsenceObject(judged.gaps) },
		}));
	}
	const auto passed = unseen
		&& covered.leaks.certified
		&& covered.gaps.certified
		&& !blind.leaks.certified
		&& blind.leaks.uncovered.isEmpty()
		&& !none.leaks.certified
		&& !none.gaps.certified
		&& quiet.gaps.certified
		&& !quiet.leaks.certified
		&& !found.leaks.certified
		&& (found.leaks.found == 1)
		&& !found.gaps.certified
		&& (found.gaps.found == 1)
		&& (found.gaps.uncovered.isEmpty());
	return ObservationRow(
		passed,
		u"with the scanner's control leaks matched, a run without a tap, one "
			u"whose tap wrapped no host, one with no scanned batch, one whose "
			u"reading is not final and one with a scan gap each leave the "
			u"leak absence uncertified and are named, the first two and the "
			u"non-final one the gap absence too; runs read in full, and for "
			u"the gaps a final reading with no batch, certify them, while a "
			u"scan without its control, an empty selection and a found leak "
			u"or gap certify nothing"_q,
		details);
}

[[nodiscard]] WidgetOpsSelfCheckRow CheckBridgeCoverage() {
	const auto good = CoveredRun(u"good"_q);
	auto calls = CoveredRun(u"calls"_q);
	calls.bridge = std::vector<ConformanceBridgeCall>{
		{ .method = u"ready"_q, .args = QJsonArray() },
		{
			.method = u"openLink"_q,
			.args = QJsonArray{ u"https://example.org/x"_q },
		},
	};
	auto refused = CoveredRun(u"refused"_q);
	refused.bridge = std::nullopt;
	refused.bridgeRefusals = QStringList{ u"summary-multiple-hosts"_q };
	auto unread = CoveredRun(u"unread"_q);
	unread.bridge = std::nullopt;
	auto link = CoveredRun(u"link"_q);
	link.bridge = std::vector<ConformanceBridgeCall>{
		{
			.method = u"openTelegramLink"_q,
			.args = QJsonArray{ u"https://t.me/x"_q },
		},
	};
	const auto certified = JudgeConformanceAbsences({ good, calls }, 2);
	const auto partial = JudgeConformanceAbsences(
		{ good, calls, refused },
		2);
	const auto missing = JudgeConformanceAbsences({ refused, unread }, 2);
	const auto found = JudgeConformanceAbsences({ good, link }, 2);
	auto details = QStringList();
	for (const auto &judged : { certified, partial, missing, found }) {
		details.push_back(CompactJson(
			ConformanceAbsenceObject(judged.telegramLinks)));
	}
	const auto &uncovered = missing.telegramLinks.uncovered;
	const auto passed = certified.telegramLinks.certified
		&& !partial.telegramLinks.certified
		&& (partial.telegramLinks.uncovered == QStringList{
			u"refused: its bridge reading was refused: "
				u"summary-multiple-hosts"_q,
		})
		&& !missing.telegramLinks.certified
		&& (uncovered.size() == 2)
		&& uncovered[0].startsWith(u"refused: "_q)
		&& uncovered[1].startsWith(u"unread: "_q)
		&& uncovered[1].contains(u"not read"_q)
		&& !found.telegramLinks.certified
		&& (found.telegramLinks.found == 1)
		&& found.telegramLinks.uncovered.isEmpty();
	return ObservationRow(
		passed,
		u"a run whose bridge reading was refused or never read leaves the "
			u"openTelegramLink absence uncertified and is named with why, "
			u"whether one run of several or every run is uncovered; readings "
			u"of no call or other calls certify it, and an accepted "
			u"openTelegramLink call is found"_q,
		details);
}

} // namespace

ConformanceSummary ParseConformanceSummary(const QString &summary) {
	auto result = ConformanceSummary();
	const auto lines = summary.split(QChar('\n'));
	auto bridge = int(lines.size());
	while ((bridge > 0) && (BridgeHeaderCount(lines[bridge - 1]) < 0)) {
		--bridge;
	}
	if (!bridge) {
		Refuse(
			result,
			u"bridge-section-missing"_q,
			u"no line of the %1-line summary is a \"Bridge calls: N.\" "
				u"header"_q.arg(NumberText(lines.size())));
		Refuse(
			result,
			u"console-ambiguous"_q,
			u"without the bridge section's header the console section has "
				u"no end"_q);
		return result;
	}
	ParseBridge(lines, bridge - 1, result);
	ParseConsole(lines, bridge - 1, result);
	return result;
}

ConformanceSummary ReadConformanceSummary(
		const WidgetRuntimeLog &log,
		int hosts) {
	const auto state = log.summaryState;
	const auto own = (state == WidgetLogSummary::Fresh)
		|| (state == WidgetLogSummary::Final);
	auto result = own
		? ParseConformanceSummary(log.summary)
		: ConformanceSummary();
	result.hosts = hosts;
	auto refusal = ConformanceRefusal();
	if (state == WidgetLogSummary::Absent) {
		refusal = ConformanceRefusal{
			.name = u"summary-unavailable"_q,
			.detail = u"the runtime held no summary of a host (log source "
				u"[%1])"_q.arg(log.source),
		};
	} else if (state == WidgetLogSummary::Stale) {
		refusal = ConformanceRefusal{
			.name = u"summary-stale"_q,
			.detail = u"no fresh sample of the host answered the log request "
				u"(log source [%1]), so the summary held may predate the "
				u"run's last calls"_q.arg(log.source),
		};
	} else if (hosts < 1) {
		refusal = ConformanceRefusal{
			.name = u"summary-unavailable"_q,
			.detail = u"the run's tap selected no host factory, so the "
				u"summary is of no host it armed"_q,
		};
	} else if (hosts > 1) {
		refusal = ConformanceRefusal{
			.name = u"summary-multiple-hosts"_q,
			.detail = u"%1 hosts were constructed for the run, and the "
				u"summary is the last one's alone"_q.arg(NumberText(hosts)),
		};
	}
	if (!refusal.name.isEmpty()) {
		result.bridge = std::nullopt;
		result.errors = std::nullopt;
		result.refusals.insert(begin(result.refusals), refusal);
	}
	return result;
}

QJsonObject ConformanceSummaryObject(const ConformanceSummary &summary) {
	auto refusals = QJsonArray();
	for (const auto &refusal : summary.refusals) {
		refusals.push_back(refusal.name);
	}
	return QJsonObject{
		{ u"hosts"_q, summary.hosts },
		{ u"bridgeCount"_q, summary.bridgeCount },
		{ u"bridgeOmitted"_q, summary.bridgeOmitted },
		{ u"bridgeLines"_q, summary.bridgeLines },
		{ u"bridgeAccepted"_q, summary.bridge
			? QJsonValue(int(summary.bridge->size()))
			: QJsonValue(QJsonValue::Null) },
		{ u"consoleHeaders"_q, summary.consoleHeaders },
		{ u"consoleCount"_q, summary.consoleCount },
		{ u"consoleOmitted"_q, summary.consoleOmitted },
		{ u"consoleDropped"_q, summary.consoleDropped },
		{ u"consoleEntries"_q, summary.consoleEntries },
		{ u"consoleContinuations"_q, summary.consoleContinuations },
		{ u"errors"_q, summary.errors
			? QJsonValue(int(summary.errors->size()))
			: QJsonValue(QJsonValue::Null) },
		{ u"errorsClipped"_q, summary.errorsClipped },
		{ u"refusals"_q, refusals },
	};
}

std::vector<WidgetOpsSelfCheckRow> RunConformanceSummaryCheck() {
	return {
		CheckAccepted(),
		CheckArguments(),
		CheckSection(),
		CheckLookalikes(),
		CheckOuterNotes(),
		CheckProvenance(),
	};
}

QStringList ConformanceBridgeRefusals(const ConformanceSummary &summary) {
	auto result = QStringList();
	for (const auto &refusal : summary.refusals) {
		if (refusal.name.startsWith(u"summary-"_q)
			|| refusal.name.startsWith(u"bridge-"_q)) {
			result.push_back(refusal.name);
		}
	}
	return result;
}

ConformanceHostFields ConformanceHostFieldsOf(
		const ConformanceObservation &observation,
		const QJsonArray &manifestHost) {
	auto result = ConformanceHostFields{
		.observed = QJsonObject{
			{ u"syntheticInput"_q, observation.syntheticInput },
		},
	};
	auto &observed = result.observed;
	if (observation.bridge) {
		observed.insert(u"bridge"_q, CallsArray(*observation.bridge));
	}
	if (observation.height) {
		observed.insert(u"height"_q, *observation.height);
	}
	if (observation.chatFocused) {
		observed.insert(u"chatFocused"_q, *observation.chatFocused);
	}
	if (!observation.focusHolder.isEmpty()) {
		observed.insert(u"focusHolder"_q, observation.focusHolder);
	}
	if (observation.bridge) {
		return result;
	}
	const auto unknown = u"bridge calls not observed (%1)"_q.arg(
		observation.bridgeRefusals.isEmpty()
			? u"the run's runtime summary was not read"_q
			: (u"runner refusal: "_q
				+ observation.bridgeRefusals.join(u", "_q)));
	auto host = QJsonArray();
	for (const auto &check : manifestHost) {
		host.push_back(JudgeHostCheck(check.toObject(), observation, unknown));
	}
	result.host = std::move(host);
	return result;
}

ConformanceAbsences JudgeConformanceAbsences(
		const std::vector<ConformanceCoverage> &runs,
		int controls) {
	auto result = ConformanceAbsences();
	for (const auto &run : runs) {
		Cover(result.leaks, run.key, LeakUncovered(run), run.reading.leaks);
		Cover(result.gaps, run.key, TapUncovered(run), run.reading.scanGaps);
		Cover(
			result.telegramLinks,
			run.key,
			BridgeUncovered(run),
			TelegramLinks(run));
	}
	Certify(result.leaks, (controls > 0));
	Certify(result.gaps, true);
	Certify(result.telegramLinks, true);
	return result;
}

QJsonObject ConformanceAbsenceObject(const ConformanceAbsence &absence) {
	return QJsonObject{
		{ u"selected"_q, absence.selected },
		{ u"covered"_q, absence.selected - int(absence.uncovered.size()) },
		{ u"found"_q, absence.found },
		{ u"certified"_q, absence.certified },
		{ u"uncovered"_q, QJsonArray::fromStringList(absence.uncovered) },
	};
}

std::vector<WidgetOpsSelfCheckRow> RunConformanceObservationCheck() {
	return {
		CheckUnknownBridge(),
		CheckIndependentHost(),
		CheckMeasuredBridge(),
		CheckScanCoverage(),
		CheckBridgeCoverage(),
	};
}

} // namespace Test

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
