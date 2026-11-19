/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "tgw/tgw_hosted_wire.h"
#include "twidget/renderer.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

// What both renderer taps share: the forwarding half of the validated-ops
// tap's wrapper in Telegram (test_widget_ops.cpp) and of the widget helper's
// Debug tap (test_widget_helper_tap.cpp), and the helper work record that
// the helper's tap writes and Telegram's tap reads. Qt-free and header-only,
// and listed by both targets, so the standalone helper compiles it as
// Telegram does and neither includes a header of the other's target.
// Reference: docs/helpers/test_widget_renderer_forward.md.

namespace Test {

enum class RendererCall {
	Load,
	Send,
	SetTheme,
	SetWidth,
	Destroy,
};

// What one tap records around the renderer it wraps. Every hook runs on the
// thread of the call or callback it observes and must not call into the
// renderer; the defaults do nothing.
class RendererObserver {
public:
	virtual ~RendererObserver() = default;

	// Right before a call is forwarded: |text| is the message of Send and
	// the theme of SetTheme, empty for the other calls.
	virtual void calling(RendererCall call, const std::string &text);
	// Right after the forwarded call returned or threw: every calling()
	// gets its called().
	virtual void called(RendererCall call);
	// A renderer -> host message, before the wrapper's onMessage gets it.
	virtual void received(const std::string &json);

};

// Forwards every call to |inner| unchanged, on the calling thread, and
// chains |inner|'s onMessage and onDiag to its own, which WidgetHost (or the
// served host in the widget helper) assigns after the factory returned, so
// they are read when invoked, never when installed. Destroying it destroys
// |inner| first, which waits for a callback in flight, and only then
// |observer|: no hook runs on a destroyed observer.
class ForwardingRenderer final : public twidget::Renderer {
public:
	ForwardingRenderer(
		std::unique_ptr<twidget::Renderer> inner,
		std::unique_ptr<RendererObserver> observer);
	ForwardingRenderer(const ForwardingRenderer &other) = delete;
	ForwardingRenderer &operator=(const ForwardingRenderer &other) = delete;
	~ForwardingRenderer();

	void load() override;
	void send(const std::string &json) override;
	void setTheme(const std::string &theme) override;
	void setWidth(double width) override;
	void destroy() override;

private:
	template <typename Forward>
	void forward(RendererCall call, const std::string &text, Forward method);

	std::unique_ptr<RendererObserver> _observer;
	std::unique_ptr<twidget::Renderer> _inner;

};

inline void RendererObserver::calling(RendererCall, const std::string &) {
}

inline void RendererObserver::called(RendererCall) {
}

inline void RendererObserver::received(const std::string &) {
}

inline ForwardingRenderer::ForwardingRenderer(
	std::unique_ptr<twidget::Renderer> inner,
	std::unique_ptr<RendererObserver> observer)
: _observer(std::move(observer))
, _inner(std::move(inner)) {
	_inner->onMessage = [this](const std::string &json) {
		_observer->received(json);
		if (onMessage) {
			onMessage(json);
		}
	};
	_inner->onDiag = [this](
			const std::string &kind,
			const std::string &detail) {
		if (onDiag) {
			onDiag(kind, detail);
		}
	};
}

inline ForwardingRenderer::~ForwardingRenderer() {
	_inner = nullptr;
	_observer = nullptr;
}

template <typename Forward>
void ForwardingRenderer::forward(
		RendererCall call,
		const std::string &text,
		Forward method) {
	struct Called {
		RendererObserver &observer;
		RendererCall call = RendererCall::Load;

		~Called() {
			observer.called(call);
		}
	};
	_observer->calling(call, text);
	const auto called = Called{ *_observer, call };
	method(*_inner);
}

inline void ForwardingRenderer::load() {
	forward(RendererCall::Load, std::string(), [](twidget::Renderer &inner) {
		inner.load();
	});
}

inline void ForwardingRenderer::send(const std::string &json) {
	forward(RendererCall::Send, json, [&](twidget::Renderer &inner) {
		inner.send(json);
	});
}

inline void ForwardingRenderer::setTheme(const std::string &theme) {
	forward(RendererCall::SetTheme, theme, [&](twidget::Renderer &inner) {
		inner.setTheme(theme);
	});
}

inline void ForwardingRenderer::setWidth(double width) {
	forward(RendererCall::SetWidth, std::string(), [&](
			twidget::Renderer &inner) {
		inner.setWidth(width);
	});
}

inline void ForwardingRenderer::destroy() {
	forward(RendererCall::Destroy, std::string(), [](
			twidget::Renderer &inner) {
		inner.destroy();
	});
}

// What one helper renderer operation was, by the call that started it on
// the served host's glue thread.
enum class HelperWorkKind : std::uint8_t {
	// A served-host -> renderer message: TwidgetCore's receive(), a
	// validated batch applied or a layout query answered, with the style
	// and layout they need.
	Message = 1,
	// The served host's setTheme(): the scheme applied to the renderer.
	Theme = 2,
	// A FrameRequest that ticks and does not paint: TwidgetCore's tick(),
	// its animations and the build they need.
	Tick = 3,
	// A FrameRequest that paints: the settle, style, layout, the display
	// list build and the Frame's encoding.
	Paint = 4,
	// Any other TwidgetCore platform message: input, sizes, selection and
	// claims.
	Platform = 5,
	// A glue control: Open (the renderer end's construction, its theme and
	// first layout) and the press controls.
	Control = 6,
};

// One way 'w' Mirrored record: an outermost renderer operation of one
// widget, timed on the helper's steady clock (microseconds since its
// epoch; on macOS the system-wide uptime clock both processes read).
struct HelperWorkRecord {
	std::int64_t started = 0;
	std::int64_t finished = 0;
	// Microseconds of [started, finished] spent inside the platform's
	// request(), waiting for Telegram's answers, and how many.
	std::int64_t waited = 0;
	// The FrameRequest's seq for Tick and Paint, else 0.
	std::uint64_t seq = 0;
	std::uint32_t waits = 0;
	// Bytes of the Frame messages the operation sent, 0 for none.
	std::uint32_t frameBytes = 0;
	HelperWorkKind kind = HelperWorkKind::Message;
};

inline constexpr auto kHelperWorkRecordBytes = std::size_t(1 + 8 * 4 + 4 * 2);

// The longest interval one record may span, in microseconds: an hour, far
// past any renderer operation and far below what a sum of a full record
// room of them could overflow.
inline constexpr auto kMaxHelperInterval = std::int64_t(3600) * 1000 * 1000;

[[nodiscard]] inline std::string EncodeHelperWork(
		const HelperWorkRecord &record) {
	namespace details = Tgw::HostedWire::details;
	auto result = std::string();
	result.reserve(kHelperWorkRecordBytes);
	result.push_back(char(std::uint8_t(record.kind)));
	details::PutNumber(result, std::uint64_t(record.started), 8);
	details::PutNumber(result, std::uint64_t(record.finished), 8);
	details::PutNumber(result, std::uint64_t(record.waited), 8);
	details::PutNumber(result, record.seq, 8);
	details::PutNumber(result, record.waits, 4);
	details::PutNumber(result, record.frameBytes, 4);
	return result;
}

// Exactly kHelperWorkRecordBytes, a known kind, 0 <= started <= finished
// with finished - started at most kMaxHelperInterval, and 0 <= waited <=
// finished - started; nullopt otherwise. Each check comes before the
// arithmetic it guards, so no stamp of any value overflows here.
[[nodiscard]] inline std::optional<HelperWorkRecord> DecodeHelperWork(
		std::string_view text) {
	namespace details = Tgw::HostedWire::details;
	if (text.size() != kHelperWorkRecordBytes) {
		return std::nullopt;
	}
	const auto kind = std::uint8_t(text.front());
	text.remove_prefix(1);
	auto result = HelperWorkRecord();
	result.started = std::int64_t(details::TakeNumber(text, 8));
	result.finished = std::int64_t(details::TakeNumber(text, 8));
	result.waited = std::int64_t(details::TakeNumber(text, 8));
	result.seq = details::TakeNumber(text, 8);
	result.waits = std::uint32_t(details::TakeNumber(text, 4));
	result.frameBytes = std::uint32_t(details::TakeNumber(text, 4));
	if ((kind < std::uint8_t(HelperWorkKind::Message))
		|| (kind > std::uint8_t(HelperWorkKind::Control))
		|| (result.started < 0)
		|| (result.finished < result.started)) {
		return std::nullopt;
	}
	const auto interval = result.finished - result.started;
	if ((interval > kMaxHelperInterval)
		|| (result.waited < 0)
		|| (result.waited > interval)) {
		return std::nullopt;
	}
	result.kind = HelperWorkKind(kind);
	return result;
}

} // namespace Test
