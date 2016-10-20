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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "ui/effects/round_image_checkbox.h"

namespace Ui {
namespace {

void prepareCheckCaches(const style::RoundImageCheckbox *st, QPixmap &checkBgCache, QPixmap &checkFullCache) {
	auto size = st->checkRadius * 2;
	auto wideSize = size * kWideRoundImageCheckboxScale;
	QImage cache(wideSize * cIntRetinaFactor(), wideSize * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	cache.setDevicePixelRatio(cRetinaFactor());
	{
		Painter p(&cache);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.fillRect(0, 0, wideSize, wideSize, Qt::transparent);
		p.setCompositionMode(QPainter::CompositionMode_SourceOver);
		p.setRenderHint(QPainter::HighQualityAntialiasing, true);
		auto pen = st->checkBorder->p;
		pen.setWidth(st->selectWidth);
		p.setPen(pen);
		p.setBrush(st->checkBg);
		auto ellipse = QRect((wideSize - size) / 2, (wideSize - size) / 2, size, size);
		p.drawEllipse(ellipse);
	}
	QImage cacheIcon = cache;
	{
		Painter p(&cacheIcon);
		auto ellipse = QRect((wideSize - size) / 2, (wideSize - size) / 2, size, size);
		st->checkIcon.paint(p, ellipse.topLeft(), wideSize);
	}
	checkBgCache = App::pixmapFromImageInPlace(std_::move(cache));
	checkBgCache.setDevicePixelRatio(cRetinaFactor());
	checkFullCache = App::pixmapFromImageInPlace(std_::move(cacheIcon));
	checkFullCache.setDevicePixelRatio(cRetinaFactor());
}

struct AnimBumpy {
	AnimBumpy(float64 bump) : bump(bump)
		, dt0(bump - sqrt(bump * (bump - 1.)))
		, k(1 / (2 * dt0 - 1)) {
	}
	float64 bump;
	float64 dt0;
	float64 k;
};

template <int BumpRatioPercent>
float64 anim_bumpy(const float64 &delta, const float64 &dt) {
	static AnimBumpy data = { BumpRatioPercent / 100. };
	return delta * (data.bump - data.k * (dt - data.dt0) * (dt - data.dt0));
}

} // namespace

RoundImageCheckbox::RoundImageCheckbox(const style::RoundImageCheckbox &st, UpdateCallback &&updateCallback, PaintRoundImage &&paintRoundImage)
: _st(st)
, _updateCallback(std_::move(updateCallback))
, _paintRoundImage(std_::move(paintRoundImage)) {
	prepareCheckCaches(&_st, _wideCheckBgCache, _wideCheckFullCache);
}

void RoundImageCheckbox::paint(Painter &p, int x, int y, int outerWidth) {
	auto selectionLevel = _selection.current(_selected ? 1. : 0.);
	if (_selection.animating()) {
		p.setRenderHint(QPainter::SmoothPixmapTransform, true);
		auto userpicRadius = qRound(kWideRoundImageCheckboxScale * (_st.imageRadius + (_st.imageSmallRadius - _st.imageRadius) * selectionLevel));
		auto userpicShift = kWideRoundImageCheckboxScale * _st.imageRadius - userpicRadius;
		auto userpicLeft = x - (kWideRoundImageCheckboxScale - 1) * _st.imageRadius + userpicShift;
		auto userpicTop = y - (kWideRoundImageCheckboxScale - 1) * _st.imageRadius + userpicShift;
		auto to = QRect(userpicLeft, userpicTop, userpicRadius * 2, userpicRadius * 2);
		auto from = QRect(QPoint(0, 0), _wideCache.size());
		p.drawPixmapLeft(to, outerWidth, _wideCache, from);
		p.setRenderHint(QPainter::SmoothPixmapTransform, false);
	} else {
		if (!_wideCache.isNull()) {
			_wideCache = QPixmap();
		}
		auto userpicRadius = _selected ? _st.imageSmallRadius : _st.imageRadius;
		auto userpicShift = _st.imageRadius - userpicRadius;
		auto userpicLeft = x + userpicShift;
		auto userpicTop = y + userpicShift;
		_paintRoundImage(p, userpicLeft, userpicTop, outerWidth, userpicRadius * 2);
	}

	if (selectionLevel > 0) {
		p.setRenderHint(QPainter::HighQualityAntialiasing, true);
		p.setOpacity(snap(selectionLevel, 0., 1.));
		p.setBrush(Qt::NoBrush);
		QPen pen = _st.selectFg;
		pen.setWidth(_st.selectWidth);
		p.setPen(pen);
		p.drawEllipse(rtlrect(x, y, _st.imageRadius * 2, _st.imageRadius * 2, outerWidth));
		p.setOpacity(1.);
		p.setRenderHint(QPainter::HighQualityAntialiasing, false);
	}

	removeFadeOutedIcons();
	p.setRenderHint(QPainter::SmoothPixmapTransform, true);
	for (auto &icon : _icons) {
		auto fadeIn = icon.fadeIn.current(1.);
		auto fadeOut = icon.fadeOut.current(1.);
		auto iconRadius = qRound(kWideRoundImageCheckboxScale * (_st.checkSmallRadius + fadeOut * (_st.checkRadius - _st.checkSmallRadius)));
		auto iconShift = kWideRoundImageCheckboxScale * _st.checkRadius - iconRadius;
		auto iconLeft = x + 2 * _st.imageRadius + _st.selectWidth - 2 * _st.checkRadius - (kWideRoundImageCheckboxScale - 1) * _st.checkRadius + iconShift;
		auto iconTop = y + 2 * _st.imageRadius + _st.selectWidth - 2 * _st.checkRadius - (kWideRoundImageCheckboxScale - 1) * _st.checkRadius + iconShift;
		auto to = QRect(iconLeft, iconTop, iconRadius * 2, iconRadius * 2);
		auto from = QRect(QPoint(0, 0), _wideCheckFullCache.size());
		auto opacity = fadeIn * fadeOut;
		p.setOpacity(opacity);
		if (fadeOut < 1.) {
			p.drawPixmapLeft(to, outerWidth, icon.wideCheckCache, from);
		} else {
			auto divider = qRound((kWideRoundImageCheckboxScale - 2) * _st.checkRadius + fadeIn * 3 * _st.checkRadius);
			p.drawPixmapLeft(QRect(iconLeft, iconTop, divider, iconRadius * 2), outerWidth, _wideCheckFullCache, QRect(0, 0, divider * cIntRetinaFactor(), _wideCheckFullCache.height()));
			p.drawPixmapLeft(QRect(iconLeft + divider, iconTop, iconRadius * 2 - divider, iconRadius * 2), outerWidth, _wideCheckBgCache, QRect(divider * cIntRetinaFactor(), 0, _wideCheckBgCache.width() - divider * cIntRetinaFactor(), _wideCheckBgCache.height()));
		}
	}
	p.setRenderHint(QPainter::SmoothPixmapTransform, false);
	p.setOpacity(1.);
}

void RoundImageCheckbox::toggleSelected() {
	_selected = !_selected;
	if (_selected) {
		_icons.push_back(Icon());
		_icons.back().fadeIn.start(UpdateCallback(_updateCallback), 0, 1, _st.selectDuration);
	} else {
		prepareWideCheckIconCache(&_icons.back());
		_icons.back().fadeOut.start([this] {
			_updateCallback();
			removeFadeOutedIcons(); // this call can destroy current lambda
		}, 1, 0, _st.selectDuration);
	}
	prepareWideCache();
	_selection.start(UpdateCallback(_updateCallback), _selected ? 0 : 1, _selected ? 1 : 0, _st.selectDuration, anim_bumpy<125>);
}

void RoundImageCheckbox::removeFadeOutedIcons() {
	while (!_icons.empty() && !_icons.front().fadeIn.animating() && !_icons.front().fadeOut.animating()) {
		if (_icons.size() > 1 || !_selected) {
			_icons.erase(_icons.begin());
		} else {
			break;
		}
	}
}

void RoundImageCheckbox::prepareWideCache() {
	if (_wideCache.isNull()) {
		auto size = _st.imageRadius * 2;
		auto wideSize = size * kWideRoundImageCheckboxScale;
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

void RoundImageCheckbox::prepareWideCheckIconCache(Icon *icon) {
	auto cacheWidth = _wideCheckBgCache.width() / _wideCheckBgCache.devicePixelRatio();
	auto cacheHeight = _wideCheckBgCache.height() / _wideCheckBgCache.devicePixelRatio();
	auto wideCache = QImage(cacheWidth * cIntRetinaFactor(), cacheHeight * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	wideCache.setDevicePixelRatio(cRetinaFactor());
	{
		Painter p(&wideCache);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		auto iconRadius = kWideRoundImageCheckboxScale * _st.checkRadius;
		auto divider = qRound((kWideRoundImageCheckboxScale - 2) * _st.checkRadius + icon->fadeIn.current(1.) * (kWideRoundImageCheckboxScale - 1) * _st.checkRadius);
		p.drawPixmapLeft(QRect(0, 0, divider, iconRadius * 2), cacheWidth, _wideCheckFullCache, QRect(0, 0, divider * cIntRetinaFactor(), _wideCheckFullCache.height()));
		p.drawPixmapLeft(QRect(divider, 0, iconRadius * 2 - divider, iconRadius * 2), cacheWidth, _wideCheckBgCache, QRect(divider * cIntRetinaFactor(), 0, _wideCheckBgCache.width() - divider * cIntRetinaFactor(), _wideCheckBgCache.height()));
	}
	icon->wideCheckCache = App::pixmapFromImageInPlace(std_::move(wideCache));
	icon->wideCheckCache.setDevicePixelRatio(cRetinaFactor());
}

} // namespace Ui
