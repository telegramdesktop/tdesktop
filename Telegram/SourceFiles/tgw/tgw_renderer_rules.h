/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/src/renderer.h"
#include "twidget/gesture_policy.h"
#include "twidget/json.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Tgw::RendererRules {

inline constexpr auto kReadyMessage = "{\"type\":\"ready\"}";
inline constexpr auto kMinRatio = 0.25;
inline constexpr auto kMaxRatio = 64.;
inline constexpr auto kTouchDown = 0;
inline constexpr auto kTouchMove = 1;
inline constexpr auto kTouchUp = 2;
inline constexpr auto kTouchCancel = 3;
inline constexpr auto kHoverMove = 0;
inline constexpr auto kHoverLeave = 1;
inline constexpr auto kMousePointer = 0;
inline constexpr auto kFingerPointer = 1;
inline constexpr auto kPrimaryButton = 0;
inline constexpr auto kSecondaryButton = 2;
inline constexpr auto kLocalQueryId = 0;
// Mirrors the private twidget::WidgetHost::kRenderAckQid, its post-load query.
inline constexpr auto kLoadAckQueryId = -1;

inline constexpr auto kMaxNewCanvasFonts = 16;
inline constexpr auto kMaxCanvasFonts = 32;
inline constexpr auto kMaxCanvasFontParses = 64;
inline constexpr auto kMaxCanvasFontRequests = 64;
inline constexpr auto kMaxCanvasFontSpellings = std::size_t(64);
inline constexpr auto kMaxCanvasFontSpellingSize = std::size_t(512);
inline constexpr auto kCanvasFontLimitKind = std::string_view(
	"canvas-font-limit");

struct LoadChange {
	bool content = false;
	bool loaded = false;
};

struct HoverTargets {
	std::int64_t hover = 0;
	std::int64_t active = 0;
};

template <typename Entry>
class CanvasSpellings final {
public:
	template <typename Parse>
	[[nodiscard]] Entry *find(const std::string &css, Parse &&parse);
	void refill();

private:
	std::unordered_map<std::string, Entry> _entries;
	int _parsesLeft = kMaxCanvasFontParses;

};

template <typename Entry>
template <typename Parse>
Entry *CanvasSpellings<Entry>::find(const std::string &css, Parse &&parse) {
	if (css.size() > kMaxCanvasFontSpellingSize) {
		return nullptr;
	}
	const auto i = _entries.find(css);
	if (i != _entries.end()) {
		return &i->second;
	} else if (_parsesLeft <= 0) {
		return nullptr;
	}
	--_parsesLeft;
	auto entry = parse(css);
	if (_entries.size() >= kMaxCanvasFontSpellings) {
		// Its entries own no fonts, so clearing it mid-message is safe.
		_entries.clear();
	}
	return &_entries.emplace(css, std::move(entry)).first->second;
}

template <typename Entry>
void CanvasSpellings<Entry>::refill() {
	_parsesLeft = kMaxCanvasFontParses;
}

[[nodiscard]] inline double DeviceRatio(double ratio) {
	return std::isnan(ratio)
		? kMinRatio
		: std::clamp(ratio, kMinRatio, kMaxRatio);
}

[[nodiscard]] inline bool OutsideSlop(double dx, double dy) {
	return std::hypot(dx, dy) > twidget::gesture::TOUCH_SLOP_PX;
}

[[nodiscard]] inline int ParsedDocuments(const twlite::Renderer &renderer) {
	const auto &stats = renderer.stats();
	return stats.builds - stats.patched;
}

[[nodiscard]] inline HoverTargets PointerTargets(
		std::int64_t hit,
		bool pressHeld,
		bool synced,
		std::int64_t pressHit,
		bool active) {
	const auto hover = pressHeld ? (synced ? pressHit : 0) : hit;
	return {
		.hover = hover,
		.active = (synced && active && (hover == pressHit)) ? hover : 0,
	};
}

[[nodiscard]] inline auto OfType(std::string_view type) {
	return [=](const twidget::Json &message) {
		return (message["type"].asString() == type);
	};
}

[[nodiscard]] inline twidget::Json FrameMessage() {
	auto result = twidget::Json::object();
	result.set("type", "frame");
	result.set("time", twlite::Renderer::clock() * 1000.);
	return result;
}

[[nodiscard]] inline twidget::Json LocalQuery(twidget::Json query) {
	auto result = twidget::Json::object();
	result.set("type", "query");
	result.set("qid", kLocalQueryId);
	result.set("query", std::move(query));
	return result;
}

[[nodiscard]] inline twidget::Json ViewportQuery() {
	auto query = twidget::Json::object();
	query.set("q", "viewport");
	return LocalQuery(std::move(query));
}

[[nodiscard]] inline bool KeptBeforeReady(const twidget::Json &message) {
	return OfType("size")(message);
}

[[nodiscard]] inline bool DrawsContent(const twidget::Json &message) {
	return OfType("ops")(message) && !message["ops"].arr().empty();
}

[[nodiscard]] inline bool IsLoadAck(const twidget::Json &message) {
	const auto &qid = message["qid"];
	return OfType("query")(message)
		&& qid.isNumber()
		&& (qid.asInt() == kLoadAckQueryId);
}

[[nodiscard]] inline LoadChange AdvanceLoad(
		const twidget::Json &message,
		bool &hasContent,
		bool &acked,
		bool &loaded) {
	auto result = LoadChange();
	if (DrawsContent(message)) {
		result.content = !hasContent;
		hasContent = true;
	} else if (IsLoadAck(message)) {
		acked = true;
	}
	if (acked && hasContent && !loaded) {
		loaded = true;
		result.loaded = true;
	}
	return result;
}

[[nodiscard]] inline bool IsContextMenu(
		const twidget::Json &message,
		std::string_view pointerType) {
	const auto &event = message["ev"];
	return OfType("event")(message)
		&& (event["type"].asString() == "contextmenu")
		&& (event["pointerType"].asString() == pointerType);
}

[[nodiscard]] inline bool IsClick(const twidget::Json &message) {
	return OfType("event")(message)
		&& (message["ev"]["type"].asString() == "click");
}

[[nodiscard]] inline std::vector<twidget::Json> TakeContextMenus(
		std::vector<twidget::Json> &out,
		std::string_view pointerType) {
	const auto menus = std::stable_partition(
		out.begin(),
		out.end(),
		[&](const twidget::Json &message) {
			return !IsContextMenu(message, pointerType);
		});
	auto result = std::vector<twidget::Json>(
		std::make_move_iterator(menus),
		std::make_move_iterator(out.end()));
	out.erase(menus, out.end());
	return result;
}

inline void Append(
		std::vector<twidget::Json> &to,
		std::vector<twidget::Json> &&from) {
	to.insert(
		to.end(),
		std::make_move_iterator(from.begin()),
		std::make_move_iterator(from.end()));
}

template <typename Receiver>
[[nodiscard]] std::vector<twidget::Json> TakeSettled(Receiver &receiver) {
	auto out = receiver.receive(ViewportQuery());
	out.erase(
		std::remove_if(out.begin(), out.end(), OfType("result")),
		out.end());
	return out;
}

[[nodiscard]] inline std::string CanvasFontLimitText() {
	return "canvas text exceeded the allowance of "
		+ std::to_string(kMaxNewCanvasFonts)
		+ " new fonts per frame, "
		+ std::to_string(kMaxCanvasFonts)
		+ " per widget, "
		+ std::to_string(kMaxCanvasFontParses)
		+ " font strings per frame of at most "
		+ std::to_string(kMaxCanvasFontSpellingSize)
		+ " bytes; text in other fonts is skipped";
}

} // namespace Tgw::RendererRules
