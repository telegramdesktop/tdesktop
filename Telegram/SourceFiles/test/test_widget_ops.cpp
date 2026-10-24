/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "test/test_widget_ops.h"

#ifdef _DEBUG

#include "test/test_agent.h"
#include "twidget/json.h"

#include <QtCore/QStringList>

#include <algorithm>
#include <cctype>
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

struct HostScan {
	std::vector<std::string> leaks;
	std::vector<std::string> notes;
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
	bool violation = false;
	bool trustedEvent = false;
	bool ack = false;
};

} // namespace

class WidgetOpsTap final {
public:
	void factorySelected();
	void factoryCompleted(bool threw);
	void factoryReleased();
	void wrapperBuilt();
	void wrapperDestroyed();

	// Parse and scan outside the lock, merge under it; never call out.
	void hostMessage(const std::string &json);
	void rendererMessage(const std::string &json);

	void markEvents();
	[[nodiscard]] WidgetOpsReading read() const;

private:
	mutable std::mutex _mutex;
	WidgetOpsReading _reading;

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
		std::shared_ptr<WidgetOpsTap> tap);
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

};

// Shared by every copy of one wrapping factory: released with the last.
class FactorySelection final {
public:
	explicit FactorySelection(std::shared_ptr<WidgetOpsTap> tap);
	FactorySelection(const FactorySelection &other) = delete;
	FactorySelection &operator=(const FactorySelection &other) = delete;
	~FactorySelection();

	[[nodiscard]] const std::shared_ptr<WidgetOpsTap> &tap() const;

private:
	const std::shared_ptr<WidgetOpsTap> _tap;

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
constexpr auto kDiagKind = "renderer-message";
constexpr auto kMainDiag = "self-check diag on the main thread";
constexpr auto kWorkerDiag = "self-check diag on a worker thread";
constexpr auto kReturnedMark = "self-check: send() returned";

constexpr auto kLeakSrc = "el.src: https://127.0.0.1:9999/leak.png";
constexpr auto kLeakStyle = "el.style: url(https://127.0.0.1:9999/leak.css)";
constexpr auto kNoteData = "attr data-x (inert URL inside a data: payload): "
	"data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg'/>";

[[nodiscard]] bool IsAck(const twidget::Json &qid) {
	return qid.isNumber() && (qid.asInt() == kAckQid);
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
		}
	} else if (type == "query") {
		result.ack = IsAck(message["qid"]);
	} else if (type == "frozen") {
		result.frozen = message["fatal"].asBool();
	}
	return result;
}

// The fields WidgetHost::handleRendererMessage() itself reads.
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
	return std::make_unique<TapRenderer>(std::move(inner), tap);
}

[[nodiscard]] twidget::RendererFactory WrapFactory(
		std::shared_ptr<WidgetOpsTap> tap,
		twidget::RendererFactory factory) {
	const auto selection = std::make_shared<FactorySelection>(
		std::move(tap));
	return [selection, factory = std::move(factory)](
			std::shared_ptr<twidget::SandboxResources> resources,
			const std::string &theme,
			double width) {
		return BuildTapped(
			selection->tap(),
			factory,
			std::move(resources),
			theme,
			width);
	};
}

TapRenderer::TapRenderer(
	std::unique_ptr<twidget::Renderer> inner,
	std::shared_ptr<WidgetOpsTap> tap)
: _inner(std::move(inner))
, _tap(std::move(tap)) {
	_inner->onMessage = [this](const std::string &json) {
		received(json);
	};
	_inner->onDiag = [this](
			const std::string &kind,
			const std::string &detail) {
		diagnosed(kind, detail);
	};
	_tap->wrapperBuilt();
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
	_tap->hostMessage(json);
	_inner->send(json);
}

void TapRenderer::setTheme(const std::string &theme) {
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

FactorySelection::FactorySelection(std::shared_ptr<WidgetOpsTap> tap)
: _tap(std::move(tap)) {
}

FactorySelection::~FactorySelection() {
	_tap->factoryReleased();
}

const std::shared_ptr<WidgetOpsTap> &FactorySelection::tap() const {
	return _tap;
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

} // namespace

void WidgetOpsTap::factorySelected() {
	auto lock = std::lock_guard(_mutex);
	++_reading.factoriesSelected;
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

void WidgetOpsTap::wrapperBuilt() {
	auto lock = std::lock_guard(_mutex);
	++_reading.wrappersBuilt;
}

void WidgetOpsTap::wrapperDestroyed() {
	auto lock = std::lock_guard(_mutex);
	++_reading.wrappersDestroyed;
}

void WidgetOpsTap::hostMessage(const std::string &json) {
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
	}
	if (scan.ack) {
		++reading.ackQueries;
	}
	if (scan.frozen) {
		++reading.fatalFrozen;
	}
}

void WidgetOpsTap::rendererMessage(const std::string &json) {
	auto seen = ReadRendererMessage(json);
	if (!seen.violation && !seen.trustedEvent && !seen.ack) {
		return;
	}
	auto lock = std::lock_guard(_mutex);
	auto &reading = _reading;
	if (seen.violation) {
		++reading.violations;
		AppendExample(reading.violationDetails, std::move(seen.detail));
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
	i->second->factorySelected();
	return WrapFactory(i->second, std::move(factory));
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
	return result;
}

} // namespace Test

#else // _DEBUG

namespace Test {

twidget::RendererFactory TapWidgetRenderer(
		GlobalMsgId,
		twidget::RendererFactory factory) {
	return factory;
}

} // namespace Test

#endif // _DEBUG
