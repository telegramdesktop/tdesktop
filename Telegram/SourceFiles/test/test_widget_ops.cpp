/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "test/test_widget_ops.h"

#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_agent.h"
#include "twidget/json.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QStringList>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <exception>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace Test {
namespace {

constexpr auto kMaxExamples = 64;
constexpr auto kGapBytes = 160;
constexpr auto kDetailBytes = 240;

// WidgetHost's kRenderAckQid: the qid of the host's own post-load query.
constexpr auto kAckQid = -1;

constexpr auto kResultAttribute = "data-conformance";
constexpr auto kStressAttribute = "data-stress";

// Integral doubles are exact up to 2^53; node ids stay far below.
constexpr auto kMaxNodeId = 9007199254740992.;

// The block below, from its "Anything in the validated ops" comment to the
// end of scanForLeaks(), is a verbatim copy of widget-sandbox's
// packages/desktop/harness/main.cpp at the v0.1.2 tag: the --attacks leak
// definition, in a console tool Telegram does not build. Nothing inside it
// is edited, its `static` included, and `using namespace twidget` brings Json
// into scope as that file does. Re-diff it against that file at every
// widget-sandbox pin move and copy the new block whole.
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

static void scanCssForLeaks(const std::string& where, const std::string& css, std::vector<std::string>& leaks) {
    std::string l = css;
    std::transform(l.begin(), l.end(), l.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (l.find("@import") != std::string::npos) leaks.push_back(where + ": @import survived: " + css.substr(0, 160));
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
            leaks.push_back(where + ": " + css.substr(pos, 160));
        }
    }
}

static void scanForLeaks(const Json& op, std::vector<std::string>& leaks, std::vector<std::string>& notes) {
    const std::string& kind = op["op"].asString();
    auto flagAttr = [&](const std::string& where, const std::string& value) {
        if (value.rfind("data:", 0) == 0) { if (mentionsNetwork(value)) notes.push_back(where + " (inert URL inside a data: payload): " + value.substr(0, 120)); return; }
        if (mentionsNetwork(value)) leaks.push_back(where + ": " + value.substr(0, 160));
    };
    if (kind == "el") {
        for (const auto& pair : op["attrs"].arr()) {
            const std::string& name = pair[0].asString();
            if (name == "style") scanCssForLeaks("el.style", pair[1].asString(), leaks);
            else flagAttr("el." + name, pair[1].asString());
        }
    } else if (kind == "attr") {
        if (op["name"].asString() == "style") scanCssForLeaks("attr style", op["value"].asString(), leaks);
        else flagAttr("attr " + op["name"].asString(), op["value"].asString());
    } else if (kind == "style") {
        scanCssForLeaks("style " + op["name"].asString(), op["value"].asString(), leaks);
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
	std::vector<std::string> leaks;
	std::vector<std::string> notes;
	std::vector<ResultOp> results;
	std::optional<std::string> theme;
	std::string gap;
	int ops = 0;
	bool gapped = false;
	bool batch = false;
	bool ack = false;
	bool frozen = false;
};

struct RendererSeen {
	std::string detail;
	std::string eventType;
	std::string limitExample;
	bool violation = false;
	bool trustedEvent = false;
	bool ack = false;
	bool limit = false;
};

} // namespace

class WidgetOpsTap final {
public:
	// Returns the selection's 1-based index, its host's generation.
	[[nodiscard]] int factorySelected();
	void factoryCompleted(bool threw);
	void factoryReleased();
	void wrapperBuilt(int selection, const std::string &theme);
	void wrapperDestroyed();

	// Parse and scan outside the lock, merge under it; never call out.
	void hostMessage(int selection, const std::string &json);
	void themeSet(int selection, const std::string &theme);
	void rendererMessage(const std::string &json);

	void markEvents();
	[[nodiscard]] WidgetOpsReading read() const;
	[[nodiscard]] WidgetOpsLatest latest() const;

private:
	// Under the lock: whether |selection| is the newest host's.
	[[nodiscard]] bool newest(int selection) const;
	// Under the lock: false, counted as stale, for an older host's wrapper.
	[[nodiscard]] bool current(int selection);

	mutable std::mutex _mutex;
	WidgetOpsReading _reading;
	WidgetOpsLatest _latest;

};

namespace {

// Forwards every call and callback straight to the product renderer, on
// the calling thread, recording on the way. WidgetHost assigns this
// object's own onMessage / onDiag after the factory returned, so the inner
// callbacks read them when invoked, not when installed.
class TapRenderer final : public twidget::Renderer {
public:
	TapRenderer(
		std::unique_ptr<twidget::Renderer> inner,
		std::shared_ptr<WidgetOpsTap> tap,
		int selection,
		const std::string &theme);
	~TapRenderer();

	void load() override;
	void send(const std::string &json) override;
	void setTheme(const std::string &theme) override;
	void setWidth(double width) override;
	void destroy() override;

private:
	void received(const std::string &json);
	void diagnosed(const std::string &kind, const std::string &detail);

	std::unique_ptr<twidget::Renderer> _inner;
	const std::shared_ptr<WidgetOpsTap> _tap;
	const int _selection = 0;

};

// Shared by every copy of one wrapping factory: released with the last.
class FactorySelection final {
public:
	FactorySelection(std::shared_ptr<WidgetOpsTap> tap, int index);
	FactorySelection(const FactorySelection &other) = delete;
	FactorySelection &operator=(const FactorySelection &other) = delete;
	~FactorySelection();

	[[nodiscard]] const std::shared_ptr<WidgetOpsTap> &tap() const;
	[[nodiscard]] int index() const;

private:
	const std::shared_ptr<WidgetOpsTap> _tap;
	const int _index = 0;

};

// The self-check's stand-in for the product renderer and its factory.

class StubRenderer;

struct StubCall {
	std::string text;
	std::thread::id thread;
};

struct StubState {
	std::vector<StubCall> log;
	std::shared_ptr<twidget::SandboxResources> resources;
	std::shared_ptr<WidgetOpsTap> tap;
	std::string theme;
	std::thread::id factoryThread;
	StubRenderer *built = nullptr;
	const std::string *lastSent = nullptr;
	double width = 0.;
	int factoryCalls = 0;
	int destroyedSeenByDestructor = -1;
	bool fail = false;
};

class StubRenderer final : public twidget::Renderer {
public:
	explicit StubRenderer(std::shared_ptr<StubState> state);
	~StubRenderer();

	void load() override;
	void send(const std::string &json) override;
	void setTheme(const std::string &theme) override;
	void setWidth(double width) override;
	void destroy() override;

	void emitMessage(const std::string &json);
	void emitDiag(const std::string &kind, const std::string &detail);

private:
	void called(std::string text);

	const std::shared_ptr<StubState> _state;

};

struct StubFactory {
	std::shared_ptr<StubState> state;

	std::unique_ptr<twidget::Renderer> operator()(
		std::shared_ptr<twidget::SandboxResources> resources,
		const std::string &theme,
		double width) const;
};

// The self-check's inputs and its independently written expectations.

constexpr auto kFactoryFailure = "widget ops self-check factory failure";
constexpr auto kTheme = "dark";
constexpr auto kWidth = 240.5;
constexpr auto kNextTheme = "light";
constexpr auto kNextWidth = 512.25;

constexpr auto kControlBatch = R"json({"type":"ops","ops":[)json"
	R"json({"op":"el","id":5,"tag":"img","attrs":[)json"
	R"json(["src","https://127.0.0.1:9999/leak.png"],)json"
	R"json(["style","background:url(https://127.0.0.1:9999/leak.css)"]]},)json"
	R"json({"op":"attr","id":5,"name":"data-x",)json"
	R"json("value":"data:image/svg+xml,)json"
	R"json(<svg xmlns='http://www.w3.org/2000/svg'/>"},)json"
	R"json({"op":"style","id":5,"name":"background-image",)json"
	R"json("value":"url(data:image/png;base64,AAAA)"},)json"
	R"json({"op":"style","id":5,"name":"fill","value":"url(#g)"}]})json";
constexpr auto kUnparsable = R"json({"type":"ops","ops":[)json";
constexpr auto kNotArray = R"json({"type":"ops","ops":{"op":"rm","id":5}})json";
constexpr auto kAckQuery = R"json({"type":"query","qid":-1,)json"
	R"json("query":{"q":"viewport"}})json";
constexpr auto kAckResult = R"json({"type":"result","qid":-1,)json"
	R"json("result":{"innerWidth":512.25,"innerHeight":300,"dpr":2}})json";
constexpr auto kRemovalBatch = R"json({"type":"ops","ops":[)json"
	R"json({"op":"rm","id":5}]})json";
constexpr auto kFrozen = R"json({"type":"frozen",)json"
	R"json("reason":"Widget stopped","fatal":true})json";
constexpr auto kPressEvent = R"json({"type":"event",)json"
	R"json("ev":{"type":"pointerdown","targetId":2,"detail":0},)json"
	R"json("trusted":true})json";
constexpr auto kTrustedClick = R"json({"type":"event",)json"
	R"json("ev":{"type":"click","targetId":2,"detail":1},)json"
	R"json("trusted":true})json";
constexpr auto kUntrustedClick = R"json({"type":"event",)json"
	R"json("ev":{"type":"click","targetId":2,"detail":1},)json"
	R"json("trusted":false})json";
constexpr auto kViolationDetail
	= "image refused: https://127.0.0.1:9999/refused.png";
constexpr auto kViolation = R"json({"type":"violation",)json"
	R"json("detail":"image refused: https://127.0.0.1:9999/refused.png"})json";
constexpr auto kPaintReport = R"json({"type":"limit",)json"
	R"json("code":"paint-limit","count":17})json";
constexpr auto kBackdropReport = R"json({"type":"limit",)json"
	R"json("code":"backdrop-limit","count":3})json";
constexpr auto kPaintExample = "paint-limit 17";
constexpr auto kBackdropExample = "backdrop-limit 3";
constexpr auto kDiagKind = "renderer-message";
constexpr auto kMainDiag = "self-check diag on the main thread";
constexpr auto kWorkerDiag = "self-check diag on a worker thread";
constexpr auto kReturnedMark = "self-check: send() returned";

constexpr auto kLeakSrc = "el.src: https://127.0.0.1:9999/leak.png";
constexpr auto kLeakStyle = "el.style: url(https://127.0.0.1:9999/leak.css)";
constexpr auto kNoteData = "attr data-x (inert URL inside a data: payload): "
	"data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg'/>";

// The conformance probe check's own legs and their expected values.

constexpr auto kOldAdopt = R"json({"type":"ops","ops":[)json"
	R"json({"op":"el","id":2,"tag":"html","attrs":[["data-conformance",)json"
	R"json("{\"id\":\"probe/old\",\"status\":\"running\"}"]],)json"
	R"json("adopt":"html"}]})json";
constexpr auto kOldDone = R"json({"type":"ops","ops":[)json"
	R"json({"op":"attr","id":2,"name":"data-conformance",)json"
	R"json("value":"{\"id\":\"probe/old\",\"status\":\"done\"}"}]})json";
constexpr auto kNewAdopt = R"json({"type":"ops","ops":[)json"
	R"json({"op":"el","id":2,"tag":"html","attrs":[],"adopt":"html"}]})json";
constexpr auto kNewRunning = R"json({"type":"ops","ops":[)json"
	R"json({"op":"attr","id":2,"name":"data-conformance",)json"
	R"json("value":"{\"id\":\"probe/new\",\"status\":\"running\"}"}]})json";
constexpr auto kReadopt = R"json({"type":"ops","ops":[)json"
	R"json({"op":"el","id":6,"tag":"html","attrs":[],"adopt":"html"},)json"
	R"json({"op":"attr","id":2,"name":"data-conformance",)json"
	R"json("value":"{\"id\":\"probe/new\",\"status\":\"done\"}"}]})json";
constexpr auto kThemeMessage = R"json({"type":"theme","theme":{)json"
	R"json("colorScheme":"light","params":{"bg_color":"#ffffff",)json"
	R"json("text_color":"#000000"},"button":{"radius":"8px"},)json"
	R"json("text":{"fontSize":14}}})json";

constexpr auto kOldDoneValue = R"json({"id":"probe/old","status":"done"})json";
constexpr auto kNewRunningValue
	= R"json({"id":"probe/new","status":"running"})json";
constexpr auto kHtmlId = 2;
constexpr auto kReadoptHtmlId = 6;
constexpr auto kProbeTextChars = 400;

// The stress leg's batches: the older host's adoption with a value; the
// newer host's 1, 3 and 5 ops with one value on <html> each (a value on
// another element and a `rattr` ignored); the older host's late 6 ops with
// a value on the same id; the newer host's 2 ops without a value and its
// 2-op re-adoption followed by a value on the earlier id.

constexpr auto kStressOldAdopt = R"json({"type":"ops","ops":[)json"
	R"json({"op":"el","id":2,"tag":"html","attrs":[["data-stress",)json"
	R"json("{\"v\":1,\"id\":\"self/old\",\"status\":\"running\"}"]],)json"
	R"json("adopt":"html"}]})json";
constexpr auto kStressAdopt = R"json({"type":"ops","ops":[)json"
	R"json({"op":"el","id":2,"tag":"html","attrs":[["data-stress",)json"
	R"json("{\"v\":1,\"id\":\"self/new\",\"status\":\"setup\"}"]],)json"
	R"json("adopt":"html"}]})json";
constexpr auto kStressThree = R"json({"type":"ops","ops":[)json"
	R"json({"op":"el","id":3,"tag":"div","attrs":[]},)json"
	R"json({"op":"style","id":3,"name":"opacity","value":"0.5"},)json"
	R"json({"op":"attr","id":2,"name":"data-stress","value":"{\"v\":1,)json"
	R"json(\"id\":\"self/new\",\"status\":\"running\",\"t\":0}"}]})json";
constexpr auto kStressFive = R"json({"type":"ops","ops":[)json"
	R"json({"op":"style","id":3,"name":"opacity","value":"0.25"},)json"
	R"json({"op":"attr","id":3,"name":"data-stress","value":"elsewhere"},)json"
	R"json({"op":"rattr","id":2,"name":"data-stress"},)json"
	R"json({"op":"style","id":3,"name":"width","value":"10px"},)json"
	R"json({"op":"attr","id":2,"name":"data-stress","value":"{\"v\":1,)json"
	R"json(\"id\":\"self/new\",\"status\":\"running\",\"t\":500}"}]})json";
constexpr auto kStressLate = R"json({"type":"ops","ops":[)json"
	R"json({"op":"style","id":3,"name":"opacity","value":"0.1"},)json"
	R"json({"op":"style","id":3,"name":"opacity","value":"0.2"},)json"
	R"json({"op":"style","id":3,"name":"opacity","value":"0.3"},)json"
	R"json({"op":"style","id":3,"name":"opacity","value":"0.4"},)json"
	R"json({"op":"style","id":3,"name":"opacity","value":"0.6"},)json"
	R"json({"op":"attr","id":2,"name":"data-stress","value":"late"}]})json";
constexpr auto kStressPlain = R"json({"type":"ops","ops":[)json"
	R"json({"op":"style","id":3,"name":"opacity","value":"0.7"},)json"
	R"json({"op":"style","id":3,"name":"opacity","value":"0.8"}]})json";
constexpr auto kStressReadopt = R"json({"type":"ops","ops":[)json"
	R"json({"op":"el","id":7,"tag":"html","attrs":[],"adopt":"html"},)json"
	R"json({"op":"attr","id":2,"name":"data-stress","value":"old id"}]})json";

constexpr auto kStressSetupValue
	= R"json({"v":1,"id":"self/new","status":"setup"})json";
constexpr auto kStressStartValue
	= R"json({"v":1,"id":"self/new","status":"running","t":0})json";
constexpr auto kStressHalfValue
	= R"json({"v":1,"id":"self/new","status":"running","t":500})json";
constexpr auto kStressHtmlId = 2;
constexpr auto kStressReadoptHtmlId = 7;
constexpr auto kStressShownValues = 3;

[[nodiscard]] bool IsAck(const twidget::Json &qid) {
	return qid.isNumber() && (qid.asInt() == kAckQid);
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
	latest.stressDropped = 0;
}

void AppendStress(
		WidgetOpsLatest &latest,
		std::string &&value,
		crl::profile_time receivedAt) {
	if (int(latest.stress.size()) < kWidgetStressValuesLimit) {
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

[[nodiscard]] HostScan ScanHostMessage(const std::string &json) {
	auto result = HostScan();
	const auto parsed = twidget::Json::parse(json);
	if (!parsed) {
		result.gapped = true;
		result.gap = "unparsable host message: " + json.substr(0, kGapBytes);
		return result;
	}
	const auto &message = *parsed;
	const auto &type = message["type"].asString();
	if (type == "ops") {
		const auto &ops = message["ops"];
		if (!ops.isArray()) {
			result.gapped = true;
			result.gap = "ops message without an ops array: "
				+ json.substr(0, kGapBytes);
			return result;
		}
		result.batch = true;
		for (const auto &op : ops.arr()) {
			++result.ops;
			Upstream::scanForLeaks(op, result.leaks, result.notes);
			ReadResultOps(op, result.results);
		}
	} else if (type == "theme") {
		result.theme = json;
	} else if (type == "query") {
		result.ack = IsAck(message["qid"]);
	} else if (type == "frozen") {
		result.frozen = message["fatal"].asBool();
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

// The fields WidgetHost::handleRendererMessage() itself reads. A `limit`
// report is read whatever its code and count: the host judges those.
[[nodiscard]] RendererSeen ReadRendererMessage(const std::string &json) {
	auto result = RendererSeen();
	const auto parsed = twidget::Json::parse(json);
	if (!parsed) {
		return result;
	}
	const auto &message = *parsed;
	const auto &type = message["type"].asString();
	if (type == "violation") {
		result.violation = true;
		result.detail = message["detail"].asString().substr(0, kDetailBytes);
	} else if (type == "event") {
		const auto ev = message.find("ev");
		if (ev && message["trusted"].asBool()) {
			result.trustedEvent = true;
			result.eventType = (*ev)["type"].asString().substr(
				0,
				kDetailBytes);
		}
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

// Keyed by session too: local message ids are allocated per session.
using ArmedMap = std::map<GlobalMsgId, std::shared_ptr<WidgetOpsTap>>;

[[nodiscard]] ArmedMap &ArmedTaps() {
	static auto result = ArmedMap();
	return result;
}

std::unique_ptr<twidget::Renderer> BuildTapped(
		const std::shared_ptr<WidgetOpsTap> &tap,
		int selection,
		const twidget::RendererFactory &factory,
		std::shared_ptr<twidget::SandboxResources> resources,
		const std::string &theme,
		double width) {
	auto inner = std::unique_ptr<twidget::Renderer>();
	try {
		inner = factory(std::move(resources), theme, width);
	} catch (...) {
		tap->factoryCompleted(true);
		throw;
	}
	tap->factoryCompleted(false);
	if (!inner) {
		// WidgetHost throws on a null renderer, as it would without the tap.
		return inner;
	}
	return std::make_unique<TapRenderer>(
		std::move(inner),
		tap,
		selection,
		theme);
}

[[nodiscard]] twidget::RendererFactory WrapFactory(
		std::shared_ptr<WidgetOpsTap> tap,
		int index,
		twidget::RendererFactory factory) {
	const auto selection = std::make_shared<FactorySelection>(
		std::move(tap),
		index);
	return [selection, factory = std::move(factory)](
			std::shared_ptr<twidget::SandboxResources> resources,
			const std::string &theme,
			double width) {
		return BuildTapped(
			selection->tap(),
			selection->index(),
			factory,
			std::move(resources),
			theme,
			width);
	};
}

TapRenderer::TapRenderer(
	std::unique_ptr<twidget::Renderer> inner,
	std::shared_ptr<WidgetOpsTap> tap,
	int selection,
	const std::string &theme)
: _inner(std::move(inner))
, _tap(std::move(tap))
, _selection(selection) {
	_inner->onMessage = [this](const std::string &json) {
		received(json);
	};
	_inner->onDiag = [this](
			const std::string &kind,
			const std::string &detail) {
		diagnosed(kind, detail);
	};
	_tap->wrapperBuilt(_selection, theme);
}

TapRenderer::~TapRenderer() {
	// The product renderer detaches from its link first, which waits for a
	// callback in flight, so a destroyed wrapper has no renderer left.
	_inner = nullptr;
	_tap->wrapperDestroyed();
}

void TapRenderer::load() {
	_inner->load();
}

void TapRenderer::send(const std::string &json) {
	_tap->hostMessage(_selection, json);
	_inner->send(json);
}

void TapRenderer::setTheme(const std::string &theme) {
	_tap->themeSet(_selection, theme);
	_inner->setTheme(theme);
}

void TapRenderer::setWidth(double width) {
	_inner->setWidth(width);
}

void TapRenderer::destroy() {
	_inner->destroy();
}

void TapRenderer::received(const std::string &json) {
	_tap->rendererMessage(json);
	if (onMessage) {
		onMessage(json);
	}
}

void TapRenderer::diagnosed(
		const std::string &kind,
		const std::string &detail) {
	if (onDiag) {
		onDiag(kind, detail);
	}
}

FactorySelection::FactorySelection(
	std::shared_ptr<WidgetOpsTap> tap,
	int index)
: _tap(std::move(tap))
, _index(index) {
}

FactorySelection::~FactorySelection() {
	_tap->factoryReleased();
}

const std::shared_ptr<WidgetOpsTap> &FactorySelection::tap() const {
	return _tap;
}

int FactorySelection::index() const {
	return _index;
}

StubRenderer::StubRenderer(std::shared_ptr<StubState> state)
: _state(std::move(state)) {
}

StubRenderer::~StubRenderer() {
	if (_state->tap) {
		_state->destroyedSeenByDestructor
			= ReadWidgetOps(*_state->tap).wrappersDestroyed;
	}
	called("renderer.destructor");
}

void StubRenderer::load() {
	called("renderer.load");
}

void StubRenderer::send(const std::string &json) {
	_state->lastSent = &json;
	called("renderer.send " + json);
	if (json == kAckQuery && onMessage) {
		onMessage(kAckResult);
	}
}

void StubRenderer::setTheme(const std::string &theme) {
	called("renderer.setTheme " + theme);
}

void StubRenderer::setWidth(double width) {
	called("renderer.setWidth " + std::to_string(width));
}

void StubRenderer::destroy() {
	called("renderer.destroy");
}

void StubRenderer::emitMessage(const std::string &json) {
	if (onMessage) {
		onMessage(json);
	}
}

void StubRenderer::emitDiag(
		const std::string &kind,
		const std::string &detail) {
	if (onDiag) {
		onDiag(kind, detail);
	}
}

void StubRenderer::called(std::string text) {
	_state->log.push_back({ std::move(text), std::this_thread::get_id() });
}

std::unique_ptr<twidget::Renderer> StubFactory::operator()(
		std::shared_ptr<twidget::SandboxResources> resources,
		const std::string &theme,
		double width) const {
	++state->factoryCalls;
	state->resources = std::move(resources);
	state->theme = theme;
	state->width = width;
	state->factoryThread = std::this_thread::get_id();
	if (state->fail) {
		throw std::runtime_error(kFactoryFailure);
	}
	auto result = std::make_unique<StubRenderer>(state);
	state->built = result.get();
	return result;
}

// Peer 0 belongs to no history, so no real message of any session has such
// an id. The session values only tell the self-check's own ids apart.
constexpr auto kSelfCheckSession = uint64(1);
constexpr auto kOtherSession = uint64(2);

// The stress leg's id, after the four of the legs before it.
constexpr auto kStressIndex = 5;

// The conformance probe check's ids; its fixture cases count from the last.
constexpr auto kProbeReplacementIndex = 11;
constexpr auto kProbeThemeIndex = 12;
constexpr auto kProbeCaseIndex = 21;

[[nodiscard]] GlobalMsgId SelfCheckId(
		int index,
		uint64 session = kSelfCheckSession) {
	return { FullMsgId(PeerId(), MsgId(-index)), session };
}

[[nodiscard]] QString Flag(bool value) {
	return value ? u"1"_q : u"0"_q;
}

[[nodiscard]] QString ExamplesText(const std::vector<std::string> &list) {
	auto result = QStringList();
	for (const auto &entry : list) {
		result.push_back(QString::fromStdString(entry));
	}
	return u"["_q + result.join(u" | "_q) + u"]"_q;
}

[[nodiscard]] QString ReadingText(const WidgetOpsReading &reading) {
	const auto counts = std::vector<std::pair<QString, int>>{
		{ u"selected"_q, reading.factoriesSelected },
		{ u"completed"_q, reading.factoriesCompleted },
		{ u"threw"_q, reading.factoriesThrew },
		{ u"released"_q, reading.factoriesReleased },
		{ u"built"_q, reading.wrappersBuilt },
		{ u"destroyed"_q, reading.wrappersDestroyed },
		{ u"hostMessages"_q, reading.hostMessages },
		{ u"batches"_q, reading.batches },
		{ u"ops"_q, reading.ops },
		{ u"scanGaps"_q, reading.scanGaps },
		{ u"leaks"_q, reading.leaks },
		{ u"notes"_q, reading.notes },
		{ u"violations"_q, reading.violations },
		{ u"limitReports"_q, reading.limitReports },
		{ u"trustedEvents"_q, reading.trustedEvents },
		{ u"trustedClicks"_q, reading.trustedClicks },
		{ u"ackQueries"_q, reading.ackQueries },
		{ u"ackResults"_q, reading.ackResults },
		{ u"fatalFrozen"_q, reading.fatalFrozen },
	};
	auto result = QStringList();
	for (const auto &[name, value] : counts) {
		result.push_back(name + QChar('=') + QString::number(value));
	}
	return result.join(QChar(' '));
}

void AddRow(
		WidgetOpsSelfCheck &result,
		bool passed,
		const QString &name,
		const QString &details) {
	result.rows.push_back({
		.name = u"widget ops self-check: "_q + name,
		.details = details,
		.passed = passed,
	});
}

struct Expected {
	std::string text;
	bool worker = false;
};

[[nodiscard]] std::vector<Expected> ExpectedSequence() {
	const auto sent = [](const char *json) {
		return Expected{ std::string("renderer.send ") + json };
	};
	const auto delivered = [](const char *json, bool worker) {
		return Expected{ std::string("host.message ") + json, worker };
	};
	const auto diag = [](const char *detail, bool worker) {
		return Expected{
			std::string("host.diag ") + kDiagKind + ' ' + detail,
			worker,
		};
	};
	return {
		{ "renderer.load" },
		{ "renderer.setTheme light" },
		{ "renderer.setWidth 512.250000" },
		sent(kControlBatch),
		sent(kUnparsable),
		sent(kNotArray),
		sent(kAckQuery),
		delivered(kAckResult, false),
		{ kReturnedMark },
		sent(kRemovalBatch),
		delivered(kPressEvent, false),
		delivered(kTrustedClick, false),
		delivered(kUntrustedClick, false),
		delivered(kPaintReport, false),
		delivered(kBackdropReport, false),
		diag(kMainDiag, false),
		delivered(kViolation, true),
		diag(kWorkerDiag, true),
		sent(kFrozen),
		{ "renderer.destroy" },
		{ "renderer.destructor" },
	};
}

// Empty when |log| is |expected| entry for entry, else its first difference.
[[nodiscard]] QString SequenceDifference(
		const std::vector<StubCall> &log,
		const std::vector<Expected> &expected,
		std::thread::id mainThread,
		std::thread::id workerThread) {
	const auto text = [](const std::string &value) {
		return QString::fromStdString(value.substr(0, 120));
	};
	const auto threadName = [&](std::thread::id thread) {
		return (thread == mainThread)
			? u"main"_q
			: (thread == workerThread)
			? u"worker"_q
			: u"other"_q;
	};
	const auto count = (log.size() > expected.size())
		? log.size()
		: expected.size();
	for (auto i = size_t(0); i != count; ++i) {
		const auto index = QString::number(int(i));
		if (i >= log.size()) {
			return u"#%1 missing: %2"_q.arg(index, text(expected[i].text));
		} else if (i >= expected.size()) {
			return u"#%1 extra: %2"_q.arg(index, text(log[i].text));
		}
		const auto thread = expected[i].worker ? workerThread : mainThread;
		if (log[i].text != expected[i].text || log[i].thread != thread) {
			return u"#%1 got [%2] on %3, expected [%4] on %5"_q.arg(
				index,
				text(log[i].text),
				threadName(log[i].thread),
				text(expected[i].text),
				threadName(thread));
		}
	}
	return QString();
}

[[nodiscard]] int IndexOf(
		const std::vector<StubCall> &log,
		const std::string &text) {
	for (auto i = 0; i != int(log.size()); ++i) {
		if (log[i].text == text) {
			return i;
		}
	}
	return -1;
}

void CheckUnarmed(WidgetOpsSelfCheck &result) {
	const auto id = SelfCheckId(1);
	const auto state = std::make_shared<StubState>();
	auto factory = TapWidgetRenderer(id, StubFactory{ state });
	const auto untouched = (factory.target<StubFactory>() != nullptr);
	auto renderer = factory(
		std::make_shared<twidget::SandboxResources>(),
		kTheme,
		kWidth);
	const auto stub = renderer && (renderer.get() == state->built);
	AddRow(
		result,
		untouched && stub && (state->factoryCalls == 1),
		u"an unarmed message gets the product factory itself"_q,
		u"untouched=%1 stubItself=%2 factoryCalls=%3"_q.arg(
			Flag(untouched),
			Flag(stub),
			QString::number(state->factoryCalls)));
	renderer = nullptr;
	DisarmWidgetOpsTap(id);
}

void ExerciseWrapper(
		WidgetOpsSelfCheck &result,
		WidgetOpsTap &tap,
		StubState &state,
		twidget::Renderer &wrapper,
		StubRenderer &stub) {
	const auto mainThread = std::this_thread::get_id();
	const auto log = &state.log;
	wrapper.onMessage = [log](const std::string &json) {
		log->push_back({ "host.message " + json, std::this_thread::get_id() });
	};
	wrapper.onDiag = [log](
			const std::string &kind,
			const std::string &detail) {
		log->push_back({
			"host.diag " + kind + ' ' + detail,
			std::this_thread::get_id(),
		});
	};

	wrapper.load();
	wrapper.setTheme(kNextTheme);
	wrapper.setWidth(kNextWidth);
	const auto control = std::string(kControlBatch);
	wrapper.send(control);
	const auto sameObject = (state.lastSent == &control);
	const auto afterControl = ReadWidgetOps(tap);
	wrapper.send(kUnparsable);
	wrapper.send(kNotArray);
	wrapper.send(kAckQuery);
	log->push_back({ kReturnedMark, std::this_thread::get_id() });
	wrapper.send(kRemovalBatch);
	stub.emitMessage(kPressEvent);
	MarkWidgetOpsEvents(tap);
	stub.emitMessage(kTrustedClick);
	stub.emitMessage(kUntrustedClick);
	const auto beforeReports = ReadWidgetOps(tap);
	stub.emitMessage(kPaintReport);
	stub.emitMessage(kBackdropReport);
	stub.emitDiag(kDiagKind, kMainDiag);
	auto workerThread = std::thread::id();
	auto worker = std::thread([&stub, &workerThread] {
		workerThread = std::this_thread::get_id();
		stub.emitMessage(kViolation);
		stub.emitDiag(kDiagKind, kWorkerDiag);
	});
	worker.join();
	wrapper.send(kFrozen);
	wrapper.destroy();
	const auto reading = ReadWidgetOps(tap);

	auto expected = ExpectedSequence();
	expected.pop_back(); // the stub's destructor has not run yet
	const auto difference = SequenceDifference(
		*log,
		expected,
		mainThread,
		workerThread);
	AddRow(
		result,
		difference.isEmpty() && sameObject,
		u"every call and callback is forwarded once, in order, with the "
		"exact bytes, on its caller's thread"_q,
		u"entries=%1 sameStringObject=%2 difference=[%3]"_q.arg(
			QString::number(int(log->size())),
			Flag(sameObject),
			difference));

	const auto query = IndexOf(*log, std::string("renderer.send ") + kAckQuery);
	const auto reply = IndexOf(*log, std::string("host.message ") + kAckResult);
	const auto returned = IndexOf(*log, kReturnedMark);
	AddRow(
		result,
		(query >= 0) && (reply == query + 1) && (returned == reply + 1),
		u"a reply sent from inside send() arrives before send() returns"_q,
		u"send=%1 reply=%2 returned=%3"_q.arg(
			QString::number(query),
			QString::number(reply),
			QString::number(returned)));

	const auto violation = IndexOf(
		*log,
		std::string("host.message ") + kViolation);
	const auto workerDiag = IndexOf(
		*log,
		std::string("host.diag ") + kDiagKind + ' ' + kWorkerDiag);
	const auto onWorker = (violation >= 0)
		&& (workerDiag >= 0)
		&& (workerThread != mainThread)
		&& ((*log)[violation].thread == workerThread)
		&& ((*log)[workerDiag].thread == workerThread);
	AddRow(
		result,
		onWorker,
		u"callbacks from a joined worker thread arrive on that thread"_q,
		u"message=%1 diag=%2 onWorker=%3"_q.arg(
			QString::number(violation),
			QString::number(workerDiag),
			Flag(onWorker)));

	const auto leaks = std::vector<std::string>{ kLeakSrc, kLeakStyle };
	const auto notes = std::vector<std::string>{ kNoteData };
	AddRow(
		result,
		(afterControl.batches == 1)
			&& (afterControl.ops == 4)
			&& (afterControl.leaks == 2)
			&& (afterControl.notes == 1)
			&& (afterControl.leakExamples == leaks)
			&& (afterControl.noteExamples == notes),
		u"the control batch gives exactly 2 leaks and 1 note, its data: "
		"and #fragment CSS negatives none"_q,
		u"%1 leakExamples=%2 noteExamples=%3"_q.arg(
			ReadingText(afterControl),
			ExamplesText(afterControl.leakExamples),
			ExamplesText(afterControl.noteExamples)));
	AddRow(
		result,
		(reading.hostMessages == 6)
			&& (reading.batches == 2)
			&& (reading.ops == 5)
			&& (reading.leaks == 2)
			&& (reading.notes == 1)
			&& (reading.leakExamples == leaks)
			&& (reading.noteExamples == notes),
		u"the removal batch is scanned too and the earlier findings "
		"stay"_q,
		ReadingText(reading));
	AddRow(
		result,
		(reading.scanGaps == 2) && (reading.gapExamples.size() == 2),
		u"an unparsable message and an ops message without an array are "
		"scan gaps, forwarded unchanged"_q,
		u"scanGaps=%1 gapExamples=%2"_q.arg(
			QString::number(reading.scanGaps),
			ExamplesText(reading.gapExamples)));
	AddRow(
		result,
		(reading.ackQueries == 1)
			&& (reading.ackResults == 1)
			&& (reading.fatalFrozen == 1),
		u"the host's post-load ack, the renderer's answer and a fatal "
		"frozen are read"_q,
		ReadingText(reading));
	AddRow(
		result,
		(reading.violations == 1)
			&& (reading.violationDetails
				== std::vector<std::string>{ kViolationDetail })
			&& (reading.leaks == 2),
		u"a renderer violation is recorded apart from the leaks"_q,
		u"violations=%1 details=%2 leaks=%3"_q.arg(
			QString::number(reading.violations),
			ExamplesText(reading.violationDetails),
			QString::number(reading.leaks)));
	AddRow(
		result,
		(reading.trustedEvents == 1)
			&& (reading.trustedClicks == 1)
			&& (reading.trustedEventTypes
				== std::vector<std::string>{ "click" }),
		u"trusted events count from the mark, untrusted ones never"_q,
		u"trustedEvents=%1 trustedClicks=%2 types=%3"_q.arg(
			QString::number(reading.trustedEvents),
			QString::number(reading.trustedClicks),
			ExamplesText(reading.trustedEventTypes)));

	const auto paintReport = IndexOf(
		*log,
		std::string("host.message ") + kPaintReport);
	const auto backdropReport = IndexOf(
		*log,
		std::string("host.message ") + kBackdropReport);
	const auto examples = std::vector<std::string>{
		kPaintExample,
		kBackdropExample,
	};
	AddRow(
		result,
		(beforeReports.limitReports == 0)
			&& beforeReports.limitExamples.empty()
			&& (reading.limitReports == 2)
			&& (reading.limitExamples == examples)
			&& (reading.violations == 1)
			&& (paintReport >= 0)
			&& (backdropReport == paintReport + 1),
		u"two renderer limit reports count twice from none, keep their "
		"code and count, and reach the host in order with their bytes"_q,
		u"before=%1 limitReports=%2 examples=%3 violations=%4 "
		"delivered=%5,%6"_q.arg(
			QString::number(beforeReports.limitReports),
			QString::number(reading.limitReports),
			ExamplesText(reading.limitExamples),
			QString::number(reading.violations),
			QString::number(paintReport),
			QString::number(backdropReport)));
}

[[nodiscard]] WidgetOpsReading CheckArmed(WidgetOpsSelfCheck &result) {
	const auto id = SelfCheckId(2);
	const auto mainThread = std::this_thread::get_id();
	const auto tap = ArmWidgetOpsTap(id);
	const auto again = ArmWidgetOpsTap(id);
	AddRow(
		result,
		tap && (again == tap),
		u"arming an armed message returns its tap"_q,
		u"tap=%1 same=%2"_q.arg(Flag(tap != nullptr), Flag(again == tap)));

	const auto state = std::make_shared<StubState>();
	state->tap = tap;
	auto factory = TapWidgetRenderer(id, StubFactory{ state });
	const auto wrapping = (factory.target<StubFactory>() == nullptr);
	const auto selected = ReadWidgetOps(*tap);
	AddRow(
		result,
		wrapping
			&& (selected.factoriesSelected == 1)
			&& (selected.factoriesCompleted == 0)
			&& (selected.factoriesReleased == 0)
			&& (selected.wrappersBuilt == 0),
		u"an armed message gets a wrapping factory, counted as "
		"selected"_q,
		u"wrapping=%1 %2"_q.arg(Flag(wrapping), ReadingText(selected)));

	const auto resources = std::make_shared<twidget::SandboxResources>();
	auto wrapper = factory(resources, kTheme, kWidth);
	const auto built = ReadWidgetOps(*tap);
	AddRow(
		result,
		(state->factoryCalls == 1)
			&& (state->resources == resources)
			&& (state->theme == kTheme)
			&& (state->width == kWidth)
			&& (state->factoryThread == mainThread)
			&& (built.factoriesCompleted == 1)
			&& (built.factoriesThrew == 0)
			&& (built.wrappersBuilt == 1)
			&& (built.wrappersDestroyed == 0),
		u"the wrapping factory calls the product factory once, with the "
		"same arguments, on its caller's thread"_q,
		u"factoryCalls=%1 sameResources=%2 theme=%3 width=%4 "
		"callerThread=%5 %6"_q.arg(
			QString::number(state->factoryCalls),
			Flag(state->resources == resources),
			QString::fromStdString(state->theme),
			QString::number(state->width),
			Flag(state->factoryThread == mainThread),
			ReadingText(built)));

	const auto stub = state->built;
	const auto wrapped = wrapper
		&& stub
		&& (wrapper.get() != stub)
		&& (dynamic_cast<TapRenderer*>(wrapper.get()) != nullptr);
	const auto callbacks = wrapped
		&& !wrapper->onMessage
		&& !wrapper->onDiag
		&& stub->onMessage
		&& stub->onDiag;
	AddRow(
		result,
		callbacks,
		u"the wrapper installs the product renderer's callbacks and leaves "
		"its own for the host to assign after the factory returned"_q,
		u"wrapped=%1 callbacks=%2"_q.arg(Flag(wrapped), Flag(callbacks)));
	if (wrapped) {
		ExerciseWrapper(result, *tap, *state, *wrapper, *stub);
	}

	wrapper = nullptr;
	const auto destroyed = ReadWidgetOps(*tap);
	const auto last = state->log.empty()
		? std::string()
		: state->log.back().text;
	AddRow(
		result,
		wrapped
			&& (state->destroyedSeenByDestructor == 0)
			&& (destroyed.wrappersDestroyed == 1)
			&& (last == "renderer.destructor"),
		u"destroying the wrapper destroys the product renderer before it "
		"is counted"_q,
		u"destroyedSeenByProductDestructor=%1 destroyed=%2 last=[%3]"_q.arg(
			QString::number(state->destroyedSeenByDestructor),
			QString::number(destroyed.wrappersDestroyed),
			QString::fromStdString(last)));

	factory = nullptr;
	const auto released = ReadWidgetOps(*tap);
	AddRow(
		result,
		(released.factoriesSelected == 1)
			&& (released.factoriesCompleted == 1)
			&& (released.factoriesThrew == 0)
			&& (released.factoriesReleased == 1)
			&& (released.wrappersBuilt == 1)
			&& (released.wrappersDestroyed == 1),
		u"every selected factory completed and was released, every "
		"wrapper destroyed"_q,
		ReadingText(released));

	DisarmWidgetOpsTap(id);
	const auto later = TapWidgetRenderer(
		id,
		StubFactory{ std::make_shared<StubState>() });
	const auto untouched = (later.target<StubFactory>() != nullptr);
	const auto selectedAfter = ReadWidgetOps(*tap).factoriesSelected;
	AddRow(
		result,
		untouched && (selectedAfter == 1),
		u"a disarmed message gets the product factory itself again"_q,
		u"untouched=%1 selected=%2"_q.arg(
			Flag(untouched),
			QString::number(selectedAfter)));
	return released;
}

void CheckThrowing(WidgetOpsSelfCheck &result) {
	const auto id = SelfCheckId(3);
	const auto tap = ArmWidgetOpsTap(id);
	const auto state = std::make_shared<StubState>();
	state->fail = true;
	auto factory = TapWidgetRenderer(id, StubFactory{ state });
	auto returned = false;
	auto rethrown = std::string();
	try {
		factory(std::make_shared<twidget::SandboxResources>(), kTheme, kWidth);
		returned = true;
	} catch (const std::runtime_error &error) {
		rethrown = error.what();
	} catch (...) {
		rethrown = "an exception of another type";
	}
	const auto threw = ReadWidgetOps(*tap);
	factory = nullptr;
	const auto released = ReadWidgetOps(*tap);
	DisarmWidgetOpsTap(id);
	AddRow(
		result,
		!returned
			&& (rethrown == kFactoryFailure)
			&& (state->factoryCalls == 1)
			&& (threw.factoriesSelected == 1)
			&& (threw.factoriesCompleted == 1)
			&& (threw.factoriesThrew == 1)
			&& (threw.wrappersBuilt == 0)
			&& (released.factoriesReleased == 1),
		u"a throwing product factory is counted and its exception reaches "
		"the caller"_q,
		u"returned=%1 rethrown=[%2] factoryCalls=%3 %4"_q.arg(
			Flag(returned),
			QString::fromStdString(rethrown),
			QString::number(state->factoryCalls),
			ReadingText(released)));
}

// One FullMsgId in two sessions is two messages: arming one wraps it alone,
// arming both gives two taps, and disarming one leaves the other armed.
void CheckSessions(WidgetOpsSelfCheck &result) {
	const auto first = SelfCheckId(4, kSelfCheckSession);
	const auto second = SelfCheckId(4, kOtherSession);
	const auto resources = std::make_shared<twidget::SandboxResources>();
	const auto stubFactory = [] {
		return StubFactory{ std::make_shared<StubState>() };
	};

	const auto firstTap = ArmWidgetOpsTap(first);
	const auto otherState = std::make_shared<StubState>();
	auto otherFactory = TapWidgetRenderer(second, StubFactory{ otherState });
	const auto otherUntouched
		= (otherFactory.target<StubFactory>() != nullptr);
	auto otherRenderer = otherFactory(resources, kTheme, kWidth);
	const auto otherStub = otherRenderer
		&& (otherRenderer.get() == otherState->built);
	otherRenderer = nullptr;
	otherFactory = nullptr;
	const auto onlyFirst = ReadWidgetOps(*firstTap);
	AddRow(
		result,
		otherUntouched
			&& otherStub
			&& (onlyFirst.factoriesSelected == 0)
			&& (onlyFirst.wrappersBuilt == 0),
		u"another session's message with the same FullMsgId gets the "
		"product factory itself while only this session's is armed"_q,
		u"untouched=%1 stubItself=%2 armed{%3}"_q.arg(
			Flag(otherUntouched),
			Flag(otherStub),
			ReadingText(onlyFirst)));

	const auto secondTap = ArmWidgetOpsTap(second);
	auto firstFactory = TapWidgetRenderer(first, stubFactory());
	auto secondFactory = TapWidgetRenderer(second, stubFactory());
	auto firstWrapper = firstFactory(resources, kTheme, kWidth);
	auto secondWrapper = secondFactory(resources, kTheme, kWidth);
	if (firstWrapper) {
		firstWrapper->send(kControlBatch);
	}
	if (secondWrapper) {
		secondWrapper->send(kRemovalBatch);
	}
	const auto firstBoth = ReadWidgetOps(*firstTap);
	const auto secondBoth = ReadWidgetOps(*secondTap);
	const auto separate = secondTap && (secondTap != firstTap);
	AddRow(
		result,
		separate
			&& firstWrapper
			&& secondWrapper
			&& (firstBoth.factoriesSelected == 1)
			&& (firstBoth.wrappersBuilt == 1)
			&& (firstBoth.batches == 1)
			&& (firstBoth.ops == 4)
			&& (firstBoth.leaks == 2)
			&& (secondBoth.factoriesSelected == 1)
			&& (secondBoth.wrappersBuilt == 1)
			&& (secondBoth.batches == 1)
			&& (secondBoth.ops == 1)
			&& (secondBoth.leaks == 0)
			&& (secondBoth.notes == 0),
		u"with both sessions' messages armed, each has its own tap and "
		"its own reading"_q,
		u"separateTaps=%1 first{%2} second{%3}"_q.arg(
			Flag(separate),
			ReadingText(firstBoth),
			ReadingText(secondBoth)));

	DisarmWidgetOpsTap(first);
	auto firstLater = TapWidgetRenderer(first, stubFactory());
	auto secondLater = TapWidgetRenderer(second, stubFactory());
	const auto firstUntouched
		= (firstLater.target<StubFactory>() != nullptr);
	const auto secondWrapping
		= (secondLater.target<StubFactory>() == nullptr);
	if (secondWrapper) {
		secondWrapper->send(kRemovalBatch);
	}
	const auto firstAfter = ReadWidgetOps(*firstTap);
	const auto secondAfter = ReadWidgetOps(*secondTap);
	AddRow(
		result,
		firstUntouched
			&& secondWrapping
			&& (firstAfter.factoriesSelected == 1)
			&& (firstAfter.hostMessages == 1)
			&& (secondAfter.factoriesSelected == 2)
			&& (secondAfter.batches == 2),
		u"disarming one session's message leaves the other's armed and "
		"recording"_q,
		u"firstUntouched=%1 secondWrapping=%2 first{%3} second{%4}"_q.arg(
			Flag(firstUntouched),
			Flag(secondWrapping),
			ReadingText(firstAfter),
			ReadingText(secondAfter)));

	firstLater = nullptr;
	secondLater = nullptr;
	firstWrapper = nullptr;
	secondWrapper = nullptr;
	firstFactory = nullptr;
	secondFactory = nullptr;
	DisarmWidgetOpsTap(second);
}

// One case of probe-fixture.json, its `expect` kept as the fixture has it.
struct ProbeCase {
	QString name;
	std::vector<std::string> messages;
	QJsonValue htmlId;
	QJsonValue updates;
	QJsonValue finalValue;
	QJsonValue awaitingTargets;
};

struct ProbeFixture {
	std::vector<ProbeCase> cases;
	QString error;
};

[[nodiscard]] ProbeFixture ParseProbeFixture(const QByteArray &bytes) {
	auto result = ProbeFixture();
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(bytes, &error);
	if (error.error != QJsonParseError::NoError) {
		result.error = u"unparsable: "_q + error.errorString();
		return result;
	}
	const auto cases = document.object().value(u"cases"_q).toArray();
	for (auto i = 0; i != int(cases.size()); ++i) {
		const auto object = cases[i].toObject();
		const auto expect = object.value(u"expect"_q).toObject();
		auto parsed = ProbeCase{
			.name = object.value(u"name"_q).toString(),
			.htmlId = expect.value(u"htmlId"_q),
			.updates = expect.value(u"updates"_q),
			.finalValue = expect.value(u"final"_q),
			.awaitingTargets = expect.value(u"awaitingTargets"_q),
		};
		const auto messages = object.value(u"messages"_q).toArray();
		for (const auto &message : messages) {
			if (message.isString()) {
				parsed.messages.push_back(message.toString().toStdString());
			}
		}
		if (parsed.messages.empty()
			|| (int(parsed.messages.size()) != int(messages.size()))
			|| !parsed.htmlId.isDouble()
			|| !parsed.updates.isDouble()
			|| !parsed.finalValue.isObject()) {
			result.error = u"case %1 lacks string messages, a numeric "
				"htmlId and updates, or an object final"_q.arg(i + 1);
			return result;
		}
		result.cases.push_back(std::move(parsed));
	}
	if (result.cases.empty()) {
		result.error = u"no cases"_q;
	}
	return result;
}

// Undefined unless |value| is a JSON object, as a published result is.
[[nodiscard]] QJsonValue ParsedResult(
		const std::optional<std::string> &value) {
	if (!value) {
		return QJsonValue(QJsonValue::Undefined);
	}
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(
		QByteArray::fromStdString(*value),
		&error);
	return (error.error == QJsonParseError::NoError && document.isObject())
		? QJsonValue(document.object())
		: QJsonValue(QJsonValue::Undefined);
}

[[nodiscard]] QString JsonText(const QJsonValue &value) {
	const auto compact = [](const QJsonDocument &document) {
		return QString::fromUtf8(document.toJson(QJsonDocument::Compact));
	};
	const auto text = value.isObject()
		? compact(QJsonDocument(value.toObject()))
		: value.isArray()
		? compact(QJsonDocument(value.toArray()))
		: value.isDouble()
		? QString::number(value.toDouble())
		: value.isString()
		? value.toString()
		: value.isUndefined()
		? u"none"_q
		: u"null"_q;
	return text.left(kProbeTextChars);
}

[[nodiscard]] QString LatestText(const WidgetOpsLatest &latest) {
	const auto conformance = latest.conformance
		? QString::fromStdString(*latest.conformance).left(kProbeTextChars)
		: u"none"_q;
	return QStringList{
		u"generation="_q + QString::number(latest.generation),
		u"adopted="_q + Flag(latest.adopted),
		u"htmlId="_q + QString::number(latest.htmlId),
		u"updates="_q + QString::number(latest.updates),
		u"serial="_q + QString::number(latest.serial),
		u"scheme=["_q + QString::fromStdString(latest.scheme) + u"]"_q,
		u"schemeChanges="_q + QString::number(latest.schemeChanges),
		u"staleUpdates="_q + QString::number(latest.staleUpdates),
		(u"themeMessageBytes="_q
			+ QString::number(int(latest.themeMessage.size()))),
		u"batches="_q + QString::number(latest.batches),
		u"ops="_q + QString::number(latest.ops),
		u"maxBatchOps="_q + QString::number(latest.maxBatchOps),
		u"stressValues="_q + QString::number(int(latest.stress.size())),
		u"stressDropped="_q + QString::number(latest.stressDropped),
		u"conformance=["_q + conformance + u"]"_q,
	}.join(QChar(' '));
}

[[nodiscard]] int CountOf(
		const std::vector<StubCall> &log,
		const std::string &text) {
	const auto matches = [&](const StubCall &call) {
		return (call.text == text);
	};
	return int(std::count_if(log.begin(), log.end(), matches));
}

void SendTo(
		const std::unique_ptr<twidget::Renderer> &wrapper,
		const std::string &json) {
	if (wrapper) {
		wrapper->send(json);
	}
}

void AddProbeRow(
		std::vector<WidgetOpsSelfCheckRow> &rows,
		bool passed,
		const QString &name,
		const QString &details) {
	rows.push_back({
		.name = u"widget conformance probe: "_q + name,
		.details = details,
		.passed = passed,
	});
}

// Reads ReadWidgetOpsLatest() after every message, as a runner polls it.
void CheckProbeCase(
		std::vector<WidgetOpsSelfCheckRow> &rows,
		const ProbeCase &probe,
		int index) {
	const auto id = SelfCheckId(kProbeCaseIndex + index);
	const auto tap = ArmWidgetOpsTap(id);
	const auto state = std::make_shared<StubState>();
	auto factory = TapWidgetRenderer(id, StubFactory{ state });
	auto wrapper = factory(
		std::make_shared<twidget::SandboxResources>(),
		kTheme,
		kWidth);
	auto awaitingTargets = QJsonValue(QJsonValue::Undefined);
	auto serial = ReadWidgetOpsLatest(*tap).serial;
	for (const auto &message : probe.messages) {
		SendTo(wrapper, message);
		const auto latest = ReadWidgetOpsLatest(*tap);
		if (latest.serial == serial) {
			continue;
		}
		serial = latest.serial;
		const auto value = ParsedResult(latest.conformance).toObject();
		if (value.value(u"status"_q).toString() == u"awaiting"_q) {
			awaitingTargets = value.value(u"targets"_q);
		}
	}
	const auto latest = ReadWidgetOpsLatest(*tap);
	auto expected = std::vector<Expected>();
	for (const auto &message : probe.messages) {
		expected.push_back({ "renderer.send " + message });
	}
	const auto difference = SequenceDifference(
		state->log,
		expected,
		std::this_thread::get_id(),
		std::thread::id());
	wrapper = nullptr;
	factory = nullptr;
	DisarmWidgetOpsTap(id);

	const auto label = u"case %1 (%2): "_q.arg(
		QString::number(index + 1),
		probe.name);
	const auto finalValue = ParsedResult(latest.conformance);
	AddProbeRow(
		rows,
		latest.adopted
			&& (double(latest.htmlId) == probe.htmlId.toDouble())
			&& (double(latest.updates) == probe.updates.toDouble()),
		label + u"the html id and the count of values"_q,
		u"htmlId=%1 expected=%2 updates=%3 expected=%4 {%5}"_q.arg(
			QString::number(latest.htmlId),
			JsonText(probe.htmlId),
			QString::number(latest.updates),
			JsonText(probe.updates),
			LatestText(latest)));
	AddProbeRow(
		rows,
		finalValue.isObject() && (finalValue == probe.finalValue),
		label + u"the last value is the expected final one"_q,
		u"final=%1 expected=%2"_q.arg(
			JsonText(finalValue),
			JsonText(probe.finalValue)));
	if (!probe.awaitingTargets.isUndefined()) {
		AddProbeRow(
			rows,
			(awaitingTargets == probe.awaitingTargets),
			label + u"the targets published while awaiting"_q,
			u"targets=%1 expected=%2"_q.arg(
				JsonText(awaitingTargets),
				JsonText(probe.awaitingTargets)));
	}
	AddProbeRow(
		rows,
		difference.isEmpty(),
		label + u"every message forwarded once with its exact bytes"_q,
		u"messages=%1 difference=[%2]"_q.arg(
			QString::number(int(probe.messages.size())),
			difference));
}

// Both hosts adopt <html> under one id, as fresh sandboxes of one widget do,
// so a retiring host's late value would land on the newer one if merged.
void CheckProbeReplacement(std::vector<WidgetOpsSelfCheckRow> &rows) {
	const auto id = SelfCheckId(kProbeReplacementIndex);
	const auto tap = ArmWidgetOpsTap(id);
	const auto resources = std::make_shared<twidget::SandboxResources>();
	const auto oldState = std::make_shared<StubState>();
	auto oldFactory = TapWidgetRenderer(id, StubFactory{ oldState });
	auto oldWrapper = oldFactory(resources, kNextTheme, kWidth);
	SendTo(oldWrapper, kOldAdopt);
	SendTo(oldWrapper, kOldDone);
	const auto before = ReadWidgetOpsLatest(*tap);

	const auto newState = std::make_shared<StubState>();
	auto newFactory = TapWidgetRenderer(id, StubFactory{ newState });
	const auto selected = ReadWidgetOpsLatest(*tap);
	SendTo(oldWrapper, kOldAdopt);
	auto newWrapper = newFactory(resources, kTheme, kWidth);
	SendTo(newWrapper, kNewAdopt);
	SendTo(newWrapper, kNewRunning);
	const auto own = ReadWidgetOpsLatest(*tap);

	SendTo(oldWrapper, kOldDone);
	SendTo(oldWrapper, kControlBatch);
	if (oldWrapper) {
		oldWrapper->setTheme(kNextTheme);
	}
	SendTo(oldWrapper, kThemeMessage);
	const auto late = ReadWidgetOpsLatest(*tap);
	const auto lateReading = ReadWidgetOps(*tap);
	const auto sent = std::string("renderer.send ");
	const auto themed = std::string("renderer.setTheme ") + kNextTheme;
	const auto oldForwarded = (CountOf(oldState->log, sent + kOldAdopt) == 2)
		&& (CountOf(oldState->log, sent + kOldDone) == 2)
		&& (CountOf(oldState->log, sent + kThemeMessage) == 1)
		&& (CountOf(oldState->log, themed) == 1);

	SendTo(newWrapper, kReadopt);
	const auto readopted = ReadWidgetOpsLatest(*tap);

	newWrapper = nullptr;
	oldWrapper = nullptr;
	newFactory = nullptr;
	oldFactory = nullptr;
	const auto finished = ReadWidgetOps(*tap);
	DisarmWidgetOpsTap(id);

	AddProbeRow(
		rows,
		(before.generation == 1)
			&& (before.conformance == std::string(kOldDoneValue))
			&& (before.htmlId == kHtmlId)
			&& (before.updates == 2)
			&& (selected.generation == 2)
			&& !selected.conformance
			&& !selected.adopted
			&& (selected.updates == 0)
			&& selected.scheme.empty()
			&& (selected.serial == before.serial + 1),
		u"selecting a newer host clears the older host's done result "
		"before the newer one is built"_q,
		u"before{%1} selected{%2}"_q.arg(
			LatestText(before),
			LatestText(selected)));
	AddProbeRow(
		rows,
		(own.generation == 2)
			&& own.adopted
			&& (own.htmlId == kHtmlId)
			&& (own.conformance == std::string(kNewRunningValue))
			&& (own.updates == 1)
			&& (own.serial == selected.serial + 1)
			&& (own.scheme == kTheme)
			&& (own.staleUpdates == 1),
		u"the newer host reads its own adoption and value from one, "
		"whatever the older host adopted after the selection"_q,
		LatestText(own));
	AddProbeRow(
		rows,
		(late.generation == 2)
			&& (late.conformance == std::string(kNewRunningValue))
			&& (late.htmlId == kHtmlId)
			&& (late.updates == 1)
			&& (late.serial == own.serial)
			&& (late.scheme == kTheme)
			&& (late.schemeChanges == 0)
			&& late.themeMessage.empty()
			&& (late.staleUpdates == 4)
			&& (lateReading.batches == 7)
			&& (lateReading.leaks == 2)
			&& oldForwarded,
		u"the older host's late value, theme message and setTheme() are "
		"forwarded and its batches scanned, but the newer host's reading "
		"keeps its own"_q,
		u"late{%1} oldForwarded=%2 %3"_q.arg(
			LatestText(late),
			Flag(oldForwarded),
			ReadingText(lateReading)));
	AddProbeRow(
		rows,
		readopted.adopted
			&& (readopted.htmlId == kReadoptHtmlId)
			&& !readopted.conformance
			&& (readopted.updates == 1)
			&& (readopted.serial == late.serial + 1),
		u"a later adoption without a value clears it, and the earlier "
		"html id no longer matches"_q,
		LatestText(readopted));
	AddProbeRow(
		rows,
		(finished.factoriesSelected == 2)
			&& (finished.factoriesReleased == 2)
			&& (finished.wrappersBuilt == 2)
			&& (finished.wrappersDestroyed == 2)
			&& (finished.batches == 8)
			&& (finished.leaks == 2)
			&& (finished.notes == 1)
			&& (finished.scanGaps == 0),
		u"the final reading scanned both hosts' batches, and both "
		"finished"_q,
		ReadingText(finished));
}

void CheckProbeTheme(std::vector<WidgetOpsSelfCheckRow> &rows) {
	const auto id = SelfCheckId(kProbeThemeIndex);
	const auto tap = ArmWidgetOpsTap(id);
	const auto state = std::make_shared<StubState>();
	auto factory = TapWidgetRenderer(id, StubFactory{ state });
	const auto selected = ReadWidgetOpsLatest(*tap);
	auto wrapper = factory(
		std::make_shared<twidget::SandboxResources>(),
		kTheme,
		kWidth);
	const auto built = ReadWidgetOpsLatest(*tap);
	auto switched = WidgetOpsLatest();
	auto repeated = WidgetOpsLatest();
	if (wrapper) {
		wrapper->setTheme(kNextTheme);
		switched = ReadWidgetOpsLatest(*tap);
		wrapper->setTheme(kNextTheme);
		repeated = ReadWidgetOpsLatest(*tap);
	}
	SendTo(wrapper, kThemeMessage);
	const auto themed = ReadWidgetOpsLatest(*tap);
	const auto reading = ReadWidgetOps(*tap);
	const auto themeCalls = CountOf(
		state->log,
		std::string("renderer.setTheme ") + kNextTheme);
	const auto themeSends = CountOf(
		state->log,
		std::string("renderer.send ") + kThemeMessage);
	wrapper = nullptr;
	factory = nullptr;
	DisarmWidgetOpsTap(id);

	AddProbeRow(
		rows,
		(selected.generation == 1)
			&& selected.scheme.empty()
			&& (built.scheme == kTheme)
			&& (built.schemeChanges == 0)
			&& (state->factoryCalls == 1)
			&& (state->theme == kTheme),
		u"a new host's scheme is the theme its factory was given, passed "
		"on once"_q,
		u"selected{%1} built{%2} factoryCalls=%3 factoryTheme=%4"_q.arg(
			LatestText(selected),
			LatestText(built),
			QString::number(state->factoryCalls),
			QString::fromStdString(state->theme)));
	AddProbeRow(
		rows,
		(switched.scheme == kNextTheme)
			&& (switched.schemeChanges == 1)
			&& (repeated.scheme == kNextTheme)
			&& (repeated.schemeChanges == 1)
			&& (themeCalls == 2),
		u"each setTheme() is forwarded once, and only a new scheme counts "
		"as a change"_q,
		u"switched{%1} repeated{%2} forwarded=%3"_q.arg(
			LatestText(switched),
			LatestText(repeated),
			QString::number(themeCalls)));
	AddProbeRow(
		rows,
		(themed.themeMessage == kThemeMessage)
			&& (themeSends == 1)
			&& (reading.hostMessages == 1)
			&& (reading.batches == 0)
			&& (reading.scanGaps == 0),
		u"the host's theme message is kept byte for byte and forwarded "
		"once, not as a batch"_q,
		u"kept=%1 forwarded=%2 %3"_q.arg(
			Flag(themed.themeMessage == kThemeMessage),
			QString::number(themeSends),
			ReadingText(reading)));
}

[[nodiscard]] std::string StressBulkBatch(int index) {
	return R"json({"type":"ops","ops":[{"op":"attr","id":2,)json"
		R"json("name":"data-stress","value":"bulk/)json"
		+ std::to_string(index)
		+ R"json("}]})json";
}

[[nodiscard]] std::vector<std::string> StressValues(
		const WidgetOpsLatest &latest) {
	auto result = std::vector<std::string>();
	result.reserve(latest.stress.size());
	for (const auto &entry : latest.stress) {
		result.push_back(entry.value);
	}
	return result;
}

// Every receipt within [from, till], none before the one ahead of it.
[[nodiscard]] bool ReceiptsInOrder(
		const std::vector<WidgetStressValue> &values,
		crl::profile_time from,
		crl::profile_time till) {
	auto last = from;
	for (const auto &value : values) {
		if (value.receivedAt < last) {
			return false;
		}
		last = value.receivedAt;
	}
	return (last <= till);
}

// The first values and the last, and the receipts of the first, in
// microseconds after |from|.
[[nodiscard]] QString StressText(
		const WidgetOpsLatest &latest,
		crl::profile_time from) {
	const auto count = int(latest.stress.size());
	const auto value = [&](int index) {
		return QString::fromStdString(
			latest.stress[index].value).left(kProbeTextChars);
	};
	auto values = QStringList();
	auto receipts = QStringList();
	for (auto i = 0; i != std::min(count, kStressShownValues); ++i) {
		values.push_back(value(i));
		receipts.push_back(
			QString::number(latest.stress[i].receivedAt - from));
	}
	if (count > kStressShownValues) {
		values.push_back(u"... "_q + value(count - 1));
	}
	return u"{%1} values=[%2] receipts=[%3]"_q.arg(
		LatestText(latest),
		values.join(u" | "_q),
		receipts.join(QChar(',')));
}

// An older host adopts <html> with a value, then a newer host is selected:
// its batches alone, from its own adoption on, give the newest counts and
// values, as a stress runner reads them for one level.
void CheckStress(WidgetOpsSelfCheck &result) {
	const auto id = SelfCheckId(kStressIndex);
	const auto tap = ArmWidgetOpsTap(id);
	const auto resources = std::make_shared<twidget::SandboxResources>();
	const auto oldState = std::make_shared<StubState>();
	auto oldFactory = TapWidgetRenderer(id, StubFactory{ oldState });
	auto oldWrapper = oldFactory(resources, kTheme, kWidth);
	SendTo(oldWrapper, kStressOldAdopt);
	const auto old = ReadWidgetOpsLatest(*tap);

	const auto newState = std::make_shared<StubState>();
	auto newFactory = TapWidgetRenderer(id, StubFactory{ newState });
	const auto selected = ReadWidgetOpsLatest(*tap);
	auto newWrapper = newFactory(resources, kTheme, kWidth);
	const auto from = crl::profile();
	SendTo(newWrapper, kStressAdopt);
	SendTo(newWrapper, kStressThree);
	SendTo(newWrapper, kStressFive);
	const auto till = crl::profile();
	const auto own = ReadWidgetOpsLatest(*tap);
	const auto ownReading = ReadWidgetOps(*tap);

	SendTo(oldWrapper, kStressLate);
	const auto late = ReadWidgetOpsLatest(*tap);
	const auto lateReading = ReadWidgetOps(*tap);
	const auto lateForwarded = CountOf(
		oldState->log,
		std::string("renderer.send ") + kStressLate);

	SendTo(newWrapper, kStressPlain);
	const auto plain = ReadWidgetOpsLatest(*tap);

	const auto bulk = kWidgetStressValuesLimit - 2;
	for (auto i = 0; i != bulk; ++i) {
		SendTo(newWrapper, StressBulkBatch(i));
	}
	const auto filledTill = crl::profile();
	const auto filled = ReadWidgetOpsLatest(*tap);
	const auto lastKept = "bulk/" + std::to_string(bulk - 2);

	SendTo(newWrapper, kStressReadopt);
	const auto readopted = ReadWidgetOpsLatest(*tap);

	newWrapper = nullptr;
	oldWrapper = nullptr;
	newFactory = nullptr;
	oldFactory = nullptr;
	DisarmWidgetOpsTap(id);

	const auto ownValues = std::vector<std::string>{
		kStressSetupValue,
		kStressStartValue,
		kStressHalfValue,
	};
	AddRow(
		result,
		(old.generation == 1)
			&& (old.batches == 1)
			&& (old.stress.size() == 1)
			&& (selected.generation == 2)
			&& !selected.adopted
			&& (selected.batches == 0)
			&& (selected.ops == 0)
			&& (selected.maxBatchOps == 0)
			&& selected.stress.empty()
			&& (own.generation == 2)
			&& own.adopted
			&& (own.htmlId == kStressHtmlId)
			&& (own.batches == 3)
			&& (own.ops == 9)
			&& (own.maxBatchOps == 5)
			&& (StressValues(own) == ownValues)
			&& ReceiptsInOrder(own.stress, from, till)
			&& (own.stressDropped == 0)
			&& !own.conformance
			&& (own.updates == 0)
			&& (own.serial == selected.serial),
		u"stress: a newer host counts its own 3 batches, 9 ops and a "
		"largest of 5 and keeps its three data-stress values in order, "
		"receipts non-decreasing, its conformance reading untouched"_q,
		u"old{%1} selected{%2} own%3 till=%4"_q.arg(
			LatestText(old),
			LatestText(selected),
			StressText(own, from),
			QString::number(till - from)));
	AddRow(
		result,
		(late.batches == own.batches)
			&& (late.ops == own.ops)
			&& (late.maxBatchOps == own.maxBatchOps)
			&& (StressValues(late) == ownValues)
			&& (late.stressDropped == 0)
			&& (late.staleUpdates == own.staleUpdates + 1)
			&& (lateReading.batches == ownReading.batches + 1)
			&& (lateReading.ops == ownReading.ops + 6)
			&& (lateForwarded == 1),
		u"stress: an older wrapper's late 6-op batch with a value on the "
		"same id is forwarded and scanned, but its value and counts never "
		"reach the newer host's reading"_q,
		u"late%1 forwarded=%2 %3"_q.arg(
			StressText(late, from),
			QString::number(lateForwarded),
			ReadingText(lateReading)));
	AddRow(
		result,
		(plain.batches == 4)
			&& (plain.ops == 11)
			&& (plain.maxBatchOps == 5)
			&& (StressValues(plain) == ownValues)
			&& (plain.staleUpdates == late.staleUpdates),
		u"stress: a batch without a value counts with its ops"_q,
		StressText(plain, from));
	AddRow(
		result,
		(int(filled.stress.size()) == kWidgetStressValuesLimit)
			&& (filled.stressDropped == 1)
			&& (filled.stress[kStressShownValues].value == "bulk/0")
			&& (filled.stress.back().value == lastKept)
			&& ReceiptsInOrder(filled.stress, from, filledTill)
			&& (filled.batches == 4 + bulk)
			&& (filled.ops == 11 + bulk)
			&& (filled.maxBatchOps == 5),
		u"stress: a value past the limit counts as dropped and never "
		"replaces a kept one"_q,
		u"limit=%1 lastKept=%2 %3"_q.arg(
			QString::number(kWidgetStressValuesLimit),
			QString::fromStdString(lastKept),
			StressText(filled, from)));
	AddRow(
		result,
		readopted.adopted
			&& (readopted.htmlId == kStressReadoptHtmlId)
			&& readopted.stress.empty()
			&& (readopted.stressDropped == 0)
			&& (readopted.batches == filled.batches + 1)
			&& (readopted.ops == filled.ops + 2)
			&& (readopted.maxBatchOps == 5)
			&& !readopted.conformance
			&& (readopted.serial == own.serial),
		u"stress: a later adoption clears the values and the drops, a "
		"value on the earlier id is ignored, and the counts go on"_q,
		StressText(readopted, from));
}

} // namespace

int WidgetOpsTap::factorySelected() {
	auto lock = std::lock_guard(_mutex);
	const auto index = ++_reading.factoriesSelected;
	ClearResult(_latest);
	auto next = WidgetOpsLatest();
	next.generation = index;
	next.serial = _latest.serial;
	next.staleUpdates = _latest.staleUpdates;
	_latest = std::move(next);
	return index;
}

void WidgetOpsTap::factoryCompleted(bool threw) {
	auto lock = std::lock_guard(_mutex);
	++_reading.factoriesCompleted;
	if (threw) {
		++_reading.factoriesThrew;
	}
}

void WidgetOpsTap::factoryReleased() {
	auto lock = std::lock_guard(_mutex);
	++_reading.factoriesReleased;
}

void WidgetOpsTap::wrapperBuilt(int selection, const std::string &theme) {
	auto lock = std::lock_guard(_mutex);
	++_reading.wrappersBuilt;
	if (current(selection)) {
		_latest.scheme = theme;
	}
}

void WidgetOpsTap::wrapperDestroyed() {
	auto lock = std::lock_guard(_mutex);
	++_reading.wrappersDestroyed;
}

void WidgetOpsTap::hostMessage(int selection, const std::string &json) {
	const auto receivedAt = crl::profile();
	auto scan = ScanHostMessage(json);
	auto lock = std::lock_guard(_mutex);
	auto &reading = _reading;
	++reading.hostMessages;
	if (scan.gapped) {
		++reading.scanGaps;
		AppendExample(reading.gapExamples, std::move(scan.gap));
	}
	if (scan.batch) {
		++reading.batches;
		reading.ops += scan.ops;
		reading.leaks += int(scan.leaks.size());
		reading.notes += int(scan.notes.size());
		AppendExamples(reading.leakExamples, std::move(scan.leaks));
		AppendExamples(reading.noteExamples, std::move(scan.notes));
		if (newest(selection)) {
			CountBatch(_latest, scan.ops);
		}
	}
	if (scan.ack) {
		++reading.ackQueries;
	}
	if (scan.frozen) {
		++reading.fatalFrozen;
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

void WidgetOpsTap::rendererMessage(const std::string &json) {
	auto seen = ReadRendererMessage(json);
	if (!seen.violation && !seen.trustedEvent && !seen.ack && !seen.limit) {
		return;
	}
	auto lock = std::lock_guard(_mutex);
	auto &reading = _reading;
	if (seen.violation) {
		++reading.violations;
		AppendExample(reading.violationDetails, std::move(seen.detail));
	}
	if (seen.limit) {
		++reading.limitReports;
		AppendExample(reading.limitExamples, std::move(seen.limitExample));
	}
	if (seen.trustedEvent) {
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
	_reading.trustedEvents = 0;
	_reading.trustedClicks = 0;
	_reading.trustedEventTypes.clear();
}

WidgetOpsReading WidgetOpsTap::read() const {
	auto lock = std::lock_guard(_mutex);
	return _reading;
}

WidgetOpsLatest WidgetOpsTap::latest() const {
	auto lock = std::lock_guard(_mutex);
	return _latest;
}

twidget::RendererFactory TapWidgetRenderer(
		GlobalMsgId id,
		twidget::RendererFactory factory) {
	if (!Active()) {
		return factory;
	}
	const auto &armed = ArmedTaps();
	const auto i = armed.find(id);
	if (i == end(armed)) {
		return factory;
	}
	const auto index = i->second->factorySelected();
	return WrapFactory(i->second, index, std::move(factory));
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

WidgetOpsLatest ReadWidgetOpsLatest(const WidgetOpsTap &tap) {
	return tap.latest();
}

void MarkWidgetOpsEvents(WidgetOpsTap &tap) {
	tap.markEvents();
}

WidgetOpsSelfCheck RunWidgetOpsSelfCheck() {
	auto result = WidgetOpsSelfCheck();
	const auto active = Active();
	AddRow(
		result,
		active,
		u"the harness is active, so the seam can tap"_q,
		u"active=%1"_q.arg(Flag(active)));
	if (!active) {
		return result;
	}
	CheckUnarmed(result);
	result.control = CheckArmed(result);
	CheckThrowing(result);
	CheckSessions(result);
	CheckStress(result);
	return result;
}

auto RunWidgetConformanceProbeCheck(const QByteArray &fixture)
-> std::vector<WidgetOpsSelfCheckRow> {
	auto rows = std::vector<WidgetOpsSelfCheckRow>();
	const auto active = Active();
	AddProbeRow(
		rows,
		active,
		u"the harness is active, so the seam can tap"_q,
		u"active=%1"_q.arg(Flag(active)));
	if (!active) {
		return rows;
	}
	const auto parsed = ParseProbeFixture(fixture);
	AddProbeRow(
		rows,
		parsed.error.isEmpty(),
		u"the probe fixture parses into its cases"_q,
		u"bytes=%1 cases=%2 error=[%3]"_q.arg(
			QString::number(fixture.size()),
			QString::number(int(parsed.cases.size())),
			parsed.error));
	if (parsed.error.isEmpty()) {
		for (auto i = 0; i != int(parsed.cases.size()); ++i) {
			CheckProbeCase(rows, parsed.cases[i], i);
		}
	}
	CheckProbeReplacement(rows);
	CheckProbeTheme(rows);
	return rows;
}

} // namespace Test

#else // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS

namespace Test {

twidget::RendererFactory TapWidgetRenderer(
		GlobalMsgId,
		twidget::RendererFactory factory) {
	return factory;
}

} // namespace Test

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
