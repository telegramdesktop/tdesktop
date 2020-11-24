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
class IconButton;
class FlatLabel;
template <typename Widget>
class FadeWrap;
template <typename Widget>
class PaddingWrap;
class Window;
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

class GroupPanel final {
public:
	GroupPanel(not_null<GroupCall*> call);
	~GroupPanel();

	void showAndActivate();
	void closeBeforeDestroy();

private:
	class Button;
	using State = GroupCall::State;

	[[nodiscard]] not_null<Ui::RpWidget*> widget() const;

	void paint(QRect clip);

	void initWindow();
	void initWidget();
	void initControls();
	void initWithCall(GroupCall *call);
	void initLayout();
	void initGeometry();

	void handleClose();

	void updateControlsGeometry();
	void stateChanged(State state);
	void showControls();
	void startDurationUpdateTimer(crl::time currentDuration);

	void toggleFullScreen(bool fullscreen);

	GroupCall *_call = nullptr;
	not_null<ChannelData*> _channel;

	const std::unique_ptr<Ui::Window> _window;

#ifdef Q_OS_WIN
	std::unique_ptr<Ui::Platform::TitleControls> _controls;
#endif // Q_OS_WIN

	rpl::lifetime _callLifetime;

	object_ptr<Button> _settings;
	object_ptr<Button> _hangup;
	object_ptr<Button> _mute;

};

} // namespace Calls
