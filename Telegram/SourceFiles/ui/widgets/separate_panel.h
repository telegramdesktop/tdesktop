/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "ui/layers/layer_widget.h"

namespace Ui {
class BoxContent;
class IconButton;
class LayerStackWidget;
class FlatLabel;
template <typename Widget>
class FadeWrapScaled;
} // namespace Ui

namespace Ui {

class SeparatePanel final : public Ui::RpWidget {
public:
	SeparatePanel();

	void setTitle(rpl::producer<QString> title);
	void setInnerSize(QSize size);
	[[nodiscard]] QRect innerGeometry() const;

	void setHideOnDeactivate(bool hideOnDeactivate);
	void showAndActivate();
	int hideGetDuration();

	void showInner(base::unique_qptr<Ui::RpWidget> inner);
	void showBox(
		object_ptr<Ui::BoxContent> box,
		Ui::LayerOptions options,
		anim::type animated);
	void showToast(const TextWithEntities &text);
	void destroyLayer();

	[[nodiscard]] rpl::producer<> backRequests() const;
	[[nodiscard]] rpl::producer<> closeRequests() const;
	[[nodiscard]] rpl::producer<> closeEvents() const;
	void setBackAllowed(bool allowed);

protected:
	void paintEvent(QPaintEvent *e) override;
	void closeEvent(QCloseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void focusInEvent(QFocusEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void leaveToChildEvent(QEvent *e, QWidget *child) override;
	void keyPressEvent(QKeyEvent *e) override;
	bool eventHook(QEvent *e) override;

private:
	void initControls();
	void initLayout();
	void initGeometry(QSize size);
	void updateGeometry(QSize size);
	void showControls();
	void updateControlsGeometry();
	void createBorderImage();
	void opacityCallback();
	void ensureLayerCreated();

	void updateTitleGeometry(int newWidth);
	void updateTitlePosition();
	void paintShadowBorder(Painter &p) const;
	void paintOpaqueBorder(Painter &p) const;

	void toggleOpacityAnimation(bool visible);
	void finishAnimating();
	void finishClose();

	object_ptr<Ui::IconButton> _close;
	object_ptr<Ui::FlatLabel> _title = { nullptr };
	object_ptr<Ui::FadeWrapScaled<Ui::IconButton>> _back;
	object_ptr<Ui::RpWidget> _body;
	base::unique_qptr<Ui::RpWidget> _inner;
	base::unique_qptr<Ui::LayerStackWidget> _layer = { nullptr };
	rpl::event_stream<> _synteticBackRequests;
	rpl::event_stream<> _userCloseRequests;
	rpl::event_stream<> _closeEvents;

	bool _hideOnDeactivate = false;
	bool _useTransparency = true;
	style::margins _padding;

	bool _dragging = false;
	QPoint _dragStartMousePosition;
	QPoint _dragStartMyPosition;

	Ui::Animations::Simple _titleLeft;
	bool _visible = false;

	Ui::Animations::Simple _opacityAnimation;
	QPixmap _animationCache;
	QPixmap _borderParts;

};

} // namespace Ui
