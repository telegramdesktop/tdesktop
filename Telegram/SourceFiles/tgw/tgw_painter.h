/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "TwidgetCore/src/display_list.h"

namespace Tgw {

class QtBackend;

class PaintAllowance final {
public:
	PaintAllowance();

	[[nodiscard]] bool chargeElements(int64 count);
	[[nodiscard]] bool chargeStops(int64 count);
	[[nodiscard]] bool chargeFill(
		const QPainter &painter,
		const QPainterPath &path,
		const QBrush &brush);
	[[nodiscard]] bool chargeStroke(
		const QPainter &painter,
		const QPainterPath &path,
		const QPen &pen);
	[[nodiscard]] bool strokeDashes(
		QPainter &painter,
		const QPainterPath &path,
		const QPen &pen,
		std::vector<float64> dashes,
		float64 offset);

private:
	[[nodiscard]] bool chargeShape(
		const QPainterPath &path,
		const QTransform &device,
		QSize size,
		const QPen *pen,
		Qt::BrushStyle style);
	[[nodiscard]] bool charge(float64 edges, float64 work);

	int64 _elements = 0;
	int64 _edges = 0;
	int64 _work = 0;
	int64 _dashSteps = 0;
	int64 _flattenBytes = 0;

};

void PrepareHints(QPainter &painter);
[[nodiscard]] bool Finite(QPointF point);
[[nodiscard]] bool Finite(const QTransform &transform);
[[nodiscard]] QBrush GradientBrush(
	QGradient &gradient,
	const std::vector<twlite::Stop> &stops,
	bool reversed);
[[nodiscard]] QBrush LinearBrush(
	QPointF from,
	QPointF to,
	const std::vector<twlite::Stop> &stops);
[[nodiscard]] QBrush ConicBrush(
	QPointF centre,
	float64 from,
	const std::vector<twlite::Stop> &stops);
[[nodiscard]] QPainterPath VectorPath(
	const std::vector<twlite::Path::Op> &ops,
	const std::vector<float> &points,
	bool evenodd);
[[nodiscard]] Qt::PenCapStyle PathCap(int cap);
[[nodiscard]] Qt::PenJoinStyle PathJoin(int join);
[[nodiscard]] std::vector<float64> DashPattern(
	const std::vector<float> &dashes);
[[nodiscard]] float64 StrokeReach(float64 width, float64 miterLimit);
[[nodiscard]] int StackRadius(float64 sigma);
[[nodiscard]] QImage ShadowFromCoverage(QImage mask, int radius, QRgb color);

[[nodiscard]] QImage PaintFrame(
	const twlite::DisplayList &list,
	const QtBackend &backend,
	QSize deviceSize,
	float64 cssToDevice);

} // namespace Tgw
