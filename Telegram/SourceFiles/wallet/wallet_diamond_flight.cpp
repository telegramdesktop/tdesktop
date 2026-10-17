/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_diamond_flight.h"

#include "lottie/lottie_icon.h"
#include "ui/effects/reaction_fly_animation.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "wallet/wallet_amount_painter.h"

namespace Wallet {
namespace {

constexpr auto kFlightRise = 0.06;
constexpr auto kApexLatest = 0.4;
constexpr auto kDiamondCentre = QPointF(
	(kGramDiamondLeft + kGramDiamondRight) / 2.,
	(kGramDiamondTop + kGramDiamondBottom) / 2.);

[[nodiscard]] float64 FlightY(
		float64 from,
		float64 to,
		float64 rise,
		float64 t) {
	const auto start = int(base::SafeRound(from));
	const auto finish = int(base::SafeRound(to));
	const auto parabola = Ui::ComputeFlyParabola(
		start,
		finish,
		int(base::SafeRound(rise)));
	if (finish > start && parabola.a != 0.) {
		const auto apex = -parabola.b / (2. * parabola.a);
		if (apex <= kApexLatest) {
			return parabola.a * t * t + parabola.b * t + from;
		}
	}
	const auto top = std::min(from, to) - rise;
	if (t <= kApexLatest) {
		const auto u = 1. - t / kApexLatest;
		return top + (from - top) * u * u;
	}
	const auto u = (t - kApexLatest) / (1. - kApexLatest);
	return top + (to - top) * u * u;
}

[[nodiscard]] QPointF DiamondCentre(const QRectF &canvas) {
	return canvas.topLeft() + QPointF(
		canvas.width() * kDiamondCentre.x(),
		canvas.height() * kDiamondCentre.y());
}

[[nodiscard]] crl::time DiamondLoop(not_null<Lottie::Icon*> icon) {
	const auto frames = icon->framesCount();
	const auto rate = icon->frameRate();
	return (frames > 0 && rate > 0.)
		? crl::time(base::SafeRound(frames * 1000. / rate))
		: crl::time(0);
}

} // namespace

crl::time SendingDiamondLoopStart(
		not_null<Lottie::Icon*> icon,
		crl::time now) {
	const auto loop = DiamondLoop(icon);
	return (loop > 0)
		? (now - icon->frameIndex() * loop / icon->framesCount())
		: now;
}

void AdvanceSendingDiamond(
		not_null<Lottie::Icon*> icon,
		crl::time loopStarted,
		crl::time now) {
	if (anim::Disabled()
		|| PowerSaving::On(PowerSaving::kStickersChat)
		|| !icon->valid()) {
		return;
	}
	const auto loop = DiamondLoop(icon);
	if (loop <= 0) {
		return;
	}
	const auto frames = icon->framesCount();
	const auto index = std::min(
		int(((now - loopStarted) % loop) * frames / loop),
		frames - 1);
	if (index != icon->frameIndex()) {
		icon->jumpTo(index, nullptr);
	}
}

DiamondFlight::DiamondFlight(DiamondFlightArgs &&args)
: _body(args.body)
, _layer(Ui::CreateChild<Ui::RpWidget>(args.body.get()))
, _icon(std::move(args.icon))
, _target(std::move(args.target))
, _landed(std::move(args.landed))
, _finished(std::move(args.finished))
, _fromSide(args.from.width())
, _started(crl::now())
, _loopStarted(args.loopStarted)
, _duration(args.duration) {
	const auto layer = _layer.get();
	_body->sizeValue() | rpl::on_next([=](QSize size) {
		layer->setGeometry(QRect(QPoint(), size));
	}, layer->lifetime());
	layer->paintRequest() | rpl::on_next([=](QRect) {
		paint();
	}, layer->lifetime());
	layer->setAttribute(Qt::WA_TransparentForMouseEvents);
	layer->show();
	layer->raise();

	_current = args.from.translated(
		QPointF(layer->mapFromGlobal(QPoint())));
	_fromCentre = DiamondCentre(_current);
	updateArea(_current.toAlignedRect().marginsAdded({ 1, 1, 1, 1 }));

	_animation.init([=](crl::time now) { return tick(now); });
	_animation.start();
}

DiamondFlight::~DiamondFlight() = default;

bool DiamondFlight::tick(crl::time now) {
	const auto target = _target(now);
	if (!target) {
		_icon = nullptr;
		_layer->update(_area);
		_finished();
		return false;
	}
	const auto t = (_duration > 0)
		? std::clamp((now - _started) / float64(_duration), 0., 1.)
		: 1.;
	if (t >= 1.) {
		_landed(base::take(_icon), _loopStarted);
		_layer->update(_area);
		_finished();
		return false;
	}
	const auto toCentre = DiamondCentre(*target);
	const auto rise = std::max(
		std::min(
			base::SafeRound(kFlightRise * _body->height()),
			(std::min(_fromCentre.y(), toCentre.y())
				- _fromSide * kDiamondCentre.y())),
		0.);
	const auto side = _fromSide + (target->width() - _fromSide) * t;
	const auto centre = QPointF(
		_fromCentre.x() + (toCentre.x() - _fromCentre.x()) * t,
		FlightY(_fromCentre.y(), toCentre.y(), rise, t));
	_current = QRectF(
		centre - QPointF(side * kDiamondCentre.x(), side * kDiamondCentre.y()),
		QSizeF(side, side));
	updateArea(_current.toAlignedRect().marginsAdded({ 1, 1, 1, 1 }));
	return true;
}

void DiamondFlight::updateArea(QRect area) {
	_layer->update(_area.united(area));
	_area = area;
}

void DiamondFlight::paint() {
	if (!_icon || !_icon->valid()) {
		return;
	}
	AdvanceSendingDiamond(_icon.get(), _loopStarted, crl::now());
	const auto size = _icon->size();
	if (size.isEmpty()) {
		return;
	}
	auto p = QPainter(_layer.get());
	auto hq = PainterHighQualityEnabler(p);
	p.translate(_current.topLeft());
	p.scale(
		_current.width() / size.width(),
		_current.height() / size.height());
	_icon->paint(p, 0, 0);
}

} // namespace Wallet
