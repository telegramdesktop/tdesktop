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
#include "calls/calls_call.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

class Image;

namespace Data {
class PhotoMedia;
class CloudImageView;
} // namespace Data

namespace Ui {
class IconButton;
class CallButton;
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
class VideoBubble;

class Panel final {
public:
	Panel(not_null<Call*> call);
	~Panel();

	[[nodiscard]] bool isActive() const;
	void showAndActivate();
	void replaceCall(not_null<Call*> call);
	void closeBeforeDestroy();

private:
	class Incoming;
	using State = Call::State;
	using Type = Call::Type;
	enum class AnswerHangupRedialState : uchar {
		Answer,
		Hangup,
		Redial,
	};

	[[nodiscard]] not_null<Ui::RpWidget*> widget() const;

	void paint(QRect clip);

	void initWindow();
	void initWidget();
	void initControls();
	void reinitWithCall(Call *call);
	void initLayout();
	void initGeometry();

	void handleClose();

	QRect signalBarsRect() const;
	void paintSignalBarsBg(Painter &p);

	void updateControlsGeometry();
	void updateHangupGeometry();
	void updateStatusGeometry();
	void updateOutgoingVideoBubbleGeometry();
	void stateChanged(State state);
	void showControls();
	void updateStatusText(State state);
	void startDurationUpdateTimer(crl::time currentDuration);
	void setIncomingSize(QSize size);
	void refreshIncomingGeometry();

	void refreshOutgoingPreviewInBody(State state);
	void toggleFullScreen(bool fullscreen);
	void createRemoteAudioMute();
	void refreshAnswerHangupRedialLabel();

	[[nodiscard]] QRect incomingFrameGeometry() const;
	[[nodiscard]] QRect outgoingFrameGeometry() const;

	Call *_call = nullptr;
	not_null<UserData*> _user;

	const std::unique_ptr<Ui::Window> _window;
	std::unique_ptr<Incoming> _incoming;

#ifdef Q_OS_WIN
	std::unique_ptr<Ui::Platform::TitleControls> _controls;
#endif // Q_OS_WIN

	QSize _incomingFrameSize;

	rpl::lifetime _callLifetime;

	not_null<const style::CallBodyLayout*> _bodySt;
	object_ptr<Ui::CallButton> _answerHangupRedial;
	object_ptr<Ui::FadeWrap<Ui::CallButton>> _decline;
	object_ptr<Ui::FadeWrap<Ui::CallButton>> _cancel;
	bool _hangupShown = false;
	bool _outgoingPreviewInBody = false;
	std::optional<AnswerHangupRedialState> _answerHangupRedialState;
	Ui::Animations::Simple _hangupShownProgress;
	object_ptr<Ui::CallButton> _camera;
	object_ptr<Ui::CallButton> _mute;
	object_ptr<Ui::FlatLabel> _name;
	object_ptr<Ui::FlatLabel> _status;
	object_ptr<Ui::RpWidget> _fingerprint = { nullptr };
	object_ptr<Ui::PaddingWrap<Ui::FlatLabel>> _remoteAudioMute = { nullptr };
	std::unique_ptr<Userpic> _userpic;
	std::unique_ptr<VideoBubble> _outgoingVideoBubble;
	QPixmap _bottomShadow;
	int _bodyTop = 0;
	int _buttonsTop = 0;

	base::Timer _updateDurationTimer;
	base::Timer _updateOuterRippleTimer;

};

} // namespace Calls
