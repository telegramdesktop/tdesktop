/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "tgw/tgw_hosted_wire.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// The helper work record that the widget helper's Debug renderer tap
// (twidget's Rust serving, rust/crates/helper: tap.rs and forward.rs)
// writes and Telegram's hosted tap reads. Qt-free and header-only.
// Reference: docs/helpers/test_widget_renderer_forward.md.

namespace Test {

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
