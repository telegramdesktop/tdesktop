/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_badge_tooltip.h"

#include "data/data_emoji_statuses.h"
#include "base/event_filter.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "styles/style_info.h"

namespace Info::Profile {
namespace {

constexpr auto kGlareDurationStep = crl::time(320);
constexpr auto kGlareTimeout = crl::time(1000);

} // namespace

BadgeTooltip::BadgeTooltip(
	not_null<QWidget*> parent,
	std::shared_ptr<Data::EmojiStatusCollectible> collectible,
	not_null<QWidget*> pointTo)
: Ui::RpWidget(parent)
, _st(st::infoGiftTooltip)
, _collectible(std::move(collectible))
, _text(_collectible->title)
, _font(st::infoGiftTooltipFont)
, _inner(_font->width(_text), _font->height)
, _outer(_inner.grownBy(_st.padding))
, _stroke(st::lineWidth)
, _skip(2 * _stroke)
, _full(_outer + QSize(2 * _skip, _st.arrow + 2 * _skip))
, _glareSize(_outer.height() * 3)
, _glareRange(_outer.width() + _glareSize)
, _glareDuration(_glareRange * kGlareDurationStep / _glareSize)
, _glareTimer([=] { showGlare(); }) {
	resize(_full + QSize(0, _st.shift));
	setupGeometry(pointTo);
}

void BadgeTooltip::fade(bool shown) {
	if (_shown == shown) {
		return;
	}
	show();
	_shown = shown;
	_showAnimation.start([=] {
		update();
		if (!_showAnimation.animating()) {
			if (!_shown) {
				hide();
			} else {
				showGlare();
			}
		}
	}, _shown ? 0. : 1., _shown ? 1. : 0., _st.duration, anim::easeInCirc);
}

void BadgeTooltip::showGlare() {
	_glareAnimation.start([=] {
		update();
		if (!_glareAnimation.animating()) {
			_glareTimer.callOnce(kGlareTimeout);
		}
	}, 0., 1., _glareDuration);
}

void BadgeTooltip::finishAnimating() {
	_showAnimation.stop();
	if (!_shown) {
		hide();
	}
}

void BadgeTooltip::setOpacity(float64 opacity) {
	_opacity = opacity;
	update();
}

crl::time BadgeTooltip::glarePeriod() const {
	return _glareDuration + kGlareTimeout;
}

void BadgeTooltip::paintEvent(QPaintEvent *e) {
	const auto glare = _glareAnimation.value(0.);
	_glareRight = anim::interpolate(0, _glareRange, glare);
	prepareImage();

	auto p = QPainter(this);
	const auto shown = _showAnimation.value(_shown ? 1. : 0.);
	p.setOpacity(shown * _opacity);
	const auto imageHeight = _image.height() / _image.devicePixelRatio();
	const auto top = anim::interpolate(0, height() - imageHeight, shown);
	p.drawImage(0, top, _image);
}

void BadgeTooltip::setupGeometry(not_null<QWidget*> pointTo) {
	auto widget = pointTo.get();
	const auto parent = parentWidget();

	const auto refresh = [=, weak = base::make_weak(pointTo)] {
		const auto strong = weak.get();
		if (!strong) {
			hide();
			return setGeometry({});
		}
		const auto rect = Ui::MapFrom(parent, pointTo, pointTo->rect());
		const auto point = QPoint(rect.center().x(), rect.y());
		const auto left = point.x() - (width() / 2);
		const auto skip = _st.padding.left();
		setGeometry(
			std::min(std::max(left, skip), parent->width() - width() - skip),
			std::max(point.y() - height() - _st.margin.bottom(), skip),
			width(),
			height());
		const auto arrowMiddle = point.x() - x();
		if (_arrowMiddle != arrowMiddle) {
			_arrowMiddle = arrowMiddle;
			update();
		}
	};
	refresh();
	while (widget && widget != parent) {
		base::install_event_filter(this, widget, [=](not_null<QEvent*> e) {
			if (e->type() == QEvent::Resize
				|| e->type() == QEvent::Move
				|| e->type() == QEvent::ZOrderChange) {
				refresh();
				raise();
			}
			return base::EventFilterResult::Continue;
		});
		widget = widget->parentWidget();
	}
}

void BadgeTooltip::prepareImage() {
	const auto ratio = style::DevicePixelRatio();
	const auto arrow = _st.arrow;
	const auto size = _full * ratio;
	if (_image.size() != size) {
		_image = QImage(size, QImage::Format_ARGB32_Premultiplied);
		_image.setDevicePixelRatio(ratio);
	} else if (_imageGlareRight == _glareRight
		&& _imageArrowMiddle == _arrowMiddle) {
		return;
	}
	_imageGlareRight = _glareRight;
	_imageArrowMiddle = _arrowMiddle;
	_image.fill(Qt::transparent);

	const auto gfrom = _imageGlareRight - _glareSize;
	const auto gtill = _imageGlareRight;

	auto path = QPainterPath();
	const auto width = _outer.width();
	const auto height = _outer.height();
	const auto radius = (height + 1) / 2;
	const auto diameter = height;
	path.moveTo(radius, 0);
	path.lineTo(width - radius, 0);
	path.arcTo(
		QRect(QPoint(width - diameter, 0), QSize(diameter, diameter)),
		90,
		-180);
	const auto xarrow = _arrowMiddle - _skip;
	if (xarrow - arrow <= radius || xarrow + arrow >= width - radius) {
		path.lineTo(radius, height);
	} else {
		path.lineTo(xarrow + arrow, height);
		path.lineTo(xarrow, height + arrow);
		path.lineTo(xarrow - arrow, height);
		path.lineTo(radius, height);
	}
	path.arcTo(
		QRect(QPoint(0, 0), QSize(diameter, diameter)),
		-90,
		-180);
	path.closeSubpath();

	auto p = QPainter(&_image);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	if (gtill > 0) {
		auto gradient = QLinearGradient(gfrom, 0, gtill, 0);
		gradient.setStops({
			{ 0., _collectible->edgeColor },
			{ 0.5, _collectible->centerColor },
			{ 1., _collectible->edgeColor },
		});
		p.setBrush(gradient);
	} else {
		p.setBrush(_collectible->edgeColor);
	}
	p.translate(_skip, _skip);
	p.drawPath(path);
	p.setCompositionMode(QPainter::CompositionMode_Source);
	p.setBrush(Qt::NoBrush);
	auto copy = _collectible->textColor;
	copy.setAlpha(0);
	if (gtill > 0) {
		auto gradient = QLinearGradient(gfrom, 0, gtill, 0);
		gradient.setStops({
			{ 0., copy },
			{ 0.5, _collectible->textColor },
			{ 1., copy },
		});
		p.setPen(QPen(gradient, _stroke));
	} else {
		p.setPen(QPen(copy, _stroke));
	}
	p.drawPath(path);
	p.setCompositionMode(QPainter::CompositionMode_SourceOver);
	p.setFont(_font);
	p.setPen(QColor(255, 255, 255));
	p.drawText(_st.padding.left(), _st.padding.top() + _font->ascent, _text);
}

} // namespace Info::Profile
