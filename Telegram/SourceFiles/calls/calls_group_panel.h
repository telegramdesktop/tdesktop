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
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

class Image;

namespace Data {
class PhotoMedia;
class CloudImageView;
} // namespace Data

namespace Ui {
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

namespace Calls {

class Userpic;
class SignalBars;

class GroupMembers;

enum class BoxContext {
	GroupCallPanel,
	MainWindow,
};

void LeaveGroupCallBox(
	not_null<Ui::GenericBox*> box,
	not_null<GroupCall*> call,
	bool discardChecked,
	BoxContext context);

class GroupPanel final {
public:
	GroupPanel(not_null<GroupCall*> call);
	~GroupPanel();

	[[nodiscard]] bool isActive() const;
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

	bool handleClose();

	void updateControlsGeometry();
	void showControls();

	void hangup(bool discardCallChecked);

	void addMembers();
	void kickMember(not_null<UserData*> user);
	void kickMemberSure(not_null<UserData*> user);
	[[nodiscard]] int computeMembersListTop() const;
	[[nodiscard]] std::optional<QRect> computeTitleRect() const;
	void refreshTitle();

	GroupCall *_call = nullptr;
	not_null<ChannelData*> _channel;

	const std::unique_ptr<Ui::Window> _window;
	const std::unique_ptr<Ui::LayerManager> _layerBg;

#ifdef Q_OS_WIN
	std::unique_ptr<Ui::Platform::TitleControls> _controls;
#endif // Q_OS_WIN

	rpl::lifetime _callLifetime;

	object_ptr<Ui::FlatLabel> _title = { nullptr };
	object_ptr<GroupMembers> _members;

	object_ptr<Ui::CallButton> _settings;
	std::unique_ptr<Ui::CallMuteButton> _mute;
	object_ptr<Ui::CallButton> _hangup;

};

} // namespace Calls
