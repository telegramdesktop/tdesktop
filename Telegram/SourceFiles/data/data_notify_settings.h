/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {

class NotifySettingsValue;

struct NotifySound {
	QString	title;
	QString data;
	DocumentId id = 0;
	bool none = false;
};

inline bool operator==(const NotifySound &a, const NotifySound &b) {
	return (a.id == b.id)
		&& (a.none == b.none)
		&& (a.title == b.title)
		&& (a.data == b.data);
}

class NotifySettings {
public:
	NotifySettings();

	static constexpr auto kDefaultMutePeriod = 86400 * 365;

	bool change(const MTPPeerNotifySettings &settings);
	bool change(
		std::optional<int> muteForSeconds,
		std::optional<bool> silentPosts,
		std::optional<bool> soundIsNone);

	bool settingsUnknown() const;
	std::optional<TimeId> muteUntil() const;
	std::optional<bool> silentPosts() const;
	std::optional<bool> soundIsNone() const;
	MTPinputPeerNotifySettings serialize() const;

	~NotifySettings();

private:
	bool _known = false;
	std::unique_ptr<NotifySettingsValue> _value;

};

} // namespace Data
