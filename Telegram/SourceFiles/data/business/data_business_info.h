/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/business/data_business_common.h"

namespace Data {

class Session;

class BusinessInfo final {
public:
	explicit BusinessInfo(not_null<Session*> owner);
	~BusinessInfo();

	[[nodiscard]] const WorkingHours &workingHours() const;
	[[nodiscard]] rpl::producer<WorkingHours> workingHoursValue() const;
	void saveWorkingHours(WorkingHours data);

	void preload();
	void preloadTimezones();
	[[nodiscard]] rpl::producer<Timezones> timezonesValue() const;

private:
	const not_null<Session*> _owner;

	rpl::variable<WorkingHours> _workingHours;

	rpl::variable<Timezones> _timezones;

	mtpRequestId _timezonesRequestId = 0;
	int32 _timezonesHash = 0;

};

} // namespace Data
