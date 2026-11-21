/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "test/test_widget_ops.h"

#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_agent.h"
#include "test/test_widget_ops_internal.h"
#include "tgw/tgw_renderer.h"
#include "tgw/tgw_renderer_input.h"
#include "tgw/tgw_runtime.h"
#include "twidget/json.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <string_view>
#include <utility>

namespace Test::WidgetOps {
namespace {

// The bytes all kept `data-stress` values of one host may have together.
constexpr auto kMaxStressBytes = int64(4) << 20;

// WidgetHost's kRenderAckQid: the qid of the host's own post-load query.
constexpr auto kAckQid = -1;

constexpr auto kResultAttribute = "data-conformance";
constexpr auto kStressAttribute = "data-stress";

// Integral doubles are exact up to 2^53; node ids stay far below.
constexpr auto kMaxNodeId = 9007199254740992.;

// Where the copied scanner below puts its matches, one sink for its leaks
// and one for its notes: every match counted, and its example built only
// while fewer than kMaxExamples are held within kMaxSampleBytes, so many
// matches in one message allocate no more examples than the tap keeps.
class ScanSink final {
public:
	void add(
		std::string_view where,
		std::string_view separator,
		std::string_view text);

	[[nodiscard]] int count() const;
	[[nodiscard]] std::vector<std::string> take();

private:
	std::vector<std::string> _examples;
	std::size_t _bytes = 0;
	int _count = 0;

};

void ScanSink::add(
		std::string_view where,
		std::string_view separator,
		std::string_view text) {
	++_count;
	if (int(_examples.size()) >= kMaxExamples) {
		return;
	}
	where = where.substr(0, kDetailBytes);
	const auto size = where.size() + separator.size() + text.size();
	if (_bytes + size > kMaxSampleBytes) {
		return;
	}
	auto example = std::string();
	example.reserve(size);
	example.append(where).append(separator).append(text);
	_bytes += size;
	_examples.push_back(std::move(example));
}

int ScanSink::count() const {
	return _count;
}

std::vector<std::string> ScanSink::take() {
	_bytes = 0;
	return base::take(_examples);
}

// The place a match names, |prefix| and the start of |name|, cut to
// kDetailBytes before anything is appended to it: an attribute or style
// name comes from the widget and can be as long as its message.
[[nodiscard]] std::string Where(
		std::string_view prefix,
		std::string_view name) {
	auto result = std::string(prefix.substr(0, kDetailBytes));
	result.append(name.substr(0, std::size_t(kDetailBytes) - result.size()));
	return result;
}

// The block below, from its "Anything in the validated ops" comment to the
// end of scanForLeaks(), is widget-sandbox's packages/desktop/harness/main.cpp
// leak definition at the v0.1.2 tag, which the --attacks console tool runs
// and Telegram does not build. Its predicates, prefixes and cuts are that
// file's, its `static` too, and `using namespace twidget` brings Json into
// scope as that file does. Only its output differs: each match goes to a
// ScanSink instead of a vector, as string views, and every place built from
// a widget's name goes through Where(), so no example is built past the
// sink's bounds. Re-diff its predicates against that file at every
// widget-sandbox pin move and carry a change over into this form.
namespace Upstream {

using namespace twidget;

/// Anything in the validated ops that could make a renderer touch the network.
///
/// `data:` payloads are opaque to the renderer's network stack: an SVG loaded as
/// an image may not fetch external resources (SVG-as-image rules in every
/// engine), so a URL *inside* a data: URL is reported as a note, not a leak —
/// the browser attack suite confirms zero canary hits for exactly these files.
/// In CSS only `url()`-family functions and `@import` can trigger a fetch; a
/// string token holding "http://…" (e.g. in a custom property) is inert.
static bool mentionsNetwork(std::string l) {
    std::transform(l.begin(), l.end(), l.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return l.find("127.0.0.1") != std::string::npos || l.find("http://") != std::string::npos || l.find("https://") != std::string::npos ||
           l.find("ws://") != std::string::npos || l.find("wss://") != std::string::npos || l.rfind("//", 0) == 0;
}

static void scanCssForLeaks(std::string_view where, const std::string& css, ScanSink& leaks) {
    std::string l = css;
    std::transform(l.begin(), l.end(), l.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (l.find("@import") != std::string::npos) leaks.add(where, ": @import survived: ", std::string_view(css).substr(0, 160));
    static const char* const fns[] = {"url(", "image-set(", "image(", "src(", "cross-fade("};
    for (const char* fn : fns) {
        size_t len = std::strlen(fn);
        for (size_t pos = l.find(fn); pos != std::string::npos; pos = l.find(fn, pos + len)) {
            if (pos > 0 && (std::isalnum((unsigned char)l[pos - 1]) || l[pos - 1] == '-')) continue; // e.g. "-webkit-image(" prefixes handled by their own match, "burl(" is not url(
            size_t a = pos + len;
            while (a < l.size() && (l[a] == ' ' || l[a] == '\t' || l[a] == '\n' || l[a] == '"' || l[a] == '\'')) a++;
            std::string arg = l.substr(a, 12);
            if (arg.rfind("data:", 0) == 0 || arg.rfind("#", 0) == 0) continue;
            if (std::strcmp(fn, "image-set(") == 0 && (arg.rfind("url(", 0) == 0 || arg.rfind("image(", 0) == 0)) continue; // nested, checked by its own match
            leaks.add(where, ": ", std::string_view(css).substr(pos, 160));
        }
    }
}

static void scanForLeaks(const Json& op, ScanSink& leaks, ScanSink& notes) {
    const std::string& kind = op["op"].asString();
    auto flagAttr = [&](std::string_view where, const std::string& value) {
        if (value.rfind("data:", 0) == 0) { if (mentionsNetwork(value)) notes.add(where, " (inert URL inside a data: payload): ", std::string_view(value).substr(0, 120)); return; }
        if (mentionsNetwork(value)) leaks.add(where, ": ", std::string_view(value).substr(0, 160));
    };
    if (kind == "el") {
        for (const auto& pair : op["attrs"].arr()) {
            const std::string& name = pair[0].asString();
            if (name == "style") scanCssForLeaks("el.style", pair[1].asString(), leaks);
            else flagAttr(Where("el.", name), pair[1].asString());
        }
    } else if (kind == "attr") {
        if (op["name"].asString() == "style") scanCssForLeaks("attr style", op["value"].asString(), leaks);
        else flagAttr(Where("attr ", op["name"].asString()), op["value"].asString());
    } else if (kind == "style") {
        scanCssForLeaks(Where("style ", op["name"].asString()), op["value"].asString(), leaks);
    } else if (kind == "css") {
        scanCssForLeaks("css", op["text"].asString(), leaks);
    } else if (kind == "canvas") {
        for (const auto& cmd : op["cmds"].arr())
            for (const auto& a : cmd.arr()) if (a.isString()) { scanCssForLeaks("canvas", a.asString(), leaks); flagAttr("canvas", a.asString()); }
    }
}

} // namespace Upstream

enum class ResultKind {
	Adopt,
	Conformance,
	Stress,
};

// An op of CLIENTS.md's result reader: an <html> adoption, or a
// `data-conformance` or `data-stress` value for element |id|, in batch
// order.
struct ResultOp {
	int64 id = 0;
	std::string value;
	ResultKind kind = ResultKind::Adopt;
};

struct HostScan {
	ScanSink leaks;
	ScanSink notes;
	std::vector<std::string> gaps;
	std::vector<ResultOp> results;
	std::optional<std::string> theme;
	int ops = 0;
	bool batch = false;
	bool ack = false;
};

struct RendererSeen {
	std::string detail;
	std::string eventType;
	std::string limitExample;
	std::string gap;
	bool violation = false;
	bool trustedEvent = false;
	bool ack = false;
	bool limit = false;
};

[[nodiscard]] bool IsAck(const twidget::Json &qid) {
	return qid.isNumber() && (qid.asInt() == kAckQid);
}

[[nodiscard]] std::string BudgetGap(
		std::string_view what,
		std::string_view json) {
	return std::string(what)
		+ " of "
		+ std::to_string(json.size())
		+ " bytes past the scan budget of "
		+ std::to_string(kMaxScanValues)
		+ " values: "
		+ std::string(json.substr(0, kGapBytes));
}

[[nodiscard]] std::optional<int64> NodeId(const twidget::Json &op) {
	const auto &id = op["id"];
	const auto value = id.asNumber();
	if (!id.isNumber() || !(std::abs(value) < kMaxNodeId)) {
		return std::nullopt;
	}
	return int64(value);
}

[[nodiscard]] std::optional<ResultKind> ValueKind(const std::string &name) {
	if (name == kResultAttribute) {
		return ResultKind::Conformance;
	} else if (name == kStressAttribute) {
		return ResultKind::Stress;
	}
	return std::nullopt;
}

// CLIENTS.md's "Reading the ops": an `el` op adopting <html> with its
// `data-conformance` and `data-stress` attributes, and every `attr` op of
// those names, whose element is matched against the adopted id only when
// merged.
void ReadResultOps(const twidget::Json &op, std::vector<ResultOp> &to) {
	const auto id = NodeId(op);
	if (!id) {
		return;
	}
	const auto &kind = op["op"].asString();
	if (kind == "el" && op["adopt"].asString() == "html") {
		to.push_back({ .id = *id, .kind = ResultKind::Adopt });
		for (const auto &pair : op["attrs"].arr()) {
			if (const auto attribute = ValueKind(pair[size_t(0)].asString())) {
				to.push_back({
					.id = *id,
					.value = pair[size_t(1)].asString(),
					.kind = *attribute,
				});
			}
		}
	} else if (kind == "attr") {
		if (const auto attribute = ValueKind(op["name"].asString())) {
			to.push_back({
				.id = *id,
				.value = op["value"].asString(),
				.kind = *attribute,
			});
		}
	}
}

void ClearResult(WidgetOpsLatest &latest) {
	if (latest.conformance) {
		latest.conformance = std::nullopt;
		++latest.serial;
	}
}

void ClearStress(WidgetOpsLatest &latest) {
	latest.stress.clear();
	latest.stressBytes = 0;
	latest.stressDropped = 0;
}

void AppendStress(
		WidgetOpsLatest &latest,
		std::string &&value,
		crl::profile_time receivedAt) {
	const auto bytes = int64(value.size());
	if ((int(latest.stress.size()) < kWidgetStressValuesLimit)
		&& (latest.stressBytes + bytes <= kMaxStressBytes)) {
		latest.stressBytes += bytes;
		latest.stress.push_back({
			.value = std::move(value),
			.receivedAt = receivedAt,
		});
	} else {
		++latest.stressDropped;
	}
}

void CountBatch(WidgetOpsLatest &latest, int ops) {
	++latest.batches;
	latest.ops += ops;
	latest.maxBatchOps = std::max(latest.maxBatchOps, ops);
}

void ApplyResults(
		WidgetOpsLatest &latest,
		std::vector<ResultOp> &&results,
		crl::profile_time receivedAt) {
	for (auto &op : results) {
		if (op.kind == ResultKind::Adopt) {
			ClearResult(latest);
			ClearStress(latest);
			latest.adopted = true;
			latest.htmlId = op.id;
		} else if (latest.adopted && (op.id == latest.htmlId)) {
			if (op.kind == ResultKind::Conformance) {
				latest.conformance = std::move(op.value);
				++latest.updates;
				++latest.serial;
			} else {
				AppendStress(latest, std::move(op.value), receivedAt);
			}
		}
	}
}

// A result value or theme message too long to keep is a gap, not kept.
void DropUnkept(HostScan &scan) {
	const auto unkept = [&](const ResultOp &op) {
		if ((op.kind == ResultKind::Adopt)
			|| (op.value.size() <= kMaxKeptValueBytes)) {
			return false;
		}
		scan.gaps.push_back("result value of "
			+ std::to_string(op.value.size())
			+ " bytes, past "
			+ std::to_string(kMaxKeptValueBytes)
			+ ": "
			+ op.value.substr(0, kGapBytes));
		return true;
	};
	scan.results.erase(
		std::remove_if(scan.results.begin(), scan.results.end(), unkept),
		scan.results.end());
	if (scan.theme && (scan.theme->size() > kMaxKeptValueBytes)) {
		scan.gaps.push_back("theme message of "
			+ std::to_string(scan.theme->size())
			+ " bytes, past "
			+ std::to_string(kMaxKeptValueBytes));
		scan.theme = std::nullopt;
	}
}

[[nodiscard]] HostScan ScanHostMessage(std::string_view json) {
	auto result = HostScan();
	if (!WithinScanBudget(json)) {
		result.gaps.push_back(BudgetGap("host message", json));
		return result;
	}
	const auto parsed = twidget::Json::parse(json);
	if (!parsed) {
		result.gaps.push_back("unparsable host message: "
			+ std::string(json.substr(0, kGapBytes)));
		return result;
	}
	const auto &message = *parsed;
	const auto &type = message["type"].asString();
	if (type == "ops") {
		const auto &ops = message["ops"];
		if (!ops.isArray()) {
			result.gaps.push_back("ops message without an ops array: "
				+ std::string(json.substr(0, kGapBytes)));
			return result;
		}
		result.batch = true;
		for (const auto &op : ops.arr()) {
			++result.ops;
			Upstream::scanForLeaks(op, result.leaks, result.notes);
			ReadResultOps(op, result.results);
		}
		DropUnkept(result);
	} else if (type == "theme") {
		result.theme = std::string(json);
		DropUnkept(result);
	} else if (type == "query") {
		result.ack = IsAck(message["qid"]);
	}
	return result;
}

// A `limit` report's `code` and `count` as they came: a string code bare,
// anything else, a missing member included, as its JSON text.
[[nodiscard]] std::string LimitExample(const twidget::Json &message) {
	const auto &code = message["code"];
	const auto text = code.isString() ? code.asString() : code.dump();
	return (text + ' ' + message["count"].dump()).substr(0, kDetailBytes);
}

// The `ev` of a renderer `event` message marked `trusted`, as
// WidgetHost::handleRendererMessage() reads one; null for any other.
[[nodiscard]] const twidget::Json *TrustedEvent(const twidget::Json &message) {
	return ((message["type"].asString() == "event")
			&& message["trusted"].asBool())
		? message.find("ev")
		: nullptr;
}

// The fields WidgetHost::handleRendererMessage() itself reads. A `limit`
// report is read whatever its code and count: the host judges those.
[[nodiscard]] RendererSeen ReadRendererMessage(std::string_view json) {
	auto result = RendererSeen();
	if (!WithinScanBudget(json)) {
		result.gap = BudgetGap("renderer message", json);
		return result;
	}
	const auto parsed = twidget::Json::parse(json);
	if (!parsed) {
		return result;
	}
	const auto &message = *parsed;
	const auto &type = message["type"].asString();
	if (type == "violation") {
		result.violation = true;
		result.detail = message["detail"].asString().substr(0, kDetailBytes);
	} else if (const auto ev = TrustedEvent(message)) {
		result.trustedEvent = true;
		result.eventType = (*ev)["type"].asString().substr(0, kDetailBytes);
	} else if (type == "result") {
		result.ack = IsAck(message["qid"]);
	} else if (type == "limit") {
		result.limit = true;
		result.limitExample = LimitExample(message);
	}
	return result;
}

void AppendExample(std::vector<std::string> &to, std::string example) {
	if (int(to.size()) < kMaxExamples) {
		to.push_back(std::move(example));
	}
}

void AppendExamples(
		std::vector<std::string> &to,
		std::vector<std::string> &&examples) {
	for (auto &example : examples) {
		AppendExample(to, std::move(example));
	}
}

} // namespace

bool WithinScanBudget(std::string_view json) {
	auto values = std::size_t();
	auto previous = '\0';
	auto quoted = false;
	auto escaped = false;
	for (const auto c : json) {
		if (quoted) {
			if (escaped) {
				escaped = false;
			} else if (c == '\\') {
				escaped = true;
			} else if (c == '"') {
				quoted = false;
				previous = c;
			}
			continue;
		} else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
			continue;
		}
		const auto starts = (previous == '\0')
			|| (previous == '[')
			|| (previous == '{')
			|| (previous == ',')
			|| (previous == ':');
		const auto value = (c != ']')
			&& (c != '}')
			&& (c != ',')
			&& (c != ':');
		if (starts && value && (++values > kMaxScanValues)) {
			return false;
		}
		quoted = (c == '"');
		previous = c;
	}
	return true;
}

ArmedMap &ArmedTaps() {
	static auto result = ArmedMap();
	return result;
}

} // namespace Test::WidgetOps

namespace Test {

using namespace WidgetOps;

int WidgetOpsTap::factorySelected() {
	auto lock = std::lock_guard(_mutex);
	const auto index = ++_reading.factoriesSelected;
	if (_reading.markedGeneration && !_reading.markSuperseded) {
		_reading.markSuperseded = index;
	}
	ClearResult(_latest);
	auto next = WidgetOpsLatest();
	next.generation = index;
	next.serial = _latest.serial;
	next.staleUpdates = _latest.staleUpdates;
	_latest = std::move(next);
	_pendingMessage.clear();
	_startedMessage.clear();
	_startedToken = 0;
	_startedRecorded = false;
	return index;
}

void WidgetOpsTap::factoryCompleted() {
	auto lock = std::lock_guard(_mutex);
	++_reading.factoriesCompleted;
}

void WidgetOpsTap::factoryReleased() {
	auto lock = std::lock_guard(_mutex);
	++_reading.factoriesReleased;
}

void WidgetOpsTap::wrapperDestroyed() {
	auto lock = std::lock_guard(_mutex);
	++_reading.wrappersDestroyed;
}

void WidgetOpsTap::hostedBuilt() {
	auto lock = std::lock_guard(_mutex);
	++_reading.wrappersBuilt;
}

void WidgetOpsTap::schemeReported(int selection, const std::string &scheme) {
	auto lock = std::lock_guard(_mutex);
	if (current(selection)) {
		_latest.scheme = scheme;
	}
}

void WidgetOpsTap::hostMessage(int selection, std::string_view json) {
	merge(selection, json);
}

void WidgetOpsTap::servedMessage(int selection, std::string_view json) {
	merge(selection, json);
}

void WidgetOpsTap::hostedSelected(int selection, uint64 work, bool awaited) {
	auto lock = std::lock_guard(_mutex);
	auto &runs = _reading.hostedRuns;
	if (int(runs.size()) >= kWidgetHostedRuns) {
		runs.erase(begin(runs));
	}
	runs.push_back({ .work = work, .generation = selection });
	if (awaited) {
		++_reading.hostedSelected;
	}
}

void WidgetOpsTap::hostedDied(int selection, const std::string &reason) {
	auto died = Tgw::ClipLogLine(reason);
	auto lock = std::lock_guard(_mutex);
	const auto run = hostedRun(selection);
	if (run && !run->died) {
		run->died = std::move(died);
	}
}

void WidgetOpsTap::hostedFinal(
		int selection,
		uint64 work,
		const Tgw::HostedDecision &decision) {
	using Outcome = Tgw::HostedOutcome;
	const auto outcome = decision.outcome;
	auto reason = Tgw::ClipLogLine(decision.reason);
	auto lock = std::lock_guard(_mutex);
	auto &reading = _reading;
	++reading.hostedFinal;
	switch (outcome) {
	case Outcome::None:
		++reading.unaffectedRuns;
		break;
	case Outcome::Spared:
		++reading.sparedRuns;
		break;
	case Outcome::Ordinary:
	case Outcome::Stalled:
		++reading.fatalFrozen;
		break;
	case Outcome::Refused:
		++reading.desktopCharges;
		break;
	}
	const auto run = hostedRun(selection);
	if (!run || (run->work != work)) {
		return;
	}
	run->reason = std::move(reason);
	run->outcome = outcome;
	run->transport = decision.transport;
	run->claimed = Tgw::ChargesCrash(outcome)
		&& (decision.transport == Outcome::None);
	run->final = true;
}

void WidgetOpsTap::hostRendered() {
	auto lock = std::lock_guard(_mutex);
	++_reading.renderedHosts;
}

void WidgetOpsTap::themeSelected(int selection, std::string message) {
	auto lock = std::lock_guard(_mutex);
	auto &latest = _latest;
	if (!current(selection) || latest.hostRetired) {
		return;
	} else if (message == _startedMessage) {
		latest.themePending = _startedRecorded ? 0 : _startedToken;
	} else if (!latest.themePending || (message != _pendingMessage)) {
		latest.themePending = ++_themeTokens;
	}
	_pendingMessage = latest.themePending ? std::move(message) : std::string();
}

uint64 WidgetOpsTap::themeScheduled(int selection, std::string message) {
	auto lock = std::lock_guard(_mutex);
	auto &latest = _latest;
	if (!current(selection) || latest.hostRetired) {
		return 0;
	} else if (!latest.themePending || (message != _pendingMessage)) {
		latest.themePending = ++_themeTokens;
		_pendingMessage = message;
	}
	_startedMessage = std::move(message);
	_startedToken = latest.themePending;
	_startedRecorded = false;
	return latest.themePending;
}

int WidgetOpsTap::themeApplied(
		int selection,
		std::string scheme,
		std::string message,
		uint64 token) {
	auto lock = std::lock_guard(_mutex);
	if (!current(selection)) {
		return 0;
	}
	auto &latest = _latest;
	if (latest.hostRetired) {
		++latest.effectiveMissed;
		latest.effectiveError = "the host was retired before theme "
			+ std::to_string(latest.appliedThemes + 1)
			+ " was applied to it";
		return 0;
	}
	if (!token) {
		_startedMessage = message;
		_startedToken = 0;
		_startedRecorded = true;
	} else if (token == _startedToken) {
		_startedRecorded = true;
	}
	if (token
		&& (token == latest.themePending)
		&& (message == _pendingMessage)) {
		latest.themePending = 0;
		_pendingMessage.clear();
	}
	latest.appliedScheme = std::move(scheme);
	latest.appliedThemeMessage = std::move(message);
	return ++latest.appliedThemes;
}

void WidgetOpsTap::themeEffective(
		int selection,
		int serial,
		EffectiveReading seen) {
	auto lock = std::lock_guard(_mutex);
	if (!current(selection)) {
		return;
	}
	auto &latest = _latest;
	if (latest.hostRetired) {
		seen.missed = "a reading for theme "
			+ std::to_string(serial)
			+ " after its host was retired";
	} else if (seen.missed.empty() && (serial != latest.appliedThemes)) {
		seen.missed = "a reading for theme "
			+ std::to_string(serial)
			+ ", the newest applied is theme "
			+ std::to_string(latest.appliedThemes);
	}
	if (!seen.missed.empty()) {
		++latest.effectiveMissed;
		latest.effectiveError = std::move(seen.missed);
		return;
	}
	if (!kHelperMirrors) {
		if (latest.effectiveThemes && (latest.scheme != seen.scheme)) {
			++latest.schemeChanges;
		}
		latest.scheme = seen.scheme;
		latest.themeMessage = std::move(seen.message);
	}
	++latest.effectiveThemes;
	latest.effectiveSerial = serial;
	latest.effectiveScheme = std::move(seen.scheme);
	latest.effectiveParams = std::move(seen.params);
	latest.effectiveMismatch = std::move(seen.mismatch);
}

void WidgetOpsTap::hostRetired(int selection, uint64 work) {
	auto lock = std::lock_guard(_mutex);
	if (newest(selection)) {
		_latest.hostRetired = true;
	}
	auto &reading = _reading;
	if (marks(selection, work) && !reading.clicksCompleted) {
		reading.markDetached = true;
	}
}

bool WidgetOpsTap::marks(int selection, uint64 work) const {
	const auto &reading = _reading;
	return reading.markedSerial
		&& !reading.markSuperseded
		&& !reading.markDetached
		&& (selection == reading.markedGeneration)
		&& (work == reading.markedWork);
}

void WidgetOpsTap::pressDispatched(
		int selection,
		uint64 work,
		uint64 serial) {
	auto lock = std::lock_guard(_mutex);
	auto &reading = _reading;
	if ((_markPress != MarkPress::Open)
		|| reading.markSuperseded
		|| reading.markedSerial
		|| (selection != reading.markedGeneration)
		|| !serial) {
		return;
	}
	reading.markedWork = work;
	reading.markedSerial = serial;
}

void WidgetOpsTap::pressDelivery(bool open) {
	auto lock = std::lock_guard(_mutex);
	if (!open) {
		_markPress = MarkPress::Sealed;
	} else if (_markPress == MarkPress::Awaiting) {
		_markPress = _reading.markSuperseded
			? MarkPress::Sealed
			: MarkPress::Open;
	}
}

void WidgetOpsTap::pressObserved(
		int selection,
		uint64 work,
		uint64 press,
		const Tgw::PressStep &step) {
	using Kind = Tgw::PressStep::Kind;
	auto lock = std::lock_guard(_mutex);
	auto &reading = _reading;
	const auto marked = marks(selection, work) && press;
	const auto played = marked
		&& (step.kind == Kind::Sent)
		&& (step.serial == reading.markedSerial);
	if (played && !reading.markedPress) {
		reading.markedPress = press;
		return;
	} else if (!marked
		|| (press != reading.markedPress)
		|| (step.kind == Kind::Sent)
		|| ((step.kind == Kind::Click) && !reading.upsConfirmed)) {
		++reading.pressStepsElsewhere;
		return;
	}
	switch (step.kind) {
	case Kind::Down: ++reading.downsConfirmed; break;
	case Kind::Up: ++reading.upsConfirmed; break;
	case Kind::Click: ++reading.clicksCompleted; break;
	case Kind::Sent: break;
	}
}

void WidgetOpsTap::detached(int selection, uint64 work) {
	auto lock = std::lock_guard(_mutex);
	auto &reading = _reading;
	if (!marks(selection, work) || reading.clicksCompleted) {
		return;
	}
	reading.markDetached = true;
}

void WidgetOpsTap::gap(std::string example) {
	auto lock = std::lock_guard(_mutex);
	++_reading.scanGaps;
	AppendExample(_reading.gapExamples, std::move(example));
}

void WidgetOpsTap::merge(int selection, std::string_view json) {
	const auto receivedAt = crl::profile();
	auto scan = ScanHostMessage(json);
	auto lock = std::lock_guard(_mutex);
	auto &reading = _reading;
	++reading.hostMessages;
	reading.scanGaps += int(scan.gaps.size());
	AppendExamples(reading.gapExamples, std::move(scan.gaps));
	if (scan.batch) {
		++reading.batches;
		reading.ops += scan.ops;
		reading.leaks += scan.leaks.count();
		reading.notes += scan.notes.count();
		AppendExamples(reading.leakExamples, scan.leaks.take());
		AppendExamples(reading.noteExamples, scan.notes.take());
		if (newest(selection)) {
			CountBatch(_latest, scan.ops);
		}
	}
	if (scan.ack) {
		++reading.ackQueries;
	}
	if ((scan.results.empty() && !scan.theme) || !current(selection)) {
		return;
	}
	ApplyResults(_latest, std::move(scan.results), receivedAt);
	if (scan.theme) {
		_latest.themeMessage = std::move(*scan.theme);
	}
}

void WidgetOpsTap::themeSet(int selection, const std::string &theme) {
	auto lock = std::lock_guard(_mutex);
	if (!current(selection) || (_latest.scheme == theme)) {
		return;
	}
	_latest.scheme = theme;
	++_latest.schemeChanges;
}

bool WidgetOpsTap::newest(int selection) const {
	return (selection == _latest.generation);
}

bool WidgetOpsTap::current(int selection) {
	if (newest(selection)) {
		return true;
	}
	++_latest.staleUpdates;
	return false;
}

WidgetHostedRun *WidgetOpsTap::hostedRun(int selection) {
	auto &runs = _reading.hostedRuns;
	const auto i = ranges::find(
		runs,
		selection,
		&WidgetHostedRun::generation);
	return (i != end(runs)) ? &*i : nullptr;
}

void WidgetOpsTap::rendererMessage(int selection, std::string_view json) {
	auto seen = ReadRendererMessage(json);
	if (!seen.violation
		&& !seen.trustedEvent
		&& !seen.ack
		&& !seen.limit
		&& seen.gap.empty()) {
		return;
	}
	auto lock = std::lock_guard(_mutex);
	auto &reading = _reading;
	if (!seen.gap.empty()) {
		++reading.scanGaps;
		AppendExample(reading.gapExamples, std::move(seen.gap));
	}
	if (seen.violation) {
		++reading.violations;
		AppendExample(reading.violationDetails, std::move(seen.detail));
	}
	if (seen.limit) {
		++reading.limitReports;
		AppendExample(reading.limitExamples, std::move(seen.limitExample));
	}
	if (seen.trustedEvent
		&& (!reading.markedGeneration
			|| ((selection == reading.markedGeneration)
				&& !reading.markSuperseded))) {
		++reading.trustedEvents;
		if (seen.eventType == "click") {
			++reading.trustedClicks;
		}
		AppendExample(reading.trustedEventTypes, std::move(seen.eventType));
	}
	if (seen.ack) {
		++reading.ackResults;
	}
}

void WidgetOpsTap::markEvents() {
	auto lock = std::lock_guard(_mutex);
	auto &reading = _reading;
	reading.trustedEvents = 0;
	reading.trustedClicks = 0;
	reading.trustedEventTypes.clear();
	reading.markedGeneration = _latest.generation;
	reading.markedWork = 0;
	reading.markedSerial = 0;
	reading.markedPress = 0;
	reading.downsConfirmed = 0;
	reading.upsConfirmed = 0;
	reading.clicksCompleted = 0;
	reading.pressStepsElsewhere = 0;
	reading.markSuperseded = 0;
	reading.markDetached = false;
	_markPress = MarkPress::Awaiting;
}

WidgetOpsReading WidgetOpsTap::read() const {
	auto lock = std::lock_guard(_mutex);
	return _reading;
}

WidgetOpsLatest WidgetOpsTap::latest() const {
	auto lock = std::lock_guard(_mutex);
	return _latest;
}

void WidgetOpsTap::askWork() {
	auto lock = std::lock_guard(_mutex);
	_work.asked = true;
}

bool WidgetOpsTap::asksWork() const {
	auto lock = std::lock_guard(_mutex);
	return _work.asked;
}

void WidgetOpsTap::reserveWork(int capacity) {
	auto room = std::vector<WidgetHelperWorkEvent>();
	room.reserve(std::max(capacity, 0));
	const auto reservedAt = crl::profile();
	{
		auto lock = std::lock_guard(_mutex);
		std::swap(_work.events, room);
		_work.reservedAt = reservedAt;
		_work.capacity = std::max(capacity, 0);
		_work.dropped = 0;
		_work.malformed = 0;
	}
}

void WidgetOpsTap::helperWork(WidgetHelperWorkEvent event) {
	auto lock = std::lock_guard(_mutex);
	if (int(_work.events.size()) < _work.capacity) {
		_work.events.push_back(event);
	} else {
		++_work.dropped;
	}
}

void WidgetOpsTap::helperWorkMalformed() {
	auto lock = std::lock_guard(_mutex);
	++_work.malformed;
	++_work.dropped;
}

WidgetHelperWorkReading WidgetOpsTap::work(crl::profile_time since) const {
	auto lock = std::lock_guard(_mutex);
	auto result = WidgetHelperWorkReading{
		.reservedAt = _work.reservedAt,
		.capacity = _work.capacity,
		.dropped = _work.dropped,
		.malformed = _work.malformed,
		.asked = _work.asked,
	};
	for (const auto &event : _work.events) {
		if (event.finished >= since) {
			result.events.push_back(event);
		}
	}
	return result;
}

std::shared_ptr<WidgetOpsTap> ArmWidgetOpsTap(GlobalMsgId id) {
	auto &tap = ArmedTaps()[id];
	if (!tap) {
		tap = std::make_shared<WidgetOpsTap>();
	}
	return tap;
}

void DisarmWidgetOpsTap(GlobalMsgId id) {
	ArmedTaps().erase(id);
}

WidgetOpsReading ReadWidgetOps(const WidgetOpsTap &tap) {
	return tap.read();
}

bool WidgetTapFinished(const WidgetOpsReading &reading) {
	return (reading.factoriesReleased == reading.factoriesSelected)
		&& (reading.wrappersDestroyed == reading.wrappersBuilt)
		&& (reading.hostedFinal == reading.hostedSelected);
}

WidgetOpsLatest ReadWidgetOpsLatest(const WidgetOpsTap &tap) {
	return tap.latest();
}

void MarkWidgetOpsEvents(WidgetOpsTap &tap) {
	tap.markEvents();
}

void DeliverMarkedPress(WidgetOpsTap &tap, Fn<void()> deliver) {
	tap.pressDelivery(true);
	const auto seal = gsl::finally([&] {
		tap.pressDelivery(false);
	});
	if (deliver) {
		deliver();
	}
}

bool WidgetHelperMirrors() {
	return kHelperMirrors;
}

void TimeWidgetHelperWork(WidgetOpsTap &tap) {
	tap.askWork();
}

void ReserveWidgetHelperWork(WidgetOpsTap &tap, int capacity) {
	tap.reserveWork(capacity);
}

WidgetHelperWorkReading ReadWidgetHelperWork(
		const WidgetOpsTap &tap,
		crl::profile_time since) {
	return tap.work(since);
}

} // namespace Test

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
