/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "base/weak_ptr.h"

class UserData;
struct ShareBoxStyleOverrides;

namespace style {
struct Box;
struct FlatLabel;
struct IconButton;
struct InputField;
struct PopupMenu;
} // namespace style

namespace Data {
class GroupCall;
} // namespace Data

namespace Main {
class SessionShow;
} // namespace Main

namespace Ui {
class Show;
class RpWidget;
class GenericBox;
} // namespace Ui

namespace TdE2E {
class Call;
} // namespace TdE2E

namespace tgcalls {
class VideoCaptureInterface;
} // namespace tgcalls

namespace Window {
class SessionController;
} // namespace Window

namespace Calls {

class Window;

struct InviteRequest {
	not_null<UserData*> user;
	bool video = false;
};

struct InviteResult {
	std::vector<not_null<UserData*>> invited;
	std::vector<not_null<UserData*>> alreadyIn;
	std::vector<not_null<UserData*>> privacyRestricted;
	std::vector<not_null<UserData*>> kicked;
	std::vector<not_null<UserData*>> failed;
};

struct StartConferenceInfo {
	std::shared_ptr<Main::SessionShow> show;
	std::shared_ptr<Data::GroupCall> call;
	QString linkSlug;
	MsgId joinMessageId;
	std::vector<InviteRequest> invite;
	bool sharingLink = false;
	bool migrating = false;
	bool muted = false;
	std::shared_ptr<tgcalls::VideoCaptureInterface> videoCapture;
	QString videoCaptureScreenId;
};

struct ConferencePanelMigration {
	std::shared_ptr<Window> window;
};

} // namespace Calls

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

struct RtmpInfo {
	QString url;
	QString key;
};

struct JoinInfo {
	not_null<PeerData*> peer;
	not_null<PeerData*> joinAs;
	std::vector<not_null<PeerData*>> possibleJoinAs;
	QString joinHash;
	RtmpInfo rtmpInfo;
	TimeId scheduleDate = 0;
	bool rtmp = false;
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

[[nodiscard]] object_ptr<Ui::GenericBox> ScreenSharingPrivacyRequestBox();

[[nodiscard]] object_ptr<Ui::RpWidget> MakeJoinCallLogo(
	not_null<QWidget*> parent);

void ConferenceCallJoinConfirm(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<Data::GroupCall> call,
	UserData *maybeInviter,
	Fn<void(Fn<void()> close)> join);

struct ConferenceCallLinkStyleOverrides {
	const style::Box *box = nullptr;
	const style::IconButton *menuToggle = nullptr;
	const style::PopupMenu *menu = nullptr;
	const style::IconButton *close = nullptr;
	const style::FlatLabel *centerLabel = nullptr;
	const style::InputField *linkPreview = nullptr;
	const style::icon *contextRevoke = nullptr;
	std::shared_ptr<ShareBoxStyleOverrides> shareBox;
};
[[nodiscard]] ConferenceCallLinkStyleOverrides DarkConferenceCallLinkStyle();

struct ConferenceCallLinkArgs {
	ConferenceCallLinkStyleOverrides st;
	bool initial = false;
};
void ShowConferenceCallLinkBox(
	std::shared_ptr<Main::SessionShow> show,
	std::shared_ptr<Data::GroupCall> call,
	const ConferenceCallLinkArgs &args);

struct ConferenceFactoryArgs {
	std::shared_ptr<Main::SessionShow> show;
	Fn<void(bool)> finished;
	bool joining = false;
	StartConferenceInfo info;
};
void MakeConferenceCall(ConferenceFactoryArgs &&args);

[[nodiscard]] QString ExtractConferenceSlug(const QString &link);

} // namespace Calls::Group
