/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_icon.h"

namespace Info {
namespace Profile {

FloatingIcon::FloatingIcon(
	RpWidget *parent,
	const style::icon &icon,
	QPoint position)
: FloatingIcon(parent, icon, position, Tag{}) {
}

FloatingIcon::FloatingIcon(
	RpWidget *parent,
	const style::icon &icon,
	QPoint position,
	const Tag &)
: RpWidget(parent)
, _icon(&icon)
, _point(position) {
	resize(
		_point.x() + _icon->width(),
		_point.y() + _icon->height());
	setAttribute(Qt::WA_TransparentForMouseEvents);
	parent->widthValue(
	) | rpl::start_with_next(
		[this] { moveToLeft(0, 0); },
		lifetime());
}

void FloatingIcon::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	_icon->paint(p, _point, width());
}

} // namespace Profile
} // namespace Info
