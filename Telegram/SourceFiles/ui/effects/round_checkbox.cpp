/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "ui/effects/round_checkbox.h"

namespace Ui {
namespace {

static constexpr int kWideScale = 3;

void prepareCheckCaches(const style::RoundCheckbox *st, bool displayInactive, QPixmap &checkBgCache, QPixmap &checkFullCache) {
	auto size = st->size;
	auto wideSize = size * kWideScale;
	auto cache = QImage(wideSize * cIntRetinaFactor(), wideSize * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	cache.setDevicePixelRatio(cRetinaFactor());
	cache.fill(Qt::transparent);
	{
		Painter p(&cache);
		PainterHighQualityEnabler hq(p);

		if (displayInactive) {
			p.setPen(Qt::NoPen);
		} else {
			auto pen = st->border->p;
			pen.setWidth(st->width);
			p.setPen(pen);
		}
		p.setBrush(st->bgActive);
		auto ellipse = QRect((wideSize - size) / 2, (wideSize - size) / 2, size, size);
		p.drawEllipse(ellipse);
	}
	auto cacheIcon = cache;
	{
		Painter p(&cacheIcon);
		auto ellipse = QRect((wideSize - size) / 2, (wideSize - size) / 2, size, size);
		st->check.paint(p, ellipse.topLeft(), wideSize);
	}
	checkBgCache = App::pixmapFromImageInPlace(std_::move(cache));
	checkFullCache = App::pixmapFromImageInPlace(std_::move(cacheIcon));
}

} // namespace

RoundCheckbox::RoundCheckbox(const style::RoundCheckbox &st, const base::lambda_copy<void()> &updateCallback)
: _st(st)
, _updateCallback(updateCallback) {
}

QRect RoundCheckbox::cacheDestRect(int x, int y, float64 scale) const {
	auto iconSizeFull = kWideScale * _st.size;
	auto iconSize = qRound(iconSizeFull * scale);
	if (iconSize % 2 != iconSizeFull % 2) {
		++iconSize;
	}
	auto iconShift = (iconSizeFull - iconSize) / 2;
	auto iconLeft = x - (kWideScale - 1) * _st.size / 2 + iconShift;
	auto iconTop = y - (kWideScale - 1) * _st.size / 2 + iconShift;
	return QRect(iconLeft, iconTop, iconSize, iconSize);
}

void RoundCheckbox::paint(Painter &p, TimeMs ms, int x, int y, int outerWidth, float64 masterScale) {
	for (auto &icon : _icons) {
		icon.fadeIn.step(ms);
		icon.fadeOut.step(ms);
	}
	removeFadeOutedIcons();

	auto cacheSize = kWideScale * _st.size * cIntRetinaFactor();
	auto cacheFrom = QRect(0, 0, cacheSize, cacheSize);
	auto displayInactive = !_inactiveCacheBg.isNull();
	auto inactiveTo = cacheDestRect(x, y, masterScale);

	PainterHighQualityEnabler hq(p);
	if (!_inactiveCacheBg.isNull()) {
		p.drawPixmap(inactiveTo, _inactiveCacheBg, cacheFrom);
	}
	for (auto &icon : _icons) {
		auto fadeIn = icon.fadeIn.current(1.);
		auto fadeOut = icon.fadeOut.current(1.);
		auto to = cacheDestRect(x, y, (1. - (1. - _st.sizeSmall) * (1. - fadeOut)) * masterScale);
		p.setOpacity(fadeIn * fadeOut);
		if (fadeOut < 1.) {
			p.drawPixmapLeft(to, outerWidth, icon.wideCheckCache, cacheFrom);
		} else if (fadeIn == 1.) {
			p.drawPixmapLeft(to, outerWidth, _wideCheckFullCache, cacheFrom);
		} else {
			auto realDivider = ((kWideScale - 1) * _st.size / 2 + qMax(fadeIn - 0.5, 0.) * 2. * _st.size);
			auto divider = qRound(realDivider * masterScale);
			auto cacheDivider = qRound(realDivider) * cIntRetinaFactor();
			p.drawPixmapLeft(QRect(to.x(), to.y(), divider, to.height()), outerWidth, _wideCheckFullCache, QRect(0, 0, cacheDivider, cacheFrom.height()));
			p.drawPixmapLeft(QRect(to.x() + divider, to.y(), to.width() - divider, to.height()), outerWidth, _wideCheckBgCache, QRect(cacheDivider, 0, cacheFrom.width() - cacheDivider, _wideCheckBgCache.height()));
		}
	}
	p.setOpacity(1.);
	if (!_inactiveCacheFg.isNull()) {
		p.drawPixmap(inactiveTo, _inactiveCacheFg, cacheFrom);
	}
}

void RoundCheckbox::setChecked(bool newChecked, SetStyle speed) {
	if (_checked == newChecked) {
		if (speed != SetStyle::Animated && !_icons.isEmpty()) {
			_icons.back().fadeIn.finish();
			_icons.back().fadeOut.finish();
		}
		return;
	}
	_checked = newChecked;
	if (_checked) {
		if (_wideCheckBgCache.isNull()) {
			prepareCheckCaches(&_st, _displayInactive, _wideCheckBgCache, _wideCheckFullCache);
		}
		_icons.push_back(Icon());
		_icons.back().fadeIn.start(_updateCallback, 0, 1, _st.duration);
		if (speed != SetStyle::Animated) {
			_icons.back().fadeIn.finish();
		}
	} else {
		if (speed == SetStyle::Animated) {
			prepareWideCheckIconCache(&_icons.back());
		}
		_icons.back().fadeOut.start(_updateCallback, 1, 0, _st.duration);
		if (speed != SetStyle::Animated) {
			_icons.back().fadeOut.finish();
		}
	}
}

void RoundCheckbox::invalidateCache() {
	if (!_wideCheckBgCache.isNull() || !_wideCheckFullCache.isNull()) {
		prepareCheckCaches(&_st, _displayInactive, _wideCheckBgCache, _wideCheckFullCache);
	}
	if (!_inactiveCacheBg.isNull() || !_inactiveCacheFg.isNull()) {
		prepareInactiveCache();
	}
}

void RoundCheckbox::setDisplayInactive(bool displayInactive) {
	if (_displayInactive != displayInactive) {
		_displayInactive = displayInactive;
		if (_displayInactive) {
			prepareInactiveCache();
		} else {
			_inactiveCacheBg = _inactiveCacheFg = QPixmap();
		}
		if (!_wideCheckBgCache.isNull()) {
			prepareCheckCaches(&_st, _displayInactive, _wideCheckBgCache, _wideCheckFullCache);
		}
		for (auto &icon : _icons) {
			if (!icon.wideCheckCache.isNull()) {
				prepareWideCheckIconCache(&icon);
			}
		}
	}
}

void RoundCheckbox::removeFadeOutedIcons() {
	while (!_icons.empty() && !_icons.front().fadeIn.animating() && !_icons.front().fadeOut.animating()) {
		if (_icons.size() > 1 || !_checked) {
			_icons.erase(_icons.begin());
		} else {
			break;
		}
	}
}

void RoundCheckbox::prepareWideCheckIconCache(Icon *icon) {
	auto cacheWidth = _wideCheckBgCache.width() / _wideCheckBgCache.devicePixelRatio();
	auto cacheHeight = _wideCheckBgCache.height() / _wideCheckBgCache.devicePixelRatio();
	auto wideCache = QImage(cacheWidth * cIntRetinaFactor(), cacheHeight * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	wideCache.setDevicePixelRatio(cRetinaFactor());
	{
		Painter p(&wideCache);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		auto iconSize = kWideScale * _st.size;
		auto realDivider = ((kWideScale - 1) * _st.size / 2 + qMax(icon->fadeIn.current(1.) - 0.5, 0.) * 2. * _st.size);
		auto divider = qRound(realDivider);
		auto cacheDivider = qRound(realDivider) * cIntRetinaFactor();
		p.drawPixmapLeft(QRect(0, 0, divider, iconSize), cacheWidth, _wideCheckFullCache, QRect(0, 0, divider * cIntRetinaFactor(), _wideCheckFullCache.height()));
		p.drawPixmapLeft(QRect(divider, 0, iconSize - divider, iconSize), cacheWidth, _wideCheckBgCache, QRect(cacheDivider, 0, _wideCheckBgCache.width() - cacheDivider, _wideCheckBgCache.height()));
	}
	icon->wideCheckCache = App::pixmapFromImageInPlace(std_::move(wideCache));
	icon->wideCheckCache.setDevicePixelRatio(cRetinaFactor());
}

void RoundCheckbox::prepareInactiveCache() {
	auto wideSize = _st.size * kWideScale;
	auto ellipse = QRect((wideSize - _st.size) / 2, (wideSize - _st.size) / 2, _st.size, _st.size);

	auto cacheBg = QImage(wideSize * cIntRetinaFactor(), wideSize * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	cacheBg.setDevicePixelRatio(cRetinaFactor());
	cacheBg.fill(Qt::transparent);
	auto cacheFg = cacheBg;
	if (_st.bgInactive) {
		Painter p(&cacheBg);
		PainterHighQualityEnabler hq(p);

		p.setPen(Qt::NoPen);
		p.setBrush(_st.bgInactive);
		p.drawEllipse(ellipse);
	}
	_inactiveCacheBg = App::pixmapFromImageInPlace(std_::move(cacheBg));

	{
		Painter p(&cacheFg);
		PainterHighQualityEnabler hq(p);

		auto pen = _st.border->p;
		pen.setWidth(_st.width);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(ellipse);
	}
	_inactiveCacheFg = App::pixmapFromImageInPlace(std_::move(cacheFg));
}

RoundImageCheckbox::RoundImageCheckbox(const style::RoundImageCheckbox &st, const base::lambda_copy<void()> &updateCallback, PaintRoundImage &&paintRoundImage)
: _st(st)
, _updateCallback(updateCallback)
, _paintRoundImage(std_::move(paintRoundImage))
, _check(_st.check, _updateCallback) {
}

void RoundImageCheckbox::paint(Painter &p, TimeMs ms, int x, int y, int outerWidth) {
	_selection.step(ms);

	auto selectionLevel = _selection.current(checked() ? 1. : 0.);
	if (_selection.animating()) {
		auto userpicRadius = qRound(kWideScale * (_st.imageRadius + (_st.imageSmallRadius - _st.imageRadius) * selectionLevel));
		auto userpicShift = kWideScale * _st.imageRadius - userpicRadius;
		auto userpicLeft = x - (kWideScale - 1) * _st.imageRadius + userpicShift;
		auto userpicTop = y - (kWideScale - 1) * _st.imageRadius + userpicShift;
		auto to = QRect(userpicLeft, userpicTop, userpicRadius * 2, userpicRadius * 2);
		auto from = QRect(QPoint(0, 0), _wideCache.size());

		PainterHighQualityEnabler hq(p);
		p.drawPixmapLeft(to, outerWidth, _wideCache, from);
	} else {
		if (!_wideCache.isNull()) {
			_wideCache = QPixmap();
		}
		auto userpicRadius = checked() ? _st.imageSmallRadius : _st.imageRadius;
		auto userpicShift = _st.imageRadius - userpicRadius;
		auto userpicLeft = x + userpicShift;
		auto userpicTop = y + userpicShift;
		_paintRoundImage(p, userpicLeft, userpicTop, outerWidth, userpicRadius * 2);
	}

	if (selectionLevel > 0) {
		PainterHighQualityEnabler hq(p);
		p.setOpacity(snap(selectionLevel, 0., 1.));
		p.setBrush(Qt::NoBrush);
		auto pen = _st.selectFg->p;
		pen.setWidth(_st.selectWidth);
		p.setPen(pen);
		p.drawEllipse(rtlrect(x, y, _st.imageRadius * 2, _st.imageRadius * 2, outerWidth));
		p.setOpacity(1.);
	}

	auto iconLeft = x + 2 * _st.imageRadius + _st.selectWidth - _st.check.size;
	auto iconTop = y + 2 * _st.imageRadius + _st.selectWidth - _st.check.size;
	_check.paint(p, ms, iconLeft, iconTop, outerWidth);
}

float64 RoundImageCheckbox::checkedAnimationRatio() const {
	return snap(_selection.current(checked() ? 1. : 0.), 0., 1.);
}

void RoundImageCheckbox::setChecked(bool newChecked, SetStyle speed) {
	auto changed = (checked() != newChecked);
	_check.setChecked(newChecked, speed);
	if (!changed) {
		if (speed != SetStyle::Animated) {
			_selection.finish();
		}
		return;
	}
	if (speed == SetStyle::Animated) {
		prepareWideCache();
		_selection.start(_updateCallback, checked() ? 0 : 1, checked() ? 1 : 0, _st.selectDuration, anim::bumpy(1.25));
	} else {
		_selection.finish();
	}
}

void RoundImageCheckbox::prepareWideCache() {
	if (_wideCache.isNull()) {
		auto size = _st.imageRadius * 2;
		auto wideSize = size * kWideScale;
		QImage cache(wideSize * cIntRetinaFactor(), wideSize * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
		cache.setDevicePixelRatio(cRetinaFactor());
		{
			Painter p(&cache);
			p.setCompositionMode(QPainter::CompositionMode_Source);
			p.fillRect(0, 0, wideSize, wideSize, Qt::transparent);
			p.setCompositionMode(QPainter::CompositionMode_SourceOver);
			_paintRoundImage(p, (wideSize - size) / 2, (wideSize - size) / 2, wideSize, size);
		}
		_wideCache = App::pixmapFromImageInPlace(std_::move(cache));
	}
}

} // namespace Ui
