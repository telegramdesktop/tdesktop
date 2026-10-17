/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_rate_call.h"

#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "styles/style_calls.h"

#include <QtGui/QPainterPathStroker>

namespace Calls {
namespace {

constexpr auto kBurstMinRating = 4;
constexpr auto kShowDuration = crl::time(250);
constexpr auto kStarShowDelay = crl::time(16);
constexpr auto kSelectDuration = crl::time(250);
constexpr auto kCardShowScaleFrom = 0.7;
constexpr auto kStarShowScaleFrom = 0.3;
constexpr auto kStarPressedScale = 0.8;
constexpr auto kDarkOpacity = 0.14;
constexpr auto kRippleOpacity = 0.3;
constexpr auto kPathSize = 96.;
constexpr auto kPathStroke = 6.;
constexpr auto kPathMiterLimit = 4.;
constexpr auto kCloseTextShadeOpacity = 0.15;
constexpr auto kCloseRippleOpacity = 0.12;
constexpr auto kMaxBursts = 5;

struct StarPoint {
	float64 x = 0.;
	float64 y = 0.;
	float64 inX = 0.;
	float64 inY = 0.;
	float64 outX = 0.;
	float64 outY = 0.;
};

const StarPoint kStarPath[] = {
	{ 46.080, 69.940, 0.855, -0.588, 0.000, 0.000 },
	{ 32.727, 79.118, 0.000, 0.000, -2.274, 1.563 },
	{ 25.770, 77.844, 1.568, 2.267, -0.860, -1.243 },
	{ 25.094, 73.591, -0.432, 1.447, 0.000, 0.000 },
	{ 29.725, 58.097, 0.000, 0.000, 0.296, -0.992 },
	{ 28.848, 55.405, 0.825, 0.629, 0.000, 0.000 },
	{ 15.963, 45.583, 0.000, 0.000, -2.194, -1.673 },
	{ 15.029, 38.595, -1.678, 2.187, 0.920, -1.199 },
	{ 18.879, 36.640, -1.514, 0.037, 0.000, 0.000 },
	{ 35.095, 36.242, 0.000, 0.000, 1.038, -0.025 },
	{ 37.392, 34.578, -0.345, 0.976, 0.000, 0.000 },
	{ 42.783, 19.330, 0.000, 0.000, 0.918, -2.597 },
	{ 49.162, 16.285, -2.605, -0.915, 1.429, 0.502 },
	{ 52.218, 19.330, -0.503, -1.424, 0.000, 0.000 },
	{ 57.608, 34.578, 0.000, 0.000, 0.345, 0.976 },
	{ 59.905, 36.242, -1.038, -0.025, 0.000, 0.000 },
	{ 76.121, 36.640, 0.000, 0.000, 2.762, 0.068 },
	{ 80.998, 41.746, 0.068, -2.752, -0.037, 1.509 },
	{ 79.037, 45.583, 1.203, -0.917, 0.000, 0.000 },
	{ 66.152, 55.405, 0.000, 0.000, -0.825, 0.629 },
	{ 65.275, 58.097, -0.296, -0.992, 0.000, 0.000 },
	{ 69.906, 73.591, 0.000, 0.000, 0.789, 2.639 },
	{ 66.541, 79.792, 2.647, -0.786, -1.452, 0.431 },
	{ 62.273, 79.118, 1.247, 0.857, 0.000, 0.000 },
	{ 48.920, 69.940, 0.000, 0.000, -0.855, -0.588 },
};

[[nodiscard]] QPainterPath StarPath(int size) {
	const auto scale = size / kPathSize;
	const auto map = [&](float64 x, float64 y) {
		return QPointF(x * scale, y * scale);
	};
	const auto count = int(std::size(kStarPath));
	auto result = QPainterPath();
	result.moveTo(map(kStarPath[0].x, kStarPath[0].y));
	for (auto i = 0; i != count; ++i) {
		const auto &from = kStarPath[i];
		const auto &to = kStarPath[(i + 1) % count];
		result.cubicTo(
			map(from.x + from.outX, from.y + from.outY),
			map(to.x + to.inX, to.y + to.inY),
			map(to.x, to.y));
	}
	result.closeSubpath();
	return result;
}

[[nodiscard]] QImage StarImage(int size, QColor color, bool filled) {
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		QSize(size, size) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);

	auto p = QPainter(&result);
	auto hq = PainterHighQualityEnabler(p);
	const auto path = StarPath(size);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	if (filled) {
		p.drawPath(path);
	} else {
		auto stroker = QPainterPathStroker();
		stroker.setWidth(kPathStroke * size / kPathSize);
		stroker.setJoinStyle(Qt::MiterJoin);
		stroker.setMiterLimit(kPathMiterLimit);
		p.drawPath(stroker.createStroke(path).intersected(path));
	}
	p.end();

	return result;
}

} // namespace

RateCall::RateCall(QWidget *parent)
: RpWidget(parent)
, _title(st::callRateTitle.minWidth)
, _description(st::callRateDescription.minWidth) {
	_title.setText(st::callRateTitle.style, tr::lng_call_rate_title(tr::now));
	_description.setText(
		st::callRateDescription.style,
		tr::lng_call_rate_description(tr::now));
	_cardOverlay = QColor(0, 0, 0, anim::interpolate(0, 255, kDarkOpacity));
	setMouseTracking(true);
	resize(st::callRateWidth, Height());
}

RateCall::~RateCall() = default;

int RateCall::Height() {
	return st::callRateStarsTop + st::callRateStarsHeight;
}

void RateCall::showAnimated() {
	_showAnimation.stop();
	_showAnimation = {};
	_showAnimation.start(
		[=] { update(); },
		0.,
		1.,
		kShowDuration + (kStarsCount - 1) * kStarShowDelay,
		anim::linear);
	update();
}

void RateCall::setCardOverlay(QColor color) {
	if (_cardOverlay == color) {
		return;
	}
	_cardOverlay = color;
	update();
}

void RateCall::setTextColorOverride(std::optional<QColor> color) {
	if (_textColorOverride == color) {
		return;
	}
	_textColorOverride = color;
	_starOutline = _starFilled = QImage();
	update();
}

int RateCall::rating() const {
	return _rating.current();
}

rpl::producer<int> RateCall::ratingValue() const {
	return _rating.value();
}

QColor RateCall::textColor() const {
	return _textColorOverride.value_or(st::callRateTitle.textFg->c);
}

QRect RateCall::cardRect() const {
	return QRect(
		(width() - st::callRateWidth) / 2,
		0,
		st::callRateWidth,
		st::callRateHeight);
}

QRect RateCall::starRect(int index) const {
	const auto single = st::callRateStarSize;
	const auto full = kStarsCount * single
		+ (kStarsCount - 1) * st::callRateStarSkip;
	const auto left = (width() - full) / 2
		+ index * (single + st::callRateStarSkip);
	return QRect(left, st::callRateStarsTop, single, single);
}

int RateCall::starByPosition(QPoint position) const {
	for (auto i = 0; i != kStarsCount; ++i) {
		if (starRect(i).contains(position)) {
			return i;
		}
	}
	return -1;
}

float64 RateCall::cardShown() const {
	const auto total = kShowDuration + (kStarsCount - 1) * kStarShowDelay;
	const auto passed = _showAnimation.value(1.) * total;
	return anim::sineInOut(1., std::clamp(passed / kShowDuration, 0., 1.));
}

float64 RateCall::starShown(int index) const {
	const auto total = kShowDuration + (kStarsCount - 1) * kStarShowDelay;
	const auto passed = _showAnimation.value(1.) * total
		- index * kStarShowDelay;
	return anim::sineInOut(1., std::clamp(passed / kShowDuration, 0., 1.));
}

const QImage &RateCall::starImage(bool filled) const {
	const auto color = textColor();
	if (_starColor != color) {
		_starColor = color;
		_starOutline = _starFilled = QImage();
	}
	auto &image = filled ? _starFilled : _starOutline;
	if (image.isNull()) {
		image = StarImage(st::callRateStarSize, color, filled);
	}
	return image;
}

void RateCall::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);

	paintCard(p, cardShown());
	for (auto i = 0; i != kStarsCount; ++i) {
		paintStar(p, i, starShown(i));
	}
	paintBurst(p);
}

void RateCall::paintCard(QPainter &p, float64 shown) {
	if (shown <= 0.) {
		return;
	}
	const auto card = cardRect();
	const auto scale = kCardShowScaleFrom
		+ (1. - kCardShowScaleFrom) * shown;

	p.save();
	p.setOpacity(shown);
	p.translate(0, anim::interpolate(st::callRateShowShift, 0, shown));
	p.translate(card.center());
	p.scale(scale, scale);
	p.translate(-card.center());

	p.setPen(Qt::NoPen);
	p.setBrush(_cardOverlay);
	p.drawRoundedRect(card, st::callRateRadius, st::callRateRadius);

	p.setPen(textColor());
	_title.draw(p, {
		.position = QPoint(card.x(), card.y() + st::callRateTitleTop),
		.outerWidth = width(),
		.availableWidth = card.width(),
		.align = style::al_top,
	});
	_description.draw(p, {
		.position = QPoint(card.x(), card.y() + st::callRateDescriptionTop),
		.outerWidth = width(),
		.availableWidth = card.width(),
		.align = style::al_top,
	});
	p.restore();
}

void RateCall::paintStar(QPainter &p, int index, float64 shown) {
	if (shown <= 0.) {
		return;
	}
	auto &star = _stars[index];
	const auto rect = starRect(index);
	if (star.ripple && !star.ripple->empty()) {
		auto color = textColor();
		color.setAlphaF(color.alphaF() * kRippleOpacity);
		p.setOpacity(shown);
		star.ripple->paint(p, rect.x(), rect.y(), width(), &color);
		p.setOpacity(1.);
	}
	const auto filled = star.filled.value(star.filledTo);
	const auto scale = star.pressed.value(star.pressedTo)
		* (kStarShowScaleFrom + (1. - kStarShowScaleFrom) * shown);

	p.save();
	p.translate(0, anim::interpolate(st::callRateStarShowShift, 0, shown));
	p.translate(rect.center());
	p.scale(scale, scale);
	p.translate(-rect.center());
	if (filled < 1.) {
		p.setOpacity(shown * (1. - filled));
		p.drawImage(rect.topLeft(), starImage(false));
	}
	if (filled > 0.) {
		p.setOpacity(shown * filled);
		p.drawImage(rect.topLeft(), starImage(true));
	}
	p.restore();
	p.setOpacity(1.);
}

void RateCall::paintBurst(QPainter &p) {
	const auto size = st::callRateBurstSize;
	for (const auto &burst : _bursts) {
		if (!burst.icon || !burst.icon->animating()) {
			continue;
		}
		burst.icon->paint(
			p,
			burst.center.x() - size / 2,
			burst.center.y() - size / 2);
	}
}

void RateCall::setOver(int index) {
	if (_over == index) {
		return;
	}
	_over = index;
	setCursor((_over >= 0) ? style::cur_pointer : style::cur_default);
}

void RateCall::applyPreview(int preview) {
	if (_preview == preview) {
		return;
	}
	_preview = preview;
	const auto shown = _preview ? _preview : _rating.current();
	for (auto i = 0; i != kStarsCount; ++i) {
		auto &star = _stars[i];
		const auto filled = (i < shown) ? 1. : 0.;
		const auto pressed = (_preview && (i < _preview))
			? kStarPressedScale
			: 1.;
		if (star.filledTo != filled) {
			star.filled.start(
				[=] { update(); },
				star.filled.value(star.filledTo),
				filled,
				kSelectDuration);
			star.filledTo = filled;
		}
		if (star.pressedTo != pressed) {
			star.pressed.start(
				[=] { update(); },
				star.pressed.value(star.pressedTo),
				pressed,
				kSelectDuration);
			star.pressedTo = pressed;
		}
	}
	update();
}

void RateCall::commit(int index) {
	_rating = index + 1;
	applyPreview(0);
	if (_rating.current() >= kBurstMinRating) {
		startBurst(index);
	}
}

void RateCall::startBurst(int index) {
	const auto free = ranges::find_if(_bursts, [](const Burst &burst) {
		return !burst.icon || !burst.icon->animating();
	});
	const auto burst = (free != end(_bursts))
		? &*free
		: (int(_bursts.size()) < kMaxBursts)
		? &_bursts.emplace_back()
		: &*ranges::min_element(_bursts, ranges::less(), &Burst::started);
	if (!burst->icon) {
		burst->icon = Lottie::MakeIcon({
			.name = u"call_rate"_q,
			.sizeOverride = QSize(
				st::callRateBurstSize,
				st::callRateBurstSize),
		});
		if (!burst->icon->valid()) {
			burst->icon = nullptr;
			return;
		}
	}
	burst->center = starRect(index).center();
	burst->started = crl::now();
	const auto last = burst->icon->framesCount() - 1;
	burst->icon->animate([=] { update(); }, 0, last);
	update();
}

void RateCall::mouseMoveEvent(QMouseEvent *e) {
	setOver(starByPosition(e->pos()));
}

void RateCall::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	setOver(starByPosition(e->pos()));
	if (_over < 0) {
		return;
	}
	_pressed = _over;
	auto &star = _stars[_pressed];
	const auto rect = starRect(_pressed);
	if (!star.ripple) {
		star.ripple = std::make_unique<Ui::RippleAnimation>(
			st::callRateStarRipple,
			Ui::RippleAnimation::EllipseMask(rect.size()),
			[=] { update(); });
	}
	star.ripple->add(e->pos() - rect.topLeft());
	applyPreview(_pressed + 1);
}

void RateCall::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	const auto pressed = std::exchange(_pressed, -1);
	if (pressed < 0) {
		return;
	} else if (const auto &ripple = _stars[pressed].ripple) {
		ripple->lastStop();
	}
	setOver(starByPosition(e->pos()));
	if (_over == pressed) {
		commit(pressed);
	} else {
		applyPreview(0);
	}
}

void RateCall::leaveEventHook(QEvent *e) {
	setOver(-1);
}

EndCloseButton::EndCloseButton(QWidget *parent)
: RippleButton(parent, st::callRateCloseRipple)
, _text(tr::lng_close(tr::now)) {
	setDisabled(true);
	hide();
}

void EndCloseButton::switchToClose(QRect hangupCircle, Fn<void()> close) {
	_from = hangupCircle;
	setClickedCallback(std::move(close));
	_animation.start([=] { update(); }, 0., 1., st::callPanelDuration);
	_animation.setFinishedCallback([=] { setDisabled(false); });
	show();
	update();
}

void EndCloseButton::prepareFrame() {
	const auto ratio = style::DevicePixelRatio();
	if (_frame.size() != size() * ratio) {
		_frame = QImage(size() * ratio, QImage::Format_ARGB32_Premultiplied);
		_frame.setDevicePixelRatio(ratio);
	}
	_frame.fill(Qt::transparent);

	auto p = QPainter(&_frame);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::callIconBgActive);
	p.drawRoundedRect(
		rect(),
		st::callRateCloseRadius,
		st::callRateCloseRadius);

	auto ripple = st::callIconFgActive->c;
	ripple.setAlphaF(ripple.alphaF() * kCloseRippleOpacity);
	paintRipple(p, 0, 0, &ripple);

	p.setFont(st::callRateCloseFont);
	p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
	p.setPen(QColor(0, 0, 0, 255));
	p.drawText(rect(), Qt::AlignCenter, _text);
	p.setCompositionMode(QPainter::CompositionMode_SourceOver);
	auto shade = st::callIconFgActive->c;
	shade.setAlphaF(shade.alphaF() * kCloseTextShadeOpacity);
	p.setPen(shade);
	p.drawText(rect(), Qt::AlignCenter, _text);
}

void EndCloseButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto shown = _animation.value(1.);
	if (shown < 1.) {
		auto hq = PainterHighQualityEnabler(p);
		const auto circle = _from.translated(-pos());
		p.setOpacity(1. - shown);
		p.setPen(Qt::NoPen);
		p.setBrush(st::callHangupBg);
		p.drawEllipse(circle);
		st::callHangup.button.icon.paintInCenter(
			p,
			circle,
			st::callIconFg->c);
	}

	prepareFrame();
	p.setOpacity(shown);
	p.drawImage(0, 0, _frame);
}

QImage EndCloseButton::prepareRippleMask() const {
	return Ui::RippleAnimation::RoundRectMask(
		size(),
		st::callRateCloseRadius);
}

} // namespace Calls
