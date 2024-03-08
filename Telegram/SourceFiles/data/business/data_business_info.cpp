/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/business/data_business_info.h"

#include "apiwrap.h"
#include "base/unixtime.h"
#include "data/business/data_business_common.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"

namespace Data {
namespace {

[[nodiscard]] MTPBusinessWorkHours ToMTP(const WorkingHours &data) {
	const auto list = data.intervals.normalized().list;
	const auto proj = [](const WorkingInterval &data) {
		return MTPBusinessWeeklyOpen(MTP_businessWeeklyOpen(
			MTP_int(data.start / 60),
			MTP_int(data.end / 60)));
	};
	return MTP_businessWorkHours(
		MTP_flags(0),
		MTP_string(data.timezoneId),
		MTP_vector_from_range(list | ranges::views::transform(proj)));
}

[[nodiscard]] MTPBusinessAwayMessageSchedule ToMTP(
		const AwaySchedule &data) {
	Expects(data.type != AwayScheduleType::Never);

	return (data.type == AwayScheduleType::Always)
		? MTP_businessAwayMessageScheduleAlways()
		: (data.type == AwayScheduleType::OutsideWorkingHours)
		? MTP_businessAwayMessageScheduleOutsideWorkHours()
		: MTP_businessAwayMessageScheduleCustom(
			MTP_int(data.customInterval.start),
			MTP_int(data.customInterval.end));
}

[[nodiscard]] MTPInputBusinessAwayMessage ToMTP(const AwaySettings &data) {
	using Flag = MTPDinputBusinessAwayMessage::Flag;
	return MTP_inputBusinessAwayMessage(
		MTP_flags(data.offlineOnly ? Flag::f_offline_only : Flag()),
		MTP_int(data.shortcutId),
		ToMTP(data.schedule),
		ToMTP(data.recipients));
}

[[nodiscard]] MTPInputBusinessGreetingMessage ToMTP(
		const GreetingSettings &data) {
	return MTP_inputBusinessGreetingMessage(
		MTP_int(data.shortcutId),
		ToMTP(data.recipients),
		MTP_int(data.noActivityDays));
}

} // namespace

BusinessInfo::BusinessInfo(not_null<Session*> owner)
: _owner(owner) {
}

BusinessInfo::~BusinessInfo() = default;

void BusinessInfo::saveWorkingHours(
		WorkingHours data,
		Fn<void(QString)> fail) {
	const auto session = &_owner->session();
	auto details = session->user()->businessDetails();
	const auto &was = details.hours;
	if (was == data) {
		return;
	}

	using Flag = MTPaccount_UpdateBusinessWorkHours::Flag;
	session->api().request(MTPaccount_UpdateBusinessWorkHours(
		MTP_flags(data ? Flag::f_business_work_hours : Flag()),
		ToMTP(data)
	)).fail([=](const MTP::Error &error) {
		auto details = session->user()->businessDetails();
		details.hours = was;
		session->user()->setBusinessDetails(std::move(details));
		if (fail) {
			fail(error.type());
		}
	}).send();

	details.hours = std::move(data);
	session->user()->setBusinessDetails(std::move(details));
}

void BusinessInfo::applyAwaySettings(AwaySettings data) {
	if (_awaySettings == data) {
		return;
	}
	_awaySettings = data;
	_awaySettingsChanged.fire({});
}

void BusinessInfo::saveAwaySettings(
		AwaySettings data,
		Fn<void(QString)> fail) {
	const auto &was = _awaySettings;
	if (was == data) {
		return;
	} else if (!data || data.shortcutId) {
		using Flag = MTPaccount_UpdateBusinessAwayMessage::Flag;
		const auto session = &_owner->session();
		session->api().request(MTPaccount_UpdateBusinessAwayMessage(
			MTP_flags(data ? Flag::f_message : Flag()),
			data ? ToMTP(data) : MTPInputBusinessAwayMessage()
		)).fail([=](const MTP::Error &error) {
			_awaySettings = was;
			_awaySettingsChanged.fire({});
			if (fail) {
				fail(error.type());
			}
		}).send();
	}
	_awaySettings = std::move(data);
	_awaySettingsChanged.fire({});
}

bool BusinessInfo::awaySettingsLoaded() const {
	return _awaySettings.has_value();
}

AwaySettings BusinessInfo::awaySettings() const {
	return _awaySettings.value_or(AwaySettings());
}

rpl::producer<> BusinessInfo::awaySettingsChanged() const {
	return _awaySettingsChanged.events();
}

void BusinessInfo::applyGreetingSettings(GreetingSettings data) {
	if (_greetingSettings == data) {
		return;
	}
	_greetingSettings = data;
	_greetingSettingsChanged.fire({});
}

void BusinessInfo::saveGreetingSettings(
		GreetingSettings data,
		Fn<void(QString)> fail) {
	const auto &was = _greetingSettings;
	if (was == data) {
		return;
	} else if (!data || data.shortcutId) {
		using Flag = MTPaccount_UpdateBusinessGreetingMessage::Flag;
		_owner->session().api().request(
			MTPaccount_UpdateBusinessGreetingMessage(
				MTP_flags(data ? Flag::f_message : Flag()),
				data ? ToMTP(data) : MTPInputBusinessGreetingMessage())
		).fail([=](const MTP::Error &error) {
			_greetingSettings = was;
			_greetingSettingsChanged.fire({});
			if (fail) {
				fail(error.type());
			}
		}).send();
	}
	_greetingSettings = std::move(data);
	_greetingSettingsChanged.fire({});
}

bool BusinessInfo::greetingSettingsLoaded() const {
	return _greetingSettings.has_value();
}

GreetingSettings BusinessInfo::greetingSettings() const {
	return _greetingSettings.value_or(GreetingSettings());
}

rpl::producer<> BusinessInfo::greetingSettingsChanged() const {
	return _greetingSettingsChanged.events();
}

void BusinessInfo::preload() {
	preloadTimezones();
}

void BusinessInfo::preloadTimezones() {
	if (!_timezones.current().list.empty() || _timezonesRequestId) {
		return;
	}
	_timezonesRequestId = _owner->session().api().request(
		MTPhelp_GetTimezonesList(MTP_int(_timezonesHash))
	).done([=](const MTPhelp_TimezonesList &result) {
		result.match([&](const MTPDhelp_timezonesList &data) {
			_timezonesHash = data.vhash().v;
			const auto proj = [](const MTPtimezone &result) {
				return Timezone{
					.id = qs(result.data().vid()),
					.name = qs(result.data().vname()),
					.utcOffset = result.data().vutc_offset().v,
				};
			};
			_timezones = Timezones{
				.list = ranges::views::all(
					data.vtimezones().v
				) | ranges::views::transform(
					proj
				) | ranges::to_vector,
			};
		}, [](const MTPDhelp_timezonesListNotModified &) {
		});
	}).send();
}

rpl::producer<Timezones> BusinessInfo::timezonesValue() const {
	const_cast<BusinessInfo*>(this)->preloadTimezones();
	return _timezones.value();
}

bool BusinessInfo::timezonesLoaded() const {
	return !_timezones.current().list.empty();
}

QString FindClosestTimezoneId(const std::vector<Timezone> &list) {
	const auto local = QDateTime::currentDateTime();
	const auto utc = QDateTime(local.date(), local.time(), Qt::UTC);
	const auto shift = base::unixtime::now() - (TimeId)::time(nullptr);
	const auto delta = int(utc.toSecsSinceEpoch())
		- int(local.toSecsSinceEpoch())
		- shift;
	const auto proj = [&](const Timezone &value) {
		auto distance = value.utcOffset - delta;
		while (distance > 12 * 3600) {
			distance -= 24 * 3600;
		}
		while (distance < -12 * 3600) {
			distance += 24 * 3600;
		}
		return std::abs(distance);
	};
	return ranges::min_element(list, ranges::less(), proj)->id;
}

} // namespace Data
