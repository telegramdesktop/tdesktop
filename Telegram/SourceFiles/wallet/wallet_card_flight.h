/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "ui/effects/animations.h"

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Wallet {

struct FlightPose {
	QPointF center;
	float64 side = 0.;
	float64 rotation = 0.;
};

[[nodiscard]] bool ValidFlightPose(const std::optional<FlightPose> &pose);
[[nodiscard]] float64 FlightEaseOut(float64 progress);
[[nodiscard]] QRectF FlightImageRect(const QImage &image, float64 side);

void MarkTravelLayer(not_null<QWidget*> layer);
[[nodiscard]] bool IsTravelLayer(not_null<const QWidget*> widget);

enum class CardFlightDirection : uchar {
	ToCard,
	ToSquare,
};

struct CardFlightArgs {
	not_null<Ui::RpWidget*> body;
	QImage image;
	Fn<std::optional<FlightPose>()> square;
	Fn<std::optional<FlightPose>()> card;
	CardFlightDirection direction = CardFlightDirection::ToCard;
	Fn<void(bool arrived)> done;
};

class CardFlight final {
public:
	explicit CardFlight(CardFlightArgs &&args);
	~CardFlight();

	void start();

private:
	class Layer;

	[[nodiscard]] bool tick(crl::time now);
	[[nodiscard]] bool step(float64 progress);
	void finish(bool arrived);

	base::unique_qptr<Layer> _layer;
	Fn<std::optional<FlightPose>()> _square;
	Fn<std::optional<FlightPose>()> _card;
	Fn<void(bool arrived)> _done;
	crl::time _started = 0;
	CardFlightDirection _direction = CardFlightDirection::ToCard;
	bool _finished = false;
	Ui::Animations::Basic _animation;

};

} // namespace Wallet
