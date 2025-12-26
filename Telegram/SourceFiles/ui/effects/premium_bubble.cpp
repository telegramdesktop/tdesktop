/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/premium_bubble.h"

#include "base/debug_log.h"
#include "base/object_ptr.h"
#include "lang/lang_keys.h"
#include "ui/effects/gradient.h"
#include "ui/effects/ministar_particles.h"
#include "ui/effects/premium_graphics.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "styles/style_info_levels.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"

namespace Ui::Premium {
namespace {

constexpr auto kBubbleRadiusSubtractor = 2;
constexpr auto kDeflectionSmall = 20.;
constexpr auto kDeflection = 30.;
constexpr auto kStepBeforeDeflection = 0.75;
constexpr auto kStepAfterDeflection = kStepBeforeDeflection
+ (1. - kStepBeforeDeflection) / 2.;
constexpr auto kSlideDuration = crl::time(1000);

} // namespace

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

crl::time Bubble::SlideNoDeflectionDuration() {
	return kSlideDuration * kStepBeforeDeflection;
}

std::optional<int> Bubble::counter() const {
	return _counter;
}

int Bubble::height() const {
	return _height;
}

int Bubble::bubbleRadius() const {
	return (_st.height / 2) - kBubbleRadiusSubtractor;
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

int Bubble::countTargetWidth(int targetCounter) const {
	auto numbers = Ui::NumbersAnimation(_st.font, [] {});
	numbers.setDisabledMonospace(true);
	numbers.setDuration(0);
	const auto texts = _textFactory(targetCounter);
	numbers.setText(texts.counter, targetCounter);
	numbers.finishAnimating();
	const auto top = filledWidth()
		+ numbers.maxWidth()
		+ (_additional.isEmpty()
			? 0
			: (_st.additionalSkip
				+ _st.additionalStyle.font->width(texts.additional)));
	return std::max(top, bottomTextWidth());
}

void Bubble::setCounter(int value) {
	if (_counter != value) {
		_counter = value;
		const auto texts = _textFactory(value);
		_numberAnimation.setText(texts.counter, value);
		if (!texts.additional.isEmpty()) {
			_additional.setText(_st.additionalStyle, texts.additional);
		}
	}
}

void Bubble::setTailEdge(EdgeProgress edge) {
	_tailEdge = std::clamp(edge, 0., 1.);
}

void Bubble::setFlipHorizontal(bool value) {
	_flipHorizontal = value;
}

QRect Bubble::bubbleGeometry(const QRect &r) const {
	const auto penWidth = _st.penWidth;
	const auto penWidthHalf = penWidth / 2;
	return r - style::margins(
		penWidthHalf,
		penWidthHalf,
		penWidthHalf,
		_st.tailSize.height() + penWidthHalf);
}

QPainterPath Bubble::bubblePath(const QRect &r) const {
	const auto bubbleRect = bubbleGeometry(r);
	const auto radius = bubbleRadius();
	auto pathTail = QPainterPath();

	const auto tailWHalf = _st.tailSize.width() / 2.;
	const auto progress = _tailEdge;

	const auto tailTop = bubbleRect.y() + bubbleRect.height();
	const auto tailLeftFull = bubbleRect.x()
		+ (bubbleRect.width() * 0.5)
		- tailWHalf;
	const auto tailLeft = bubbleRect.x()
		+ (bubbleRect.width() * 0.5 * (progress + 1.))
		- tailWHalf;
	const auto tailCenter = tailLeft + tailWHalf;
	const auto tailRight = [&] {
		const auto max = bubbleRect.x() + bubbleRect.width();
		const auto right = tailLeft + _st.tailSize.width();
		const auto bottomMax = max - radius;
		return (right > bottomMax)
			? std::max(float64(tailCenter), float64(bottomMax))
			: right;
	}();
	if (_hasTail) {
		pathTail.moveTo(tailLeftFull, tailTop);
		pathTail.lineTo(tailLeft, tailTop);
		pathTail.lineTo(tailCenter, tailTop + _st.tailSize.height());
		pathTail.lineTo(tailRight, tailTop);
		pathTail.lineTo(tailRight, tailTop - radius);
		pathTail.moveTo(tailLeftFull, tailTop);
	}
	auto pathBubble = QPainterPath();
	pathBubble.setFillRule(Qt::WindingFill);
	pathBubble.addRoundedRect(bubbleRect, radius, radius);

	auto result = pathTail + pathBubble;
	if (_flipHorizontal) {
		auto m = QTransform();
		const auto center = QRectF(bubbleRect).center();
		m.translate(center.x(), center.y());
		m.scale(-1., 1.);
		m.translate(-center.x(), -center.y());
		return m.map(result);
	}
	return result;
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

void Bubble::finishAnimating() {
	_numberAnimation.finishAnimating();
}

void Bubble::paintBubble(QPainter &p, const QRect &r, const QBrush &brush) {
	if (!_counter.has_value()) {
		return;
	}

	const auto bubbleRect = bubbleGeometry(r);
	const auto penWidth = _st.penWidth;
	{
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(QPen(
			brush,
			penWidth,
			Qt::SolidLine,
			Qt::RoundCap,
			Qt::RoundJoin));
		p.setBrush(brush);
		p.drawPath(bubblePath(r));
	}
	p.setPen(st::activeButtonFg);
	p.setFont(_st.font);
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
	const auto iconTop = bubbleRect.y()
		+ topShift
		+ (bubbleRect.height() - _icon->height()) / 2;
	_icon->paint(p, iconLeft, iconTop, bubbleRect.width());
	const auto numberLeft = iconLeft + _icon->width() + _st.textSkip;
	const auto numberTop = r.y() + (_st.height - height) / 2;
	_numberAnimation.paint(p, numberLeft, numberTop, width());
	if (!_additional.isEmpty()) {
		p.setOpacity(0.7);
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
		p.setOpacity(1.);
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
, _outerPadding(outerPadding)
, _deflection(kDeflection)
, _stepBeforeDeflection(kStepBeforeDeflection)
, _stepAfterDeflection(kStepAfterDeflection) {
	if (_type == BubbleType::Credits) {
		setupParticles(parent);
	}
	const auto resizeTo = [=](int w, int h) {
		_deflection = (w > _st.widthLimit)
			? kDeflectionSmall
			: kDeflection;
		_spaceForDeflection = QSize(_st.skip, _st.skip);
		resize(QSize(w, h) + 2 * _spaceForDeflection);
	};

	resizeTo(_bubble.width(), _bubble.height());
	_bubble.widthChanges(
	) | rpl::on_next([=] {
		resizeTo(_bubble.width(), _bubble.height());
	}, lifetime());

	const auto instant = !showFinishes;
	if (instant) {
		showFinishes = rpl::single(rpl::empty);
	}
	std::move(
		showFinishes
	) | rpl::take(1) | rpl::on_next([=] {
		_state.value(
		) | rpl::on_next([=](BubbleRowState state) {
			animateTo(state);
		}, lifetime());
		if (instant) {
			_appearanceAnimation.stop();
			if (const auto onstack = base::take(_appearanceCallback)) {
				onstack(1.);
			}
			_bubble.finishAnimating();
		}

		parent->widthValue() | rpl::on_next([=](int w) {
			if (!_appearanceAnimation.animating()) {
				const auto available = w
					- _outerPadding.left()
					- _outerPadding.right();
				const auto x = (available * _animatingFromResultRatio);
				moveToLeft(-_spaceForDeflection.width()
					+ std::max(
						int(base::SafeRound(x
							- (_bubble.width() / 2)
							+ _outerPadding.left())),
						0),
					0);
			}
		}, lifetime());
	}, lifetime());
}

void BubbleWidget::setupParticles(not_null<Ui::RpWidget*> parent) {
	_particles.emplace(StarParticles::Type::Radial, 50, st::lineWidth * 4);
	_particles->setSpeed(0.1);

	_particlesWidget = Ui::CreateChild<Ui::RpWidget>(parent);
	_particlesWidget->setAttribute(Qt::WA_TransparentForMouseEvents);
	_particlesWidget->show();
	_particlesWidget->raise();

	_particlesAnimation.init([=] { _particlesWidget->update(); });

	_particlesWidget->paintRequest() | rpl::on_next([=] {
		if (!_particlesAnimation.animating()) {
			_particlesAnimation.start();
		}
		auto p = QPainter(_particlesWidget);
		auto hq = PainterHighQualityEnabler(p);

		const auto offset = QPoint(
			(_particlesWidget->width() - width()) / 2,
			(_particlesWidget->height() - height()) / 2);
		const auto bubbleRect = rect().translated(offset)
			- QMargins(
				_spaceForDeflection.width(),
				_spaceForDeflection.height(),
				_spaceForDeflection.width(),
				_spaceForDeflection.height());

		p.save();
		p.translate(offset);
		const auto bubblePath = _bubble.bubblePath(bubbleRect);
		p.restore();

		auto fullRect = QPainterPath();
		fullRect.addRect(QRectF(_particlesWidget->rect()));

		p.setClipPath(bubblePath);
		_particles->setColor(st::premiumButtonFg->c);
		_particles->paint(p, _particlesWidget->rect(), crl::now());
		p.setClipping(false);

		p.setClipPath(fullRect.subtracted(bubblePath));
		_particles->setColor(_brushOverride
			? st::groupCallMemberInactiveIcon->c
			: st::creditsBg3->c);
		_particles->paint(p, _particlesWidget->rect(), crl::now());
	}, _particlesWidget->lifetime());

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

void BubbleWidget::animateTo(BubbleRowState state) {
	const auto targetWidth = _bubble.countTargetWidth(state.counter);
	const auto parent = parentWidget();
	const auto available = parent->width()
		- _outerPadding.left()
		- _outerPadding.right();
	const auto halfWidth = (targetWidth / 2);
	const auto computeLeft = [=](float64 pointRatio, float64 animProgress) {
		const auto delta = (pointRatio - _animatingFromResultRatio);
		const auto center = available
			* (_animatingFromResultRatio + delta * animProgress);
		return center - halfWidth + _outerPadding.left();
	};
	const auto moveEndPoint = state.ratio;
	const auto computeRightEdge = [=] {
		return parent->width() - _outerPadding.right() - targetWidth;
	};
	struct Edge final {
		float64 goodPointRatio = 0.;
		float64 bubbleEdge = 0.;
	};
	const auto desiredFinish = computeLeft(moveEndPoint, 1.);
	const auto leftEdge = [&]() -> Edge {
		const auto edge = _outerPadding.left();
		if (desiredFinish < edge) {
			const auto goodPointRatio = float64(halfWidth) / available;
			const auto bubbleLeftEdge = (desiredFinish - edge)
				/ float64(halfWidth);
			return { goodPointRatio, bubbleLeftEdge };
		}
		return {};
	}();
	const auto rightEdge = [&]() -> Edge {
		const auto edge = computeRightEdge();
		if (desiredFinish > edge) {
			const auto goodPointRatio = 1. - float64(halfWidth) / available;
			const auto bubbleRightEdge = (desiredFinish - edge)
				/ float64(halfWidth);
			return { goodPointRatio, bubbleRightEdge };
		}
		return {};
	}();
	const auto finalEdge = (leftEdge.bubbleEdge < 0.)
		? leftEdge.bubbleEdge
		: rightEdge.bubbleEdge;
	_ignoreDeflection = !_state.current().dynamic && (finalEdge != 0.);
	if (_ignoreDeflection) {
		_stepBeforeDeflection = 1.;
		_stepAfterDeflection = 1.;
	} else {
		_stepBeforeDeflection = kStepBeforeDeflection;
		_stepAfterDeflection = kStepAfterDeflection;
	}
	const auto resultMoveEndPoint = (finalEdge < 0)
		? leftEdge.goodPointRatio
		: (finalEdge > 0)
		? rightEdge.goodPointRatio
		: moveEndPoint;

	const auto duration = kSlideDuration
		* (_ignoreDeflection ? kStepBeforeDeflection : 1.)
		* ((_state.current().ratio < 0.001) ? 0.5 : 1.);
	if (state.animateFromZero) {
		_animatingFrom.ratio = 0.;
		_animatingFrom.counter = 0;
		_animatingFromResultRatio = 0.;
		_animatingFromBubbleEdge = 0.;
	}
	_appearanceCallback = [=](float64 value) {
		if (!_appearanceAnimation.animating()) {
			_animatingFrom = state;
			_animatingFromResultRatio = resultMoveEndPoint;
			_animatingFromBubbleEdge = finalEdge;
			_appearanceCallback = nullptr;
		}
		value = std::abs(value);
		const auto moveProgress = std::clamp(
			(value / _stepBeforeDeflection),
			0.,
			1.);
		const auto counterProgress = std::clamp(
			(value / _stepAfterDeflection),
			0.,
			1.);
		const auto nowBubbleEdge = _animatingFromBubbleEdge
			+ (finalEdge - _animatingFromBubbleEdge) * moveProgress;
		moveToLeft(-_spaceForDeflection.width()
			+ std::max(
				int(base::SafeRound(
					computeLeft(resultMoveEndPoint, moveProgress))),
				0),
			0);

		const auto now = _animatingFrom.counter
			+ counterProgress * (state.counter - _animatingFrom.counter);
		_bubble.setCounter(int(base::SafeRound(now)));

		if (_particles) {
			const auto progress = now / float(state.counter);
			_particles->setSpeed(0.01 + progress * 0.25);
		}

		_bubble.setFlipHorizontal(nowBubbleEdge < 0);
		_bubble.setTailEdge(std::abs(nowBubbleEdge));
		update();
	};
	_appearanceAnimation.start(
		_appearanceCallback,
		0.,
		(state.ratio >= _animatingFrom.ratio) ? 1. : -1.,
		duration,
		anim::easeOutCirc);
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

	const auto padding = QMargins(
		_spaceForDeflection.width(),
		_spaceForDeflection.height(),
		_spaceForDeflection.width(),
		_spaceForDeflection.height());
	const auto bubbleRect = rect() - padding;

	const auto params = GradientParams{
		.left = x() + _spaceForDeflection.width(),
		.width = bubbleRect.width(),
		.outer = parentWidget()->parentWidget()->width(),
	};
	if (_cachedGradientParams != params) {
		_cachedGradient = ComputeGradient(
			parentWidget(),
			params.left,
			params.width);
		_cachedGradientParams = params;
	}
	if (_appearanceAnimation.animating()) {
		const auto value = _appearanceAnimation.value(1.);
		const auto progress = std::abs(value);
		const auto finalScale = (_animatingFromResultRatio > 0.)
			|| (_state.current().ratio < 0.001);
		const auto scaleProgress = finalScale
			? 1.
			: std::clamp((progress / _stepBeforeDeflection), 0., 1.);
		const auto scale = scaleProgress;
		const auto rotationProgress = std::clamp(
			(progress - _stepBeforeDeflection) / (1. - _stepBeforeDeflection),
			0.,
			1.);
		const auto rotationProgressReverse = std::clamp(
			(progress - _stepAfterDeflection) / (1. - _stepAfterDeflection),
			0.,
			1.);

		const auto offsetX = bubbleRect.x() + bubbleRect.width() / 2;
		const auto offsetY = bubbleRect.y() + bubbleRect.height();
		p.translate(offsetX, offsetY);
		p.scale(scale, scale);
		if (!_ignoreDeflection) {
			p.rotate((rotationProgress - rotationProgressReverse)
				* _deflection
				* (value < 0. ? -1. : 1.));
		}
		p.translate(-offsetX, -offsetY);
	}


	_bubble.paintBubble(p, bubbleRect, [&] {
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
	}());
}

void BubbleWidget::resizeEvent(QResizeEvent *e) {
	RpWidget::resizeEvent(e);
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
			.ratio = (current - min) / float64(max - min),
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
