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
#include "ui/effects/premium_graphics.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
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
		? TextFactory([=](int n) { return (*phrase)(tr::now, lt_count, n); })
		: TextFactory([=](int n) { return QString::number(n); });
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
, _textTop((_height - _st.tailSize.height() - _st.font->height) / 2)
, _hasTail(hasTail) {
	_numberAnimation.setDisabledMonospace(true);
	_numberAnimation.setWidthChangedCallback([=] {
		_widthChanges.fire({});
	});
	_numberAnimation.setText(_textFactory(0), 0);
	_numberAnimation.finishAnimating();
}

crl::time Bubble::SlideNoDeflectionDuration() {
	return kSlideDuration * kStepBeforeDeflection;
}

int Bubble::counter() const {
	return _counter;
}

int Bubble::height() const {
	return _height;
}

int Bubble::bubbleRadius() const {
	return (_height - _st.tailSize.height()) / 2 - kBubbleRadiusSubtractor;
}

int Bubble::filledWidth() const {
	return _st.padding.left()
		+ _icon->width()
		+ _st.textSkip
		+ _st.padding.right();
}

int Bubble::width() const {
	return filledWidth() + _numberAnimation.countWidth();
}

int Bubble::countMaxWidth(int maxPossibleCounter) const {
	auto numbers = Ui::NumbersAnimation(_st.font, [] {});
	numbers.setDisabledMonospace(true);
	numbers.setDuration(0);
	numbers.setText(_textFactory(0), 0);
	numbers.setText(_textFactory(maxPossibleCounter), maxPossibleCounter);
	numbers.finishAnimating();
	return filledWidth() + numbers.maxWidth();
}

void Bubble::setCounter(int value) {
	if (_counter != value) {
		_counter = value;
		_numberAnimation.setText(_textFactory(_counter), _counter);
	}
}

void Bubble::setTailEdge(EdgeProgress edge) {
	_tailEdge = std::clamp(edge, 0., 1.);
}

void Bubble::setFlipHorizontal(bool value) {
	_flipHorizontal = value;
}

void Bubble::paintBubble(QPainter &p, const QRect &r, const QBrush &brush) {
	if (_counter < 0) {
		return;
	}

	const auto penWidth = _st.penWidth;
	const auto penWidthHalf = penWidth / 2;
	const auto bubbleRect = r - style::margins(
		penWidthHalf,
		penWidthHalf,
		penWidthHalf,
		_st.tailSize.height() + penWidthHalf);
	{
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

		auto hq = PainterHighQualityEnabler(p);
		p.setPen(QPen(
			brush,
			penWidth,
			Qt::SolidLine,
			Qt::RoundCap,
			Qt::RoundJoin));
		p.setBrush(brush);
		if (_flipHorizontal) {
			auto m = QTransform();
			const auto center = QRectF(bubbleRect).center();
			m.translate(center.x(), center.y());
			m.scale(-1., 1.);
			m.translate(-center.x(), -center.y());
			p.drawPath(m.map(pathTail + pathBubble));
		} else {
			p.drawPath(pathTail + pathBubble);
		}
	}
	p.setPen(st::activeButtonFg);
	p.setFont(_st.font);
	const auto iconLeft = r.x() + _st.padding.left();
	_icon->paint(
		p,
		iconLeft,
		bubbleRect.y() + (bubbleRect.height() - _icon->height()) / 2,
		bubbleRect.width());
	_numberAnimation.paint(
		p,
		iconLeft + _icon->width() + _st.textSkip,
		r.y() + _textTop,
		width() / 2);
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
: RpWidget(parent)
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
	const auto resizeTo = [=](int w, int h) {
		_deflection = (w > _st.widthLimit)
			? kDeflectionSmall
			: kDeflection;
		_spaceForDeflection = QSize(_st.skip, _st.skip);
		resize(QSize(w, h) + 2 * _spaceForDeflection);
	};

	resizeTo(_bubble.width(), _bubble.height());
	_bubble.widthChanges(
	) | rpl::start_with_next([=] {
		resizeTo(_bubble.width(), _bubble.height());
	}, lifetime());

	std::move(
		showFinishes
	) | rpl::take(1) | rpl::start_with_next([=] {
		_state.value(
		) | rpl::start_with_next([=](BubbleRowState state) {
			animateTo(state);
		}, lifetime());

		parent->widthValue() | rpl::start_with_next([=](int w) {
			if (!_appearanceAnimation.animating()) {
				const auto x = base::SafeRound(
					w * _state.current().ratio - width() / 2);
				const auto padding = _spaceForDeflection.width();
				moveToLeft(
					std::clamp(int(x), -padding, w - width() + padding),
					y());
			}
		}, lifetime());
	}, lifetime());
}

void BubbleWidget::animateTo(BubbleRowState state) {
	_maxBubbleWidth = _bubble.countMaxWidth(state.counter);
	const auto parent = parentWidget();
	const auto available = parent->width()
		- _outerPadding.left()
		- _outerPadding.right();
	const auto halfWidth = (_maxBubbleWidth / 2);
	const auto computeLeft = [=](float64 pointRatio, float64 animProgress) {
		const auto delta = (pointRatio - _animatingFromResultRatio);
		const auto center = available
			* (_animatingFromResultRatio + delta * animProgress);
		return center - halfWidth + _outerPadding.left();
	};
	const auto moveEndPoint = state.ratio;
	const auto computeRightEdge = [=] {
		return parent->width()
			- _outerPadding.right()
			- _maxBubbleWidth;
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
	_appearanceAnimation.start([=](float64 value) {
		if (!_appearanceAnimation.animating()) {
			_animatingFrom = state;
			_animatingFromResultRatio = resultMoveEndPoint;
			_animatingFromBubbleEdge = finalEdge;
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

		_bubble.setFlipHorizontal(nowBubbleEdge < 0);
		_bubble.setTailEdge(std::abs(nowBubbleEdge));
		update();
	},
	0.,
	(state.ratio >= _animatingFrom.ratio) ? 1. : -1.,
	duration,
	anim::easeOutCirc);
}

void BubbleWidget::paintEvent(QPaintEvent *e) {
	if (_bubble.counter() < 0) {
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
		switch (_type) {
		case BubbleType::NoPremium: return st::windowBgActive->b;
		case BubbleType::Premium: return QBrush(_cachedGradient);
		case BubbleType::Credits: return st::creditsBg3->b;
		}
		Unexpected("Type in Premium::BubbleWidget.");
	}());
}

void AddBubbleRow(
		not_null<Ui::VerticalLayout*> parent,
		const style::PremiumBubble &st,
		rpl::producer<> showFinishes,
		int min,
		int current,
		int max,
		BubbleType type,
		std::optional<tr::phrase<lngtag_count>> phrase,
		const style::icon *icon) {
	AddBubbleRow(
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

void AddBubbleRow(
		not_null<Ui::VerticalLayout*> parent,
		const style::PremiumBubble &st,
		rpl::producer<> showFinishes,
		rpl::producer<BubbleRowState> state,
		BubbleType type,
		Fn<QString(int)> text,
		const style::icon *icon,
		const style::margins &outerPadding) {
	const auto container = parent->add(
		object_ptr<Ui::FixedHeightWidget>(parent, 0));
	const auto bubble = Ui::CreateChild<BubbleWidget>(
		container,
		st,
		text ? std::move(text) : ProcessTextFactory(std::nullopt),
		std::move(state),
		type,
		std::move(showFinishes),
		icon,
		outerPadding);
	rpl::combine(
		container->sizeValue(),
		bubble->sizeValue()
	) | rpl::start_with_next([=](const QSize &parentSize, const QSize &size) {
		container->resize(parentSize.width(), size.height());
	}, bubble->lifetime());
	bubble->show();
}

} // namespace Ui::Premium
