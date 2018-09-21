/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {

class NotifySettingsValue;

class NotifySettings {
public:
	NotifySettings();

	static constexpr auto kDefaultMutePeriod = 86400 * 365;

	bool change(const MTPPeerNotifySettings &settings);
	bool change(
		std::optional<int> muteForSeconds,
		std::optional<bool> silentPosts);

	bool settingsUnknown() const;
	std::optional<TimeId> muteUntil() const;
	std::optional<bool> silentPosts() const;
	MTPinputPeerNotifySettings serialize() const;

	~NotifySettings();

private:
	bool _known = false;
	std::unique_ptr<NotifySettingsValue> _value;

};

} // namespace Data
