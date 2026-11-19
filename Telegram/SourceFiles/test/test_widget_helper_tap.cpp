/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "test/test_widget_helper_tap.h"

#ifdef _DEBUG

#include "core/src/remote.h"
#include "test/test_widget_renderer_forward.h"
#include "tgw/tgw_hosted_wire.h"
#include "twidget/remote_hosted.h"

#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>

namespace Test {
namespace {

namespace HostedWire = Tgw::HostedWire;
namespace Remote = twlite::remote;

// The served host's own prefix of every validated batch it sends.
constexpr auto kOpsPrefix = std::string_view("{\"type\":\"ops\",");

// Shared by the wrapper, its observer, the timed platform and the chained
// platform handler, read and written on the glue thread alone.
struct TapState {
	std::shared_ptr<twidget::SandboxHostedPlatform> platform;
	std::string factoryScheme;
	std::string scheme;
	// The outermost operation being timed, while `depth` is above 0.
	std::optional<HelperWorkRecord> work;
	int depth = 0;
	bool armed = false;
	bool timing = false;
};

// What |build| gets for the platform: |state|'s platform, with each
// request() timed into the operation being timed.
class TimedPlatform final : public twidget::SandboxHostedPlatform {
public:
	explicit TimedPlatform(std::shared_ptr<TapState> state);

	void send(const std::string &message) override;
	std::optional<std::string> request(const std::string &message) override;

private:
	const std::shared_ptr<TapState> _state;

};

// One outermost operation of the glue thread, timed when the tap times.
class WorkScope final {
public:
	WorkScope(TapState &state, HelperWorkKind kind, std::uint64_t seq);
	WorkScope(const WorkScope &other) = delete;
	WorkScope &operator=(const WorkScope &other) = delete;
	~WorkScope();

private:
	TapState &_state;

};

class HelperObserver final : public RendererObserver {
public:
	explicit HelperObserver(std::shared_ptr<TapState> state);

	void calling(RendererCall call, const std::string &text) override;
	void called(RendererCall call) override;
	void received(const std::string &json) override;

private:
	const std::shared_ptr<TapState> _state;

};

[[nodiscard]] std::int64_t SteadyMicros() {
	using namespace std::chrono;
	return duration_cast<microseconds>(
		steady_clock::now().time_since_epoch()).count();
}

[[nodiscard]] bool IsOps(std::string_view json) {
	return (json.substr(0, kOpsPrefix.size()) == kOpsPrefix);
}

void Mirror(const TapState &state, char way, std::string_view text) {
	if (!state.armed) {
		return;
	} else if (const auto bytes = HostedWire::EncodeMirrored(way, text)) {
		state.platform->send(*bytes);
	}
}

void Arm(TapState &state, bool timing) {
	state.timing = state.timing || timing;
	if (std::exchange(state.armed, true)) {
		return;
	}
	Mirror(state, HostedWire::kMirroredBuilt, state.factoryScheme);
	if (state.scheme != state.factoryScheme) {
		Mirror(state, HostedWire::kMirroredTheme, state.scheme);
	}
}

void BeginWork(TapState &state, HelperWorkKind kind, std::uint64_t seq) {
	if (state.depth++ || !state.timing) {
		return;
	}
	state.work = HelperWorkRecord{
		.started = SteadyMicros(),
		.seq = seq,
		.kind = kind,
	};
}

// An operation that threw sends no record: no send while unwinding.
void EndWork(TapState &state) {
	if (--state.depth || !state.work) {
		return;
	}
	auto record = std::move(*state.work);
	state.work = std::nullopt;
	record.finished = SteadyMicros();
	if (!std::uncaught_exceptions()) {
		Mirror(state, HostedWire::kMirroredWork, EncodeHelperWork(record));
	}
}

[[nodiscard]] HelperWorkKind PlatformKind(
		std::string_view message,
		std::uint64_t &seq) {
	if (HostedWire::IsControl(message)) {
		return HelperWorkKind::Control;
	} else if (Remote::kind_of(message) != Remote::Kind::FrameRequest) {
		return HelperWorkKind::Platform;
	}
	auto request = Remote::FrameRequest();
	if (!Remote::decode(message, HostedWire::EmbeddingCaps(), request)) {
		return HelperWorkKind::Platform;
	}
	seq = request.seq;
	return (request.paint != Remote::Paint::Skip)
		? HelperWorkKind::Paint
		: request.tick
		? HelperWorkKind::Tick
		: HelperWorkKind::Platform;
}

void Chained(
		const std::weak_ptr<TapState> &weak,
		const std::weak_ptr<TimedPlatform> &timed,
		const std::string &message) {
	const auto state = weak.lock();
	if (state && HostedWire::IsExactly(message, HostedWire::Kind::Mirror)) {
		Arm(*state, false);
		return;
	} else if (state
		&& HostedWire::IsExactly(message, HostedWire::Kind::MirrorWork)) {
		Arm(*state, true);
		return;
	}
	const auto platform = timed.lock();
	if (!platform || !platform->onMessage) {
		return;
	} else if (!state) {
		platform->onMessage(message);
		return;
	}
	auto seq = std::uint64_t();
	const auto kind = PlatformKind(message, seq);
	const auto scope = WorkScope(*state, kind, seq);
	platform->onMessage(message);
}

WorkScope::WorkScope(
	TapState &state,
	HelperWorkKind kind,
	std::uint64_t seq)
: _state(state) {
	BeginWork(_state, kind, seq);
}

WorkScope::~WorkScope() {
	EndWork(_state);
}

TimedPlatform::TimedPlatform(std::shared_ptr<TapState> state)
: _state(std::move(state)) {
}

void TimedPlatform::send(const std::string &message) {
	if (_state->work && (Remote::kind_of(message) == Remote::Kind::Frame)) {
		_state->work->frameBytes += std::uint32_t(message.size());
	}
	_state->platform->send(message);
}

std::optional<std::string> TimedPlatform::request(
		const std::string &message) {
	if (!_state->work) {
		return _state->platform->request(message);
	}
	const auto started = SteadyMicros();
	auto result = _state->platform->request(message);
	_state->work->waited += SteadyMicros() - started;
	++_state->work->waits;
	return result;
}

HelperObserver::HelperObserver(std::shared_ptr<TapState> state)
: _state(std::move(state)) {
}

void HelperObserver::calling(RendererCall call, const std::string &text) {
	auto &state = *_state;
	if (call == RendererCall::Send) {
		if (!IsOps(text)) {
			Mirror(state, HostedWire::kMirroredHost, text);
		}
		BeginWork(state, HelperWorkKind::Message, 0);
	} else if (call == RendererCall::SetTheme) {
		state.scheme = text;
		Mirror(state, HostedWire::kMirroredTheme, text);
		BeginWork(state, HelperWorkKind::Theme, 0);
	}
}

void HelperObserver::called(RendererCall call) {
	if ((call == RendererCall::Send) || (call == RendererCall::SetTheme)) {
		EndWork(*_state);
	}
}

void HelperObserver::received(const std::string &json) {
	Mirror(*_state, HostedWire::kMirroredRenderer, json);
}

} // namespace

std::unique_ptr<twidget::Renderer> TapHelperRenderer(
		std::shared_ptr<twidget::SandboxHostedPlatform> platform,
		const std::string &scheme,
		const HelperRendererBuild &build) {
	if (!platform) {
		return build(std::move(platform));
	}
	const auto state = std::make_shared<TapState>(TapState{
		.platform = platform,
		.factoryScheme = scheme,
		.scheme = scheme,
	});
	const auto timed = std::make_shared<TimedPlatform>(state);
	auto renderer = build(timed);
	if (!renderer) {
		return renderer;
	}
	platform->onMessage = [
		weak = std::weak_ptr<TapState>(state),
		timed = std::weak_ptr<TimedPlatform>(timed)
	](const std::string &message) {
		Chained(weak, timed, message);
	};
	return std::make_unique<ForwardingRenderer>(
		std::move(renderer),
		std::make_unique<HelperObserver>(state));
}

} // namespace Test

#endif // _DEBUG
