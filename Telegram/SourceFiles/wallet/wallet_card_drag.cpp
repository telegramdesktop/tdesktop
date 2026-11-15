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

constexpr auto kSquareLiftScale = 1.05;
constexpr auto kSquareLiftDuration = crl::time(50);
constexpr auto kSquareReturnDuration = crl::time(185);
constexpr auto kCardLiftScale = 1.28;
constexpr auto kCardLiftDuration = crl::time(100);
constexpr auto kCardReturnDuration = crl::time(140);
constexpr auto kSettleDuration = crl::time(110);
constexpr auto kReturnFade = 0.5;
constexpr auto kReturnFadeDuration = crl::time(100);
constexpr auto kVanishDuration = crl::time(150);
constexpr auto kVanishShrink = 0.4;
constexpr auto kScaleOutDuration = crl::time(180);
constexpr auto kDriftDecay = crl::time(35);
constexpr auto kVelocityWindow = crl::time(50);

struct PreparedFrame {
	QImage image;
	float64 scale = 1.;
};

[[nodiscard]] FlightPose Interpolate(
		const FlightPose &from,
		const FlightPose &to,
		float64 along) {
	const auto turn = std::remainder(to.rotation - from.rotation, 360.);
	return {
		.center = from.center + (to.center - from.center) * along,
		.side = from.side + (to.side - from.side) * along,
		.rotation = from.rotation + turn * along,
	};
}

[[nodiscard]] PreparedFrame PrepareFrame(QImage frame, bool outline) {
	if (!outline || frame.isNull()) {
		return { .image = std::move(frame) };
	}
	const auto extent = std::max(frame.width(), frame.height());
	auto image = CardStickerOutlined(
		std::move(frame),
		style::DevicePixelRatio());
	const auto scale = std::max(image.width(), image.height())
		/ float64(extent);
	return { .image = std::move(image), .scale = scale };
}

} // namespace

struct DraggedGift::Frame {
	FlightPose pose;
	float64 opacity = 1.;
};

class DraggedGift::Layer final : public Ui::RpWidget {
public:
	explicit Layer(DraggedGiftArgs &&args);

	void setFrame(const Frame &frame);
	[[nodiscard]] TravelArtwork takeArtwork();

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	[[nodiscard]] bool sync();
	[[nodiscard]] QTransform transform(const Frame &frame) const;
	[[nodiscard]] QRectF imageRect(float64 side) const;
	[[nodiscard]] QRect mappedArea(const Frame &frame) const;
	void setImage(PreparedFrame prepared);
	bool advance();
	void presented();
	void rest();

	const not_null<Ui::RpWidget*> _body;
	const int _titleExtension = 0;
	const bool _outline = false;
	TravelArtwork _artwork;
	float64 _scale = 1.;
	float64 _restScale = 1.;
	std::optional<Frame> _frame;
	QPoint _origin;
	QRect _area;

};

DraggedGift::Layer::Layer(DraggedGiftArgs &&args)
: RpWidget(args.body->window())
, _body(args.body)
, _titleExtension(args.titleExtension)
, _outline(args.outline)
, _artwork({
	.playback = {
		.player = std::move(args.player),
		.frameSize = args.frameSize,
		.side = std::max(args.frameSize.width(), args.frameSize.height()),
	},
	.document = args.document,
}) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	MarkTravelLayer(this);
	auto rest = PrepareFrame(std::move(args.firstFrame), _outline);
	_artwork.rest = rest.image;
	_restScale = rest.scale;
	setImage(args.image.isNull()
		? std::move(rest)
		: PrepareFrame(std::move(args.image), _outline));
	if (_artwork.playback.player) {
		BindStickerPlayback(_artwork.playback, [=] { update(_area); });
		advance();
	}
	_body->geometryValue() | rpl::on_next([=] {
		if (_frame) {
			setFrame(*_frame);
		} else if (sync()) {
			update();
		}
	}, lifetime());
}

void DraggedGift::Layer::setFrame(const Frame &frame) {
	const auto moved = sync();
	const auto area = mappedArea(frame);
	_frame = frame;
	update(moved ? rect() : _area.united(area));
	_area = area;
}

TravelArtwork DraggedGift::Layer::takeArtwork() {
	if (const auto player = _artwork.playback.player.get()) {
		player->setRepaintCallback(nullptr);
	}
	return base::take(_artwork);
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
		.rotate(frame.pose.rotation);
}

QRectF DraggedGift::Layer::imageRect(float64 side) const {
	return FlightImageRect(_artwork.playback.frame, side * _scale);
}

QRect DraggedGift::Layer::mappedArea(const Frame &frame) const {
	return transform(frame)
		.mapRect(imageRect(frame.pose.side))
		.toAlignedRect()
		.marginsAdded({ 1, 1, 1, 1 });
}

void DraggedGift::Layer::setImage(PreparedFrame prepared) {
	_artwork.playback.frame = std::move(prepared.image);
	_scale = prepared.scale;
	if (!_frame) {
		return;
	}
	const auto area = mappedArea(*_frame);
	if (area != _area) {
		update(_area.united(area));
		_area = area;
	}
}

bool DraggedGift::Layer::advance() {
	if (!_artwork.playback.player) {
		return false;
	} else if (TravelArtworkPaused()) {
		rest();
		return false;
	}
	auto step = AdvanceStickerPlayback(_artwork.playback, crl::now());
	if (step.finished) {
		rest();
		return false;
	} else if (!step.fresh.isNull()) {
		setImage(PrepareFrame(std::move(step.fresh), _outline));
	}
	return step.show;
}

void DraggedGift::Layer::presented() {
	if (!PresentStickerPlayback(_artwork.playback)) {
		rest();
	}
}

void DraggedGift::Layer::rest() {
	auto &playback = _artwork.playback;
	playback.player = nullptr;
	playback.index = 0;
	playback.advanced = false;
	playback.awaitingFrame = false;
	if (!_artwork.rest.isNull()) {
		setImage({ .image = _artwork.rest, .scale = _restScale });
	}
}

void DraggedGift::Layer::paintEvent(QPaintEvent *e) {
	if (!_frame) {
		return;
	}
	const auto playing = advance();
	const auto &image = _artwork.playback.frame;
	if (image.isNull()) {
		return;
	}
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	p.setOpacity(_frame->opacity);
	p.setTransform(transform(*_frame), true);
	p.drawImage(imageRect(_frame->pose.side), image);
	if (playing) {
		presented();
	}
}

DraggedGift::DraggedGift(DraggedGiftArgs &&args)
: _moved(std::move(args.moved))
, _source(args.source)
, _cursor(args.cursor)
, _grab(args.grab)
, _sampled(args.cursor)
, _scaleStarted(crl::now())
, _sampledAt(_scaleStarted)
, _origin(args.origin)
, _animation([=](crl::time now) { return tick(now); }) {
	if (!args.image.isNull() || !args.firstFrame.isNull()) {
		_layer = base::make_unique_q<Layer>(std::move(args));
	}
	apply({ .pose = followPose(_scaleStarted) });
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

TravelArtwork DraggedGift::takeArtwork() {
	return _layer ? _layer->takeArtwork() : base::take(_artwork);
}

void DraggedGift::move(QPointF cursor) {
	if (!following()) {
		return;
	}
	const auto now = crl::now();
	if (cursor != _sampled && now > _sampledAt) {
		_velocity = (cursor - _sampled) / float64(now - _sampledAt);
		_sampled = cursor;
		_sampledAt = now;
	}
	_cursor = cursor;
	follow(now);
}

void DraggedGift::settle(
		Fn<std::optional<FlightPose>()> target,
		Fn<void(bool arrived)> done) {
	travel(Phase::Settling, std::move(target), std::move(done));
}

void DraggedGift::returnTo(
		Fn<std::optional<FlightPose>()> square,
		Fn<void(bool arrived)> done) {
	travel(Phase::Returning, std::move(square), std::move(done));
}

void DraggedGift::vanish(Fn<void()> done) {
	disappear(Phase::Vanishing, std::move(done));
}

void DraggedGift::scaleOut(Fn<void()> done) {
	disappear(Phase::ScalingOut, std::move(done));
}

void DraggedGift::finish() {
	if (following() || _phase == Phase::Done) {
		return;
	}
	complete(disappearing() || (_target && ValidFlightPose(_target())));
}

float64 DraggedGift::liftScale() const {
	return (_origin == DraggedGiftSource::Card)
		? kCardLiftScale
		: kSquareLiftScale;
}

crl::time DraggedGift::liftDuration() const {
	return (_origin == DraggedGiftSource::Card)
		? kCardLiftDuration
		: kSquareLiftDuration;
}

float64 DraggedGift::followScale(crl::time now) const {
	const auto progress = anim::Disabled()
		? 1.
		: ((now - _scaleStarted) / float64(liftDuration()));
	return 1. + (liftScale() - 1.) * FlightEaseOut(progress);
}

FlightPose DraggedGift::followPose(crl::time now) const {
	const auto scale = followScale(now);
	return {
		.center = _cursor - _grab * scale,
		.side = _source.side * scale,
		.rotation = _source.rotation,
	};
}

QPointF DraggedGift::releaseDrift(crl::time now) const {
	if (now - _sampledAt > kVelocityWindow) {
		return QPointF();
	}
	const auto drift = _velocity * float64(kDriftDecay);
	const auto length = std::hypot(drift.x(), drift.y());
	if (!std::isfinite(length)) {
		return QPointF();
	}
	return (length > _from.side) ? (drift * (_from.side / length)) : drift;
}

bool DraggedGift::disappearing() const {
	return (_phase == Phase::Vanishing) || (_phase == Phase::ScalingOut);
}

crl::time DraggedGift::duration() const {
	const auto card = (_origin == DraggedGiftSource::Card);
	switch (_phase) {
	case Phase::Settling: return kSettleDuration;
	case Phase::Returning:
		return card ? kCardReturnDuration : kSquareReturnDuration;
	case Phase::Vanishing: return kVanishDuration;
	case Phase::ScalingOut: return kScaleOutDuration;
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
	} else if (_phase == Phase::ScalingOut) {
		const auto drifted = 1. - std::exp(-elapsed / kDriftDecay);
		return {
			.pose = {
				.center = _from.center + _drift * drifted,
				.side = _from.side * (1. - along),
				.rotation = _from.rotation,
			},
			.opacity = 1. - along * along,
		};
	}
	auto result = Frame{ .pose = Interpolate(_from, target, along) };
	if (_phase == Phase::Returning) {
		const auto fade = std::min(elapsed / kReturnFadeDuration, 1.);
		result.opacity = 1. - kReturnFade * std::sin(M_PI * fade);
	}
	return result;
}

void DraggedGift::disappear(Phase phase, Fn<void()> done) {
	travel(phase, nullptr, [done = std::move(done)](bool) {
		if (done) {
			done();
		}
	});
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
	_drift = (phase == Phase::ScalingOut) ? releaseDrift(now) : QPointF();
	_target = std::move(target);
	_done = std::move(done);
	_started = now;
	if (step(now)) {
		_animation.start();
	}
}

void DraggedGift::follow(crl::time now) {
	apply({ .pose = followPose(now) });
	if (_moved) {
		_moved(_pose);
	}
}

bool DraggedGift::tick(crl::time now) {
	if (!following()) {
		return step(now);
	}
	follow(now);
	return !anim::Disabled() && (now - _scaleStarted < liftDuration());
}

bool DraggedGift::step(crl::time now) {
	const auto target = disappearing()
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
		_artwork = layer->takeArtwork();
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
