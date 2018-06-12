/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/player_wrap_widget.h"

#include "ui/widgets/shadow.h"

namespace Window {

PlayerWrapWidget::PlayerWrapWidget(QWidget *parent)
: Parent(parent, object_ptr<Media::Player::Widget>(parent)) {
	sizeValue(
	) | rpl::start_with_next([this](const QSize &size) {
		updateShadowGeometry(size);
	}, lifetime());
}

void PlayerWrapWidget::updateShadowGeometry(const QSize &size) {
	auto skip = Adaptive::OneColumn() ? 0 : st::lineWidth;
	entity()->setShadowGeometryToLeft(
		skip,
		size.height() - st::lineWidth,
		size.width() - skip,
		st::lineWidth);
}

} // namespace Window
