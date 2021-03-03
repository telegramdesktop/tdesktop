/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QGraphicsItem>

namespace Editor {

class ItemCanvas : public QGraphicsItem {
public:
	enum { Type = UserType + 6 };

	ItemCanvas();

	QRectF boundingRect() const override;
	void paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *widget) override;
	int type() const override;

	[[nodiscard]] rpl::producer<not_null<QPainter*>> paintRequest() const;
protected:
	bool collidesWithItem(
		const QGraphicsItem *,
		Qt::ItemSelectionMode) const override;

	bool collidesWithPath(
		const QPainterPath &,
		Qt::ItemSelectionMode) const override;
private:
	rpl::event_stream<not_null<QPainter*>> _paintRequest;

};

} // namespace Editor
