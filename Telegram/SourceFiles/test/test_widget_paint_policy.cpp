/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "test/test_widget_paint_policy.h"

#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "core/src/paint_policy.h"
#include "twidget/json.h"
#include "twidget/renderer.h"

#include <QtCore/QStringList>

#include <array>
#include <clocale>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Test {
namespace {

namespace paint = twlite::paint;

using Json = twidget::Json;
using Counter = paint::PaintCounter<std::string>;

constexpr auto kJsonChars = 200;

// The fixture's prose member, which no reader runs.
constexpr auto kProseKey = "_";

// The rows so far and what the coverage row counts.
struct PolicyRun {
	std::vector<WidgetOpsSelfCheckRow> rows;
	int steps = 0;
	int checks = 0;
	int unknownSteps = 0;
};

// One case's steps: its checks, and steps of no kind the reader knows.
struct CaseTally {
	int checks = 0;
	int unknown = 0;
};

// The CSS text a paint::Style views, owned while it is judged.
struct OwnedStyle {
	std::string display;
	std::string content;
	std::string boxShadow;
	std::string filter;
	std::string backdropFilter;
	std::string opacity;
	paint::Style::Button button = paint::Style::Button::None;
};

// A fixture expectation; |valid| is false when it has the wrong shape, so
// a missing or malformed one fails its row instead of matching a default.
struct ExpectedPaint {
	std::optional<paint::BoxPaint> paint;
	bool valid = false;
};

struct ExpectedVerdict {
	std::optional<paint::Verdict> verdict;
	bool valid = false;
};

// Renderer::limitExceeded() needs a renderer; this one renders nothing.
class ReportStub final : public twidget::Renderer {
public:
	void load() override;
	void send(const std::string &json) override;
	void setTheme(const std::string &theme) override;
	void setWidth(double width) override;
	void destroy() override;

};

// A top-level section of the fixture and its reader.
struct FixtureSection {
	const char *key = nullptr;
	void (*check)(PolicyRun &run, const Json &entries) = nullptr;
	bool object = false;
};

void ReportStub::load() {
}

void ReportStub::send(const std::string &) {
}

void ReportStub::setTheme(const std::string &) {
}

void ReportStub::setWidth(double) {
}

void ReportStub::destroy() {
}

void AddRow(
		PolicyRun &run,
		bool passed,
		const QString &name,
		const QString &details) {
	run.rows.push_back({
		.name = u"paint policy fixture: "_q + name,
		.details = details,
		.passed = passed,
	});
}

[[nodiscard]] QString StdText(const std::string &value) {
	return QString::fromStdString(value);
}

[[nodiscard]] QString Quoted(const std::string &value) {
	return QChar('\'') + StdText(value) + QChar('\'');
}

[[nodiscard]] QString JsonText(const Json &value) {
	return StdText(value.dump()).left(kJsonChars);
}

[[nodiscard]] QString Flag(bool value) {
	return value ? u"true"_q : u"false"_q;
}

[[nodiscard]] QString EntryName(
		const char *section,
		int number,
		const QString &label) {
	return u"%1 %2: %3"_q.arg(
		QString::fromLatin1(section),
		QString::number(number),
		label);
}

[[nodiscard]] QString PaintText(const std::optional<paint::BoxPaint> &box) {
	return box
		? u"{heavy %1, backdrop %2}"_q.arg(
			Flag(box->heavy),
			Flag(box->backdrop))
		: u"null"_q;
}

[[nodiscard]] QString VerdictText(
		const std::optional<paint::Verdict> &verdict) {
	return verdict
		? (QString::fromLatin1(twidget::paintLimitCode(verdict->code))
			+ QChar(' ')
			+ QString::number(verdict->count))
		: u"null"_q;
}

// The fixture writes a value as CSS text, or as a number, which the
// reference readers print with Json::number().
[[nodiscard]] std::string CssText(const Json &value) {
	if (value.isString()) {
		return value.asString();
	} else if (value.isNumber()) {
		return Json::number(value.asNumber());
	}
	return std::string();
}

[[nodiscard]] OwnedStyle ReadStyle(const Json &style) {
	const auto &button = style["button"].asString();
	return {
		.display = CssText(style["display"]),
		.content = CssText(style["content"]),
		.boxShadow = CssText(style["boxShadow"]),
		.filter = CssText(style["filter"]),
		.backdropFilter = CssText(style["backdropFilter"]),
		.opacity = CssText(style["opacity"]),
		.button = (button == "disabled")
			? paint::Style::Button::Disabled
			: (button == "pressed")
			? paint::Style::Button::Pressed
			: paint::Style::Button::None,
	};
}

// Views |style|, which must outlive the result.
[[nodiscard]] paint::Style StyleView(const OwnedStyle &style) {
	return {
		.display = style.display,
		.content = style.content,
		.box_shadow = style.boxShadow,
		.filter = style.filter,
		.backdrop_filter = style.backdropFilter,
		.opacity = style.opacity,
		.button = style.button,
	};
}

// A `boxes` entry or a `set` step: its `style` (initial values when it has
// none) and its `pseudo` flag.
[[nodiscard]] std::optional<paint::BoxPaint> BoxOf(const Json &entry) {
	const auto style = ReadStyle(entry["style"]);
	return paint::box_paint(StyleView(style), entry["pseudo"].asBool());
}

// The numeric `computedOffset`, else the numeric `offset`, else none, as
// the fixture's TS reader takes a keyframe's offset.
[[nodiscard]] std::optional<double> OffsetOf(const Json &keyframe) {
	const auto &computed = keyframe["computedOffset"];
	const auto &offset = keyframe["offset"];
	if (computed.isNumber()) {
		return computed.asNumber();
	} else if (offset.isNumber()) {
		return offset.asNumber();
	}
	return std::nullopt;
}

// Every member of a keyframe goes to AnimationPaint, which skips the
// offsets, `easing` and `composite`; a value that is no string is unknown,
// never coerced to text.
[[nodiscard]] std::optional<paint::BoxPaint> AnimationOf(
		const Json &keyframes) {
	auto animation = paint::AnimationPaint();
	for (const auto &keyframe : keyframes.arr()) {
		animation.keyframe(OffsetOf(keyframe));
		for (const auto &[name, value] : keyframe.obj()) {
			animation.property(
				name,
				value.isString()
					? std::optional<std::string_view>(value.asString())
					: std::nullopt);
		}
	}
	return animation.result();
}

// |value| must be present: null, or an object with boolean `heavy` and
// `backdrop`.
[[nodiscard]] ExpectedPaint ReadExpectedPaint(const Json *value) {
	if (!value) {
		return {};
	} else if (value->isNull()) {
		return { .valid = true };
	}
	const auto &heavy = (*value)["heavy"];
	const auto &backdrop = (*value)["backdrop"];
	if (!heavy.isBool() || !backdrop.isBool()) {
		return {};
	}
	return {
		.paint = paint::BoxPaint{ heavy.asBool(), backdrop.asBool() },
		.valid = true,
	};
}

// |value| must be present: null, or an object with a known `code` and a
// numeric `count`.
[[nodiscard]] ExpectedVerdict ReadExpectedVerdict(const Json *value) {
	if (!value) {
		return {};
	} else if (value->isNull()) {
		return { .valid = true };
	}
	const auto &code = (*value)["code"];
	const auto &count = (*value)["count"];
	const auto limit = code.isString()
		? twidget::paintLimitFromCode(code.asString())
		: std::nullopt;
	if (!limit || !count.isNumber()) {
		return {};
	}
	return {
		.verdict = paint::Verdict{ *limit, count.asInt() },
		.valid = true,
	};
}

// A mirror of WidgetHost::handleRendererMessage()'s reading of a `limit`
// report, not a call into it: the glue exports no C++ checkPaintLimit().
// A known string code and a finite count of at least 0, which the host
// truncates with int(count).
[[nodiscard]] std::optional<paint::Verdict> MirrorHostReport(
		const Json &report) {
	const auto &code = report["code"];
	const auto &count = report["count"];
	const auto limit = code.isString()
		? twidget::paintLimitFromCode(code.asString())
		: std::nullopt;
	const auto value = count.isNumber() ? count.asNumber() : -1.;
	// int(count) is undefined past int; no pinned count goes there.
	if (!limit
		|| !std::isfinite(value)
		|| (value < 0.)
		|| (value > double(std::numeric_limits<int>::max()))) {
		return std::nullopt;
	}
	return paint::Verdict{ *limit, int(value) };
}

// What Renderer::limitExceeded() sends for |verdict|.
[[nodiscard]] std::vector<std::string> Emitted(
		const paint::Verdict &verdict) {
	auto stub = ReportStub();
	auto result = std::vector<std::string>();
	stub.onMessage = [&](const std::string &json) {
		result.push_back(json);
	};
	stub.limitExceeded(verdict.code, verdict.count);
	return result;
}

// One `limit` message that the mirror reads back as |verdict|.
[[nodiscard]] bool RoundTrips(
		const paint::Verdict &verdict,
		const std::vector<std::string> &emitted) {
	if (emitted.size() != 1) {
		return false;
	}
	const auto parsed = Json::parse(emitted.front());
	return parsed
		&& ((*parsed)["type"].asString() == "limit")
		&& (MirrorHostReport(*parsed) == verdict);
}

[[nodiscard]] QString MemberText(const Json &entry, const char *key) {
	const auto value = entry.find(key);
	return QString::fromLatin1(key)
		+ QChar('=')
		+ (value ? JsonText(*value) : u"missing"_q);
}

// The compiled value of a fixture constant, by its paint-policy.ts name.
[[nodiscard]] std::optional<int> CompiledConstant(const std::string &name) {
	const auto known = std::array{
		std::pair{ "PAINT_HEAVY_BOXES", twidget::kMaxHeavyBoxes },
		std::pair{ "PAINT_BACKDROP_BOXES", twidget::kMaxBackdropBoxes },
	};
	for (const auto &[key, value] : known) {
		if (name == key) {
			return value;
		}
	}
	return std::nullopt;
}

void CheckConstants(PolicyRun &run, const Json &constants) {
	auto number = 0;
	for (const auto &[name, value] : constants.obj()) {
		++number;
		const auto compiled = CompiledConstant(name);
		AddRow(
			run,
			compiled
				&& value.isNumber()
				&& (value.asNumber() == double(*compiled)),
			EntryName("constants", number, StdText(name)),
			u"compiled=%1 fixture=%2"_q.arg(
				compiled ? QString::number(*compiled) : u"unknown"_q,
				JsonText(value)));
	}
}

void CheckShadows(PolicyRun &run, const Json &shadows) {
	auto number = 0;
	for (const auto &entry : shadows.arr()) {
		++number;
		const auto &value = entry["value"];
		const auto &blurred = entry["blurred"];
		const auto &withoutBlur = entry["withoutBlur"];
		const auto gotBlurred = paint::box_shadow_blurred(value.asString());
		const auto gotWithout = paint::box_shadow_without_blur(
			value.asString());
		AddRow(
			run,
			value.isString()
				&& blurred.isBool()
				&& withoutBlur.isString()
				&& (gotBlurred == blurred.asBool())
				&& (gotWithout == withoutBlur.asString()),
			EntryName("shadows", number, Quoted(value.asString())),
			u"blurred=%1 expected=%2 withoutBlur='%3' expected=%4"_q.arg(
				Flag(gotBlurred),
				JsonText(blurred),
				StdText(gotWithout),
				JsonText(withoutBlur)));
	}
}

void CheckBoxes(PolicyRun &run, const Json &boxes) {
	auto number = 0;
	for (const auto &entry : boxes.arr()) {
		++number;
		const auto got = BoxOf(entry);
		const auto expected = ReadExpectedPaint(entry.find("expect"));
		AddRow(
			run,
			expected.valid && (got == expected.paint),
			EntryName("boxes", number, StdText(entry["name"].asString())),
			u"got=%1 expected=%2 pseudo=%3 style=%4"_q.arg(
				PaintText(got),
				MemberText(entry, "expect"),
				Flag(entry["pseudo"].asBool()),
				JsonText(entry["style"])));
	}
}

void CheckHidesSubtree(PolicyRun &run, const Json &entries) {
	auto number = 0;
	for (const auto &entry : entries.arr()) {
		++number;
		const auto &display = entry["display"];
		const auto &expected = entry["expect"];
		const auto got = paint::hides_subtree(display.asString());
		AddRow(
			run,
			display.isString()
				&& expected.isBool()
				&& (got == expected.asBool()),
			EntryName("hidesSubtree", number, Quoted(display.asString())),
			u"got=%1 expected=%2"_q.arg(Flag(got), JsonText(expected)));
	}
}

void CheckAnimations(PolicyRun &run, const Json &animations) {
	auto number = 0;
	for (const auto &entry : animations.arr()) {
		++number;
		const auto &keyframes = entry["keyframes"];
		const auto got = AnimationOf(keyframes);
		const auto expected = ReadExpectedPaint(entry.find("expect"));
		AddRow(
			run,
			keyframes.isArray()
				&& expected.valid
				&& (got == expected.paint),
			EntryName(
				"animations",
				number,
				StdText(entry["name"].asString())),
			u"got=%1 expected=%2 keyframes=%3"_q.arg(
				PaintText(got),
				MemberText(entry, "expect"),
				QString::number(int(keyframes.size()))));
	}
}

void CheckMessages(PolicyRun &run, const Json &messages) {
	auto number = 0;
	for (const auto &entry : messages.arr()) {
		++number;
		const auto &code = entry["code"];
		const auto &count = entry["count"];
		const auto &expected = entry["expect"];
		const auto limit = code.isString()
			? twidget::paintLimitFromCode(code.asString())
			: std::nullopt;
		const auto got = limit
			? twidget::paintLimitMessage(*limit, count.asInt())
			: std::string();
		AddRow(
			run,
			limit
				&& count.isNumber()
				&& expected.isString()
				&& (got == expected.asString()),
			EntryName(
				"messages",
				number,
				JsonText(code) + QChar(' ') + JsonText(count)),
			u"got=[%1] expected=%2"_q.arg(StdText(got), JsonText(expected)));
	}
}

// The fixture's `expect` against the host mirror; an accepted report also
// goes through Renderer::limitExceeded() and back through the mirror.
void CheckReports(PolicyRun &run, const Json &reports) {
	auto number = 0;
	for (const auto &entry : reports.arr()) {
		++number;
		const auto mirrored = MirrorHostReport(entry);
		const auto expected = ReadExpectedVerdict(entry.find("expect"));
		const auto emitted = expected.verdict
			? Emitted(*expected.verdict)
			: std::vector<std::string>();
		const auto roundTrip = !expected.verdict
			|| RoundTrips(*expected.verdict, emitted);
		auto sent = QStringList();
		for (const auto &json : emitted) {
			sent.push_back(StdText(json));
		}
		const auto label = MemberText(entry, "code")
			+ QChar(' ')
			+ MemberText(entry, "count");
		AddRow(
			run,
			expected.valid && (mirrored == expected.verdict) && roundTrip,
			EntryName("reports", number, label),
			u"hostMirror=%1 %2 limitExceeded=[%3] roundTrip=%4"_q.arg(
				VerdictText(mirrored),
				MemberText(entry, "expect"),
				sent.join(u" | "_q),
				Flag(roundTrip)));
	}
}

// Empty unless |value| is a key or an array of keys, all strings.
[[nodiscard]] std::optional<std::vector<std::string>> ReadKeys(
		const Json &value) {
	if (value.isString()) {
		return std::vector<std::string>{ value.asString() };
	} else if (!value.isArray()) {
		return std::nullopt;
	}
	auto result = std::vector<std::string>();
	for (const auto &key : value.arr()) {
		if (!key.isString()) {
			return std::nullopt;
		}
		result.push_back(key.asString());
	}
	return result;
}

[[nodiscard]] QString ApplySet(Counter &counter, const Json &step) {
	const auto keys = ReadKeys(step["set"]);
	if (!keys) {
		return u"set: the keys are not strings"_q;
	}
	const auto box = BoxOf(step);
	for (const auto &key : *keys) {
		counter.set_box(key, box);
	}
	return QString();
}

[[nodiscard]] QString ApplyRemove(Counter &counter, const Json &step) {
	const auto keys = ReadKeys(step["remove"]);
	if (!keys) {
		return u"remove: the keys are not strings"_q;
	}
	for (const auto &key : *keys) {
		counter.remove_box(key);
	}
	return QString();
}

// An animation that adds nothing (AnimationPaint's null) starts nothing.
[[nodiscard]] QString ApplyStart(Counter &counter, const Json &step) {
	const auto keys = ReadKeys(step["start"]);
	const auto &id = step["animation"];
	if (!keys || !id.isString()) {
		return u"start: the keys or the animation id are not strings"_q;
	}
	if (const auto box = AnimationOf(step["keyframes"])) {
		for (const auto &key : *keys) {
			counter.start_animation(key, id.asString(), *box);
		}
	}
	return QString();
}

[[nodiscard]] QString ApplyEnd(Counter &counter, const Json &step) {
	const auto keys = ReadKeys(step["end"]);
	const auto &id = step["animation"];
	if (!keys || !id.isString()) {
		return u"end: the keys or the animation id are not strings"_q;
	}
	for (const auto &key : *keys) {
		counter.end_animation(key, id.asString());
	}
	return QString();
}

// Each check asks for the verdict, as a renderer does before each frame,
// so a check after the first verdict reads whether it stuck.
[[nodiscard]] QString ApplyCheck(Counter &counter, const Json &check) {
	const auto verdict = counter.check();
	const auto &heavy = check["heavy"];
	const auto &backdrop = check["backdrop"];
	const auto stop = ReadExpectedVerdict(check.find("stop"));
	const auto matches = heavy.isNumber()
		&& backdrop.isNumber()
		&& stop.valid
		&& (heavy.asNumber() == double(counter.heavy()))
		&& (backdrop.asNumber() == double(counter.backdrop()))
		&& (verdict == stop.verdict);
	if (matches) {
		return QString();
	}
	return u"got heavy=%1 backdrop=%2 stop=%3, expected %4"_q.arg(
		QString::number(counter.heavy()),
		QString::number(counter.backdrop()),
		VerdictText(verdict),
		JsonText(check));
}

// The kind is picked in the fixture's TS reader's order.
[[nodiscard]] QString ApplyStep(
		Counter &counter,
		const Json &step,
		CaseTally &tally) {
	if (step.find("set")) {
		return ApplySet(counter, step);
	} else if (step.find("remove")) {
		return ApplyRemove(counter, step);
	} else if (step.find("start")) {
		return ApplyStart(counter, step);
	} else if (step.find("end")) {
		return ApplyEnd(counter, step);
	} else if (step["check"].isObject()) {
		++tally.checks;
		return ApplyCheck(counter, step["check"]);
	}
	++tally.unknown;
	return u"unknown step kind: "_q + JsonText(step);
}

// Every step runs, whatever an earlier one gave: one row per case.
void CheckCase(PolicyRun &run, const Json &entry, int number) {
	auto counter = Counter();
	auto tally = CaseTally();
	auto failures = QStringList();
	auto steps = 0;
	for (const auto &step : entry["steps"].arr()) {
		++steps;
		const auto failure = ApplyStep(counter, step, tally);
		if (!failure.isEmpty()) {
			failures.push_back(u"step %1: "_q.arg(steps) + failure);
		}
	}
	if (!tally.checks) {
		failures.push_back(u"no check step"_q);
	}
	run.steps += steps;
	run.checks += tally.checks;
	run.unknownSteps += tally.unknown;
	AddRow(
		run,
		failures.isEmpty(),
		EntryName("cases", number, StdText(entry["name"].asString())),
		u"steps=%1 checks=%2 heavy=%3 backdrop=%4 peakHeavy=%5 "
		"peakBackdrop=%6 failures=[%7]"_q.arg(
			QString::number(steps),
			QString::number(tally.checks),
			QString::number(counter.heavy()),
			QString::number(counter.backdrop()),
			QString::number(counter.peak_heavy()),
			QString::number(counter.peak_backdrop()),
			failures.join(u" | "_q)));
}

void CheckCases(PolicyRun &run, const Json &cases) {
	auto number = 0;
	for (const auto &entry : cases.arr()) {
		CheckCase(run, entry, ++number);
	}
}

// The sections of paint-policy.json at widget-sandbox v0.6.0, in order.
constexpr auto kSections = std::array{
	FixtureSection{ "constants", CheckConstants, true },
	FixtureSection{ "shadows", CheckShadows, false },
	FixtureSection{ "boxes", CheckBoxes, false },
	FixtureSection{ "hidesSubtree", CheckHidesSubtree, false },
	FixtureSection{ "animations", CheckAnimations, false },
	FixtureSection{ "messages", CheckMessages, false },
	FixtureSection{ "reports", CheckReports, false },
	FixtureSection{ "cases", CheckCases, false },
};

[[nodiscard]] bool KnownKey(const std::string &key) {
	if (key == kProseKey) {
		return true;
	}
	for (const auto &section : kSections) {
		if (key == section.key) {
			return true;
		}
	}
	return false;
}

// The process LC_NUMERIC, which the upstream rule's std::strtod follows.
[[nodiscard]] QString NumericLocaleText() {
	const auto name = std::setlocale(LC_NUMERIC, nullptr);
	const auto conventions = std::localeconv();
	const auto point = conventions ? conventions->decimal_point : nullptr;
	return u"LC_NUMERIC=%1 decimal=%2"_q.arg(
		name ? QString::fromLatin1(name) : u"none"_q,
		point ? QString::fromLatin1(point) : u"none"_q);
}

} // namespace

std::vector<WidgetOpsSelfCheckRow> RunWidgetPaintPolicyCheck(
		const QByteArray &fixture) {
	auto run = PolicyRun();
	const auto parsed = Json::parse(std::string_view(
		fixture.constData(),
		size_t(fixture.size())));
	if (!parsed) {
		AddRow(
			run,
			false,
			u"coverage"_q,
			u"bytes=%1 unparsable"_q.arg(QString::number(fixture.size())));
		return run.rows;
	}
	auto counts = QStringList();
	auto missing = QStringList();
	for (const auto &section : kSections) {
		const auto &entries = (*parsed)[section.key];
		const auto shaped = section.object
			? entries.isObject()
			: entries.isArray();
		const auto name = QString::fromLatin1(section.key);
		if (!shaped || !entries.size()) {
			missing.push_back(name);
		}
		counts.push_back(
			name + QChar('=') + QString::number(int(entries.size())));
		section.check(run, entries);
	}
	auto unknownKeys = QStringList();
	for (const auto &[key, value] : parsed->obj()) {
		if (!KnownKey(key)) {
			unknownKeys.push_back(StdText(key));
		}
	}
	const auto entryRows = int(run.rows.size());
	AddRow(
		run,
		parsed->isObject()
			&& missing.isEmpty()
			&& unknownKeys.isEmpty()
			&& !run.unknownSteps,
		u"coverage"_q,
		u"bytes=%1 %2 entryRows=%3 steps=%4 checks=%5 unknownSteps=%6 "
		"missing=[%7] unknownKeys=[%8] %9"_q.arg(
			QString::number(fixture.size()),
			counts.join(QChar(' ')),
			QString::number(entryRows),
			QString::number(run.steps),
			QString::number(run.checks),
			QString::number(run.unknownSteps),
			missing.join(QChar(' ')),
			unknownKeys.join(QChar(' ')),
			NumericLocaleText()));
	return run.rows;
}

} // namespace Test

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
