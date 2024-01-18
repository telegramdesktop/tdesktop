/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {

inline constexpr auto kLifeStartDate = 1375315200; // Let it be 01.08.2013.

class LastseenStatus final {
public:
	LastseenStatus() = default;

	[[nodiscard]] static LastseenStatus Recently(bool byMe = false) {
		return LastseenStatus(kRecentlyValue, false, byMe);
	}
	[[nodiscard]] static LastseenStatus WithinWeek(bool byMe = false) {
		return LastseenStatus(kWithinWeekValue, false, byMe);
	}
	[[nodiscard]] static LastseenStatus WithinMonth(bool byMe = false) {
		return LastseenStatus(kWithinMonthValue, false, byMe);
	}
	[[nodiscard]] static LastseenStatus LongAgo(bool byMe = false) {
		return LastseenStatus(kLongAgoValue, false, byMe);
	}
	[[nodiscard]] static LastseenStatus OnlineTill(
			TimeId till,
			bool local = false,
			bool hiddenByMe = false) {
		return (till >= kLifeStartDate + kSpecialValueSkip)
			? LastseenStatus(till - kLifeStartDate, !local, hiddenByMe)
			: LongAgo(hiddenByMe);
	}

	[[nodiscard]] bool isHidden() const {
		return !_available;
	}
	[[nodiscard]] bool isRecently() const {
		return !_available && (_value == kRecentlyValue);
	}
	[[nodiscard]] bool isWithinWeek() const {
		return !_available && (_value == kWithinWeekValue);
	}
	[[nodiscard]] bool isWithinMonth() const {
		return !_available && (_value == kWithinMonthValue);
	}
	[[nodiscard]] bool isLongAgo() const {
		return !_available && (_value == kLongAgoValue);
	}
	[[nodiscard]] bool isHiddenByMe() const {
		return _hiddenByMe;
	}

	[[nodiscard]] bool isOnline(TimeId now) const {
		return (_value >= kSpecialValueSkip)
			&& (kLifeStartDate + _value > now);
	}
	[[nodiscard]] bool isLocalOnlineValue() const {
		return !_available && (_value >= kSpecialValueSkip);
	}
	[[nodiscard]] TimeId onlineTill() const {
		return (_value >= kSpecialValueSkip)
			? (kLifeStartDate + _value)
			: 0;
	}

	[[nodiscard]] uint32 serialize() const {
		return (_value & 0x3FFFFFFF)
			| (_available << 30)
			| (_hiddenByMe << 31);
	}
	[[nodiscard]] static LastseenStatus FromSerialized(uint32 value) {
		auto result = LastseenStatus();
		result._value = value & 0x3FFFFFFF;
		result._available = (value >> 30) & 1;
		result._hiddenByMe = (value >> 31) & 1;
		return result.valid() ? result : LastseenStatus();
	}

	[[nodiscard]] static LastseenStatus FromLegacy(int32 value) {
		if (value == -2) {
			return LastseenStatus::Recently();
		} else if (value == -3) {
			return LastseenStatus::WithinWeek();
		} else if (value == -4) {
			return LastseenStatus::WithinMonth();
		} else if (value < -30) {
			return LastseenStatus::OnlineTill(-value, true);
		} else if (value > 0) {
			return LastseenStatus::OnlineTill(value);
		}
		return LastseenStatus();
	}

	friend inline constexpr auto operator<=>(
		LastseenStatus,
		LastseenStatus) = default;
	friend inline constexpr bool operator==(
		LastseenStatus a,
		LastseenStatus b) = default;

private:
	static constexpr auto kLongAgoValue = uint32(0);
	static constexpr auto kRecentlyValue = uint32(1);
	static constexpr auto kWithinWeekValue = uint32(2);
	static constexpr auto kWithinMonthValue = uint32(3);
	static constexpr auto kSpecialValueSkip = uint32(4);
	static constexpr auto kValidAfter = kLifeStartDate + kSpecialValueSkip;

	[[nodiscard]] bool valid() const {
		return !_available || (_value >= kSpecialValueSkip);
	}

	LastseenStatus(uint32 value, bool available, bool hiddenByMe)
	: _value(value)
	, _available(available ? 1 : 0)
	, _hiddenByMe(hiddenByMe ? 1 : 0) {
	}

	uint32 _value : 30 = 0;
	uint32 _available : 1 = 0;
	uint32 _hiddenByMe : 1 = 0;

};

} // namespace Data
