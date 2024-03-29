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

	void preload();

	void saveWorkingHours(WorkingHours data, Fn<void(QString)> fail);

	void saveAwaySettings(AwaySettings data, Fn<void(QString)> fail);
	void applyAwaySettings(AwaySettings data);
	[[nodiscard]] AwaySettings awaySettings() const;
	[[nodiscard]] bool awaySettingsLoaded() const;
	[[nodiscard]] rpl::producer<> awaySettingsChanged() const;

	void saveGreetingSettings(
		GreetingSettings data,
		Fn<void(QString)> fail);
	void applyGreetingSettings(GreetingSettings data);
	[[nodiscard]] GreetingSettings greetingSettings() const;
	[[nodiscard]] bool greetingSettingsLoaded() const;
	[[nodiscard]] rpl::producer<> greetingSettingsChanged() const;

	void preloadTimezones();
	[[nodiscard]] bool timezonesLoaded() const;
	[[nodiscard]] rpl::producer<Timezones> timezonesValue() const;

private:
	const not_null<Session*> _owner;

	rpl::variable<Timezones> _timezones;

	std::optional<AwaySettings> _awaySettings;
	rpl::event_stream<> _awaySettingsChanged;

	std::optional<GreetingSettings> _greetingSettings;
	rpl::event_stream<> _greetingSettingsChanged;

	mtpRequestId _timezonesRequestId = 0;
	int32 _timezonesHash = 0;

};

[[nodiscard]] QString FindClosestTimezoneId(
	const std::vector<Timezone> &list);

} // namespace Data
