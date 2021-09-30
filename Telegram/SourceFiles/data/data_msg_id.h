/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_peer_id.h"

struct MsgId {
	constexpr MsgId() noexcept = default;
	constexpr MsgId(int64 value) noexcept : bare(value) {
	}

	[[nodiscard]] constexpr explicit operator bool() const noexcept {
		return (bare != 0);
	}
	[[nodiscard]] constexpr bool operator!() const noexcept {
		return !bare;
	}
	[[nodiscard]] constexpr MsgId operator-() const noexcept {
		return -bare;
	}
	constexpr MsgId operator++() noexcept {
		return ++bare;
	}
	constexpr MsgId operator++(int) noexcept {
		return bare++;
	}
	constexpr MsgId operator--() noexcept {
		return --bare;
	}
	constexpr MsgId operator--(int) noexcept {
		return bare--;
	}

	int64 bare = 0;
};

Q_DECLARE_METATYPE(MsgId);

[[nodiscard]] inline constexpr MsgId operator+(MsgId a, MsgId b) noexcept {
	return MsgId(a.bare + b.bare);
}

[[nodiscard]] inline constexpr MsgId operator-(MsgId a, MsgId b) noexcept {
	return MsgId(a.bare - b.bare);
}

[[nodiscard]] inline constexpr bool operator==(MsgId a, MsgId b) noexcept {
	return (a.bare == b.bare);
}

[[nodiscard]] inline constexpr bool operator!=(MsgId a, MsgId b) noexcept {
	return (a.bare != b.bare);
}

[[nodiscard]] inline constexpr bool operator<(MsgId a, MsgId b) noexcept {
	return (a.bare < b.bare);
}

[[nodiscard]] inline constexpr bool operator>(MsgId a, MsgId b) noexcept {
	return (a.bare > b.bare);
}

[[nodiscard]] inline constexpr bool operator<=(MsgId a, MsgId b) noexcept {
	return (a.bare <= b.bare);
}

[[nodiscard]] inline constexpr bool operator>=(MsgId a, MsgId b) noexcept {
	return (a.bare >= b.bare);
}

constexpr auto StartClientMsgId = MsgId(-0x7FFFFFFF);
constexpr auto EndClientMsgId = MsgId(-0x40000000);
constexpr auto ShowAtTheEndMsgId = MsgId(-0x40000000);
constexpr auto SwitchAtTopMsgId = MsgId(-0x3FFFFFFF);
constexpr auto ShowAtProfileMsgId = MsgId(-0x3FFFFFFE);
constexpr auto ShowAndStartBotMsgId = MsgId(-0x3FFFFFFD);
constexpr auto ShowAtGameShareMsgId = MsgId(-0x3FFFFFFC);
constexpr auto ShowForChooseMessagesMsgId = MsgId(-0x3FFFFFFB);
constexpr auto ServerMaxMsgId = MsgId(1LL << 56);
constexpr auto ShowAtUnreadMsgId = MsgId(0);

[[nodiscard]] constexpr inline bool IsClientMsgId(MsgId id) noexcept {
	return (id >= StartClientMsgId && id < EndClientMsgId);
}

[[nodiscard]] constexpr inline bool IsServerMsgId(MsgId id) noexcept {
	return (id > 0 && id < ServerMaxMsgId);
}

struct MsgRange {
	constexpr MsgRange() noexcept = default;
	constexpr MsgRange(MsgId from, MsgId till) noexcept
	: from(from)
	, till(till) {
	}

	MsgId from = 0;
	MsgId till = 0;
};

[[nodiscard]] inline constexpr bool operator==(
		MsgRange a,
		MsgRange b) noexcept {
	return (a.from == b.from) && (a.till == b.till);
}

[[nodiscard]] inline constexpr bool operator!=(
		MsgRange a,
		MsgRange b) noexcept {
	return !(a == b);
}

struct FullMsgId {
	constexpr FullMsgId() noexcept = default;
	constexpr FullMsgId(ChannelId channel, MsgId msg) noexcept
	: channel(channel), msg(msg) {
	}

	constexpr explicit operator bool() const noexcept {
		return msg != 0;
	}
	constexpr bool operator!() const noexcept {
		return msg == 0;
	}

	ChannelId channel = NoChannel;
	MsgId msg = 0;
};

[[nodiscard]] inline constexpr bool operator<(
		const FullMsgId &a,
		const FullMsgId &b) noexcept {
	if (a.channel < b.channel) {
		return true;
	} else if (a.channel > b.channel) {
		return false;
	}
	return a.msg < b.msg;
}

[[nodiscard]] inline constexpr bool operator>(
		const FullMsgId &a,
		const FullMsgId &b) noexcept {
	return b < a;
}

[[nodiscard]] inline constexpr bool operator<=(
		const FullMsgId &a,
		const FullMsgId &b) noexcept {
	return !(b < a);
}

[[nodiscard]] inline constexpr bool operator>=(
		const FullMsgId &a,
		const FullMsgId &b) noexcept {
	return !(a < b);
}

[[nodiscard]] inline constexpr bool operator==(
		const FullMsgId &a,
		const FullMsgId &b) noexcept {
	return (a.channel == b.channel) && (a.msg == b.msg);
}

[[nodiscard]] inline constexpr bool operator!=(
		const FullMsgId &a,
		const FullMsgId &b) noexcept {
	return !(a == b);
}

Q_DECLARE_METATYPE(FullMsgId);

namespace std {

template <>
struct hash<MsgId> : private hash<int64> {
	size_t operator()(MsgId value) const noexcept {
		return hash<int64>::operator()(value.bare);
	}
};

} // namespace std
