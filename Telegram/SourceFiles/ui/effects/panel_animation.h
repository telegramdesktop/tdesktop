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
#pragma once

#include "styles/style_widgets.h"

namespace Ui {

class PanelAnimation {
public:
	enum class Origin {
		TopLeft,
		TopRight,
		BottomLeft,
		BottomRight,
	};
	PanelAnimation(const style::PanelAnimation &st, Origin origin) : _st(st), _origin(origin) {
	}

	void setFinalImage(QImage &&finalImage, QRect inner);
	void setCornerMasks(QImage &&topLeft, QImage &&topRight, QImage &&bottomLeft, QImage &&bottomRight);
	void setSkipShadow(bool skipShadow);

	void start();
	const QImage &getFrame(float64 dt, float64 opacity);

private:
	void setStartWidth();
	void setStartHeight();
	void setStartAlpha();
	void setStartFadeTop();
	void createFadeMask();
	void setWidthDuration();
	void setHeightDuration();
	void setAlphaDuration();
	void setShadow();

	bool started() const {
		return !_frame.isNull();
	}

	struct Corner {
		QImage image;
		int width = 0;
		int height = 0;
		const uchar *bytes = nullptr;
		int bytesPerPixel = 0;
		int bytesPerLineAdded = 0;

		bool valid() const {
			return !image.isNull();
		}
	};
	void setCornerMask(Corner &corner, QImage &&image);
	void paintCorner(Corner &corner, int left, int top);

	struct Shadow {
		style::margins extend;
		QImage left, topLeft, top, topRight, right, bottomRight, bottom, bottomLeft;

		bool valid() const {
			return !left.isNull();
		}
	};
	QImage cloneImage(const style::icon &source);
	void paintShadow(int left, int top, int right, int bottom);
	void paintShadowCorner(int left, int top, const QImage &image);
	void paintShadowVertical(int left, int top, int bottom, const QImage &image);
	void paintShadowHorizontal(int left, int right, int top, const QImage &image);

	const style::PanelAnimation &_st;
	Origin _origin = Origin::TopLeft;

	QImage _finalImage;
	const uint32 *_finalInts = nullptr;
	int _finalIntsPerLine = 0;
	int _finalIntsPerLineAdded = 0;
	int _finalWidth = 0;
	int _finalHeight = 0;
	int _finalInnerLeft = 0;
	int _finalInnerTop = 0;
	int _finalInnerRight = 0;
	int _finalInnerBottom = 0;
	int _finalInnerWidth = 0;
	int _finalInnerHeight = 0;

	Shadow _shadow;
	bool _skipShadow = false;
	int _startWidth = -1;
	int _startHeight = -1;
	int _startAlpha = 0;

	int _startFadeTop = 0;
	QImage _fadeMask;
	int _fadeHeight = 0;
	const uint32 *_fadeInts = nullptr;
	int _fadeIntsPerLine = 0;

	Corner _topLeft;
	Corner _topRight;
	Corner _bottomLeft;
	Corner _bottomRight;

	float64 _widthDuration = 1.;
	float64 _heightDuration = 1.;
	float64 _alphaDuration = 1.;

	QImage _frame;
	uint32 *_frameInts = nullptr;
	int _frameAlpha = 0; // recounted each getFrame()
	int _frameIntsPerLine = 0;
	int _frameIntsPerLineAdded = 0;

};

} // namespace Ui
