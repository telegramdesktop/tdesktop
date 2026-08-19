/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"

namespace Info::Media {

class ListWidget;

class ListZoom final {
public:
	explicit ListZoom(not_null<ListWidget*> list);

	[[nodiscard]] bool isZoomable() const;
	[[nodiscard]] int minGridSize() const;

	void zoomIn();
	void zoomOut();
	[[nodiscard]] bool canZoomIn() const;
	[[nodiscard]] bool canZoomOut() const;

	bool handleNativeGesture(not_null<QNativeGestureEvent*> e);
	bool processWheel(not_null<QWheelEvent*> e);

	[[nodiscard]] bool paint(QPainter &p);

private:
	[[nodiscard]] int minGridSizeForStep(int step) const;
	[[nodiscard]] int minVideoGridSize() const;
	[[nodiscard]] int columnsForGridSize(int gridSize) const;
	[[nodiscard]] std::optional<int> nextZoomStep(int direction) const;
	[[nodiscard]] QPoint defaultAnchor() const;
	[[nodiscard]] QPixmap grabViewport(int height);
	void zoomBy(int direction, QPoint anchor);
	void apply(int step, QPoint anchor);

	const not_null<ListWidget*> _list;
	int _step = 0;
	float64 _pinchAccumulated = 0.;
	Ui::Animations::Simple _fade;
	QPixmap _oldPix;
	QPixmap _newPix;
	QPoint _anchorScreen;
	float64 _scaleRatio = 1.;
	int _fadeHeight = 0;

};

} // namespace Info::Media
