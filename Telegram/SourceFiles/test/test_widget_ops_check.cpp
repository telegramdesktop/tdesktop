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
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace Test::WidgetOps {
namespace {

namespace HostedWire = Tgw::HostedWire;

// The self-check's inputs and its independently written expectations.

constexpr auto kTheme = "dark";
constexpr auto kWidth = 240.5;
constexpr auto kNextTheme = "light";

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
constexpr auto kAckQuery = R"json({"type":"query","qid":-1,)json"
	R"json("query":{"q":"viewport"}})json";
constexpr auto kAckResult = R"json({"type":"result","qid":-1,)json"
	R"json("result":{"innerWidth":512.25,"innerHeight":300,"dpr":2}})json";
constexpr auto kRemovalBatch = R"json({"type":"ops","ops":[)json"
	R"json({"op":"rm","id":5}]})json";
constexpr auto kFrozen = R"json({"type":"frozen",)json"
	R"json("reason":"Widget stopped","fatal":true})json";
constexpr auto kTrustedClick = R"json({"type":"event",)json"
	R"json("ev":{"type":"click","targetId":2,"detail":1},)json"
	R"json("trusted":true})json";
constexpr auto kViolation = R"json({"type":"violation",)json"
	R"json("detail":"image refused: https://127.0.0.1:9999/refused.png"})json";
constexpr auto kPaintReport = R"json({"type":"limit",)json"
	R"json("code":"paint-limit","count":17})json";
constexpr auto kThemeMessage = R"json({"type":"theme","theme":{)json"
	R"json("colorScheme":"light","params":{"bg_color":"#ffffff",)json"
	R"json("text_color":"#000000"},"button":{"radius":"8px"},)json"
	R"json("text":{"fontSize":14}}})json";

// The conformance probe check's replacement leg and its expected values.

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

constexpr auto kOldDoneValue = R"json({"id":"probe/old","status":"done"})json";
constexpr auto kNewRunningValue
	= R"json({"id":"probe/new","status":"running"})json";
constexpr auto kHtmlId = 2;
constexpr auto kReadoptHtmlId = 6;

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

// The characters a row's details show of a result or stress value.
constexpr auto kProbeTextChars = 400;

// Peer 0 belongs to no history, so no real message of any session has such
// an id. The session values only tell the self-check's own ids apart.
constexpr auto kSelfCheckSession = uint64(1);
constexpr auto kOtherSession = uint64(2);

// The session leg's id, in both sessions, and the stress leg's.
constexpr auto kSessionsIndex = 4;
constexpr auto kStressIndex = 5;

// The hosted leg's ids, then the budget leg's, then the hosted
// completion-order leg's two messages.
constexpr auto kHostedIndex = 6;
constexpr auto kHostedDroppedIndex = 7;
constexpr auto kBudgetIndex = 8;
constexpr auto kHostedOlderIndex = 9;
constexpr auto kHostedNewerIndex = 10;

// The scan-gap leg's id.
constexpr auto kScanGapsIndex = 12;

// The hosted leg's message with two generations on one tap, and its
// messages for theme applications underway, hosts retired mid-probe and
// themes selected while a theme job runs.
constexpr auto kHostedGenerationsIndex = 13;
constexpr auto kHostedThemeIndex = 14;
constexpr auto kHostedRetireIndex = 15;
constexpr auto kHostedSelectedIndex = 16;

// The hosted finals leg's message, and its eviction leg's.
constexpr auto kHostedFinalsIndex = 19;
constexpr auto kHostedEvictedIndex = 20;

// The conformance probe check's replacement leg's id, and the first of its
// fixture cases' ids, one per case.
constexpr auto kProbeReplacementIndex = 11;
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
		{ u"hostedSelected"_q, reading.hostedSelected },
		{ u"hostedFinal"_q, reading.hostedFinal },
		{ u"desktopCharges"_q, reading.desktopCharges },
		{ u"sparedRuns"_q, reading.sparedRuns },
		{ u"unaffectedRuns"_q, reading.unaffectedRuns },
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
		.name = SelfCheckRowName(name),
		.details = details,
		.passed = passed,
	});
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

// The shape of probe-fixture.json: |cases| holds the cases validated in
// order, up to the first one that fails.
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

// The `ops` array of |batch|, a compact batch as WidgetHost wraps one: what
// a hosted host hands its onRenderOps.
[[nodiscard]] std::string CleanOps(std::string_view batch) {
	return std::string(batch.substr(
		kOpsPrefix.size(),
		batch.size() - kOpsPrefix.size() - kOpsSuffix.size()));
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
// mirror and a repeated build, then its deaths (CheckHostedDeaths()).
void ExerciseHosted(
		WidgetOpsSelfCheck &result,
		WidgetOpsTap &tap,
		twidget::WidgetHost &host,
		const HostedProduct &product) {
	const auto clean = CleanOps(kControlBatch);
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

	for (const auto &row : CheckHostedDeaths(tap, host, product.deaths)) {
		AddRow(result, row.passed, row.name, row.details);
	}
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

// One tapped hosted host of a leg: its message's tap, the product's
// observers, the host and its selection's install, each step explicit so a
// leg can read the selection before the host is built or installed.
struct HostedSide {
	std::shared_ptr<WidgetOpsTap> tap;
	std::shared_ptr<HostedProduct> product;
	std::shared_ptr<twidget::WidgetHost> host;
	Fn<void(const std::shared_ptr<twidget::WidgetHost> &host)> install;
};

// Arms |id|, which returns its tap when it is armed already, and selects
// its next generation at once, as a construction for it would.
[[nodiscard]] HostedSide SelectHostedSide(GlobalMsgId id, uint64 work) {
	auto result = HostedSide{
		.tap = ArmWidgetOpsTap(id),
		.product = std::make_shared<HostedProduct>(),
	};
	result.install = TapHostedWidget(id, work, AppliedTheme());
	return result;
}

// Builds the side's host, uninstalled: usable when it was built and the
// selection gave a tap to install on it.
[[nodiscard]] bool BuildHostedSide(HostedSide &side) {
	try {
		side.host = HostedStub(side.product);
	} catch (const std::exception &) {
	}
	return side.install && side.host;
}

// |batch|'s `ops` array, as the validated batch a usable side's host hands
// its onRenderOps.
void SendOps(const HostedSide &side, std::string_view batch) {
	if (side.install && side.host) {
		side.host->onRenderOps(CleanOps(batch));
	}
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
		HostedSide &older,
		HostedSide &newer,
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
	auto older = SelectHostedSide(SelfCheckId(kHostedOlderIndex), kOlderWork);
	const auto olderBuilt = BuildHostedSide(older);
	auto newer = SelectHostedSide(SelfCheckId(kHostedNewerIndex), kNewerWork);
	const auto newerBuilt = BuildHostedSide(newer);
	const auto order = olderFirst
		? u"older first"_q
		: u"newer first, the older late"_q;
	const auto built = olderBuilt && newerBuilt;
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

// A hosted tap through its whole life, its final reading returned as the
// self-check's control, and one dropped uninstalled.
[[nodiscard]] WidgetOpsReading CheckHosted(WidgetOpsSelfCheck &result) {
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
	auto control = WidgetOpsReading();
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
				&& !WidgetTapFinished(released),
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
		control = destroyed;
		MarkWidgetOpsEvents(*tap);
		ObserveClick(kHostedWork, 11, 11);
		const auto unregistered = ReadWidgetOps(*tap);
		AddRow(
			result,
			(destroyed.wrappersDestroyed == 1)
				&& WidgetTapFinished(destroyed)
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
			&& WidgetTapFinished(after),
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
	return control;
}

// One FullMsgId in two sessions is two messages: arming one taps it alone,
// arming both gives two taps, and disarming one leaves the other armed.
void CheckSessions(WidgetOpsSelfCheck &result) {
	const auto first = SelfCheckId(kSessionsIndex, kSelfCheckSession);
	const auto second = SelfCheckId(kSessionsIndex, kOtherSession);

	const auto firstTap = ArmWidgetOpsTap(first);
	auto other = TapHostedWidget(second, 0, AppliedTheme());
	const auto otherTapped = bool(other);
	other = nullptr;
	const auto onlyFirst = ReadWidgetOps(*firstTap);
	AddRow(
		result,
		!otherTapped
			&& (onlyFirst.factoriesSelected == 0)
			&& (onlyFirst.wrappersBuilt == 0),
		u"hosted: another session's message with the same FullMsgId gets "
		"the product factory itself while only this session's is armed"_q,
		u"tap=%1 armed{%2}"_q.arg(
			Flag(otherTapped),
			ReadingText(onlyFirst)));

	auto firstSide = SelectHostedSide(first, 0);
	auto secondSide = SelectHostedSide(second, 0);
	const auto firstBuilt = BuildHostedSide(firstSide);
	const auto secondBuilt = BuildHostedSide(secondSide);
	if (firstBuilt) {
		firstSide.install(firstSide.host);
	}
	if (secondBuilt) {
		secondSide.install(secondSide.host);
	}
	SendOps(firstSide, kControlBatch);
	SendOps(secondSide, kRemovalBatch);
	const auto secondTap = secondSide.tap;
	const auto firstBoth = ReadWidgetOps(*firstTap);
	const auto secondBoth = ReadWidgetOps(*secondTap);
	const auto separate = secondTap && (secondTap != firstTap);
	AddRow(
		result,
		separate
			&& firstBuilt
			&& secondBuilt
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
		u"hosted: with both sessions' messages armed, each has its own tap "
		"and its own reading"_q,
		u"built=%1,%2 separateTaps=%3 first{%4} second{%5}"_q.arg(
			Flag(firstBuilt),
			Flag(secondBuilt),
			Flag(separate),
			ReadingText(firstBoth),
			ReadingText(secondBoth)));

	DisarmWidgetOpsTap(first);
	auto firstLater = TapHostedWidget(first, 0, AppliedTheme());
	auto secondLater = TapHostedWidget(second, 0, AppliedTheme());
	const auto firstUntouched = !firstLater;
	const auto secondWrapping = bool(secondLater);
	SendOps(secondSide, kRemovalBatch);
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
		u"hosted: disarming one session's message leaves the other's armed "
		"and recording"_q,
		u"firstUntouched=%1 secondWrapping=%2 first{%3} second{%4}"_q.arg(
			Flag(firstUntouched),
			Flag(secondWrapping),
			ReadingText(firstAfter),
			ReadingText(secondAfter)));

	firstLater = nullptr;
	secondLater = nullptr;
	firstSide.host = nullptr;
	secondSide.host = nullptr;
	firstSide.install = nullptr;
	secondSide.install = nullptr;
	DisarmWidgetOpsTap(second);
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
// values, as a stress runner reads them for one level. Every batch reaches
// the tap as its host's validated batch.
void CheckStress(WidgetOpsSelfCheck &result) {
	const auto id = SelfCheckId(kStressIndex);
	auto older = SelectHostedSide(id, 0);
	const auto tap = older.tap;
	const auto olderBuilt = BuildHostedSide(older);
	if (olderBuilt) {
		older.install(older.host);
	}
	SendOps(older, kStressOldAdopt);
	const auto old = ReadWidgetOpsLatest(*tap);

	auto newer = SelectHostedSide(id, 0);
	const auto selected = ReadWidgetOpsLatest(*tap);
	const auto newerBuilt = BuildHostedSide(newer);
	if (newerBuilt) {
		newer.install(newer.host);
	}
	const auto from = crl::profile();
	SendOps(newer, kStressAdopt);
	SendOps(newer, kStressThree);
	SendOps(newer, kStressFive);
	const auto till = crl::profile();
	const auto own = ReadWidgetOpsLatest(*tap);
	const auto ownReading = ReadWidgetOps(*tap);

	SendOps(older, kStressLate);
	const auto late = ReadWidgetOpsLatest(*tap);
	const auto lateReading = ReadWidgetOps(*tap);
	const auto &olderOps = older.product->ops;
	const auto lateForwarded = int(std::count(
		olderOps.begin(),
		olderOps.end(),
		CleanOps(kStressLate)));

	SendOps(newer, kStressPlain);
	const auto plain = ReadWidgetOpsLatest(*tap);

	const auto bulk = kWidgetStressValuesLimit - 2;
	for (auto i = 0; i != bulk; ++i) {
		SendOps(newer, StressBulkBatch(i));
	}
	const auto filledTill = crl::profile();
	const auto filled = ReadWidgetOpsLatest(*tap);
	const auto lastKept = "bulk/" + std::to_string(bulk - 2);

	SendOps(newer, kStressReadopt);
	const auto readopted = ReadWidgetOpsLatest(*tap);

	newer.host = nullptr;
	older.host = nullptr;
	newer.install = nullptr;
	older.install = nullptr;
	DisarmWidgetOpsTap(id);

	const auto ownValues = std::vector<std::string>{
		kStressSetupValue,
		kStressStartValue,
		kStressHalfValue,
	};
	AddRow(
		result,
		olderBuilt
			&& newerBuilt
			&& (old.generation == 1)
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
		u"hosted: stress: a newer host counts its own 3 batches, 9 ops and "
		"a largest of 5 and keeps its three data-stress values in order, "
		"receipts non-decreasing, its conformance reading untouched"_q,
		u"built=%1,%2 old{%3} selected{%4} own%5 till=%6"_q.arg(
			Flag(olderBuilt),
			Flag(newerBuilt),
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
		u"hosted: stress: an older wrapper's late 6-op batch with a value on "
		"the same id is forwarded and scanned, but its value and counts "
		"never reach the newer host's reading"_q,
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
		u"hosted: stress: a batch without a value counts with its ops"_q,
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
		u"hosted: stress: a value past the limit counts as dropped and never "
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
		u"hosted: stress: a later adoption clears the values and the drops, "
		"a value on the earlier id is ignored, and the counts go on"_q,
		StressText(readopted, from));
}

// The `ops` array of a validated batch of |values| JSON values in all, as
// WidgetHost wraps it (kOpsPrefix and kOpsSuffix): one `anim` op whose
// `pad` zeros follow the 10 values of the wrapping and of the op.
[[nodiscard]] std::string PaddedOps(std::size_t values) {
	auto result = std::string(R"json([{"op":"anim","pad":[)json");
	const auto used = std::size_t(10);
	for (auto i = used; i < values; ++i) {
		result += (i == used) ? "0" : ",0";
	}
	return result + "]}]";
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
		u"hosted: a batch with 100 KiB attribute and style names counts all "
		"its leaks, keeps its first 64 examples within 64 KiB and cuts each "
		"place to 240 bytes"_q,
		u"leaks=%1 examples=%2 bytes=%3 longest=%4 first=%5"_q.arg(
			QString::number(after.leaks - before.leaks),
			QString::number(int(after.leakExamples.size())),
			QString::number(qint64(bytes)),
			QString::number(qint64(longest)),
			QString::number(qint64(first.size()))));
}

// How a scan gap begins for an input past the budget: the route that read
// it and its bytes, as BudgetGap() in test_widget_ops.cpp names one.
[[nodiscard]] std::string BudgetGapStart(
		std::string_view route,
		std::string_view input) {
	return std::string(route)
		+ " of "
		+ std::to_string(input.size())
		+ " bytes ";
}

// On each route the hosted tap has, an input one value past the scan
// budget, then one exactly at it: a validated batch through onRenderOps,
// the served host's `anim` message as a mirrored served-host record and a
// renderer `violation` as a mirrored renderer record, the same in both
// builds. Those past it are gaps that are never parsed, each named by its
// route and bytes in the order sent; those at it are read. Without a Debug
// helper the records pass on unread. Then a batch with long names
// (CheckLongNames()).
void CheckBudget(WidgetOpsSelfCheck &result) {
	const auto id = SelfCheckId(kBudgetIndex);
	auto side = SelectHostedSide(id, 0);
	const auto built = BuildHostedSide(side);
	if (built) {
		side.install(side.host);
	}
	const auto overOps = PaddedOps(kMaxScanValues + 1);
	const auto atOps = PaddedOps(kMaxScanValues);
	const auto wrapped = [](const std::string &clean) {
		return std::string(kOpsPrefix) + clean + std::string(kOpsSuffix);
	};
	const auto overHost = PaddedMessage("anim", {}, kMaxScanValues + 1);
	const auto atHost = PaddedMessage("anim", {}, kMaxScanValues);
	const auto overRenderer = PaddedMessage(
		"violation",
		"over",
		kMaxScanValues + 1);
	const auto atRenderer = PaddedMessage("violation", "at", kMaxScanValues);
	const auto apart = [](const std::string &at, const std::string &over) {
		return WithinScanBudget(at) && !WithinScanBudget(over);
	};
	const auto at = apart(wrapped(atOps), wrapped(overOps))
		&& apart(atHost, overHost)
		&& apart(atRenderer, overRenderer);
	const auto records = std::vector<std::string>{
		MirroredRecord(HostedWire::kMirroredHost, overHost),
		MirroredRecord(HostedWire::kMirroredHost, atHost),
		MirroredRecord(HostedWire::kMirroredRenderer, overRenderer),
		MirroredRecord(HostedWire::kMirroredRenderer, atRenderer),
	};
	if (built) {
		side.host->onRenderOps(overOps);
		side.host->onRenderOps(atOps);
		for (const auto &record : records) {
			side.host->onPlatformMessage(record);
		}
	}
	const auto reading = ReadWidgetOps(*side.tap);
	const auto &platform = side.product->platform;
	const auto forwarded = kHelperMirrors
		? platform.empty()
		: (platform == records);
	const auto passed = int(platform.size());
	SendOps(side, LongNameBatch());
	const auto sunk = ReadWidgetOps(*side.tap);
	side.host = nullptr;
	side.install = nullptr;
	DisarmWidgetOpsTap(id);
	auto gaps = std::vector<std::string>{
		BudgetGapStart("host message", wrapped(overOps)),
	};
	if (kHelperMirrors) {
		gaps.push_back(BudgetGapStart("host message", overHost));
		gaps.push_back(BudgetGapStart("renderer message", overRenderer));
	}
	const auto named = std::ranges::equal(
		reading.gapExamples,
		gaps,
		[](const std::string &example, const std::string &start) {
			return example.starts_with(start);
		});
	const auto read = kHelperMirrors
		? ((reading.hostMessages == 4)
			&& (reading.violations == 1)
			&& (reading.violationDetails
				== std::vector<std::string>{ "at" }))
		: ((reading.hostMessages == 2)
			&& (reading.violations == 0)
			&& reading.violationDetails.empty());
	AddRow(
		result,
		built
			&& at
			&& named
			&& (reading.scanGaps == int(gaps.size()))
			&& (reading.batches == 1)
			&& (reading.ops == 1)
			&& read
			&& forwarded,
		kHelperMirrors
			? u"hosted: a host and a renderer message past the scan budget "
				"are gaps, never parsed, and ones at it are read"_q
			: u"hosted: without a Debug helper a validated batch past the "
				"scan budget is a gap, never parsed, one at it is read, and "
				"the mirrored host and renderer messages pass on unread"_q,
		u"built=%1 counted=%2 named=%3 passed=%4 %5 gapExamples=%6 "
			"details=%7"_q.arg(
			Flag(built),
			Flag(at),
			Flag(named),
			QString::number(passed),
			ReadingText(reading),
			ExamplesText(reading.gapExamples),
			ExamplesText(reading.violationDetails)));
	CheckLongNames(result, reading, sunk);
}

// Two validated batches the tap cannot read, the same in both builds,
// through onRenderOps: wrapped as WidgetHost wraps a batch, an array cut
// off is an unparsable message and an object in its place an `ops` message
// without an array. Each is a scan gap named by its whole message, never a
// batch, and both reach the stand-in unchanged, in order.
void CheckScanGaps(WidgetOpsSelfCheck &result) {
	const auto id = SelfCheckId(kScanGapsIndex);
	auto side = SelectHostedSide(id, 0);
	const auto built = BuildHostedSide(side);
	// An array cut off, then an object in an array's place.
	const auto inputs = std::vector<std::string>{
		"[",
		R"json({"op":"rm","id":5})json",
	};
	if (built) {
		side.install(side.host);
		for (const auto &input : inputs) {
			side.host->onRenderOps(input);
		}
	}
	const auto reading = ReadWidgetOps(*side.tap);
	const auto &forwarded = side.product->ops;
	side.host = nullptr;
	side.install = nullptr;
	DisarmWidgetOpsTap(id);
	const auto gaps = std::vector<std::string>{
		R"json(unparsable host message: {"type":"ops","ops":[})json",
		R"json(ops message without an ops array: )json"
			R"json({"type":"ops","ops":{"op":"rm","id":5}})json",
	};
	AddRow(
		result,
		built
			&& (forwarded == inputs)
			&& (reading.hostMessages == 2)
			&& (reading.scanGaps == 2)
			&& (reading.batches == 0)
			&& (reading.ops == 0)
			&& (reading.gapExamples == gaps),
		u"hosted: an unparsable message and an ops message without an "
		"array are scan gaps, forwarded unchanged"_q,
		u"sideBuilt=%1 forwarded=%2 %3 gapExamples=%4"_q.arg(
			Flag(built),
			ExamplesText(forwarded),
			ReadingText(reading),
			ExamplesText(reading.gapExamples)));
}

// How probe-fixture.json writes an `ops` message around its array.
constexpr auto kFixtureOpsPrefix = std::string_view(
	R"json({"type": "ops", "ops": )json");
constexpr auto kFixtureOpsSuffix = std::string_view("}");

// The bytes of |message| inside the fixture's `ops` envelope, as the
// fixture wrote them; empty when it is not so enveloped.
[[nodiscard]] std::string_view FixtureOpsArray(std::string_view message) {
	const auto envelope = kFixtureOpsPrefix.size() + kFixtureOpsSuffix.size();
	if ((message.size() <= envelope)
		|| !message.starts_with(kFixtureOpsPrefix)
		|| !message.ends_with(kFixtureOpsSuffix)) {
		return {};
	}
	return message.substr(kFixtureOpsPrefix.size(), message.size() - envelope);
}

// A fixture message as the helper's tap splits a served host's messages,
// by its type as Qt's own parser reads it: an `ops` message's array, the
// fixture's own bytes inside its envelope, for a hosted host's onRenderOps,
// once Qt reads those bytes as the very array the message holds; anything
// else as the served-host record for its onPlatformMessage, once the
// record decodes to the message's own bytes. Neither, with the reason,
// when that check fails.
struct ProbeRoute {
	std::string ops;
	std::string record;
	QString error;
};

[[nodiscard]] ProbeRoute RouteProbeMessage(const std::string &message) {
	const auto document = QJsonDocument::fromJson(
		QByteArray::fromStdString(message));
	const auto object = document.object();
	const auto ops = object.value(u"ops"_q);
	if (document.isObject()
		&& (object.value(u"type"_q).toString() == u"ops"_q)
		&& ops.isArray()) {
		const auto array = FixtureOpsArray(message);
		const auto sliced = array.empty()
			? QJsonDocument()
			: QJsonDocument::fromJson(
				QByteArray(array.data(), qsizetype(array.size())));
		if (!sliced.isArray() || (sliced.array() != ops.toArray())) {
			return {
				.error = u"an ops message whose bytes inside the fixture's "
					"envelope are not its ops array"_q,
			};
		}
		return { .ops = std::string(array) };
	}
	auto record = MirroredRecord(HostedWire::kMirroredHost, message);
	const auto decoded = HostedWire::DecodeMirrored(record);
	if (!decoded
		|| (decoded->way != HostedWire::kMirroredHost)
		|| (decoded->text != message)) {
		return {
			.error = u"a served-host record that does not decode to the "
				"message"_q,
		};
	}
	return { .record = std::move(record) };
}

// The tap's reading and the sizes of the stand-in's two lists, before or
// after one fixture message.
struct ProbeState {
	WidgetOpsReading reading;
	std::size_t ops = 0;
	std::size_t platform = 0;
};

[[nodiscard]] ProbeState ReadProbeState(
		const WidgetOpsTap &tap,
		const HostedProduct &product) {
	return {
		.reading = ReadWidgetOps(tap),
		.ops = product.ops.size(),
		.platform = product.platform.size(),
	};
}

// Empty when one routed fixture message changed just what its route
// should: an `ops` array one host message and one batch, no gap, and
// |array|, the fixture's own bytes, onto the stand-in's `ops`; a
// served-host record, in Debug, one host message, no batch and no gap and
// neither of the stand-in's lists (the record consumed), and without a
// Debug helper the record onto the stand-in's `platform`, unread, the
// reading unchanged. Else what it changed.
[[nodiscard]] QString ProbeStepError(
		const ProbeRoute &route,
		std::string_view array,
		const ProbeState &before,
		const ProbeState &after,
		const HostedProduct &product) {
	const auto hostMessages = after.reading.hostMessages
		- before.reading.hostMessages;
	const auto batches = after.reading.batches - before.reading.batches;
	const auto gaps = after.reading.scanGaps - before.reading.scanGaps;
	const auto opsAdded = int(after.ops - before.ops);
	const auto platformAdded = int(after.platform - before.platform);
	const auto kept = (ReadingText(after.reading)
		== ReadingText(before.reading));
	const auto batch = !route.ops.empty();
	const auto exact = batch
		? ((opsAdded == 1) && (product.ops.back() == array))
		: (kHelperMirrors
			|| ((platformAdded == 1)
				&& (product.platform.back() == route.record)));
	const auto counted = batch
		? ((hostMessages == 1)
			&& (batches == 1)
			&& (gaps == 0)
			&& (opsAdded == 1)
			&& (platformAdded == 0))
		: kHelperMirrors
		? ((hostMessages == 1)
			&& (batches == 0)
			&& (gaps == 0)
			&& (opsAdded == 0)
			&& (platformAdded == 0))
		: (kept && (opsAdded == 0) && (platformAdded == 1));
	if (exact && counted) {
		return QString();
	}
	return u"%1 hostMessages+%2 batches+%3 scanGaps+%4 productOps+%5 "
		"productPlatform+%6 exact=%7 readingKept=%8"_q.arg(
			batch ? u"ops"_q : u"record"_q,
			QString::number(hostMessages),
			QString::number(batches),
			QString::number(gaps),
			QString::number(opsAdded),
			QString::number(platformAdded),
			Flag(exact),
			Flag(kept));
}

// Empty when |seen| is |expected| entry for entry, else its first
// difference, by place and byte counts alone, since a mirrored record is
// not text.
[[nodiscard]] QString SequenceDifference(
		const std::vector<std::string> &seen,
		const std::vector<std::string> &expected) {
	const auto count = std::max(seen.size(), expected.size());
	for (auto i = std::size_t(0); i != count; ++i) {
		const auto index = QString::number(qint64(i));
		if (i >= seen.size()) {
			return u"#%1 missing, %2 bytes"_q.arg(
				index,
				QString::number(qint64(expected[i].size())));
		} else if (i >= expected.size()) {
			return u"#%1 extra, %2 bytes"_q.arg(
				index,
				QString::number(qint64(seen[i].size())));
		} else if (seen[i] != expected[i]) {
			return u"#%1 got %2 bytes, expected %3 other bytes"_q.arg(
				index,
				QString::number(qint64(seen[i].size())),
				QString::number(qint64(expected[i].size())));
		}
	}
	return QString();
}

// Reads ReadWidgetOpsLatest() after every message, as a runner polls it.
// Every message reaches the hosted host's observers as RouteProbeMessage()
// splits it, and each must change just what its route reads
// (ProbeStepError()); the arrays the stand-in must get are the fixture's
// own bytes.
void CheckProbeCase(
		std::vector<WidgetOpsSelfCheckRow> &rows,
		const ProbeCase &probe,
		int index) {
	const auto id = SelfCheckId(kProbeCaseIndex + index);
	auto side = SelectHostedSide(id, 0);
	const auto tap = side.tap;
	const auto built = BuildHostedSide(side);
	if (built) {
		side.install(side.host);
	}
	const auto &product = *side.product;
	auto expectedOps = std::vector<std::string>();
	auto sentRecords = std::vector<std::string>();
	auto routed = 0;
	auto step = QString();
	const auto note = [&](int i, const QString &error) {
		if (step.isEmpty() && !error.isEmpty()) {
			step = u"#%1 %2"_q.arg(QString::number(i), error);
		}
	};
	auto awaitingTargets = QJsonValue(QJsonValue::Undefined);
	auto serial = ReadWidgetOpsLatest(*tap).serial;
	for (auto i = 0; i != int(probe.messages.size()); ++i) {
		const auto &message = probe.messages[i];
		const auto route = RouteProbeMessage(message);
		if (built && !route.error.isEmpty()) {
			note(i, route.error);
		} else if (built) {
			const auto array = FixtureOpsArray(message);
			const auto before = ReadProbeState(*tap, product);
			if (!route.ops.empty()) {
				expectedOps.emplace_back(array);
				side.host->onRenderOps(route.ops);
			} else {
				sentRecords.push_back(route.record);
				side.host->onPlatformMessage(route.record);
			}
			++routed;
			const auto after = ReadProbeState(*tap, product);
			note(i, ProbeStepError(route, array, before, after, product));
		}
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
	const auto reading = ReadWidgetOps(*tap);
	const auto messages = int(probe.messages.size());
	const auto read = int(expectedOps.size())
		+ (kHelperMirrors ? int(sentRecords.size()) : 0);
	const auto opsDifference = SequenceDifference(product.ops, expectedOps);
	const auto platformDifference = SequenceDifference(
		product.platform,
		kHelperMirrors ? std::vector<std::string>() : sentRecords);
	side.host = nullptr;
	side.install = nullptr;
	DisarmWidgetOpsTap(id);

	const auto label = u"hosted: case %1 (%2): "_q.arg(
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
		built
			&& (routed == messages)
			&& step.isEmpty()
			&& opsDifference.isEmpty()
			&& platformDifference.isEmpty()
			&& (reading.hostMessages == read),
		label + u"every message forwarded once with its exact bytes"_q,
		u"built=%1 messages=%2 ops=%3 records=%4 hostMessages=%5 "
			"expected=%6 opsDifference=[%7] platformDifference=[%8] "
			"step=[%9]"_q.arg(
			Flag(built),
			QString::number(messages),
			QString::number(int(expectedOps.size())),
			QString::number(int(sentRecords.size())),
			QString::number(reading.hostMessages),
			QString::number(read),
			opsDifference,
			platformDifference,
			step));
}

// Both hosts adopt <html> under one id, as fresh sandboxes of one widget do,
// so a retiring host's late value would land on the newer one if merged.
// Each host's build is mirrored right after its install, the older's in
// the other scheme; the older host's late setTheme() and theme message come
// as the Debug helper mirrors them, and pass on unread without one.
void CheckProbeReplacement(std::vector<WidgetOpsSelfCheckRow> &rows) {
	const auto id = SelfCheckId(kProbeReplacementIndex);
	auto older = SelectHostedSide(id, 0);
	const auto tap = older.tap;
	const auto olderBuilt = BuildHostedSide(older);
	const auto olderRecords = std::vector<std::string>{
		MirroredRecord(HostedWire::kMirroredBuilt, kNextTheme),
		MirroredRecord(HostedWire::kMirroredTheme, kNextTheme),
		MirroredRecord(HostedWire::kMirroredHost, kThemeMessage),
	};
	if (olderBuilt) {
		older.install(older.host);
		older.host->onPlatformMessage(olderRecords[0]);
	}
	SendOps(older, kOldAdopt);
	SendOps(older, kOldDone);
	const auto before = ReadWidgetOpsLatest(*tap);

	auto newer = SelectHostedSide(id, 0);
	const auto selected = ReadWidgetOpsLatest(*tap);
	SendOps(older, kOldAdopt);
	const auto newerBuilt = BuildHostedSide(newer);
	if (newerBuilt) {
		newer.install(newer.host);
		newer.host->onPlatformMessage(
			MirroredRecord(HostedWire::kMirroredBuilt, kTheme));
	}
	SendOps(newer, kNewAdopt);
	SendOps(newer, kNewRunning);
	const auto own = ReadWidgetOpsLatest(*tap);

	SendOps(older, kOldDone);
	SendOps(older, kControlBatch);
	if (olderBuilt) {
		older.host->onPlatformMessage(olderRecords[1]);
		older.host->onPlatformMessage(olderRecords[2]);
	}
	const auto late = ReadWidgetOpsLatest(*tap);
	const auto lateReading = ReadWidgetOps(*tap);
	const auto &olderProduct = *older.product;
	const auto forwarded = [&](const char *batch) {
		return int(std::count(
			olderProduct.ops.begin(),
			olderProduct.ops.end(),
			CleanOps(batch)));
	};
	const auto passedOn = kHelperMirrors
		? std::vector<std::string>()
		: olderRecords;
	const auto oldForwarded = (forwarded(kOldAdopt) == 2)
		&& (forwarded(kOldDone) == 2)
		&& (forwarded(kControlBatch) == 1)
		&& (olderProduct.platform == passedOn);

	SendOps(newer, kReadopt);
	const auto readopted = ReadWidgetOpsLatest(*tap);

	newer.host = nullptr;
	older.host = nullptr;
	newer.install = nullptr;
	older.install = nullptr;
	const auto finished = ReadWidgetOps(*tap);
	DisarmWidgetOpsTap(id);

	const auto scheme = kHelperMirrors ? std::string(kTheme) : std::string();
	AddProbeRow(
		rows,
		olderBuilt
			&& newerBuilt
			&& (before.generation == 1)
			&& (before.conformance == std::string(kOldDoneValue))
			&& (before.htmlId == kHtmlId)
			&& (before.updates == 2)
			&& (selected.generation == 2)
			&& !selected.conformance
			&& !selected.adopted
			&& (selected.updates == 0)
			&& selected.scheme.empty()
			&& (selected.serial == before.serial + 1),
		u"hosted: selecting a newer host clears the older host's done result "
		"before the newer one is built"_q,
		u"built=%1,%2 before{%3} selected{%4}"_q.arg(
			Flag(olderBuilt),
			Flag(newerBuilt),
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
			&& (own.scheme == scheme)
			&& (own.staleUpdates == 1),
		u"hosted: the newer host reads its own adoption and value from one, "
		"whatever the older host adopted after the selection"_q,
		LatestText(own));
	AddProbeRow(
		rows,
		(late.generation == 2)
			&& (late.conformance == std::string(kNewRunningValue))
			&& (late.htmlId == kHtmlId)
			&& (late.updates == 1)
			&& (late.serial == own.serial)
			&& (late.scheme == scheme)
			&& (late.schemeChanges == 0)
			&& late.themeMessage.empty()
			&& (late.staleUpdates == (kHelperMirrors ? 4 : 2))
			&& (lateReading.batches == 7)
			&& (lateReading.leaks == 2)
			&& oldForwarded,
		kHelperMirrors
			? u"hosted: the older host's late value, theme message and "
				"setTheme() are forwarded and its batches scanned, but the "
				"newer host's reading keeps its own"_q
			: u"hosted: without a Debug helper the older host's late value "
				"is forwarded and its batches scanned, its theme records pass "
				"on unread, and the newer host's reading keeps its own"_q,
		u"late{%1} oldForwarded=%2 productOps=%3 passed=%4 %5"_q.arg(
			LatestText(late),
			Flag(oldForwarded),
			QString::number(int(olderProduct.ops.size())),
			QString::number(int(olderProduct.platform.size())),
			ReadingText(lateReading)));
	AddProbeRow(
		rows,
		readopted.adopted
			&& (readopted.htmlId == kReadoptHtmlId)
			&& !readopted.conformance
			&& (readopted.updates == 1)
			&& (readopted.serial == late.serial + 1),
		u"hosted: a later adoption without a value clears it, and the "
		"earlier html id no longer matches"_q,
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
		u"hosted: the final reading scanned both hosts' batches, and both "
		"finished"_q,
		ReadingText(finished));
}

} // namespace
} // namespace Test::WidgetOps

namespace Test {

using namespace WidgetOps;

WidgetOpsSelfCheck RunWidgetOpsSelfCheck(WidgetOpsExchangeLeg exchanges) {
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
	CheckSessions(result);
	CheckStress(result);
	result.control = CheckHosted(result);
	for (const auto &row : CheckHostedFinals(
			SelfCheckId(kHostedFinalsIndex),
			SelfCheckId(kHostedEvictedIndex))) {
		AddRow(result, row.passed, row.name, row.details);
	}
	if (exchanges == WidgetOpsExchangeLeg::Within) {
		for (const auto &row : CheckHostedExchanges()) {
			AddRow(result, row.passed, row.name, row.details);
		}
	}
	CheckBudget(result);
	CheckScanGaps(result);
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
	return rows;
}

} // namespace Test

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
