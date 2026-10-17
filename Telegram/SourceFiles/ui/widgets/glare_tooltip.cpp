/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/glare_tooltip.h"

#include "base/event_filter.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"

#include "styles/style_widgets.h"

namespace Ui {
namespace {

constexpr auto kGlareDurationStep = crl::time(320);
constexpr auto kGlareTimeout = crl::time(1000);

} // namespace

GlareTooltip::GlareTooltip(
	not_null<QWidget*> parent,
	const style::ImportantTooltip &st,
	const style::font &font,
	const QString &text,
	GlareTooltipColors colors)
: RpWidget(parent)
, _st(st)
, _text(text)
, _font(font)
, _colors(std::move(colors))
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
}

void GlareTooltip::fade(bool shown) {
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

void GlareTooltip::showGlare() {
	_glareAnimation.start([=] {
		update();
		if (!_glareAnimation.animating()) {
			_glareTimer.callOnce(kGlareTimeout);
		}
	}, 0., 1., _glareDuration);
}

void GlareTooltip::stopGlare() {
	_glareTimer.cancel();
	_glareAnimation.stop();
}

void GlareTooltip::finishAnimating() {
	_showAnimation.stop();
	if (!_shown) {
		hide();
	}
}

void GlareTooltip::setOpacity(float64 opacity) {
	_opacity = opacity;
	update();
}

void GlareTooltip::setColors(GlareTooltipColors colors) {
	_colors = std::move(colors);
	_image = QImage();
	update();
}

crl::time GlareTooltip::glarePeriod() const {
	return _glareDuration + kGlareTimeout;
}

crl::time GlareTooltip::glaresDuration(int glares) const {
	return glares * glarePeriod() - (_st.duration * 3) / 2;
}

void GlareTooltip::paintEvent(QPaintEvent *e) {
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

void GlareTooltip::trackWidget(not_null<QWidget*> pointTo) {
	_trackLifetime.destroy();

	auto widget = pointTo.get();
	const auto parent = parentWidget();

	const auto refresh = [=, weak = base::make_weak(pointTo)] {
		const auto strong = weak.get();
		if (!strong) {
			hide();
			return setGeometry({});
		}
		pointAt(
			MapFrom(parent, pointTo, pointTo->rect()),
			parent->rect());
	};
	refresh();
	while (widget && widget != parent) {
		base::install_event_filter(widget, [=](not_null<QEvent*> e) {
			if (e->type() == QEvent::Resize
				|| e->type() == QEvent::Move
				|| e->type() == QEvent::ZOrderChange) {
				refresh();
				raise();
			}
			return base::EventFilterResult::Continue;
		}, _trackLifetime);
		widget = widget->parentWidget();
	}
}

void GlareTooltip::pointAt(QRect area, QRect within) {
	const auto point = QPoint(area.center().x(), area.y());
	const auto skip = _st.padding.left();
	setGeometry(
		std::min(
			std::max(point.x() - (width() / 2), within.x() + skip),
			within.x() + within.width() - width() - skip),
		std::max(
			point.y() - height() - _st.margin.bottom(),
			within.y() + skip),
		width(),
		height());
	const auto arrowMiddle = point.x() - x();
	if (_arrowMiddle != arrowMiddle) {
		_arrowMiddle = arrowMiddle;
		update();
	}
}

void GlareTooltip::prepareImage() {
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
			{ 0., _colors.edge },
			{ 0.5, _colors.center },
			{ 1., _colors.edge },
		});
		p.setBrush(gradient);
	} else {
		p.setBrush(_colors.edge);
	}
	p.translate(_skip, _skip);
	p.drawPath(path);
	p.setCompositionMode(QPainter::CompositionMode_Source);
	p.setBrush(Qt::NoBrush);
	auto copy = _colors.rim;
	copy.setAlpha(0);
	if (gtill > 0) {
		auto gradient = QLinearGradient(gfrom, 0, gtill, 0);
		gradient.setStops({
			{ 0., copy },
			{ 0.5, _colors.rim },
			{ 1., copy },
		});
		p.setPen(QPen(gradient, _stroke));
	} else {
		p.setPen(QPen(copy, _stroke));
	}
	p.drawPath(path);
	p.setCompositionMode(QPainter::CompositionMode_SourceOver);
	p.setFont(_font);
	p.setPen(_colors.text);
	p.drawText(_st.padding.left(), _st.padding.top() + _font->ascent, _text);
}

} // namespace Ui
