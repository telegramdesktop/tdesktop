/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/ripple_animation.h"

namespace Ui {

class RippleAnimation::Ripple {
public:
	Ripple(const style::RippleAnimation &st, QPoint origin, int startRadius, const QPixmap &mask, Fn<void()> update);
	Ripple(const style::RippleAnimation &st, const QPixmap &mask, Fn<void()> update);

	void paint(QPainter &p, const QPixmap &mask, TimeMs ms, const QColor *colorOverride);

	void stop();
	void unstop();
	void finish();
	void clearCache();
	bool finished() const {
		return _hiding && !_hide.animating();
	}

private:
	const style::RippleAnimation &_st;
	Fn<void()> _update;

	QPoint _origin;
	int _radiusFrom = 0;
	int _radiusTo = 0;

	bool _hiding = false;
	Animation _show;
	Animation _hide;
	QPixmap _cache;
	QImage _frame;

};

RippleAnimation::Ripple::Ripple(const style::RippleAnimation &st, QPoint origin, int startRadius, const QPixmap &mask, Fn<void()> update)
: _st(st)
, _update(update)
, _origin(origin)
, _radiusFrom(startRadius)
, _frame(mask.size(), QImage::Format_ARGB32_Premultiplied) {
	_frame.setDevicePixelRatio(mask.devicePixelRatio());

	QPoint points[] = {
		{ 0, 0 },
		{ _frame.width() / cIntRetinaFactor(), 0 },
		{ _frame.width() / cIntRetinaFactor(), _frame.height() / cIntRetinaFactor() },
		{ 0, _frame.height() / cIntRetinaFactor() },
	};
	for (auto point : points) {
		accumulate_max(_radiusTo, style::point::dotProduct(_origin - point, _origin - point));
	}
	_radiusTo = qRound(sqrt(_radiusTo));

	_show.start(_update, 0., 1., _st.showDuration, anim::easeOutQuint);
}

RippleAnimation::Ripple::Ripple(const style::RippleAnimation &st, const QPixmap &mask, Fn<void()> update)
: _st(st)
, _update(update)
, _origin(mask.width() / (2 * cIntRetinaFactor()), mask.height() / (2 * cIntRetinaFactor()))
, _radiusFrom(mask.width() + mask.height())
, _frame(mask.size(), QImage::Format_ARGB32_Premultiplied) {
	_frame.setDevicePixelRatio(mask.devicePixelRatio());
	_radiusTo = _radiusFrom;
	_hide.start(_update, 0., 1., _st.hideDuration);
}

void RippleAnimation::Ripple::paint(QPainter &p, const QPixmap &mask, TimeMs ms, const QColor *colorOverride) {
	auto opacity = _hide.current(ms, _hiding ? 0. : 1.);
	if (opacity == 0.) {
		return;
	}

	if (_cache.isNull() || colorOverride != nullptr) {
		auto radius = anim::interpolate(_radiusFrom, _radiusTo, _show.current(ms, 1.));
		_frame.fill(Qt::transparent);
		{
			Painter p(&_frame);
			p.setPen(Qt::NoPen);
			if (colorOverride) {
				p.setBrush(*colorOverride);
			} else {
				p.setBrush(_st.color);
			}
			{
				PainterHighQualityEnabler hq(p);
				p.drawEllipse(_origin, radius, radius);
			}
			p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
			p.drawPixmap(0, 0, mask);
		}
		if (radius == _radiusTo && colorOverride == nullptr) {
			_cache = App::pixmapFromImageInPlace(std::move(_frame));
		}
	}
	auto saved = p.opacity();
	if (opacity != 1.) p.setOpacity(saved * opacity);
	if (_cache.isNull()) {
		p.drawImage(0, 0, _frame);
	} else {
		p.drawPixmap(0, 0, _cache);
	}
	if (opacity != 1.) p.setOpacity(saved);
}

void RippleAnimation::Ripple::stop() {
	_hiding = true;
	_hide.start(_update, 1., 0., _st.hideDuration);
}

void RippleAnimation::Ripple::unstop() {
	if (_hiding) {
		if (_hide.animating()) {
			_hide.start(_update, 0., 1., _st.hideDuration);
		}
		_hiding = false;
	}
}

void RippleAnimation::Ripple::finish() {
	if (_update) {
		_update();
	}
	_show.finish();
	_hide.finish();
}

void RippleAnimation::Ripple::clearCache() {
	_cache = QPixmap();
}

RippleAnimation::RippleAnimation(const style::RippleAnimation &st, QImage mask, Fn<void()> callback)
: _st(st)
, _mask(App::pixmapFromImageInPlace(std::move(mask)))
, _update(callback) {
}


void RippleAnimation::add(QPoint origin, int startRadius) {
	lastStop();
	_ripples.push_back(std::make_unique<Ripple>(_st, origin, startRadius, _mask, _update));
}

void RippleAnimation::addFading() {
	lastStop();
	_ripples.push_back(std::make_unique<Ripple>(_st, _mask, _update));
}

void RippleAnimation::lastStop() {
	if (!_ripples.empty()) {
		_ripples.back()->stop();
	}
}

void RippleAnimation::lastUnstop() {
	if (!_ripples.empty()) {
		_ripples.back()->unstop();
	}
}

void RippleAnimation::lastFinish() {
	if (!_ripples.empty()) {
		_ripples.back()->finish();
	}
}

void RippleAnimation::forceRepaint() {
	for (const auto &ripple : _ripples) {
		ripple->clearCache();
	}
	if (_update) {
		_update();
	}
}

void RippleAnimation::paint(QPainter &p, int x, int y, int outerWidth, TimeMs ms, const QColor *colorOverride) {
	if (_ripples.empty()) {
		return;
	}

	if (rtl()) x = outerWidth - x - (_mask.width() / cIntRetinaFactor());
	p.translate(x, y);
	for (const auto &ripple : _ripples) {
		ripple->paint(p, _mask, ms, colorOverride);
	}
	p.translate(-x, -y);
	clearFinished();
}

QImage RippleAnimation::maskByDrawer(QSize size, bool filled, Fn<void(QPainter &p)> drawer) {
	auto result = QImage(size * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cRetinaFactor());
	result.fill(filled ? QColor(255, 255, 255) : Qt::transparent);
	if (drawer) {
		Painter p(&result);
		PainterHighQualityEnabler hq(p);

		p.setPen(Qt::NoPen);
		p.setBrush(QColor(255, 255, 255));
		drawer(p);
	}
	return result;
}

QImage RippleAnimation::rectMask(QSize size) {
	return maskByDrawer(size, true, Fn<void(QPainter&)>());
}

QImage RippleAnimation::roundRectMask(QSize size, int radius) {
	return maskByDrawer(size, false, [size, radius](QPainter &p) {
		p.drawRoundedRect(0, 0, size.width(), size.height(), radius, radius);
	});
}

QImage RippleAnimation::ellipseMask(QSize size) {
	return maskByDrawer(size, false, [size](QPainter &p) {
		p.drawEllipse(0, 0, size.width(), size.height());
	});
}

void RippleAnimation::clearFinished() {
	while (!_ripples.empty() && _ripples.front()->finished()) {
		_ripples.pop_front();
	}
}

void RippleAnimation::clear() {
	_ripples.clear();
}

RippleAnimation::~RippleAnimation() = default;

} // namespace Ui
