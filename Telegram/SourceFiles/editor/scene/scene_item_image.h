/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "editor/scene/scene_item_base.h"

namespace Editor {

class ItemImage : public ItemBase {
public:
	ItemImage(
		const QPixmap &&pixmap,
		rpl::producer<float64> zoomValue,
		std::shared_ptr<float64> zPtr,
		int size,
		int x,
		int y);
	void paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *widget) override;
protected:
	void performFlip() override;
	std::shared_ptr<ItemBase> duplicate(
		rpl::producer<float64> zoomValue,
		std::shared_ptr<float64> zPtr,
		int size,
		int x,
		int y) const override;
private:
	QPixmap _pixmap;

};

} // namespace Editor
