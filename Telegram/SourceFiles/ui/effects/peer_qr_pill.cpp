/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/peer_qr_pill.h"

#include "lang/lang_keys.h"
#include "lang/lang_tag.h"
#include "ui/abstract_button.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "styles/style_boxes.h"

namespace Ui {
namespace {

struct Range {
	float64 from = 0.;
	float64 till = 0.;
};

struct LifeRange {
	crl::time from = 0;
	crl::time till = 0;
};

constexpr auto kDemoCardWidth = 242.;
constexpr auto kFillDuration = crl::time(340);
constexpr auto kBounceDuration = crl::time(320);
constexpr auto kShatterFillFade = crl::time(60);
constexpr auto kShatterTextFade = crl::time(80);
constexpr auto kMaxStep = crl::time(33);
constexpr auto kFillOpacity = 0.97;
constexpr auto kFeeDim = 1.25;
constexpr auto kSlashFromX = 0.28;
constexpr auto kSlashFromY = -0.21;
constexpr auto kSlashTillX = 0.74;
constexpr auto kSlashTillY = 0.25;
constexpr auto kSlashWidth = 0.095;
constexpr auto kSlashGap = 0.065;
constexpr auto kDustSize = Range{ 1.4, 3. };
constexpr auto kSparkSize = Range{ 0.7, 1.4 };
constexpr auto kChunkSize = Range{ 3., 4.5 };
constexpr auto kHaloScale = 3.;
constexpr auto kHaloAlpha = 0.2;
constexpr auto kFillBurst = 24;
constexpr auto kFillBurstShare = 0.4;
constexpr auto kFillRate = 260.;
constexpr auto kFillDustShare = 0.65;
constexpr auto kFillSparkShare = 0.25;
constexpr auto kFillVelocityX = Range{ -30., 50. };
constexpr auto kFillVelocityY = Range{ 50., 150. };
constexpr auto kFillLife = LifeRange{ crl::time(300), crl::time(500) };
constexpr auto kFillDrag = 3.;
constexpr auto kFillLift = 20.;
constexpr auto kFillCeiling = 40.;
constexpr auto kFizzRate = 110.;
constexpr auto kFizzSize = Range{ 0.8, 1.5 };
constexpr auto kFizzBand = 5.;
constexpr auto kFizzVelocityX = Range{ 10., 30. };
constexpr auto kFizzVelocityY = Range{ 4., 14. };
constexpr auto kFizzLife = LifeRange{ crl::time(250), crl::time(500) };
constexpr auto kTrailRate = 70.;
constexpr auto kTrailSparkShare = 0.12;
constexpr auto kTrailSize = Range{ 0.8, 1.5 };
constexpr auto kTrailVelocityX = Range{ 40., 110. };
constexpr auto kTrailVelocityY = Range{ 10., 40. };
constexpr auto kTrailLift = Range{ 26., 60. };
constexpr auto kTrailWobble = Range{ 4., 7. };
constexpr auto kTrailWobbleAmplitude = 2.;
constexpr auto kTrailLife = LifeRange{ crl::time(600), crl::time(1300) };
constexpr auto kTrailCeiling = 48.;
constexpr auto kShatterCount = 260;
constexpr auto kShatterGrid = 3.;
constexpr auto kShatterSpeed = Range{ 60., 200. };
constexpr auto kShatterRise = Range{ 40., 120. };
constexpr auto kShatterSparkShare = 0.15;
constexpr auto kShatterLife = LifeRange{ crl::time(350), crl::time(650) };
constexpr auto kShatterDrag = 2.4;
constexpr auto kShatterGravity = 60.;
constexpr auto kShatterGravityDelay = crl::time(120);
constexpr auto kCeilingSoftness = 12.;
constexpr auto kMaxParticles = 400;

[[nodiscard]] float64 Dp(float64 value) {
	return value * st::peerQrCardWidth / kDemoCardWidth;
}

[[nodiscard]] int ExtendAbove() {
	return int(std::ceil(Dp(kTrailCeiling)));
}

[[nodiscard]] float64 RandomIn(ParticlesRandom &random, Range range) {
	return random.value(range.from, range.till);
}

[[nodiscard]] crl::time RandomIn(ParticlesRandom &random, LifeRange range) {
	return range.from + crl::time(random.value() * (range.till - range.from));
}

[[nodiscard]] int CountdownSeconds(crl::time left) {
	return int(std::max(left + 999, crl::time(0)) / 1000);
}

[[nodiscard]] QString CountdownText(int seconds) {
	return tr::lng_no_paid_token_pill_temporary(
		tr::now,
		lt_time,
		u"%1:%2"_q.arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0')));
}

[[nodiscard]] QRectF ScaledRect(const QRectF &rect, float64 scale) {
	const auto size = rect.size() * scale;
	return QRectF(
		rect.center() - QPointF(size.width(), size.height()) / 2.,
		size);
}

[[nodiscard]] bool InsidePill(const QRectF &pill, QPointF point) {
	const auto radius = pill.height() / 2.;
	const auto center = QPointF(
		std::max(
			pill.x() + radius,
			std::min(point.x(), pill.x() + pill.width() - radius)),
		pill.y() + radius);
	const auto delta = point - center;
	return QPointF::dotProduct(delta, delta) <= radius * radius;
}

[[nodiscard]] float64 Falloff(QPointF position, QPointF origin, float64 fade) {
	if (fade <= 0.) {
		return 1.;
	}
	const auto delta = position - origin;
	return std::clamp(
		1. - std::hypot(delta.x(), delta.y()) / fade,
		0.,
		1.);
}

[[nodiscard]] float64 CeilingFactor(float64 y, float64 ceiling) {
	return std::clamp((y - ceiling) / Dp(kCeilingSoftness), 0., 1.);
}

[[nodiscard]] QImage PrepareStarBox(const QColor &color, bool crossed) {
	const auto ratio = style::DevicePixelRatio();
	const auto box = st::peerQrPillStarBox;
	const auto &star = st::peerQrPillStar;
	auto result = QImage(
		QSize(box, box) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	auto p = QPainter(&result);
	p.drawImage(
		QPoint((box - star.width()) / 2, (box - star.height()) / 2),
		star.instance(color));
	if (crossed) {
		auto hq = PainterHighQualityEnabler(p);
		const auto from = QPointF(kSlashFromX, 0.5 + kSlashFromY) * box;
		const auto till = QPointF(kSlashTillX, 0.5 + kSlashTillY) * box;
		p.setCompositionMode(QPainter::CompositionMode_Clear);
		p.setPen(QPen(
			Qt::black,
			(kSlashWidth + 2. * kSlashGap) * box,
			Qt::SolidLine,
			Qt::RoundCap));
		p.drawLine(from, till);
		p.setCompositionMode(QPainter::CompositionMode_SourceOver);
		p.setPen(QPen(color, kSlashWidth * box, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(from, till);
	}
	p.end();
	return result;
}

} // namespace

struct PeerQrPill::Particle {
	QPointF position;
	QPointF velocity;
	QPointF origin;
	float64 lift = 0.;
	float64 drag = 0.;
	float64 gravity = 0.;
	float64 radius = 0.;
	float64 ceiling = 0.;
	float64 fade = 0.;
	float64 wobbleFrequency = 0.;
	float64 wobblePhase = 0.;
	float64 wobbleAmplitude = 0.;
	float64 halo = 0.;
	crl::time born = 0;
	crl::time life = 0;
	crl::time gravityAfter = 0;
};

struct PeerQrPill::Frame {
	QRectF pill;
	float64 edge = 0.;
	float64 fill = 0.;
	float64 fillAlpha = 1.;
	float64 feeAlpha = 0.;
	float64 freeAlpha = 0.;
	float64 glassAlpha = 0.;
	float64 scale = 1.;
	bool feeClipped = false;
};

PeerQrPill::PeerQrPill(not_null<QWidget*> parent, PeerQrColors colors)
: RpWidget(parent)
, _colors(std::move(colors))
, _feeStar(PrepareStarBox(_colors.foreground, false))
, _crossedInk(PrepareStarBox(_colors.ink, true))
, _crossedForeground(PrepareStarBox(_colors.foreground, true))
, _button(base::make_unique_q<AbstractButton>(parent))
, _freeText(st::peerQrPillFont, [=] { updatePill(); })
, _animation([=](crl::time now) { return animationCallback(now); })
, _timer([=] { tick(); }) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	PowerSaving::Changes() | rpl::on_next([=] {
		checkAnimation();
		update();
	}, lifetime());
	stateChanged();
}

PeerQrPill::~PeerQrPill() = default;

void PeerQrPill::setColors(PeerQrColors colors) {
	if (_colors == colors) {
		return;
	}
	_colors = std::move(colors);
	_feeStar = PrepareStarBox(_colors.foreground, false);
	_crossedInk = PrepareStarBox(_colors.ink, true);
	_crossedForeground = PrepareStarBox(_colors.foreground, true);
	update();
}

void PeerQrPill::setPillPosition(int centerX, int top) {
	const auto above = ExtendAbove();
	_centerX = centerX;
	setGeometry(
		0,
		top - above,
		parentWidget()->width(),
		above + st::peerQrPillHeight + st::peerQrCardSkip);
	refreshButton(computeFrame(crl::now()));
}

void PeerQrPill::showFee(int amount) {
	_feeText = tr::lng_no_paid_token_pill_fee(
		tr::now,
		lt_amount,
		Lang::FormatCountDecimal(amount));
	_feeWidth = st::peerQrPillFont->width(_feeText);
	if (_mode == Mode::Temporary || _mode == Mode::Permanent) {
		startTransition(Transition::Shatter, crl::now());
	}
	_mode = Mode::Fee;
	stateChanged();
}

void PeerQrPill::showTemporary(crl::time arrival, crl::time deadline) {
	const auto now = crl::now();
	startTransition(freeTransition(), now);
	_mode = Mode::Temporary;
	_arrival = arrival;
	_deadline = deadline;
	_seconds = CountdownSeconds(deadline - now);
	_freeText.setText(
		CountdownText(_seconds),
		(_transition == Transition::Refill)
			|| (_transition == Transition::Retext));
	stateChanged();
}

void PeerQrPill::setDeadline(crl::time deadline) {
	if (_deadline == deadline) {
		return;
	}
	_deadline = deadline;
	if (_mode == Mode::Temporary) {
		refreshCountdown(crl::now());
		checkAnimation();
		updatePill();
	}
}

void PeerQrPill::showPermanent() {
	startTransition(freeTransition(), crl::now());
	_mode = Mode::Permanent;
	_freeText.setText(
		tr::lng_no_paid_token_pill_permanent(tr::now),
		(_transition == Transition::Refill)
			|| (_transition == Transition::Retext));
	stateChanged();
}

void PeerQrPill::hidePill() {
	_mode = Mode::Hidden;
	_transition = Transition::None;
	_particles.clear();
	_animation.stop();
	_timer.cancel();
	stateChanged();
}

QRect PeerQrPill::pillGeometry() const {
	return (_mode == Mode::Hidden)
		? QRect()
		: computeFrame(crl::now()).pill.toAlignedRect().translated(pos());
}

rpl::producer<> PeerQrPill::clicks() const {
	return _button->clicks() | rpl::to_empty;
}

void PeerQrPill::paintEvent(QPaintEvent *e) {
	if (_mode == Mode::Hidden) {
		return;
	}
	const auto now = crl::now();
	const auto frame = computeFrame(now);
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	p.save();
	if (frame.scale != 1.) {
		const auto center = frame.pill.center();
		p.translate(center);
		p.scale(frame.scale, frame.scale);
		p.translate(-center);
	}
	paintPill(p, frame);
	p.restore();
	paintParticles(p, now);
}

PeerQrPill::Frame PeerQrPill::computeFrame(crl::time now) const {
	const auto elapsed = now - _transitionStartedAt;
	const auto progress = (_transition == Transition::None)
		? 1.
		: std::clamp(float64(elapsed) / kFillDuration, 0., 1.);
	const auto eased = anim::easeOutCubic(1., progress);
	const auto width = _widthFrom + (targetWidth() - _widthFrom) * eased;
	auto result = Frame{
		.pill = QRectF(
			std::floor(_centerX - width / 2.),
			ExtendAbove(),
			width,
			st::peerQrPillHeight),
	};
	if (_mode == Mode::Temporary || _mode == Mode::Permanent) {
		const auto target = targetFill(now);
		const auto sweep = (_transition == Transition::Sweep);
		const auto filling = sweep || (_transition == Transition::Refill);
		result.fill = filling
			? (_fillFrom + (target - _fillFrom) * eased)
			: target;
		result.freeAlpha = 1.;
		result.glassAlpha = sweep ? 0. : 1.;
		result.feeAlpha = sweep ? std::max(0., 1. - kFeeDim * eased) : 0.;
		result.feeClipped = sweep;
		if (filling || (_transition == Transition::Retext)) {
			result.scale = PeerQrBounceScale(
				float64(elapsed) / kBounceDuration);
		}
	} else if (_transition == Transition::Shatter) {
		const auto fade = [&](crl::time duration) {
			return std::max(0., 1. - float64(elapsed) / duration);
		};
		result.fill = _fillFrom;
		result.fillAlpha = fade(kShatterFillFade);
		result.freeAlpha = result.glassAlpha = fade(kShatterTextFade);
		result.feeAlpha = eased;
	} else {
		result.feeAlpha = 1.;
	}
	result.edge = result.pill.x() + result.fill * result.pill.width();
	return result;
}

float64 PeerQrPill::targetWidth() const {
	const auto free = (_mode == Mode::Temporary)
		|| (_mode == Mode::Permanent);
	const auto &padding = st::peerQrPillPadding;
	return padding.left()
		+ st::peerQrPillStarBox
		+ st::peerQrPillStarSkip
		+ (free ? _freeText.width() : float64(_feeWidth))
		+ padding.right();
}

float64 PeerQrPill::targetFill(crl::time now) const {
	if (_mode == Mode::Permanent) {
		return 1.;
	} else if (_mode != Mode::Temporary || _deadline <= _arrival) {
		return 0.;
	}
	return std::clamp(
		float64(_deadline - now) / (_deadline - _arrival),
		0.,
		1.);
}

QString PeerQrPill::stateText() const {
	switch (_mode) {
	case Mode::Fee: return _feeText;
	case Mode::Temporary: return CountdownText(std::max(_seconds, 0));
	case Mode::Permanent: return tr::lng_no_paid_token_pill_permanent(tr::now);
	case Mode::Hidden: break;
	}
	return QString();
}

PeerQrPill::Transition PeerQrPill::freeTransition() const {
	switch (_mode) {
	case Mode::Fee: return Transition::Sweep;
	case Mode::Temporary: return Transition::Refill;
	case Mode::Permanent: return Transition::Retext;
	case Mode::Hidden: break;
	}
	return Transition::None;
}

void PeerQrPill::startTransition(Transition transition, crl::time now) {
	const auto frame = computeFrame(now);
	_fillFrom = (_transition == Transition::Shatter) ? 0. : frame.fill;
	_widthFrom = frame.pill.width();
	_transitionStartedAt = now;
	_transition = anim::Disabled() ? Transition::None : transition;
	_fillCarry = 0.;
	if (_transition == Transition::Sweep) {
		const auto &pill = frame.pill;
		for (auto i = 0; i != kFillBurst; ++i) {
			const auto shift = _random.value() * kFillBurstShare;
			spawnFill(frame, pill.x() + shift * pill.width(), now);
		}
	} else if (_transition == Transition::Shatter) {
		spawnShatter(frame, now);
	}
}

void PeerQrPill::stateChanged() {
	const auto shown = (_mode != Mode::Hidden);
	setVisible(shown);
	_button->setVisible(shown);
	if (shown) {
		_button->setAccessibleName(stateText());
		refreshButton(computeFrame(crl::now()));
	}
	checkAnimation();
	update();
}

void PeerQrPill::checkAnimation() {
	if (anim::Disabled()) {
		_animation.stop();
		_particles.clear();
		_transition = Transition::None;
		tick();
		return;
	}
	_timer.cancel();
	const auto now = crl::now();
	if (animationNeeded(now) && !_animation.animating()) {
		_lastStep = now;
		_animation.start();
	}
}

bool PeerQrPill::animationNeeded(crl::time now) const {
	return (_transition != Transition::None)
		|| !_particles.empty()
		|| (_mode == Mode::Temporary && now < _deadline);
}

bool PeerQrPill::animationCallback(crl::time now) {
	if (anim::Disabled() || _mode == Mode::Hidden) {
		return false;
	}
	const auto step = std::clamp(now - _lastStep, crl::time(0), kMaxStep);
	const auto seconds = step / 1000.;
	_lastStep = now;
	if (_transition != Transition::None
		&& now - _transitionStartedAt >= kFillDuration) {
		_transition = Transition::None;
	}
	const auto frame = computeFrame(now);
	emitParticles(frame, seconds, now);
	stepParticles(seconds, now);
	refreshCountdown(now);
	refreshButton(frame);
	updateDirty(frame);
	return animationNeeded(now);
}

void PeerQrPill::tick() {
	if (_mode != Mode::Temporary || !anim::Disabled()) {
		_timer.cancel();
		return;
	}
	const auto now = crl::now();
	refreshCountdown(now);
	updatePill();
	const auto left = _deadline - now;
	if (left > 0) {
		_timer.callOnce((left - 1) % 1000 + 1);
	}
}

void PeerQrPill::refreshCountdown(crl::time now) {
	if (_mode != Mode::Temporary) {
		return;
	}
	const auto seconds = CountdownSeconds(_deadline - now);
	if (_seconds != seconds) {
		_seconds = seconds;
		_freeText.setText(CountdownText(seconds), !anim::Disabled());
	}
}

void PeerQrPill::refreshButton(const Frame &frame) {
	if (_mode != Mode::Hidden) {
		_button->setGeometry(frame.pill.toAlignedRect().translated(pos()));
	}
}

void PeerQrPill::updatePill() {
	if (_mode != Mode::Hidden) {
		const auto frame = computeFrame(crl::now());
		update(ScaledRect(
			frame.pill,
			frame.scale).toAlignedRect().marginsAdded({ 1, 1, 1, 1 }));
	}
}

void PeerQrPill::updateDirty(const Frame &frame) {
	auto dirty = ScaledRect(frame.pill, frame.scale);
	for (const auto &particle : _particles) {
		const auto radius = particle.wobbleAmplitude
			+ particle.radius * ((particle.halo > 0.) ? kHaloScale : 1.);
		dirty = dirty.united(QRectF(
			particle.position - QPointF(radius, radius),
			QSizeF(2. * radius, 2. * radius)));
	}
	const auto rect = dirty.toAlignedRect().marginsAdded({ 1, 1, 1, 1 });
	update(_dirty.united(rect));
	_dirty = rect;
}

void PeerQrPill::emitParticles(
		const Frame &frame,
		float64 seconds,
		crl::time now) {
	if (_transition == Transition::Sweep
		|| _transition == Transition::Refill) {
		_fillCarry += kFillRate * seconds;
		for (; _fillCarry >= 1.; _fillCarry -= 1.) {
			spawnFill(frame, frame.edge, now);
		}
	} else if (_mode == Mode::Temporary && frame.fill > 0.) {
		_fizzCarry += kFizzRate * seconds;
		for (; _fizzCarry >= 1.; _fizzCarry -= 1.) {
			spawnFizz(frame, now);
		}
		_trailCarry += kTrailRate * seconds;
		for (; _trailCarry >= 1.; _trailCarry -= 1.) {
			spawnTrail(frame, now);
		}
	}
}

void PeerQrPill::spawnFill(const Frame &frame, float64 x, crl::time now) {
	if (int(_particles.size()) >= kMaxParticles) {
		return;
	}
	const auto &pill = frame.pill;
	const auto kind = _random.value();
	const auto spark = (kind >= kFillDustShare)
		&& (kind < kFillDustShare + kFillSparkShare);
	const auto size = (kind < kFillDustShare)
		? kDustSize
		: spark
		? kSparkSize
		: kChunkSize;
	_particles.push_back({
		.position = QPointF(x, pill.y() + _random.value() * pill.height()),
		.velocity = QPointF(
			Dp(RandomIn(_random, kFillVelocityX)),
			-Dp(RandomIn(_random, kFillVelocityY))),
		.lift = Dp(kFillLift),
		.drag = kFillDrag,
		.radius = Dp(RandomIn(_random, size)) / 2.,
		.ceiling = pill.y() - Dp(kFillCeiling),
		.halo = spark ? kHaloAlpha : 0.,
		.born = now,
		.life = RandomIn(_random, kFillLife),
	});
}

void PeerQrPill::spawnFizz(const Frame &frame, crl::time now) {
	if (int(_particles.size()) >= kMaxParticles) {
		return;
	}
	const auto &pill = frame.pill;
	_particles.push_back({
		.position = QPointF(
			frame.edge - _random.value() * Dp(kFizzBand),
			pill.y() + _random.value() * pill.height()),
		.velocity = QPointF(
			Dp(RandomIn(_random, kFizzVelocityX)),
			-Dp(RandomIn(_random, kFizzVelocityY))),
		.radius = Dp(RandomIn(_random, kFizzSize)) / 2.,
		.ceiling = pill.y() - Dp(kFillCeiling),
		.born = now,
		.life = RandomIn(_random, kFizzLife),
	});
}

void PeerQrPill::spawnTrail(const Frame &frame, crl::time now) {
	if (int(_particles.size()) >= kMaxParticles) {
		return;
	}
	const auto &pill = frame.pill;
	const auto position = QPointF(
		frame.edge,
		pill.y() + _random.value() * pill.height());
	const auto spark = (_random.value() < kTrailSparkShare);
	_particles.push_back({
		.position = position,
		.velocity = QPointF(
			Dp(RandomIn(_random, kTrailVelocityX)),
			-Dp(RandomIn(_random, kTrailVelocityY))),
		.origin = position,
		.lift = Dp(RandomIn(_random, kTrailLift)),
		.radius = Dp(RandomIn(_random, kTrailSize)) / 2.,
		.ceiling = pill.y() - Dp(kTrailCeiling),
		.fade = Dp(kTrailCeiling),
		.wobbleFrequency = RandomIn(_random, kTrailWobble),
		.wobblePhase = _random.value() * 2. * M_PI,
		.wobbleAmplitude = Dp(kTrailWobbleAmplitude),
		.halo = spark ? kHaloAlpha : 0.,
		.born = now,
		.life = RandomIn(_random, kTrailLife),
	});
}

void PeerQrPill::spawnShatter(const Frame &frame, crl::time now) {
	const auto &pill = frame.pill;
	const auto grid = Dp(kShatterGrid);
	const auto bottom = pill.y() + pill.height();
	auto points = std::vector<QPointF>();
	for (auto y = pill.y() + grid / 2.; y < bottom; y += grid) {
		for (auto x = pill.x() + grid / 2.; x < frame.edge; x += grid) {
			if (InsidePill(pill, QPointF(x, y))) {
				points.emplace_back(x, y);
			}
		}
	}
	const auto count = std::clamp(
		std::min(kShatterCount, kMaxParticles - int(_particles.size())),
		0,
		int(points.size()));
	for (auto i = 0; i != count; ++i) {
		const auto j = i + _random.index(int(points.size()) - i);
		std::swap(points[i], points[j]);
	}
	points.resize(count);
	const auto center = pill.center();
	for (const auto &point : points) {
		const auto delta = point - center;
		const auto length = std::hypot(delta.x(), delta.y());
		const auto direction = (length > 0.)
			? (delta / length)
			: QPointF(0., -1.);
		const auto spark = (_random.value() < kShatterSparkShare);
		const auto size = spark ? kSparkSize : kDustSize;
		_particles.push_back({
			.position = point,
			.velocity = direction * Dp(RandomIn(_random, kShatterSpeed))
				- QPointF(0., Dp(RandomIn(_random, kShatterRise))),
			.drag = kShatterDrag,
			.gravity = Dp(kShatterGravity),
			.radius = Dp(RandomIn(_random, size)) / 2.,
			.ceiling = pill.y() - Dp(kTrailCeiling),
			.halo = spark ? kHaloAlpha : 0.,
			.born = now,
			.life = RandomIn(_random, kShatterLife),
			.gravityAfter = kShatterGravityDelay,
		});
	}
}

void PeerQrPill::stepParticles(float64 seconds, crl::time now) {
	for (auto &particle : _particles) {
		particle.velocity *= std::max(0., 1. - particle.drag * seconds);
		auto acceleration = -particle.lift;
		if (now - particle.born >= particle.gravityAfter) {
			acceleration += particle.gravity;
		}
		particle.velocity.ry() += acceleration * seconds;
		particle.position += particle.velocity * seconds;
	}
	const auto dead = [&](const Particle &particle) {
		const auto &position = particle.position;
		return (now - particle.born >= particle.life)
			|| (CeilingFactor(position.y(), particle.ceiling) <= 0.)
			|| (Falloff(position, particle.origin, particle.fade) <= 0.);
	};
	_particles.erase(ranges::remove_if(_particles, dead), end(_particles));
}

void PeerQrPill::paintPill(QPainter &p, const Frame &frame) const {
	const auto &pill = frame.pill;
	const auto radius = pill.height() / 2.;
	p.setPen(Qt::NoPen);
	p.setBrush(_colors.control);
	p.drawRoundedRect(pill, radius, radius);

	const auto right = pill.x() + pill.width();
	const auto white = QRectF(
		pill.x(),
		pill.y(),
		frame.edge - pill.x(),
		pill.height());
	const auto glass = QRectF(
		frame.edge,
		pill.y(),
		right - frame.edge,
		pill.height());
	if (frame.fill > 0. && frame.fillAlpha > 0.) {
		auto color = QColor(255, 255, 255);
		color.setAlphaF(kFillOpacity * frame.fillAlpha);
		p.save();
		p.setClipRect(
			white.marginsAdded({ radius, radius, 0., radius }),
			Qt::IntersectClip);
		p.setBrush(color);
		p.drawRoundedRect(pill, radius, radius);
		p.restore();
	}

	const auto &font = st::peerQrPillFont;
	const auto star = QPointF(
		pill.x() + st::peerQrPillPadding.left(),
		pill.y() + (pill.height() - st::peerQrPillStarBox) / 2.);
	const auto text = QPointF(
		star.x() + st::peerQrPillStarBox + st::peerQrPillStarSkip,
		pill.y() + (pill.height() - font->height) / 2.);
	if (frame.feeAlpha > 0.) {
		p.save();
		p.setClipRect(frame.feeClipped ? glass : pill, Qt::IntersectClip);
		p.setOpacity(frame.feeAlpha);
		p.drawImage(star, _feeStar);
		p.setFont(font);
		p.setPen(_colors.foreground);
		p.drawText(QPointF(text.x(), text.y() + font->ascent), _feeText);
		p.restore();
	}
	const auto paintFree = [&](
			const QRectF &clip,
			const QImage &image,
			const QColor &color,
			float64 alpha) {
		if (alpha <= 0. || clip.isEmpty()) {
			return;
		}
		p.save();
		p.setClipRect(clip, Qt::IntersectClip);
		p.setOpacity(alpha);
		p.drawImage(star, image);
		_freeText.draw(
			p,
			int(base::SafeRound(text.x())),
			int(base::SafeRound(text.y())),
			color);
		p.restore();
	};
	paintFree(white, _crossedInk, _colors.ink, frame.freeAlpha);
	paintFree(glass, _crossedForeground, _colors.foreground, frame.glassAlpha);
}

void PeerQrPill::paintParticles(QPainter &p, crl::time now) const {
	p.setPen(Qt::NoPen);
	auto color = QColor(255, 255, 255);
	for (const auto &particle : _particles) {
		const auto age = float64(now - particle.born);
		const auto falloff = Falloff(
			particle.position,
			particle.origin,
			particle.fade);
		const auto alpha = std::clamp(1. - age / particle.life, 0., 1.)
			* CeilingFactor(particle.position.y(), particle.ceiling)
			* falloff;
		if (alpha <= 0.) {
			continue;
		}
		const auto radius = particle.radius * falloff;
		const auto wobble = particle.wobbleAmplitude * std::sin(
			2. * M_PI * particle.wobbleFrequency * age / 1000.
				+ particle.wobblePhase);
		const auto center = particle.position + QPointF(wobble, 0.);
		if (particle.halo > 0.) {
			color.setAlphaF(alpha * particle.halo);
			p.setBrush(color);
			p.drawEllipse(center, radius * kHaloScale, radius * kHaloScale);
		}
		color.setAlphaF(alpha);
		p.setBrush(color);
		p.drawEllipse(center, radius, radius);
	}
}

} // namespace Ui
