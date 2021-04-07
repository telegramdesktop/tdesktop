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
#include "calls/calls_group_call.h"
#include "calls/calls_choose_join_as.h"
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

class Panel final {
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

	[[nodiscard]] not_null<Ui::RpWidget*> widget() const;

	void paint(QRect clip);

	void initWindow();
	void initWidget();
	void initControls();
	void initShareAction();
	void initLayout();
	void initGeometry();
	void setupScheduledLabels(rpl::producer<TimeId> date);
	void setupMembers();
	void setupJoinAsChangedToasts();
	void setupTitleChangedToasts();
	void setupAllowedToSpeakToasts();
	void setupRealMuteButtonState(not_null<Data::GroupCall*> real);

	bool handleClose();
	void startScheduledNow();

	void updateControlsGeometry();
	void updateMembersGeometry();
	void showControls();
	void refreshLeftButton();

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

	const not_null<GroupCall*> _call;
	not_null<PeerData*> _peer;

	const std::unique_ptr<Ui::Window> _window;
	const std::unique_ptr<Ui::LayerManager> _layerBg;

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
	object_ptr<Ui::FlatLabel> _startsIn = { nullptr };
	object_ptr<Ui::RpWidget> _countdown = { nullptr };
	std::shared_ptr<Ui::GroupCallScheduledLeft> _countdownData;
	object_ptr<Ui::FlatLabel> _startsWhen = { nullptr };
	ChooseJoinAsProcess _joinAsProcess;

	object_ptr<Ui::CallButton> _settings = { nullptr };
	object_ptr<Ui::CallButton> _share = { nullptr };
	std::unique_ptr<Ui::CallMuteButton> _mute;
	object_ptr<Ui::CallButton> _hangup;
	Fn<void()> _shareLinkCallback;

	rpl::lifetime _peerLifetime;

};

} // namespace Calls::Group
