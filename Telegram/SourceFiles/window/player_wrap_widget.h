#pragma once

#include "ui/wrap/slide_wrap.h"
#include "media/player/media_player_widget.h"

namespace Ui {
class PlainShadow;
} // namespace Ui

namespace Window {

class PlayerWrapWidget : public Ui::SlideWrap<Media::Player::Widget> {
	using Parent = Ui::SlideWrap<Media::Player::Widget>;

public:
	PlayerWrapWidget(QWidget *parent);

	void updateAdaptiveLayout() {
		updateShadowGeometry(size());
	}
	void showShadow() {
		entity()->showShadow();
	}
	void hideShadow() {
		entity()->hideShadow();
	}
	int contentHeight() const {
		return qMax(height() - st::lineWidth, 0);
	}

private:
	void updateShadowGeometry(const QSize &size);

};

} // namespace Window
