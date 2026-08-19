/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_compose_ai_button.h"

#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "styles/style_chat_helpers.h"

namespace HistoryView::Controls {
namespace {

constexpr auto kAnimationDuration = crl::time(640);

} // namespace

ComposeAiButton::ComposeAiButton(
	QWidget *parent,
	const style::IconButton &st)
: ComposeAiButton(
	parent,
	st,
	st::historyAiComposeButtonLetters,
	st::historyAiComposeButtonStar1,
	st::historyAiComposeButtonStar2,
	&st::historyComposeIconFgOver) {
}

ComposeAiButton::ComposeAiButton(
	QWidget *parent,
	const style::IconButton &st,
	const style::icon &letters,
	const style::icon &star1,
	const style::icon &star2,
	const style::color *overColor)
: RippleButton(parent, st.ripple)
, _st(st)
, _letters(letters)
, _star1(star1)
, _star2(star2)
, _overColor(overColor) {
	resize(_st.width, _st.height);
	setCursor(style::cur_pointer);

	shownValue(
	) | rpl::on_next([=](bool shown) {
		if (shown) {
			_animation.start([=] { update(); }, 0., 1., kAnimationDuration);
		} else {
			_animation.stop();
		}
	}, lifetime());
}

void ComposeAiButton::setPremiumStar(
		QImage image,
		QPoint position,
		int outline) {
	_premiumStar = std::move(image);
	_premiumStarPosition = position;
	_premiumStarOutline = outline;
	_frame = QImage();
	update();
}

void ComposeAiButton::paintEvent(QPaintEvent *e) {
	Painter p(this);
	PainterHighQualityEnabler hq(p);

	const auto over = isDown() || isOver() || forceRippled();
	paintRipple(p, _st.rippleAreaPosition);

	const auto progress = _animation.value(1.);
	auto star1Opacity = 1.;
	auto star2Opacity = 1.;
	if (progress < 0.25) {
		star1Opacity = 1. - (progress / 0.25);
	} else if (progress < 0.5) {
		star1Opacity = 0.;
		star2Opacity = 1. - ((progress - 0.25) / 0.25);
	} else if (progress < 0.75) {
		star1Opacity = (progress - 0.5) / 0.25;
		star2Opacity = 0.;
	} else {
		star2Opacity = (progress - 0.75) / 0.25;
	}

	if (_premiumStar.isNull()) {
		paintIcons(p, over, star1Opacity, star2Opacity);
		return;
	}
	validateFrame(over, star1Opacity, star2Opacity);
	p.drawImage(0, 0, _frame);
}

void ComposeAiButton::paintIcons(
		QPainter &p,
		bool over,
		float64 star1Opacity,
		float64 star2Opacity) {
	const auto part = [&](const style::icon &icon) {
		if (over && _overColor) {
			icon.paintInCenter(p, rect(), (*_overColor)->c);
		} else {
			icon.paintInCenter(p, rect());
		}
	};
	part(_letters);
	if (star1Opacity > 0.) {
		p.setOpacity(star1Opacity);
		part(_star1);
	}
	if (star2Opacity > 0.) {
		p.setOpacity(star2Opacity);
		part(_star2);
	}
}

void ComposeAiButton::validateFrame(
		bool over,
		float64 star1Opacity,
		float64 star2Opacity) {
	const auto ratio = style::DevicePixelRatio();
	if (!_frame.isNull()
		&& _frame.size() == size() * ratio
		&& _frameOver == over
		&& _frameStar1 == star1Opacity
		&& _frameStar2 == star2Opacity) {
		return;
	}
	_frameOver = over;
	_frameStar1 = star1Opacity;
	_frameStar2 = star2Opacity;
	if (_frame.size() != size() * ratio) {
		_frame = QImage(
			size() * ratio,
			QImage::Format_ARGB32_Premultiplied);
		_frame.setDevicePixelRatio(ratio);
	}
	_frame.fill(Qt::transparent);
	auto q = QPainter(&_frame);
	auto hq = PainterHighQualityEnabler(q);
	paintIcons(q, over, star1Opacity, star2Opacity);
	q.setOpacity(1.);
	const auto outline = _premiumStarOutline;
	q.setCompositionMode(QPainter::CompositionMode_DestinationOut);
	q.drawImage(_premiumStarPosition - QPoint(outline, 0), _premiumStar);
	q.drawImage(_premiumStarPosition + QPoint(outline, 0), _premiumStar);
	q.drawImage(_premiumStarPosition - QPoint(0, outline), _premiumStar);
	q.drawImage(_premiumStarPosition + QPoint(0, outline), _premiumStar);
	q.setCompositionMode(QPainter::CompositionMode_SourceOver);
	q.drawImage(_premiumStarPosition, _premiumStar);
}

void ComposeAiButton::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	update();
}

QImage ComposeAiButton::prepareRippleMask() const {
	return Ui::RippleAnimation::EllipseMask(
		QSize(_st.rippleAreaSize, _st.rippleAreaSize));
}

QPoint ComposeAiButton::prepareRippleStartPosition() const {
	const auto result = mapFromGlobal(QCursor::pos()) - _st.rippleAreaPosition;
	const auto rect = QRect(0, 0, _st.rippleAreaSize, _st.rippleAreaSize);
	return rect.contains(result)
		? result
		: DisabledRippleStartPosition();
}

} // namespace HistoryView::Controls
