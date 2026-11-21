/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_widget_ops.h"

#include "test/test_widget_ops_internal.h"
#include "tgw/tgw_hosted_wire.h"
#include "tgw/tgw_remote.h"
#include "twidget/remote_hosted.h"
#include "twidget/remote_stream.h"
#include "twidget/sandbox_engine.h"
#include "../../ThirdParty/widget-sandbox/packages/desktop/tests/test_stream_frames.h"

#include <QtCore/QStringList>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Test::WidgetOps {
namespace {

namespace HostedWire = Tgw::HostedWire;
namespace Remote = twlite::remote;
namespace Wire = twlite::wire;

using Role = twidget::SandboxHostedAnswers::Role;
using Death = twidget::SandboxHostedChannel::Death;

// Telegram's cap on one frame's bytes, which the render thread refuses
// before it decodes anything and past which a painted frame earns no
// credit: written out rather than taken from HostedWire::kMaxFrameBytes,
// so that the cases at it and one byte past it fail when that cap moves.
constexpr auto kTelegramFrameBytes = std::size_t(8) << 20;

// How long the end leg waits for each step of its stream (the opening, a
// route's requests, a client's death, the stream's end, the engine
// process's end done reading once the stream stopped) before its row fails
// instead of hanging the self-check.
constexpr auto kEndBound = std::chrono::seconds(5);

// How the framework words a hosted client's death at its stream's end and
// the end of a stream its engine process's end closed.
constexpr auto kEngineGone = "the widget engine process ended";
constexpr auto kEngineClosed = "the engine process closed the stream";

// A hosted client's delegate that only waits for the client's death.
class DeathWait final : public twidget::SandboxEngineDelegate {
public:
	void onOps(twidget::SandboxEngine &, const std::string &) override;
	twidget::QueryAnswer onQuery(
		twidget::SandboxEngine &,
		const std::string &) override;
	void onListen(twidget::SandboxEngine &, const std::string &) override;
	void onPreventDefault(
		twidget::SandboxEngine &,
		const std::string &) override;
	void onRequestFrame(twidget::SandboxEngine &) override;
	void onLog(
		twidget::SandboxEngine &,
		const std::string &,
		const std::string &) override;
	void onDiag(
		twidget::SandboxEngine &,
		const std::string &,
		const std::string &) override;
	void onLoaded(twidget::SandboxEngine &) override;
	void onStats(
		twidget::SandboxEngine &,
		const twidget::SandboxStats &) override;
	void onDied(
		twidget::SandboxEngine &,
		const std::string &reason) override;

	// The reason of the client's death, none when none came in |bound|.
	[[nodiscard]] std::optional<std::string> died(
		std::chrono::milliseconds bound);

private:
	std::mutex _mutex;
	std::condition_variable _changed;
	std::optional<std::string> _reason;

};

void DeathWait::onOps(twidget::SandboxEngine &, const std::string &) {
}

twidget::QueryAnswer DeathWait::onQuery(
		twidget::SandboxEngine &,
		const std::string &) {
	return {};
}

void DeathWait::onListen(twidget::SandboxEngine &, const std::string &) {
}

void DeathWait::onPreventDefault(
		twidget::SandboxEngine &,
		const std::string &) {
}

void DeathWait::onRequestFrame(twidget::SandboxEngine &) {
}

void DeathWait::onLog(
		twidget::SandboxEngine &,
		const std::string &,
		const std::string &) {
}

void DeathWait::onDiag(
		twidget::SandboxEngine &,
		const std::string &,
		const std::string &) {
}

void DeathWait::onLoaded(twidget::SandboxEngine &) {
}

void DeathWait::onStats(
		twidget::SandboxEngine &,
		const twidget::SandboxStats &) {
}

void DeathWait::onDied(twidget::SandboxEngine &, const std::string &reason) {
	{
		auto lock = std::lock_guard(_mutex);
		if (!_reason) {
			_reason = reason;
		}
	}
	_changed.notify_all();
}

std::optional<std::string> DeathWait::died(std::chrono::milliseconds bound) {
	auto lock = std::unique_lock(_mutex);
	_changed.wait_for(lock, bound, [&] { return _reason.has_value(); });
	return _reason;
}

[[nodiscard]] QString Flag(bool value) {
	return value ? u"1"_q : u"0"_q;
}

template <typename Message>
[[nodiscard]] std::string Encoded(const Message &message) {
	auto bytes = std::string();
	const auto caps = HostedWire::EmbeddingCaps();
	return Remote::encode(message, caps, bytes) ? bytes : std::string();
}

template <typename Value>
[[nodiscard]] std::string EncodedValue(const Value &value) {
	auto bytes = std::string();
	const auto caps = HostedWire::EmbeddingCaps().values;
	return Wire::encode(value, caps, bytes) ? bytes : std::string();
}

[[nodiscard]] std::string Cut(const std::string &bytes) {
	return bytes.empty() ? bytes : bytes.substr(0, bytes.size() - 1);
}

// A value's two header bytes, |form| and the wire version, then |count|
// as its u32 and |tail| as given.
[[nodiscard]] std::string Form(
		Wire::Form form,
		uint64 count,
		std::string_view tail = {},
		uchar version = Wire::VERSION) {
	auto result = std::string();
	result.push_back(char(uchar(form)));
	result.push_back(char(version));
	HostedWire::details::PutNumber(result, count, HostedWire::kLengthBytes);
	result.append(tail);
	return result;
}

// A frame of |seq| marked painted whose two values are |list| and |damage|
// byte for byte, after TwidgetCore's own encoding of its fixed part.
[[nodiscard]] std::string PaintedFrame(
		uint64 seq,
		std::string_view list,
		std::string_view damage) {
	auto result = Encoded(Remote::Frame{ .seq = seq });
	if (result.empty()) {
		return result;
	}
	result.back() = char(1);
	for (const auto value : { list, damage }) {
		HostedWire::details::PutNumber(
			result,
			value.size(),
			HostedWire::kLengthBytes);
		result.append(value);
	}
	return result;
}

// A painted frame of |seq| exactly |bytes| long, its list one command whose
// bytes are filler: a size only the render thread's own cap can refuse,
// its content only that thread's decoder.
[[nodiscard]] std::string FrameOfSize(
		uint64 seq,
		std::size_t bytes,
		std::string_view damage) {
	const auto fixed = PaintedFrame(seq, Form(Wire::Form::List, 1), damage);
	return (fixed.size() > bytes)
		? std::string()
		: PaintedFrame(
			seq,
			Form(
				Wire::Form::List,
				1,
				std::string(bytes - fixed.size(), char(0))),
			damage);
}

// TwidgetCore's own encodings, read through Tgw::HostedAnswers(): each
// request the platform end expects one answer to shares its answer's key
// (a family per pair, the sequence where the pair has one), only a frame
// request is timed while visible alone, a frame or a claim answer of
// another sequence keys apart, Stopped is the last word, and the rest reads
// as nothing.
[[nodiscard]] WidgetOpsSelfCheckRow CheckKinds() {
	struct Case {
		std::string bytes;
		Role role = Role::None;
		std::optional<uint64> sequence;
		bool whenVisible = false;
		int pair = -1;
	};
	const auto read = Tgw::HostedAnswers().read;
	const auto frame = Encoded(Remote::FrameRequest{ .seq = 7 });
	const auto painted = Encoded(Remote::Frame{ .seq = 7, .painted = true });
	const auto pointer = Encoded(Remote::Pointer{
		.cursor = "pointer",
		.claim = twidget::WidgetGestureClaim{ .x = true },
	});
	const auto claim = Encoded(Remote::ClaimAnswer{
		.seq = 3,
		.claim = { .hold = true },
	});
	auto unflagged = claim;
	if (!unflagged.empty()) {
		unflagged.back() = char(2);
	}
	const auto cases = std::vector<Case>{
		{ frame, Role::Expects, 7, true, 0 },
		{ Encoded(Remote::FrameRequest{ .seq = 8, .tick = true }),
			Role::Expects, 8, true, 0 },
		{ painted, Role::Answers, 7, false, 0 },
		{ Encoded(Remote::Frame{ .seq = 8 }), Role::Answers, 8, false, 0 },
		{ Encoded(Remote::Frame{ .seq = 9 }), Role::Answers, 9, false, 0 },
		{ Encoded(Remote::Touch{ .primary = true, .mouse = true }),
			Role::Expects, {}, false, 1 },
		{ Encoded(Remote::Hover()), Role::Expects, {}, false, 1 },
		{ Encoded(Remote::Pointer{ .cursor = "pointer" }),
			Role::Answers, {}, false, 1 },
		{ pointer, Role::Answers, {}, false, 1 },
		{ Encoded(Remote::ClaimQuery{ .seq = 3 }), Role::Expects, 3, false, 2 },
		{ claim, Role::Answers, 3, false, 2 },
		{ Encoded(Remote::ClaimAnswer{ .seq = 4 }),
			Role::Answers, 4, false, 2 },
		{ HostedWire::EncodeStopped("stopped"), Role::Last },
		{ Encoded(Remote::SetWidth{ .width = 200. }) },
		{ Encoded(Remote::State()) },
		{ frame.substr(0, 9) },
		{ Cut(claim) },
		{ unflagged },
		{ claim.empty() ? claim : (claim + char(0)) },
		{ Cut(painted) },
		{ Cut(pointer) },
		{ HostedWire::EncodeContent() },
	};
	auto families = std::array<std::optional<uint32_t>, 3>();
	auto passed = true;
	auto details = QStringList();
	for (const auto &entry : cases) {
		const auto reading = read(entry.bytes);
		auto family = (entry.pair >= 0)
			? &families[entry.pair]
			: nullptr;
		if (family && !*family) {
			*family = reading.family;
		}
		passed = passed
			&& !entry.bytes.empty()
			&& (reading.role == entry.role)
			&& (reading.sequence == entry.sequence)
			&& (reading.whenVisible == entry.whenVisible)
			&& (!family || (**family == reading.family));
		details.push_back(u"%1/%2/%3/%4"_q.arg(
			QString::number(int(reading.role)),
			QString::number(reading.family),
			(reading.sequence
				? QString::number(*reading.sequence)
				: u"-"_q),
			Flag(reading.whenVisible)));
	}
	return {
		.name = u"hosted exchanges: a frame request, painting or ticking, "
			"timed only while visible, expects the frame of its sequence, "
			"painted or unchanged, and a frame of another sequence keys "
			"apart; touch and hover expect a pointer, a claim query the "
			"claim answer of its sequence, and one of another sequence keys "
			"apart; the renderer's stop is its last word, and a width, a "
			"state, a cut sequenced prefix and another control read as "
			"nothing; an answer TwidgetCore's codec would refuse whole "
			"earns no credit: a claim answer cut short, with a flag that is "
			"no flag or a byte past its end, a painted frame and a pointer "
			"with a claim each cut short"_q,
		.details = details.join(u" "_q),
		.passed = passed
			&& (families[0] != families[1])
			&& (families[0] != families[2])
			&& (families[1] != families[2]),
	};
}

// A painted frame earns its request's credit only past the checks of a
// constant cost the stream thread may make (the fixed part, both values'
// headers and counts, the whole damage, Telegram's frame cap), never past
// a decode of its list: each case is read through Tgw::HostedAnswers()
// and, where the codec decides it, by TwidgetCore's own decode() too.
[[nodiscard]] WidgetOpsSelfCheckRow CheckPaintedFrames() {
	struct Case {
		const char *name = nullptr;
		std::string bytes;
		bool credited = false;
		// What TwidgetCore's decode() makes of it; none for a frame whose
		// list only the render thread reads.
		std::optional<bool> decodes;
	};
	const auto read = Tgw::HostedAnswers().read;
	const auto caps = HostedWire::EmbeddingCaps();
	auto command = twlite::Command();
	command.rect = { .x = 1, .y = 2, .w = 30, .h = 40 };
	auto drawn = Remote::Frame{ .seq = 7, .painted = true };
	drawn.list.push_back(command);
	drawn.damage = std::vector<twlite::Rect>{
		{ .x = 0, .y = 0, .w = 10, .h = 10 },
		{ .x = 5, .y = 5, .w = 20, .h = 20 },
	};
	const auto emptyList = EncodedValue(twlite::DisplayList());
	const auto noDamage = EncodedValue(Wire::Damage());
	const auto noRects = EncodedValue(
		Wire::Damage(std::vector<twlite::Rect>()));
	const auto rects = [](uint64 count, std::size_t bytes) {
		return Form(
			Wire::Form::Damage,
			count,
			std::string(bytes, char(0))).insert(2, 1, char(1));
	};
	const auto commands = caps.values.commands + 1;
	const auto overRects = caps.values.rects + 1;
	auto staleDamage = noDamage;
	if (!staleDamage.empty()) {
		staleDamage[1] = char(Wire::VERSION + 1);
	}
	const auto cases = std::vector<Case>{
		{ "drawn", Encoded(drawn), true, true },
		{ "empty list, no damage",
			PaintedFrame(7, emptyList, noDamage), true, true },
		{ "empty list, no rectangle",
			PaintedFrame(7, emptyList, noRects), true, true },
		{ "at the frame cap",
			FrameOfSize(7, kTelegramFrameBytes, noDamage), true, {} },
		{ "past the frame cap",
			FrameOfSize(7, kTelegramFrameBytes + 1, noDamage), false, {} },
		{ "empty values", PaintedFrame(7, {}, {}), false, false },
		{ "empty damage", PaintedFrame(7, emptyList, {}), false, false },
		{ "list of another form",
			PaintedFrame(7, Form(Wire::Form::Damage, 0), noDamage),
			false,
			false },
		{ "commands that do not fit",
			PaintedFrame(7, Form(Wire::Form::List, 3), noDamage),
			false,
			false },
		{ "commands past the cap",
			PaintedFrame(
				7,
				Form(
					Wire::Form::List,
					commands,
					std::string(commands * 5, char(0))),
				noDamage),
			false,
			false },
		{ "a byte after an empty list",
			PaintedFrame(7, emptyList + char(0), noDamage),
			false,
			false },
		{ "damage flag that is no flag",
			PaintedFrame(
				7,
				emptyList,
				noDamage.empty() ? noDamage : (Cut(noDamage) + char(2))),
			false,
			false },
		{ "damage of another version",
			PaintedFrame(7, emptyList, staleDamage),
			false,
			false },
		{ "a rectangle cut short",
			PaintedFrame(7, emptyList, rects(1, 15)),
			false,
			false },
		{ "a byte after the rectangles",
			PaintedFrame(7, emptyList, noRects + char(0)),
			false,
			false },
		{ "rectangles past the cap",
			PaintedFrame(7, emptyList, rects(overRects, overRects * 16)),
			false,
			false },
		{ "a byte after an unchanged frame",
			Encoded(Remote::Frame{ .seq = 7 }) + char(0),
			false,
			false },
	};
	auto passed = true;
	auto details = QStringList();
	for (const auto &entry : cases) {
		const auto reading = read(entry.bytes);
		const auto credited = (reading.role == Role::Answers)
			&& (reading.sequence == uint64(7));
		auto decoded = Remote::Frame();
		const auto decodes = entry.decodes
			? std::make_optional(Remote::decode(entry.bytes, caps, decoded))
			: std::nullopt;
		passed = passed
			&& !entry.bytes.empty()
			&& (credited == entry.credited)
			&& (credited || (reading.role == Role::None))
			&& (decodes == entry.decodes);
		details.push_back(u"{%1: %2 bytes credit=%3 decode=%4}"_q.arg(
			QString::fromLatin1(entry.name),
			QString::number(qint64(entry.bytes.size())),
			Flag(credited),
			decodes ? Flag(*decodes) : u"-"_q));
	}
	return {
		.name = u"hosted exchanges: a painted frame earns its request's "
			"credit only when its list and damage have their headers, "
			"counts that fit what follows within the caps, a whole damage "
			"and nothing after them, and within Telegram's 8 MiB frame cap: "
			"one the codec refuses whole (empty values, a value of another "
			"form or version, commands that do not fit or past the cap, a "
			"flag that is no flag, a cut or excess rectangle) or one byte "
			"past the cap earns none, while a drawn one, an empty list "
			"without or with no rectangles, and one at the cap whose list "
			"only the render thread decodes, earn it"_q,
		.details = details.join(u" "_q),
		.passed = passed,
	};
}

[[nodiscard]] QString DeathName(Death death) {
	switch (death) {
	case Death::Ordinary: return u"ordinary"_q;
	case Death::Stalled: return u"stalled"_q;
	case Death::Spared: return u"spared"_q;
	}
	return u"?"_q;
}

// One hosted widget on the leg's stream: the client with its delegate, and
// the channel it was given, whose death() it reads as Tgw's runtime does.
struct EndRoute {
	DeathWait wait;
	std::shared_ptr<twidget::SandboxHostedChannel> channel;
	std::unique_ptr<twidget::SandboxEngine> client;
};

// What the end leg read at its stream's end: each route's death as its
// client heard it, that death's reason, and the reason the host's end gave
// for the stream's end.
struct EndReading {
	std::array<Death, 2> deaths = {};
	std::array<std::string, 2> reasons;
	std::string ended;
};

// What the end leg plays on its two routes: the one request both send, the
// answer the engine process's end gives each, none for a request it leaves
// unanswered, and whether each is visible from its start (its HostedStart's
// config, with no Visible before it), which decides whether its frame
// request is timed.
struct EndScript {
	std::string request;
	std::array<std::optional<std::string>, 2> answers;
	std::array<bool, 2> visible = { true, true };
};

// Two hosted clients on a widget helper's stream opened with
// Tgw::HostedAnswers() attach as routes 1 and 2, each visible from its
// start as |script| says, start and send its request each; once the engine
// process's end, played by the framework's scripted end, has read every
// one of those frames, it gives each route the answer |script| has for it,
// if any, and closes its end. Every step waits for what the step before it
// showed.
[[nodiscard]] std::optional<EndReading> PlayEnd(
		twidget::ScriptedEngineEnd &engine,
		std::array<EndRoute, 2> &routes,
		const EndScript &script,
		QString &error) {
	using Handshake = twidget::ScriptedEngineEnd::Handshake;
	const auto handshake = engine.ready(
		kEndBound,
		twidget::SandboxRemoteWire::kVersion);
	if (handshake != Handshake::Answered) {
		error = (handshake == Handshake::Unopened)
			? u"no opening came"_q
			: u"the opening's answer was not written"_q;
		return std::nullopt;
	}
	for (auto i = 0; i != 2; ++i) {
		auto &route = routes[i];
		auto config = twidget::SandboxConfig();
		config.visible = script.visible[i];
		route.channel = engine.host().hostedChannel();
		route.client = twidget::SandboxEngine::hostedClient(
			"<p>widget ops self-check</p>",
			config,
			route.channel);
		route.client->setDelegate(&route.wait);
		route.client->startHosted(twidget::SandboxHostedStart());
		route.client->platform(script.request);
	}
	const auto frames = std::array<std::string, 3>{
		"open",
		"hosted-start",
		"platform:" + script.request,
	};
	for (auto runtime = uint32(1); runtime != 3; ++runtime) {
		for (const auto &frame : frames) {
			if (!engine.sawWithin(runtime, frame, kEndBound)) {
				error = u"route %1 sent no %2"_q.arg(runtime).arg(
					QString::fromStdString(frame.substr(0, frame.find(':'))));
				return std::nullopt;
			}
		}
	}
	auto answers = std::string();
	for (auto i = 0; i != 2; ++i) {
		if (const auto &answer = script.answers[i]) {
			answers += twidget::FrameBytes(
				uint32(i + 1),
				twidget::wire::PlatformMessage).str(*answer).bytes();
		}
	}
	if (!answers.empty() && !engine.send(answers)) {
		error = u"the answers were not written"_q;
		return std::nullopt;
	}
	engine.close();
	auto result = EndReading();
	for (auto i = 0; i != 2; ++i) {
		const auto reason = routes[i].wait.died(kEndBound);
		if (!reason) {
			error = u"route %1 heard no end"_q.arg(i + 1);
			return std::nullopt;
		}
		result.deaths[i] = routes[i].channel->death();
		result.reasons[i] = *reason;
	}
	const auto ended = engine.ended(kEndBound);
	if (!ended) {
		error = u"the stream's end was not reported"_q;
		return std::nullopt;
	}
	result.ended = *ended;
	return result;
}

// PlayEnd() on the framework's scripted engine process's end, whose own
// worker reads the host's end's frames over its memory pair; then the
// clients go, and the end stops and that worker returns within the leg's
// bound. What the leg read, or why it could not tell.
[[nodiscard]] std::optional<EndReading> EndOfStream(
		const EndScript &script,
		QString &error) {
	auto resources = twidget::SandboxResources();
	resources.sandboxBundle = "s";
	resources.validatorBundle = "v";
	auto engine = std::unique_ptr<twidget::ScriptedEngineEnd>();
	auto routes = std::array<EndRoute, 2>();
	auto result = std::optional<EndReading>();
	try {
		engine = std::make_unique<twidget::ScriptedEngineEnd>(
			resources,
			"widget ops self-check",
			twidget::SandboxRemoteHostEnd::Events(),
			twidget::SandboxRemoteHostEnd::Limits(),
			true,
			Tgw::HostedAnswers());
		result = PlayEnd(*engine, routes, script, error);
	} catch (const std::exception &e) {
		error = QString::fromUtf8(e.what());
	}
	for (auto &route : routes) {
		route.client = nullptr;
	}
	if (engine && !engine->finish(kEndBound)) {
		error = (error.isEmpty() ? QString() : (error + u"; "_q))
			+ u"the engine process's end still read after the stream stopped"_q;
		result = std::nullopt;
	}
	return result;
}

// The classifier at a stream's end on two routes: each sends one request
// and gets the case's answer, or none, route 2's a legal one but in the
// last case, once the engine process's end read both requests; then that
// end closes and the stream ends of it, as the host's end's reason and
// each client's death say. A route whose request no answer settled keeps
// its ordinary death and the answered one is spared, whether its answer
// earned no credit or came of another sequence, or none came to a frame
// it requested while hidden, which no deadline times but which is still
// unanswered; when both answers earned theirs neither had a request
// unanswered and both keep it, as they do when neither earned it.
[[nodiscard]] WidgetOpsSelfCheckRow CheckEndOfStream() {
	struct Case {
		const char *name = nullptr;
		EndScript script;
		std::array<Death, 2> deaths;
	};
	const auto frameRequest = Encoded(Remote::FrameRequest{ .seq = 7 });
	const auto frame = Encoded(Remote::Frame{ .seq = 7, .painted = true });
	const auto otherFrame = Encoded(Remote::Frame{
		.seq = 8,
		.painted = true,
	});
	const auto claimQuery = Encoded(Remote::ClaimQuery{ .seq = 3 });
	const auto claim = Encoded(Remote::ClaimAnswer{
		.seq = 3,
		.claim = { .hold = true },
	});
	const auto noDamage = EncodedValue(Wire::Damage());
	const auto selective = std::array{ Death::Ordinary, Death::Spared };
	const auto cases = std::vector<Case>{
		{
			"an impossible painted frame",
			{ frameRequest, { PaintedFrame(7, {}, {}), frame } },
			selective,
		},
		{
			"a frame past the cap",
			{
				frameRequest,
				{ FrameOfSize(7, kTelegramFrameBytes + 1, noDamage), frame },
			},
			selective,
		},
		{
			"a claim answer cut short",
			{ claimQuery, { Cut(claim), claim } },
			selective,
		},
		{
			"a frame of another sequence",
			{ frameRequest, { otherFrame, frame } },
			selective,
		},
		{
			"a hidden frame left unanswered",
			{ frameRequest, { std::nullopt, frame }, { false, true } },
			selective,
		},
		{
			"both answered",
			{ frameRequest, { frame, frame } },
			{ Death::Ordinary, Death::Ordinary },
		},
		{
			"neither answered",
			{ frameRequest, { PaintedFrame(7, {}, {}), Cut(frame) } },
			{ Death::Ordinary, Death::Ordinary },
		},
	};
	const auto encoded = [](const EndScript &script) {
		return !script.request.empty()
			&& std::none_of(
				begin(script.answers),
				end(script.answers),
				[](const std::optional<std::string> &answer) {
					return answer && answer->empty();
				});
	};
	auto passed = true;
	auto details = QStringList();
	for (const auto &entry : cases) {
		auto error = QString();
		const auto read = EndOfStream(entry.script, error);
		const auto gone = std::array<std::string, 2>{
			kEngineGone,
			kEngineGone,
		};
		passed = passed
			&& encoded(entry.script)
			&& read
			&& (read->deaths == entry.deaths)
			&& (read->reasons == gone)
			&& (read->ended == kEngineClosed);
		const auto reading = read
			? (DeathName(read->deaths[0])
				+ u"/"_q
				+ DeathName(read->deaths[1])
				+ ((read->reasons == gone)
					? QString()
					: u" died of "_q
						+ QString::fromStdString(read->reasons[0])
						+ u" / "_q
						+ QString::fromStdString(read->reasons[1]))
				+ u", ended: "_q
				+ QString::fromStdString(read->ended))
			: error;
		details.push_back(u"{%1: %2}"_q.arg(
			QString::fromLatin1(entry.name),
			reading));
	}
	return {
		.name = u"hosted exchanges: at a widget helper's stream end on two "
			"routes read through Tgw::HostedAnswers(), the engine process's "
			"end answering only once it read the opening and both real "
			"requests and the stream ending of that end's close, a route "
			"whose request stayed unanswered (its real answer an impossible "
			"painted frame, one past Telegram's frame cap, a claim answer "
			"cut short or a valid frame of another sequence, or no answer to "
			"a frame it requested while hidden, which no deadline times) "
			"keeps its ordinary death while the route whose request was "
			"legally answered is spared, and when both were answered both "
			"keep it, the fallback for an end with nothing unanswered, as "
			"both do when neither answer earned it"_q,
		.details = details.join(u" "_q),
		.passed = passed,
	};
}

// A frame delta answers the frame request of its sequence as a whole frame
// does: the frame's family, its full 64-bit sequence as the key, only past
// TwidgetCore's own scan of its runs (delta_scan()), with a list length a
// whole frame's list can have, and the stream's check of its damage, and
// within Telegram's frame cap. Each case is read through
// Tgw::HostedAnswers() and each delta by TwidgetCore's own decode() under
// the same caps, which takes every delta that earns credit, with its
// sequence, so every malformed case it refuses shows its change hit a field
// the codec checks. It also takes a delta of a list length no list can
// have, which earns no credit, as a whole frame of such a list earns none.
[[nodiscard]] WidgetOpsSelfCheckRow CheckFrameDeltas() {
	struct Case {
		const char *name = nullptr;
		std::string bytes;
		Role role = Role::None;
		std::optional<uint64> sequence;
		bool whenVisible = false;
		// What TwidgetCore's decode() makes of it as a frame delta; none
		// for a message of another kind.
		std::optional<bool> decodes;
	};
	// A delta's header, sequence, state and base, then its length and its
	// run count before its first run's tag.
	constexpr auto kIdsBytes = std::size_t(2 + 8 + 23 + 8);
	constexpr auto kFirstRunAt = kIdsBytes + 4 + 4;
	const auto read = Tgw::HostedAnswers().read;
	const auto caps = HostedWire::EmbeddingCaps();
	const auto family = uint32(Remote::Kind::Frame);
	// Another sequence, whose low 32 bits are the request's.
	const auto other = (uint64(0x89abcdef) << 32) | 7;
	const auto delta = [](
			uint64 seq,
			std::vector<Remote::DeltaRun> runs,
			Wire::Damage damage) {
		return Encoded(Remote::FrameDelta{
			.seq = seq,
			.base = seq - 1,
			.runs = std::move(runs),
			.damage = std::move(damage),
		});
	};
	// |bytes| changed by |change|, or left empty when it did not encode.
	const auto changed = [](std::string bytes, auto change) {
		if (!bytes.empty()) {
			change(bytes);
		}
		return bytes;
	};
	const auto mixed = std::vector<Remote::DeltaRun>{
		{ .offset = 0, .length = 6 },
		{ .literal = "abc" },
		{ .offset = 10, .length = 4 },
	};
	const auto damage = Wire::Damage(std::vector<twlite::Rect>{
		{ .x = 0, .y = 0, .w = 10, .h = 10 },
	});
	const auto painted = delta(7, mixed, damage);
	const auto undamaged = delta(7, mixed, std::nullopt);
	// A list of one byte, too short for an empty list, and one of ten, too
	// long for an empty list and too short for one with a command.
	const auto oneByte = std::string(1, char(0));
	const auto tenBytes = std::string(10, char(0));
	const auto listOf = [&](const std::string &bytes) {
		return delta(7, { { .literal = bytes } }, std::nullopt);
	};
	// The ten bytes and a copy of one at |offset|, the shortest list with a
	// command, encoded under a base a byte longer than Telegram's so that a
	// copy a byte past Telegram's base encodes too.
	const auto copyAt = [&](uint32 offset) {
		auto loose = caps;
		++loose.delta_base;
		auto bytes = std::string();
		const auto copy = Remote::FrameDelta{
			.seq = 7,
			.base = 6,
			.runs = {
				{ .literal = tenBytes },
				{ .offset = offset, .length = 1 },
			},
		};
		return Remote::encode(copy, loose, bytes) ? bytes : std::string();
	};
	const auto lastCopy = copyAt(uint32(caps.delta_base - 1));
	const auto pastBase = copyAt(uint32(caps.delta_base));
	const auto cases = std::vector<Case>{
		{
			"request",
			Encoded(Remote::FrameRequest{ .seq = 7 }),
			Role::Expects,
			7,
			true,
		},
		{
			"frame",
			Encoded(Remote::Frame{ .seq = 7, .painted = true }),
			Role::Answers,
			7,
		},
		{ "delta", painted, Role::Answers, 7, false, true },
		{ "no damage", undamaged, Role::Answers, 7, false, true },
		{
			"an empty list",
			listOf(EncodedValue(twlite::DisplayList())),
			Role::Answers,
			7,
			false,
			true,
		},
		{
			"a copy of the base's last byte",
			lastCopy,
			Role::Answers,
			7,
			false,
			true,
		},
		{
			"another sequence",
			delta(other, mixed, damage),
			Role::Answers,
			other,
			false,
			true,
		},
		{
			"a whole one-byte list",
			PaintedFrame(7, oneByte, EncodedValue(Wire::Damage())),
			Role::None,
		},
		{ "a one-byte list", listOf(oneByte), Role::None, {}, false, true },
		{ "a ten-byte list", listOf(tenBytes), Role::None, {}, false, true },
		{
			"cut after its ids",
			changed(painted, [](std::string &bytes) {
				bytes.resize(kIdsBytes);
			}),
			Role::None,
			{},
			false,
			false,
		},
		{ "cut short", Cut(painted), Role::None, {}, false, false },
		{
			"a byte after it",
			changed(painted, [](std::string &bytes) {
				bytes.push_back(char(0));
			}),
			Role::None,
			{},
			false,
			false,
		},
		{
			"a run of an unknown tag",
			changed(painted, [](std::string &bytes) {
				bytes[kFirstRunAt] = char(2);
			}),
			Role::None,
			{},
			false,
			false,
		},
		{
			"a copy a byte past the base",
			pastBase,
			Role::None,
			{},
			false,
			false,
		},
		{
			"damage flag that is no flag",
			changed(undamaged, [](std::string &bytes) {
				bytes.back() = char(2);
			}),
			Role::None,
			{},
			false,
			false,
		},
		{
			"past the frame cap",
			changed(painted, [](std::string &bytes) {
				bytes.resize(kTelegramFrameBytes + 1, char(0));
			}),
			Role::None,
			{},
			false,
			false,
		},
	};
	auto passed = true;
	auto details = QStringList();
	for (const auto &entry : cases) {
		const auto reading = read(entry.bytes);
		auto decoded = Remote::FrameDelta();
		const auto decodes = entry.decodes
			? std::make_optional(Remote::decode(entry.bytes, caps, decoded))
			: std::nullopt;
		const auto credited = decodes.has_value()
			&& (reading.role == Role::Answers);
		passed = passed
			&& !entry.bytes.empty()
			&& (reading.role == entry.role)
			&& ((reading.role == Role::None) || (reading.family == family))
			&& (reading.sequence == entry.sequence)
			&& (reading.whenVisible == entry.whenVisible)
			&& (decodes == entry.decodes)
			&& (!credited || (*decodes && (reading.sequence == decoded.seq)));
		details.push_back(u"{%1: %2 bytes %3/%4/%5/%6 decode=%7}"_q.arg(
			QString::fromLatin1(entry.name),
			QString::number(qint64(entry.bytes.size())),
			QString::number(int(reading.role)),
			QString::number(reading.family),
			(reading.sequence
				? QString::number(*reading.sequence)
				: u"-"_q),
			Flag(reading.whenVisible),
			decodes ? Flag(*decodes) : u"-"_q));
	}
	return {
		.name = u"hosted exchanges: a frame delta answers the frame request "
			"of its sequence as a frame does, one of another sequence keys "
			"apart, and one TwidgetCore's decode() refuses (cut, a byte after "
			"it, a bad run or damage flag), past Telegram's frame cap or of a "
			"list length a whole frame's list cannot have earns no credit"_q,
		.details = details.join(u" "_q),
		.passed = passed,
	};
}

} // namespace

std::vector<WidgetOpsSelfCheckRow> CheckHostedExchanges() {
	return {
		CheckKinds(),
		CheckPaintedFrames(),
		CheckEndOfStream(),
		CheckFrameDeltas(),
	};
}

} // namespace Test::WidgetOps

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
