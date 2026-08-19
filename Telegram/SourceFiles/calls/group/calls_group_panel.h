/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "calls/group/calls_group_call.h"
#include "calls/group/calls_group_common.h"
#include "calls/group/calls_choose_join_as.h"
#include "calls/group/ui/desktop_capture_choose_source.h"
#include "ui/effects/animations.h"

class Image;

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Data {
class PhotoMedia;
class GroupCall;
} // namespace Data

namespace Ui {
class Show;
class BoxContent;
class AbstractButton;
class ImportantTooltip;
class DropdownMenu;
class CallButton;
class CallMuteButton;
class IconButton;
class FlatLabel;
class RpWidget;
class RpWindow;
template <typename Widget>
class FadeWrap;
template <typename Widget>
class PaddingWrap;
class ScrollArea;
class GenericBox;
class GroupCallScheduledLeft;
struct CallButtonColors;
} // namespace Ui

namespace Ui::Toast {
class Instance;
struct Config;
} // namespace Ui::Toast

namespace style {
struct CallSignalBars;
struct CallBodyLayout;
} // namespace style

namespace Calls {
struct InviteRequest;
struct ConferencePanelMigration;
class Window;
} // namespace Calls

namespace Calls::Group {

class Toasts;
class Members;
class Viewport;
enum class PanelMode;
enum class StickedTooltip;
class MicLevelTester;
class MessageField;
class MessagesUi;

class Panel final
	: public base::has_weak_ptr
	, private Ui::DesktopCapture::ChooseSourceDelegate {
public:
	explicit Panel(not_null<GroupCall*> call);
	Panel(not_null<GroupCall*> call, ConferencePanelMigration info);
	~Panel();

	[[nodiscard]] not_null<Ui::RpWidget*> widget() const;
	[[nodiscard]] not_null<GroupCall*> call() const;
	[[nodiscard]] bool isVisible() const;
	[[nodiscard]] bool isActive() const;

	void migrationShowShareLink();
	void migrationInviteUsers(std::vector<InviteRequest> users);

	void minimize();
	void toggleFullScreen();
	void toggleFullScreen(bool fullscreen);
	void close();
	void showAndActivate();
	void closeBeforeDestroy();

	[[nodiscard]] std::shared_ptr<ChatHelpers::Show> uiShow();
	[[nodiscard]] not_null<Window*> callWindow() const;
	[[nodiscard]] not_null<Ui::RpWindow*> window() const;

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

	[[nodiscard]] PanelMode mode() const;

	void paint(QRect clip);

	void initWindow();
	void initWidget();
	void initControls();
	void initShareAction();
	void initLayout(ConferencePanelMigration info);
	void initGeometry(ConferencePanelMigration info);
	void setupScheduledLabels(rpl::producer<TimeId> date);
	void setupMembers();
	void setupVideo(not_null<Viewport*> viewport);
	void setupRealMuteButtonState(not_null<Data::GroupCall*> real);
	[[nodiscard]] rpl::producer<QString> titleText();

	bool handleClose();
	void startScheduledNow();
	void trackControls(bool track, bool force = false);
	void raiseControls();
	void enlargeVideo();

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
	void refreshTitleBackground();
	void setupControlsBackgroundWide();
	void setupControlsBackgroundNarrow();
	void showControls();
	void createMessageButton();
	void refreshLeftButton();
	void refreshVideoButtons(
		std::optional<bool> overrideWideMode = std::nullopt);
	void refreshTopButton();
	void createPinOnTop();
	void setupEmptyRtmp();
	void toggleWideControls(bool shown);
	void updateWideControlsVisibility();
	[[nodiscard]] bool videoButtonInNarrowMode() const;
	[[nodiscard]] Fn<void()> shareConferenceLinkCallback();
	void toggleMessageTyping();
	[[nodiscard]] rpl::producer<Ui::CallButtonColors> toggleableOverrides(
		rpl::producer<bool> active);

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
	void refreshTitleColors();
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

	std::shared_ptr<Window> _window;
	rpl::variable<PanelMode> _mode;
	rpl::variable<bool> _fullScreenOrMaximized = false;
	bool _unpinnedMaximized = false;
	bool _rtmpFull = false;

	rpl::lifetime _callLifetime;

	object_ptr<Ui::RpWidget> _titleBackground = { nullptr };
	object_ptr<Ui::FlatLabel> _title = { nullptr };
	object_ptr<Ui::FlatLabel> _titleSeparator = { nullptr };
	object_ptr<Ui::FlatLabel> _viewers = { nullptr };
	object_ptr<Ui::FlatLabel> _subtitle = { nullptr };
	object_ptr<Ui::AbstractButton> _recordingMark = { nullptr };
	object_ptr<Ui::IconButton> _menuToggle = { nullptr };
	object_ptr<Ui::IconButton> _pinOnTop = { nullptr };
	object_ptr<Ui::DropdownMenu> _menu = { nullptr };
	rpl::variable<bool> _wideMenuShown = false;
	rpl::variable<bool> _messageTyping = false;
	object_ptr<Ui::AbstractButton> _joinAsToggle = { nullptr };
	object_ptr<Members> _members = { nullptr };
	std::unique_ptr<Viewport> _viewport;
	rpl::lifetime _trackControlsOverStateLifetime;
	rpl::lifetime _trackControlsMenuLifetime;
	object_ptr<Ui::FlatLabel> _startsIn = { nullptr };
	object_ptr<Ui::RpWidget> _countdown = { nullptr };
	std::shared_ptr<Ui::GroupCallScheduledLeft> _countdownData;
	object_ptr<Ui::FlatLabel> _startsWhen = { nullptr };
	object_ptr<Ui::RpWidget> _emptyRtmp = { nullptr };
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
	object_ptr<Ui::CallButton> _message = { nullptr };
	std::unique_ptr<Ui::CallMuteButton> _mute;
	object_ptr<Ui::CallButton> _hangup;
	object_ptr<Ui::ImportantTooltip> _niceTooltip = { nullptr };
	QPointer<Ui::IconButton> _stickedTooltipClose;
	QPointer<Ui::RpWidget> _niceTooltipControl;
	StickedTooltips _stickedTooltipsShown;
	Fn<void()> _callShareLinkCallback;

	std::shared_ptr<ChatHelpers::Show> _cachedShow;
	std::unique_ptr<MessageField> _messageField;
	std::unique_ptr<MessagesUi> _messages;

	const std::unique_ptr<Toasts> _toasts;

	std::unique_ptr<MicLevelTester> _micLevelTester;

	style::complex_color _controlsBackgroundColor;
	base::Timer _hideControlsTimer;
	rpl::lifetime _hideControlsTimerLifetime;

	rpl::lifetime _peerLifetime;
	rpl::lifetime _lifetime;

};

} // namespace Calls::Group
