/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "base/timer.h"
#include "base/object_ptr.h"
#include "calls/group/calls_group_call.h"
#include "calls/group/calls_choose_join_as.h"
#include "calls/group/ui/desktop_capture_choose_source.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

class Image;

namespace Data {
class PhotoMedia;
class CloudImageView;
class GroupCall;
} // namespace Data

namespace Ui {
class AbstractButton;
class DropdownMenu;
class CallButton;
class CallMuteButton;
class IconButton;
class FlatLabel;
class RpWidget;
template <typename Widget>
class FadeWrap;
template <typename Widget>
class PaddingWrap;
class Window;
class ScrollArea;
class GenericBox;
class LayerManager;
class GroupCallScheduledLeft;
namespace Platform {
class TitleControls;
} // namespace Platform
} // namespace Ui

namespace style {
struct CallSignalBars;
struct CallBodyLayout;
} // namespace style

namespace Calls::Group {

class Members;
class LargeVideo;
enum class PanelMode;

class Panel final : private Ui::DesktopCapture::ChooseSourceDelegate {
public:
	Panel(not_null<GroupCall*> call);
	~Panel();

	[[nodiscard]] bool isActive() const;
	void minimize();
	void close();
	void showAndActivate();
	void closeBeforeDestroy();

private:
	using State = GroupCall::State;
	struct VideoTile;
	struct ControlsBackgroundNarrow;

	[[nodiscard]] not_null<Ui::RpWidget*> widget() const;

	[[nodiscard]] PanelMode mode() const;

	void paint(QRect clip);

	void initWindow();
	void initWidget();
	void initControls();
	void initShareAction();
	void initLayout();
	void initGeometry();
	void setupScheduledLabels(rpl::producer<TimeId> date);
	void setupMembers();
	void setupPinnedVideo();
	void setupJoinAsChangedToasts();
	void setupTitleChangedToasts();
	void setupAllowedToSpeakToasts();
	void setupRealMuteButtonState(not_null<Data::GroupCall*> real);

	bool handleClose();
	void startScheduledNow();
	void trackControls(bool track);
	void raiseControls();
	void enlargeVideo();
	void minimizeVideo();

	bool updateMode();
	void updateControlsGeometry();
	void updateButtonsGeometry();
	void updateButtonsStyles();
	void updateMembersGeometry();
	void refreshControlsBackground();
	void setupControlsBackgroundWide();
	void setupControlsBackgroundNarrow();
	void showControls();
	void refreshLeftButton();
	void refreshTilesGeometry();
	void toggleWideControls(bool shown);

	void endCall();

	void showMainMenu();
	void chooseJoinAs();
	void addMembers();
	void kickParticipant(not_null<PeerData*> participantPeer);
	void kickParticipantSure(not_null<PeerData*> participantPeer);
	[[nodiscard]] QRect computeTitleRect() const;
	void refreshTitle();
	void refreshTitleGeometry();
	void setupRealCallViewers();
	void subscribeToChanges(not_null<Data::GroupCall*> real);

	void migrate(not_null<ChannelData*> channel);
	void subscribeToPeerChanges();

	QWidget *chooseSourceParent() override;
	QString chooseSourceActiveDeviceId() override;
	rpl::lifetime &chooseSourceInstanceLifetime() override;
	void chooseSourceAccepted(const QString &deviceId) override;
	void chooseSourceStop() override;

	const not_null<GroupCall*> _call;
	not_null<PeerData*> _peer;

	const std::unique_ptr<Ui::Window> _window;
	const std::unique_ptr<Ui::LayerManager> _layerBg;
	rpl::variable<PanelMode> _mode;

#ifndef Q_OS_MAC
	std::unique_ptr<Ui::Platform::TitleControls> _controls;
#endif // !Q_OS_MAC

	rpl::lifetime _callLifetime;

	object_ptr<Ui::FlatLabel> _title = { nullptr };
	object_ptr<Ui::FlatLabel> _subtitle = { nullptr };
	object_ptr<Ui::AbstractButton> _recordingMark = { nullptr };
	object_ptr<Ui::IconButton> _menuToggle = { nullptr };
	object_ptr<Ui::DropdownMenu> _menu = { nullptr };
	object_ptr<Ui::AbstractButton> _joinAsToggle = { nullptr };
	object_ptr<Members> _members = { nullptr };
	std::unique_ptr<Ui::RpWidget> _pinnedVideoWrap;
	std::vector<VideoTile> _videoTiles;
	rpl::lifetime _trackControlsLifetime;
	rpl::lifetime _trackControlsOverStateLifetime;
	object_ptr<Ui::FlatLabel> _startsIn = { nullptr };
	object_ptr<Ui::RpWidget> _countdown = { nullptr };
	std::shared_ptr<Ui::GroupCallScheduledLeft> _countdownData;
	object_ptr<Ui::FlatLabel> _startsWhen = { nullptr };
	ChooseJoinAsProcess _joinAsProcess;
	rpl::variable<bool> _videoMode;
	std::optional<QRect> _lastSmallGeometry;
	std::optional<QRect> _lastLargeGeometry;
	bool _lastLargeMaximized = false;
	bool _showWideControls = false;
	bool _trackControls = false;
	bool _wideControlsShown = false;
	Ui::Animations::Simple _wideControlsAnimation;

	object_ptr<Ui::RpWidget> _controlsBackgroundWide = { nullptr };
	std::unique_ptr<ControlsBackgroundNarrow> _controlsBackgroundNarrow;
	object_ptr<Ui::CallButton> _settings = { nullptr };
	object_ptr<Ui::CallButton> _callShare = { nullptr };
	object_ptr<Ui::CallButton> _video = { nullptr };
	object_ptr<Ui::CallButton> _screenShare = { nullptr };
	std::unique_ptr<Ui::CallMuteButton> _mute;
	object_ptr<Ui::CallButton> _hangup;
	Fn<void()> _callShareLinkCallback;

	rpl::lifetime _peerLifetime;

};

} // namespace Calls::Group
