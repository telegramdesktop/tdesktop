/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_card_flight.h"

#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"

namespace Wallet {
namespace {

constexpr auto kTravelLayerProperty = "walletTravelLayer";
constexpr auto kFlightDuration = crl::time(250);
constexpr auto kBend = 0.45;
constexpr auto kBendSteps = 16;
constexpr auto kEpsilon = 1e-9;
constexpr auto kWidestAngle = 45.;
constexpr auto kQuarterTurn = 90.;

[[nodiscard]] float64 RotationSpan(float64 from, float64 to) {
	const auto span = [](float64 degrees) {
		const auto radians = qDegreesToRadians(degrees);
		return std::abs(std::cos(radians)) + std::abs(std::sin(radians));
	};
	const auto widest = kWidestAngle + kQuarterTurn * std::ceil(
		(std::min(from, to) - kWidestAngle) / kQuarterTurn);
	return (widest <= std::max(from, to))
		? std::sqrt(2.)
		: std::max(span(from), span(to));
}

[[nodiscard]] float64 CurveAt(
		float64 from,
		float64 control,
		float64 to,
		float64 along) {
	const auto rest = 1. - along;
	return from * rest * rest
		+ control * 2. * along * rest
		+ to * along * along;
}

[[nodiscard]] QPointF CurvePoint(
		QPointF from,
		QPointF control,
		QPointF to,
		float64 along) {
	return QPointF(
		CurveAt(from.x(), control.x(), to.x(), along),
		CurveAt(from.y(), control.y(), to.y(), along));
}

[[nodiscard]] std::pair<float64, float64> CurveRange(
		float64 from,
		float64 control,
		float64 to) {
	auto low = std::min(from, to);
	auto high = std::max(from, to);
	const auto denominator = from - 2. * control + to;
	if (std::abs(denominator) > kEpsilon) {
		const auto along = (from - control) / denominator;
		if (along > 0. && along < 1.) {
			const auto extremum = CurveAt(from, control, to, along);
			low = std::min(low, extremum);
			high = std::max(high, extremum);
		}
	}
	return { low, high };
}

[[nodiscard]] bool CurveFits(
		QPointF from,
		QPointF control,
		QPointF to,
		float64 half,
		float64 width) {
	const auto [left, right] = CurveRange(from.x(), control.x(), to.x());
	const auto top = CurveRange(from.y(), control.y(), to.y()).first;
	return (left >= half) && (right + half <= width) && (top >= half);
}

[[nodiscard]] QPointF CurveControl(
		QPointF from,
		QPointF to,
		float64 half,
		float64 width) {
	const auto middle = (from + to) / 2.;
	const auto chord = to - from;
	const auto normal = QPointF(chord.y(), -chord.x());
	const auto fits = [&](float64 bend) {
		return CurveFits(from, middle + normal * bend, to, half, width);
	};
	if (fits(kBend)) {
		return middle + normal * kBend;
	} else if (!fits(0.)) {
		return middle;
	}
	auto low = 0.;
	auto high = kBend;
	for (auto i = 0; i != kBendSteps; ++i) {
		const auto bend = (low + high) / 2.;
		if (fits(bend)) {
			low = bend;
		} else {
			high = bend;
		}
	}
	return middle + normal * low;
}

} // namespace

bool ValidFlightPose(const std::optional<FlightPose> &pose) {
	return pose
		&& std::isfinite(pose->center.x())
		&& std::isfinite(pose->center.y())
		&& std::isfinite(pose->rotation)
		&& std::isfinite(pose->side)
		&& (pose->side > 0.);
}

float64 FlightEaseOut(float64 progress) {
	const auto left = 1. - std::clamp(progress, 0., 1.);
	return 1. - left * left;
}

QRectF FlightImageRect(const QImage &image, float64 side) {
	const auto size = QSizeF(image.size()).scaled(
		QSizeF(side, side),
		Qt::KeepAspectRatio);
	return QRectF(
		QPointF(-size.width() / 2., -size.height() / 2.),
		size);
}

void MarkTravelLayer(not_null<QWidget*> layer) {
	layer->setProperty(kTravelLayerProperty, true);
}

bool IsTravelLayer(not_null<const QWidget*> widget) {
	return widget->property(kTravelLayerProperty).toBool();
}

class CardFlight::Layer final : public Ui::RpWidget {
public:
	Layer(not_null<Ui::RpWidget*> body, QImage image);

	void setPose(const FlightPose &pose);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	[[nodiscard]] bool sync();

	const not_null<Ui::RpWidget*> _body;
	const QImage _image;
	std::optional<FlightPose> _pose;
	QRect _area;

};

CardFlight::Layer::Layer(not_null<Ui::RpWidget*> body, QImage image)
: RpWidget(body->window())
, _body(body)
, _image(std::move(image)) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	MarkTravelLayer(this);
	body->geometryValue() | rpl::on_next([=] {
		if (sync()) {
			update();
		}
	}, lifetime());
}

void CardFlight::Layer::setPose(const FlightPose &pose) {
	const auto moved = sync();
	const auto area = QTransform()
		.translate(pose.center.x(), pose.center.y())
		.rotate(pose.rotation)
		.mapRect(FlightImageRect(_image, pose.side))
		.toAlignedRect()
		.marginsAdded({ 1, 1, 1, 1 });
	_pose = pose;
	update(moved ? rect() : _area.united(area));
	_area = area;
}

bool CardFlight::Layer::sync() {
	const auto geometry = Ui::MapFrom(parentWidget(), _body, _body->rect());
	if (geometry == this->geometry()) {
		return false;
	}
	setGeometry(geometry);
	return true;
}

void CardFlight::Layer::paintEvent(QPaintEvent *e) {
	if (!_pose) {
		return;
	}
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	p.translate(_pose->center);
	p.rotate(_pose->rotation);
	p.drawImage(FlightImageRect(_image, _pose->side), _image);
}

CardFlight::CardFlight(CardFlightArgs &&args)
: _square(std::move(args.square))
, _card(std::move(args.card))
, _done(std::move(args.done))
, _direction(args.direction)
, _animation([=](crl::time now) { return tick(now); }) {
	if (!args.image.isNull()) {
		_layer = base::make_unique_q<Layer>(args.body, std::move(args.image));
	}
}

CardFlight::~CardFlight() = default;

void CardFlight::start() {
	if (_finished || _animation.animating()) {
		return;
	}
	_started = crl::now();
	if (!step(0.)) {
		finish(false);
		return;
	}
	_layer->show();
	_layer->raise();
	_animation.start();
}

bool CardFlight::tick(crl::time now) {
	const auto progress = (now - _started) / float64(kFlightDuration);
	if (!step(progress)) {
		finish(false);
		return false;
	} else if (progress >= 1.) {
		finish(true);
		return false;
	}
	return true;
}

bool CardFlight::step(float64 progress) {
	if (!_layer || anim::Disabled()) {
		return false;
	}
	const auto square = _square();
	const auto card = _card();
	if (!ValidFlightPose(square) || !ValidFlightPose(card)) {
		return false;
	}
	const auto toCard = (_direction == CardFlightDirection::ToCard);
	const auto eased = FlightEaseOut(progress);
	const auto along = toCard ? eased : (1. - eased);
	const auto widest = toCard
		? square->side
		: std::max(square->side, card->side);
	const auto half = widest / 2.
		* RotationSpan(square->rotation, card->rotation);
	const auto control = CurveControl(
		square->center,
		card->center,
		half,
		_layer->width());
	_layer->setPose({
		.center = CurvePoint(square->center, control, card->center, along),
		.side = toCard
			? square->side
			: (card->side + (square->side - card->side) * eased),
		.rotation = square->rotation
			+ (card->rotation - square->rotation) * along,
	});
	return true;
}

void CardFlight::finish(bool arrived) {
	if (_finished) {
		return;
	}
	_finished = true;
	_animation.stop();
	_layer = nullptr;
	if (const auto done = base::take(_done)) {
		done(arrived);
	}
}

} // namespace Wallet
