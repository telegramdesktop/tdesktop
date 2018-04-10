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
#include "ui/rp_widget.h"

namespace Ui {
class IconButton;
class FlatLabel;
template <typename Widget>
class FadeWrap;
} // namespace Ui

namespace Calls {

class Panel
	: public Ui::RpWidget
	, private base::Subscriber
	, private Ui::AbstractTooltipShower {

public:
	Panel(not_null<Call*> call);

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
	using State = Call::State;
	using Type = Call::Type;

	// AbstractTooltipShower interface
	QString tooltipText() const override;
	QPoint tooltipPos() const override;
	bool tooltipWindowActive() const override;

	void initControls();
	void reinitControls();
	void initLayout();
	void initGeometry();
	void hideDeactivated();
	void createBottomImage();
	void createDefaultCacheImage();
	void refreshCacheImageUserPhoto();

	void processUserPhoto();
	void refreshUserPhoto();
	bool isGoodUserPhoto(PhotoData *photo);
	void createUserpicCache(ImagePtr image);

	void updateControlsGeometry();
	void updateHangupGeometry();
	void updateStatusGeometry();
	void stateChanged(State state);
	void showControls();
	void updateStatusText(State state);
	void startDurationUpdateTimer(TimeMs currentDuration);
	void fillFingerprint();
	void toggleOpacityAnimation(bool visible);
	void finishAnimating();
	void destroyDelayed();

	Call *_call = nullptr;
	not_null<UserData*> _user;

	bool _useTransparency = true;
	style::margins _padding;
	int _contentTop = 0;

	bool _dragging = false;
	QPoint _dragStartMousePosition;
	QPoint _dragStartMyPosition;

	int _stateChangedSubscription = 0;

	class Button;
	object_ptr<Button> _answerHangupRedial;
	object_ptr<Ui::FadeWrap<Button>> _decline;
	object_ptr<Ui::FadeWrap<Button>> _cancel;
	bool _hangupShown = false;
	Animation _hangupShownProgress;
	object_ptr<Ui::IconButton> _mute;
	object_ptr<Ui::FlatLabel> _name;
	object_ptr<Ui::FlatLabel> _status;
	std::vector<EmojiPtr> _fingerprint;
	QRect _fingerprintArea;

	base::Timer _updateDurationTimer;
	base::Timer _updateOuterRippleTimer;

	bool _visible = false;
	QPixmap _userPhoto;
	PhotoId _userPhotoId = 0;
	bool _userPhotoFull = false;

	Animation _opacityAnimation;
	QPixmap _animationCache;
	QPixmap _bottomCache;
	QPixmap _cache;

};

} // namespace Calls
