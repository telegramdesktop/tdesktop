/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"
#include "data/data_location.h"

class UserData;

namespace Data {

class Session;

enum class BusinessChatType {
	NewChats = (1 << 0),
	ExistingChats = (1 << 1),
	Contacts = (1 << 2),
	NonContacts = (1 << 3),
};
inline constexpr bool is_flag_type(BusinessChatType) { return true; }

using BusinessChatTypes = base::flags<BusinessChatType>;

struct BusinessChats {
	BusinessChatTypes types;
	std::vector<not_null<UserData*>> list;

	[[nodiscard]] bool empty() const {
		return !types && list.empty();
	}

	friend inline bool operator==(
		const BusinessChats &a,
		const BusinessChats &b) = default;
};

struct BusinessRecipients {
	BusinessChats included;
	BusinessChats excluded;
	bool allButExcluded = false;

	friend inline bool operator==(
		const BusinessRecipients &a,
		const BusinessRecipients &b) = default;
};

[[nodiscard]] MTPInputBusinessRecipients ToMTP(
	const BusinessRecipients &data);
[[nodiscard]] BusinessRecipients FromMTP(
	not_null<Session*> owner,
	const MTPBusinessRecipients &recipients);

struct Timezone {
	QString id;
	QString name;
	TimeId utcOffset = 0;

	friend inline bool operator==(
		const Timezone &a,
		const Timezone &b) = default;
};

struct Timezones {
	std::vector<Timezone> list;

	friend inline bool operator==(
		const Timezones &a,
		const Timezones &b) = default;
};;

struct WorkingInterval {
	static constexpr auto kDay = 24 * 3600;
	static constexpr auto kWeek = 7 * kDay;
	static constexpr auto kInNextDayMax = 6 * 3600;

	TimeId start = 0;
	TimeId end = 0;

	explicit operator bool() const {
		return start < end;
	}

	[[nodiscard]] WorkingInterval shifted(TimeId offset) const {
		return { start + offset, end + offset };
	}
	[[nodiscard]] WorkingInterval united(WorkingInterval other) const {
		if (!*this) {
			return other;
		} else if (!other) {
			return *this;
		}
		return {
			std::min(start, other.start),
			std::max(end, other.end),
		};
	}
	[[nodiscard]] WorkingInterval intersected(WorkingInterval other) const {
		const auto result = WorkingInterval{
			std::max(start, other.start),
			std::min(end, other.end),
		};
		return result ? result : WorkingInterval();
	}

	friend inline bool operator==(
		const WorkingInterval &a,
		const WorkingInterval &b) = default;
};

struct WorkingIntervals {
	std::vector<WorkingInterval> list;

	[[nodiscard]] WorkingIntervals normalized() const;

	explicit operator bool() const {
		for (const auto &interval : list) {
			if (interval) {
				return true;
			}
		}
		return false;
	}
	friend inline bool operator==(
		const WorkingIntervals &a,
		const WorkingIntervals &b) = default;
};

struct WorkingHours {
	WorkingIntervals intervals;
	QString timezoneId;

	[[nodiscard]] WorkingHours normalized() const {
		return { intervals.normalized(), timezoneId };
	}

	explicit operator bool() const {
		return !timezoneId.isEmpty() && !intervals.list.empty();
	}

	friend inline bool operator==(
		const WorkingHours &a,
		const WorkingHours &b) = default;
};

[[nodiscard]] WorkingIntervals ExtractDayIntervals(
	const WorkingIntervals &intervals,
	int dayIndex);
[[nodiscard]] bool IsFullOpen(const WorkingIntervals &extractedDay);
[[nodiscard]] WorkingIntervals RemoveDayIntervals(
	const WorkingIntervals &intervals,
	int dayIndex);
[[nodiscard]] WorkingIntervals ReplaceDayIntervals(
	const WorkingIntervals &intervals,
	int dayIndex,
	WorkingIntervals replacement);

struct BusinessLocation {
	QString address;
	std::optional<LocationPoint> point;

	explicit operator bool() const {
		return !address.isEmpty();
	}

	friend inline bool operator==(
		const BusinessLocation &a,
		const BusinessLocation &b) = default;
};

struct BusinessDetails {
	WorkingHours hours;
	BusinessLocation location;

	explicit operator bool() const {
		return hours || location;
	}

	friend inline bool operator==(
		const BusinessDetails &a,
		const BusinessDetails &b) = default;
};

[[nodiscard]] BusinessDetails FromMTP(
	const tl::conditional<MTPBusinessWorkHours> &hours,
	const tl::conditional<MTPBusinessLocation> &location);

enum class AwayScheduleType : uchar {
	Never = 0,
	Always = 1,
	OutsideWorkingHours = 2,
	Custom = 3,
};

struct AwaySchedule {
	AwayScheduleType type = AwayScheduleType::Never;
	WorkingInterval customInterval;

	friend inline bool operator==(
		const AwaySchedule &a,
		const AwaySchedule &b) = default;
};

struct AwaySettings {
	BusinessRecipients recipients;
	AwaySchedule schedule;
	BusinessShortcutId shortcutId = 0;
	bool offlineOnly = false;

	explicit operator bool() const {
		return schedule.type != AwayScheduleType::Never;
	}

	friend inline bool operator==(
		const AwaySettings &a,
		const AwaySettings &b) = default;
};

[[nodiscard]] AwaySettings FromMTP(
	not_null<Session*> owner,
	const tl::conditional<MTPBusinessAwayMessage> &message);

struct GreetingSettings {
	BusinessRecipients recipients;
	int noActivityDays = 0;
	BusinessShortcutId shortcutId = 0;

	explicit operator bool() const {
		return noActivityDays > 0;
	}

	friend inline bool operator==(
		const GreetingSettings &a,
		const GreetingSettings &b) = default;
};

[[nodiscard]] GreetingSettings FromMTP(
	not_null<Session*> owner,
	const tl::conditional<MTPBusinessGreetingMessage> &message);

} // namespace Data
