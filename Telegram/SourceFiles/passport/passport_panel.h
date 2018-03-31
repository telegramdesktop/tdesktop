/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Ui {
class IconButton;
class FlatLabel;
template <typename Widget>
class FadeWrapScaled;
} // namespace Ui

namespace Passport {

class PanelController;

class Panel
	: public Ui::RpWidget
	, private base::Subscriber {

public:
	Panel(not_null<PanelController*> controller);

	void showAndActivate();
	void hideAndDestroy();

	void showAskPassword();
	void showNoPassword();
	void showPasswordUnconfirmed();
	void showForm();
	void showEditValue(object_ptr<Ui::RpWidget> form);

	rpl::producer<> backRequests() const;
	void setBackAllowed(bool allowed);

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
	void initControls();
	void initLayout();
	void initGeometry();
	void showControls();
	void updateControlsGeometry();
	void createBorderImage();
	void opacityCallback();
	void showInner(base::unique_qptr<Ui::RpWidget> inner);

	void updateTitlePosition();
	void paintShadowBorder(Painter &p) const;
	void paintOpaqueBorder(Painter &p) const;

	void toggleOpacityAnimation(bool visible);
	void finishAnimating();
	void destroyDelayed();

	not_null<PanelController*> _controller;
	object_ptr<Ui::IconButton> _close;
	object_ptr<Ui::FlatLabel> _title;
	object_ptr<Ui::FadeWrapScaled<Ui::IconButton>> _back;
	base::unique_qptr<Ui::RpWidget> _inner;

	bool _useTransparency = true;
	style::margins _padding;

	bool _dragging = false;
	QPoint _dragStartMousePosition;
	QPoint _dragStartMyPosition;

	int _stateChangedSubscription = 0;

	Animation _titleLeft;
	bool _visible = false;

	Animation _opacityAnimation;
	QPixmap _animationCache;
	QPixmap _borderParts;

};

} // namespace Passport
