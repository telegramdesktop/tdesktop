/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "editor/scene/scene_item_base.h"

namespace Editor {

struct WeatherData {
	QString emoji;
	float64 celsius = 0.;
	QColor color;

	friend inline bool operator==(
		const WeatherData &,
		const WeatherData &) = default;
};

class ItemWeather final : public ItemBase {
public:
	enum { Type = ItemBase::Type + 9 };

	ItemWeather(WeatherData weather, ItemBase::Data data);

	[[nodiscard]] static QSizeF PillSize(
		const WeatherData &weather,
		QSize imageSize);

	void paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *widget) override;
	int type() const override;
	bool bakes() const override;

	[[nodiscard]] const WeatherData &weather() const;
	[[nodiscard]] QColor foreground() const;

protected:
	[[nodiscard]] QRectF visibleRect() const override;
	[[nodiscard]] bool flippable() const override;
	void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
	void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
	std::shared_ptr<ItemBase> duplicate(ItemBase::Data data) const override;

private:
	[[nodiscard]] QSizeF pillSize() const;

	WeatherData _weather;
	const QSize _imageSize;
	const float64 _density;
	QFont _font;
	QPointF _pressPosition;
	bool _pressedSelected = false;

};

} // namespace Editor
