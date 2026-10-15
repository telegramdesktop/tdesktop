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

struct DraggedGiftArgs {
	not_null<Ui::RpWidget*> body;
	int titleExtension = 0;
	QImage image;
	FlightPose source;
	QPointF cursor;
	QPointF grab;
};

class DraggedGift final {
public:
	explicit DraggedGift(DraggedGiftArgs &&args);
	~DraggedGift();

	[[nodiscard]] FlightPose pose() const;
	[[nodiscard]] bool following() const;

	void move(QPointF cursor);
	void settle(
		Fn<std::optional<FlightPose>()> target,
		bool squash,
		Fn<void(bool arrived)> done);
	void returnTo(
		Fn<std::optional<FlightPose>()> square,
		Fn<void(bool arrived)> done);
	void vanish(Fn<void()> done);
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
		Done,
	};

	[[nodiscard]] FlightPose followPose(crl::time now) const;
	[[nodiscard]] crl::time duration() const;
	[[nodiscard]] Frame travelFrame(
		float64 elapsed,
		const FlightPose &target) const;
	void travel(
		Phase phase,
		Fn<std::optional<FlightPose>()> target,
		Fn<void(bool arrived)> done);
	[[nodiscard]] bool tick(crl::time now);
	[[nodiscard]] bool step(crl::time now);
	void apply(const Frame &frame);
	void complete(bool arrived);

	base::unique_qptr<Layer> _layer;
	Fn<std::optional<FlightPose>()> _target;
	Fn<void(bool arrived)> _done;
	FlightPose _source;
	FlightPose _from;
	FlightPose _pose;
	QPointF _cursor;
	QPointF _grab;
	crl::time _started = 0;
	Phase _phase = Phase::Following;
	Ui::Animations::Basic _animation;

};

} // namespace Wallet
