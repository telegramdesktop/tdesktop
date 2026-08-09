/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/web_proxy/web_proxy_frame.h"

namespace MTP::WebProxy {
namespace {

[[nodiscard]] bool IsKnownType(uchar value) {
	switch (FrameType(value)) {
	case FrameType::Open:
	case FrameType::Data:
	case FrameType::Close:
	case FrameType::Window:
	case FrameType::Ping:
	case FrameType::Pong:
	case FrameType::Hello:
	case FrameType::Welcome:
	case FrameType::AuthChallenge:
	case FrameType::AuthResponse:
	case FrameType::Bye:
		return true;
	}
	return false;
}

} // namespace

QByteArray SerializeFrame(
		FrameType type,
		uint32 streamId,
		const QByteArray &payload) {
	Expects(streamId <= 0x00FFFFFF);
	Expects(payload.size() <= kMaxFramePayload);

	auto result = QByteArray(kFrameHeaderSize + payload.size(), Qt::Uninitialized);
	auto data = reinterpret_cast<uchar*>(result.data());
	data[0] = uchar(type);
	data[1] = uchar(streamId >> 16);
	data[2] = uchar(streamId >> 8);
	data[3] = uchar(streamId);
	const auto size = uint32(payload.size());
	data[4] = uchar(size >> 24);
	data[5] = uchar(size >> 16);
	data[6] = uchar(size >> 8);
	data[7] = uchar(size);
	if (!payload.isEmpty()) {
		memcpy(result.data() + kFrameHeaderSize, payload.constData(), payload.size());
	}
	return result;
}

bool ParseFrames(QByteArray &buffer, std::vector<Frame> &output) {
	auto offset = 0;
	while (buffer.size() - offset >= kFrameHeaderSize) {
		const auto data = reinterpret_cast<const uchar*>(buffer.constData() + offset);
		if (!IsKnownType(data[0])) {
			return false;
		}
		const auto streamId = (uint32(data[1]) << 16)
			| (uint32(data[2]) << 8)
			| uint32(data[3]);
		const auto size = (uint32(data[4]) << 24)
			| (uint32(data[5]) << 16)
			| (uint32(data[6]) << 8)
			| uint32(data[7]);
		if (size > kMaxFramePayload) {
			return false;
		}
		const auto full = kFrameHeaderSize + int(size);
		if (buffer.size() - offset < full) {
			break;
		}
		if (output.size() >= std::size_t(kMaxBatchFrames)) {
			return false;
		}
		output.push_back({
			FrameType(data[0]),
			streamId,
			buffer.mid(offset + kFrameHeaderSize, size),
		});
		offset += full;
	}
	if (offset > 0) {
		buffer.remove(0, offset);
	}
	return true;
}

} // namespace MTP::WebProxy
