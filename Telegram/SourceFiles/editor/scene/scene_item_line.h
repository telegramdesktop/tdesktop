/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "editor/scene/scene_item_base.h"

namespace Editor {

class ItemLine : public NumberedItem {
public:
	ItemLine(const QPixmap &&pixmap);
	QRectF boundingRect() const override;
	void paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *widget) override;
protected:
	bool collidesWithItem(
		const QGraphicsItem *,
		Qt::ItemSelectionMode) const override;
	bool collidesWithPath(
		const QPainterPath &,
		Qt::ItemSelectionMode) const override;
private:
	const QPixmap _pixmap;
	const QRectF _rect;

};

} // namespace Editor
