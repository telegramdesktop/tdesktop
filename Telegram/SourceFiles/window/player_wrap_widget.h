#pragma once

#include "ui/effects/widget_slide_wrap.h"
#include "media/player/media_player_widget.h"

namespace Ui {
class PlainShadow;
} // namespace Ui

namespace Window {

class PlayerWrapWidget : public Ui::WidgetSlideWrap<Media::Player::Widget> {
	using Parent = Ui::WidgetSlideWrap<Media::Player::Widget>;

public:
	PlayerWrapWidget(QWidget *parent, base::lambda_unique<void()> updateCallback);

	void updateAdaptiveLayout() {
		updateShadowGeometry();
	}
	void showShadow() {
		entity()->showShadow();
	}
	void hideShadow() {
		entity()->hideShadow();
	}
	int contentHeight() const {
		return height() - st::lineWidth;
	}

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	void updateShadowGeometry();

};

} // namespace Window
