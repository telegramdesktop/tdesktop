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
constexpr auto kBlobsEnterDuration = crl::time(250);

struct MuteRequest {
	not_null<PeerData*> peer;
	bool mute = false;
	bool locallyOnly = false;
};

struct VolumeRequest {
	not_null<PeerData*> peer;
	int volume = kDefaultVolume;
	bool finalized = true;
	bool locallyOnly = false;
};

struct ParticipantState {
	not_null<PeerData*> peer;
	std::optional<int> volume;
	bool mutedByMe = false;
	bool locallyOnly = false;
};

struct RejoinEvent {
	not_null<PeerData*> wasJoinAs;
	not_null<PeerData*> nowJoinAs;
};

struct JoinInfo {
	not_null<PeerData*> peer;
	not_null<PeerData*> joinAs;
	std::vector<not_null<PeerData*>> possibleJoinAs;
	QString joinHash;
	TimeId scheduleDate = 0;
};

enum class PanelMode {
	Default,
	Wide,
};

enum class VideoQuality {
	Thumbnail,
	Medium,
	Full,
};

enum class Error {
	NoCamera,
	CameraFailed,
	ScreenFailed,
	MutedNoCamera,
	MutedNoScreen,
	DisabledNoCamera,
	DisabledNoScreen,
};

enum class StickedTooltip {
	Camera     = 0x01,
	Microphone = 0x02,
};
constexpr inline bool is_flag_type(StickedTooltip) {
	return true;
}
using StickedTooltips = base::flags<StickedTooltip>;

} // namespace Calls::Group
