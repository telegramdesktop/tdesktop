/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "base/timer.h"
#include "calls/calls_call.h"
#include "ui/widgets/tooltip.h"
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

class Panel final : private Ui::AbstractTooltipShower {
public:
	Panel(not_null<Call*> call);
	~Panel();

	void showAndActivate();
	void replaceCall(not_null<Call*> call);
	void hideBeforeDestroy();

private:
	class Content;
	class Button;
	using State = Call::State;
	using Type = Call::Type;

	[[nodiscard]] not_null<Ui::RpWidget*> widget() const;

	// AbstractTooltipShower interface
	QString tooltipText() const override;
	QPoint tooltipPos() const override;
	bool tooltipWindowActive() const override;

	void paint(QRect clip);

	void initWindow();
	void initWidget();
	void initControls();
	void reinitWithCall(Call *call);
	void initLayout();
	void initGeometry();

	void handleClose();
	void handleMouseMove(not_null<QMouseEvent*> e);

	QRect signalBarsRect() const;
	void paintSignalBarsBg(Painter &p);

	void updateFingerprintGeometry();
	void updateControlsGeometry();
	void updateHangupGeometry();
	void updateStatusGeometry();
	void updateOutgoingVideoBubbleGeometry();
	void stateChanged(State state);
	void showControls();
	void updateStatusText(State state);
	void startDurationUpdateTimer(crl::time currentDuration);
	void fillFingerprint();
	void setIncomingShown(bool shown);

	void refreshOutgoingPreviewInBody(State state);
	void toggleFullScreen(bool fullscreen);

	Call *_call = nullptr;
	not_null<UserData*> _user;

	const std::unique_ptr<Ui::Window> _window;

#ifdef Q_OS_WIN
	std::unique_ptr<Ui::Platform::TitleControls> _controls;
#endif // Q_OS_WIN

	bool _incomingShown = false;

	rpl::lifetime _callLifetime;

	not_null<const style::CallBodyLayout*> _bodySt;
	object_ptr<Button> _answerHangupRedial;
	object_ptr<Ui::FadeWrap<Button>> _decline;
	object_ptr<Ui::FadeWrap<Button>> _cancel;
	bool _hangupShown = false;
	bool _outgoingPreviewInBody = false;
	Ui::Animations::Simple _hangupShownProgress;
	object_ptr<Button> _camera;
	object_ptr<Button> _mute;
	object_ptr<Ui::FlatLabel> _name;
	object_ptr<Ui::FlatLabel> _status;
	object_ptr<SignalBars> _signalBars = { nullptr };
	std::unique_ptr<Userpic> _userpic;
	std::unique_ptr<VideoBubble> _outgoingVideoBubble;
	std::vector<EmojiPtr> _fingerprint;
	QRect _fingerprintArea;
	int _bodyTop = 0;
	int _buttonsTop = 0;
	int _fingerprintHeight = 0;

	base::Timer _updateDurationTimer;
	base::Timer _updateOuterRippleTimer;

};

} // namespace Calls
