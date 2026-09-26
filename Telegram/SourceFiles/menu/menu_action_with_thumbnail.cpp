/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "menu/menu_action_with_thumbnail.h"

#include "ui/dynamic_image.h"
#include "ui/painter.h"

namespace Menu {

ActionWithThumbnail::ActionWithThumbnail(
	not_null<Ui::Menu::Menu*> parent,
	const style::Menu &st,
	not_null<QAction*> action,
	std::shared_ptr<Ui::DynamicImage> thumbnail,
	int thumbnailSize)
: Ui::Menu::Action(parent, st, action, nullptr, nullptr)
, _thumbnail(std::move(thumbnail))
, _thumbnailSize(thumbnailSize) {
	if (_thumbnail) {
		_thumbnail->subscribeToUpdates([=] { update(); });
	}
}

ActionWithThumbnail::~ActionWithThumbnail() {
	if (_thumbnail) {
		_thumbnail->subscribeToUpdates(nullptr);
	}
}

void ActionWithThumbnail::paintEvent(QPaintEvent *e) {
	Ui::Menu::Action::paintEvent(e);
	if (!_thumbnail) {
		return;
	}
	auto p = QPainter(this);
	const auto pos = st().itemIconPosition;
	p.drawImage(
		QRect(pos.x(), pos.y(), _thumbnailSize, _thumbnailSize),
		_thumbnail->image(_thumbnailSize));
}

} // namespace Menu
