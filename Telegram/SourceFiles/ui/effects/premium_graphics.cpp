/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/premium_graphics.h"

#include "lang/lang_keys.h"
#include "ui/effects/animations.h"
#include "ui/effects/gradient.h"
#include "ui/effects/numbers_animation.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

namespace Ui {
namespace Premium {
namespace {

using TextFactory = Fn<QString(int)>;

constexpr auto kDeflection = 30.;

constexpr auto kStepBeforeDeflection = 0.75;
constexpr auto kStepAfterDeflection = kStepBeforeDeflection
	+ (1. - kStepBeforeDeflection) / 2.;

[[nodiscard]] QGradientStops GradientStops() {
	return QGradientStops{
		QGradientStop(0.0, st::premiumButtonBg1->c),
		QGradientStop(.25, st::premiumButtonBg1->c),
		QGradientStop(.85, st::premiumButtonBg2->c),
		QGradientStop(1.0, st::premiumButtonBg3->c),
	};
}

[[nodiscard]] QLinearGradient ComputeGradient(
		not_null<QWidget*> content,
		int left,
		int width) {

	// Take a full width of parent box without paddings.
	const auto fullGradientWidth = content->parentWidget()->width();
	auto fullGradient = QLinearGradient(0, 0, fullGradientWidth, 0);
	fullGradient.setStops(GradientStops());

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

class Bubble final {
public:
	Bubble(
		Fn<void()> updateCallback,
		TextFactory textFactory,
		const style::icon *icon);

	[[nodiscard]] int counter() const;
	[[nodiscard]] int height() const;
	[[nodiscard]] int width() const;
	[[nodiscard]] int bubbleRadius() const;

	void setCounter(int value);
	void setTailEdge(std::optional<Qt::Edge> edge);
	void paintBubble(Painter &p, const QRect &r, const QBrush &brush);

	[[nodiscard]] rpl::producer<> widthChanges() const;
	[[nodiscard]] rpl::producer<> updateRequests() const;

private:
	const Fn<void()> _updateCallback;
	const TextFactory _textFactory;

	const style::font &_font;
	const style::margins &_padding;
	const style::icon *_icon;
	NumbersAnimation _numberAnimation;
	const QSize _tailSize;
	const int _height;
	const int _textTop;

	int _counter = -1;
	std::optional<Qt::Edge> _tailEdge;

	rpl::event_stream<> _widthChanges;

};

Bubble::Bubble(
	Fn<void()> updateCallback,
	TextFactory textFactory,
	const style::icon *icon)
: _updateCallback(std::move(updateCallback))
, _textFactory(std::move(textFactory))
, _font(st::premiumBubbleFont)
, _padding(st::premiumBubblePadding)
, _icon(icon)
, _numberAnimation(_font, _updateCallback)
, _tailSize(st::premiumBubbleTailSize)
, _height(st::premiumBubbleHeight + _tailSize.height())
, _textTop((_height - _tailSize.height() - _font->height) / 2) {
	_numberAnimation.setWidthChangedCallback([=] {
		_widthChanges.fire({});
	});
}

int Bubble::counter() const {
	return _counter;
}

int Bubble::height() const {
	return _height;
}

int Bubble::bubbleRadius() const {
	return (_height - _tailSize.height()) / 2;
}

int Bubble::width() const {
	return _padding.left()
		+ _icon->width()
		+ st::premiumBubbleTextSkip
		+ _numberAnimation.countWidth()
		+ _padding.right();
}

void Bubble::setCounter(int value) {
	if (_counter != value) {
		_counter = value;
		_numberAnimation.setText(_textFactory(_counter), _counter);
	}
}

void Bubble::setTailEdge(std::optional<Qt::Edge> edge) {
	_tailEdge = edge;
}

void Bubble::paintBubble(Painter &p, const QRect &r, const QBrush &brush) {
	if (_counter < 0) {
		return;
	}

	const auto bubbleRect = r - style::margins{ 0, 0, 0, _tailSize.height() };
	{
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(brush);
		const auto radius = bubbleRadius();
		auto pathTail = QPainterPath();

		const auto offset = bubbleRect.topLeft()
			+ QPoint(
				(_tailEdge.value_or(Qt::TopEdge) == Qt::RightEdge)
					? (bubbleRect.width() - _tailSize.width() - radius)
					: (bubbleRect.width() - _tailSize.width()) / 2,
				bubbleRect.height())
			- QPoint(0, 1);

		pathTail.moveTo(offset);
		pathTail.lineTo(QPoint(_tailSize.width() / 2, _tailSize.height())
			+ offset);
		pathTail.lineTo(offset + QPoint(_tailSize.width(), 0));
		pathTail.lineTo(offset);

		auto pathBubble = QPainterPath();
		pathBubble.setFillRule(Qt::WindingFill);
		pathBubble.addRoundedRect(bubbleRect, radius, radius);

		p.fillPath(pathTail + pathBubble, p.brush());
	}
	p.setPen(st::activeButtonFg);
	p.setFont(_font);
	const auto iconLeft = r.x() + _padding.left();
	_icon->paint(
		p,
		iconLeft,
		r.y() + (bubbleRect.height() - _icon->height()) / 2,
		r.width());
	_numberAnimation.paint(
		p,
		iconLeft + _icon->width() + st::premiumBubbleTextSkip,
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
		TextFactory textFactory,
		int current,
		int maxCounter,
		rpl::producer<> showFinishes,
		const style::icon *icon);

	[[nodiscard]] bool animating() const;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const int _currentCounter;
	const int _maxCounter;
	Bubble _bubble;

	Ui::Animations::Simple _appearanceAnimation;
	QSize _spaceForDeflection;

	QLinearGradient _cachedGradient;

};

BubbleWidget::BubbleWidget(
	not_null<Ui::RpWidget*> parent,
	TextFactory textFactory,
	int current,
	int maxCounter,
	rpl::producer<> showFinishes,
	const style::icon *icon)
: RpWidget(parent)
, _currentCounter(current)
, _maxCounter(maxCounter)
, _bubble([=] { update(); }, std::move(textFactory), icon) {
	const auto resizeTo = [=](int w, int h) {
		_spaceForDeflection = QSize(
			st::premiumBubbleSkip,
			st::premiumBubbleSkip);
		resize(QSize(w, h) + _spaceForDeflection);
	};

	resizeTo(_bubble.width(), _bubble.height());
	_bubble.widthChanges(
	) | rpl::start_with_next([=] {
		resizeTo(_bubble.width(), _bubble.height());
	}, lifetime());

	const auto moveEndPoint = _currentCounter / float64(_maxCounter);
	const auto computeLeft = [=](float64 pointRatio, float64 animProgress) {
		const auto &padding = st::boxRowPadding;
		const auto left = _bubble.bubbleRadius() + padding.left();
		const auto right = _bubble.bubbleRadius() + padding.right();
		return ((parent->width() - left - right)
				* pointRatio
				* animProgress)
			- (_bubble.width() / 2)
			+ left;
	};

	std::move(
		showFinishes
	) | rpl::take(1) | rpl::start_with_next([=] {
		const auto computeEdge = [=] {
			return computeLeft(1., 1.);
		};
		const auto checkBubbleEdges = [&]() -> std::optional<Qt::Edge> {
			const auto finish = computeLeft(moveEndPoint, 1.);
			if (finish >= computeEdge()) {
				return Qt::RightEdge;
			}
			return std::nullopt;
		};
		_bubble.setTailEdge(checkBubbleEdges());

		_appearanceAnimation.start([=](float64 value) {
			const auto moveProgress = std::clamp(
				(value / kStepBeforeDeflection),
				0.,
				1.);
			const auto counterProgress = std::clamp(
				(value / kStepAfterDeflection),
				0.,
				1.);
			moveToLeft(
				std::clamp(
					int(computeLeft(moveEndPoint, moveProgress)),
					0,
					int(computeEdge())),
				0);

			const auto counter = int(0 + counterProgress * _currentCounter);
			if (!(counter % 4) || counterProgress > 0.8) {
				_bubble.setCounter(counter);
			}
			update();
		},
		0.,
		1.,
		st::premiumBubbleSlideDuration,
		anim::easeOutCubic);
	}, lifetime());
}

bool BubbleWidget::animating() const {
	return _appearanceAnimation.animating();
}

void BubbleWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

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
		const auto scaleProgress = std::clamp(
			(progress / kStepBeforeDeflection),
			0.,
			1.);
		const auto scale = scaleProgress;
		const auto rotationProgress = std::clamp(
			(progress - kStepBeforeDeflection) / (1. - kStepBeforeDeflection),
			0.,
			1.);
		const auto rotationProgressReverse = std::clamp(
			(progress - kStepAfterDeflection) / (1. - kStepAfterDeflection),
			0.,
			1.);

		const auto offsetX = bubbleRect.x() + bubbleRect.width() / 2;
		const auto offsetY = bubbleRect.y() + bubbleRect.height();
		p.translate(offsetX, offsetY);
		p.scale(scale, scale);
		p.rotate(rotationProgress * kDeflection
			- rotationProgressReverse * kDeflection);
		p.translate(-offsetX, -offsetY);
	}

	_bubble.paintBubble(p, bubbleRect, QBrush(_cachedGradient));
}

class Line final : public Ui::RpWidget {
public:
	Line(not_null<Ui::RpWidget*> parent, int max);

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	void recache(const QSize &s);

	int _leftWidth = 0;
	int _rightWidth = 0;

	QPixmap _leftPixmap;
	QPixmap _rightPixmap;

	Ui::Text::String _leftText;
	Ui::Text::String _rightText;
	Ui::Text::String _rightLabel;

};

Line::Line(not_null<Ui::RpWidget*> parent, int max)
: Ui::RpWidget(parent)
, _leftText(st::defaultTextStyle, tr::lng_premium_free(tr::now))
, _rightText(st::defaultTextStyle, tr::lng_premium(tr::now))
, _rightLabel(st::defaultTextStyle, QString::number(max)) {
	resize(width(), st::requestsAcceptButton.height);

	sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		_leftWidth = (s.width() / 2);
		_rightWidth = (s.width() - _leftWidth);
		recache(s);
		update();
	}, lifetime());
}

void Line::paintEvent(QPaintEvent *event) {
	Painter p(this);

	p.drawPixmap(0, 0, _leftPixmap);
	p.drawPixmap(_leftWidth, 0, _rightPixmap);

	p.setFont(st::normalFont);

	const auto textPadding = st::premiumLineTextSkip;
	const auto textTop = (height() - _leftText.minHeight()) / 2;

	p.setPen(st::windowFg);
	_leftText.drawLeft(
		p,
		textPadding,
		textTop,
		_leftWidth - textPadding,
		_leftWidth);

	p.setPen(st::activeButtonFg);
	_rightLabel.drawRight(
		p,
		textPadding,
		textTop,
		_rightWidth - textPadding,
		width(),
		style::al_right);
	_rightText.drawLeftElided(
		p,
		_leftWidth + textPadding,
		textTop,
		_rightWidth - _rightLabel.countWidth(_rightWidth) - textPadding * 2,
		_rightWidth);
}

void Line::recache(const QSize &s) {
	const auto r = QRect(0, 0, _leftWidth, s.height());
	auto pixmap = QPixmap(r.size() * style::DevicePixelRatio());
	pixmap.setDevicePixelRatio(style::DevicePixelRatio());
	pixmap.fill(Qt::transparent);

	auto pathRound = QPainterPath();
	pathRound.addRoundedRect(r, st::buttonRadius, st::buttonRadius);

	{
		auto leftPixmap = pixmap;
		Painter p(&leftPixmap);
		PainterHighQualityEnabler hq(p);
		auto pathRect = QPainterPath();
		auto halfRect = r;
		halfRect.setLeft(r.center().x());
		pathRect.addRect(halfRect);

		p.fillPath(pathRound + pathRect, st::windowShadowFgFallback);

		_leftPixmap = std::move(leftPixmap);
	}
	{
		auto rightPixmap = pixmap;
		Painter p(&rightPixmap);
		PainterHighQualityEnabler hq(p);
		auto pathRect = QPainterPath();
		auto halfRect = r;
		halfRect.setRight(r.center().x());
		pathRect.addRect(halfRect);

		auto gradient = ComputeGradient(
			this,
			(_leftPixmap.width() / style::DevicePixelRatio()) + r.x(),
			r.width());
		p.fillPath(pathRound + pathRect, QBrush(std::move(gradient)));

		_rightPixmap = std::move(rightPixmap);
	}
}

} // namespace

void AddBubbleRow(
		not_null<Ui::VerticalLayout*> parent,
		rpl::producer<> showFinishes,
		int min,
		int current,
		int max,
		std::optional<tr::phrase<lngtag_count>> phrase,
		const style::icon *icon) {
	auto textFactory = phrase
		? TextFactory([=](int n) { return (*phrase)(tr::now, lt_count, n); })
		: TextFactory([=](int n) { return QString::number(n); });

	const auto container = parent->add(
		object_ptr<Ui::FixedHeightWidget>(parent, 0));
	const auto bubble = Ui::CreateChild<BubbleWidget>(
		container,
		std::move(textFactory),
		current,
		max,
		std::move(showFinishes),
		icon);
	rpl::combine(
		container->sizeValue(),
		bubble->sizeValue()
	) | rpl::start_with_next([=](const QSize &parentSize, const QSize &size) {
		container->resize(parentSize.width(), size.height());
	}, bubble->lifetime());
}

void AddLimitRow(not_null<Ui::VerticalLayout*> parent, int max) {
	const auto line = parent->add(
		object_ptr<Line>(parent, max),
		st::boxRowPadding);
}

} // namespace Premium
} // namespace Ui
