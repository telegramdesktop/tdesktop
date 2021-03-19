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
	void initWithCall(GroupCall *call);
	void initLayout();
	void initGeometry();
	void setupJoinAsChangedToasts();
	void setupTitleChangedToasts();

	bool handleClose();

	void updateControlsGeometry();
	void showControls();

	void endCall();

	void showMainMenu();
	void chooseJoinAs();
	void addMembers();
	void kickParticipant(not_null<PeerData*> participantPeer);
	void kickParticipantSure(not_null<PeerData*> participantPeer);
	[[nodiscard]] QRect computeTitleRect() const;
	void refreshTitle();
	void refreshTitleGeometry();
	void setupRealCallViewers(not_null<GroupCall*> call);
	void subscribeToChanges(not_null<Data::GroupCall*> real);

	void migrate(not_null<ChannelData*> channel);
	void subscribeToPeerChanges();

	GroupCall *_call = nullptr;
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
	object_ptr<Members> _members;
	rpl::variable<QString> _titleText;
	ChooseJoinAsProcess _joinAsProcess;

	object_ptr<Ui::CallButton> _settings;
	std::unique_ptr<Ui::CallMuteButton> _mute;
	object_ptr<Ui::CallButton> _hangup;

	rpl::lifetime _peerLifetime;

};

} // namespace Calls::Group
