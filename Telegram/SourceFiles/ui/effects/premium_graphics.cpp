/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/premium_graphics.h"

#include "data/data_subscription_option.h"
#include "lang/lang_keys.h"
#include "ui/abstract_button.h"
#include "ui/effects/animations.h"
#include "ui/effects/gradient.h"
#include "ui/effects/numbers_animation.h"
#include "ui/text/text_utilities.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_options.h"
#include "ui/widgets/checkbox.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "styles/style_premium.h"
#include "styles/style_boxes.h"
#include "styles/style_settings.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

#include <QtGui/QBrush>

namespace Ui {
namespace Premium {
namespace {

using TextFactory = Fn<QString(int)>;

constexpr auto kBubbleRadiusSubtractor = 2;
constexpr auto kDeflectionSmall = 20.;
constexpr auto kDeflection = 30.;
constexpr auto kSlideDuration = crl::time(1000);

constexpr auto kStepBeforeDeflection = 0.75;
constexpr auto kStepAfterDeflection = kStepBeforeDeflection
	+ (1. - kStepBeforeDeflection) / 2.;

class GradientRadioView : public Ui::RadioView {
public:
	GradientRadioView(
		const style::Radio &st,
		bool checked,
		Fn<void()> updateCallback = nullptr);

	void setBrush(std::optional<QBrush> brush);

	void paint(QPainter &p, int left, int top, int outerWidth) override;

private:

	not_null<const style::Radio*> _st;
	std::optional<QBrush> _brushOverride;

};

GradientRadioView::GradientRadioView(
	const style::Radio &st,
	bool checked,
	Fn<void()> updateCallback)
: Ui::RadioView(st, checked, updateCallback)
, _st(&st) {
}

void GradientRadioView::paint(QPainter &p, int left, int top, int outerWidth) {
	PainterHighQualityEnabler hq(p);

	const auto toggled = currentAnimationValue();
	const auto toggledFg = _brushOverride
		? (*_brushOverride)
		: QBrush(_st->toggledFg);

	{
		const auto skip = (_st->outerSkip / 10.) + (_st->thickness / 2);
		const auto rect = QRectF(left, top, _st->diameter, _st->diameter)
			- QMarginsF(skip, skip, skip, skip);

		p.setBrush(_st->bg);
		if (toggled < 1) {
			p.setPen(QPen(_st->untoggledFg, _st->thickness));
			p.drawEllipse(style::rtlrect(rect, outerWidth));
		}
		if (toggled > 0) {
			p.setOpacity(toggled);
			p.setPen(QPen(toggledFg, _st->thickness));
			p.drawEllipse(style::rtlrect(rect, outerWidth));
		}
	}

	if (toggled > 0) {
		p.setPen(Qt::NoPen);
		p.setBrush(toggledFg);

		const auto skip0 = _st->diameter / 2.;
		const auto skip1 = _st->skip / 10.;
		const auto checkSkip = skip0 * (1. - toggled) + skip1 * toggled;
		const auto rect = QRectF(left, top, _st->diameter, _st->diameter)
			- QMarginsF(checkSkip, checkSkip, checkSkip, checkSkip);
		p.drawEllipse(style::rtlrect(rect, outerWidth));
	}
}

void GradientRadioView::setBrush(std::optional<QBrush> brush) {
	_brushOverride = brush;
}

[[nodiscard]] TextFactory ProcessTextFactory(
		std::optional<tr::phrase<lngtag_count>> phrase) {
	return phrase
		? TextFactory([=](int n) { return (*phrase)(tr::now, lt_count, n); })
		: TextFactory([=](int n) { return QString::number(n); });
}

[[nodiscard]] QLinearGradient ComputeGradient(
		not_null<QWidget*> content,
		int left,
		int width) {

	// Take a full width of parent box without paddings.
	const auto fullGradientWidth = content->parentWidget()->width();
	auto fullGradient = QLinearGradient(0, 0, fullGradientWidth, 0);
	fullGradient.setStops(ButtonGradientStops());

	auto gradient = QLinearGradient(0, 0, width, 0);
	const auto fullFinal = float64(fullGradient.finalStop().x());
	left += ((fullGradientWidth - content->width()) / 2);
	gradient.setColorAt(
		.0,
		anim::gradient_color_at(fullGradient, left / fullFinal));
	gradient.setColorAt(
		1.,
		anim::gradient_color_at(fullGradient, (left + width) / fullFinal));

	return gradient;
}

class PartialGradient final {
public:
	PartialGradient(int from, int to, QGradientStops stops);

	[[nodiscard]] QLinearGradient compute(int position, int size) const;

private:
	const int _from;
	const int _to;
	QLinearGradient _gradient;

};

PartialGradient::PartialGradient(int from, int to, QGradientStops stops)
: _from(from)
, _to(to)
, _gradient(0, 0, 0, to - from) {
	_gradient.setStops(std::move(stops));
}

QLinearGradient PartialGradient::compute(int position, int size) const {
	const auto pointTop = position - _from;
	const auto pointBottom = pointTop + size;
	const auto ratioTop = pointTop / float64(_to - _from);
	const auto ratioBottom = pointBottom / float64(_to - _from);

	auto resultGradient = QLinearGradient(
		QPointF(),
		QPointF(0, pointBottom - pointTop));

	resultGradient.setColorAt(
		.0,
		anim::gradient_color_at(_gradient, ratioTop));
	resultGradient.setColorAt(
		.1,
		anim::gradient_color_at(_gradient, ratioBottom));
	return resultGradient;
}

class Bubble final {
public:
	using EdgeProgress = float64;

	Bubble(
		const style::PremiumBubble &st,
		Fn<void()> updateCallback,
		TextFactory textFactory,
		const style::icon *icon,
		bool premiumPossible);

	[[nodiscard]] int counter() const;
	[[nodiscard]] int height() const;
	[[nodiscard]] int width() const;
	[[nodiscard]] int bubbleRadius() const;
	[[nodiscard]] int countMaxWidth(int maxCounter) const;

	void setCounter(int value);
	void setTailEdge(EdgeProgress edge);
	void setFlipHorizontal(bool value);
	void paintBubble(QPainter &p, const QRect &r, const QBrush &brush);

	[[nodiscard]] rpl::producer<> widthChanges() const;

private:
	[[nodiscard]] int filledWidth() const;

	const style::PremiumBubble &_st;

	const Fn<void()> _updateCallback;
	const TextFactory _textFactory;

	const style::icon *_icon;
	NumbersAnimation _numberAnimation;
	const int _height;
	const int _textTop;
	const bool _premiumPossible;

	int _counter = -1;
	EdgeProgress _tailEdge = 0.;
	bool _flipHorizontal = false;

	rpl::event_stream<> _widthChanges;

};

Bubble::Bubble(
	const style::PremiumBubble &st,
	Fn<void()> updateCallback,
	TextFactory textFactory,
	const style::icon *icon,
	bool premiumPossible)
: _st(st)
, _updateCallback(std::move(updateCallback))
, _textFactory(std::move(textFactory))
, _icon(icon)
, _numberAnimation(_st.font, _updateCallback)
, _height(_st.height + _st.tailSize.height())
, _textTop((_height - _st.tailSize.height() - _st.font->height) / 2)
, _premiumPossible(premiumPossible) {
	_numberAnimation.setDisabledMonospace(true);
	_numberAnimation.setWidthChangedCallback([=] {
		_widthChanges.fire({});
	});
	_numberAnimation.setText(_textFactory(0), 0);
	_numberAnimation.finishAnimating();
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

int Bubble::countMaxWidth(int maxCounter) const {
	auto numbers = Ui::NumbersAnimation(_st.font, [] {});
	numbers.setDisabledMonospace(true);
	numbers.setDuration(0);
	numbers.setText(_textFactory(0), 0);
	numbers.setText(_textFactory(maxCounter), maxCounter);
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
		if (_premiumPossible) {
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

		PainterHighQualityEnabler hq(p);
		p.setPen(QPen(
			brush,
			penWidth,
			Qt::SolidLine,
			Qt::RoundCap,
			Qt::RoundJoin));
		p.setBrush(brush);
		if (_flipHorizontal) {
			auto m = QTransform();
			const auto center = bubbleRect.center();
			m.translate(center.x(), center.y());
			m.scale(-1., 1.);
			m.translate(-center.x(), -center.y());
			m.translate(-bubbleRect.left() + 1., 0);
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

class BubbleWidget final : public Ui::RpWidget {
public:
	BubbleWidget(
		not_null<Ui::RpWidget*> parent,
		const style::PremiumBubble &st,
		TextFactory textFactory,
		rpl::producer<BubbleRowState> state,
		int maxCounter,
		bool premiumPossible,
		rpl::producer<> showFinishes,
		const style::icon *icon);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void animateTo(BubbleRowState state);

	const style::PremiumBubble &_st;
	BubbleRowState _animatingFrom;
	float64 _animatingFromResultRatio = 0.;
	rpl::variable<BubbleRowState> _state;
	const int _maxCounter;
	Bubble _bubble;
	const int _maxBubbleWidth;
	const bool _premiumPossible;

	Ui::Animations::Simple _appearanceAnimation;
	QSize _spaceForDeflection;

	QLinearGradient _cachedGradient;

	float64 _deflection;

	bool _ignoreDeflection = false;
	float64 _stepBeforeDeflection;
	float64 _stepAfterDeflection;

};

BubbleWidget::BubbleWidget(
	not_null<Ui::RpWidget*> parent,
	const style::PremiumBubble &st,
	TextFactory textFactory,
	rpl::producer<BubbleRowState> state,
	int maxCounter,
	bool premiumPossible,
	rpl::producer<> showFinishes,
	const style::icon *icon)
: RpWidget(parent)
, _st(st)
, _state(std::move(state))
, _maxCounter(maxCounter)
, _bubble(
	_st,
	[=] { update(); },
	std::move(textFactory),
	icon,
	premiumPossible)
, _maxBubbleWidth(_bubble.countMaxWidth(_maxCounter))
, _premiumPossible(premiumPossible)
, _deflection(kDeflection)
, _stepBeforeDeflection(kStepBeforeDeflection)
, _stepAfterDeflection(kStepAfterDeflection) {
	const auto resizeTo = [=](int w, int h) {
		_deflection = (w > _st.widthLimit)
			? kDeflectionSmall
			: kDeflection;
		_spaceForDeflection = QSize(_st.skip, _st.skip);
		resize(QSize(w, h) + _spaceForDeflection);
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
	}, lifetime());
}

void BubbleWidget::animateTo(BubbleRowState state) {
	const auto parent = parentWidget();
	const auto computeLeft = [=](float64 pointRatio, float64 animProgress) {
		const auto &padding = st::boxRowPadding;
		const auto halfWidth = (_maxBubbleWidth / 2);
		const auto left = padding.left();
		const auto right = padding.right();
		const auto available = parent->width() - left - right;
		const auto delta = (pointRatio - _animatingFromResultRatio);
		const auto center = available
			* (_animatingFromResultRatio + delta * animProgress);
		return center - halfWidth + left;
	};
	const auto moveEndPoint = state.ratio;
	const auto computeEdge = [=] {
		return parent->width()
			- st::boxRowPadding.right()
			- _maxBubbleWidth;
	};
	struct LeftEdge final {
		float64 goodPointRatio = 0.;
		float64 bubbleLeftEdge = 0.;
	};
	const auto leftEdge = [&]() -> LeftEdge {
		const auto finish = computeLeft(moveEndPoint, 1.);
		const auto &padding = st::boxRowPadding;
		if (finish <= padding.left()) {
			const auto halfWidth = (_maxBubbleWidth / 2);
			const auto goodPointRatio = float64(halfWidth)
				/ (parent->width() - padding.left() - padding.right());
			const auto bubbleLeftEdge = (padding.left() - finish)
				/ (_maxBubbleWidth / 2.);
			return { goodPointRatio, bubbleLeftEdge };
		}
		return {};
	}();
	const auto checkBubbleRightEdge = [&]() -> Bubble::EdgeProgress {
		const auto finish = computeLeft(moveEndPoint, 1.);
		const auto edge = computeEdge();
		return (finish >= edge)
			? (finish - edge) / (_maxBubbleWidth / 2.)
			: 0.;
	};
	const auto bubbleRightEdge = checkBubbleRightEdge();
	_ignoreDeflection = !_state.current().dynamic
		&& (bubbleRightEdge || leftEdge.goodPointRatio);
	if (_ignoreDeflection) {
		_stepBeforeDeflection = 1.;
		_stepAfterDeflection = 1.;
	}
	const auto resultMoveEndPoint = leftEdge.goodPointRatio
		? leftEdge.goodPointRatio
		: moveEndPoint;
	_bubble.setFlipHorizontal(leftEdge.bubbleLeftEdge);

	const auto duration = kSlideDuration
		* (_ignoreDeflection ? kStepBeforeDeflection : 1.)
		* ((_state.current().ratio < 0.001) ? 0.5 : 1.);
	_appearanceAnimation.start([=](float64 value) {
		if (!_appearanceAnimation.animating()) {
			_animatingFrom = state;
			_animatingFromResultRatio = resultMoveEndPoint;
		}
		const auto moveProgress = std::clamp(
			(value / _stepBeforeDeflection),
			0.,
			1.);
		const auto counterProgress = std::clamp(
			(value / _stepAfterDeflection),
			0.,
			1.);
		moveToLeft(
			std::max(
				int(base::SafeRound(
					(computeLeft(resultMoveEndPoint, moveProgress)
						- (_maxBubbleWidth / 2.) * bubbleRightEdge))),
				0),
			0);

		const auto now = _animatingFrom.counter
			+ counterProgress * (state.counter - _animatingFrom.counter);
		_bubble.setCounter(int(base::SafeRound(now)));

		const auto edgeProgress = leftEdge.bubbleLeftEdge
			? leftEdge.bubbleLeftEdge
			: (bubbleRightEdge * value);
		_bubble.setTailEdge(edgeProgress);
		update();
	},
	0.,
	1.,
	duration,
	anim::easeOutCirc);
}

void BubbleWidget::paintEvent(QPaintEvent *e) {
	if (_bubble.counter() < 0) {
		return;
	}

	auto p = QPainter(this);

	const auto padding = QMargins(
		0,
		_spaceForDeflection.height(),
		_spaceForDeflection.width(),
		0);
	const auto bubbleRect = rect() - padding;

	if (_appearanceAnimation.animating()) {
		auto gradient = ComputeGradient(
			parentWidget(),
			x(),
			bubbleRect.width());
		_cachedGradient = std::move(gradient);

		const auto progress = _appearanceAnimation.value(1.);
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
			p.rotate(rotationProgress * _deflection
				- rotationProgressReverse * _deflection);
		}
		p.translate(-offsetX, -offsetY);
	}

	_bubble.paintBubble(
		p,
		bubbleRect,
		_premiumPossible ? QBrush(_cachedGradient) : st::windowBgActive->b);
}

class Line final : public Ui::RpWidget {
public:
	Line(
		not_null<Ui::RpWidget*> parent,
		const style::PremiumLimits &st,
		int max,
		TextFactory textFactory,
		int min,
		float64 ratio);
	Line(
		not_null<Ui::RpWidget*> parent,
		const style::PremiumLimits &st,
		QString max,
		QString min,
		float64 ratio);

	Line(
		not_null<Ui::RpWidget*> parent,
		const style::PremiumLimits &st,
		LimitRowLabels labels,
		rpl::producer<float64> ratio);

	void setColorOverride(QBrush brush);

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	void recache(const QSize &s);

	const style::PremiumLimits &_st;

	QPixmap _leftPixmap;
	QPixmap _rightPixmap;

	float64 _ratio = 0.;
	Ui::Animations::Simple _animation;
	Ui::Text::String _leftLabel;
	Ui::Text::String _leftText;
	Ui::Text::String _rightLabel;
	Ui::Text::String _rightText;
	bool _dynamic = false;

	std::optional<QBrush> _overrideBrush;

};

Line::Line(
	not_null<Ui::RpWidget*> parent,
	const style::PremiumLimits &st,
	int max,
	TextFactory textFactory,
	int min,
	float64 ratio)
: Line(
	parent,
	st,
	max ? textFactory(max) : QString(),
	min ? textFactory(min) : QString(),
	ratio) {
}

Line::Line(
	not_null<Ui::RpWidget*> parent,
	const style::PremiumLimits &st,
	QString max,
	QString min,
	float64 ratio)
: Line(parent, st, LimitRowLabels{
	.leftLabel = tr::lng_premium_free(tr::now),
	.leftCount = min,
	.rightLabel = tr::lng_premium(tr::now),
	.rightCount = max,
}, rpl::single(ratio)) {
}

Line::Line(
	not_null<Ui::RpWidget*> parent,
	const style::PremiumLimits &st,
	LimitRowLabels labels,
	rpl::producer<float64> ratio)
: Ui::RpWidget(parent)
, _st(st)
, _leftLabel(st::semiboldTextStyle, labels.leftLabel)
, _leftText(st::semiboldTextStyle, labels.leftCount)
, _rightLabel(st::semiboldTextStyle, labels.rightLabel)
, _rightText(st::semiboldTextStyle, labels.rightCount)
, _dynamic(labels.dynamic) {
	resize(width(), st::requestsAcceptButton.height);

	std::move(ratio) | rpl::start_with_next([=](float64 ratio) {
		if (width() > 0) {
			const auto from = _animation.value(_ratio);
			const auto duration = kSlideDuration * kStepBeforeDeflection;
			_animation.start([=] {
				update();
			}, from, ratio, duration, anim::easeOutCirc);
		}
		_ratio = ratio;
	}, lifetime());

	sizeValue(
	) | rpl::filter([](QSize size) {
		return !size.isEmpty();
	}) | rpl::start_with_next([=](QSize size) {
		recache(size);
		update();
	}, lifetime());

}

void Line::setColorOverride(QBrush brush) {
	if (brush.style() == Qt::NoBrush) {
		_overrideBrush = std::nullopt;
	} else {
		_overrideBrush = brush;
	}
}

void Line::paintEvent(QPaintEvent *event) {
	Painter p(this);

	const auto ratio = _animation.value(_ratio);
	const auto left = int(base::SafeRound(ratio * width()));
	const auto dpr = int(_leftPixmap.devicePixelRatio());
	const auto height = _leftPixmap.height() / dpr;
	p.drawPixmap(
		QRect(0, 0, left, height),
		_leftPixmap,
		QRect(0, 0, left * dpr, height * dpr));
	p.drawPixmap(
		QRect(left, 0, width() - left, height),
		_rightPixmap,
		QRect(left * dpr, 0, (width() - left) * dpr, height * dpr));

	p.setFont(st::normalFont);

	const auto textPadding = st::premiumLineTextSkip;
	const auto textTop = (height - _leftLabel.minHeight()) / 2;

	const auto leftMinWidth = _leftLabel.maxWidth()
		+ _leftText.maxWidth()
		+ 3 * textPadding;
	const auto pen = [&](bool gradient) {
		return gradient ? st::activeButtonFg : _st.nonPremiumFg;
	};
	if (!_dynamic && left >= leftMinWidth) {
		p.setPen(pen(_st.gradientFromLeft));
		_leftLabel.drawLeft(
			p,
			textPadding,
			textTop,
			left - textPadding,
			left);
		_leftText.drawRight(
			p,
			textPadding,
			textTop,
			left - textPadding,
			left,
			style::al_right);
	}
	const auto right = width() - left;
	const auto rightMinWidth = 2 * _rightText.maxWidth()
		+ 3 * textPadding;
	if (!_dynamic && right >= rightMinWidth) {
		p.setPen(pen(!_st.gradientFromLeft));
		_rightLabel.drawLeftElided(
			p,
			left + textPadding,
			textTop,
			(right - _rightText.countWidth(right) - textPadding * 2),
			right);
		_rightText.drawRight(
			p,
			textPadding,
			textTop,
			right - textPadding,
			width(),
			style::al_right);
	}
}

void Line::recache(const QSize &s) {
	const auto r = [&](int width) {
		return QRect(0, 0, width, s.height());
	};
	const auto pixmap = [&](int width) {
		auto result = QPixmap(r(width).size() * style::DevicePixelRatio());
		result.setDevicePixelRatio(style::DevicePixelRatio());
		result.fill(Qt::transparent);
		return result;
	};

	const auto pathRound = [&](int width) {
		auto result = QPainterPath();
		result.addRoundedRect(r(width), st::buttonRadius, st::buttonRadius);
		return result;
	};
	const auto width = s.width();
	const auto fill = [&](QPainter &p, QPainterPath path, bool gradient) {
		if (!gradient) {
			p.fillPath(path, _st.nonPremiumBg);
		} else if (_overrideBrush) {
			p.fillPath(path, *_overrideBrush);
		} else {
			p.fillPath(path, QBrush(ComputeGradient(this, 0, width)));
		}
	};
	const auto textPadding = st::premiumLineTextSkip;
	const auto textTop = (s.height() - _leftLabel.minHeight()) / 2;
	const auto rwidth = _rightLabel.maxWidth();
	const auto pen = [&](bool gradient) {
		return gradient ? st::activeButtonFg : _st.nonPremiumFg;
	};
	{
		auto leftPixmap = pixmap(width);
		auto p = Painter(&leftPixmap);
		PainterHighQualityEnabler hq(p);
		fill(p, pathRound(width), _st.gradientFromLeft);
		if (_dynamic) {
			p.setFont(st::normalFont);
			p.setPen(pen(_st.gradientFromLeft));
			_leftLabel.drawLeft(p, textPadding, textTop, width, width);
			_rightLabel.drawRight(p, textPadding, textTop, rwidth, width);
		}
		_leftPixmap = std::move(leftPixmap);
	}
	{
		auto rightPixmap = pixmap(width);
		auto p = Painter(&rightPixmap);
		PainterHighQualityEnabler hq(p);
		fill(p, pathRound(width), !_st.gradientFromLeft);
		if (_dynamic) {
			p.setFont(st::normalFont);
			p.setPen(pen(!_st.gradientFromLeft));
			_leftLabel.drawLeft(p, textPadding, textTop, width, width);
			_rightLabel.drawRight(p, textPadding, textTop, rwidth, width);
		}
		_rightPixmap = std::move(rightPixmap);
	}
}

} // namespace

void AddBubbleRow(
		not_null<Ui::VerticalLayout*> parent,
		const style::PremiumBubble &st,
		rpl::producer<> showFinishes,
		int min,
		int current,
		int max,
		bool premiumPossible,
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
		max,
		premiumPossible,
		ProcessTextFactory(phrase),
		icon);
}

void AddBubbleRow(
		not_null<Ui::VerticalLayout*> parent,
		const style::PremiumBubble &st,
		rpl::producer<> showFinishes,
		rpl::producer<BubbleRowState> state,
		int max,
		bool premiumPossible,
		Fn<QString(int)> text,
		const style::icon *icon) {
	const auto container = parent->add(
		object_ptr<Ui::FixedHeightWidget>(parent, 0));
	const auto bubble = Ui::CreateChild<BubbleWidget>(
		container,
		st,
		text ? std::move(text) : ProcessTextFactory(std::nullopt),
		std::move(state),
		max,
		premiumPossible,
		std::move(showFinishes),
		icon);
	rpl::combine(
		container->sizeValue(),
		bubble->sizeValue()
	) | rpl::start_with_next([=](const QSize &parentSize, const QSize &size) {
		container->resize(parentSize.width(), size.height());
	}, bubble->lifetime());
}

void AddLimitRow(
		not_null<Ui::VerticalLayout*> parent,
		const style::PremiumLimits &st,
		QString max,
		QString min,
		float64 ratio) {
	parent->add(
		object_ptr<Line>(parent, st, max, min, ratio),
		st::boxRowPadding);
}

void AddLimitRow(
		not_null<Ui::VerticalLayout*> parent,
		const style::PremiumLimits &st,
		int max,
		std::optional<tr::phrase<lngtag_count>> phrase,
		int min,
		float64 ratio) {
	const auto factory = ProcessTextFactory(phrase);
	AddLimitRow(
		parent,
		st,
		max ? factory(max) : QString(),
		min ? factory(min) : QString(),
		ratio);
}

void AddLimitRow(
		not_null<Ui::VerticalLayout*> parent,
		const style::PremiumLimits &st,
		LimitRowLabels labels,
		rpl::producer<float64> ratio) {
	parent->add(
		object_ptr<Line>(parent, st, std::move(labels), std::move(ratio)),
		st::boxRowPadding);
}

void AddAccountsRow(
		not_null<Ui::VerticalLayout*> parent,
		AccountsRowArgs &&args) {
	const auto container = parent->add(
		object_ptr<Ui::FixedHeightWidget>(parent, st::premiumAccountsHeight),
		st::boxRowPadding);

	struct Account {
		not_null<Ui::AbstractButton*> widget;
		Ui::RoundImageCheckbox checkbox;
		Ui::Text::String name;
		QPixmap badge;
	};
	struct State {
		std::vector<Account> accounts;
	};
	const auto state = container->lifetime().make_state<State>();
	const auto group = args.group;

	const auto imageRadius = args.st.imageRadius;
	const auto checkSelectWidth = args.st.selectWidth;
	const auto nameFg = args.stNameFg;

	const auto cacheBadge = [=](int center) {
		const auto &padding = st::premiumAccountsLabelPadding;
		const auto size = st::premiumAccountsLabelSize
			+ QSize(
				padding.left() + padding.right(),
				padding.top() + padding.bottom());
		auto badge = QPixmap(size * style::DevicePixelRatio());
		badge.setDevicePixelRatio(style::DevicePixelRatio());
		badge.fill(Qt::transparent);

		auto p = QPainter(&badge);
		PainterHighQualityEnabler hq(p);

		p.setPen(Qt::NoPen);
		const auto rectOut = QRect(QPoint(), size);
		const auto rectIn = rectOut - padding;

		const auto radius = st::premiumAccountsLabelRadius;
		p.setBrush(st::premiumButtonFg);
		p.drawRoundedRect(rectOut, radius, radius);

		const auto left = center - rectIn.width() / 2;
		p.setBrush(QBrush(ComputeGradient(container, left, rectIn.width())));
		p.drawRoundedRect(rectIn, radius / 2, radius / 2);

		p.setPen(st::premiumButtonFg);
		p.setFont(st::semiboldFont);
		p.drawText(rectIn, u"+1"_q, style::al_center);

		return badge;
	};

	for (auto &entry : args.entries) {
		const auto widget = Ui::CreateChild<Ui::AbstractButton>(container);
		auto name = Ui::Text::String(imageRadius * 2);
		name.setText(args.stName, entry.name, Ui::NameTextOptions());
		state->accounts.push_back(Account{
			.widget = widget,
			.checkbox = Ui::RoundImageCheckbox(
				args.st,
				[=] { widget->update(); },
				base::take(entry.paintRoundImage)),
			.name = std::move(name),
		});
		const auto index = int(state->accounts.size()) - 1;
		state->accounts[index].checkbox.setChecked(
			index == group->value(),
			anim::type::instant);

		widget->paintRequest(
		) | rpl::start_with_next([=] {
			Painter p(widget);
			const auto width = widget->width();
			const auto photoLeft = (width - (imageRadius * 2)) / 2;
			const auto photoTop = checkSelectWidth;
			const auto &account = state->accounts[index];
			account.checkbox.paint(p, photoLeft, photoTop, width);

			const auto &badgeSize = account.badge.size()
				/ style::DevicePixelRatio();
			p.drawPixmap(
				(width - badgeSize.width()) / 2,
				photoTop + (imageRadius * 2) - badgeSize.height() / 2,
				account.badge);

			p.setPen(nameFg);
			p.setBrush(Qt::NoBrush);
			account.name.drawLeftElided(
				p,
				0,
				photoTop + imageRadius * 2 + st::premiumAccountsNameTop,
				width,
				width,
				2,
				style::al_top,
				0,
				-1,
				0,
				true);
		}, widget->lifetime());

		widget->setClickedCallback([=] {
			group->setValue(index);
		});
	}

	container->sizeValue(
	) | rpl::start_with_next([=](const QSize &size) {
		const auto count = state->accounts.size();
		const auto columnWidth = size.width() / count;
		for (auto i = 0; i < count; i++) {
			auto &account = state->accounts[i];
			account.widget->resize(columnWidth, size.height());
			const auto left = columnWidth * i;
			account.widget->moveToLeft(left, 0);
			account.badge = cacheBadge(left + columnWidth / 2);

			const auto photoWidth = ((imageRadius + checkSelectWidth) * 2);
			account.checkbox.setColorOverride(QBrush(
				ComputeGradient(
					container,
					left + (columnWidth - photoWidth) / 2,
					photoWidth)));
		}
	}, container->lifetime());

	group->setChangedCallback([=](int value) {
		for (auto i = 0; i < state->accounts.size(); i++) {
			state->accounts[i].checkbox.setChecked(i == value);
		}
	});
}

QGradientStops LimitGradientStops() {
	return {
		{ 0.0, st::premiumButtonBg1->c },
		{ .25, st::premiumButtonBg1->c },
		{ .85, st::premiumButtonBg2->c },
		{ 1.0, st::premiumButtonBg3->c },
	};
}

QGradientStops ButtonGradientStops() {
	return {
		{ 0., st::premiumButtonBg1->c },
		{ 0.6, st::premiumButtonBg2->c },
		{ 1., st::premiumButtonBg3->c },
	};
}

QGradientStops LockGradientStops() {
	return ButtonGradientStops();
}

QGradientStops FullHeightGradientStops() {
	return {
		{ 0.0, st::premiumIconBg1->c },
		{ .28, st::premiumIconBg2->c },
		{ .55, st::premiumButtonBg2->c },
		{ .75, st::premiumButtonBg1->c },
		{ 1.0, st::premiumIconBg3->c },
	};
}

QGradientStops GiftGradientStops() {
	return {
		{ 0., st::premiumButtonBg1->c },
		{ 1., st::premiumButtonBg2->c },
	};
}

QGradientStops StoriesIconsGradientStops() {
	return {
		{ 0., st::premiumButtonBg1->c },
		{ .33, st::premiumButtonBg2->c },
		{ .66, st::premiumButtonBg3->c },
		{ 1., st::premiumIconBg1->c },
	};
}

void ShowListBox(
		not_null<Ui::GenericBox*> box,
		const style::PremiumLimits &st,
		std::vector<ListEntry> entries) {
	box->setWidth(st::boxWideWidth);

	const auto &stLabel = st::defaultFlatLabel;
	const auto &titlePadding = st::settingsPremiumPreviewTitlePadding;
	const auto &aboutPadding = st::settingsPremiumPreviewAboutPadding;
	const auto iconTitlePadding = st::settingsPremiumPreviewIconTitlePadding;
	const auto iconAboutPadding = st::settingsPremiumPreviewIconAboutPadding;

	auto lines = std::vector<Line*>();
	lines.reserve(int(entries.size()));

	auto icons = std::shared_ptr<std::vector<QColor>>();

	const auto content = box->verticalLayout();
	for (auto &entry : entries) {
		const auto title = content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				base::take(entry.title) | rpl::map(Ui::Text::Bold),
				stLabel),
			entry.icon ? iconTitlePadding : titlePadding);
		content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				base::take(entry.about),
				st::boxDividerLabel),
			entry.icon ? iconAboutPadding : aboutPadding);
		if (const auto outlined = entry.icon) {
			if (!icons) {
				icons = std::make_shared<std::vector<QColor>>();
			}
			const auto index = int(icons->size());
			icons->push_back(QColor());
			const auto icon = Ui::CreateChild<Ui::RpWidget>(content.get());
			icon->resize(outlined->size());
			title->topValue() | rpl::start_with_next([=](int y) {
				const auto shift = st::settingsPremiumPreviewIconPosition;
				icon->move(QPoint(0, y) + shift);
			}, icon->lifetime());
			icon->paintRequest() | rpl::start_with_next([=] {
				auto p = QPainter(icon);
				outlined->paintInCenter(p, icon->rect(), (*icons)[index]);
			}, icon->lifetime());
		}
		if (entry.leftNumber || entry.rightNumber) {
			auto factory = [=, text = ProcessTextFactory({})](int n) {
				if (entry.customRightText && (n == entry.rightNumber)) {
					return *entry.customRightText;
				} else {
					return text(n);
				}
			};
			const auto limitRow = content->add(
				object_ptr<Line>(
					content,
					st,
					entry.rightNumber,
					TextFactory(std::move(factory)),
					entry.leftNumber,
					kLimitRowRatio),
				st::settingsPremiumPreviewLinePadding);
			lines.push_back(limitRow);
		}
	}

	content->resizeToWidth(content->height());

	// Colors for icons.
	if (icons) {
		box->addSkip(st::settingsPremiumPreviewLinePadding.bottom());

		const auto stops = Ui::Premium::StoriesIconsGradientStops();
		for (auto i = 0, count = int(icons->size()); i != count; ++i) {
			(*icons)[i] = anim::gradient_color_at(
				stops,
				(count > 1) ? (i / float64(count - 1)) : 0.);
		}
	}

	// Color lines.
	if (!lines.empty()) {
		box->addSkip(st::settingsPremiumPreviewLinePadding.bottom());

		const auto from = lines.front()->y();
		const auto to = lines.back()->y() + lines.back()->height();

		const auto partialGradient = [&] {
			auto stops = Ui::Premium::FullHeightGradientStops();
			// Reverse.
			for (auto &stop : stops) {
				stop.first = std::abs(stop.first - 1.);
			}
			return PartialGradient(from, to, std::move(stops));
		}();

		for (auto i = 0; i < int(lines.size()); i++) {
			const auto &line = lines[i];

			const auto brush = QBrush(
				partialGradient.compute(line->y(), line->height()));
			line->setColorOverride(brush);
		}
		box->addSkip(st::settingsPremiumPreviewLinePadding.bottom());
	}
}

void AddGiftOptions(
		not_null<Ui::VerticalLayout*> parent,
		std::shared_ptr<Ui::RadiobuttonGroup> group,
		std::vector<Data::SubscriptionOption> gifts,
		const style::PremiumOption &st,
		bool topBadges) {

	struct Edges {
		Ui::RpWidget *top = nullptr;
		Ui::RpWidget *bottom = nullptr;
	};
	const auto edges = parent->lifetime().make_state<Edges>();
	struct Animation {
		int nowIndex = 0;
		Ui::Animations::Simple animation;
	};
	const auto animation = parent->lifetime().make_state<Animation>();

	const auto stops = GiftGradientStops();

	const auto addRow = [&](const Data::SubscriptionOption &info, int index) {
		const auto row = parent->add(
			object_ptr<Ui::AbstractButton>(parent),
			st.rowPadding);
		row->resize(row->width(), st.rowHeight);
		{
			if (!index) {
				edges->top = row;
			}
			edges->bottom = row;
		}

		const auto &stCheckbox = st::defaultBoxCheckbox;
		auto radioView = std::make_unique<GradientRadioView>(
			st::defaultRadio,
			(group->hasValue() && group->value() == index));
		const auto radioViewRaw = radioView.get();
		const auto radio = Ui::CreateChild<Ui::Radiobutton>(
			row,
			group,
			index,
			QString(),
			stCheckbox,
			std::move(radioView));
		radio->setAttribute(Qt::WA_TransparentForMouseEvents);
		{ // Paint the last frame instantly for the layer animation.
			group->setValue(0);
			radio->finishAnimating();
		}

		row->sizeValue(
		) | rpl::start_with_next([=, margins = stCheckbox.margin](
				const QSize &s) {
			const auto radioHeight = radio->height()
				- margins.top()
				- margins.bottom();
			radio->moveToLeft(
				st.rowMargins.left(),
				(s.height() - radioHeight) / 2);
		}, radio->lifetime());

		{
			auto onceLifetime = std::make_shared<rpl::lifetime>();
			row->paintRequest(
			) | rpl::take(
				1
			) | rpl::start_with_next([=]() mutable {
				const auto from = edges->top->y();
				const auto to = edges->bottom->y() + edges->bottom->height();
				auto partialGradient = PartialGradient(from, to, stops);

				radioViewRaw->setBrush(
					partialGradient.compute(row->y(), row->height()));
				if (onceLifetime) {
					base::take(onceLifetime)->destroy();
				}
			}, *onceLifetime);
		}

		row->paintRequest(
		) | rpl::start_with_next([=](const QRect &r) {
			auto p = QPainter(row);
			PainterHighQualityEnabler hq(p);

			p.fillRect(r, Qt::transparent);

			const auto left = st.textLeft;
			const auto halfHeight = row->height() / 2;

			const auto titleFont = st::semiboldFont;
			p.setFont(titleFont);
			p.setPen(st::boxTextFg);
			if (info.costPerMonth.isEmpty() && info.discount.isEmpty()) {
				const auto r = row->rect().translated(
					-row->rect().left() + left,
					0);
				p.drawText(r, info.duration, style::al_left);
			} else {
				p.drawText(
					left,
					st.subtitleTop + titleFont->ascent,
					info.duration);
			}

			const auto discountFont = st::windowFiltersButton.badgeStyle.font;
			const auto discountWidth = discountFont->width(info.discount);
			const auto &discountMargins = discountWidth
				? st.badgeMargins
				: style::margins();

			const auto bottomLeftRect = QRect(
				left,
				halfHeight + discountMargins.top(),
				discountWidth
					+ discountMargins.left()
					+ discountMargins.right(),
				st.badgeHeight);
			const auto discountRect = topBadges
				? bottomLeftRect.translated(
					titleFont->width(info.duration) + st.badgeShift.x(),
					-bottomLeftRect.top()
						+ st.badgeShift.y()
						+ st.subtitleTop
						+ (titleFont->height - bottomLeftRect.height()) / 2)
				: bottomLeftRect;
			const auto from = edges->top->y();
			const auto to = edges->bottom->y() + edges->bottom->height();
			auto partialGradient = PartialGradient(from, to, stops);
			const auto partialGradientBrush = partialGradient.compute(
				row->y(),
				row->height());
			{
				p.setPen(Qt::NoPen);
				p.setBrush(partialGradientBrush);
				const auto round = st.badgeRadius;
				p.drawRoundedRect(discountRect, round, round);
			}

			if (st.borderWidth && (animation->nowIndex == index)) {
				const auto progress = animation->animation.value(1.);
				const auto w = row->width();
				auto gradient = QLinearGradient(w - w * progress, 0, w * 2, 0);
				gradient.setSpread(QGradient::Spread::RepeatSpread);
				gradient.setStops(stops);
				const auto pen = QPen(
					QBrush(partialGradientBrush),
					st.borderWidth);
				p.setPen(pen);
				p.setBrush(Qt::NoBrush);
				const auto borderRect = row->rect()
					- QMargins(
						pen.width() / 2,
						pen.width() / 2,
						pen.width() / 2,
						pen.width() / 2);
				const auto round = st.borderRadius;
				p.drawRoundedRect(borderRect, round, round);
			}

			p.setPen(st::premiumButtonFg);
			p.setFont(discountFont);
			p.drawText(discountRect, info.discount, style::al_center);

			const auto perRect = QMargins(0, 0, row->width(), 0)
				+ bottomLeftRect.translated(
					topBadges
						? 0
						: bottomLeftRect.width() + discountMargins.left(),
					0);
			p.setPen(st::windowSubTextFg);
			p.setFont(st::shareBoxListItem.nameStyle.font);
			p.drawText(perRect, info.costPerMonth, style::al_left);

			const auto totalRect = row->rect()
				- QMargins(0, 0, st.rowMargins.right(), 0);
			p.setFont(st::normalFont);
			p.drawText(totalRect, info.costTotal, style::al_right);
		}, row->lifetime());

		row->setClickedCallback([=, duration = st::defaultCheck.duration] {
			group->setValue(index);
			animation->nowIndex = group->value();
			animation->animation.stop();
			animation->animation.start(
				[=] { parent->update(); },
				0.,
				1.,
				duration);
		});

	};
	for (auto i = 0; i < gifts.size(); i++) {
		addRow(gifts[i], i);
	}

	parent->resizeToWidth(parent->height());
	parent->update();
}

} // namespace Premium
} // namespace Ui
