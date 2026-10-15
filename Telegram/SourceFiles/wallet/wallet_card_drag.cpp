/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_card_drag.h"

#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"

namespace Wallet {
namespace {

constexpr auto kLiftScale = 1.05;
constexpr auto kLiftDuration = crl::time(50);
constexpr auto kSettleDuration = crl::time(110);
constexpr auto kSquashWide = 0.11;
constexpr auto kSquashTall = 0.22;
constexpr auto kReturnDuration = crl::time(185);
constexpr auto kReturnFade = 0.5;
constexpr auto kReturnFadeDuration = crl::time(100);
constexpr auto kVanishDuration = crl::time(150);
constexpr auto kVanishShrink = 0.4;

[[nodiscard]] FlightPose Interpolate(
		const FlightPose &from,
		const FlightPose &to,
		float64 along) {
	return {
		.center = from.center + (to.center - from.center) * along,
		.side = from.side + (to.side - from.side) * along,
		.rotation = from.rotation + (to.rotation - from.rotation) * along,
	};
}

} // namespace

struct DraggedGift::Frame {
	FlightPose pose;
	float64 opacity = 1.;
	float64 scaleX = 1.;
	float64 scaleY = 1.;
};

class DraggedGift::Layer final : public Ui::RpWidget {
public:
	Layer(
		not_null<Ui::RpWidget*> body,
		int titleExtension,
		QImage image);

	void setFrame(const Frame &frame);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	[[nodiscard]] bool sync();
	[[nodiscard]] QTransform transform(const Frame &frame) const;

	const not_null<Ui::RpWidget*> _body;
	const int _titleExtension = 0;
	const QImage _image;
	std::optional<Frame> _frame;
	QPoint _origin;
	QRect _area;

};

DraggedGift::Layer::Layer(
	not_null<Ui::RpWidget*> body,
	int titleExtension,
	QImage image)
: RpWidget(body->window())
, _body(body)
, _titleExtension(titleExtension)
, _image(std::move(image)) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	body->geometryValue() | rpl::on_next([=] {
		if (_frame) {
			setFrame(*_frame);
		} else if (sync()) {
			update();
		}
	}, lifetime());
}

void DraggedGift::Layer::setFrame(const Frame &frame) {
	const auto moved = sync();
	const auto area = transform(frame)
		.mapRect(FlightImageRect(_image, frame.pose.side))
		.toAlignedRect()
		.marginsAdded({ 1, 1, 1, 1 });
	_frame = frame;
	update(moved ? rect() : _area.united(area));
	_area = area;
}

bool DraggedGift::Layer::sync() {
	const auto body = Ui::MapFrom(parentWidget(), _body, _body->rect());
	const auto geometry = body.marginsAdded({ 0, _titleExtension, 0, 0 });
	_origin = body.topLeft() - geometry.topLeft();
	if (geometry == this->geometry()) {
		return false;
	}
	setGeometry(geometry);
	return true;
}

QTransform DraggedGift::Layer::transform(const Frame &frame) const {
	const auto center = QPointF(_origin) + frame.pose.center;
	return QTransform()
		.translate(center.x(), center.y())
		.rotate(frame.pose.rotation)
		.scale(frame.scaleX, frame.scaleY);
}

void DraggedGift::Layer::paintEvent(QPaintEvent *e) {
	if (!_frame) {
		return;
	}
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	p.setOpacity(_frame->opacity);
	p.setTransform(transform(*_frame), true);
	p.drawImage(FlightImageRect(_image, _frame->pose.side), _image);
}

DraggedGift::DraggedGift(DraggedGiftArgs &&args)
: _source(args.source)
, _cursor(args.cursor)
, _grab(args.grab)
, _started(crl::now())
, _animation([=](crl::time now) { return tick(now); }) {
	if (!args.image.isNull()) {
		_layer = base::make_unique_q<Layer>(
			args.body,
			args.titleExtension,
			std::move(args.image));
	}
	apply({ .pose = followPose(_started) });
	if (_layer) {
		_layer->show();
		_layer->raise();
	}
	if (!anim::Disabled()) {
		_animation.start();
	}
}

DraggedGift::~DraggedGift() = default;

FlightPose DraggedGift::pose() const {
	return following() ? followPose(crl::now()) : _pose;
}

bool DraggedGift::following() const {
	return (_phase == Phase::Following);
}

void DraggedGift::move(QPointF cursor) {
	if (!following()) {
		return;
	}
	_cursor = cursor;
	apply({ .pose = followPose(crl::now()) });
}

void DraggedGift::settle(
		Fn<std::optional<FlightPose>()> target,
		bool squash,
		Fn<void(bool arrived)> done) {
	travel(
		squash ? Phase::Squashing : Phase::Settling,
		std::move(target),
		std::move(done));
}

void DraggedGift::returnTo(
		Fn<std::optional<FlightPose>()> square,
		Fn<void(bool arrived)> done) {
	travel(Phase::Returning, std::move(square), std::move(done));
}

void DraggedGift::vanish(Fn<void()> done) {
	travel(Phase::Vanishing, nullptr, [done = std::move(done)](bool) {
		if (done) {
			done();
		}
	});
}

void DraggedGift::finish() {
	if (following() || _phase == Phase::Done) {
		return;
	}
	const auto vanishing = (_phase == Phase::Vanishing);
	complete(vanishing || (_target && ValidFlightPose(_target())));
}

FlightPose DraggedGift::followPose(crl::time now) const {
	const auto progress = anim::Disabled()
		? 1.
		: ((now - _started) / float64(kLiftDuration));
	const auto scale = 1. + (kLiftScale - 1.) * FlightEaseOut(progress);
	return {
		.center = _cursor - _grab * scale,
		.side = _source.side * scale,
	};
}

crl::time DraggedGift::duration() const {
	switch (_phase) {
	case Phase::Settling:
	case Phase::Squashing: return kSettleDuration;
	case Phase::Returning: return kReturnDuration;
	case Phase::Vanishing: return kVanishDuration;
	case Phase::Following:
	case Phase::Done: break;
	}
	return 0;
}

DraggedGift::Frame DraggedGift::travelFrame(
		float64 elapsed,
		const FlightPose &target) const {
	const auto progress = elapsed / duration();
	const auto along = FlightEaseOut(progress);
	if (_phase == Phase::Vanishing) {
		return {
			.pose = {
				.center = _from.center,
				.side = _from.side * (1. - kVanishShrink * along),
				.rotation = _from.rotation,
			},
			.opacity = 1. - along,
		};
	}
	auto result = Frame{ .pose = Interpolate(_from, target, along) };
	if (_phase == Phase::Returning) {
		const auto fade = std::min(elapsed / kReturnFadeDuration, 1.);
		result.opacity = 1. - kReturnFade * std::sin(M_PI * fade);
	} else if (_phase == Phase::Squashing) {
		const auto left = 1. - std::clamp(progress, 0., 1.);
		result.scaleX = 1. + kSquashWide * left * left;
		result.scaleY = 1. - kSquashTall * left * left;
	}
	return result;
}

void DraggedGift::travel(
		Phase phase,
		Fn<std::optional<FlightPose>()> target,
		Fn<void(bool arrived)> done) {
	if (!following()) {
		if (done) {
			done(false);
		}
		return;
	}
	const auto now = crl::now();
	_from = followPose(now);
	_pose = _from;
	_phase = phase;
	_target = std::move(target);
	_done = std::move(done);
	_started = now;
	if (step(now)) {
		_animation.start();
	}
}

bool DraggedGift::tick(crl::time now) {
	if (!following()) {
		return step(now);
	}
	apply({ .pose = followPose(now) });
	return !anim::Disabled() && (now - _started < kLiftDuration);
}

bool DraggedGift::step(crl::time now) {
	const auto target = (_phase == Phase::Vanishing)
		? std::make_optional(_from)
		: _target
		? _target()
		: std::optional<FlightPose>();
	if (!_layer || !ValidFlightPose(target)) {
		complete(false);
		return false;
	}
	const auto elapsed = float64(now - _started);
	if (anim::Disabled() || elapsed >= duration()) {
		complete(true);
		return false;
	}
	apply(travelFrame(elapsed, *target));
	return true;
}

void DraggedGift::apply(const Frame &frame) {
	_pose = frame.pose;
	if (_layer) {
		_layer->setFrame(frame);
	}
}

void DraggedGift::complete(bool arrived) {
	_phase = Phase::Done;
	_animation.stop();
	if (const auto layer = _layer.release()) {
		// WHY: completion can run inside Qt's hide or deactivation pass over
		// the window's children, which keeps a raw pointer to this layer.
		layer->hide();
		layer->deleteLater();
	}
	_target = nullptr;
	if (const auto done = base::take(_done)) {
		done(arrived);
	}
}

} // namespace Wallet
