/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/premium_bubble.h"

#include "base/object_ptr.h"
#include "lang/lang_keys.h"
#include "ui/effects/gradient.h"
#include "ui/effects/ministar_particles.h"
#include "ui/effects/premium_3d_support.h"
#include "ui/effects/premium_graphics.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"

#include <QtCore/QtMath>

#include "styles/style_layers.h"
#include "styles/style_premium.h"

namespace Ui::Premium {
namespace {

constexpr auto kFadeDuration = crl::time(100);
constexpr auto kScaleDuration = crl::time(400);
constexpr auto kTiltDuration = crl::time(150);
constexpr auto kBounceDuration = crl::time(200);
constexpr auto kSettleDuration = crl::time(150);
constexpr auto kCounterUpdateDuration = crl::time(300);
constexpr auto kNarrowTilt = 0.26;
constexpr auto kWideTilt = 0.20;
constexpr auto kBounceAngle = 0.04;

float64 PhaseProgress(float64 elapsed, crl::time duration) {
	return std::clamp(elapsed / duration, 0., 1.);
}

float64 EaseOut(float64 progress) {
	return CubicBezier(0., 0., 0.58, 1., progress);
}

float64 EaseIn(float64 progress) {
	return CubicBezier(0.42, 0., 1., 1., progress);
}

float64 BubbleRotation(float64 elapsed, float64 from, float64 tilt) {
	if (elapsed < kLimitSlideDuration) {
		const auto progress = PhaseProgress(elapsed, kTiltDuration);
		return std::lerp(from, tilt, EaseOut(progress));
	}
	elapsed -= kLimitSlideDuration;
	if (elapsed < kBounceDuration) {
		const auto progress = PhaseProgress(elapsed, kBounceDuration);
		return std::lerp(tilt, kBounceAngle, EaseOut(progress));
	}
	const auto progress = PhaseProgress(
		elapsed - kBounceDuration,
		kSettleDuration);
	return std::lerp(kBounceAngle, 0., EaseIn(progress));
}

void AppendArc(
		QPainterPath &path,
		QPointF center,
		float64 radius,
		float64 from,
		float64 to) {
	if (radius <= 0.) {
		path.lineTo(center);
	} else {
		const auto size = QSizeF(2. * radius, 2. * radius);
		path.arcTo(
			QRectF(center - QPointF(radius, radius), size),
			from,
			to - from);
	}
}

struct TailSide {
	float64 cornerSweep = 0.;
	float64 connectionRadius = 0.;
	float64 halfWidth = 0.;
	float64 taper = 0.;
};

TailSide ComputeTailSide(float64 position, float64 width, float64 radius) {
	const auto distance = std::min(position, 0.5);
	const auto taper = std::min(distance / 0.27, 1.);
	return {
		.cornerSweep = 90. * std::max(0., distance - 0.15) / 0.35,
		.connectionRadius = radius
			* std::max(0., distance - 0.35) / 0.15,
		.halfWidth = (width / 2.) * taper,
		.taper = taper,
	};
}

float64 TailFlankAngle(
		const TailSide &side,
		float64 horizontal,
		float64 height,
		float64 tipRadius) {
	const auto radii = side.connectionRadius + tipRadius;
	const auto vertical = height - radii;
	const auto distance = std::hypot(horizontal, vertical);
	if (!distance) {
		return 0.;
	}
	// The flank is the internal tangent of the shoulder and tip circles.
	const auto angle = std::atan2(vertical, horizontal)
		+ std::asin(std::min(radii / distance, 1.));
	return std::min(qRadiansToDegrees(angle), 90.) * side.taper;
}

} // namespace

float64 LimitSlideProgress(float64 delta, float64 progress) {
	return delta * CubicBezier(0.42, 0., 0.58, 1., progress);
}

TextFactory ProcessTextFactory(
		std::optional<tr::phrase<lngtag_count>> phrase) {
	return phrase
		? TextFactory([=](int n) -> BubbleText {
			return { (*phrase)(tr::now, lt_count, n) };
		})
		: TextFactory([=](int n) -> BubbleText {
			return { QString::number(n) };
		});
}

Bubble::Bubble(
	const style::PremiumBubble &st,
	Fn<void()> updateCallback,
	TextFactory textFactory,
	const style::icon *icon,
	bool hasTail)
: _st(st)
, _updateCallback(std::move(updateCallback))
, _textFactory(std::move(textFactory))
, _icon(icon)
, _numberAnimation(_st.font, _updateCallback)
, _height(_st.height + _st.tailSize.height())
, _hasTail(hasTail) {
	_numberAnimation.setDisabledMonospace(true);
	_numberAnimation.setWidthChangedCallback([=] {
		_widthChanges.fire({});
	});
	const auto texts = _textFactory(0);
	_numberAnimation.setText(texts.counter, 0);
	_numberAnimation.finishAnimating();
	if (!texts.additional.isEmpty()) {
		_additional.setText(_st.additionalStyle, texts.additional);
	}
}

std::optional<int> Bubble::counter() const {
	return _counter;
}

int Bubble::height() const {
	return _height;
}

int Bubble::filledWidth() const {
	return _st.padding.left()
		+ _icon->width()
		+ _st.textSkip
		+ _st.padding.right();
}

int Bubble::topTextWidth() const {
	return filledWidth()
		+ _numberAnimation.countWidth()
		+ (_additional.isEmpty()
			? 0
			: (_st.additionalSkip + _additional.maxWidth()));
}

int Bubble::bottomTextWidth() const {
	return _subtext.isEmpty()
		? 0
		: (_st.subtextPadding.left()
			+ _subtext.maxWidth()
			+ _st.subtextPadding.right());
}

int Bubble::width() const {
	return std::max(topTextWidth(), bottomTextWidth());
}

int Bubble::widthForCounter(int value) const {
	auto numbers = NumbersAnimation(_st.font, [] {});
	numbers.setDisabledMonospace(true);
	numbers.setDuration(0);
	const auto texts = _textFactory(value);
	numbers.setText(texts.counter, value);
	numbers.finishAnimating();
	const auto top = filledWidth()
		+ numbers.countWidth()
		+ (texts.additional.isEmpty()
			? 0
			: (_st.additionalSkip
				+ _st.additionalStyle.font->width(texts.additional)));
	return std::max(top, bottomTextWidth());
}

void Bubble::setCounter(int value, crl::time duration) {
	if (_counter == value) {
		return;
	}
	_counter = value;
	_numberAnimation.setDuration(duration);
	if (!duration) {
		_numberAnimation.finishAnimating();
	}
	const auto texts = _textFactory(value);
	_numberAnimation.setText(texts.counter, value);
	if (!duration) {
		_numberAnimation.finishAnimating();
	}
	if (_additional.toString() != texts.additional) {
		if (texts.additional.isEmpty()) {
			_additional = {};
		} else {
			_additional.setText(
				_st.additionalStyle,
				texts.additional);
		}
		_widthChanges.fire({});
	}
}

void Bubble::setTailPosition(float64 position) {
	_tailPosition = position;
}

QPainterPath Bubble::bubblePath(const QRect &r) const {
	const auto body = QRectF(r.x(), r.y(), r.width(), _st.height);
	const auto radius = std::min(body.width(), body.height()) / 2.;
	auto path = QPainterPath();
	if (!_hasTail) {
		path.addRoundedRect(body, radius, radius);
		return path;
	}

	const auto tailWidth = _st.tailSize.width();
	const auto tailHeight = _st.tailSize.height();
	const auto tipRadius = std::min(
		float64(_st.tailRadius),
		tailHeight / 2.);
	const auto left = ComputeTailSide(
		_tailPosition,
		tailWidth,
		tipRadius);
	const auto right = ComputeTailSide(
		1. - _tailPosition,
		tailWidth,
		tipRadius);
	const auto x = body.x() + body.width() * _tailPosition;
	const auto bottom = body.bottom();
	const auto leftShoulder = std::max(
		body.left(),
		x - left.halfWidth - left.connectionRadius);
	const auto rightShoulder = std::min(
		body.right(),
		x + right.halfWidth + right.connectionRadius);
	const auto leftAngle = TailFlankAngle(
		left,
		x - leftShoulder,
		tailHeight,
		tipRadius);
	const auto rightAngle = TailFlankAngle(
		right,
		rightShoulder - x,
		tailHeight,
		tipRadius);

	path.moveTo(body.left(), bottom - radius);
	AppendArc(
		path,
		{ body.left() + radius, bottom - radius },
		radius,
		180.,
		180. + left.cornerSweep);
	AppendArc(
		path,
		{ leftShoulder, bottom + left.connectionRadius },
		left.connectionRadius,
		90.,
		90. - leftAngle);
	AppendArc(
		path,
		{ x, bottom + tailHeight - tipRadius },
		tipRadius,
		-90. - leftAngle,
		-90. + rightAngle);
	AppendArc(
		path,
		{ rightShoulder, bottom + right.connectionRadius },
		right.connectionRadius,
		90. + rightAngle,
		90.);
	AppendArc(
		path,
		{ body.right() - radius, bottom - radius },
		radius,
		-right.cornerSweep,
		0.);
	AppendArc(
		path,
		{ body.right() - radius, body.top() + radius },
		radius,
		0.,
		90.);
	AppendArc(
		path,
		{ body.left() + radius, body.top() + radius },
		radius,
		90.,
		180.);
	path.closeSubpath();
	return path;
}

void Bubble::setSubtext(QString subtext) {
	if (_subtext.toString() == subtext) {
		return;
	} else if (subtext.isEmpty()) {
		_subtext = {};
	} else {
		_subtext.setText(_st.subtextStyle, subtext);
	}
	_widthChanges.fire({});
	_updateCallback();
}

void Bubble::paintBubble(QPainter &p, const QRect &r, const QBrush &brush) {
	if (!_counter.has_value()) {
		return;
	}

	auto hq = PainterHighQualityEnabler(p);
	p.fillPath(bubblePath(r), brush);
	const auto opacity = p.opacity();
	const auto outerWidth = r.x() * 2 + r.width();
	p.setPen(st::activeButtonFg);
	const auto withSubtext = !_subtext.isEmpty();
	const auto height = withSubtext
		? (_st.font->height
			+ _st.subtextPadding.top()
			+ _st.subtextStyle.font->height)
		: _st.font->height;
	const auto topWidth = withSubtext ? topTextWidth() : 0;
	const auto bottomWidth = withSubtext ? bottomTextWidth() : 0;
	const auto iconShift = (topWidth >= bottomWidth)
		? 0
		: (bottomWidth - topWidth) / 2;
	const auto iconLeft = r.x() + _st.padding.left() + iconShift;
	const auto topShift = (_st.font->height - height) / 2;
	const auto iconTop = r.y()
		+ topShift
		+ (_st.height - _icon->height()) / 2;
	_icon->paint(p, iconLeft, iconTop, outerWidth);
	const auto numberLeft = iconLeft + _icon->width() + _st.textSkip;
	const auto numberTop = r.y() + (_st.height - height) / 2;
	p.save();
	p.setClipRect(
		QRect(r.x(), numberTop, r.width(), _st.font->height),
		Qt::IntersectClip);
	_numberAnimation.paint(p, numberLeft, numberTop, outerWidth);
	p.restore();
	if (!_additional.isEmpty()) {
		p.setOpacity(opacity * 0.7);
		const auto additionalLeft = numberLeft
			+ _numberAnimation.countWidth()
			+ _st.additionalSkip;
		const auto additionalTop = numberTop
			+ _st.font->ascent
			- _st.additionalStyle.font->ascent;
		_additional.draw(p, {
			.position = { additionalLeft, additionalTop },
			.availableWidth = _additional.maxWidth(),
		});
	}
	if (withSubtext) {
		p.setOpacity(opacity);
		const auto subtextShift = (bottomWidth >= topWidth)
			? 0
			: (topWidth - bottomWidth) / 2;
		const auto subtextLeft = r.x()
			+ _st.subtextPadding.left()
			+ subtextShift;
		const auto subtextTop = numberTop
			+ _st.font->height
			+ _st.subtextPadding.top();
		_subtext.draw(p, {
			.position = { subtextLeft, subtextTop },
			.availableWidth = _subtext.maxWidth(),
		});
	}
}

rpl::producer<> Bubble::widthChanges() const {
	return _widthChanges.events();
}

BubbleWidget::BubbleWidget(
	not_null<Ui::RpWidget*> parent,
	const style::PremiumBubble &st,
	TextFactory textFactory,
	rpl::producer<BubbleRowState> state,
	BubbleType type,
	rpl::producer<> showFinishes,
	const style::icon *icon,
	const style::margins &outerPadding)
: AbstractButton(parent)
, _st(st)
, _state(std::move(state))
, _bubble(
	_st,
	[=] { update(); },
	std::move(textFactory),
	icon,
	(type != BubbleType::NoPremium))
, _type(type)
, _outerPadding(outerPadding) {
	if (_type == BubbleType::Credits) {
		setupParticles(parent);
	}
	resizeToBubble();
	_bubble.widthChanges() | rpl::on_next(
		[=] { resizeToBubble(); },
		lifetime());

	parent->widthValue() | rpl::on_next(
		[=] { refreshPosition(); },
		lifetime());

	const auto instant = !showFinishes;
	if (instant) {
		showFinishes = rpl::single(rpl::empty);
	}
	std::move(
		showFinishes
	) | rpl::take(1) | rpl::on_next(
		[=] {
			_state.value() | rpl::on_next(
				[=](BubbleRowState state) {
					const auto first = !_bubble.counter();
					animateTo(state, instant && first);
				},
				lifetime());
		},
		lifetime());
}

void BubbleWidget::setupParticles(not_null<Ui::RpWidget*> parent) {
	_particles.emplace(StarParticles::Type::Radial, 50, st::lineWidth * 4);
	_particles->setSpeed(0.1);

	_particlesWidget = Ui::CreateChild<Ui::RpWidget>(parent);
	_particlesWidget->setAttribute(Qt::WA_TransparentForMouseEvents);
	_particlesWidget->show();
	_particlesWidget->raise();

	_particlesAnimation.init([=] { _particlesWidget->update(); });

	_particlesWidget->paintRequest() | rpl::on_next(
		[=] { paintParticles(); },
		_particlesWidget->lifetime());

	geometryValue() | rpl::on_next([=](QRect geometry) {
		const auto particlesSize = QSize(
			int(geometry.width() * 1.5),
			int(geometry.height() * 1.5));
		const auto center = geometry.center();
		_particlesWidget->setGeometry(
			center.x() - particlesSize.width() / 2,
			center.y() - particlesSize.height() / 2,
			particlesSize.width(),
			particlesSize.height());
	}, lifetime());
}

void BubbleWidget::paintParticles() {
	if (!_particlesAnimation.animating()) {
		_particlesAnimation.start();
	}
	auto p = QPainter(_particlesWidget);
	auto hq = PainterHighQualityEnabler(p);

	const auto offset = QPoint(
		(_particlesWidget->width() - width()) / 2,
		(_particlesWidget->height() - height()) / 2);
	const auto bubblePath = bubbleTransform().map(
		_bubble.bubblePath(bubbleRect())).translated(offset);

	auto fullRect = QPainterPath();
	fullRect.addRect(QRectF(_particlesWidget->rect()));

	p.setClipPath(bubblePath);
	p.setOpacity(_opacity);
	_particles->setColor(st::premiumButtonFg->c);
	_particles->paint(p, _particlesWidget->rect(), crl::now(), false);
	p.setClipping(false);
	p.setOpacity(1.);

	p.setClipPath(fullRect.subtracted(bubblePath));
	_particles->setColor(
		_brushOverride
			? st::groupCallMemberInactiveIcon->c
			: st::creditsBg3->c);
	_particles->paint(p, _particlesWidget->rect(), crl::now(), false);
}

void BubbleWidget::animateTo(BubbleRowState state, bool instant) {
	const auto first = !_bubble.counter().has_value();
	const auto target = std::clamp(state.ratio, 0., 1.);
	const auto from = (first || state.animateFromZero) ? 0. : _ratio;
	const auto fromAnchor = _anchor;
	const auto fromTail = _tailPosition;
	const auto fromRotation = first ? 0. : _rotation;
	const auto fromScale = first ? 0.1 : _scale;
	const auto fromOpacity = first ? 0. : _opacity;
	instant = instant
		|| anim::Disabled()
		|| _type == BubbleType::NoPremium
		|| (first && target < 0.1);

	_appearanceAnimation.stop();
	const auto counterDuration = instant
		? 0
		: first ? kLimitSlideDuration : kCounterUpdateDuration;
	_bubble.setCounter(state.counter, counterDuration);

	const auto targetWidth = instant
		? _bubble.width()
		: _bubble.widthForCounter(state.counter);
	const auto tilt = (targetWidth > _st.widthLimit)
		? -kWideTilt
		: -kNarrowTilt;
	const auto travels = (from != target);
	const auto rotates = !instant
		&& (_type != BubbleType::Credits)
		&& (travels || fromRotation != 0.);
	const auto duration = instant
		? crl::time(0)
		: rotates
		? kLimitSlideDuration + kBounceDuration + kSettleDuration
		: kLimitSlideDuration;
	const auto frame = [=](float64 elapsed) {
		const auto time = PhaseProgress(elapsed, kLimitSlideDuration);
		const auto slide = LimitSlideProgress(1., time);
		const auto targetAnchor = anchorForRatio(target);
		_ratio = std::lerp(from, target, slide);
		_anchor = first
			? targetAnchor
			: std::lerp(fromAnchor, targetAnchor, slide);
		_tailPosition = first
			? targetAnchor
			: std::lerp(fromTail, targetAnchor, time);
		_rotation = rotates
			? BubbleRotation(elapsed, fromRotation, tilt)
			: 0.;
		const auto scale = PhaseProgress(elapsed, kScaleDuration);
		const auto fade = PhaseProgress(elapsed, kFadeDuration);
		_scale = std::lerp(
			fromScale,
			1.,
			LimitSlideProgress(1., scale));
		_opacity = std::lerp(
			fromOpacity,
			1.,
			LimitSlideProgress(1., fade));
		if (_particles && first) {
			_particles->setSpeed(0.01 + slide * 0.25);
		}
		updatePosition();
	};
	if (duration) {
		_appearanceAnimation.start(
			frame,
			0.,
			duration,
			duration);
		resizeToBubble(targetWidth);
		frame(0.);
	} else {
		_ratio = target;
		resizeToBubble();
		frame(kLimitSlideDuration);
	}
}

float64 BubbleWidget::anchorForRatio(float64 ratio) const {
	const auto available = std::max(
		parentWidget()->width()
			- _outerPadding.left()
			- _outerPadding.right(),
		0);
	const auto width = _bubble.width();
	if (width <= 0 || width > available) {
		return 0.5;
	}
	const auto x = available * ratio;
	const auto left = std::clamp(
		x - width / 2.,
		0.,
		float64(available - width));
	return std::clamp((x - left) / width, 0., 1.);
}

void BubbleWidget::resizeToBubble(int targetWidth) {
	const auto width = _bubble.width();
	const auto height = _bubble.height();
	const auto skip = _st.skip;
	const auto angle = (_type == BubbleType::Credits
		|| _type == BubbleType::NoPremium) ? 0. : kNarrowTilt;
	const auto reservedWidth = std::max(width, targetWidth);
	const auto radius = std::min(reservedWidth, _st.height) / 2.;
	const auto verticalTravel = (reservedWidth - radius)
		* std::sin(angle);
	const auto horizontal = std::max(
		skip,
		int(std::ceil(height * std::sin(angle))));
	const auto top = std::max({
		skip,
		_bubblePadding.top(),
		int(
			std::ceil(verticalTravel
				- (height - radius)
					* (1. - std::cos(angle)))),
	});
	const auto bottom = std::max({
		skip,
		_bubblePadding.bottom(),
		int(
			std::ceil(verticalTravel
				+ radius
				- (radius + _st.tailSize.height())
					* std::cos(angle))),
	});
	_bubblePadding = QMargins(horizontal, top, horizontal, bottom);
	resize(QSize(width + 2 * horizontal, height + top + bottom));
	refreshPosition();
}

void BubbleWidget::refreshPosition() {
	if (_bubble.counter().has_value()) {
		if (!_appearanceAnimation.animating()) {
			_anchor = _tailPosition = anchorForRatio(_ratio);
		}
		updatePosition();
	}
}

void BubbleWidget::updatePosition() {
	const auto available = std::max(
		parentWidget()->width()
			- _outerPadding.left()
			- _outerPadding.right(),
		0);
	const auto left = _outerPadding.left()
		+ available * _ratio
		- _anchor * _bubble.width()
		- _bubblePadding.left();
	const auto rounded = int(base::SafeRound(left));
	_positionOffset = (style::RightToLeft() ? -1. : 1.)
		* (left - rounded);
	moveToLeft(rounded, 0);
	_bubble.setTailPosition(style::RightToLeft()
		? (1. - _tailPosition)
		: _tailPosition);
	update();
}

QRect BubbleWidget::bubbleRect() const {
	return rect() - _bubblePadding;
}

QTransform BubbleWidget::bubbleTransform() const {
	const auto r = bubbleRect();
	const auto anchor = style::RightToLeft() ? (1. - _anchor) : _anchor;
	const auto pivotX = r.x() + r.width() * anchor;
	const auto pivotY = r.y() + r.height();
	auto transform = QTransform();
	transform.translate(pivotX + _positionOffset, pivotY);
	transform.rotateRadians(
		style::RightToLeft() ? -_rotation : _rotation);
	transform.scale(_scale, _scale);
	transform.translate(-pivotX, -pivotY);
	return transform;
}

void BubbleWidget::setBrushOverride(std::optional<QBrush> brushOverride) {
	_brushOverride = std::move(brushOverride);
	update();
}

void BubbleWidget::setSubtext(QString subtext) {
	_bubble.setSubtext(subtext);
}

void BubbleWidget::paintEvent(QPaintEvent *e) {
	if (!_bubble.counter().has_value()) {
		return;
	}

	auto p = QPainter(this);

	const auto r = bubbleRect();

	const auto params = GradientParams{
		.left = x() + r.x(),
		.width = r.width(),
		.outer = parentWidget()->parentWidget()->width(),
	};
	if (_cachedGradientParams != params) {
		_cachedGradient = ComputeGradient(
			parentWidget(),
			params.left,
			params.width);
		_cachedGradientParams = params;
	}
	p.setTransform(bubbleTransform(), true);
	p.setOpacity(_opacity);

	const auto brush = [&] {
		if (_brushOverride) {
			return *_brushOverride;
		}
		switch (_type) {
		case BubbleType::NoPremium:
		case BubbleType::UpgradePrice:
		case BubbleType::StarRating: return st::windowBgActive->b;
		case BubbleType::NegativeRating: return st::attentionButtonFg->b;
		case BubbleType::Premium: return QBrush(_cachedGradient);
		case BubbleType::Credits: return st::creditsBg3->b;
		}
		Unexpected("Type in Premium::BubbleWidget.");
	}();
	_bubble.paintBubble(p, r, brush);
}

not_null<BubbleWidget*> AddBubbleRow(
		not_null<Ui::VerticalLayout*> parent,
		const style::PremiumBubble &st,
		rpl::producer<> showFinishes,
		int min,
		int current,
		int max,
		BubbleType type,
		std::optional<tr::phrase<lngtag_count>> phrase,
		const style::icon *icon) {
	return AddBubbleRow(
		parent,
		st,
		std::move(showFinishes),
		rpl::single(BubbleRowState{
			.counter = current,
			.ratio = (max > min)
				? (current - min) / float64(max - min)
				: 0.,
		}),
		type,
		ProcessTextFactory(phrase),
		icon,
		st::boxRowPadding);
}

not_null<BubbleWidget*> AddBubbleRow(
		not_null<Ui::VerticalLayout*> parent,
		const style::PremiumBubble &st,
		rpl::producer<> showFinishes,
		rpl::producer<BubbleRowState> state,
		BubbleType type,
		TextFactory text,
		const style::icon *icon,
		const style::margins &outerPadding) {
	const auto container = parent->add(
		object_ptr<Ui::FixedHeightWidget>(parent, 0));
	container->show();

	const auto bubble = Ui::CreateChild<BubbleWidget>(
		container,
		st,
		text ? std::move(text) : ProcessTextFactory(std::nullopt),
		std::move(state),
		type,
		std::move(showFinishes),
		icon,
		outerPadding);
	bubble->setAttribute(Qt::WA_TransparentForMouseEvents);
	rpl::combine(
		container->sizeValue(),
		bubble->sizeValue()
	) | rpl::on_next([=](const QSize &parentSize, const QSize &size) {
		container->resize(parentSize.width(), size.height());
	}, bubble->lifetime());
	bubble->show();
	return bubble;
}

} // namespace Ui::Premium
