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
} // namespace Ui

namespace style {
struct CallSignalBars;
struct CallBodyLayout;
} // namespace style

namespace Calls {

class Userpic;
class SignalBars;
class VideoBubble;

class Panel final : public Ui::RpWidget, private Ui::AbstractTooltipShower {

public:
	Panel(not_null<Call*> call);
	~Panel();

	void showAndActivate();
	void replaceCall(not_null<Call*> call);
	void hideAndDestroy();

protected:
	void paintEvent(QPaintEvent *e) override;
	void closeEvent(QCloseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void leaveToChildEvent(QEvent *e, QWidget *child) override;
	bool eventHook(QEvent *e) override;

private:
	class Button;
	using State = Call::State;
	using Type = Call::Type;

	// AbstractTooltipShower interface
	QString tooltipText() const override;
	QPoint tooltipPos() const override;
	bool tooltipWindowActive() const override;

	void initControls();
	void reinitWithCall(Call *call);
	void initLayout();
	void initGeometry();
	void hideDeactivated();
	void createBottomImage();
	void createDefaultCacheImage();

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
	void fillFingerprint();
	void toggleOpacityAnimation(bool visible);
	void finishAnimating();
	void destroyDelayed();
	void setIncomingShown(bool shown);

	[[nodiscard]] bool hasActiveVideo() const;
	void checkForInactiveHide();
	void checkForInactiveShow();
	void refreshOutgoingPreviewInBody(State state);

	Call *_call = nullptr;
	not_null<UserData*> _user;

	bool _useTransparency = true;
	bool _incomingShown = false;
	style::margins _padding;

	bool _dragging = false;
	QPoint _dragStartMousePosition;
	QPoint _dragStartMyPosition;

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

	bool _visible = false;

	Ui::Animations::Simple _opacityAnimation;
	QPixmap _animationCache;
	QPixmap _bottomCache;
	QPixmap _cache;

};

} // namespace Calls
