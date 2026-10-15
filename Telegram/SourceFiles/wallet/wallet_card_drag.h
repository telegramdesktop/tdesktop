/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "ui/effects/animations.h"
#include "wallet/wallet_card_flight.h"

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Wallet {

enum class DraggedGiftSource : uchar {
	Square,
	Card,
};

struct DraggedGiftArgs {
	not_null<Ui::RpWidget*> body;
	int titleExtension = 0;
	QImage image;
	FlightPose source;
	QPointF cursor;
	QPointF grab;
	DraggedGiftSource origin = DraggedGiftSource::Square;
	Fn<void(const FlightPose &pose)> moved;
};

class DraggedGift final {
public:
	explicit DraggedGift(DraggedGiftArgs &&args);
	~DraggedGift();

	[[nodiscard]] FlightPose pose() const;
	[[nodiscard]] bool following() const;
	[[nodiscard]] float64 liftScale() const;

	void move(QPointF cursor);
	void resize(float64 scale);
	void settle(
		Fn<std::optional<FlightPose>()> target,
		bool squash,
		Fn<void(bool arrived)> done);
	void returnTo(
		Fn<std::optional<FlightPose>()> square,
		Fn<void(bool arrived)> done);
	void vanish(Fn<void()> done);
	void scaleOut(Fn<void()> done);
	void finish();

private:
	class Layer;
	struct Frame;

	enum class Phase : uchar {
		Following,
		Settling,
		Squashing,
		Returning,
		Vanishing,
		ScalingOut,
		Done,
	};

	[[nodiscard]] crl::time liftDuration() const;
	[[nodiscard]] float64 followScale(crl::time now) const;
	[[nodiscard]] FlightPose followPose(crl::time now) const;
	[[nodiscard]] QPointF releaseDrift(crl::time now) const;
	[[nodiscard]] bool disappearing() const;
	[[nodiscard]] crl::time duration() const;
	[[nodiscard]] Frame travelFrame(
		float64 elapsed,
		const FlightPose &target) const;
	void disappear(Phase phase, Fn<void()> done);
	void travel(
		Phase phase,
		Fn<std::optional<FlightPose>()> target,
		Fn<void(bool arrived)> done);
	void follow(crl::time now);
	[[nodiscard]] bool tick(crl::time now);
	[[nodiscard]] bool step(crl::time now);
	void apply(const Frame &frame);
	void complete(bool arrived);

	base::unique_qptr<Layer> _layer;
	Fn<std::optional<FlightPose>()> _target;
	Fn<void(bool arrived)> _done;
	Fn<void(const FlightPose &pose)> _moved;
	FlightPose _source;
	FlightPose _from;
	FlightPose _pose;
	QPointF _cursor;
	QPointF _grab;
	QPointF _sampled;
	QPointF _velocity;
	QPointF _drift;
	crl::time _started = 0;
	crl::time _scaleStarted = 0;
	crl::time _sampledAt = 0;
	float64 _scaleFrom = 1.;
	float64 _scaleTo = 1.;
	DraggedGiftSource _origin = DraggedGiftSource::Square;
	Phase _phase = Phase::Following;
	Ui::Animations::Basic _animation;

};

} // namespace Wallet
