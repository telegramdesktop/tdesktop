/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "base/weak_unique_ptr.h"
#include "base/timer.h"
#include "calls/calls_call.h"
#include "ui/widgets/tooltip.h"

namespace Ui {
class IconButton;
class FlatLabel;
template <typename Widget>
class WidgetFadeWrap;
} // namespace Ui

namespace Calls {

class Panel : public TWidget, private base::Subscriber, private Ui::AbstractTooltipShower {
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
	bool event(QEvent *e) override;

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
	void finishAnimation();
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
	object_ptr<Ui::WidgetFadeWrap<Button>> _decline;
	object_ptr<Ui::WidgetFadeWrap<Button>> _cancel;
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
