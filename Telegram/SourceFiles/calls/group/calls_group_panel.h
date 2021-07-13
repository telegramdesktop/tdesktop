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
#include "calls/group/calls_group_common.h"
#include "calls/group/calls_choose_join_as.h"
#include "calls/group/ui/desktop_capture_choose_source.h"
#include "ui/effects/animations.h"
#include "ui/gl/gl_window.h"
#include "ui/rp_widget.h"

class Image;

namespace Data {
class PhotoMedia;
class CloudImageView;
class GroupCall;
} // namespace Data

namespace Ui {
class AbstractButton;
class ImportantTooltip;
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
class ScrollArea;
class GenericBox;
class LayerManager;
class GroupCallScheduledLeft;
namespace Toast {
class Instance;
} // namespace Toast
namespace Platform {
class TitleControls;
} // namespace Platform
} // namespace Ui

namespace style {
struct CallSignalBars;
struct CallBodyLayout;
} // namespace style

namespace Calls::Group {

class Toasts;
class Members;
class Viewport;
enum class PanelMode;
enum class StickedTooltip;

class Panel final : private Ui::DesktopCapture::ChooseSourceDelegate {
public:
	Panel(not_null<GroupCall*> call);
	~Panel();

	[[nodiscard]] not_null<GroupCall*> call() const;
	[[nodiscard]] bool isActive() const;

	void showToast(TextWithEntities &&text, crl::time duration = 0);

	void minimize();
	void close();
	void showAndActivate();
	void closeBeforeDestroy();

	rpl::lifetime &lifetime();

private:
	using State = GroupCall::State;
	struct ControlsBackgroundNarrow;

	enum class NiceTooltipType {
		Normal,
		Sticked,
	};
	enum class StickedTooltipHide {
		Unavailable,
		Activated,
		Discarded,
	};
	class MicLevelTester;

	[[nodiscard]] not_null<Ui::Window*> window() const;
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
	void setupVideo(not_null<Viewport*> viewport);
	void setupRealMuteButtonState(not_null<Data::GroupCall*> real);

	bool handleClose();
	void startScheduledNow();
	void trackControls(bool track);
	void raiseControls();
	void enlargeVideo();
	void minimizeVideo();

	void trackControl(Ui::RpWidget *widget, rpl::lifetime &lifetime);
	void trackControlOver(not_null<Ui::RpWidget*> control, bool over);
	void showNiceTooltip(
		not_null<Ui::RpWidget*> control,
		NiceTooltipType type = NiceTooltipType::Normal);
	void showStickedTooltip();
	void hideStickedTooltip(StickedTooltipHide hide);
	void hideStickedTooltip(StickedTooltip type, StickedTooltipHide hide);
	void hideNiceTooltip();

	bool updateMode();
	void updateControlsGeometry();
	void updateButtonsGeometry();
	void updateTooltipGeometry();
	void updateButtonsStyles();
	void updateMembersGeometry();
	void refreshControlsBackground();
	void setupControlsBackgroundWide();
	void setupControlsBackgroundNarrow();
	void showControls();
	void refreshLeftButton();
	void refreshVideoButtons(
		std::optional<bool> overrideWideMode = std::nullopt);
	void refreshTopButton();
	void toggleWideControls(bool shown);
	void updateWideControlsVisibility();
	[[nodiscard]] bool videoButtonInNarrowMode() const;

	void endCall();

	void showMainMenu();
	void chooseJoinAs();
	void chooseShareScreenSource();
	void screenSharingPrivacyRequest();
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
	bool chooseSourceActiveWithAudio() override;
	bool chooseSourceWithAudioSupported() override;
	rpl::lifetime &chooseSourceInstanceLifetime() override;
	void chooseSourceAccepted(
		const QString &deviceId,
		bool withAudio) override;
	void chooseSourceStop() override;

	const not_null<GroupCall*> _call;
	not_null<PeerData*> _peer;

	Ui::GL::Window _window;
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
	rpl::variable<bool> _wideMenuShown = false;
	object_ptr<Ui::AbstractButton> _joinAsToggle = { nullptr };
	object_ptr<Members> _members = { nullptr };
	std::unique_ptr<Viewport> _viewport;
	rpl::lifetime _trackControlsLifetime;
	rpl::lifetime _trackControlsOverStateLifetime;
	rpl::lifetime _trackControlsMenuLifetime;
	object_ptr<Ui::FlatLabel> _startsIn = { nullptr };
	object_ptr<Ui::RpWidget> _countdown = { nullptr };
	std::shared_ptr<Ui::GroupCallScheduledLeft> _countdownData;
	object_ptr<Ui::FlatLabel> _startsWhen = { nullptr };
	ChooseJoinAsProcess _joinAsProcess;
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
	object_ptr<Ui::CallButton> _wideMenu = { nullptr };
	object_ptr<Ui::CallButton> _callShare = { nullptr };
	object_ptr<Ui::CallButton> _video = { nullptr };
	object_ptr<Ui::CallButton> _screenShare = { nullptr };
	std::unique_ptr<Ui::CallMuteButton> _mute;
	object_ptr<Ui::CallButton> _hangup;
	object_ptr<Ui::ImportantTooltip> _niceTooltip = { nullptr };
	QPointer<Ui::IconButton> _stickedTooltipClose;
	QPointer<Ui::RpWidget> _niceTooltipControl;
	StickedTooltips _stickedTooltipsShown;
	Fn<void()> _callShareLinkCallback;

	const std::unique_ptr<Toasts> _toasts;
	base::weak_ptr<Ui::Toast::Instance> _lastToast;

	std::unique_ptr<MicLevelTester> _micLevelTester;

	rpl::lifetime _peerLifetime;

};

} // namespace Calls::Group
