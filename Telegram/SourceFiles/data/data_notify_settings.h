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

	enum class MuteChange {
		Ignore,
		Mute,
		Unmute,
	};
	enum class SilentPostsChange {
		Ignore,
		Silent,
		Notify,
	};

	bool change(const MTPPeerNotifySettings &settings);
	bool change(
		MuteChange mute,
		SilentPostsChange silent,
		int muteForSeconds);
	TimeMs muteFinishesIn() const;
	bool settingsUnknown() const;
	bool silentPosts() const;
	MTPinputPeerNotifySettings serialize() const;

	~NotifySettings();

private:
	bool _known = false;
	std::unique_ptr<NotifySettingsValue> _value;

};

} // namespace Data
