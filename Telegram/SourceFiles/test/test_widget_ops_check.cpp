/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_widget_ops.h"

#include "test/test_agent.h"
#include "test/test_widget_ops_internal.h"
#include "test/test_widget_renderer_forward.h"
#include "tgw/tgw_hosted_wire.h"
#include "tgw/tgw_renderer_input.h"
#include "twidget/json.h"
#include "twidget/widget_host.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QStringList>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <future>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace Test::WidgetOps {
namespace {

namespace HostedWire = Tgw::HostedWire;

// The death WidgetHost's local glue sends no `frozen` for, and the text a
// hosted host's onDeath may still carry for a death its owner never asked for.
constexpr auto kTerminated = "terminated";

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

// The hosted leg's ids, after the stress leg's, then the budget leg's,
// then the hosted completion-order leg's two messages.
constexpr auto kHostedIndex = 6;
constexpr auto kHostedDroppedIndex = 7;
constexpr auto kBudgetIndex = 8;
constexpr auto kHostedOlderIndex = 9;
constexpr auto kHostedNewerIndex = 10;

// The hosted leg's message with two generations on one tap, and its
// messages for theme applications underway, hosts retired mid-probe and
// themes selected while a theme job runs.
constexpr auto kHostedGenerationsIndex = 13;
constexpr auto kHostedThemeIndex = 14;
constexpr auto kHostedRetireIndex = 15;
constexpr auto kHostedSelectedIndex = 16;

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
		{ u"markedGeneration"_q, reading.markedGeneration },
		{ u"markedPress"_q, int(reading.markedPress) },
		{ u"downsConfirmed"_q, reading.downsConfirmed },
		{ u"upsConfirmed"_q, reading.upsConfirmed },
		{ u"clicksCompleted"_q, reading.clicksCompleted },
		{ u"pressStepsElsewhere"_q, reading.pressStepsElsewhere },
		{ u"ackQueries"_q, reading.ackQueries },
		{ u"ackResults"_q, reading.ackResults },
		{ u"fatalFrozen"_q, reading.fatalFrozen },
		{ u"renderedHosts"_q, reading.renderedHosts },
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
		&& (dynamic_cast<ForwardingRenderer*>(wrapper.get()) != nullptr);
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

// |values| JSON values in all: an object of `type` |type| and a `pad` array
// of zeros, with |detail| as a `detail` member when it is not empty.
[[nodiscard]] std::string PaddedMessage(
		std::string_view type,
		std::string_view detail,
		std::size_t values) {
	auto result = "{\"type\":\"" + std::string(type) + '"';
	auto used = std::size_t(5);
	if (!detail.empty()) {
		result += ",\"detail\":\"" + std::string(detail) + '"';
		used += 2;
	}
	result += ",\"pad\":[";
	for (auto i = used; i < values; ++i) {
		result += (i == used) ? "0" : ",0";
	}
	return result + "]}";
}

// The leaks of LongNameBatch(): one in its attribute's value and
// kMaxExamples + 99 in its style's.
constexpr auto kLongNameLeaks = kMaxExamples + 100;

// A batch whose `attr` and `style` op names are 100 KiB long.
[[nodiscard]] std::string LongNameBatch() {
	const auto name = std::string(std::size_t(100) << 10, 'n');
	auto css = std::string();
	for (auto i = 1; i != kLongNameLeaks; ++i) {
		css += "url(https://127.0.0.1:9999/b) ";
	}
	return R"json({"type":"ops","ops":[{"op":"attr","id":5,"name":")json"
		+ name
		+ R"json(","value":"https://127.0.0.1:9999/a"},)json"
		+ R"json({"op":"style","id":5,"name":")json"
		+ name
		+ R"json(","value":")json"
		+ css
		+ R"json("}]})json";
}

// Every leak of a batch with long names is counted, while its examples stay
// within kMaxExamples and kMaxSampleBytes, each place cut to kDetailBytes.
void CheckLongNames(
		WidgetOpsSelfCheck &result,
		const WidgetOpsReading &before,
		const WidgetOpsReading &after) {
	auto bytes = std::size_t();
	auto longest = std::size_t();
	for (const auto &example : after.leakExamples) {
		bytes += example.size();
		longest = std::max(longest, example.size());
	}
	const auto attribute = "attr " + std::string(kDetailBytes - 5, 'n')
		+ ": https://127.0.0.1:9999/a";
	const auto first = after.leakExamples.empty()
		? std::string()
		: after.leakExamples.front();
	AddRow(
		result,
		before.leakExamples.empty()
			&& (after.batches == before.batches + 1)
			&& (after.leaks == before.leaks + kLongNameLeaks)
			&& (after.leakExamples.size() == std::size_t(kMaxExamples))
			&& (bytes <= kMaxSampleBytes)
			&& (longest <= std::size_t(kDetailBytes) + 2 + 160)
			&& (first == attribute)
			&& (after.scanGaps == before.scanGaps),
		u"a batch with 100 KiB attribute and style names counts all its "
		"leaks, keeps its first 64 examples within 64 KiB and cuts each "
		"place to 240 bytes"_q,
		u"leaks=%1 examples=%2 bytes=%3 longest=%4 first=%5"_q.arg(
			QString::number(after.leaks - before.leaks),
			QString::number(int(after.leakExamples.size())),
			QString::number(qint64(bytes)),
			QString::number(qint64(longest)),
			QString::number(qint64(first.size()))));
}

// A host message and a renderer message one value past the scan budget are
// gaps that are never parsed; ones exactly at it are scanned. Then a batch
// with long names (CheckLongNames()).
void CheckBudget(WidgetOpsSelfCheck &result) {
	const auto id = SelfCheckId(kBudgetIndex);
	const auto tap = ArmWidgetOpsTap(id);
	const auto state = std::make_shared<StubState>();
	auto factory = TapWidgetRenderer(id, StubFactory{ state });
	auto renderer = factory(
		std::make_shared<twidget::SandboxResources>(),
		kTheme,
		kWidth);
	const auto at = WithinScanBudget(
			PaddedMessage("anim", {}, kMaxScanValues))
		&& !WithinScanBudget(PaddedMessage("anim", {}, kMaxScanValues + 1));
	if (renderer && state->built) {
		renderer->send(PaddedMessage("anim", {}, kMaxScanValues + 1));
		renderer->send(PaddedMessage("anim", {}, kMaxScanValues));
		state->built->emitMessage(
			PaddedMessage("violation", "over", kMaxScanValues + 1));
		state->built->emitMessage(
			PaddedMessage("violation", "at", kMaxScanValues));
	}
	const auto reading = ReadWidgetOps(*tap);
	if (renderer && state->built) {
		renderer->send(LongNameBatch());
	}
	const auto sunk = ReadWidgetOps(*tap);
	renderer = nullptr;
	factory = nullptr;
	DisarmWidgetOpsTap(id);
	AddRow(
		result,
		at
			&& (reading.hostMessages == 2)
			&& (reading.scanGaps == 2)
			&& (reading.gapExamples.size() == 2)
			&& (reading.batches == 0)
			&& (reading.violations == 1)
			&& (reading.violationDetails
				== std::vector<std::string>{ "at" }),
		u"a host and a renderer message past the scan budget are gaps, "
		"never parsed, and ones at it are read"_q,
		u"counted=%1 %2 gapExamples=%3 details=%4"_q.arg(
			Flag(at),
			ReadingText(reading),
			ExamplesText(reading.gapExamples),
			ExamplesText(reading.violationDetails)));
	CheckLongNames(result, reading, sunk);
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

// The hosted leg's stand-ins for the runtime's own observers.
struct HostedProduct {
	std::vector<std::string> ops;
	std::vector<std::string> platform;
	std::vector<std::string> deaths;
};

// Work ids no RendererCore is given (theirs count up from 1), standing for
// the hosted legs' cores.
constexpr auto kHostedWork = std::numeric_limits<uint64>::max() - 3;
constexpr auto kOtherWork = kHostedWork + 1;
constexpr auto kOlderWork = kHostedWork + 2;
constexpr auto kNewerWork = kHostedWork + 3;

constexpr auto kHostedHtml = "<p>widget ops self-check</p>";
constexpr auto kHostedDeath = "the widget engine process ended";
constexpr auto kPlainPlatform = "self-check: a platform message";

// What the hosted leg's two themes give the helper's renderer, written out
// from WidgetHost::themeMessage(): the scheme the host keeps, the palette
// checked (an unknown key dropped, colours lower-cased) under the theme
// keys' order, then the set button and text tokens.
constexpr auto kAppliedThemeMessage = R"json({"type":"theme","theme":{)json"
	R"json("colorScheme":"dark","params":{"bg_color":"#000000",)json"
	R"json("text_color":"#ffffff"},"button":{"radius":"8px"},)json"
	R"json("text":{"fontSize":14}}})json";
constexpr auto kReappliedThemeMessage = R"json({"type":"theme","theme":{)json"
	R"json("colorScheme":"light","params":{"bg_color":"#ffffff"},)json"
	R"json("button":{},"text":{}}})json";

// The theme the leg's host is built with: dark, a palette with an upper-case
// colour and a key that is no theme key, one button and one text token.
[[nodiscard]] twidget::WidgetTheme AppliedTheme() {
	auto result = twidget::WidgetTheme();
	result.colorScheme = kTheme;
	result.params = {
		{ "text_color", "#FFFFFF" },
		{ "bg_color", "#000000" },
		{ "no_such_key", "#123456" },
	};
	result.button.radius = "8px";
	result.text.fontSize = 14.;
	return result;
}

// A later theme whose scheme is not "dark", which a host keeps as light.
[[nodiscard]] twidget::WidgetTheme ReappliedTheme() {
	auto result = twidget::WidgetTheme();
	result.colorScheme = "sepia";
	result.params = { { "bg_color", "#FFFFFF" } };
	return result;
}

// A hosted host with no channel, never started, so nothing calls its
// observers but the leg itself; its threads are joined when it is
// destroyed.
[[nodiscard]] std::shared_ptr<twidget::WidgetHost> HostedStub(
		const std::shared_ptr<HostedProduct> &product) {
	auto result = std::make_shared<twidget::WidgetHost>(
		std::string(kHostedHtml),
		AppliedTheme(),
		kWidth,
		twidget::WidgetHostOptions(),
		std::shared_ptr<twidget::SandboxHostedChannel>());
	result->onRenderOps = [product](const std::string &clean) {
		product->ops.push_back(clean);
	};
	result->onPlatformMessage = [product](const std::string &message) {
		product->platform.push_back(message);
	};
	result->onDeath = [product](const std::string &reason) {
		product->deaths.push_back(reason);
	};
	return result;
}

[[nodiscard]] std::string MirroredRecord(char way, std::string_view text) {
	return HostedWire::EncodeMirrored(way, text).value_or(std::string());
}

[[nodiscard]] QString HostedLatestText(const WidgetOpsLatest &latest) {
	return u"generation=%1 scheme=%2 schemeChanges=%3 themeMessage=%4 "
		"batches=%5 ops=%6 staleUpdates=%7"_q.arg(
			QString::number(latest.generation),
			QString::fromStdString(latest.scheme),
			QString::number(latest.schemeChanges),
			Flag(latest.themeMessage == kThemeMessage),
			QString::number(latest.batches),
			QString::number(latest.ops),
			QString::number(latest.staleUpdates));
}

[[nodiscard]] QString AppliedText(const WidgetOpsLatest &latest) {
	return u"appliedScheme=%1 appliedThemes=%2 appliedThemeMessage=[%3] "
		"scheme=%4 schemeChanges=%5 themeMessage=[%6]"_q.arg(
			QString::fromStdString(latest.appliedScheme),
			QString::number(latest.appliedThemes),
			QString::fromStdString(latest.appliedThemeMessage),
			QString::fromStdString(latest.scheme),
			QString::number(latest.schemeChanges),
			QString::fromStdString(latest.themeMessage));
}

[[nodiscard]] QString EffectiveText(const WidgetOpsLatest &latest) {
	return u"appliedThemes=%1 effectiveThemes=%2 effectiveSerial=%3 "
		"effectiveMissed=%4 effectiveScheme=%5 effectiveParams=[%6] "
		"effectiveMismatch=[%7] effectiveError=[%8] scheme=%9 "
		"schemeChanges=%10 themeMessage=[%11]"_q.arg(
			QString::number(latest.appliedThemes),
			QString::number(latest.effectiveThemes),
			QString::number(latest.effectiveSerial),
			QString::number(latest.effectiveMissed),
			QString::fromStdString(latest.effectiveScheme),
			QString::fromStdString(latest.effectiveParams),
			QString::fromStdString(latest.effectiveMismatch),
			QString::fromStdString(latest.effectiveError),
			QString::fromStdString(latest.scheme),
			QString::number(latest.schemeChanges),
			QString::fromStdString(latest.themeMessage));
}

[[nodiscard]] bool TapFinal(const WidgetOpsReading &reading) {
	return (reading.factoriesReleased == reading.factoriesSelected)
		&& (reading.wrappersDestroyed == reading.wrappersBuilt);
}

// The mapping of helper stamps to Telegram's clock and the sums of their
// times, without a record: full 32-bit counts kept as they came, a record
// that ends at its receipt mapped to the profile reading taken with it,
// one that ends after its receipt refused before mapping however short and
// ordered, an offset that does not fit refused, and sums at the limits.
void CheckHelperWorkArithmetic(WidgetOpsSelfCheck &result) {
	using Limits = std::numeric_limits<crl::profile_time>;
	constexpr auto kAll = std::numeric_limits<std::uint32_t>::max();
	const auto wide = HelperWorkEvent(
		HelperWorkRecord{
			.started = 5,
			.finished = 12,
			.waited = 7,
			.waits = kAll,
			.frameBytes = kAll,
			.kind = HelperWorkKind::Tick,
		},
		1000,
		400,
		3);
	const auto late = HelperWorkRecord{
		.started = Limits::max() - 57,
		.finished = Limits::max() - 50,
	};
	const auto brief = HelperWorkRecord{ .started = 995, .finished = 1003 };
	const auto last = HelperWorkEvent(late, Limits::max(), late.finished, 3);
	const auto ahead = HelperWorkEvent(brief, 5000, 1000, 3);
	const auto received = HelperWorkEvent(brief, 5000, 1003, 3);
	const auto early = HelperWorkRecord{ .started = 0, .finished = 5 };
	const auto offsetLow = HelperWorkEvent(early, Limits::min() + 10, 11, 3);
	const auto offsetHigh = HelperWorkEvent(
		HelperWorkRecord{ .started = -9, .finished = -2 },
		Limits::max(),
		-1,
		3);
	const auto mapped = wide
		&& (wide->started == 605)
		&& (wide->finished == 612)
		&& (wide->waited == 7)
		&& (wide->waits == kAll)
		&& (wide->frameBytes == kAll)
		&& (wide->generation == 3)
		&& (wide->kind == WidgetHelperWorkKind::Tick)
		&& last
		&& (last->finished == Limits::max())
		&& !ahead
		&& received
		&& (received->finished == 5000)
		&& !offsetLow
		&& !offsetHigh;
	const auto sums = (CheckedTimeSum(Limits::max(), 0) == Limits::max())
		&& !CheckedTimeSum(Limits::max(), 1)
		&& (CheckedTimeSum(Limits::min(), 0) == Limits::min())
		&& !CheckedTimeSum(Limits::min(), -1)
		&& (CheckedTimeSum(-5, -3) == -8)
		&& (CheckedTimeSum(Limits::max(), Limits::min()) == -1);
	AddRow(
		result,
		mapped && sums,
		u"hosted: helper stamps map to the profile clock with full 32-bit "
		"counts, a record that ends after its receipt is refused before "
		"mapping however short, an offset past the clock's range is "
		"refused, and time sums refuse an overflow"_q,
		u"wide=%1 waits=%2 frameBytes=%3 last=%4 ahead=%5 received=%6 "
			"offsets=%7,%8 sums=%9"_q.arg(
			Flag(wide.has_value()),
			QString::number(wide ? wide->waits : 0),
			QString::number(wide ? wide->frameBytes : 0),
			Flag(last.has_value()),
			Flag(ahead.has_value()),
			Flag(received.has_value()),
			Flag(offsetLow.has_value()),
			Flag(offsetHigh.has_value()),
			Flag(sums)));
}

// Helper work records: one within the reserved room, one past it and one
// spanning exactly an hour past it too (malformed instead while the clock
// has run for less than an hour), then records that do not decode (a stray
// byte; stamps unordered, negative or more than an hour apart; more waited
// than the interval) and one that ends a minute after its receipt, each
// consumed in Debug and passed on without a Debug helper, with the kept
// event on Telegram's profile clock.
void ExerciseHelperWork(
		WidgetOpsSelfCheck &result,
		WidgetOpsTap &tap,
		twidget::WidgetHost &host,
		const HostedProduct &product) {
	const auto platformBefore = product.platform.size();
	ReserveWidgetHelperWork(tap, 1);
	const auto before = crl::profile();
	const auto now = SteadyMicros();
	const auto record = HelperWorkRecord{
		.started = now - 1000,
		.finished = now - 993,
		.waited = 3,
		.seq = 11,
		.waits = 2,
		.frameBytes = 99,
		.kind = HelperWorkKind::Paint,
	};
	const auto mirrored = [](const HelperWorkRecord &changed) {
		return MirroredRecord(
			HostedWire::kMirroredWork,
			EncodeHelperWork(changed));
	};
	auto hour = record;
	hour.started = record.finished - kMaxHelperInterval;
	hour.waited = kMaxHelperInterval;
	const auto hourKept = (hour.started >= 0);
	auto unordered = record;
	unordered.finished = record.started - 1;
	auto negative = record;
	negative.started = -1;
	auto longer = record;
	longer.started = record.finished - kMaxHelperInterval - 1;
	auto overwaited = record;
	overwaited.waited = 8;
	auto ahead = record;
	ahead.started = now - 5;
	ahead.finished = now + 60'000'000;
	ahead.waited = 0;
	const auto valid = mirrored(record);
	const auto malformed = std::vector<std::string>{
		MirroredRecord(
			HostedWire::kMirroredWork,
			EncodeHelperWork(record) + 'x'),
		mirrored(unordered),
		mirrored(negative),
		mirrored(longer),
		mirrored(overwaited),
		mirrored(ahead),
	};
	host.onPlatformMessage(valid);
	host.onPlatformMessage(valid);
	host.onPlatformMessage(mirrored(hour));
	for (const auto &message : malformed) {
		host.onPlatformMessage(message);
	}
	const auto after = crl::profile();
	const auto work = ReadWidgetHelperWork(tap);
	const auto passed = int(product.platform.size() - platformBefore);
	const auto refused = int(malformed.size());
	const auto event = work.events.empty()
		? WidgetHelperWorkEvent()
		: work.events.front();
	const auto kept = (work.events.size() == 1)
		&& (event.started >= before - 2000)
		&& (event.started <= after)
		&& (event.finished == event.started + 7)
		&& (event.waited == 3)
		&& (event.waits == 2)
		&& (event.seq == 11)
		&& (event.frameBytes == 99)
		&& (event.kind == WidgetHelperWorkKind::Paint)
		&& (work.dropped == 2 + refused)
		&& (work.malformed == refused + (hourKept ? 0 : 1))
		&& (work.capacity == 1)
		&& !work.asked
		&& (passed == 0);
	const auto unread = work.events.empty()
		&& (work.dropped == 0)
		&& (passed == 3 + refused);
	AddRow(
		result,
		kHelperMirrors ? kept : unread,
		kHelperMirrors
			? u"hosted: a helper work record is kept on the profile clock "
				"within its room, two past it drop, and unordered, negative, "
				"over an hour long, overwaited, stray-byte records and one "
				"that ends after its receipt are malformed"_q
			: u"hosted: without a Debug helper a helper work record passes "
				"on unread"_q,
		u"events=%1 dropped=%2 malformed=%3 passed=%4 started=%5 "
			"window=%6..%7 hourKept=%8"_q.arg(
			QString::number(int(work.events.size())),
			QString::number(work.dropped),
			QString::number(work.malformed),
			QString::number(passed),
			QString::number(event.started),
			QString::number(before),
			QString::number(after),
			Flag(hourKept)));
}

// Drives a hosted host's observers the way its host thread would: the
// validated batch, platform messages with every mirrored way, a malformed
// mirror and a repeated build, then two deaths.
void ExerciseHosted(
		WidgetOpsSelfCheck &result,
		WidgetOpsTap &tap,
		twidget::WidgetHost &host,
		const HostedProduct &product) {
	const auto control = std::string(kControlBatch);
	const auto clean = control.substr(
		kOpsPrefix.size(),
		control.size() - kOpsPrefix.size() - kOpsSuffix.size());
	host.onRenderOps(clean);
	const auto batch = ReadWidgetOps(tap);
	AddRow(
		result,
		(batch.hostMessages == 1)
			&& (batch.batches == 1)
			&& (batch.ops == 4)
			&& (batch.leaks == 2)
			&& (batch.notes == 1)
			&& (product.ops == std::vector<std::string>{ clean }),
		u"hosted: a validated batch is scanned as the host's ops message "
		"and then reaches the observer it wrapped, unchanged"_q,
		u"productOps=%1 %2"_q.arg(
			QString::number(int(product.ops.size())),
			ReadingText(batch)));

	const auto malformed = MirroredRecord(
		HostedWire::kMirroredHost,
		kAckQuery) + 'x';
	const auto messages = std::vector<std::string>{
		kPlainPlatform,
		malformed,
		MirroredRecord(HostedWire::kMirroredBuilt, kTheme),
		MirroredRecord(HostedWire::kMirroredHost, kAckQuery),
		MirroredRecord(HostedWire::kMirroredHost, kThemeMessage),
		MirroredRecord(HostedWire::kMirroredHost, kFrozen),
		MirroredRecord(HostedWire::kMirroredRenderer, kAckResult),
		MirroredRecord(HostedWire::kMirroredRenderer, kViolation),
		MirroredRecord(HostedWire::kMirroredRenderer, kPaintReport),
		MirroredRecord(HostedWire::kMirroredRenderer, kTrustedClick),
		MirroredRecord(HostedWire::kMirroredTheme, kNextTheme),
		MirroredRecord(HostedWire::kMirroredBuilt, kTheme),
	};
	MarkWidgetOpsEvents(tap);
	for (const auto &message : messages) {
		host.onPlatformMessage(message);
	}
	const auto mirrored = ReadWidgetOps(tap);
	const auto latest = ReadWidgetOpsLatest(tap);
	const auto passed = kHelperMirrors
		? std::vector<std::string>{ kPlainPlatform, malformed }
		: messages;
	AddRow(
		result,
		(product.platform == passed),
		kHelperMirrors
			? u"hosted: every platform message but a well-formed mirror "
				"reaches the observer it wrapped, in order, unchanged"_q
			: u"hosted: without a Debug helper every platform message "
				"reaches the observer it wrapped, in order, unchanged"_q,
		u"productMessages=%1 expected=%2"_q.arg(
			QString::number(int(product.platform.size())),
			QString::number(int(passed.size()))));
	const auto recorded = kHelperMirrors
		? ((mirrored.hostMessages == 4)
			&& (mirrored.ackQueries == 1)
			&& (mirrored.ackResults == 1)
			&& (mirrored.fatalFrozen == 0)
			&& (mirrored.violations == 1)
			&& (mirrored.limitReports == 1)
			&& (mirrored.trustedClicks == 1)
			&& (latest.scheme == kNextTheme)
			&& (latest.schemeChanges == 1)
			&& (latest.themeMessage == kThemeMessage))
		: ((mirrored.hostMessages == 1)
			&& (mirrored.ackQueries == 0)
			&& (mirrored.ackResults == 0)
			&& (mirrored.violations == 0)
			&& (mirrored.trustedClicks == 0)
			&& latest.scheme.empty()
			&& (latest.schemeChanges == 0)
			&& latest.themeMessage.empty());
	AddRow(
		result,
		recorded
			&& (latest.batches == 1)
			&& (latest.ops == 4)
			&& !mirrored.scanGaps,
		kHelperMirrors
			? u"hosted: the mirrored build, served-host messages, renderer "
				"messages and theme reach their recorders; a served frozen is "
				"no death and a repeated build changes nothing"_q
			: u"hosted: without a Debug helper only the validated batches "
				"are recorded, no scheme before the widget's own reading"_q,
		u"%1 %2"_q.arg(ReadingText(mirrored), HostedLatestText(latest)));

	auto diagnostics = twidget::WidgetDiagnostics();
	host.onChange(diagnostics);
	const auto unrendered = ReadWidgetOps(tap).renderedHosts;
	diagnostics.rendered = true;
	host.onChange(diagnostics);
	host.onChange(diagnostics);
	const auto rendered = ReadWidgetOps(tap).renderedHosts;
	const auto unprobed = ReadWidgetOpsLatest(tap);
	const auto observe = WidgetThemeObservation(host);
	if (observe) {
		observe(host);
	}
	const auto probed = ReadWidgetOpsLatest(tap);
	AddRow(
		result,
		(unrendered == 0)
			&& (rendered == 1)
			&& (unprobed.effectiveMissed == 0)
			&& (unprobed.effectiveThemes == 0)
			&& observe
			&& (probed.effectiveMissed == 1)
			&& (probed.effectiveThemes == 0)
			&& probed.effectiveError.starts_with("no theme reading")
			&& !WidgetThemeUnobserved(probed).empty()
			&& (kHelperMirrors || probed.scheme.empty()),
		u"hosted: the host's own rendered acknowledgment counts once and "
		"probes nothing itself; the observation the runtime runs as the "
		"host's job probes the built theme, and a widget with no runtime "
		"gives no reading, which leaves the theme unobserved"_q,
		u"before=%1 after=%2 observation=%3 unprobedMissed=%4 %5"_q.arg(
			QString::number(unrendered),
			QString::number(rendered),
			Flag(observe != nullptr),
			QString::number(unprobed.effectiveMissed),
			EffectiveText(probed)));

	ExerciseHelperWork(result, tap, host, product);
	CheckHelperWorkArithmetic(result);

	host.onDeath(kTerminated);
	const auto terminated = ReadWidgetOps(tap).fatalFrozen;
	host.onDeath(kHostedDeath);
	const auto died = ReadWidgetOps(tap).fatalFrozen;
	const auto deaths = std::vector<std::string>{ kTerminated, kHostedDeath };
	AddRow(
		result,
		(terminated == 1) && (died == 2) && (product.deaths == deaths),
		u"hosted: every death the host reports counts as a fatal frozen, "
		"one reading terminated too, and reaches the observer it wrapped"_q,
		u"afterTerminated=%1 afterDeath=%2 productDeaths=%3"_q.arg(
			QString::number(terminated),
			QString::number(died),
			QString::number(int(product.deaths.size()))));
}

// What a widget answers the effective theme probe with, read against
// ReappliedTheme(): a matching reading (the palette completed beyond the
// theme's one key), one in the other scheme, one with another colour for
// the theme's key, one that is no [scheme, palette] pair, and the theme
// message a matching reading gives without a Debug helper.
constexpr auto kEffectiveMatching
	= R"json(["light",{"bg_color":"#ffffff","text_color":"#000000"}])json";
constexpr auto kEffectiveDark = R"json(["dark",{"bg_color":"#ffffff"}])json";
constexpr auto kEffectivePalette
	= R"json(["light",{"bg_color":"#fefefe"}])json";
constexpr auto kEffectiveMalformed = R"json({"colorScheme":"light"})json";
constexpr auto kEffectiveMessage = R"json({"type":"theme","theme":{)json"
	R"json("colorScheme":"light","params":{"bg_color":"#ffffff",)json"
	R"json("text_color":"#000000"}}})json";
constexpr auto kDarkMismatch = "colorScheme [dark], applied [light]";
constexpr auto kPaletteMismatch = "bg_color [#fefefe], applied [#ffffff]";

// The effective readings of the tap's newest theme, ReappliedTheme() as
// its second, as the seam's probe would hand them over: a reading for the
// superseded first theme, an oversized one and a malformed one are not
// kept; a reading in the other scheme and one with another colour are kept
// with their difference; a matching one is kept clean. Without a Debug
// helper they are also the scheme, its changes and the theme message.
void ExerciseEffective(WidgetOpsSelfCheck &result, WidgetOpsTap &tap) {
	const auto generation = ReadWidgetOpsLatest(tap).generation;
	const auto theme = ReappliedTheme();
	const auto take = [&](int serial, std::string_view answer) {
		tap.themeEffective(generation, serial, ReadEffective(answer, theme));
	};
	const auto before = ReadWidgetOpsLatest(tap);
	take(1, kEffectiveMatching);
	take(2, std::string(kMaxEffectiveBytes + 1, ' '));
	take(2, kEffectiveMalformed);
	const auto missed = ReadWidgetOpsLatest(tap);
	AddRow(
		result,
		(missed.effectiveMissed == before.effectiveMissed + 3)
			&& (missed.effectiveThemes == 0)
			&& missed.effectiveError.starts_with("no theme reading")
			&& !WidgetThemeUnobserved(missed).empty()
			&& (missed.scheme == before.scheme)
			&& (missed.schemeChanges == before.schemeChanges)
			&& (missed.themeMessage == before.themeMessage),
		u"hosted: a theme reading for a superseded theme, one past 64 KiB "
		"and one that is no [scheme, palette] pair are not kept, and the "
		"theme stays unobserved"_q,
		EffectiveText(missed));

	take(2, kEffectiveDark);
	const auto dark = ReadWidgetOpsLatest(tap);
	take(2, kEffectivePalette);
	const auto palette = ReadWidgetOpsLatest(tap);
	const auto releaseDiffers = (dark.scheme == "dark")
		&& (dark.schemeChanges == 0)
		&& (palette.scheme == "light")
		&& (palette.schemeChanges == 1);
	const auto mirrorKept = (dark.scheme == before.scheme)
		&& (palette.scheme == before.scheme)
		&& (palette.schemeChanges == before.schemeChanges);
	AddRow(
		result,
		WidgetThemeUnobserved(dark).empty()
			&& (dark.effectiveThemes == 1)
			&& (dark.effectiveSerial == 2)
			&& (dark.effectiveScheme == "dark")
			&& (dark.effectiveMismatch == kDarkMismatch)
			&& (palette.effectiveMismatch == kPaletteMismatch)
			&& (kHelperMirrors ? mirrorKept : releaseDiffers),
		kHelperMirrors
			? u"hosted: a reading in another scheme or with another colour "
				"for the theme's key is kept with its difference, the "
				"renderer's scheme staying the mirror's"_q
			: u"hosted: without a Debug helper a reading in another scheme "
				"or with another colour is kept with its difference and is "
				"the scheme: set by the first, changed by the next"_q,
		EffectiveText(dark) + u" | "_q + EffectiveText(palette));

	take(2, kEffectiveMatching);
	const auto matching = ReadWidgetOpsLatest(tap);
	const auto releaseMatches = (matching.scheme == "light")
		&& (matching.schemeChanges == 1)
		&& (matching.themeMessage == kEffectiveMessage);
	const auto mirrorMatches = (matching.scheme == before.scheme)
		&& (matching.themeMessage == before.themeMessage);
	AddRow(
		result,
		WidgetThemeUnobserved(matching).empty()
			&& (matching.effectiveThemes == 3)
			&& matching.effectiveMismatch.empty()
			&& (matching.effectiveScheme == "light")
			&& (matching.effectiveParams
				== R"json({"bg_color":"#ffffff","text_color":"#000000"})json")
			&& (kHelperMirrors ? mirrorMatches : releaseMatches),
		kHelperMirrors
			? u"hosted: a reading with the theme's scheme and palette is "
				"kept clean, the palette as the widget completed it"_q
			: u"hosted: without a Debug helper a reading with the theme's "
				"scheme and palette is kept clean and gives the scheme and "
				"the theme message"_q,
		EffectiveText(matching));
}

using PressStep = Tgw::PressStep::Kind;

// The chat's dispatch of a primary press to |work| with the pointer
// |serial|, through the product's seam, as RendererCore::pointerPress()
// reports it.
void DispatchPress(uint64 work, uint64 serial) {
	WidgetPressDispatched(work, serial);
}

// The fixture's own mouse-down, which the chat dispatches to |work| with
// |serial| while it is being delivered, as the attack audit plays it.
void PlayPress(WidgetOpsTap &tap, uint64 work, uint64 serial) {
	DeliverMarkedPress(tap, [&] {
		DispatchPress(work, serial);
	});
}

// |work|'s primary press |press| through the product's seam, |steps| in
// order, as Tgw::HostedInput reports them, its Sent with the pointer
// |serial| the chat dispatched it with.
void ObservePress(
		uint64 work,
		uint64 press,
		std::initializer_list<PressStep> steps,
		uint64 serial = 0) {
	for (const auto kind : steps) {
		WidgetPressObserved(work, press, {
			.kind = kind,
			.serial = (kind == PressStep::Sent) ? serial : uint64(),
		});
	}
}

// A press the chat dispatched with |serial| whose up landed where its down
// did: the dispatch and every step.
void ObserveClick(uint64 work, uint64 press, uint64 serial) {
	DispatchPress(work, serial);
	ObservePress(
		work,
		press,
		{ PressStep::Sent, PressStep::Down, PressStep::Up, PressStep::Click },
		serial);
}

// The fixture's played click: its mouse-down dispatched with |serial|
// inside its delivery, then every step of that press.
void PlayClick(WidgetOpsTap &tap, uint64 work, uint64 press, uint64 serial) {
	PlayPress(tap, work, serial);
	ObservePress(
		work,
		press,
		{ PressStep::Sent, PressStep::Down, PressStep::Up, PressStep::Click },
		serial);
}

[[nodiscard]] QString PressText(const WidgetOpsReading &reading) {
	return u"marked=%1/%2/%3/%4 downs=%5 ups=%6 clicks=%7 elsewhere=%8 "
		"superseded=%9"_q.arg(
			QString::number(reading.markedGeneration),
			QString::number(reading.markedWork),
			QString::number(reading.markedSerial),
			QString::number(reading.markedPress),
			QString::number(reading.downsConfirmed),
			QString::number(reading.upsConfirmed),
			QString::number(reading.clicksCompleted),
			QString::number(reading.pressStepsElsewhere),
			QString::number(reading.markSuperseded))
		+ u" detached="_q
		+ Flag(reading.markDetached);
}

[[nodiscard]] bool PressCounts(
		const WidgetOpsReading &reading,
		uint64 press,
		int downs,
		int ups,
		int clicks,
		int elsewhere) {
	return (reading.markedPress == press)
		&& (reading.downsConfirmed == downs)
		&& (reading.upsConfirmed == ups)
		&& (reading.clicksCompleted == clicks)
		&& (reading.pressStepsElsewhere == elsewhere);
}

// The runtime's theme and press seams on a tapped hosted host: a later
// applied theme (its probe answered by no runtime), the readings a probe
// could give, another host's theme, and the steps of its work's presses
// around marks: an earlier press that completes late, a marked press
// answered without a click, a played mouse-down that reached no core
// followed by a genuine click, a stable click, and other works' presses.
void ExerciseSeams(
		WidgetOpsSelfCheck &result,
		WidgetOpsTap &tap,
		twidget::WidgetHost &host) {
	const auto before = ReadWidgetOpsLatest(tap);
	const auto token = WidgetThemeScheduled(host, ReappliedTheme());
	const auto pending = ReadWidgetOpsLatest(tap).themePending;
	WidgetThemeApplied(host, ReappliedTheme(), token);
	const auto reapplied = ReadWidgetOpsLatest(tap);
	const auto renderer = kHelperMirrors
		? ((reapplied.scheme == kNextTheme)
			&& (reapplied.schemeChanges == 1)
			&& (reapplied.themeMessage == kThemeMessage))
		: (reapplied.scheme.empty()
			&& (reapplied.schemeChanges == 0)
			&& reapplied.themeMessage.empty());
	AddRow(
		result,
		token
			&& (pending == token)
			&& !reapplied.themePending
			&& (reapplied.appliedScheme == kNextTheme)
			&& (reapplied.appliedThemeMessage == kReappliedThemeMessage)
			&& (reapplied.appliedThemes == 2)
			&& (reapplied.effectiveMissed == before.effectiveMissed + 1)
			&& (reapplied.effectiveThemes == 0)
			&& !WidgetThemeUnobserved(reapplied).empty()
			&& renderer,
		kHelperMirrors
			? u"hosted: a theme the runtime applies later is pending from "
				"its scheduling, then recorded as applied and probed; a "
				"widget with no runtime gives no reading, and the "
				"renderer's reading stays the mirror's"_q
			: u"hosted: without a Debug helper a theme the runtime applies "
				"later is pending from its scheduling, then recorded as "
				"applied and probed, never as the renderer's scheme; a "
				"widget with no runtime gives none"_q,
		u"token=%1 pending=%2 "_q.arg(
			QString::number(token),
			QString::number(pending))
			+ AppliedText(reapplied)
			+ u" "_q
			+ EffectiveText(reapplied));

	ExerciseEffective(result, tap);

	const auto otherProduct = std::make_shared<HostedProduct>();
	auto other = std::shared_ptr<twidget::WidgetHost>();
	try {
		other = HostedStub(otherProduct);
	} catch (const std::exception &) {
	}
	const auto read = ReadWidgetOpsLatest(tap);
	auto observed = true;
	auto scheduled = uint64(1);
	if (other) {
		WidgetThemeSelected(*other, ReappliedTheme());
		scheduled = WidgetThemeScheduled(*other, AppliedTheme());
		WidgetThemeApplied(*other, AppliedTheme(), scheduled);
		WidgetHostRetired(*other);
		observed = (WidgetThemeObservation(*other) != nullptr);
	}
	const auto untouched = ReadWidgetOpsLatest(tap);
	AddRow(
		result,
		other
			&& !observed
			&& !scheduled
			&& (untouched.appliedThemes == 2)
			&& (untouched.appliedScheme == kNextTheme)
			&& (untouched.effectiveThemes == read.effectiveThemes)
			&& (untouched.effectiveMissed == read.effectiveMissed)
			&& !untouched.themePending
			&& !untouched.hostRetired,
		u"hosted: a theme selected, scheduled and applied to a host without "
		"a tap, and its retirement, change no tap, and that host has no "
		"theme observation"_q,
		u"other=%1 observation=%2 scheduled=%3 %4"_q.arg(
			Flag(other != nullptr),
			Flag(observed),
			QString::number(scheduled),
			AppliedText(untouched)));
	other = nullptr;

	const auto generation = ReadWidgetOpsLatest(tap).generation;
	MarkWidgetOpsEvents(tap);
	PlayPress(tap, kHostedWork, 1);
	ObservePress(kHostedWork, 7, { PressStep::Sent, PressStep::Down }, 1);
	MarkWidgetOpsEvents(tap);
	ObservePress(kHostedWork, 7, { PressStep::Up, PressStep::Click });
	const auto late = ReadWidgetOps(tap);
	PlayPress(tap, kHostedWork, 2);
	ObservePress(
		kHostedWork,
		8,
		{ PressStep::Sent, PressStep::Down, PressStep::Up },
		2);
	const auto replaced = ReadWidgetOps(tap);
	AddRow(
		result,
		(late.markedGeneration == generation)
			&& !late.markedSerial
			&& PressCounts(late, 0, 0, 0, 0, 2)
			&& (replaced.markedWork == kHostedWork)
			&& (replaced.markedSerial == 2)
			&& PressCounts(replaced, 8, 1, 1, 0, 2),
		u"hosted: a press sent before the mark that completes after it "
		"counts for no reading; the press played after the mark is bound "
		"at its dispatch inside its delivery, and its answered down and up "
		"are delivery, no click while its renderer decided none (its target "
		"replaced before the up)"_q,
		PressText(late) + u" | "_q + PressText(replaced));

	DispatchPress(kHostedWork, 3);
	MarkWidgetOpsEvents(tap);
	PlayPress(tap, kHostedWork, 4);
	DispatchPress(kHostedWork, 5);
	ObservePress(
		kHostedWork,
		9,
		{ PressStep::Sent, PressStep::Down, PressStep::Up, PressStep::Click },
		3);
	ObservePress(
		kHostedWork,
		10,
		{ PressStep::Sent, PressStep::Down, PressStep::Up },
		4);
	const auto queued = ReadWidgetOps(tap);
	AddRow(
		result,
		(queued.markedSerial == 4)
			&& PressCounts(queued, 10, 1, 1, 0, 4),
		u"hosted: a press the chat dispatched before the mark, sent and "
		"clicked only after it, never takes the mark: the press the chat "
		"dispatched inside the played delivery is the marked one, and its "
		"own missing click decision leaves the reading without a click"_q,
		PressText(queued));

	MarkWidgetOpsEvents(tap);
	DispatchPress(kHostedWork, 13);
	DeliverMarkedPress(tap, [] {});
	ObserveClick(kHostedWork, 13, 13);
	ObserveClick(kHostedWork, 14, 14);
	PlayPress(tap, kHostedWork, 15);
	const auto missed = ReadWidgetOps(tap);
	AddRow(
		result,
		!missed.markedWork
			&& !missed.markedSerial
			&& PressCounts(missed, 0, 0, 0, 0, 8),
		u"hosted: a press the chat dispatched after the mark but before the "
		"played delivery, a played mouse-down that reached no core, then a "
		"genuine click of the same work and a second delivery bind nothing: "
		"the mark sealed unbound when its delivery returned, and every later "
		"step counts elsewhere"_q,
		PressText(missed));

	MarkWidgetOpsEvents(tap);
	PlayPress(tap, kHostedWork, 6);
	ObservePress(kHostedWork, 11, { PressStep::Sent, PressStep::Down }, 6);
	ObservePress(kHostedWork, 11, { PressStep::Click });
	ObserveClick(kOtherWork, 11, 7);
	ObserveClick(0, 11, 8);
	ObservePress(kHostedWork, 11, { PressStep::Up, PressStep::Click });
	ObserveClick(kHostedWork, 12, 9);
	const auto stable = ReadWidgetOps(tap);
	MarkWidgetOpsEvents(tap);
	const auto marked = ReadWidgetOps(tap);
	AddRow(
		result,
		(stable.markedSerial == 6)
			&& PressCounts(stable, 11, 1, 1, 1, 1 + 4)
			&& !marked.markedWork
			&& !marked.markedSerial
			&& PressCounts(marked, 0, 0, 0, 0, 0),
		u"hosted: the marked press's click counts once, after its up; a "
		"click before its up and a later press's steps count elsewhere, "
		"another work's and work 0's reach no tap, and the mark restarts "
		"every press reading"_q,
		PressText(stable) + u" | "_q + PressText(marked));
}

// One message's tap with two generations, an older host whose work is
// kOlderWork and a newer one with kNewerWork: marked on the newer, the
// older generation's press that completes late counts elsewhere, a down
// and up of the newer's press without a click is incomplete, and its
// stable click completes; then a third generation selected between the
// newer's marked down and its up supersedes the mark, so neither that
// press's late click decision nor the newer host's mirrored trusted click
// counts, while one counted before the selection stays.
void CheckHostedGenerations(WidgetOpsSelfCheck &result) {
	const auto id = SelfCheckId(kHostedGenerationsIndex);
	const auto tap = ArmWidgetOpsTap(id);
	const auto older = std::make_shared<HostedProduct>();
	const auto newer = std::make_shared<HostedProduct>();
	auto olderInstall = TapHostedWidget(id, kOlderWork, AppliedTheme());
	auto newerInstall = TapHostedWidget(id, kNewerWork, AppliedTheme());
	auto olderHost = std::shared_ptr<twidget::WidgetHost>();
	auto newerHost = std::shared_ptr<twidget::WidgetHost>();
	try {
		olderHost = HostedStub(older);
		newerHost = HostedStub(newer);
	} catch (const std::exception &) {
	}
	const auto built = olderInstall && newerInstall && olderHost && newerHost;
	if (built) {
		olderInstall(olderHost);
		newerInstall(newerHost);
	}
	DispatchPress(kOlderWork, 1);
	ObservePress(kOlderWork, 1, { PressStep::Sent, PressStep::Down }, 1);
	MarkWidgetOpsEvents(*tap);
	ObservePress(kOlderWork, 1, { PressStep::Up, PressStep::Click });
	PlayPress(*tap, kNewerWork, 1);
	ObservePress(
		kNewerWork,
		1,
		{ PressStep::Sent, PressStep::Down, PressStep::Up },
		1);
	const auto withheld = ReadWidgetOps(*tap);
	MarkWidgetOpsEvents(*tap);
	PlayClick(*tap, kNewerWork, 2, 2);
	const auto clicked = ReadWidgetOps(*tap);
	AddRow(
		result,
		built
			&& (withheld.markedGeneration == 2)
			&& (withheld.markedWork == kNewerWork)
			&& PressCounts(withheld, 1, 1, 1, 0, 2)
			&& PressCounts(clicked, 2, 1, 1, 1, 0),
		u"hosted: on one tap marked on the newer generation, the older "
		"generation's late up and click count elsewhere, the newer's down "
		"and up alone complete no click, and its stable click does"_q,
		u"built=%1 %2 | %3"_q.arg(
			Flag(built),
			PressText(withheld),
			PressText(clicked)));

	const auto mirroredClick = [&] {
		if (newerHost) {
			newerHost->onPlatformMessage(MirroredRecord(
				HostedWire::kMirroredRenderer,
				kTrustedClick));
		}
	};
	MarkWidgetOpsEvents(*tap);
	PlayPress(*tap, kNewerWork, 3);
	ObservePress(kNewerWork, 3, { PressStep::Sent, PressStep::Down }, 3);
	mirroredClick();
	auto third = TapHostedWidget(id, 0, AppliedTheme());
	ObservePress(kNewerWork, 3, { PressStep::Up, PressStep::Click });
	mirroredClick();
	const auto superseded = ReadWidgetOps(*tap);
	third = nullptr;
	const auto trusted = kHelperMirrors ? 1 : 0;
	AddRow(
		result,
		built
			&& (superseded.markedGeneration == 2)
			&& (superseded.markSuperseded == 3)
			&& PressCounts(superseded, 3, 1, 0, 0, 2)
			&& (superseded.trustedClicks == trusted),
		u"hosted: a generation selected between the marked press's down "
		"and its up supersedes the mark: the press's late up and click "
		"decision count elsewhere and the old host's mirrored trusted click "
		"no longer counts, while what counted before the selection stays"_q,
		u"built=%1 %2 trustedClicks=%3"_q.arg(
			Flag(built),
			PressText(superseded),
			QString::number(superseded.trustedClicks)));
	olderHost = nullptr;
	newerHost = nullptr;
	olderInstall = nullptr;
	newerInstall = nullptr;
	DisarmWidgetOpsTap(id);
}

// The theme legs' palettes: a light one, another light one with only its
// text colour changed, and a dark one; what a widget answers the effective
// probe with for each of the light ones, and a dark reading whose link
// colour stayed the light one's.
[[nodiscard]] twidget::WidgetTheme LegTheme(
		std::string scheme,
		std::string background,
		std::string text,
		std::string link) {
	auto result = twidget::WidgetTheme();
	result.colorScheme = std::move(scheme);
	result.params = {
		{ "bg_color", std::move(background) },
		{ "text_color", std::move(text) },
		{ "link_color", std::move(link) },
	};
	return result;
}

[[nodiscard]] twidget::WidgetTheme LightTheme() {
	return LegTheme("light", "#ffffff", "#000000", "#2481cc");
}

[[nodiscard]] twidget::WidgetTheme OtherLightTheme() {
	return LegTheme("light", "#ffffff", "#222222", "#2481cc");
}

[[nodiscard]] twidget::WidgetTheme DarkTheme() {
	return LegTheme("dark", "#000000", "#ffffff", "#6ab2f2");
}

[[nodiscard]] twidget::WidgetTheme ThirdLightTheme() {
	return LegTheme("light", "#ffffff", "#000000", "#168acd");
}

constexpr auto kLightReading = R"json(["light",{"bg_color":"#ffffff",)json"
	R"json("text_color":"#000000","link_color":"#2481cc"}])json";
constexpr auto kOtherLightReading = R"json(["light",{"bg_color":"#ffffff",)json"
	R"json("text_color":"#222222","link_color":"#2481cc"}])json";
constexpr auto kThirdLightReading = R"json(["light",{"bg_color":"#ffffff",)json"
	R"json("text_color":"#000000","link_color":"#168acd"}])json";
constexpr auto kStaleLinkReading = R"json(["dark",{"bg_color":"#000000",)json"
	R"json("text_color":"#ffffff","link_color":"#2481cc"}])json";
constexpr auto kStaleLinkMismatch = "link_color [#2481cc], applied [#6ab2f2]";

// How long a held probe and the leg waiting for it may take before the
// row fails instead of hanging the self-check.
constexpr auto kHeldProbeBound = std::chrono::seconds(5);

[[nodiscard]] QString ThemeCheckText(
		const WidgetOpsLatest &latest,
		const WidgetThemeCheck &check) {
	const auto verdict = (check.verdict == WidgetThemeVerdict::Matches)
		? u"matches"_q
		: (check.verdict == WidgetThemeVerdict::Wrong)
		? u"wrong"_q
		: u"unobserved"_q;
	return u"verdict=%1 detail=[%2] pending=%3 retired=%4 %5"_q.arg(
		verdict,
		QString::fromStdString(check.detail),
		QString::number(latest.themePending),
		Flag(latest.hostRetired),
		EffectiveText(latest));
}

// A probe the leg holds on the worker that runs it: |answer| once the main
// thread lets it go, after it told the main thread it began. Each wait is
// bounded, so a lost signal fails the row instead of hanging.
class HeldProbe final {
public:
	explicit HeldProbe(std::string answer);

	[[nodiscard]] Fn<std::string()> probe();
	[[nodiscard]] bool waitEntered();
	void release();

private:
	struct Signals {
		std::promise<void> entered;
		std::promise<void> released;
		std::shared_future<void> releasedFuture;
	};

	const std::string _answer;
	const std::shared_ptr<Signals> _signals;
	std::future<void> _entered;

};

HeldProbe::HeldProbe(std::string answer)
: _answer(std::move(answer))
, _signals(std::make_shared<Signals>()) {
	_signals->releasedFuture = _signals->released.get_future().share();
	_entered = _signals->entered.get_future();
}

Fn<std::string()> HeldProbe::probe() {
	return [signals = _signals, answer = _answer] {
		signals->entered.set_value();
		signals->releasedFuture.wait_for(kHeldProbeBound);
		return answer;
	};
}

bool HeldProbe::waitEntered() {
	return _entered.wait_for(kHeldProbeBound) == std::future_status::ready;
}

void HeldProbe::release() {
	_signals->released.set_value();
}

// One tapped hosted host whose theme the leg drives through the product's
// seams, built with LightTheme().
struct ThemeLeg {
	std::shared_ptr<WidgetOpsTap> tap;
	std::shared_ptr<HostedProduct> product;
	std::shared_ptr<twidget::WidgetHost> host;
	Fn<void(const std::shared_ptr<twidget::WidgetHost> &host)> install;
};

[[nodiscard]] ThemeLeg StartThemeLeg(int index) {
	const auto id = SelfCheckId(index);
	auto result = ThemeLeg{
		.tap = ArmWidgetOpsTap(id),
		.product = std::make_shared<HostedProduct>(),
	};
	result.install = TapHostedWidget(id, 0, LightTheme());
	try {
		result.host = HostedStub(result.product);
	} catch (const std::exception &) {
	}
	if (result.install && result.host) {
		result.install(result.host);
	}
	return result;
}

void EndThemeLeg(ThemeLeg &leg, int index) {
	if (leg.host) {
		ReplaceEffectiveProbe(*leg.host, nullptr);
	}
	leg.host = nullptr;
	leg.install = nullptr;
	DisarmWidgetOpsTap(SelfCheckId(index));
}

// A matched light palette changed to another light one on the same host,
// through the runtime's seams: from the scheduling on, the old reading
// certifies nothing, whatever the scheme; an older job's application ends
// no newer pending one; the pending job's own application and its probe
// after the barrier make it observed again. Then a fresh switch to dark
// whose reading kept one light colour (the link's) is Wrong, naming it.
void CheckHostedTheme(WidgetOpsSelfCheck &result) {
	auto leg = StartThemeLeg(kHostedThemeIndex);
	const auto built = leg.install && leg.host;
	const auto &tap = *leg.tap;
	const auto generation = ReadWidgetOpsLatest(tap).generation;
	leg.tap->themeEffective(
		generation,
		1,
		ReadEffective(kLightReading, LightTheme()));
	const auto matched = ReadWidgetOpsLatest(tap);
	const auto matchedCheck = CheckWidgetTheme(matched, "light");
	auto older = uint64();
	auto newer = uint64();
	auto replaced = false;
	if (built) {
		replaced = ReplaceEffectiveProbe(*leg.host, [] {
			return std::string(kOtherLightReading);
		});
		older = WidgetThemeScheduled(*leg.host, OtherLightTheme());
	}
	const auto barrier = ReadWidgetOpsLatest(tap);
	const auto barrierCheck = CheckWidgetTheme(barrier, "light");
	if (built) {
		newer = WidgetThemeScheduled(*leg.host, LightTheme());
		WidgetThemeApplied(*leg.host, OtherLightTheme(), older);
	}
	const auto stale = ReadWidgetOpsLatest(tap);
	const auto staleCheck = CheckWidgetTheme(stale, "light");
	if (built) {
		ReplaceEffectiveProbe(*leg.host, [] {
			return std::string(kLightReading);
		});
		WidgetThemeApplied(*leg.host, LightTheme(), newer);
	}
	const auto confirmed = ReadWidgetOpsLatest(tap);
	const auto confirmedCheck = CheckWidgetTheme(confirmed, "light");
	AddRow(
		result,
		built
			&& replaced
			&& (matchedCheck.verdict == WidgetThemeVerdict::Matches)
			&& older
			&& (barrier.themePending == older)
			&& (barrier.appliedThemes == 1)
			&& (barrier.effectiveSerial == 1)
			&& barrier.effectiveMismatch.empty()
			&& (barrierCheck.verdict == WidgetThemeVerdict::Unobserved)
			&& barrierCheck.detail.starts_with("theme-pending")
			&& newer
			&& (newer != older)
			&& (stale.themePending == newer)
			&& (stale.appliedThemes == 2)
			&& (staleCheck.verdict == WidgetThemeVerdict::Unobserved)
			&& staleCheck.detail.starts_with("theme-pending")
			&& !confirmed.themePending
			&& (confirmed.appliedThemes == 3)
			&& (confirmed.effectiveSerial == 3)
			&& (confirmedCheck.verdict == WidgetThemeVerdict::Matches),
		u"hosted: a light palette changed to another light one is "
		"unobserved from its scheduling, though the old reading had the "
		"same scheme and no mismatch; an older job's application ends no "
		"newer pending one, and the pending job's application and its "
		"probe after the barrier make it observed"_q,
		u"built=%1 replaced=%2 tokens=%3,%4 | %5 | %6 | %7 | %8"_q.arg(
			Flag(built),
			Flag(replaced),
			QString::number(older),
			QString::number(newer),
			ThemeCheckText(matched, matchedCheck),
			ThemeCheckText(barrier, barrierCheck),
			ThemeCheckText(stale, staleCheck),
			ThemeCheckText(confirmed, confirmedCheck)));

	const auto before = ReadWidgetOpsLatest(tap);
	if (built) {
		ReplaceEffectiveProbe(*leg.host, [] {
			return std::string(kStaleLinkReading);
		});
		const auto token = WidgetThemeScheduled(*leg.host, DarkTheme());
		WidgetThemeApplied(*leg.host, DarkTheme(), token);
	}
	const auto switched = ReadWidgetOpsLatest(tap);
	const auto wrong = CheckWidgetTheme(switched, "dark", &before);
	AddRow(
		result,
		built
			&& (switched.appliedThemes == before.appliedThemes + 1)
			&& (switched.effectiveSerial == switched.appliedThemes)
			&& (switched.appliedScheme == "dark")
			&& (switched.effectiveScheme == "dark")
			&& (switched.effectiveMismatch == kStaleLinkMismatch)
			&& (wrong.verdict == WidgetThemeVerdict::Wrong)
			&& wrong.detail.ends_with(kStaleLinkMismatch),
		u"hosted: a fresh switch to dark whose reading has the dark scheme "
		"and background but kept one light colour is Wrong, naming that "
		"colour"_q,
		ThemeCheckText(switched, wrong));
	EndThemeLeg(leg, kHostedThemeIndex);
}

// A host retired while its theme jobs' probes run, with no newer factory
// selected for its message: a held constructor observation released with
// no retirement is kept, the control; a switch's probe held across the
// retirement keeps its application but no reading, and the switch stays
// unobserved; after it the runtime can schedule nothing for that host and
// a late job records nothing.
void CheckHostedRetire(WidgetOpsSelfCheck &result) {
	auto leg = StartThemeLeg(kHostedRetireIndex);
	const auto built = leg.install && leg.host;
	const auto &tap = *leg.tap;
	const auto observe = built
		? WidgetThemeObservation(*leg.host)
		: Fn<void(twidget::WidgetHost &host)>();
	auto entered = false;
	if (built && observe) {
		auto held = HeldProbe(kLightReading);
		ReplaceEffectiveProbe(*leg.host, held.probe());
		auto job = std::thread([&] {
			observe(*leg.host);
		});
		entered = held.waitEntered();
		held.release();
		job.join();
	}
	const auto kept = ReadWidgetOpsLatest(tap);
	const auto keptCheck = CheckWidgetTheme(kept, "light");
	AddRow(
		result,
		built
			&& observe
			&& entered
			&& (kept.effectiveThemes == 1)
			&& (keptCheck.verdict == WidgetThemeVerdict::Matches),
		u"hosted: a held constructor observation released while its host "
		"is live is kept, the control for the retirement below"_q,
		u"built=%1 observation=%2 entered=%3 %4"_q.arg(
			Flag(built),
			Flag(bool(observe)),
			Flag(entered),
			ThemeCheckText(kept, keptCheck)));

	const auto before = ReadWidgetOpsLatest(tap);
	auto heldEntered = false;
	auto token = uint64();
	if (built) {
		auto held = HeldProbe(kStaleLinkReading);
		ReplaceEffectiveProbe(*leg.host, held.probe());
		token = WidgetThemeScheduled(*leg.host, DarkTheme());
		auto job = std::thread([&] {
			WidgetThemeApplied(*leg.host, DarkTheme(), token);
		});
		heldEntered = held.waitEntered();
		WidgetHostRetired(*leg.host);
		held.release();
		job.join();
	}
	const auto revoked = ReadWidgetOpsLatest(tap);
	const auto revokedCheck = CheckWidgetTheme(revoked, "dark", &before);
	auto late = uint64(1);
	if (built) {
		ReplaceEffectiveProbe(*leg.host, [] {
			return std::string(kLightReading);
		});
		WidgetThemeSelected(*leg.host, LightTheme());
		late = WidgetThemeScheduled(*leg.host, LightTheme());
		WidgetThemeApplied(*leg.host, LightTheme(), late);
		if (observe) {
			observe(*leg.host);
		}
	}
	const auto after = ReadWidgetOpsLatest(tap);
	AddRow(
		result,
		built
			&& heldEntered
			&& token
			&& revoked.hostRetired
			&& (revoked.generation == before.generation)
			&& (revoked.appliedThemes == before.appliedThemes + 1)
			&& (revoked.effectiveSerial == before.effectiveSerial)
			&& (revoked.effectiveMissed == before.effectiveMissed + 1)
			&& (revoked.effectiveError.find("retired") != std::string::npos)
			&& (revokedCheck.verdict == WidgetThemeVerdict::Unobserved)
			&& !late
			&& !after.themePending
			&& (after.appliedThemes == revoked.appliedThemes)
			&& (after.effectiveThemes == revoked.effectiveThemes)
			&& (after.effectiveMissed == revoked.effectiveMissed + 2),
		u"hosted: a switch's probe held across its host's retirement, with "
		"no newer factory selected, keeps no reading and the switch stays "
		"unobserved; after the retirement nothing is selected or scheduled, "
		"and a late application or observation records nothing"_q,
		u"built=%1 entered=%2 token=%3 late=%4 | %5 | %6"_q.arg(
			Flag(built),
			Flag(heldEntered),
			QString::number(token),
			QString::number(late),
			ThemeCheckText(revoked, revokedCheck),
			EffectiveText(after)));
	EndThemeLeg(leg, kHostedRetireIndex);
}

// The runtime's busy guard: a theme selected while another theme's job
// runs (WidgetThemeSelected()) and the job for it started only after that
// one ended. A light palette B applied and read by its job after a third
// light palette C was selected leaves the load and stress reading and a
// switch begun before B unobserved, though B's job recorded a fresh serial
// and a reading with the scheme and no mismatch; C's own job carries the
// token its selection made pending, and its application and probe make
// both Matches. A selection that returns to the running job's palette
// makes that job's application the pending one again; one that returns
// after the job recorded its application leaves none pending, as no job
// is still to start.
void CheckHostedSelected(WidgetOpsSelfCheck &result) {
	auto leg = StartThemeLeg(kHostedSelectedIndex);
	const auto built = leg.install && leg.host;
	const auto &tap = *leg.tap;
	const auto answer = [&](std::string_view reading) {
		if (built) {
			ReplaceEffectiveProbe(*leg.host, [reading] {
				return std::string(reading);
			});
		}
	};
	const auto pending = [&] {
		return ReadWidgetOpsLatest(tap).themePending;
	};
	leg.tap->themeEffective(
		ReadWidgetOpsLatest(tap).generation,
		1,
		ReadEffective(kLightReading, LightTheme()));
	const auto before = ReadWidgetOpsLatest(tap);
	auto older = uint64();
	auto selected = uint64();
	auto newer = uint64();
	if (built) {
		answer(kOtherLightReading);
		older = WidgetThemeScheduled(*leg.host, OtherLightTheme());
		WidgetThemeSelected(*leg.host, ThirdLightTheme());
		selected = pending();
		WidgetThemeApplied(*leg.host, OtherLightTheme(), older);
	}
	const auto between = ReadWidgetOpsLatest(tap);
	const auto load = CheckWidgetTheme(between, "light");
	const auto switched = CheckWidgetTheme(between, "light", &before);
	if (built) {
		answer(kThirdLightReading);
		newer = WidgetThemeScheduled(*leg.host, ThirdLightTheme());
		WidgetThemeApplied(*leg.host, ThirdLightTheme(), newer);
	}
	const auto measured = ReadWidgetOpsLatest(tap);
	const auto loadMeasured = CheckWidgetTheme(measured, "light");
	const auto switchMeasured = CheckWidgetTheme(measured, "light", &before);
	const auto unobserved = [](const WidgetThemeCheck &check) {
		return (check.verdict == WidgetThemeVerdict::Unobserved)
			&& check.detail.starts_with("theme-pending");
	};
	AddRow(
		result,
		built
			&& older
			&& selected
			&& (selected != older)
			&& (between.themePending == selected)
			&& (between.appliedThemes == 2)
			&& (between.effectiveSerial == 2)
			&& between.effectiveMismatch.empty()
			&& unobserved(load)
			&& unobserved(switched)
			&& (newer == selected)
			&& !measured.themePending
			&& (measured.appliedThemes == 3)
			&& (measured.effectiveSerial == 3)
			&& (loadMeasured.verdict == WidgetThemeVerdict::Matches)
			&& (switchMeasured.verdict == WidgetThemeVerdict::Matches),
		u"hosted: a light palette selected while another light palette's "
		"job runs is pending from its selection: after that job recorded "
		"its fresh serial and a clean reading of the same scheme, the load "
		"and stress reading and a switch begun before it are unobserved, "
		"until the selected palette's own job, carrying the selection's "
		"token, is applied and read"_q,
		u"built=%1 tokens=%2,%3,%4 | load %5 | switch %6 | measured %7 | "
		"switch %8"_q.arg(
			Flag(built),
			QString::number(older),
			QString::number(selected),
			QString::number(newer),
			ThemeCheckText(between, load),
			ThemeCheckText(between, switched),
			ThemeCheckText(measured, loadMeasured),
			ThemeCheckText(measured, switchMeasured)));

	auto running = uint64();
	auto away = uint64();
	auto back = uint64();
	if (built) {
		answer(kOtherLightReading);
		running = WidgetThemeScheduled(*leg.host, OtherLightTheme());
		WidgetThemeSelected(*leg.host, LightTheme());
		away = pending();
		WidgetThemeSelected(*leg.host, OtherLightTheme());
		back = pending();
		WidgetThemeApplied(*leg.host, OtherLightTheme(), running);
	}
	const auto returned = ReadWidgetOpsLatest(tap);
	const auto returnedCheck = CheckWidgetTheme(returned, "light");
	auto recorded = uint64();
	auto late = uint64();
	if (built) {
		answer(kLightReading);
		recorded = WidgetThemeScheduled(*leg.host, LightTheme());
		WidgetThemeApplied(*leg.host, LightTheme(), recorded);
		WidgetThemeSelected(*leg.host, ThirdLightTheme());
		late = pending();
		WidgetThemeSelected(*leg.host, LightTheme());
	}
	const auto settled = ReadWidgetOpsLatest(tap);
	const auto settledCheck = CheckWidgetTheme(settled, "light");
	AddRow(
		result,
		built
			&& running
			&& away
			&& (away != running)
			&& (back == running)
			&& !returned.themePending
			&& (returned.effectiveSerial == returned.appliedThemes)
			&& (returnedCheck.verdict == WidgetThemeVerdict::Matches)
			&& recorded
			&& late
			&& (late != recorded)
			&& !settled.themePending
			&& (settled.appliedThemes == returned.appliedThemes + 1)
			&& (settledCheck.verdict == WidgetThemeVerdict::Matches),
		u"hosted: a selection that returns to the running job's palette "
		"makes that job's application the pending one again, and one that "
		"returns after the job recorded its application leaves none "
		"pending: the reading of the palette the host was given matches"_q,
		u"built=%1 tokens=%2,%3,%4,%5,%6 | %7 | %8"_q.arg(
			Flag(built),
			QString::number(running),
			QString::number(away),
			QString::number(back),
			QString::number(recorded),
			QString::number(late),
			ThemeCheckText(returned, returnedCheck),
			ThemeCheckText(settled, settledCheck)));
	EndThemeLeg(leg, kHostedSelectedIndex);
}

struct HostedOrderSide {
	std::shared_ptr<WidgetOpsTap> tap;
	std::shared_ptr<HostedProduct> product;
	std::shared_ptr<twidget::WidgetHost> host;
	Fn<void(const std::shared_ptr<twidget::WidgetHost> &host)> install;
};

[[nodiscard]] HostedOrderSide HostedOrderStart(int index, uint64 work) {
	const auto id = SelfCheckId(index);
	auto result = HostedOrderSide{
		.tap = ArmWidgetOpsTap(id),
		.product = std::make_shared<HostedProduct>(),
	};
	result.install = TapHostedWidget(id, work, AppliedTheme());
	try {
		result.host = HostedStub(result.product);
	} catch (const std::exception &) {
	}
	return result;
}

// The press steps a tap saw since its mark: its marked press's, its Sent
// included, and every other.
[[nodiscard]] int PressSteps(const WidgetOpsReading &reading) {
	return (reading.markedPress ? 1 : 0)
		+ reading.downsConfirmed
		+ reading.upsConfirmed
		+ reading.clicksCompleted
		+ reading.pressStepsElsewhere;
}

// Whole clicks of both works, then the steps each side's tap saw since its
// mark.
[[nodiscard]] std::pair<int, int> HostedOrderClicks(
		HostedOrderSide &older,
		HostedOrderSide &newer,
		int olderClicks,
		int newerClicks) {
	MarkWidgetOpsEvents(*older.tap);
	MarkWidgetOpsEvents(*newer.tap);
	for (auto i = 0; i != olderClicks; ++i) {
		ObserveClick(kOlderWork, uint64(i + 1), uint64(i + 1));
	}
	for (auto i = 0; i != newerClicks; ++i) {
		ObserveClick(kNewerWork, uint64(i + 1), uint64(i + 1));
	}
	return {
		PressSteps(ReadWidgetOps(*older.tap)),
		PressSteps(ReadWidgetOps(*newer.tap)),
	};
}

// Two messages' hosted taps, the older construction's selected first, as
// for a core destroyed while its construction ran and a newer core built
// after it. They install in the order |olderFirst| names, the older late
// otherwise; then each work's press steps reach its own message's tap
// alone, and the newer's still do after the older host is destroyed.
void CheckHostedOrder(WidgetOpsSelfCheck &result, bool olderFirst) {
	auto older = HostedOrderStart(kHostedOlderIndex, kOlderWork);
	auto newer = HostedOrderStart(kHostedNewerIndex, kNewerWork);
	const auto order = olderFirst
		? u"older first"_q
		: u"newer first, the older late"_q;
	const auto built = older.install
		&& newer.install
		&& older.host
		&& newer.host;
	if (built) {
		auto &first = olderFirst ? older : newer;
		auto &second = olderFirst ? newer : older;
		first.install(first.host);
		second.install(second.host);
	}
	const auto both = HostedOrderClicks(older, newer, 1, 2);
	older.host = nullptr;
	const auto after = HostedOrderClicks(older, newer, 3, 4);
	AddRow(
		result,
		built && (both == std::pair(4, 8)) && (after == std::pair(0, 16)),
		u"hosted: installed %1, each work's press steps reach its own "
		"message's tap alone, and destroying the older host leaves the "
		"newer's registration"_q.arg(order),
		u"built=%1 both=%2,%3 afterDestroy=%4,%5"_q.arg(
			Flag(built),
			QString::number(both.first),
			QString::number(both.second),
			QString::number(after.first),
			QString::number(after.second)));
	newer.host = nullptr;
	older.install = nullptr;
	newer.install = nullptr;
	DisarmWidgetOpsTap(SelfCheckId(kHostedOlderIndex));
	DisarmWidgetOpsTap(SelfCheckId(kHostedNewerIndex));
}

// A hosted tap through its whole life, and one dropped uninstalled.
void CheckHosted(WidgetOpsSelfCheck &result) {
	const auto id = SelfCheckId(kHostedIndex);
	const auto product = std::make_shared<HostedProduct>();
	const auto theme = AppliedTheme();
	const auto unarmed = TapHostedWidget(id, kHostedWork, theme);
	AddRow(
		result,
		!unarmed,
		u"hosted: an unarmed message gets no tap"_q,
		u"tap=%1"_q.arg(Flag(bool(unarmed))));

	const auto tap = ArmWidgetOpsTap(id);
	auto install = TapHostedWidget(id, kHostedWork, theme);
	const auto selected = ReadWidgetOps(*tap);
	const auto generation = ReadWidgetOpsLatest(*tap).generation;
	AddRow(
		result,
		bool(install)
			&& (selected.factoriesSelected == 1)
			&& (selected.factoriesCompleted == 0)
			&& (selected.factoriesReleased == 0)
			&& (selected.wrappersBuilt == 0)
			&& (generation == 1),
		u"hosted: an armed message gets a tap, selected at once as the "
		"next generation"_q,
		u"tap=%1 generation=%2 %3"_q.arg(
			Flag(bool(install)),
			QString::number(generation),
			ReadingText(selected)));

	auto host = std::shared_ptr<twidget::WidgetHost>();
	auto error = QString();
	try {
		host = HostedStub(product);
	} catch (const std::exception &e) {
		error = QString::fromUtf8(e.what());
	}
	AddRow(
		result,
		(host != nullptr),
		u"hosted: a hosted host without a channel is built"_q,
		u"error=[%1]"_q.arg(error));
	if (host) {
		install(host);
		install(host);
		const auto installed = ReadWidgetOps(*tap);
		const auto applied = ReadWidgetOpsLatest(*tap);
		install = nullptr;
		const auto released = ReadWidgetOps(*tap);
		AddRow(
			result,
			(installed.factoriesCompleted == 1)
				&& (installed.wrappersBuilt == 1)
				&& (installed.factoriesReleased == 0)
				&& (released.factoriesReleased == 1)
				&& (released.wrappersDestroyed == 0)
				&& !TapFinal(released),
			u"hosted: a second install does nothing, and the tap's last "
			"copy releases it while the host's observations go on"_q,
			u"installed: %1 | released: %2"_q.arg(
				ReadingText(installed),
				ReadingText(released)));
		AddRow(
			result,
			(applied.appliedScheme == kTheme)
				&& (applied.appliedThemeMessage == kAppliedThemeMessage)
				&& (applied.appliedThemes == 1)
				&& applied.scheme.empty()
				&& applied.themeMessage.empty()
				&& (applied.effectiveThemes == 0)
				&& (applied.effectiveMissed == 0)
				&& !WidgetThemeUnobserved(applied).empty(),
			u"hosted: the install records the theme the host was built with "
			"as applied, its message as the served host builds it, and "
			"leaves the renderer's scheme and theme message to a reading "
			"(the Debug helper's mirror, else the widget's own), the theme "
			"unobserved until then"_q,
			AppliedText(applied) + u" "_q + EffectiveText(applied));
		ExerciseHosted(result, *tap, *host, *product);
		ExerciseSeams(result, *tap, *host);
		host = nullptr;
		const auto destroyed = ReadWidgetOps(*tap);
		MarkWidgetOpsEvents(*tap);
		ObserveClick(kHostedWork, 11, 11);
		const auto unregistered = ReadWidgetOps(*tap);
		AddRow(
			result,
			(destroyed.wrappersDestroyed == 1)
				&& TapFinal(destroyed)
				&& (PressSteps(unregistered) == 0),
			u"hosted: destroying the host ends its observations, no step of "
			"its work's presses counts after it, and the reading is "
			"final"_q,
			u"%1 | %2"_q.arg(
				ReadingText(destroyed),
				PressText(unregistered)));
	}
	install = nullptr;

	const auto droppedId = SelfCheckId(kHostedDroppedIndex);
	const auto droppedTap = ArmWidgetOpsTap(droppedId);
	auto dropped = TapHostedWidget(droppedId, 0, theme);
	const auto before = ReadWidgetOps(*droppedTap);
	dropped = nullptr;
	const auto after = ReadWidgetOps(*droppedTap);
	AddRow(
		result,
		(before.factoriesSelected == 1)
			&& (before.factoriesCompleted == 0)
			&& (after.factoriesCompleted == 1)
			&& (after.factoriesThrew == 0)
			&& (after.factoriesReleased == 1)
			&& (after.wrappersBuilt == 0)
			&& TapFinal(after),
		u"hosted: a tap dropped uninstalled, as by a failed or discarded "
		"construction, completes and releases its generation"_q,
		ReadingText(after));

	DisarmWidgetOpsTap(id);
	DisarmWidgetOpsTap(droppedId);
	const auto later = TapHostedWidget(id, kHostedWork, theme);
	AddRow(
		result,
		!later && (ReadWidgetOps(*tap).factoriesSelected == 1),
		u"hosted: a disarmed message gets no tap again"_q,
		u"tap=%1"_q.arg(Flag(bool(later))));

	CheckHostedOrder(result, true);
	CheckHostedOrder(result, false);
	CheckHostedGenerations(result);
	CheckHostedTheme(result);
	CheckHostedRetire(result);
	CheckHostedSelected(result);
}

// The local press leg's message, and works no RendererCore is given and no
// other leg uses: its two generations', a later one's and one never built.
constexpr auto kLocalPressIndex = 17;
constexpr auto kLocalOlderWork = std::numeric_limits<uint64>::max() - 11;
constexpr auto kLocalNewerWork = kLocalOlderWork + 1;
constexpr auto kLocalLaterWork = kLocalOlderWork + 2;
constexpr auto kLocalPendingWork = kLocalOlderWork + 3;

// The local retirement leg's message, and its works below the press leg's:
// one generation each, the newest when it is marked.
constexpr auto kLocalRetireIndex = 18;
constexpr auto kLocalRetiredWork = kLocalOlderWork - 1;
constexpr auto kLocalHeldWork = kLocalOlderWork - 2;
constexpr auto kLocalClosedWork = kLocalOlderWork - 3;
constexpr auto kLocalKeptWork = kLocalOlderWork - 4;
constexpr auto kLocalUnforwardedWork = kLocalOlderWork - 5;

// A local generation: the stub's state and the wrapper the wrapping
// factory built around it, the factory itself released right after, as
// WidgetHost releases it once it built its renderer.
struct LocalSide {
	std::shared_ptr<StubState> state = std::make_shared<StubState>();
	std::unique_ptr<twidget::Renderer> wrapper;
};

[[nodiscard]] LocalSide LocalStart(GlobalMsgId id, uint64 work) {
	auto result = LocalSide();
	auto factory = TapWidgetRenderer(id, work, StubFactory{ result.state });
	result.wrapper = factory(nullptr, kTheme, kWidth);
	return result;
}

// The trusted click the renderer of |side| sends its host, through the
// wrapper.
void SendLocalClick(const LocalSide &side) {
	if (side.wrapper && side.state->built) {
		side.state->built->emitMessage(kTrustedClick);
	}
}

// What RendererWork::releasePointer() does with a release its renderer
// answered with a trusted click, or with none: hands the answer to the
// seam, forwards it, its click reaching the tap through the wrapper of
// |to| (none: the click reached no wrapper, the host link closed or the
// work not ready), and reports the forwarding.
void ReleaseLocal(uint64 work, bool clicked, const LocalSide *to = nullptr) {
	auto out = std::vector<twidget::Json>();
	auto parsed = clicked
		? twidget::Json::parse(kTrustedClick)
		: std::nullopt;
	if (parsed) {
		out.push_back(std::move(*parsed));
	}
	WidgetLocalPressReleased(work, out);
	if (clicked && to) {
		SendLocalClick(*to);
	}
	WidgetLocalPressForwarded(work);
}

// One message's local taps, two generations registered with their works
// by TapWidgetRenderer() and marked on the newer, through the product's
// WidgetPressDispatched(), WidgetLocalPressDown(),
// WidgetLocalPressReleased() and WidgetLocalPressForwarded(), each played
// press dispatched inside DeliverMarkedPress(): only the release of the
// played press's own down, taken since the mark while its generation is
// the newest, counts its trusted click, as the wrapper got it.
void CheckLocalPresses(WidgetOpsSelfCheck &result) {
	const auto id = SelfCheckId(kLocalPressIndex);
	auto unarmed = TapWidgetRenderer(
		id,
		kLocalNewerWork,
		StubFactory{ std::make_shared<StubState>() });
	const auto untouched = (unarmed.target<StubFactory>() != nullptr);
	unarmed = nullptr;
	const auto tap = ArmWidgetOpsTap(id);
	auto older = LocalStart(id, kLocalOlderWork);
	auto newer = LocalStart(id, kLocalNewerWork);
	const auto wrapped = [](const LocalSide &side) {
		return side.wrapper
			&& side.state->built
			&& (side.wrapper.get() != side.state->built);
	};
	const auto built = untouched && wrapped(older) && wrapped(newer);

	DispatchPress(kLocalOlderWork, 1);
	WidgetLocalPressDown(kLocalOlderWork, 1);
	MarkWidgetOpsEvents(*tap);
	ReleaseLocal(kLocalOlderWork, true, &older);
	const auto late = ReadWidgetOps(*tap);
	PlayPress(*tap, kLocalNewerWork, 2);
	WidgetLocalPressDown(kLocalNewerWork, 2);
	ReleaseLocal(kLocalNewerWork, false);
	const auto replaced = ReadWidgetOps(*tap);
	AddRow(
		result,
		built
			&& (late.markedGeneration == 2)
			&& !late.markedSerial
			&& PressCounts(late, 0, 0, 0, 0, 2)
			&& !late.trustedClicks
			&& (replaced.markedWork == kLocalNewerWork)
			&& (replaced.markedSerial == 2)
			&& PressCounts(replaced, 0, 1, 1, 0, 2),
		u"local: an unarmed message's work is registered for no tap; marked "
		"on the newer of two local generations, the older's press taken "
		"before the mark and clicked after it counts elsewhere, and the "
		"press played to the newer's work, bound at its dispatch inside its "
		"delivery, taken and released without a trusted click is no click"_q,
		u"built=%1 %2 | %3"_q.arg(
			Flag(built),
			PressText(late),
			PressText(replaced)));

	DispatchPress(kLocalNewerWork, 3);
	MarkWidgetOpsEvents(*tap);
	PlayPress(*tap, kLocalNewerWork, 4);
	WidgetLocalPressDown(kLocalNewerWork, 3);
	ReleaseLocal(kLocalNewerWork, true, &newer);
	WidgetLocalPressDown(kLocalNewerWork, 4);
	ReleaseLocal(kLocalNewerWork, false);
	const auto queued = ReadWidgetOps(*tap);
	AddRow(
		result,
		(queued.markedSerial == 4)
			&& PressCounts(queued, 0, 1, 1, 0, 3)
			&& (queued.trustedClicks == 1),
		u"local: a press the chat dispatched before the mark, taken and "
		"clicked after it, counts elsewhere beside the generation's trusted "
		"click, and the played press that clicks none has no click"_q,
		PressText(queued)
			+ u" trustedClicks="_q
			+ QString::number(queued.trustedClicks));

	MarkWidgetOpsEvents(*tap);
	DispatchPress(kLocalNewerWork, 5);
	DeliverMarkedPress(*tap, [] {});
	WidgetLocalPressDown(kLocalNewerWork, 5);
	ReleaseLocal(kLocalNewerWork, true, &newer);
	DispatchPress(kLocalNewerWork, 6);
	WidgetLocalPressDown(kLocalNewerWork, 6);
	ReleaseLocal(kLocalNewerWork, true);
	PlayPress(*tap, kLocalNewerWork, 7);
	const auto missed = ReadWidgetOps(*tap);
	AddRow(
		result,
		!missed.markedWork
			&& !missed.markedSerial
			&& PressCounts(missed, 0, 0, 0, 0, 6)
			&& (missed.trustedClicks == 1),
		u"local: after a played mouse-down that reached no core, genuine "
		"clicks of the same work and a second delivery bind nothing and "
		"count elsewhere, whatever trusted click the generation sent"_q,
		PressText(missed)
			+ u" trustedClicks="_q
			+ QString::number(missed.trustedClicks));

	MarkWidgetOpsEvents(*tap);
	PlayPress(*tap, kLocalNewerWork, 8);
	WidgetLocalPressDown(kLocalNewerWork, 8);
	const auto withheld = ReadWidgetOps(*tap);
	DispatchPress(kLocalNewerWork, 9);
	WidgetLocalPressDown(kLocalNewerWork, 9);
	ReleaseLocal(kLocalNewerWork, true);
	const auto overtaken = ReadWidgetOps(*tap);
	AddRow(
		result,
		(withheld.markedSerial == 8)
			&& PressCounts(withheld, 0, 1, 0, 0, 0)
			&& (overtaken.markedSerial == 8)
			&& PressCounts(overtaken, 0, 1, 0, 0, 3),
		u"local: the played press taken with no release yet has no click, "
		"and a later down of the same work makes the next release and its "
		"click that press's, never the played one's"_q,
		PressText(withheld) + u" | "_q + PressText(overtaken));

	MarkWidgetOpsEvents(*tap);
	PlayPress(*tap, kLocalNewerWork, 10);
	WidgetLocalPressDown(kLocalNewerWork, 10);
	ReleaseLocal(kLocalNewerWork, true, &newer);
	ReleaseLocal(kLocalNewerWork, true);
	const auto stable = ReadWidgetOps(*tap);
	AddRow(
		result,
		(stable.markedSerial == 10)
			&& PressCounts(stable, 0, 1, 1, 1, 2)
			&& (stable.trustedClicks == 1),
		u"local: the played press's own release with a trusted click counts "
		"its click once, the renderer's click reaching the tap through the "
		"wrapper while that release is forwarded, and a release after that "
		"press ended counts elsewhere"_q,
		PressText(stable)
			+ u" trustedClicks="_q
			+ QString::number(stable.trustedClicks));

	MarkWidgetOpsEvents(*tap);
	PlayPress(*tap, kLocalNewerWork, 11);
	WidgetLocalPressDown(kLocalNewerWork, 11);
	auto third = TapWidgetRenderer(
		id,
		StubFactory{ std::make_shared<StubState>() });
	ReleaseLocal(kLocalNewerWork, true, &newer);
	const auto superseded = ReadWidgetOps(*tap);
	third = nullptr;
	AddRow(
		result,
		(superseded.markSuperseded == 3)
			&& PressCounts(superseded, 0, 1, 0, 0, 2)
			&& !superseded.trustedClicks,
		u"local: a generation selected between the played press's down and "
		"its release supersedes the mark: that release, its click and the "
		"trusted click count for no reading"_q,
		PressText(superseded)
			+ u" trustedClicks="_q
			+ QString::number(superseded.trustedClicks));

	auto later = LocalStart(id, kLocalLaterWork);
	const auto laterBuilt = wrapped(later);
	MarkWidgetOpsEvents(*tap);
	PlayPress(*tap, kLocalLaterWork, 12);
	WidgetLocalPressDown(kLocalLaterWork, 12);
	later.wrapper = nullptr;
	ReleaseLocal(kLocalLaterWork, true);
	const auto gone = ReadWidgetOps(*tap);
	auto pending = TapWidgetRenderer(
		id,
		kLocalPendingWork,
		StubFactory{ std::make_shared<StubState>() });
	MarkWidgetOpsEvents(*tap);
	PlayPress(*tap, kLocalPendingWork, 13);
	const auto selected = ReadWidgetOps(*tap);
	pending = nullptr;
	WidgetLocalPressDown(kLocalPendingWork, 13);
	ReleaseLocal(kLocalPendingWork, true);
	const auto dropped = ReadWidgetOps(*tap);
	older.wrapper = nullptr;
	newer.wrapper = nullptr;
	const auto final = ReadWidgetOps(*tap);
	AddRow(
		result,
		laterBuilt
			&& (gone.markedWork == kLocalLaterWork)
			&& (gone.markedSerial == 12)
			&& PressCounts(gone, 0, 1, 0, 0, 0)
			&& (selected.markedWork == kLocalPendingWork)
			&& (selected.markedSerial == 13)
			&& (PressSteps(dropped) == 0)
			&& TapFinal(final),
		u"local: a work's registration outlives its factory with the "
		"wrapper and ends with it, so a release after the wrapper is gone "
		"counts nowhere; a factory's registration binds from its selection, "
		"before any build, and ends with its last copy; the reading is "
		"final then"_q,
		PressText(gone)
			+ u" | "_q
			+ PressText(selected)
			+ u" | "_q
			+ PressText(dropped)
			+ u" | "_q
			+ ReadingText(final));
	DisarmWidgetOpsTap(id);
}

// The retirement of local cores through the product's WidgetCoreDetached(),
// which RendererCore::detach() calls on main right before it closes the
// host link, so nothing the work answers later reaches the wrapper, while
// the host still holds the wrapper and with it the registration (a final
// host sample keeps the host). Each generation's work is marked while it is
// the newest; a click counts only when it reached the host before the
// detach.
void CheckLocalRetirement(WidgetOpsSelfCheck &result) {
	const auto id = SelfCheckId(kLocalRetireIndex);
	const auto tap = ArmWidgetOpsTap(id);
	const auto wrapped = [](const LocalSide &side) {
		return side.wrapper
			&& side.state->built
			&& (side.wrapper.get() != side.state->built);
	};

	auto retired = LocalStart(id, kLocalRetiredWork);
	DispatchPress(kLocalRetiredWork, 1);
	WidgetLocalPressDown(kLocalRetiredWork, 1);
	MarkWidgetOpsEvents(*tap);
	ReleaseLocal(kLocalRetiredWork, true, &retired);
	PlayPress(*tap, kLocalRetiredWork, 2);
	WidgetLocalPressDown(kLocalRetiredWork, 2);
	WidgetCoreDetached(kLocalRetiredWork);
	ReleaseLocal(kLocalRetiredWork, true);
	const auto queued = ReadWidgetOps(*tap);
	const auto held = (queued.wrappersBuilt == 1)
		&& !queued.wrappersDestroyed;
	AddRow(
		result,
		wrapped(retired)
			&& held
			&& (queued.markedWork == kLocalRetiredWork)
			&& (queued.markedSerial == 2)
			&& PressCounts(queued, 0, 1, 0, 0, 4)
			&& queued.markDetached
			&& (queued.trustedClicks == 1),
		u"local retirement: the played press's up queued across its core's "
		"detach, its seam still finding the registration the held wrapper "
		"keeps, counts elsewhere with its click reaching no host, beside an "
		"earlier press's trusted click after the mark: the detach revoked "
		"the mark, and no click counts"_q,
		u"wrapped=%1 held=%2 %3 trustedClicks=%4"_q.arg(
			Flag(wrapped(retired)),
			Flag(held),
			PressText(queued),
			QString::number(queued.trustedClicks)));

	auto lookup = LocalStart(id, kLocalHeldWork);
	MarkWidgetOpsEvents(*tap);
	PlayPress(*tap, kLocalHeldWork, 3);
	WidgetLocalPressDown(kLocalHeldWork, 3);
	const auto selection = ReadWidgetOpsLatest(*tap).generation;
	WidgetCoreDetached(kLocalHeldWork);
	tap->localPressReleased(selection, kLocalHeldWork, true);
	SendLocalClick(lookup);
	tap->localPressForwarded(kLocalHeldWork);
	const auto looked = ReadWidgetOps(*tap);
	auto closed = LocalStart(id, kLocalClosedWork);
	MarkWidgetOpsEvents(*tap);
	PlayPress(*tap, kLocalClosedWork, 4);
	WidgetLocalPressDown(kLocalClosedWork, 4);
	auto out = std::vector<twidget::Json>();
	if (auto parsed = twidget::Json::parse(kTrustedClick)) {
		out.push_back(std::move(*parsed));
	}
	WidgetLocalPressReleased(kLocalClosedWork, out);
	WidgetCoreDetached(kLocalClosedWork);
	WidgetLocalPressForwarded(kLocalClosedWork);
	const auto taken = ReadWidgetOps(*tap);
	AddRow(
		result,
		wrapped(lookup)
			&& wrapped(closed)
			&& (selection == 2)
			&& (looked.markedSerial == 3)
			&& PressCounts(looked, 0, 1, 0, 0, 2)
			&& looked.markDetached
			&& (looked.trustedClicks == 1)
			&& (taken.markedSerial == 4)
			&& PressCounts(taken, 0, 1, 1, 0, 0)
			&& taken.markDetached
			&& !out.empty(),
		u"local retirement: a release that a lookup made before the detach "
		"brings to the tap after it, with a trusted click handed the "
		"wrapper after the detach, counts no click; a played release taken "
		"before the detach whose click had not reached the host when the "
		"link closed counts its up and no click"_q,
		u"selection=%1 %2 trustedClicks=%3 | %4"_q.arg(
			QString::number(selection),
			PressText(looked),
			QString::number(looked.trustedClicks),
			PressText(taken)));

	auto kept = LocalStart(id, kLocalKeptWork);
	MarkWidgetOpsEvents(*tap);
	PlayPress(*tap, kLocalKeptWork, 5);
	WidgetLocalPressDown(kLocalKeptWork, 5);
	WidgetCoreDetached(kLocalRetiredWork);
	WidgetCoreDetached(kLocalOlderWork);
	const auto other = ReadWidgetOps(*tap);
	ReleaseLocal(kLocalKeptWork, true, &kept);
	WidgetCoreDetached(kLocalKeptWork);
	const auto completed = ReadWidgetOps(*tap);
	AddRow(
		result,
		wrapped(kept)
			&& (other.markedSerial == 5)
			&& PressCounts(other, 0, 1, 0, 0, 0)
			&& !other.markDetached
			&& PressCounts(completed, 0, 1, 1, 1, 0)
			&& !completed.markDetached
			&& (completed.trustedClicks == 1),
		u"local retirement: the detach of an older generation's work and of "
		"a work no tap registered changes nothing, and the played click "
		"that reached the host before its core's detach stays counted"_q,
		PressText(other) + u" | "_q + PressText(completed));

	auto unforwarded = LocalStart(id, kLocalUnforwardedWork);
	const auto unforwardedBuilt = wrapped(unforwarded);
	MarkWidgetOpsEvents(*tap);
	PlayPress(*tap, kLocalUnforwardedWork, 6);
	WidgetLocalPressDown(kLocalUnforwardedWork, 6);
	ReleaseLocal(kLocalUnforwardedWork, true);
	SendLocalClick(unforwarded);
	const auto late = ReadWidgetOps(*tap);
	retired.wrapper = nullptr;
	lookup.wrapper = nullptr;
	closed.wrapper = nullptr;
	kept.wrapper = nullptr;
	unforwarded.wrapper = nullptr;
	const auto final = ReadWidgetOps(*tap);
	AddRow(
		result,
		unforwardedBuilt
			&& PressCounts(late, 0, 1, 1, 0, 0)
			&& !late.markDetached
			&& (late.trustedClicks == 1)
			&& PressCounts(final, 0, 1, 1, 0, 0)
			&& TapFinal(final),
		u"local: a played release whose click reached no wrapper while it "
		"was forwarded counts no click, a trusted click handed the wrapper "
		"after that included; the reading is final once the wrappers are "
		"gone"_q,
		PressText(late)
			+ u" trustedClicks="_q
			+ QString::number(late.trustedClicks)
			+ u" | "_q
			+ ReadingText(final));
	DisarmWidgetOpsTap(id);
}

} // namespace
} // namespace Test::WidgetOps

namespace Test {

using namespace WidgetOps;

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
	CheckHosted(result);
	CheckLocalPresses(result);
	CheckLocalRetirement(result);
	CheckBudget(result);
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

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
