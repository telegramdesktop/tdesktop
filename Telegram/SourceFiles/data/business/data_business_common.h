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

	[[nodiscard]] static BusinessRecipients MakeValid(
		BusinessRecipients value);

	friend inline bool operator==(
		const BusinessRecipients &a,
		const BusinessRecipients &b) = default;
};

enum class BusinessRecipientsType : uchar {
	Messages,
	Bots,
};

enum class ChatbotsPermission {
	ViewMessages    = 0x0001,
	ReplyToMessages = 0x0002,
	MarkAsRead      = 0x0004,
	DeleteSent      = 0x0008,
	DeleteReceived  = 0x0010,
	EditName        = 0x0020,
	EditBio         = 0x0040,
	EditUserpic     = 0x0080,
	EditUsername    = 0x0100,
	ViewGifts       = 0x0200,
	SellGifts       = 0x0400,
	GiftSettings    = 0x0800,
	TransferGifts   = 0x1000,
	TransferStars   = 0x2000,
	ManageStories   = 0x4000,
};
inline constexpr bool is_flag_type(ChatbotsPermission) { return true; }
using ChatbotsPermissions = base::flags<ChatbotsPermission>;

[[nodiscard]] MTPInputBusinessRecipients ForMessagesToMTP(
	const BusinessRecipients &data);
[[nodiscard]] MTPInputBusinessBotRecipients ForBotsToMTP(
	const BusinessRecipients &data);
[[nodiscard]] BusinessRecipients FromMTP(
	not_null<Session*> owner,
	const MTPBusinessRecipients &recipients);
[[nodiscard]] BusinessRecipients FromMTP(
	not_null<Session*> owner,
	const MTPBusinessBotRecipients &recipients);
[[nodiscard]] ChatbotsPermissions FromMTP(
	const MTPBusinessBotRights &rights);
[[nodiscard]] MTPBusinessBotRights ToMTP(ChatbotsPermissions rights);

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

struct ChatIntro {
	QString title;
	QString description;
	DocumentData *sticker = nullptr;

	[[nodiscard]] bool customPhrases() const {
		return !title.isEmpty() || !description.isEmpty();
	}

	explicit operator bool() const {
		return customPhrases() || sticker;
	}

	friend inline bool operator==(
		const ChatIntro &a,
		const ChatIntro &b) = default;
};

struct BusinessDetails {
	WorkingHours hours;
	BusinessLocation location;
	ChatIntro intro;

	explicit operator bool() const {
		return hours || location || intro;
	}

	friend inline bool operator==(
		const BusinessDetails &a,
		const BusinessDetails &b) = default;
};

[[nodiscard]] BusinessDetails FromMTP(
	not_null<Session*> owner,
	const tl::conditional<MTPBusinessWorkHours> &hours,
	const tl::conditional<MTPBusinessLocation> &location,
	const tl::conditional<MTPBusinessIntro> &intro);

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
