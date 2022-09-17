/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {

class NotifyPeerSettingsValue;

struct NotifySound {
	QString	title;
	QString data;
	DocumentId id = 0;
	bool none = false;
};

struct MuteValue {
	bool unmute = false;
	bool forever = false;
	int period = 0;

	[[nodiscard]] explicit operator bool() const {
		return unmute || forever || period;
	}
	[[nodiscard]] int until() const;
};

inline bool operator==(const NotifySound &a, const NotifySound &b) {
	return (a.id == b.id)
		&& (a.none == b.none)
		&& (a.title == b.title)
		&& (a.data == b.data);
}

class PeerNotifySettings {
public:
	PeerNotifySettings();

	bool change(const MTPPeerNotifySettings &settings);
	bool change(
		MuteValue muteForSeconds,
		std::optional<bool> silentPosts,
		std::optional<NotifySound> sound);

	bool settingsUnknown() const;
	std::optional<TimeId> muteUntil() const;
	std::optional<bool> silentPosts() const;
	std::optional<NotifySound> sound() const;
	MTPinputPeerNotifySettings serialize() const;

	~PeerNotifySettings();

private:
	bool _known = false;
	std::unique_ptr<NotifyPeerSettingsValue> _value;

};

} // namespace Data
