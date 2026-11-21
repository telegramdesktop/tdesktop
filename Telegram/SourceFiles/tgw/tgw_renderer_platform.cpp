/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_renderer_platform.h"

#include "core/src/canvas_ops.h"
#include "tgw/tgw_backend.h"
#include "tgw/tgw_hosted_wire.h"
#include "tgw/tgw_painter.h"
#include "tgw/tgw_render_thread.h"
#include "tgw/tgw_renderer_input.h"
#include "tgw/tgw_renderer_rules.h"
#include "twidget/json.h"
#include "twidget/sandbox_engine.h"

namespace Tgw {
namespace {

namespace Remote = twlite::remote;

constexpr auto kPumpEntries = 32;
constexpr auto kPumpTime = crl::time(8);
constexpr auto kMaxReportsBytes = std::size_t(256) << 10;
constexpr auto kMaxDecodeBytes = int64(128) << 20;
constexpr auto kHeapBlock = int64(16);
constexpr auto kSharedBlock = int64(24);
constexpr auto kCommandWireBytes = int64(5);
constexpr auto kCanvasOpWireBytes = int64(9);
constexpr auto kClipWireBytes = int64(9);
constexpr auto kFilterWireBytes = int64(17);
constexpr auto kJsonValueWireBytes = int64(2);
constexpr auto kVectorGrowth = int64(3);

constexpr auto kCommandBytes = int64(sizeof(twlite::Command))
	+ int64(sizeof(twlite::Path))
	+ int64(sizeof(twlite::Filters))
	+ 2 * kSharedBlock
	+ 10 * kHeapBlock;
constexpr auto kCanvasOpBytes = int64(sizeof(twlite::CanvasOp))
	+ 2 * int64(sizeof(std::vector<uint8_t>))
	+ 2 * kSharedBlock
	+ 9 * kHeapBlock;

struct ReportsEnvelope {
	std::size_t head = 0;
	std::size_t count = 0;
	std::size_t length = 0;
};

[[nodiscard]] constexpr int64 FrameDecodeBytes(int64 bytes) {
	const auto commands = std::min(
		int64(HostedWire::kMaxListCommands),
		bytes / kCommandWireBytes);
	return commands * kCommandBytes
		+ bytes * int64(sizeof(twlite::Filter)) / kFilterWireBytes;
}

[[nodiscard]] constexpr int64 CanvasRunDecodeBytes(int64 bytes) {
	const auto ops = std::min(
		int64(HostedWire::kMaxCanvasRunOps),
		bytes / kCanvasOpWireBytes);
	return ops * kCanvasOpBytes
		+ bytes
			* (int64(sizeof(twlite::CanvasClip)) + kHeapBlock)
			/ (kClipWireBytes + 1);
}

[[nodiscard]] constexpr int64 ReportsDecodeBytes(int64 bytes) {
	return bytes
		* kVectorGrowth
		* int64(sizeof(twidget::Json))
		/ kJsonValueWireBytes;
}

static_assert(FrameDecodeBytes(int64(HostedWire::kMaxFrameBytes))
	+ 2 * int64(HostedWire::kMaxDeltaBaseBytes)
	<= kMaxDecodeBytes);
static_assert(ReportsDecodeBytes(int64(kMaxReportsBytes)) <= kMaxDecodeBytes);

[[nodiscard]] const ReportsEnvelope &Envelope() {
	static const auto result = [] {
		const auto caps = HostedWire::EmbeddingCaps();
		auto none = std::string();
		auto one = std::string();
		const auto encoded = Remote::encode(Remote::Messages(), caps, none)
			&& Remote::encode(
				Remote::Messages{ .texts = { std::string() } },
				caps,
				one);
		Assert(encoded && (one.size() > none.size()));
		const auto count = std::mismatch(
			none.begin(),
			none.end(),
			one.begin()).first - none.begin();
		const auto length = one.size() - none.size();
		Assert(std::size_t(count) + length <= none.size());
		return ReportsEnvelope{
			.head = none.size(),
			.count = std::size_t(count),
			.length = length,
		};
	}();
	return result;
}

[[nodiscard]] std::optional<QString> UnfitReports(
		std::string_view message,
		const Remote::Caps &caps) {
	const auto &envelope = Envelope();
	if (message.size() < envelope.head) {
		return std::nullopt;
	}
	auto count = uint64();
	for (auto i = envelope.length; i != 0; --i) {
		count = (count << 8) | uchar(message[envelope.count + i - 1]);
	}
	const auto lengths = message.size() - envelope.head;
	if ((count > caps.messages) || (lengths / envelope.length < count)) {
		return std::nullopt;
	}
	const auto text = lengths - std::size_t(count) * envelope.length;
	if (text > kMaxReportsBytes) {
		return PaintLimit(
			u"%1 bytes of renderer report text"_q.arg(text),
			int64(kMaxReportsBytes));
	}
	return std::nullopt;
}

[[nodiscard]] std::optional<QString> Unfit(
		std::string_view message,
		const Remote::Caps &caps) {
	const auto kind = Remote::kind_of(message);
	if (!kind) {
		return std::nullopt;
	}
	const auto size = int64(message.size());
	switch (*kind) {
	case Remote::Kind::Frame:
	case Remote::Kind::FrameDelta:
		if (message.size() > HostedWire::kMaxFrameBytes) {
			return PaintLimit(
				u"%1 bytes of a frame"_q.arg(size),
				int64(HostedWire::kMaxFrameBytes));
		}
		break;
	case Remote::Kind::CanvasRun:
		if (CanvasRunDecodeBytes(size) > kMaxDecodeBytes) {
			return PaintLimit(
				u"%1 bytes to read a canvas batch"_q.arg(
					CanvasRunDecodeBytes(size)),
				kMaxDecodeBytes);
		}
		break;
	case Remote::Kind::Messages:
		return UnfitReports(message, caps);
	default:
		break;
	}
	return std::nullopt;
}

[[nodiscard]] QString OutOfMemory(std::size_t bytes) {
	return u"Out of memory reading %1 bytes from the widget renderer"_q.arg(
		bytes);
}

template <typename Read>
[[nodiscard]] std::optional<std::invoke_result_t<Read&>> ReadWithin(
		Read &&read) {
	try {
		return read();
	} catch (const std::bad_alloc &) {
		return std::nullopt;
	}
}

[[nodiscard]] QString Utf16(std::string_view text) {
	return QString::fromUtf8(text.data(), qsizetype(text.size()));
}

} // namespace

HostedTraffic::HostedTraffic(
	std::string bytes,
	std::shared_ptr<const void> hold,
	uint64 number,
	Fn<void(std::string)> respond)
: _bytes(std::move(bytes))
, _hold(std::move(hold))
, _respond(std::move(respond))
, _number(number) {
}

HostedTraffic::HostedTraffic(HostedTraffic &&other)
: _bytes(std::move(other._bytes))
, _hold(std::move(other._hold))
, _respond(base::take(other._respond))
, _number(other._number) {
}

HostedTraffic::~HostedTraffic() {
	respond(std::string());
}

bool HostedTraffic::request() const {
	return (_respond != nullptr);
}

uint64 HostedTraffic::number() const {
	return _number;
}

std::string_view HostedTraffic::bytes() const {
	return _bytes;
}

void HostedTraffic::respond(std::string answer) {
	if (const auto respond = base::take(_respond)) {
		respond(std::move(answer));
	}
}

RendererPlatform::RendererPlatform(
	not_null<QtBackend*> backend,
	Route route,
	Reports reports,
	std::shared_ptr<twidget::SandboxHostedWait> wait)
: _backend(backend)
, _route(std::move(route))
, _reports(std::move(reports))
, _wait(std::move(wait))
, _end(
	*backend,
	static_cast<Remote::PlatformEnd::Host&>(*this),
	static_cast<Remote::Channel&>(*this),
	HostedWire::EmbeddingCaps()) {
	_backend->publishWaits(_wait);
}

RendererPlatform::~RendererPlatform() {
	close();
}

void RendererPlatform::open(double dpr) {
	sendControl(HostedWire::EncodeOpen(dpr));
}

uint64 RendererPlatform::requestFrame(bool tick, Remote::Paint paint) {
	return _closed ? 0 : _end.request_frame(tick, paint);
}

void RendererPlatform::setWidth(double width) {
	if (!_closed) {
		_end.set_width(width);
	}
}

void RendererPlatform::setMaxHeight(double height) {
	if (!_closed) {
		_end.set_max_height(height);
	}
}

bool RendererPlatform::touch(const TouchInput &input) {
	if (!accepting()) {
		return false;
	}
	DeliverTouch(_end, input);
	if (_end.refusing()) {
		return false;
	} else if (input.action == RendererRules::kTouchDown) {
		++_downs;
	}
	return true;
}

RendererPlatform::Down RendererPlatform::press(const TouchInput &input) {
	if (!accepting()) {
		return Down::Lost;
	}
	_pressing = true;
	_pressLive = false;
	DeliverTouch(_end, input);
	_pressing = false;
	const auto live = base::take(_pressLive);
	if (_end.refusing()) {
		return Down::Lost;
	}
	++_downs;
	return live ? Down::Live : Down::Sent;
}

void RendererPlatform::released() {
	if (_route.released) {
		_route.released();
	}
}

bool RendererPlatform::hover(int action, QPointF point, int modifiers) {
	if (!accepting()) {
		return false;
	}
	_end.hover(action, point.x(), point.y(), modifiers);
	return !_end.refusing();
}

void RendererPlatform::clearSelection() {
	if (accepting()) {
		_end.clear_selection();
	}
}

uint64 RendererPlatform::askClaim(QPointF point) {
	return accepting() ? _end.ask_claim(point.x(), point.y()) : 0;
}

void RendererPlatform::deliverMenus(uint64 press) {
	sendControl(HostedWire::EncodeMenuDeliver(press));
}

void RendererPlatform::fingerHold(uint64 press) {
	sendControl(HostedWire::EncodeFingerHold(press));
}

void RendererPlatform::fingerRelease(uint64 press) {
	sendControl(HostedWire::EncodeFingerRelease(press));
}

void RendererPlatform::refillCanvasFonts() {
	sendControl(HostedWire::EncodeCanvasRefill());
}

void RendererPlatform::enqueue(HostedTraffic traffic) {
	Expects(OnRenderThread());

	if (_ended) {
		return;
	} else if (traffic.request()) {
		++_requests;
	}
	_queue.push_back(std::move(traffic));
	schedule();
}

void RendererPlatform::afterQueued(Fn<void()> done) {
	Expects(OnRenderThread());

	if (_ended || _queue.empty()) {
		done();
		return;
	}
	_afterQueued = std::move(done);
	_queuedAhead = _queue.size();
}

void RendererPlatform::schedule() {
	if (_scheduled
		|| _ended
		|| (_queue.empty() && _endRefusal.isEmpty())) {
		return;
	}
	_scheduled = true;
	PostToRenderThread(crl::guard(this, [=] {
		pump();
	}));
}

void RendererPlatform::close() {
	_closed = true;
	end();
}

const Remote::State &RendererPlatform::reportedState() const {
	return _end.state();
}

uint64 RendererPlatform::lastDown() const {
	return _downs;
}

bool RendererPlatform::accepting() const {
	return !_closed && !_ended && _route.send && !_end.refusing();
}

bool RendererPlatform::waiting() const {
	return !_ended && !_queue.empty();
}

bool RendererPlatform::requestWaiting() const {
	return !_ended && (_requests > 0);
}

bool RendererPlatform::ended() const {
	return _ended;
}

void RendererPlatform::frame(Frame &&frame) {
	_received.frame = std::move(frame);
}

void RendererPlatform::state(const Remote::State &state) {
	_received.state = state;
}

void RendererPlatform::messages(std::vector<twidget::Json> &&messages) {
	for (const auto &message : messages) {
		const auto &type = message["type"].asString();
		const auto &height = message["height"];
		if (type == "limit") {
			_received.limit = true;
		} else if ((type == "size")
			&& height.isNumber()
			&& std::isfinite(height.asNumber())) {
			_received.heights.push_back(height.asNumber());
		}
	}
}

void RendererPlatform::pointer(const Remote::Pointer &pointer) {
	_received.pointer = pointer;
}

void RendererPlatform::selection(const Remote::Selection &selection) {
	_received.selection = Utf16(selection.text);
}

void RendererPlatform::claim(const Remote::ClaimAnswer &answer) {
	_received.claim = answer;
}

void RendererPlatform::refused(const std::string &reason) {
	if (_endRefusal.isEmpty()) {
		_endRefusal = QString::fromStdString(reason);
	}
	stopLocally(_endRefusal);
	if (!_consuming) {
		schedule();
	}
}

void RendererPlatform::send(std::string message) {
	if (_closed || !_route.send) {
		return;
	} else if (_pressing && _route.press) {
		_pressLive = _route.press(std::move(message));
	} else {
		_route.send(std::move(message));
	}
}

void RendererPlatform::pump() {
	_scheduled = false;
	const auto weak = base::make_weak(this);
	const auto started = crl::now();
	auto turn = 0;
	while (!_ended) {
		if (!_endRefusal.isEmpty()) {
			failEnd();
			return;
		} else if (_afterQueued && !_queuedAhead) {
			base::take(_afterQueued)();
			if (!weak) {
				return;
			}
			continue;
		} else if (_queue.empty()) {
			if (_reports.drained) {
				_reports.drained();
			}
			return;
		} else if ((turn == kPumpEntries)
			|| (crl::now() - started >= kPumpTime)) {
			schedule();
			return;
		}
		const auto work = _backend->workUnlessHeld();
		if (!work.admitted()) {
			if (_reports.held) {
				_reports.held(_backend->heldFor());
			}
			return;
		}
		auto traffic = std::move(_queue.front());
		_queue.pop_front();
		++turn;
		if (_afterQueued) {
			--_queuedAhead;
		}
		if (traffic.request()) {
			--_requests;
		}
		consume(std::move(traffic));
		if (!weak) {
			return;
		}
	}
}

void RendererPlatform::consume(HostedTraffic traffic) {
	const auto weak = base::make_weak(this);
	if (_backend->refused()) {
		fail(_backend->refusal());
	} else if (traffic.request()) {
		answer(traffic);
	} else {
		receive(traffic);
	}
	if (weak && !_ended && _backend->refused()) {
		fail(_backend->refusal());
	}
}

void RendererPlatform::answer(HostedTraffic &traffic) {
	if (_wait && traffic.number()) {
		_wait->admitted(traffic.number());
	}
	auto reply = std::string();
	_consuming = true;
	const auto answered = ReadWithin([&] {
		return _end.answer(traffic.bytes(), reply);
	});
	_consuming = false;
	if (!answered) {
		fail(OutOfMemory(traffic.bytes().size()));
	} else if (*answered) {
		traffic.respond(std::move(reply));
	} else {
		traffic.respond(std::string());
		failEnd();
	}
}

void RendererPlatform::receive(HostedTraffic &traffic) {
	const auto message = traffic.bytes();
	if (HostedWire::IsControl(message)) {
		control(message);
		return;
	} else if (auto limit = Unfit(message, HostedWire::EmbeddingCaps())) {
		fail(std::move(*limit));
		return;
	}
	_consuming = true;
	const auto received = ReadWithin([&] {
		return _end.receive(message);
	});
	_consuming = false;
	if (!received) {
		_received = Received();
		fail(OutOfMemory(message.size()));
		return;
	} else if (!*received) {
		_received = Received();
		failEnd();
		return;
	}
	deliver(base::take(_received));
}

void RendererPlatform::control(std::string_view message) {
	using Kind = HostedWire::Kind;
	const auto kind = HostedWire::KindOf(message);
	if (!kind || !HostedWire::FromHelper(*kind)) {
		fail(u"A control the widget renderer may not send"_q);
	} else if (*kind == Kind::Stopped) {
		const auto reason = HostedWire::DecodeStopped(message);
		if (!reason) {
			fail(u"A renderer stop that did not decode"_q);
		} else if (auto text = ReadWithin([&] { return Utf16(*reason); })) {
			stop(std::move(*text));
		} else {
			fail(OutOfMemory(message.size()));
		}
	} else if (*kind == Kind::Mirrored) {
		if (!HostedWire::DecodeMirrored(message)) {
			fail(u"A mirrored record that did not decode"_q);
		}
	} else if (*kind == Kind::Cursor) {
		const auto report = HostedWire::DecodeCursor(message);
		if (!report) {
			fail(u"A cursor report that did not decode"_q);
		} else if (report->pointers != _pointers) {
			fail(u"A cursor report after %1 pointer replies, not %2"_q.arg(
				report->pointers).arg(_pointers));
		} else if (_reports.cursor) {
			_reports.cursor(report->cursor);
		}
	} else if (*kind == Kind::Clicked) {
		const auto press = HostedWire::DecodeClicked(message);
		if (!press) {
			fail(u"A click report that did not decode"_q);
		} else if (_reports.clicked) {
			_reports.clicked(*press);
		}
	} else if (!HostedWire::IsExactly(message, *kind)) {
		fail(u"A renderer control that did not decode"_q);
	} else if (*kind == Kind::CanvasRefused) {
		_backend->noteCanvasFontRefused();
	} else if (*kind == Kind::Content) {
		if (std::exchange(_content, true)) {
			fail(u"A second content report"_q);
		} else if (_reports.content) {
			_reports.content();
		}
	} else if (!_content || std::exchange(_loaded, true)) {
		fail(u"A load report out of order"_q);
	} else if (_reports.loaded) {
		_reports.loaded();
	}
}

void RendererPlatform::deliver(Received received) {
	if (received.frame) {
		if (_reports.frame) {
			_reports.frame(std::move(*received.frame));
		}
		return;
	} else if (received.state) {
		if (_reports.state) {
			_reports.state(*received.state);
		}
		return;
	} else if (received.pointer) {
		++_pointers;
		if (_reports.pointer) {
			_reports.pointer(*received.pointer);
		}
		return;
	} else if (received.selection) {
		if (_reports.selection) {
			_reports.selection(std::move(*received.selection));
		}
		return;
	} else if (received.claim) {
		if (_reports.claim) {
			_reports.claim(*received.claim);
		}
		return;
	}
	const auto weak = base::make_weak(this);
	for (const auto height : received.heights) {
		if (!weak || _ended || !_reports.size) {
			return;
		}
		_reports.size(height);
	}
	if (received.limit && weak && !_ended && _reports.limit) {
		_reports.limit();
	}
}

void RendererPlatform::sendControl(std::string message) {
	if (accepting()) {
		_route.send(std::move(message));
	}
}

void RendererPlatform::stopLocally(const QString &reason) {
	if (_route.localStop) {
		_route.localStop(reason);
	}
}

void RendererPlatform::fail(QString reason) {
	if (_ended) {
		return;
	}
	stopLocally(reason);
	end();
	if (_reports.failed) {
		_reports.failed(std::move(reason));
	}
}

void RendererPlatform::failEnd() {
	fail(_endRefusal.isEmpty()
		? u"The widget renderer's platform end refused"_q
		: _endRefusal);
}

void RendererPlatform::stop(QString reason) {
	if (_ended) {
		return;
	}
	stopLocally(reason);
	end();
	if (_reports.stopped) {
		_reports.stopped(std::move(reason));
	}
}

void RendererPlatform::end() {
	_ended = true;
	_requests = 0;
	_queuedAhead = 0;
	_afterQueued = nullptr;
	_queue.clear();
	_end.close();
}

} // namespace Tgw
