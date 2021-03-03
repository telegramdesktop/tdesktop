/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QGraphicsItem>

namespace Editor {

class ItemLine : public QGraphicsItem {
public:
	enum { Type = UserType + 5 };

	ItemLine(
		const QPainterPath &path,
		const QSize &size,
		const QColor &brushColor,
		float brushSize);
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
	const QRectF _rect;
	const QPixmap _pixmap;

};

} // namespace Editor
