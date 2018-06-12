/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/panel_animation.h"

namespace Ui {

void RoundShadowAnimation::start(int frameWidth, int frameHeight, float64 devicePixelRatio) {
	Assert(!started());
	_frameWidth = frameWidth;
	_frameHeight = frameHeight;
	_frame = QImage(_frameWidth, _frameHeight, QImage::Format_ARGB32_Premultiplied);
	_frame.setDevicePixelRatio(devicePixelRatio);
	_frameIntsPerLine = (_frame.bytesPerLine() >> 2);
	_frameInts = reinterpret_cast<uint32*>(_frame.bits());
	_frameIntsPerLineAdded = _frameIntsPerLine - _frameWidth;
	Assert(_frame.depth() == static_cast<int>(sizeof(uint32) << 3));
	Assert(_frame.bytesPerLine() == (_frameIntsPerLine << 2));
	Assert(_frameIntsPerLineAdded >= 0);
}

void RoundShadowAnimation::setShadow(const style::Shadow &st) {
	_shadow.extend = st.extend * cIntRetinaFactor();
	_shadow.left = cloneImage(st.left);
	if (_shadow.valid()) {
		_shadow.topLeft = cloneImage(st.topLeft);
		_shadow.top = cloneImage(st.top);
		_shadow.topRight = cloneImage(st.topRight);
		_shadow.right = cloneImage(st.right);
		_shadow.bottomRight = cloneImage(st.bottomRight);
		_shadow.bottom = cloneImage(st.bottom);
		_shadow.bottomLeft = cloneImage(st.bottomLeft);
		Assert(!_shadow.topLeft.isNull()
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

void RoundShadowAnimation::setCornerMasks(const QImage &topLeft, const QImage &topRight, const QImage &bottomLeft, const QImage &bottomRight) {
	setCornerMask(_topLeft, topLeft);
	setCornerMask(_topRight, topRight);
	setCornerMask(_bottomLeft, bottomLeft);
	setCornerMask(_bottomRight, bottomRight);
}

void RoundShadowAnimation::setCornerMask(Corner &corner, const QImage &image) {
	Assert(!started());
	corner.image = image;
	if (corner.valid()) {
		corner.width = corner.image.width();
		corner.height = corner.image.height();
		corner.bytes = corner.image.constBits();
		corner.bytesPerPixel = (corner.image.depth() >> 3);
		corner.bytesPerLineAdded = corner.image.bytesPerLine() - corner.width * corner.bytesPerPixel;
		Assert(corner.image.depth() == (corner.bytesPerPixel << 3));
		Assert(corner.bytesPerLineAdded >= 0);
	} else {
		corner.width = corner.height = 0;
	}
}

QImage RoundShadowAnimation::cloneImage(const style::icon &source) {
	if (source.empty()) return QImage();

	auto result = QImage(source.size() * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cRetinaFactor());
	result.fill(Qt::transparent);
	{
		Painter p(&result);
		source.paint(p, 0, 0, source.width());
	}
	return result;
}

void RoundShadowAnimation::paintCorner(Corner &corner, int left, int top) {
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

void RoundShadowAnimation::paintShadow(int left, int top, int right, int bottom) {
	paintShadowCorner(left, top, _shadow.topLeft);
	paintShadowCorner(right - _shadow.topRight.width(), top, _shadow.topRight);
	paintShadowCorner(right - _shadow.bottomRight.width(), bottom - _shadow.bottomRight.height(), _shadow.bottomRight);
	paintShadowCorner(left, bottom - _shadow.bottomLeft.height(), _shadow.bottomLeft);
	paintShadowVertical(left, top + _shadow.topLeft.height(), bottom - _shadow.bottomLeft.height(), _shadow.left);
	paintShadowVertical(right - _shadow.right.width(), top + _shadow.topRight.height(), bottom - _shadow.bottomRight.height(), _shadow.right);
	paintShadowHorizontal(left + _shadow.topLeft.width(), right - _shadow.topRight.width(), top, _shadow.top);
	paintShadowHorizontal(left + _shadow.bottomLeft.width(), right - _shadow.bottomRight.width(), bottom - _shadow.bottom.height(), _shadow.bottom);
}

void RoundShadowAnimation::paintShadowCorner(int left, int top, const QImage &image) {
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
	if (left + imageWidth > _frameWidth) {
		imageWidth = _frameWidth - left;
	}
	if (top + imageHeight > _frameHeight) {
		imageHeight = _frameHeight - top;
	}
	if (imageWidth < 0 || imageHeight < 0) return;

	auto frameInts = _frameInts + top * _frameIntsPerLine + left;
	auto frameIntsPerLineAdd = _frameIntsPerLine - imageWidth;
	for (auto y = 0; y != imageHeight; ++y) {
		for (auto x = 0; x != imageWidth; ++x) {
			auto source = *frameInts;
			auto shadowAlpha = qMax(_frameAlpha - int(source >> 24), 0);
			*frameInts = anim::unshifted(anim::shifted(source) * 256 + anim::shifted(*imageInts) * shadowAlpha);
			++frameInts;
			++imageInts;
		}
		frameInts += frameIntsPerLineAdd;
		imageInts += imageIntsPerLineAdded;
	}
}

void RoundShadowAnimation::paintShadowVertical(int left, int top, int bottom, const QImage &image) {
	auto imageWidth = image.width();
	auto imageInts = reinterpret_cast<const uint32*>(image.constBits());
	if (left < 0) {
		auto shift = -base::take(left);
		imageWidth -= shift;
		imageInts += shift;
	}
	if (top < 0) top = 0;
	accumulate_min(bottom, _frameHeight);
	accumulate_min(imageWidth, _frameWidth - left);
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

void RoundShadowAnimation::paintShadowHorizontal(int left, int right, int top, const QImage &image) {
	auto imageHeight = image.height();
	auto imageInts = reinterpret_cast<const uint32*>(image.constBits());
	auto imageIntsPerLine = (image.bytesPerLine() >> 2);
	if (top < 0) {
		auto shift = -base::take(top);
		imageHeight -= shift;
		imageInts += shift * imageIntsPerLine;
	}
	if (left < 0) left = 0;
	accumulate_min(right, _frameWidth);
	accumulate_min(imageHeight, _frameHeight - top);
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

void PanelAnimation::setFinalImage(QImage &&finalImage, QRect inner) {
	Assert(!started());
	_finalImage = App::pixmapFromImageInPlace(std::move(finalImage).convertToFormat(QImage::Format_ARGB32_Premultiplied));

	Assert(!_finalImage.isNull());
	_finalWidth = _finalImage.width();
	_finalHeight = _finalImage.height();
	Assert(!(_finalWidth % cIntRetinaFactor()));
	Assert(!(_finalHeight % cIntRetinaFactor()));
	_finalInnerLeft = inner.x();
	_finalInnerTop = inner.y();
	_finalInnerWidth = inner.width();
	_finalInnerHeight = inner.height();
	Assert(!(_finalInnerLeft % cIntRetinaFactor()));
	Assert(!(_finalInnerTop % cIntRetinaFactor()));
	Assert(!(_finalInnerWidth % cIntRetinaFactor()));
	Assert(!(_finalInnerHeight % cIntRetinaFactor()));
	_finalInnerRight = _finalInnerLeft + _finalInnerWidth;
	_finalInnerBottom = _finalInnerTop + _finalInnerHeight;
	Assert(QRect(0, 0, _finalWidth, _finalHeight).contains(inner));

	setStartWidth();
	setStartHeight();
	setStartAlpha();
	setStartFadeTop();
	createFadeMask();
	setWidthDuration();
	setHeightDuration();
	setAlphaDuration();
	if (!_skipShadow) {
		setShadow(_st.shadow);
	}

	auto checkCorner = [this, inner](Corner &corner) {
		if (!corner.valid()) return;
		if ((_startWidth >= 0 && _startWidth < _finalWidth)
			|| (_startHeight >= 0 && _startHeight < _finalHeight)) {
			Assert(corner.width <= inner.width());
			Assert(corner.height <= inner.height());
		}
	};
	checkCorner(_topLeft);
	checkCorner(_topRight);
	checkCorner(_bottomLeft);
	checkCorner(_bottomRight);
}

void PanelAnimation::setStartWidth() {
	_startWidth = qRound(_st.startWidth * _finalInnerWidth);
	if (_startWidth >= 0) Assert(_startWidth <= _finalInnerWidth);
}

void PanelAnimation::setStartHeight() {
	_startHeight = qRound(_st.startHeight * _finalInnerHeight);
	if (_startHeight >= 0) Assert(_startHeight <= _finalInnerHeight);
}

void PanelAnimation::setStartAlpha() {
	_startAlpha = qRound(_st.startOpacity * 255);
	Assert(_startAlpha >= 0 && _startAlpha < 256);
}

void PanelAnimation::setStartFadeTop() {
	_startFadeTop = qRound(_st.startFadeTop * _finalInnerHeight);
}

void PanelAnimation::createFadeMask() {
	auto resultHeight = qRound(_finalImage.height() * _st.fadeHeight);
	if (auto remove = (resultHeight % cIntRetinaFactor())) {
		resultHeight -= remove;
	}
	auto finalAlpha = qRound(_st.fadeOpacity * 255);
	Assert(finalAlpha >= 0 && finalAlpha < 256);
	auto result = QImage(cIntRetinaFactor(), resultHeight, QImage::Format_ARGB32_Premultiplied);
	auto ints = reinterpret_cast<uint32*>(result.bits());
	auto intsPerLineAdded = (result.bytesPerLine() >> 2) - cIntRetinaFactor();
	auto up = (_origin == PanelAnimation::Origin::BottomLeft || _origin == PanelAnimation::Origin::BottomRight);
	auto from = up ? resultHeight : 0, to = resultHeight - from, delta = up ? -1 : 1;
	auto fadeFirstAlpha = up ? (finalAlpha + 1) : 1;
	auto fadeLastAlpha = up ? 1 : (finalAlpha + 1);
	_fadeFirst = QBrush(QColor(_st.fadeBg->c.red(), _st.fadeBg->c.green(), _st.fadeBg->c.blue(), (_st.fadeBg->c.alpha() * fadeFirstAlpha) >> 8));
	_fadeLast = QBrush(QColor(_st.fadeBg->c.red(), _st.fadeBg->c.green(), _st.fadeBg->c.blue(), (_st.fadeBg->c.alpha() * fadeLastAlpha) >> 8));
	for (auto y = from; y != to; y += delta) {
		auto alpha = static_cast<uint32>(finalAlpha * y) / resultHeight;
		auto value = (0xFFU << 24) | (alpha << 16) | (alpha << 8) | alpha;
		for (auto x = 0; x != cIntRetinaFactor(); ++x) {
			*ints++ = value;
		}
		ints += intsPerLineAdded;
	}
	_fadeMask = App::pixmapFromImageInPlace(style::colorizeImage(result, _st.fadeBg));
	_fadeHeight = _fadeMask.height();
}

void PanelAnimation::setSkipShadow(bool skipShadow) {
	Assert(!started());
	_skipShadow = skipShadow;
}

void PanelAnimation::setWidthDuration() {
	_widthDuration = _st.widthDuration;
	Assert(_widthDuration >= 0.);
	Assert(_widthDuration <= 1.);
}

void PanelAnimation::setHeightDuration() {
	Assert(!started());
	_heightDuration = _st.heightDuration;
	Assert(_heightDuration >= 0.);
	Assert(_heightDuration <= 1.);
}

void PanelAnimation::setAlphaDuration() {
	Assert(!started());
	_alphaDuration = _st.opacityDuration;
	Assert(_alphaDuration >= 0.);
	Assert(_alphaDuration <= 1.);
}

void PanelAnimation::start() {
	Assert(!_finalImage.isNull());
	RoundShadowAnimation::start(_finalWidth, _finalHeight, _finalImage.devicePixelRatio());
	auto checkCorner = [this](const Corner &corner) {
		if (!corner.valid()) return;
		if (_startWidth >= 0) Assert(corner.width <= _startWidth);
		if (_startHeight >= 0) Assert(corner.height <= _startHeight);
		Assert(corner.width <= _finalInnerWidth);
		Assert(corner.height <= _finalInnerHeight);
	};
	checkCorner(_topLeft);
	checkCorner(_topRight);
	checkCorner(_bottomLeft);
	checkCorner(_bottomRight);
}

void PanelAnimation::paintFrame(QPainter &p, int x, int y, int outerWidth, float64 dt, float64 opacity) {
	Assert(started());
	Assert(dt >= 0.);

	auto &transition = anim::easeOutCirc;
	if (dt < _alphaDuration) opacity *= transition(1., dt / _alphaDuration);
	_frameAlpha = anim::interpolate(1, 256, opacity);

	auto frameWidth = (_startWidth < 0 || dt >= _widthDuration) ? _finalInnerWidth : anim::interpolate(_startWidth, _finalInnerWidth, transition(1., dt / _widthDuration));
	auto frameHeight = (_startHeight < 0 || dt >= _heightDuration) ? _finalInnerHeight : anim::interpolate(_startHeight, _finalInnerHeight, transition(1., dt / _heightDuration));
	if (auto decrease = (frameWidth % cIntRetinaFactor())) {
		frameWidth -= decrease;
	}
	if (auto decrease = (frameHeight % cIntRetinaFactor())) {
		frameHeight -= decrease;
	}
	auto frameLeft = (_origin == Origin::TopLeft || _origin == Origin::BottomLeft) ? _finalInnerLeft : (_finalInnerRight - frameWidth);
	auto frameTop = (_origin == Origin::TopLeft || _origin == Origin::TopRight) ? _finalInnerTop : (_finalInnerBottom - frameHeight);
	auto frameRight = frameLeft + frameWidth;
	auto frameBottom = frameTop + frameHeight;

	auto fadeTop = (_fadeHeight > 0) ? snap(anim::interpolate(_startFadeTop, _finalInnerHeight, transition(1., dt)), 0, frameHeight) : frameHeight;
	if (auto decrease = (fadeTop % cIntRetinaFactor())) {
		fadeTop -= decrease;
	}
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

	if (opacity < 1.) {
		_frame.fill(Qt::transparent);
	}
	{
		Painter p(&_frame);
		p.setOpacity(opacity);
		auto painterFrameLeft = frameLeft / cIntRetinaFactor();
		auto painterFrameTop = frameTop / cIntRetinaFactor();
		auto painterFadeBottom = fadeBottom / cIntRetinaFactor();
		p.drawPixmap(painterFrameLeft, painterFrameTop, _finalImage, frameLeft, frameTop, frameWidth, frameHeight);
		if (_fadeHeight) {
			if (frameTop != fadeTop) {
				p.fillRect(painterFrameLeft, painterFrameTop, frameWidth, fadeTop - frameTop, _fadeFirst);
			}
			if (fadeTop != fadeBottom) {
				auto painterFadeTop = fadeTop / cIntRetinaFactor();
				auto painterFrameWidth = frameWidth / cIntRetinaFactor();
				auto painterFrameHeight = frameHeight / cIntRetinaFactor();
				p.drawPixmap(painterFrameLeft, painterFadeTop, painterFrameWidth, painterFadeBottom - painterFadeTop, _fadeMask, 0, fadeSkipLines, cIntRetinaFactor(), fadeBottom - fadeTop);
			}
			if (fadeBottom != frameBottom) {
				p.fillRect(painterFrameLeft, painterFadeBottom, frameWidth, frameBottom - fadeBottom, _fadeLast);
			}
		}
	}
	auto frameInts = _frameInts + frameLeft + frameTop * _frameIntsPerLine;
	auto frameIntsPerLineAdd = (_finalWidth - frameWidth) + _frameIntsPerLineAdded;

	// Draw corners
	paintCorner(_topLeft, frameLeft, frameTop);
	paintCorner(_topRight, frameRight - _topRight.width, frameTop);
	paintCorner(_bottomLeft, frameLeft, frameBottom - _bottomLeft.height);
	paintCorner(_bottomRight, frameRight - _bottomRight.width, frameBottom - _bottomRight.height);

	// Draw shadow upon the transparent
	auto outerLeft = frameLeft;
	auto outerTop = frameTop;
	auto outerRight = frameRight;
	auto outerBottom = frameBottom;
	if (_shadow.valid()) {
		outerLeft -= _shadow.extend.left();
		outerTop -= _shadow.extend.top();
		outerRight += _shadow.extend.right();
		outerBottom += _shadow.extend.bottom();
	}
	if (cIntRetinaFactor() > 1) {
		if (auto skipLeft = (outerLeft % cIntRetinaFactor())) {
			outerLeft -= skipLeft;
		}
		if (auto skipTop = (outerTop % cIntRetinaFactor())) {
			outerTop -= skipTop;
		}
		if (auto skipRight = (outerRight % cIntRetinaFactor())) {
			outerRight += (cIntRetinaFactor() - skipRight);
		}
		if (auto skipBottom = (outerBottom % cIntRetinaFactor())) {
			outerBottom += (cIntRetinaFactor() - skipBottom);
		}
	}

	if (opacity == 1.) {
		// Fill above the frame top with transparent.
		auto fillTopInts = (_frameInts + outerTop * _frameIntsPerLine + outerLeft);
		auto fillWidth = (outerRight - outerLeft) * sizeof(uint32);
		for (auto fillTop = frameTop - outerTop; fillTop != 0; --fillTop) {
			memset(fillTopInts, 0, fillWidth);
			fillTopInts += _frameIntsPerLine;
		}

		// Fill to the left and to the right of the frame with transparent.
		auto fillLeft = (frameLeft - outerLeft) * sizeof(uint32);
		auto fillRight = (outerRight - frameRight) * sizeof(uint32);
		if (fillLeft || fillRight) {
			auto fillInts = _frameInts + frameTop * _frameIntsPerLine;
			for (auto y = frameTop; y != frameBottom; ++y) {
				memset(fillInts + outerLeft, 0, fillLeft);
				memset(fillInts + frameRight, 0, fillRight);
				fillInts += _frameIntsPerLine;
			}
		}

		// Fill below the frame bottom with transparent.
		auto fillBottomInts = (_frameInts + frameBottom * _frameIntsPerLine + outerLeft);
		for (auto fillBottom = outerBottom - frameBottom; fillBottom != 0; --fillBottom) {
			memset(fillBottomInts, 0, fillWidth);
			fillBottomInts += _frameIntsPerLine;
		}
	}

	if (_shadow.valid()) {
		paintShadow(outerLeft, outerTop, outerRight, outerBottom);
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

	p.drawImage(rtlpoint(x + (outerLeft / cIntRetinaFactor()), y + (outerTop / cIntRetinaFactor()), outerWidth), _frame, QRect(outerLeft, outerTop, outerRight - outerLeft, outerBottom - outerTop));
}

} // namespace Ui
