/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/src/remote.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace Tgw::HostedWire {

enum class Kind : std::uint8_t {
	Open = 1,
	MenuDeliver = 2,
	FingerHold = 3,
	FingerRelease = 4,
	Mirror = 5,
	MirrorWork = 6,
	CanvasRefill = 7,

	Content = 64,
	Loaded = 65,
	Stopped = 66,
	Mirrored = 67,
	Cursor = 68,
	CanvasRefused = 69,
	Clicked = 70,
};

inline constexpr auto kTag = std::uint8_t(0xFF);
inline constexpr auto kHeaderBytes = std::size_t(2);
inline constexpr auto kNumberBytes = std::size_t(8);
inline constexpr auto kLengthBytes = std::size_t(4);
inline constexpr auto kMaxMessageBytes = std::size_t(32) << 20;
inline constexpr auto kMaxListCommands = std::size_t(1) << 16;
inline constexpr auto kMaxFrameBytes = std::size_t(8) << 20;
inline constexpr auto kMaxCanvasRunOps = std::size_t(1) << 13;
inline constexpr auto kMaxWidthsTexts = std::size_t(1024);
inline constexpr auto kMaxWidthsTextBytes = std::size_t(64) << 10;
inline constexpr auto kMaxDeltaBaseBytes = std::size_t(256) << 10;
inline constexpr auto kMaxStopReasonBytes = std::size_t(4096);
inline constexpr auto kMaxCursorBytes = std::size_t(256);

inline constexpr auto kMirroredBuilt = 'b';
inline constexpr auto kMirroredHost = 'h';
inline constexpr auto kMirroredRenderer = 'r';
inline constexpr auto kMirroredTheme = 't';
inline constexpr auto kMirroredWork = 'w';

inline constexpr auto kMirroredOverheadBytes = kHeaderBytes
	+ 1
	+ kLengthBytes;
inline constexpr auto kMaxMirroredTextBytes = kMaxMessageBytes
	- kMirroredOverheadBytes;

struct MirroredRecord {
	char way = 0;
	std::string_view text;
};

struct CursorReport {
	std::uint64_t pointers = 0;
	std::string_view cursor;
};

[[nodiscard]] inline twlite::remote::Caps EmbeddingCaps() {
	auto result = twlite::remote::Caps();
	result.message = kMaxMessageBytes;
	result.values.commands = kMaxListCommands;
	result.values.canvas_ops = kMaxCanvasRunOps;
	result.widths = kMaxWidthsTexts;
	result.widths_text = kMaxWidthsTextBytes;
	result.frame = kMaxFrameBytes;
	result.delta_base = kMaxDeltaBaseBytes;
	return result;
}

[[nodiscard]] inline bool IsControl(std::string_view message) {
	return !message.empty() && (std::uint8_t(message.front()) == kTag);
}

[[nodiscard]] inline bool ToHelper(Kind kind) {
	switch (kind) {
	case Kind::Open:
	case Kind::MenuDeliver:
	case Kind::FingerHold:
	case Kind::FingerRelease:
	case Kind::Mirror:
	case Kind::MirrorWork:
	case Kind::CanvasRefill:
		return true;
	default:
		return false;
	}
}

[[nodiscard]] inline bool IsTapControl(Kind kind) {
	return (kind == Kind::Mirror) || (kind == Kind::MirrorWork);
}

[[nodiscard]] inline bool FromHelper(Kind kind) {
	switch (kind) {
	case Kind::Content:
	case Kind::Loaded:
	case Kind::Stopped:
	case Kind::Mirrored:
	case Kind::Cursor:
	case Kind::CanvasRefused:
	case Kind::Clicked:
		return true;
	default:
		return false;
	}
}

[[nodiscard]] inline std::optional<Kind> KindOf(std::string_view message) {
	if (message.size() < kHeaderBytes || !IsControl(message)) {
		return std::nullopt;
	}
	const auto kind = Kind(std::uint8_t(message[1]));
	if (!ToHelper(kind) && !FromHelper(kind)) {
		return std::nullopt;
	}
	return kind;
}

[[nodiscard]] inline bool IsExactly(std::string_view message, Kind kind) {
	return (message.size() == kHeaderBytes) && (KindOf(message) == kind);
}

[[nodiscard]] inline bool IsMirroredWay(char way) {
	return (way == kMirroredBuilt)
		|| (way == kMirroredHost)
		|| (way == kMirroredRenderer)
		|| (way == kMirroredTheme)
		|| (way == kMirroredWork);
}

namespace details {

[[nodiscard]] inline std::string Header(Kind kind, std::size_t body) {
	auto result = std::string();
	result.reserve(kHeaderBytes + body);
	result.push_back(char(kTag));
	result.push_back(char(std::uint8_t(kind)));
	return result;
}

inline void PutNumber(
		std::string &to,
		std::uint64_t value,
		std::size_t bytes) {
	for (auto i = std::size_t(); i != bytes; ++i) {
		to.push_back(char(std::uint8_t(value >> (8 * i))));
	}
}

[[nodiscard]] inline std::uint64_t TakeNumber(
		std::string_view &from,
		std::size_t bytes) {
	auto result = std::uint64_t();
	for (auto i = std::size_t(); i != bytes; ++i) {
		result |= std::uint64_t(std::uint8_t(from[i])) << (8 * i);
	}
	from.remove_prefix(bytes);
	return result;
}

[[nodiscard]] inline std::string WithNumber(Kind kind, std::uint64_t value) {
	auto result = Header(kind, kNumberBytes);
	PutNumber(result, value, kNumberBytes);
	return result;
}

[[nodiscard]] inline std::optional<std::uint64_t> NumberOf(
		std::string_view message,
		Kind kind) {
	if ((message.size() != kHeaderBytes + kNumberBytes)
		|| (KindOf(message) != kind)) {
		return std::nullopt;
	}
	message.remove_prefix(kHeaderBytes);
	return TakeNumber(message, kNumberBytes);
}

[[nodiscard]] inline std::optional<std::string_view> TextOf(
		std::string_view &body,
		std::size_t limit) {
	if (body.size() < kLengthBytes) {
		return std::nullopt;
	}
	const auto length = TakeNumber(body, kLengthBytes);
	if ((length > limit) || (length != body.size())) {
		return std::nullopt;
	}
	return body;
}

} // namespace details

[[nodiscard]] inline std::string EncodeOpen(double dpr) {
	auto bits = std::uint64_t();
	static_assert(sizeof(bits) == sizeof(dpr));
	std::memcpy(&bits, &dpr, sizeof(bits));
	return details::WithNumber(Kind::Open, bits);
}

[[nodiscard]] inline std::optional<double> DecodeOpen(
		std::string_view message) {
	const auto bits = details::NumberOf(message, Kind::Open);
	if (!bits) {
		return std::nullopt;
	}
	auto result = 0.;
	std::memcpy(&result, &*bits, sizeof(result));
	if (!std::isfinite(result)) {
		return std::nullopt;
	}
	return result;
}

[[nodiscard]] inline std::string EncodeMenuDeliver(std::uint64_t press) {
	return details::WithNumber(Kind::MenuDeliver, press);
}

[[nodiscard]] inline std::string EncodeFingerHold(std::uint64_t press) {
	return details::WithNumber(Kind::FingerHold, press);
}

[[nodiscard]] inline std::string EncodeFingerRelease(std::uint64_t press) {
	return details::WithNumber(Kind::FingerRelease, press);
}

[[nodiscard]] inline std::optional<std::uint64_t> DecodePress(
		std::string_view message) {
	const auto kind = KindOf(message);
	if ((kind != Kind::MenuDeliver)
		&& (kind != Kind::FingerHold)
		&& (kind != Kind::FingerRelease)) {
		return std::nullopt;
	}
	return details::NumberOf(message, *kind);
}

[[nodiscard]] inline std::string EncodeClicked(std::uint64_t press) {
	return details::WithNumber(Kind::Clicked, press);
}

[[nodiscard]] inline std::optional<std::uint64_t> DecodeClicked(
		std::string_view message) {
	return details::NumberOf(message, Kind::Clicked);
}

[[nodiscard]] inline std::string EncodeMirror() {
	return details::Header(Kind::Mirror, 0);
}

[[nodiscard]] inline std::string EncodeMirrorWork() {
	return details::Header(Kind::MirrorWork, 0);
}

[[nodiscard]] inline std::string EncodeCanvasRefill() {
	return details::Header(Kind::CanvasRefill, 0);
}

[[nodiscard]] inline std::string EncodeCanvasRefused() {
	return details::Header(Kind::CanvasRefused, 0);
}

[[nodiscard]] inline std::string EncodeContent() {
	return details::Header(Kind::Content, 0);
}

[[nodiscard]] inline std::string EncodeLoaded() {
	return details::Header(Kind::Loaded, 0);
}

[[nodiscard]] inline std::string EncodeStopped(std::string_view reason) {
	const auto text = reason.substr(0, kMaxStopReasonBytes);
	auto result = details::Header(Kind::Stopped, kLengthBytes + text.size());
	details::PutNumber(result, text.size(), kLengthBytes);
	result.append(text);
	return result;
}

[[nodiscard]] inline std::optional<std::string_view> DecodeStopped(
		std::string_view message) {
	if ((message.size() > kHeaderBytes + kLengthBytes + kMaxStopReasonBytes)
		|| (KindOf(message) != Kind::Stopped)) {
		return std::nullopt;
	}
	auto body = message.substr(kHeaderBytes);
	return details::TextOf(body, kMaxStopReasonBytes);
}

[[nodiscard]] inline std::string EncodeCursor(
		std::uint64_t pointers,
		std::string_view cursor) {
	const auto text = cursor.substr(0, kMaxCursorBytes);
	auto result = details::Header(
		Kind::Cursor,
		kNumberBytes + kLengthBytes + text.size());
	details::PutNumber(result, pointers, kNumberBytes);
	details::PutNumber(result, text.size(), kLengthBytes);
	result.append(text);
	return result;
}

[[nodiscard]] inline std::optional<CursorReport> DecodeCursor(
		std::string_view message) {
	const auto fixed = kHeaderBytes + kNumberBytes + kLengthBytes;
	if ((message.size() < fixed)
		|| (message.size() > fixed + kMaxCursorBytes)
		|| (KindOf(message) != Kind::Cursor)) {
		return std::nullopt;
	}
	auto body = message.substr(kHeaderBytes);
	const auto pointers = details::TakeNumber(body, kNumberBytes);
	const auto text = details::TextOf(body, kMaxCursorBytes);
	if (!text) {
		return std::nullopt;
	}
	return CursorReport{ .pointers = pointers, .cursor = *text };
}

[[nodiscard]] inline std::optional<std::string> EncodeMirrored(
		char way,
		std::string_view text) {
	if (!IsMirroredWay(way) || (text.size() > kMaxMirroredTextBytes)) {
		return std::nullopt;
	}
	auto result = details::Header(
		Kind::Mirrored,
		kMirroredOverheadBytes - kHeaderBytes + text.size());
	result.push_back(way);
	details::PutNumber(result, text.size(), kLengthBytes);
	result.append(text);
	return result;
}

[[nodiscard]] inline std::optional<MirroredRecord> DecodeMirrored(
		std::string_view message) {
	if ((message.size() < kMirroredOverheadBytes)
		|| (message.size() > kMaxMessageBytes)
		|| (KindOf(message) != Kind::Mirrored)) {
		return std::nullopt;
	}
	auto body = message.substr(kHeaderBytes);
	const auto way = body.front();
	body.remove_prefix(1);
	const auto text = details::TextOf(body, kMaxMirroredTextBytes);
	if (!IsMirroredWay(way) || !text) {
		return std::nullopt;
	}
	return MirroredRecord{ .way = way, .text = *text };
}

} // namespace Tgw::HostedWire
