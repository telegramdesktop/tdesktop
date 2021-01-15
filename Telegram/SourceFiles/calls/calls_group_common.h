/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class UserData;

namespace Calls::Group {

constexpr auto kDefaultVolume = 10000;
constexpr auto kMaxVolume = 20000;

struct MuteRequest {
	not_null<UserData*> user;
	bool mute = false;
	bool locallyOnly = false;
};
struct VolumeRequest {
	not_null<UserData*> user;
	int volume = kDefaultVolume;
	bool finalized = true;
	bool locallyOnly = false;
};

} // namespace Calls::Group
