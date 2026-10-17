/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "ui/effects/animations.h"

namespace Lottie {
class Icon;
} // namespace Lottie

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Wallet {

[[nodiscard]] crl::time SendingDiamondLoopStart(
	not_null<Lottie::Icon*> icon,
	crl::time now);
void AdvanceSendingDiamond(
	not_null<Lottie::Icon*> icon,
	crl::time loopStarted,
	crl::time now);

struct DiamondFlightArgs {
	not_null<Ui::RpWidget*> body;
	std::unique_ptr<Lottie::Icon> icon;
	QRectF from;
	crl::time loopStarted = 0;
	crl::time duration = 0;
	Fn<std::optional<QRectF>(crl::time)> target;
	Fn<void(std::unique_ptr<Lottie::Icon>, crl::time)> landed;
	Fn<void()> finished;
};

class DiamondFlight final {
public:
	explicit DiamondFlight(DiamondFlightArgs &&args);
	~DiamondFlight();

private:
	[[nodiscard]] bool tick(crl::time now);
	void paint();
	void updateArea(QRect area);

	const not_null<Ui::RpWidget*> _body;
	base::unique_qptr<Ui::RpWidget> _layer;
	std::unique_ptr<Lottie::Icon> _icon;
	Fn<std::optional<QRectF>(crl::time)> _target;
	Fn<void(std::unique_ptr<Lottie::Icon>, crl::time)> _landed;
	Fn<void()> _finished;
	QPointF _fromCentre;
	float64 _fromSide = 0.;
	crl::time _started = 0;
	crl::time _loopStarted = 0;
	crl::time _duration = 0;
	QRectF _current;
	QRect _area;
	Ui::Animations::Basic _animation;

};

} // namespace Wallet
