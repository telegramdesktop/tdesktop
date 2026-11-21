/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_widget_ops.h"

#include "core/src/platform_end.h"
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
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
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

// Telegram's caps on a frame delta, written out for the same reason: the
// bytes of the list it rebuilds (HostedWire::kMaxDeltaBaseBytes), and its
// runs, one for the list's head and one per command of Telegram's list cap
// (HostedWire::kMaxListCommands).
constexpr auto kTelegramDeltaBaseBytes = uint64(256) << 10;
constexpr auto kTelegramDeltaRuns = (uint64(1) << 16) + 1;

// How long the end leg waits for each step of its stream (the opening, a
// route's requests, a client's death, the stream's end, the engine
// process's end done reading once the stream stopped) before its row fails
// instead of hanging the self-check.
constexpr auto kEndBound = std::chrono::seconds(5);

// How long the delta leg leaves a stream open before the engine process's
// end closes it: past the live answer deadline by more than the second
// look a late wake of the stream's monitor may ask for.
constexpr auto kOpenBound = std::chrono::milliseconds(
	int(twidget::SandboxEngine::kPlatformAnswerSeconds * 1000) + 1500);

// How long the exchange leg plays at most, a deadline all its cases share:
// short of a stage's kWidgetOpsExchangesTimeout by the two bounds a stream
// that was open may still take to close (OnStream()'s finish and the
// scripted end's own), so that a leg whose waits keep failing still gives
// its rows, each saying where it stopped, while that stage waits for them.
constexpr auto kLegBound = std::chrono::milliseconds(
	kWidgetOpsExchangesTimeout) - 2 * kEndBound;

// The longest one look of a stream's wait lasts before the wait sees
// whether the leg stopped.
constexpr auto kHaltSlice = std::chrono::milliseconds(50);

// How the framework words a hosted client's death at its stream's end and
// the end of a stream its engine process's end closed.
constexpr auto kEngineGone = "the widget engine process ended";
constexpr auto kEngineClosed = "the engine process closed the stream";

// A remote message's kind and version, before its fields.
constexpr auto kRemoteHeaderBytes = std::size_t(2);

// A hosted client's delegate that waits for the client's death and counts
// its renderer's messages, holding them on the client's dispatch thread
// from a given one on, as a consumer that has not taken them yet.
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
	void onHostedMessage(
		twidget::SandboxEngine &,
		const std::string &message) override;

	// The reason of the client's death, none when none came in |bound|.
	[[nodiscard]] std::optional<std::string> died(
		std::chrono::milliseconds bound);

	// Holds each of the renderer's messages from the |from|-th on (the
	// first is 1) until release(); the client hears nothing after a held
	// one, its death included, until then. release() lets them go and
	// holds none again, at any time and as often as called: every leg
	// calls it before its clients go.
	void holdFrom(int from);
	void release();

	// Whether |count| of the renderer's messages came in |bound|.
	[[nodiscard]] bool received(int count, std::chrono::milliseconds bound);

	// Whether one of them was held in |bound|.
	[[nodiscard]] bool held(std::chrono::milliseconds bound);

private:
	std::mutex _mutex;
	std::condition_variable _changed;
	std::optional<std::string> _reason;
	int _received = 0;
	int _holdFrom = 0;
	int _held = 0;
	bool _released = false;

};

// A platform end as Telegram's render thread keeps one, on Telegram's
// embedding caps, over a backend that has no fonts or images: the last
// request it wrote, each frame it gave and its refusal.
class RebuildProbe final
	: private twlite::Backend
	, private Remote::PlatformEnd::Host
	, private Remote::Channel {
public:
	RebuildProbe();

	[[nodiscard]] Remote::PlatformEnd &end();
	[[nodiscard]] const std::string &request() const;
	[[nodiscard]] const std::vector<Remote::Frame> &frames() const;
	[[nodiscard]] const std::optional<std::string> &refusal() const;

private:
	int create_font(
		const std::string &families,
		float size,
		int weight,
		bool italic,
		const std::string &features,
		twlite::FontMetrics &out) override;
	void delete_font(int font) override;
	float text_width(int font, const std::string &text) override;
	bool load_image(
		int id,
		const std::string &url,
		float &w,
		float &h) override;

	void frame(Remote::Frame &&frame) override;
	void state(const Remote::State &state) override;
	void messages(std::vector<twidget::Json> &&messages) override;
	void pointer(const Remote::Pointer &pointer) override;
	void selection(const Remote::Selection &selection) override;
	void claim(const Remote::ClaimAnswer &answer) override;
	void refused(const std::string &reason) override;

	void send(std::string message) override;

	Remote::PlatformEnd _end;
	std::string _request;
	std::vector<Remote::Frame> _frames;
	std::optional<std::string> _refusal;

};

// When the exchange leg stops: at its deadline, kLegBound after it began
// and shared by all its cases, or once its owner raises it, whichever
// comes first. From then on the leg starts no stream, and each wait of a
// stream it has open gives up within kHaltSlice, so that the stream's
// held messages go, its clients go, it closes and its threads return.
class Halt final {
public:
	Halt();

	// From any thread, at any time, as often as called.
	void raise();

	[[nodiscard]] bool halted() const;

	// Why the leg stopped, for its rows' details.
	[[nodiscard]] QString why() const;

private:
	const std::chrono::steady_clock::time_point _deadline;
	std::atomic<bool> _raised = false;

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

void DeathWait::onHostedMessage(
		twidget::SandboxEngine &,
		const std::string &) {
	auto lock = std::unique_lock(_mutex);
	++_received;
	if (_holdFrom && (_received >= _holdFrom) && !_released) {
		++_held;
		_changed.notify_all();
		_changed.wait(lock, [&] { return _released; });
	}
	_changed.notify_all();
}

std::optional<std::string> DeathWait::died(std::chrono::milliseconds bound) {
	auto lock = std::unique_lock(_mutex);
	_changed.wait_for(lock, bound, [&] { return _reason.has_value(); });
	return _reason;
}

void DeathWait::holdFrom(int from) {
	auto lock = std::lock_guard(_mutex);
	_holdFrom = from;
}

void DeathWait::release() {
	{
		auto lock = std::lock_guard(_mutex);
		_released = true;
	}
	_changed.notify_all();
}

bool DeathWait::received(int count, std::chrono::milliseconds bound) {
	auto lock = std::unique_lock(_mutex);
	return _changed.wait_for(lock, bound, [&] {
		return (_received >= count);
	});
}

bool DeathWait::held(std::chrono::milliseconds bound) {
	auto lock = std::unique_lock(_mutex);
	return _changed.wait_for(lock, bound, [&] { return (_held > 0); });
}

Halt::Halt()
: _deadline(std::chrono::steady_clock::now() + kLegBound) {
}

void Halt::raise() {
	_raised = true;
}

bool Halt::halted() const {
	return _raised || (std::chrono::steady_clock::now() >= _deadline);
}

QString Halt::why() const {
	return _raised
		? u"the leg was stopped"_q
		: u"the leg ran past its %1 ms"_q.arg(kLegBound.count());
}

RebuildProbe::RebuildProbe()
: _end(*this, *this, *this, HostedWire::EmbeddingCaps()) {
}

Remote::PlatformEnd &RebuildProbe::end() {
	return _end;
}

const std::string &RebuildProbe::request() const {
	return _request;
}

const std::vector<Remote::Frame> &RebuildProbe::frames() const {
	return _frames;
}

const std::optional<std::string> &RebuildProbe::refusal() const {
	return _refusal;
}

int RebuildProbe::create_font(
		const std::string &,
		float,
		int,
		bool,
		const std::string &,
		twlite::FontMetrics &) {
	return 0;
}

void RebuildProbe::delete_font(int) {
}

float RebuildProbe::text_width(int, const std::string &) {
	return 0.f;
}

bool RebuildProbe::load_image(int, const std::string &, float &, float &) {
	return false;
}

void RebuildProbe::frame(Remote::Frame &&frame) {
	_frames.push_back(std::move(frame));
}

void RebuildProbe::state(const Remote::State &) {
}

void RebuildProbe::messages(std::vector<twidget::Json> &&) {
}

void RebuildProbe::pointer(const Remote::Pointer &) {
}

void RebuildProbe::selection(const Remote::Selection &) {
}

void RebuildProbe::claim(const Remote::ClaimAnswer &) {
}

void RebuildProbe::refused(const std::string &reason) {
	if (!_refusal) {
		_refusal = reason;
	}
}

void RebuildProbe::send(std::string message) {
	_request = std::move(message);
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

// A damage of |count| rectangles whose bytes are |bytes| zeros, after
// its form, version and flag.
[[nodiscard]] std::string Rects(uint64 count, std::size_t bytes) {
	return Form(
		Wire::Form::Damage,
		count,
		std::string(bytes, char(0))).insert(2, 1, char(1));
}

// A frame delta's run that copies |length| bytes from |offset| of its
// base, and one that carries |bytes| itself, as TwidgetCore lays them out,
// for the runs its codec would not write.
[[nodiscard]] std::string CopyRun(uint64 offset, uint64 length) {
	auto result = std::string(1, char(0));
	HostedWire::details::PutNumber(result, offset, HostedWire::kLengthBytes);
	HostedWire::details::PutNumber(result, length, HostedWire::kLengthBytes);
	return result;
}

[[nodiscard]] std::string LiteralRun(std::string_view bytes) {
	auto result = std::string(1, char(1));
	HostedWire::details::PutNumber(
		result,
		bytes.size(),
		HostedWire::kLengthBytes);
	result.append(bytes);
	return result;
}

// |count| literal runs of one byte each.
[[nodiscard]] std::string Literals(uint64 count) {
	auto result = std::string();
	for (auto i = uint64(); i != count; ++i) {
		result += LiteralRun("x");
	}
	return result;
}

// A frame delta of |seq| on painted frame |base| that declares |length|
// rebuilt bytes in |count| runs, then |runs| and its damage |damage| byte
// for byte; its state is TwidgetCore's own encoding of a default one.
[[nodiscard]] std::string DeltaFrame(
		uint64 seq,
		uint64 base,
		uint64 length,
		uint64 count,
		std::string_view runs,
		std::string_view damage) {
	const auto state = Encoded(Remote::State());
	if (state.size() < kRemoteHeaderBytes) {
		return std::string();
	}
	auto result = std::string();
	result.push_back(char(uchar(Remote::Kind::FrameDelta)));
	result.push_back(char(Remote::VERSION));
	HostedWire::details::PutNumber(result, seq, HostedWire::kNumberBytes);
	result.append(state, kRemoteHeaderBytes);
	HostedWire::details::PutNumber(result, base, HostedWire::kNumberBytes);
	HostedWire::details::PutNumber(result, length, HostedWire::kLengthBytes);
	HostedWire::details::PutNumber(result, count, HostedWire::kLengthBytes);
	result.append(runs);
	HostedWire::details::PutNumber(
		result,
		damage.size(),
		HostedWire::kLengthBytes);
	result.append(damage);
	return result;
}

// TwidgetCore's own encoding of the frame delta of |seq| on painted frame
// |base| made of |runs|, with |damage|, its state a default one.
[[nodiscard]] std::string Delta(
		uint64 seq,
		uint64 base,
		std::vector<Remote::DeltaRun> runs,
		Wire::Damage damage = {}) {
	return Encoded(Remote::FrameDelta{
		.seq = seq,
		.base = base,
		.runs = std::move(runs),
		.damage = std::move(damage),
	});
}

// The frame delta of |seq| that copies the whole of |list|, the list of
// painted frame |base|, with no damage: what TwidgetCore's renderer end
// sends in place of a painted frame of |seq| whose list is |list| again.
[[nodiscard]] std::string CopyDelta(
		uint64 seq,
		uint64 base,
		std::string_view list) {
	return Delta(seq, base, { { .length = uint32(list.size()) } });
}

// A display list of |count| rectangles, each a command of its own.
[[nodiscard]] twlite::DisplayList Rectangles(int count) {
	auto result = twlite::DisplayList();
	for (auto i = 0; i != count; ++i) {
		auto command = twlite::Command();
		command.rect = { .x = float(1 + i), .y = 2, .w = 30, .h = 40 };
		result.push_back(command);
	}
	return result;
}

// A painted frame of |seq| whose list is |list|, with no damage.
[[nodiscard]] std::string WholeFrame(
		uint64 seq,
		const twlite::DisplayList &list) {
	auto frame = Remote::Frame{ .seq = seq, .painted = true };
	frame.list = list;
	return Encoded(frame);
}

// A route's repaint of an unchanged list as TwidgetCore's ends make it:
// its platform end's two forced frame requests, the list's bytes, the
// painted frame that answers the first (the base), and the two equivalent
// answers its renderer end may give the second, the painted frame whole
// and the frame delta that copies the whole list from that base.
struct Repaint {
	std::string first;
	std::string second;
	std::string list;
	std::string base;
	std::string whole;
	std::string delta;
};

[[nodiscard]] Repaint MakeRepaint() {
	const auto list = Rectangles(2);
	const auto bytes = EncodedValue(list);
	return {
		.first = Encoded(Remote::FrameRequest{
			.seq = 1,
			.paint = Remote::Paint::Force,
		}),
		.second = Encoded(Remote::FrameRequest{
			.seq = 2,
			.paint = Remote::Paint::Force,
		}),
		.list = bytes,
		.base = WholeFrame(1, list),
		.whole = WholeFrame(2, list),
		.delta = CopyDelta(2, 1, bytes),
	};
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
			PaintedFrame(7, emptyList, Rects(1, 15)),
			false,
			false },
		{ "a byte after the rectangles",
			PaintedFrame(7, emptyList, noRects + char(0)),
			false,
			false },
		{ "rectangles past the cap",
			PaintedFrame(7, emptyList, Rects(overRects, overRects * 16)),
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

// The fields of a frame delta that CheckFrameDeltas() leaves unread: one
// TwidgetCore's decode() refuses for its state, its rebuilt length (none,
// or past the base cap), its runs (none, more than follow or past the run
// cap, an empty copy or literal, a literal past the message, runs that
// rebuild short of or past that length) or its damage (missing, of another
// form or with rectangles past the cap) earns no credit either, while a
// copy of the whole base, a length at the base cap and runs at the run
// cap, which that decode() takes, earn it, in the painted frame's family
// with their own sequence. The malformed ones are laid out by hand, as the
// codec would not write them, beside a plain copy laid out the same way,
// which must be TwidgetCore's own encoding. Each case is read through
// Tgw::HostedAnswers() and by that decode() too.
[[nodiscard]] WidgetOpsSelfCheckRow CheckDeltaFields() {
	struct Case {
		const char *name = nullptr;
		std::string bytes;
		bool credited = false;
	};
	const auto read = Tgw::HostedAnswers().read;
	const auto caps = HostedWire::EmbeddingCaps();
	const auto family = read(Encoded(Remote::Frame{ .seq = 7 })).family;
	const auto list = EncodedValue(Rectangles(2));
	const auto size = uint64(list.size());
	const auto half = uint32(size / 2);
	const auto noDamage = EncodedValue(Wire::Damage());
	const auto copy = CopyRun(0, size);
	const auto valid = CopyDelta(7, 6, list);
	auto unflagged = valid;
	if (unflagged.size() > kRemoteHeaderBytes + 20) {
		unflagged[kRemoteHeaderBytes + 20] = char(2);
	}
	const auto most = std::vector<Remote::DeltaRun>(
		kTelegramDeltaRuns,
		Remote::DeltaRun{ .literal = "x" });
	const auto cases = std::vector<Case>{
		{ "a copy of the whole base", valid, true },
		{ "a length at the base cap",
			Delta(7, 6, { { .length = uint32(kTelegramDeltaBaseBytes) } }),
			true },
		{ "runs at the run cap", Delta(7, 6, most), true },
		{ "a state flag that is no flag", unflagged, false },
		{ "no runs", DeltaFrame(7, 6, size, 0, {}, noDamage), false },
		{ "an empty rebuild", DeltaFrame(7, 6, 0, 0, {}, noDamage), false },
		{ "more runs than follow",
			DeltaFrame(7, 6, size, 2, copy, noDamage),
			false },
		{ "runs past the run cap",
			DeltaFrame(
				7,
				6,
				kTelegramDeltaRuns + 1,
				kTelegramDeltaRuns + 1,
				Literals(kTelegramDeltaRuns + 1),
				noDamage),
			false },
		{ "an empty copy",
			DeltaFrame(7, 6, size, 2, CopyRun(0, 0) + copy, noDamage),
			false },
		{ "an empty literal",
			DeltaFrame(7, 6, size, 2, LiteralRun({}) + copy, noDamage),
			false },
		{ "runs that rebuild short",
			DeltaFrame(7, 6, size + 1, 1, copy, noDamage),
			false },
		{ "runs that rebuild long",
			DeltaFrame(7, 6, size - 1, 1, copy, noDamage),
			false },
		{ "a literal past the message",
			DeltaFrame(
				7,
				6,
				size,
				1,
				LiteralRun(list).substr(0, 5 + half),
				noDamage),
			false },
		{ "a length past the base cap",
			DeltaFrame(
				7,
				6,
				kTelegramDeltaBaseBytes + 1,
				2,
				CopyRun(0, kTelegramDeltaBaseBytes) + LiteralRun("x"),
				noDamage),
			false },
		{ "no damage", DeltaFrame(7, 6, size, 1, copy, {}), false },
		{ "damage of another form",
			DeltaFrame(7, 6, size, 1, copy, Form(Wire::Form::List, 0)),
			false },
		{ "rectangles past the cap",
			DeltaFrame(
				7,
				6,
				size,
				1,
				copy,
				Rects(caps.values.rects + 1, (caps.values.rects + 1) * 16)),
			false },
	};
	const auto handBuilt = DeltaFrame(7, 6, size, 1, copy, noDamage);
	auto passed = !valid.empty() && (handBuilt == valid);
	auto details = QStringList{
		u"{hand-built plain copy: %1}"_q.arg(Flag(handBuilt == valid)),
	};
	for (const auto &entry : cases) {
		const auto reading = read(entry.bytes);
		const auto credited = (reading.role == Role::Answers)
			&& (reading.family == family)
			&& (reading.sequence == uint64(7))
			&& !reading.whenVisible;
		auto decoded = Remote::FrameDelta();
		const auto decodes = Remote::decode(entry.bytes, caps, decoded);
		passed = passed
			&& !entry.bytes.empty()
			&& (credited == entry.credited)
			&& (credited || (reading.role == Role::None))
			&& (decodes == entry.credited);
		details.push_back(u"{%1: %2 bytes credit=%3 decode=%4}"_q.arg(
			QString::fromLatin1(entry.name),
			QString::number(qint64(entry.bytes.size())),
			Flag(credited),
			Flag(decodes)));
	}
	return {
		.name = u"hosted exchanges: a frame delta with a state flag that is "
			"no flag, no runs, an empty rebuild, more runs than follow or "
			"runs past the run cap, an empty copy or literal, runs that "
			"rebuild short of or past its length, a literal past the "
			"message, a length past the 256 KiB base cap, or a damage "
			"missing, of another form or with rectangles past the cap, each "
			"of which TwidgetCore's decode() refuses, earns no credit, while "
			"a copy of the whole base, a length at the base cap and runs at "
			"the run cap, which it takes, earn it in the painted frame's "
			"family with their own sequence; the plain copy laid out by hand "
			"is TwidgetCore's own encoding"_q,
		.details = details.join(u" "_q),
		.passed = passed,
	};
}

// The equivalence the delta legs rest on, and the refusal that stays with
// the render thread: a platform end on Telegram's caps, as that thread
// keeps one, asks for its two forced frames with Repaint's requests, and,
// its first answered by the painted base, takes the whole frame and the
// delta that copies it as one and the same second frame; a delta the
// stream credits alike (the frame family, its own sequence) that cannot
// rebuild on the base that end actually kept (none painted, another base,
// a copy past the base, bytes that make no list) is that end's refusal,
// and gives no frame.
[[nodiscard]] WidgetOpsSelfCheckRow CheckRebuilds() {
	struct Case {
		const char *name = nullptr;
		std::string first;
		std::string second;
		bool rebuilds = false;
	};
	const auto read = Tgw::HostedAnswers().read;
	const auto repaint = MakeRepaint();
	const auto family = read(repaint.whole).family;
	const auto size = uint32(repaint.list.size());
	const auto cases = std::vector<Case>{
		{ "whole", repaint.base, repaint.whole, true },
		{ "delta", repaint.base, repaint.delta, true },
		{ "delta on no painted base",
			Encoded(Remote::Frame{ .seq = 1 }),
			repaint.delta,
			false },
		{ "delta of another base",
			repaint.base,
			CopyDelta(2, 5, repaint.list),
			false },
		{ "delta copying past its base",
			repaint.base,
			Delta(2, 1, { { .length = size + 8 } }),
			false },
		{ "delta rebuilding no list",
			repaint.base,
			Delta(2, 1, { { .literal = std::string(size, char(0)) } }),
			false },
	};
	auto passed = true;
	auto details = QStringList();
	auto rebuilt = std::vector<std::string>();
	for (const auto &entry : cases) {
		const auto reading = read(entry.second);
		const auto credited = (reading.role == Role::Answers)
			&& (reading.family == family)
			&& (reading.sequence == uint64(2));
		auto probe = RebuildProbe();
		auto &end = probe.end();
		const auto first = end.request_frame(false, Remote::Paint::Force);
		const auto asked = (first == 1) && (probe.request() == repaint.first);
		const auto based = end.receive(entry.first);
		const auto second = end.request_frame(false, Remote::Paint::Force);
		const auto askedAgain = (second == 2)
			&& (probe.request() == repaint.second);
		const auto taken = end.receive(entry.second);
		const auto &frames = probe.frames();
		const auto rebuilds = taken
			&& !probe.refusal()
			&& (frames.size() == 2)
			&& frames.back().painted
			&& (frames.back().seq == 2);
		if (rebuilds) {
			rebuilt.push_back(Encoded(frames.back()));
		}
		passed = passed
			&& !entry.first.empty()
			&& !entry.second.empty()
			&& credited
			&& asked
			&& based
			&& askedAgain
			&& (rebuilds == entry.rebuilds)
			&& (rebuilds || (probe.refusal() && (frames.size() == 1)));
		details.push_back(u"{%1: %2 bytes credit=%3 frames=%4 "
			"refused=[%5]}"_q.arg(
				QString::fromLatin1(entry.name),
				QString::number(qint64(entry.second.size())),
				Flag(credited),
				QString::number(qint64(frames.size())),
				QString::fromStdString(probe.refusal().value_or(
					std::string()))));
	}
	const auto same = (rebuilt.size() == 2)
		&& !rebuilt[0].empty()
		&& (rebuilt[0] == rebuilt[1]);
	details.push_back(u"{same frame: %1}"_q.arg(Flag(same)));
	return {
		.name = u"hosted exchanges: a platform end on Telegram's caps, as "
			"the render thread keeps one, asked for two forced frames with "
			"the delta legs' requests and given the painted base, takes the "
			"whole second frame and the frame delta that copies it as the "
			"same frame, while a delta the stream credits alike, which "
			"cannot rebuild on the base that end kept (none painted, "
			"another base, a copy past the base, bytes that make no list), "
			"is that end's own refusal and gives no frame"_q,
		.details = details.join(u" "_q),
		.passed = passed && same,
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

// One hosted widget on a leg's stream: the client with its delegate, and
// the channel it was given, whose death() it reads as Tgw's runtime does.
struct EndRoute {
	DeathWait wait;
	std::shared_ptr<twidget::SandboxHostedChannel> channel;
	std::unique_ptr<twidget::SandboxEngine> client;
};

// What a leg read at its stream's end: each route's death as its client
// heard it, that death's reason, and the reason the host's end gave for
// the stream's end.
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

// How a wait of the leg went: what it waited for came, its bound passed
// first, or the leg stopped first.
enum class Waited {
	Came,
	Passed,
	Halted,
};

// |what|, with why |halt| stopped the leg.
[[nodiscard]] QString Stopped(const QString &what, const Halt &halt) {
	return what + u" ("_q + halt.why() + u")"_q;
}

// Calls |look| with how long it may wait for what it looks for, at most
// kHaltSlice, until it gives true, |bound| passed or |halt| stopped the
// leg, which it checks before each look.
template <typename Look>
[[nodiscard]] Waited WaitFor(
		const Halt &halt,
		std::chrono::milliseconds bound,
		Look &&look) {
	using std::chrono::milliseconds;
	using Clock = std::chrono::steady_clock;
	const auto until = Clock::now() + bound;
	while (!halt.halted()) {
		const auto left = std::chrono::ceil<milliseconds>(
			until - Clock::now());
		if (look(std::clamp(left, milliseconds(0), kHaltSlice))) {
			return Waited::Came;
		} else if (left <= kHaltSlice) {
			return Waited::Passed;
		}
	}
	return Waited::Halted;
}

// WaitFor(): true once what it waited for came; otherwise false, with
// |missing| in |error|, followed by why the leg stopped when it did.
template <typename Look>
[[nodiscard]] bool Await(
		const Halt &halt,
		std::chrono::milliseconds bound,
		Look &&look,
		const QString &missing,
		QString &error) {
	switch (WaitFor(halt, bound, std::forward<Look>(look))) {
	case Waited::Came: return true;
	case Waited::Passed: error = missing; return false;
	case Waited::Halted: error = Stopped(missing, halt); return false;
	}
	return false;
}

// Whether the engine process's end read each of |frames|, as describeHost()
// names them, on both routes, each within the leg's bound and before
// |halt| stopped the leg.
[[nodiscard]] bool Saw(
		twidget::ScriptedEngineEnd &engine,
		const std::vector<std::string> &frames,
		const Halt &halt,
		QString &error) {
	for (auto runtime = uint32(1); runtime != 3; ++runtime) {
		for (const auto &frame : frames) {
			const auto look = [&](std::chrono::milliseconds slice) {
				return engine.sawWithin(runtime, frame, slice);
			};
			const auto missing = u"route %1 sent no %2"_q.arg(runtime).arg(
				QString::fromStdString(frame.substr(0, frame.find(':'))));
			if (!Await(halt, kEndBound, look, missing, error)) {
				return false;
			}
		}
	}
	return true;
}

// Once the engine process's end, played by the framework's scripted end,
// answered the opening of a widget helper's stream opened with
// Tgw::HostedAnswers(), two hosted clients attach as routes 1 and 2, each
// visible from its start as |visible| says, and start; true once that end
// read both routes' opening and start, before |halt| stopped the leg.
[[nodiscard]] bool OpenRoutes(
		twidget::ScriptedEngineEnd &engine,
		std::array<EndRoute, 2> &routes,
		const std::array<bool, 2> &visible,
		const Halt &halt,
		QString &error) {
	using Handshake = twidget::ScriptedEngineEnd::Handshake;
	auto handshake = Handshake::Unopened;
	const auto opened = [&](std::chrono::milliseconds slice) {
		handshake = engine.ready(
			slice,
			twidget::SandboxRemoteWire::kVersion);
		return (handshake != Handshake::Unopened);
	};
	if (!Await(halt, kEndBound, opened, u"no opening came"_q, error)) {
		return false;
	} else if (handshake != Handshake::Answered) {
		error = u"the opening's answer was not written"_q;
		return false;
	}
	for (auto i = 0; i != 2; ++i) {
		auto &route = routes[i];
		auto config = twidget::SandboxConfig();
		config.visible = visible[i];
		route.channel = engine.host().hostedChannel();
		route.client = twidget::SandboxEngine::hostedClient(
			"<p>widget ops self-check</p>",
			config,
			route.channel);
		route.client->setDelegate(&route.wait);
		route.client->startHosted(twidget::SandboxHostedStart());
	}
	return Saw(engine, { "open", "hosted-start" }, halt, error);
}

// Each route sends |request|; true once the engine process's end read it
// on both, before |halt| stopped the leg.
[[nodiscard]] bool Ask(
		twidget::ScriptedEngineEnd &engine,
		std::array<EndRoute, 2> &routes,
		const std::string &request,
		const Halt &halt,
		QString &error) {
	for (auto &route : routes) {
		route.client->platform(request);
	}
	return Saw(engine, { "platform:" + request }, halt, error);
}

// The engine process's end gives each route its |answers|, in order, in
// one write.
[[nodiscard]] bool Answer(
		twidget::ScriptedEngineEnd &engine,
		const std::array<std::vector<std::string>, 2> &answers,
		QString &error) {
	auto bytes = std::string();
	for (auto i = 0; i != 2; ++i) {
		for (const auto &answer : answers[i]) {
			bytes += twidget::FrameBytes(
				uint32(i + 1),
				twidget::wire::PlatformMessage).str(answer).bytes();
		}
	}
	if (!bytes.empty() && !engine.send(bytes)) {
		error = u"the answers were not written"_q;
		return false;
	}
	return true;
}

// The engine process's end closes its end; once the host's end reported
// the stream's end, which decided each route's death before any client
// heard it, the routes' held messages go, and each client's death is read
// with the death() of its channel. Each wait gives up once |halt| stopped
// the leg.
[[nodiscard]] std::optional<EndReading> CloseEnd(
		twidget::ScriptedEngineEnd &engine,
		std::array<EndRoute, 2> &routes,
		const Halt &halt,
		QString &error) {
	engine.close();
	auto ended = std::optional<std::string>();
	const auto reported = [&](std::chrono::milliseconds slice) {
		ended = engine.ended(slice);
		return ended.has_value();
	};
	const auto heard = Await(
		halt,
		kEndBound,
		reported,
		u"the stream's end was not reported"_q,
		error);
	for (auto &route : routes) {
		route.wait.release();
	}
	if (!heard) {
		return std::nullopt;
	}
	auto result = EndReading{ .ended = *ended };
	for (auto i = 0; i != 2; ++i) {
		auto &wait = routes[i].wait;
		auto reason = std::optional<std::string>();
		const auto died = [&](std::chrono::milliseconds slice) {
			reason = wait.died(slice);
			return reason.has_value();
		};
		const auto missing = u"route %1 heard no end"_q.arg(i + 1);
		if (!Await(halt, kEndBound, died, missing, error)) {
			return std::nullopt;
		}
		result.deaths[i] = routes[i].channel->death();
		result.reasons[i] = *reason;
	}
	return result;
}

// Routes 1 and 2, each visible from its start as |script| says, send its
// request each; once the engine process's end has read every one of those
// frames, it gives each route the answer |script| has for it, if any, and
// closes its end. Every step waits for what the step before it showed,
// and gives up once |halt| stopped the leg.
[[nodiscard]] std::optional<EndReading> PlayEnd(
		twidget::ScriptedEngineEnd &engine,
		std::array<EndRoute, 2> &routes,
		const EndScript &script,
		const Halt &halt,
		QString &error) {
	auto answers = std::array<std::vector<std::string>, 2>();
	for (auto i = 0; i != 2; ++i) {
		if (const auto &answer = script.answers[i]) {
			answers[i].push_back(*answer);
		}
	}
	if (!OpenRoutes(engine, routes, script.visible, halt, error)
		|| !Ask(engine, routes, script.request, halt, error)
		|| !Answer(engine, answers, error)) {
		return std::nullopt;
	}
	return CloseEnd(engine, routes, halt, error);
}

// |play| of a stream of its own on the framework's scripted engine
// process's end, whose own worker reads the host's end's frames over its
// memory pair, given that end and two routes; then every message a route
// holds goes, the clients go, and the end stops and that worker returns
// within the leg's bound. What |play| read, or why it could not tell.
// Once |halt| stopped the leg no stream starts and |play| is not called.
template <typename Reading, typename Play>
[[nodiscard]] std::optional<Reading> OnStream(
		Play &&play,
		const Halt &halt,
		QString &error) {
	if (halt.halted()) {
		error = Stopped(u"not played"_q, halt);
		return std::nullopt;
	}
	auto resources = twidget::SandboxResources();
	resources.sandboxBundle = "s";
	resources.validatorBundle = "v";
	auto engine = std::unique_ptr<twidget::ScriptedEngineEnd>();
	auto routes = std::array<EndRoute, 2>();
	auto result = std::optional<Reading>();
	try {
		engine = std::make_unique<twidget::ScriptedEngineEnd>(
			resources,
			"widget ops self-check",
			twidget::SandboxRemoteHostEnd::Events(),
			twidget::SandboxRemoteHostEnd::Limits(),
			true,
			Tgw::HostedAnswers());
		result = play(*engine, routes);
	} catch (const std::exception &e) {
		error = QString::fromUtf8(e.what());
	}
	for (auto &route : routes) {
		route.wait.release();
		route.client = nullptr;
	}
	if (engine && !engine->finish(kEndBound)) {
		error = (error.isEmpty() ? QString() : (error + u"; "_q))
			+ u"the engine process's end still read after the stream stopped"_q;
		result = std::nullopt;
	}
	return result;
}

// PlayEnd() of |script| on a stream of its own (OnStream()), until |halt|
// stops the leg.
[[nodiscard]] std::optional<EndReading> EndOfStream(
		const EndScript &script,
		const Halt &halt,
		QString &error) {
	return OnStream<EndReading>([&](
			twidget::ScriptedEngineEnd &engine,
			std::array<EndRoute, 2> &routes) {
		return PlayEnd(engine, routes, script, halt, error);
	}, halt, error);
}

// Whether |read| is the end of a stream that the engine process's end
// closed, each client hearing its engine process end, with |deaths|.
[[nodiscard]] bool EndsAs(
		const EndReading &read,
		const std::array<Death, 2> &deaths) {
	const auto gone = std::array<std::string, 2>{ kEngineGone, kEngineGone };
	return (read.deaths == deaths)
		&& (read.reasons == gone)
		&& (read.ended == kEngineClosed);
}

[[nodiscard]] QString EndText(const EndReading &read) {
	const auto gone = std::array<std::string, 2>{ kEngineGone, kEngineGone };
	return DeathName(read.deaths[0])
		+ u"/"_q
		+ DeathName(read.deaths[1])
		+ ((read.reasons == gone)
			? QString()
			: u" died of "_q
				+ QString::fromStdString(read.reasons[0])
				+ u" / "_q
				+ QString::fromStdString(read.reasons[1]))
		+ u", ended: "_q
		+ QString::fromStdString(read.ended);
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
// unanswered and both keep it, as they do when neither earned it. A case
// reached once |halt| stopped the leg is not played, and fails.
[[nodiscard]] WidgetOpsSelfCheckRow CheckEndOfStream(const Halt &halt) {
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
		const auto read = EndOfStream(entry.script, halt, error);
		passed = passed
			&& encoded(entry.script)
			&& read
			&& EndsAs(*read, entry.deaths);
		details.push_back(u"{%1: %2}"_q.arg(
			QString::fromLatin1(entry.name),
			read ? EndText(*read) : error));
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

// What the delta leg plays on its two routes once each painted its base:
// the answers the engine process's end gives each route to its second
// frame request, in order (none: the route still owes it), and whether
// that end then leaves the stream open past the answer deadline before it
// closes it.
struct DeltaScript {
	std::array<std::vector<std::string>, 2> answers;
	bool linger = false;
};

// What the delta leg read: whether the stream was still open when the
// engine process's end closed it, and that end as the end leg reads one.
struct DeltaReading {
	bool open = false;
	EndReading end;
};

// Routes 1 and 2, both visible, ask for |repaint|'s first frame, and the
// engine process's end paints each its base whole; once both clients had
// it, each holds every later message of its renderer, as a consumer that
// has not taken it, and both ask for the second frame. That end gives each
// route |script|'s answers to it and, once each answered route's first
// answer is held, leaves the stream open past the answer deadline if
// |script| says so, then closes its end, the held answers going only once
// the stream's end was reported: only their arrival can have paid. Every
// wait, that stream left open included, gives up once |halt| stopped the
// leg, and nothing is read then.
[[nodiscard]] std::optional<DeltaReading> PlayDelta(
		twidget::ScriptedEngineEnd &engine,
		std::array<EndRoute, 2> &routes,
		const Repaint &repaint,
		const DeltaScript &script,
		const Halt &halt,
		QString &error) {
	const auto bases = std::array<std::vector<std::string>, 2>{
		std::vector<std::string>{ repaint.base },
		std::vector<std::string>{ repaint.base },
	};
	if (!OpenRoutes(engine, routes, { true, true }, halt, error)
		|| !Ask(engine, routes, repaint.first, halt, error)
		|| !Answer(engine, bases, error)) {
		return std::nullopt;
	}
	for (auto i = 0; i != 2; ++i) {
		auto &wait = routes[i].wait;
		const auto based = [&](std::chrono::milliseconds slice) {
			return wait.received(1, slice);
		};
		const auto missing = u"route %1 had no base"_q.arg(i + 1);
		if (!Await(halt, kEndBound, based, missing, error)) {
			return std::nullopt;
		}
		wait.holdFrom(2);
	}
	if (!Ask(engine, routes, repaint.second, halt, error)
		|| !Answer(engine, script.answers, error)) {
		return std::nullopt;
	}
	for (auto i = 0; i != 2; ++i) {
		auto &wait = routes[i].wait;
		const auto held = [&](std::chrono::milliseconds slice) {
			return wait.held(slice);
		};
		const auto missing = u"route %1 held no answer"_q.arg(i + 1);
		if (!script.answers[i].empty()
			&& !Await(halt, kEndBound, held, missing, error)) {
			return std::nullopt;
		}
	}
	const auto ended = [&](std::chrono::milliseconds slice) {
		return engine.ended(slice).has_value();
	};
	const auto left = script.linger
		? WaitFor(halt, kOpenBound, ended)
		: Waited::Passed;
	if (left == Waited::Halted) {
		error = Stopped(u"the stream was left open"_q, halt);
		return std::nullopt;
	}
	auto end = CloseEnd(engine, routes, halt, error);
	if (!end) {
		return std::nullopt;
	}
	return DeltaReading{
		.open = (left == Waited::Passed),
		.end = std::move(*end),
	};
}

// PlayDelta() of |script| on a stream of its own (OnStream()), until
// |halt| stops the leg.
[[nodiscard]] std::optional<DeltaReading> DeltaEnd(
		const Repaint &repaint,
		const DeltaScript &script,
		const Halt &halt,
		QString &error) {
	return OnStream<DeltaReading>([&](
			twidget::ScriptedEngineEnd &engine,
			std::array<EndRoute, 2> &routes) {
		return PlayDelta(engine, routes, repaint, script, halt, error);
	}, halt, error);
}

// Starts |play| on a thread of its own, kept in |threads|; false, with why
// in |error| and |threads| as it was, when that thread did not start, so
// |play| has not run.
template <typename Play>
[[nodiscard]] bool Launch(
		std::vector<std::thread> &threads,
		const Play &play,
		QString &error) {
	try {
		threads.emplace_back(play);
		return true;
	} catch (const std::exception &e) {
		error = QString::fromUtf8(e.what());
		return false;
	}
}

// A frame delta at a stream's end on two routes that each painted their
// base whole first, each answer to the second forced frame request held
// at its consumer: route 1's delta that copies its base pays its request
// as the equivalent whole frame of the same sequence does. With route 2
// still owing its own, the close spares route 1 and keeps route 2's
// ordinary death; with both answered both keep it, the fallback for an
// end with nothing unanswered; and with both answered and the stream left
// open past the answer deadline, no deadline ends it, and both keep it at
// the close. A delta of a later sequence, the delta sent on route 2 after
// its own answer, one cut short, one whose runs rebuild short of their
// length and one past the base cap pay nothing: route 1 keeps its ordinary
// death and route 2 is spared. The two streams left open run on threads
// of their own while the others play, joined before the row is read and
// on any way out; one whose thread does not start plays on the caller in
// its turn instead, its row saying so. Once |halt| stopped the leg, each
// stream still open gives up its waits and closes, a case reached then is
// not played, and the row fails.
[[nodiscard]] WidgetOpsSelfCheckRow CheckDeltaEnds(const Halt &halt) {
	struct Case {
		const char *name = nullptr;
		DeltaScript script;
		std::array<Death, 2> deaths;
	};
	const auto repaint = MakeRepaint();
	const auto size = uint64(repaint.list.size());
	const auto noDamage = EncodedValue(Wire::Damage());
	const auto &whole = repaint.whole;
	const auto &delta = repaint.delta;
	const auto respond = [](
			std::vector<std::string> first,
			std::vector<std::string> second,
			bool linger = false) {
		return DeltaScript{
			.answers = { std::move(first), std::move(second) },
			.linger = linger,
		};
	};
	const auto spared = std::array{ Death::Spared, Death::Ordinary };
	const auto idle = std::array{ Death::Ordinary, Death::Ordinary };
	const auto owing = std::array{ Death::Ordinary, Death::Spared };
	const auto cases = std::vector<Case>{
		{ "whole, route 2 owing", respond({ whole }, {}), spared },
		{ "delta, route 2 owing", respond({ delta }, {}), spared },
		{ "whole, both answered", respond({ whole }, { whole }), idle },
		{ "delta, both answered", respond({ delta }, { whole }), idle },
		{ "whole, both answered, left open",
			respond({ whole }, { whole }, true),
			idle },
		{ "delta, both answered, left open",
			respond({ delta }, { whole }, true),
			idle },
		{ "a delta of a later sequence",
			respond({ CopyDelta(3, 1, repaint.list) }, { whole }),
			owing },
		{ "the delta on route 2", respond({}, { whole, delta }), owing },
		{ "a delta cut short", respond({ Cut(delta) }, { whole }), owing },
		{ "a delta that rebuilds short",
			respond(
				{ DeltaFrame(2, 1, size + 1, 1, CopyRun(0, size), noDamage) },
				{ whole }),
			owing },
		{ "a delta past the base cap",
			respond(
				{
					DeltaFrame(
						2,
						1,
						kTelegramDeltaBaseBytes + 1,
						2,
						(CopyRun(0, kTelegramDeltaBaseBytes)
							+ LiteralRun("x")),
						noDamage),
				},
				{ whole }),
			owing },
	};
	const auto encoded = [&](const DeltaScript &script) {
		const auto filled = [](const std::vector<std::string> &answers) {
			return std::none_of(
				begin(answers),
				end(answers),
				[](const std::string &answer) { return answer.empty(); });
		};
		return !repaint.first.empty()
			&& !repaint.second.empty()
			&& !repaint.base.empty()
			&& filled(script.answers[0])
			&& filled(script.answers[1]);
	};
	auto reads = std::vector<std::optional<DeltaReading>>(cases.size());
	auto errors = std::vector<QString>(cases.size());
	auto refused = std::vector<QString>(cases.size());
	auto lingering = std::vector<std::thread>();
	const auto join = [&] {
		for (auto &thread : lingering) {
			if (thread.joinable()) {
				thread.join();
			}
		}
	};
	const auto joined = gsl::finally(join);
	for (auto i = std::size_t(); i != cases.size(); ++i) {
		const auto play = [&, i] {
			reads[i] = DeltaEnd(repaint, cases[i].script, halt, errors[i]);
		};
		if (!cases[i].script.linger || !Launch(lingering, play, refused[i])) {
			play();
		}
	}
	join();
	auto passed = true;
	auto details = QStringList();
	for (auto i = std::size_t(); i != cases.size(); ++i) {
		const auto &entry = cases[i];
		const auto &read = reads[i];
		passed = passed
			&& encoded(entry.script)
			&& read
			&& read->open
			&& EndsAs(read->end, entry.deaths);
		const auto open = (read && entry.script.linger)
			? (u", open past the deadline: "_q + Flag(read->open))
			: QString();
		const auto caller = refused[i].isEmpty()
			? QString()
			: (u", played on the caller, its thread refused: "_q
				+ refused[i]);
		details.push_back(u"{%1: %2%3}"_q.arg(
			QString::fromLatin1(entry.name),
			read ? (EndText(read->end) + open) : errors[i],
			caller));
	}
	return {
		.name = u"hosted exchanges: at a widget helper's stream end on two "
			"visible routes read through Tgw::HostedAnswers(), each route "
			"painting its base whole first and holding its answers to the "
			"second forced frame request at its consumer, so that only "
			"their arrival can pay, route 1's frame delta that copies its "
			"base pays as the equivalent whole frame of the same sequence "
			"does: with route 2 still owing its own the close spares route "
			"1 and keeps route 2's ordinary death, with both answered both "
			"keep it, and with both answered and the stream left open past "
			"the 3000 ms answer deadline no deadline ends it and both keep "
			"it at the close; a delta of a later sequence, the delta sent "
			"on route 2 after its own answer, one cut short, one whose runs "
			"rebuild short of their length and one past the base cap pay "
			"nothing, so route 1 keeps its ordinary death and route 2 is "
			"spared"_q,
		.details = details.join(u" "_q),
		.passed = passed,
	};
}

// The exchange leg's rows, its streams played until |halt| stops it.
[[nodiscard]] std::vector<WidgetOpsSelfCheckRow> HostedExchanges(
		const Halt &halt) {
	return {
		CheckKinds(),
		CheckPaintedFrames(),
		CheckEndOfStream(halt),
		CheckFrameDeltas(),
		CheckDeltaFields(),
		CheckRebuilds(),
		CheckDeltaEnds(halt),
	};
}

} // namespace

std::vector<WidgetOpsSelfCheckRow> CheckHostedExchanges() {
	return HostedExchanges(Halt());
}

} // namespace Test::WidgetOps

namespace Test {

// |rows| written by the leg's thread before |ready|, read on main after
// it; |halt| made on main before that thread starts, then read by the
// leg's threads and raised on main; the rest on main.
struct WidgetOpsExchanges::State {
	std::vector<WidgetOpsSelfCheckRow> rows;
	std::optional<WidgetOps::Halt> halt;
	std::thread thread;
	std::atomic<bool> ready = false;
	bool started = false;
	bool stopped = false;
};

namespace {

// The one row of an exchange leg that gave none of its own, |details|
// saying why.
[[nodiscard]] WidgetOpsSelfCheckRow UnplayedRow(const QString &details) {
	return {
		.name = WidgetOps::SelfCheckRowName(u"hosted exchanges: the leg "
			"played to its end on a thread of its own"_q),
		.details = details,
		.passed = false,
	};
}

// The exchange leg until |halt| stops it, its rows named as
// RunWidgetOpsSelfCheck() names them, or UnplayedRow() with what it threw:
// nothing leaves the thread.
[[nodiscard]] std::vector<WidgetOpsSelfCheckRow> PlayExchanges(
		const WidgetOps::Halt &halt) {
	try {
		auto result = WidgetOps::HostedExchanges(halt);
		for (auto &row : result) {
			row.name = WidgetOps::SelfCheckRowName(row.name);
		}
		return result;
	} catch (const std::exception &e) {
		return { UnplayedRow(u"it threw: "_q + QString::fromUtf8(e.what())) };
	} catch (...) {
		return { UnplayedRow(u"it threw"_q) };
	}
}

} // namespace

WidgetOpsExchanges::WidgetOpsExchanges()
: _state(std::make_unique<State>()) {
}

WidgetOpsExchanges::~WidgetOpsExchanges() {
	stop();
	if (_state->thread.joinable()) {
		_state->thread.join();
	}
}

void WidgetOpsExchanges::start() {
	const auto state = _state.get();
	if (state->started || state->stopped) {
		return;
	}
	state->started = true;
	const auto halt = &state->halt.emplace();
	try {
		state->thread = std::thread([=] {
			state->rows = PlayExchanges(*halt);
			state->ready.store(true, std::memory_order_release);
		});
	} catch (const std::exception &e) {
		state->rows = {
			UnplayedRow(u"its thread did not start: "_q
				+ QString::fromUtf8(e.what())),
		};
		state->ready.store(true, std::memory_order_release);
	}
}

void WidgetOpsExchanges::stop() {
	_state->stopped = true;
	if (_state->halt) {
		_state->halt->raise();
	}
}

bool WidgetOpsExchanges::ready() const {
	return _state->ready.load(std::memory_order_acquire);
}

std::vector<WidgetOpsSelfCheckRow> WidgetOpsExchanges::take() {
	if (_state->stopped || !ready()) {
		return {};
	}
	if (_state->thread.joinable()) {
		_state->thread.join();
	}
	return base::take(_state->rows);
}

} // namespace Test

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
