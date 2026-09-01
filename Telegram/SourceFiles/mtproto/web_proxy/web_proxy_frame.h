/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QByteArray>

#include <vector>

namespace MTP::WebProxy {

inline constexpr auto kFrameHeaderSize = 8;
inline constexpr auto kMaxFramePayload = 1024 * 1024;
inline constexpr auto kMaxBatchFrames = 4096;
inline constexpr auto kInitialStreamWindow = 4 * 1024 * 1024;

enum class FrameType : uchar {
	Open = 0x01,
	Data = 0x02,
	Close = 0x03,
	Window = 0x04,
	Ping = 0x05,
	Pong = 0x06,
	Hello = 0x10,
	Welcome = 0x11,
	AuthChallenge = 0x12,
	AuthResponse = 0x13,
	Bye = 0x1F,
};

struct Frame {
	FrameType type = FrameType::Bye;
	uint32 streamId = 0;
	QByteArray payload;
};

[[nodiscard]] QByteArray SerializeFrame(
	FrameType type,
	uint32 streamId,
	const QByteArray &payload = QByteArray());
[[nodiscard]] bool ParseFrames(
	QByteArray &buffer,
	std::vector<Frame> &output);

} // namespace MTP::WebProxy
