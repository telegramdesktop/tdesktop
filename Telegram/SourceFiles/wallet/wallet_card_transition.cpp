/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_card_transition.h"

#include "data/data_session.h"
#include "main/main_session.h"
#include "ui/painter.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_card_flight.h"
#include "wallet/wallet_card_stickers.h"
#include "wallet/wallet_own_card.h"
#include "wallet/wallet_session.h"

#include <QtGui/QPaintEngine>

#include "styles/style_wallet.h"

namespace Wallet {
namespace {

constexpr auto kWaveDuration = crl::time(320);
constexpr auto kFadeDuration = crl::time(460);
constexpr auto kRimInside = 0.024;
constexpr auto kRimOutside = 0.016;
constexpr auto kRimAlpha = 0.10;

struct Disc {
	QPointF center;
	float64 radius = 0.;
};

[[nodiscard]] crl::time RevealDuration(CardLookReveal reveal) {
	return (reveal == CardLookReveal::Wave)
		? kWaveDuration
		: (reveal == CardLookReveal::Fade)
		? kFadeDuration
		: crl::time(0);
}

[[nodiscard]] float64 OutlineReach(const QRectF &card, QPointF origin) {
	const auto radius = float64(st::walletCardRadius);
	const auto inner = card.marginsRemoved(
		QMarginsF(radius, radius, radius, radius));
	const auto corners = std::array{
		inner.topLeft(),
		inner.topRight(),
		inner.bottomLeft(),
		inner.bottomRight(),
	};
	auto result = 0.;
	for (const auto &corner : corners) {
		accumulate_max(result, QLineF(origin, corner).length());
	}
	return result + radius;
}

[[nodiscard]] Disc WaveDisc(
		const QRect &card,
		const CardLookChange &change,
		float64 progress) {
	if (change.reveal != CardLookReveal::Wave) {
		return {};
	}
	const auto area = QRectF(card);
	const auto center = area.topLeft() + QPointF(
		change.origin.x() * area.width(),
		change.origin.y() * area.height());
	const auto reach = OutlineReach(area, center)
		+ CardLookRimInside(area.width());
	return { .center = center, .radius = reach * FlightEaseOut(progress) };
}

[[nodiscard]] QRect DevicePixels(const QPainter &p, const QRect &card) {
	const auto &device = p.deviceTransform();
	return (card.isEmpty() || !device.isInvertible())
		? QRect()
		: device.mapRect(QRectF(card)).toAlignedRect();
}

void SetDeviceTransform(QPainter &p, const QTransform &device) {
	p.setTransform(device * p.deviceTransform().inverted(), true);
}

void PrepareScratch(QImage &image, QSize size, float64 ratio) {
	if (image.width() < size.width() || image.height() < size.height()) {
		image = QImage(
			size.expandedTo(image.size()),
			QImage::Format_ARGB32_Premultiplied);
	}
	image.setDevicePixelRatio(ratio);
	image.fill(Qt::transparent);
}

[[nodiscard]] std::optional<QPointF> SourceOrigin(
		const OwnCardState &state,
		const QString &source) {
	const auto i = ranges::find_if(state.stickers, [&](const auto &sticker) {
		return (CanonicalAddress(sticker.address) == source);
	});
	if (i == end(state.stickers)) {
		return std::nullopt;
	}
	const auto center = i->area.geometry.center();
	if (!std::isfinite(center.x()) || !std::isfinite(center.y())) {
		return std::nullopt;
	}
	return center;
}

} // namespace

crl::time CardLookFadeDuration() {
	return kFadeDuration;
}

float64 CardLookFadeValue(float64 progress) {
	return anim::sineInOut(1., progress);
}

float64 CardLookRimInside(float64 width) {
	return kRimInside * width;
}

void PaintCardLookRim(
		QPainter &p,
		QPointF center,
		float64 radius,
		float64 width) {
	const auto inside = CardLookRimInside(width);
	const auto outer = radius + kRimOutside * width;
	const auto peak = QColor::fromRgbF(1., 1., 1., kRimAlpha);
	const auto clear = QColor(255, 255, 255, 0);
	auto gradient = QRadialGradient(center, outer);
	auto band = QPainterPath();
	band.addEllipse(center, outer, outer);
	if (radius > inside) {
		gradient.setColorAt((radius - inside) / outer, clear);
		band.addEllipse(center, radius - inside, radius - inside);
	} else {
		gradient.setColorAt(0., anim::color(peak, clear, radius / inside));
	}
	gradient.setColorAt(radius / outer, peak);
	gradient.setColorAt(1., clear);
	p.save();
	p.setCompositionMode(QPainter::CompositionMode_SourceAtop);
	p.setPen(Qt::NoPen);
	p.setBrush(gradient);
	p.drawPath(band);
	p.restore();
}

void DrawDevicePixels(QPainter &p, const QRect &pixels, const QImage &image) {
	const auto source = QRect(QPoint(), pixels.size());
	p.save();
	SetDeviceTransform(p, QTransform::fromTranslate(pixels.x(), pixels.y()));
	p.drawImage(source, image, source);
	p.restore();
}

struct CardLookTransition::Layer {
	CardLook look;
	std::unique_ptr<CardLookPainter> painter;
	CardLookChange change;
	crl::time started = 0;
};

struct CardLookTransition::Frame {
	QRect card;
	QRect pixels;
	QTransform transform;
	float64 ratio = 1.;
	float64 angle = 0.;
	crl::time now = 0;
	int from = 0;
};

CardLookTransition::CardLookTransition(Fn<void()> repaint)
: _repaint(std::move(repaint))
, _animation([=](crl::time now) { return step(now); }) {
	_layers.push_back({
		.painter = std::make_unique<CardLookPainter>(_repaint),
	});
	anim::Disables() | rpl::filter([](bool disabled) {
		return disabled;
	}) | rpl::on_next([=] {
		settle();
	}, _lifetime);
}

CardLookTransition::~CardLookTransition() {
	_lifetime.destroy();
	_animation.stop();
	_layers.clear();
}

void CardLookTransition::setLook(CardLook look, CardLookChange change) {
	const auto now = crl::now();
	prune(now);
	if (change.reveal == CardLookReveal::Instant || anim::Disabled()) {
		settle();
		auto &layer = _layers.back();
		layer.look = look;
		if (layer.painter->setLook(std::move(look))) {
			_repaint();
		}
		return;
	} else if (_layers.back().look == look) {
		return;
	}
	auto painter = std::make_unique<CardLookPainter>(_repaint);
	[[maybe_unused]] const auto changed = painter->setLook(look);
	_layers.push_back({
		.look = std::move(look),
		.painter = std::move(painter),
		.change = change,
		.started = now,
	});
	refresh();
	if (!_animation.animating()) {
		_animation.start();
	}
	_repaint();
}

void CardLookTransition::paint(
		Painter &p,
		const QRect &card,
		float64 angle,
		const Fn<void(Painter&, QColor)> &foreground) {
	const auto now = crl::now();
	const auto from = settledIndex(now);
	const auto base = _layers[from].painter.get();
	const auto pixels = (from + 1 < int(_layers.size()))
		? DevicePixels(p, card)
		: QRect();
	if (pixels.isEmpty()) {
		base->paint(p, card, angle);
		foreground(p, base->stripInk());
		return;
	}
	const auto frame = Frame{
		.card = card,
		.pixels = pixels,
		.transform = p.deviceTransform()
			* QTransform::fromTranslate(-pixels.x(), -pixels.y()),
		.ratio = p.paintEngine()->paintDevice()->devicePixelRatio(),
		.angle = angle,
		.now = now,
		.from = from,
	};
	compose(frame, foreground);
	DrawDevicePixels(p, pixels, _composed);
}

rpl::producer<bool> CardLookTransition::runningValue() const {
	return _running.value();
}

float64 CardLookTransition::Progress(const Layer &layer, crl::time now) {
	const auto duration = RevealDuration(layer.change.reveal);
	return (duration > 0)
		? std::clamp((now - layer.started) / float64(duration), 0., 1.)
		: 1.;
}

int CardLookTransition::settledIndex(crl::time now) const {
	for (auto i = int(_layers.size()) - 1; i > 0; --i) {
		if (Progress(_layers[i], now) >= 1.) {
			return i;
		}
	}
	return 0;
}

bool CardLookTransition::step(crl::time now) {
	prune(now);
	_repaint();
	return (_layers.size() > 1);
}

void CardLookTransition::prune(crl::time now) {
	if (const auto index = settledIndex(now)) {
		_layers.erase(begin(_layers), begin(_layers) + index);
		_layers.front().change = CardLookChange();
	}
	refresh();
}

void CardLookTransition::settle() {
	if (_layers.size() > 1) {
		_layers.erase(begin(_layers), end(_layers) - 1);
		_layers.front().change = CardLookChange();
		_repaint();
	}
	refresh();
}

void CardLookTransition::refresh() {
	if (_layers.size() == 1) {
		_animation.stop();
		_composed = QImage();
		_scratch = QImage();
	}
	_running = (_layers.size() > 1);
}

void CardLookTransition::compose(
		const Frame &frame,
		const Fn<void(Painter&, QColor)> &foreground) {
	const auto count = int(_layers.size());
	const auto draw = [&](QImage &image, int index) {
		PrepareScratch(image, frame.pixels.size(), frame.ratio);
		auto q = Painter(&image);
		auto hq = PainterHighQualityEnabler(q);
		SetDeviceTransform(q, frame.transform);
		auto &look = *_layers[index].painter;
		look.paint(q, frame.card, frame.angle);
		for (auto i = std::max(index, frame.from + 1); i != count; ++i) {
			const auto &above = _layers[i];
			const auto progress = Progress(above, frame.now);
			const auto disc = WaveDisc(frame.card, above.change, progress);
			if (disc.radius > 0.) {
				PaintCardLookRim(
					q,
					disc.center,
					disc.radius,
					frame.card.width());
			}
		}
		foreground(q, look.stripInk());
	};
	draw(_composed, frame.from);
	for (auto i = frame.from + 1; i != count; ++i) {
		draw(_scratch, i);
		reveal(_layers[i], frame);
	}
}

void CardLookTransition::reveal(const Layer &layer, const Frame &frame) {
	const auto progress = Progress(layer, frame.now);
	const auto rect = QRect(QPoint(), frame.pixels.size());
	auto q = QPainter(&_composed);
	auto hq = PainterHighQualityEnabler(q);
	SetDeviceTransform(q, QTransform());
	// WHY: Source interpolates premultiplied pixels by the clip coverage or
	// the opacity, so the rounded edges keep their alpha instead of stacking.
	q.setCompositionMode(QPainter::CompositionMode_Source);
	if (layer.change.reveal == CardLookReveal::Fade) {
		q.setOpacity(CardLookFadeValue(progress));
	} else {
		const auto disc = WaveDisc(frame.card, layer.change, progress);
		if (disc.radius <= 0.) {
			return;
		}
		auto path = QPainterPath();
		path.addEllipse(disc.center, disc.radius, disc.radius);
		q.setClipPath(frame.transform.map(path));
	}
	q.drawImage(rect, _scratch, rect);
}

CardLookFollower::CardLookFollower(CardLookFollowerArgs &&args)
: _session(args.session)
, _stickers(args.stickers)
, _apply(std::move(args.apply)) {
	_session->data().sessionDataAboutToBeCleared(
	) | rpl::on_next([=] {
		clear();
	}, _lifetime);
	_stickers->releases(
	) | rpl::on_next([=](const QString &address) {
		released(address);
	}, _lifetime);
	anim::Disables() | rpl::filter([](bool disabled) {
		return disabled;
	}) | rpl::on_next([=] {
		if (auto waiting = base::take(_waiting)) {
			present(std::move(*waiting), {});
		}
	}, _lifetime);
	OwnCardLookValue(
		_session
	) | rpl::on_next([=](CardLook &&look) {
		changed(std::move(look));
	}, _lifetime);
}

CardLookFollower::~CardLookFollower() {
	clear();
}

void CardLookFollower::changed(CardLook look) {
	_waiting.reset();
	if (!_initialized) {
		_initialized = true;
		present(std::move(look), {});
	} else if (look != _target) {
		if (look.brand()) {
			present(std::move(look), { .reveal = CardLookReveal::Fade });
		} else if (!anim::Disabled() && _stickers->held(look.source)) {
			_waiting = std::move(look);
		} else {
			wave(std::move(look));
		}
	}
}

void CardLookFollower::released(const QString &address) {
	if (!_waiting || _waiting->source != address) {
		return;
	}
	_waiting.reset();
	const auto state = _session->wallet().ownCard().shown();
	changed(ResolveCardLook(state.stickers, state.background));
}

void CardLookFollower::wave(CardLook look) {
	const auto origin = SourceOrigin(
		_session->wallet().ownCard().shown(),
		look.source);
	present(std::move(look), origin
		? CardLookChange{ .reveal = CardLookReveal::Wave, .origin = *origin }
		: CardLookChange{ .reveal = CardLookReveal::Fade });
}

void CardLookFollower::present(CardLook look, CardLookChange change) {
	_target = look;
	_apply(std::move(look), change);
}

void CardLookFollower::clear() {
	_lifetime.destroy();
	_waiting.reset();
	_target = CardLook();
	_apply(CardLook(), {});
}

} // namespace Wallet
