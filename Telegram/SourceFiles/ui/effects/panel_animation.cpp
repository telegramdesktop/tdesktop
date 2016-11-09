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
#include "ui/effects/panel_animation.h"

namespace Ui {

void PanelAnimation::setFinalImage(QImage &&finalImage, QRect inner) {
	t_assert(!started());
	_finalImage = std_::move(finalImage).convertToFormat(QImage::Format_ARGB32_Premultiplied);

	t_assert(!_finalImage.isNull());
	_finalWidth = _finalImage.width();
	_finalHeight = _finalImage.height();

	setStartWidth();
	setStartHeight();
	setStartAlpha();
	setStartFadeTop();
	createFadeMask();
	setWidthDuration();
	setHeightDuration();
	setAlphaDuration();
	setShadow();

	auto checkCorner = [this, inner](Corner &corner) {
		if (!corner.valid()) return;
		if ((_startWidth >= 0 && _startWidth < _finalWidth)
			|| (_startHeight >= 0 && _startHeight < _finalHeight)) {
			t_assert(corner.width <= inner.width());
			t_assert(corner.height <= inner.height());
		}
	};
	checkCorner(_topLeft);
	checkCorner(_topRight);
	checkCorner(_bottomLeft);
	checkCorner(_bottomRight);
	_finalInts = reinterpret_cast<const uint32*>(_finalImage.constBits());
	_finalIntsPerLine = (_finalImage.bytesPerLine() >> 2);
	_finalIntsPerLineAdded = _finalIntsPerLine - _finalWidth;
	t_assert(_finalImage.depth() == static_cast<int>(sizeof(uint32) << 3));
	t_assert(_finalImage.bytesPerLine() == (_finalIntsPerLine << 2));
	t_assert(_finalIntsPerLineAdded >= 0);

	_finalInnerLeft = inner.x();
	_finalInnerTop = inner.y();
	_finalInnerWidth = inner.width();
	_finalInnerHeight = inner.height();
	_finalInnerRight = _finalInnerLeft + _finalInnerWidth;
	_finalInnerBottom = _finalInnerTop + _finalInnerHeight;
	t_assert(QRect(0, 0, _finalWidth, _finalHeight).contains(inner));
}

void PanelAnimation::setShadow() {
	if (_skipShadow) return;

	_shadow.extend = _st.shadow.extend;
	_shadow.left = cloneImage(_st.shadow.left);
	if (_shadow.valid()) {
		_shadow.topLeft = cloneImage(_st.shadow.topLeft);
		_shadow.top = cloneImage(_st.shadow.top);
		_shadow.topRight = cloneImage(_st.shadow.topRight);
		_shadow.right = cloneImage(_st.shadow.right);
		_shadow.bottomRight = cloneImage(_st.shadow.bottomRight);
		_shadow.bottom = cloneImage(_st.shadow.bottom);
		_shadow.bottomLeft = cloneImage(_st.shadow.bottomLeft);
		t_assert(!_shadow.topLeft.isNull()
			&& !_shadow.top.isNull()
			&& !_shadow.topRight.isNull()
			&& !_shadow.right.isNull()
			&& !_shadow.bottomRight.isNull()
			&& !_shadow.bottom.isNull()
			&& !_shadow.bottomLeft.isNull());
	} else {
		_shadow.topLeft =
			_shadow.top =
			_shadow.topRight =
			_shadow.right =
			_shadow.bottomRight =
			_shadow.bottom =
			_shadow.bottomLeft = QImage();
	}
}

void PanelAnimation::setStartWidth() {
	_startWidth = qRound(_st.startWidth * _finalImage.width());
	if (_startWidth >= 0) t_assert(_startWidth <= _finalWidth);
}

void PanelAnimation::setStartHeight() {
	_startHeight = qRound(_st.startHeight * _finalImage.height());
	if (_startHeight >= 0) t_assert(_startHeight <= _finalHeight);
}

void PanelAnimation::setStartAlpha() {
	_startAlpha = qRound(_st.startOpacity * 255);
	t_assert(_startAlpha >= 0 && _startAlpha < 256);
}

void PanelAnimation::setStartFadeTop() {
	_startFadeTop = qRound(_st.startFadeTop * _finalImage.height());
}

void PanelAnimation::createFadeMask() {
	auto resultHeight = qRound(_finalImage.height() * _st.fadeHeight);
	auto finalAlpha = qRound(_st.fadeOpacity * 255);
	t_assert(finalAlpha >= 0 && finalAlpha < 256);
	auto result = QImage(1, resultHeight, QImage::Format_ARGB32_Premultiplied);
	auto ints = reinterpret_cast<uint32*>(result.bits());
	auto intsPerLine = (result.bytesPerLine() >> 2);
	auto up = (_origin == PanelAnimation::Origin::BottomLeft || _origin == PanelAnimation::Origin::BottomRight);
	auto from = up ? resultHeight : 0, to = resultHeight - from, delta = up ? -1 : 1;
	for (auto y = from; y != to; y += delta) {
		auto alpha = static_cast<uint32>(finalAlpha * y) / resultHeight;
		*ints = (0xFFU << 24) | (alpha << 16) | (alpha << 8) | alpha;
		ints += intsPerLine;
	}
	_fadeMask = style::colorizeImage(result, _st.fadeBg);
	_fadeHeight = _fadeMask.height();
	_fadeInts = reinterpret_cast<const uint32*>(_fadeMask.constBits());
	_fadeIntsPerLine = (_fadeMask.bytesPerLine() >> 2);
	t_assert(_fadeMask.bytesPerLine() == (_fadeIntsPerLine << 2));
}

void PanelAnimation::setCornerMasks(QImage &&topLeft, QImage &&topRight, QImage &&bottomLeft, QImage &&bottomRight) {
	setCornerMask(_topLeft, std_::move(topLeft));
	setCornerMask(_topRight, std_::move(topRight));
	setCornerMask(_bottomLeft, std_::move(bottomLeft));
	setCornerMask(_bottomRight, std_::move(bottomRight));
}

void PanelAnimation::setSkipShadow(bool skipShadow) {
	t_assert(!started());
	_skipShadow = skipShadow;
}

void PanelAnimation::setCornerMask(Corner &corner, QImage &&image) {
	t_assert(!started());
	corner.image = std_::move(image);
	if (corner.valid()) {
		corner.width = corner.image.width();
		corner.height = corner.image.height();
		corner.bytes = corner.image.constBits();
		corner.bytesPerPixel = (corner.image.depth() >> 3);
		corner.bytesPerLineAdded = corner.image.bytesPerLine() - corner.width * corner.bytesPerPixel;
		t_assert(corner.image.depth() == (corner.bytesPerPixel << 3));
		t_assert(corner.bytesPerLineAdded >= 0);
		if (_startWidth >= 0) t_assert(corner.width <= _startWidth);
		if (_startHeight >= 0) t_assert(corner.height <= _startHeight);
		if (!_finalImage.isNull()) {
			t_assert(corner.width <= _finalInnerWidth);
			t_assert(corner.height <= _finalInnerHeight);
		}
	} else {
		corner.width = corner.height = 0;
	}
}

QImage PanelAnimation::cloneImage(const style::icon &source) {
	if (source.empty()) return QImage();

	auto result = QImage(source.width(), source.height(), QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	{
		Painter p(&result);
		source.paint(p, 0, 0, source.width());
	}
	return std_::move(result);
}

void PanelAnimation::setWidthDuration() {
	_widthDuration = _st.widthDuration;
	t_assert(_widthDuration >= 0.);
	t_assert(_widthDuration <= 1.);
}

void PanelAnimation::setHeightDuration() {
	t_assert(!started());
	_heightDuration = _st.heightDuration;
	t_assert(_heightDuration >= 0.);
	t_assert(_heightDuration <= 1.);
}

void PanelAnimation::setAlphaDuration() {
	t_assert(!started());
	_alphaDuration = _st.opacityDuration;
	t_assert(_alphaDuration >= 0.);
	t_assert(_alphaDuration <= 1.);
}

void PanelAnimation::start() {
	t_assert(!started());
	t_assert(!_finalImage.isNull());
	_frame = QImage(_finalWidth, _finalHeight, QImage::Format_ARGB32_Premultiplied);
	_frame.setDevicePixelRatio(_finalImage.devicePixelRatio());
	_frameIntsPerLine = (_frame.bytesPerLine() >> 2);
	_frameInts = reinterpret_cast<uint32*>(_frame.bits());
	_frameIntsPerLineAdded = _frameIntsPerLine - _finalWidth;
	t_assert(_frame.depth() == static_cast<int>(sizeof(uint32) << 3));
	t_assert(_frame.bytesPerLine() == (_frameIntsPerLine << 2));
	t_assert(_frameIntsPerLineAdded >= 0);
}

const QImage &PanelAnimation::getFrame(float64 dt, float64 opacity) {
	t_assert(started());
	t_assert(dt >= 0.);

	auto &transition = anim::easeOutCirc;
	constexpr auto finalAlpha = 256;
	auto alpha = (dt >= _alphaDuration) ? finalAlpha : anim::interpolate(_startAlpha + 1, finalAlpha, transition(1., dt / _alphaDuration));
	_frameAlpha = anim::interpolate(0, alpha, opacity);

	auto frameWidth = (_startWidth < 0 || dt >= _widthDuration) ? _finalInnerWidth : anim::interpolate(_startWidth, _finalInnerWidth, transition(1., dt / _widthDuration));
	auto frameHeight = (_startHeight < 0 || dt >= _heightDuration) ? _finalInnerHeight : anim::interpolate(_startHeight, _finalInnerHeight, transition(1., dt / _heightDuration));
	auto frameLeft = (_origin == Origin::TopLeft || _origin == Origin::BottomLeft) ? _finalInnerLeft : (_finalInnerRight - frameWidth);
	auto frameTop = (_origin == Origin::TopLeft || _origin == Origin::TopRight) ? _finalInnerTop : (_finalInnerBottom - frameHeight);
	auto frameRight = frameLeft + frameWidth;
	auto frameBottom = frameTop + frameHeight;

	auto fadeTop = (_fadeHeight > 0) ? snap(anim::interpolate(_startFadeTop, _finalInnerHeight, transition(1., dt)), 0, frameHeight) : frameHeight;
	auto fadeBottom = (fadeTop < frameHeight) ? qMin(fadeTop + _fadeHeight, frameHeight) : frameHeight;
	auto fadeSkipLines = 0;
	if (_origin == Origin::BottomLeft || _origin == Origin::BottomRight) {
		fadeTop = frameHeight - fadeTop;
		fadeBottom = frameHeight - fadeBottom;
		qSwap(fadeTop, fadeBottom);
		fadeSkipLines = fadeTop + _fadeHeight - fadeBottom;
	}
	fadeTop += frameTop;
	fadeBottom += frameTop;

	auto finalInts = _finalInts + frameLeft + frameTop * _finalIntsPerLine;
	auto frameInts = _frameInts + frameLeft + frameTop * _frameIntsPerLine;
	auto finalIntsPerLineAdd = (_finalWidth - frameWidth) + _finalIntsPerLineAdded;
	auto frameIntsPerLineAdd = (_finalWidth - frameWidth) + _frameIntsPerLineAdded;

	// Draw frameWidth x fadeTop with fade first color.
	auto fadeInts = _fadeInts + fadeSkipLines * _fadeIntsPerLine;
	auto fadeWithMasterAlpha = [this](uint32 fade) {
		auto fadeAlphaAddition = (256 - (fade >> 24));
		auto fadePattern = anim::shifted(fade);
		return [this, fadeAlphaAddition, fadePattern](uint32 source) {
			auto sourceAlpha = (source >> 24) + 1;
			auto sourcePattern = anim::shifted(source);
			auto mixedPattern = anim::reshifted(fadePattern * sourceAlpha + sourcePattern * fadeAlphaAddition);
			return anim::unshifted(mixedPattern * _frameAlpha);
		};
	};
	if (frameTop != fadeTop) {
		// Take the fade components from the first line of the fade mask.
		auto withMasterAlpha = fadeWithMasterAlpha(_fadeInts ? *_fadeInts : 0);
		for (auto y = frameTop; y != fadeTop; ++y) {
			for (auto x = frameLeft; x != frameRight; ++x) {
				*frameInts++ = withMasterAlpha(*finalInts++);
			}
			finalInts += finalIntsPerLineAdd;
			frameInts += frameIntsPerLineAdd;
		}
	}

	// Draw frameWidth x (fadeBottom - fadeTop) with fade gradient.
	for (auto y = fadeTop; y != fadeBottom; ++y) {
		auto withMasterAlpha = fadeWithMasterAlpha(*fadeInts);
		for (auto x = frameLeft; x != frameRight; ++x) {
			*frameInts++ = withMasterAlpha(*finalInts++);
		}
		finalInts += finalIntsPerLineAdd;
		frameInts += frameIntsPerLineAdd;
		fadeInts += _fadeIntsPerLine;
	}

	// Draw frameWidth x (frameBottom - fadeBottom) with fade final color.
	if (fadeBottom != frameBottom) {
		// Take the fade components from the last line of the fade mask.
		auto withMasterAlpha = fadeWithMasterAlpha(*(fadeInts - _fadeIntsPerLine));
		for (auto y = fadeBottom; y != frameBottom; ++y) {
			for (auto x = frameLeft; x != frameRight; ++x) {
				*frameInts++ = withMasterAlpha(*finalInts++);
			}
			finalInts += finalIntsPerLineAdd;
			frameInts += frameIntsPerLineAdd;
		}
	}

	// Draw corners
	auto innerLeft = qMax(_finalInnerLeft, frameLeft);
	auto innerTop = qMax(_finalInnerTop, frameTop);
	auto innerRight = qMin(_finalInnerRight, frameRight);
	auto innerBottom = qMin(_finalInnerBottom, frameBottom);
	if (innerLeft != _finalInnerLeft || innerTop != _finalInnerTop) {
		paintCorner(_topLeft, innerLeft, innerTop);
	}
	if (innerRight != _finalInnerRight || innerTop != _finalInnerTop) {
		paintCorner(_topRight, innerRight - _topRight.width, innerTop);
	}
	if (innerLeft != _finalInnerLeft || innerBottom != _finalInnerBottom) {
		paintCorner(_bottomLeft, innerLeft, innerBottom - _bottomLeft.height);
	}
	if (innerRight != _finalInnerRight || innerBottom != _finalInnerBottom) {
		paintCorner(_bottomRight, innerRight - _bottomRight.width, innerBottom - _bottomRight.height);
	}

	// Fill the rest with transparent
	if (frameTop) {
		memset(_frameInts, 0, _frameIntsPerLine * frameTop * sizeof(uint32));
	}
	auto widthLeft = (_finalWidth - frameRight);
	if (frameLeft || widthLeft) {
		auto frameInts = _frameInts + frameTop * _frameIntsPerLine;
		for (auto y = frameTop; y != frameBottom; ++y) {
			memset(frameInts, 0, frameLeft * sizeof(uint32));
			memset(frameInts + frameLeft + frameWidth, 0, widthLeft * sizeof(uint32));
			frameInts += _frameIntsPerLine;
		}
	}
	if (auto heightLeft = (_finalHeight - frameBottom)) {
		memset(_frameInts + frameBottom * _frameIntsPerLine, 0, _frameIntsPerLine * heightLeft * sizeof(uint32));
	}

	// Draw shadow
	if (_shadow.valid()) {
		paintShadow(innerLeft, innerTop, innerRight, innerBottom);
	}

	// Debug
	//frameInts = _frameInts;
	//auto pattern = anim::shifted((static_cast<uint32>(0xFF) << 24) | (static_cast<uint32>(0xFF) << 16) | (static_cast<uint32>(0xFF) << 8) | static_cast<uint32>(0xFF));
	//for (auto y = 0; y != _finalHeight; ++y) {
	//	for (auto x = 0; x != _finalWidth; ++x) {
	//		auto source = *frameInts;
	//		auto sourceAlpha = (source >> 24);
	//		*frameInts = anim::unshifted(anim::shifted(source) * 256 + pattern * (256 - sourceAlpha));
	//		++frameInts;
	//	}
	//	frameInts += _frameIntsPerLineAdded;
	//}

	return _frame;
}

void PanelAnimation::paintCorner(Corner &corner, int left, int top) {
	auto mask = corner.bytes;
	auto bytesPerPixel = corner.bytesPerPixel;
	auto bytesPerLineAdded = corner.bytesPerLineAdded;
	auto frameInts = _frameInts + top * _frameIntsPerLine + left;
	auto frameIntsPerLineAdd = _frameIntsPerLine - corner.width;
	for (auto y = 0; y != corner.height; ++y) {
		for (auto x = 0; x != corner.width; ++x) {
			auto alpha = static_cast<uint32>(*mask) + 1;
			*frameInts = anim::unshifted(anim::shifted(*frameInts) * alpha);
			++frameInts;
			mask += bytesPerPixel;
		}
		frameInts += frameIntsPerLineAdd;
		mask += bytesPerLineAdded;
	}
}

void PanelAnimation::paintShadow(int left, int top, int right, int bottom) {
	left -= _shadow.extend.left();
	top -= _shadow.extend.top();
	right += _shadow.extend.right();
	bottom += _shadow.extend.bottom();
	paintShadowCorner(left, top, _shadow.topLeft);
	paintShadowCorner(right - _shadow.topRight.width(), top, _shadow.topRight);
	paintShadowCorner(right - _shadow.bottomRight.width(), bottom - _shadow.bottomRight.height(), _shadow.bottomRight);
	paintShadowCorner(left, bottom - _shadow.bottomLeft.height(), _shadow.bottomLeft);
	paintShadowVertical(left, top + _shadow.topLeft.height(), bottom - _shadow.bottomLeft.height(), _shadow.left);
	paintShadowVertical(right - _shadow.right.width(), top + _shadow.topRight.height(), bottom - _shadow.bottomRight.height(), _shadow.right);
	paintShadowHorizontal(left + _shadow.topLeft.width(), right - _shadow.topRight.width(), top, _shadow.top);
	paintShadowHorizontal(left + _shadow.bottomLeft.width(), right - _shadow.bottomRight.width(), bottom - _shadow.bottom.height(), _shadow.bottom);
}

void PanelAnimation::paintShadowCorner(int left, int top, const QImage &image) {
	auto imageWidth = image.width();
	auto imageHeight = image.height();
	auto imageInts = reinterpret_cast<const uint32*>(image.constBits());
	auto imageIntsPerLine = (image.bytesPerLine() >> 2);
	auto imageIntsPerLineAdded = imageIntsPerLine - imageWidth;
	if (left < 0) {
		auto shift = -base::take(left);
		imageWidth -= shift;
		imageInts += shift;
	}
	if (top < 0) {
		auto shift = -base::take(top);
		imageHeight -= shift;
		imageInts += shift * imageIntsPerLine;
	}
	if (left + imageWidth > _finalWidth) {
		imageWidth = _finalWidth - left;
	}
	if (top + imageHeight > _finalHeight) {
		imageHeight = _finalHeight - top;
	}
	if (imageWidth < 0 || imageHeight < 0) return;

	auto frameInts = _frameInts + top * _frameIntsPerLine + left;
	auto frameIntsPerLineAdd = _frameIntsPerLine - imageWidth;
	for (auto y = 0; y != imageHeight; ++y) {
		for (auto x = 0; x != imageWidth; ++x) {
			auto source = *frameInts;
			auto shadowAlpha = _frameAlpha - (source >> 24);
			*frameInts = anim::unshifted(anim::shifted(source) * 256 + anim::shifted(*imageInts) * shadowAlpha);
			++frameInts;
			++imageInts;
		}
		frameInts += frameIntsPerLineAdd;
		imageInts += imageIntsPerLineAdded;
	}
}

void PanelAnimation::paintShadowVertical(int left, int top, int bottom, const QImage &image) {
	auto imageWidth = image.width();
	auto imageInts = reinterpret_cast<const uint32*>(image.constBits());
	if (left < 0) {
		auto shift = -base::take(left);
		imageWidth -= shift;
		imageInts += shift;
	}
	if (top < 0) top = 0;
	if (bottom > _finalHeight) bottom = _finalHeight;
	if (left + imageWidth > _finalWidth) {
		imageWidth = _finalWidth - left;
	}
	if (imageWidth < 0 || bottom <= top) return;

	auto frameInts = _frameInts + top * _frameIntsPerLine + left;
	auto frameIntsPerLineAdd = _frameIntsPerLine - imageWidth;
	for (auto y = top; y != bottom; ++y) {
		for (auto x = 0; x != imageWidth; ++x) {
			auto source = *frameInts;
			auto shadowAlpha = _frameAlpha - (source >> 24);
			*frameInts = anim::unshifted(anim::shifted(source) * 256 + anim::shifted(*imageInts) * shadowAlpha);
			++frameInts;
			++imageInts;
		}
		frameInts += frameIntsPerLineAdd;
		imageInts -= imageWidth;
	}
}

void PanelAnimation::paintShadowHorizontal(int left, int right, int top, const QImage &image) {
	auto imageHeight = image.height();
	auto imageInts = reinterpret_cast<const uint32*>(image.constBits());
	auto imageIntsPerLine = (image.bytesPerLine() >> 2);
	if (top < 0) {
		auto shift = -base::take(top);
		imageHeight -= shift;
		imageInts += shift * imageIntsPerLine;
	}
	if (left < 0) left = 0;
	if (right > _finalWidth) right = _finalWidth;
	if (top + imageHeight > _finalHeight) {
		imageHeight = _finalHeight - top;
	}
	if (imageHeight < 0 || right <= left) return;

	auto frameInts = _frameInts + top * _frameIntsPerLine + left;
	auto frameIntsPerLineAdd = _frameIntsPerLine - (right - left);
	for (auto y = 0; y != imageHeight; ++y) {
		auto imagePattern = anim::shifted(*imageInts);
		for (auto x = left; x != right; ++x) {
			auto source = *frameInts;
			auto shadowAlpha = _frameAlpha - (source >> 24);
			*frameInts = anim::unshifted(anim::shifted(source) * 256 + imagePattern * shadowAlpha);
			++frameInts;
		}
		frameInts += frameIntsPerLineAdd;
		imageInts += imageIntsPerLine;
	}
}

} // namespace Ui
