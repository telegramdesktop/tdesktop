/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/premium_stars.h"

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Ui::Text {
class CustomEmoji;
} // namespace Ui::Text

namespace Ui::Premium {

class ColoredMiniStars final {
public:
	// optimizeUpdate may cause paint glitch.
	ColoredMiniStars(
		not_null<Ui::RpWidget*> parent,
		bool optimizeUpdate,
		MiniStars::Type type = MiniStars::Type::MonoStars);
	ColoredMiniStars(Fn<void(const QRect &)> update, MiniStars::Type type);

	void setSize(const QSize &size);
	void setPosition(QPoint position);
	void setColorOverride(std::optional<QGradientStops> stops);
	void setCenter(const QRect &rect);
	void paint(QPainter &p);

	void setPaused(bool paused);

private:
	MiniStars _ministars;
	QRectF _ministarsRect;
	QImage _frame;
	QImage _mask;
	QSize _size;
	QPoint _position;
	std::optional<QGradientStops> _stopsOverride;

};

[[nodiscard]] std::unique_ptr<Text::CustomEmoji> MakeCollectibleEmoji(
	QStringView entityData,
	QColor centerColor,
	QColor edgeColor,
	std::unique_ptr<Text::CustomEmoji> inner,
	Fn<void()> update,
	int size);

} // namespace Ui::Premium
