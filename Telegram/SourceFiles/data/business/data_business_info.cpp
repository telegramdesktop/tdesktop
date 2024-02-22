/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/business/data_business_info.h"

#include "apiwrap.h"
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

} // namespace

BusinessInfo::BusinessInfo(not_null<Session*> owner)
: _owner(owner) {
}

BusinessInfo::~BusinessInfo() = default;

void BusinessInfo::saveWorkingHours(WorkingHours data) {
	auto details = _owner->session().user()->businessDetails();
	if (details.hours == data) {
		return;
	}
	details.hours = std::move(data);

	using Flag = MTPaccount_UpdateBusinessWorkHours::Flag;
	_owner->session().api().request(MTPaccount_UpdateBusinessWorkHours(
		MTP_flags(details.hours ? Flag::f_business_work_hours : Flag()),
		ToMTP(details.hours)
	)).send();

	_owner->session().user()->setBusinessDetails(std::move(details));
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

} // namespace Data
