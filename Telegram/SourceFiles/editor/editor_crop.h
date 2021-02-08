/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

#include "base/flat_map.h"
#include "editor/photo_editor_common.h"

namespace Editor {

// Crop control.
class Crop final : public Ui::RpWidget {
public:
	Crop(
		not_null<Ui::RpWidget*> parent,
		const PhotoModifications &modifications,
		const QSize &imageSize);

	void applyTransform(QRect geometry, int angle, bool flipped);
	[[nodiscard]] QRect innerRect() const;
	[[nodiscard]] QRect saveCropRect(
		const QRect &from,
		const QRect &to);
	[[nodiscard]] style::margins cropMargins() const;

protected:
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;

private:
	struct InfoAtDown {
		QRect rect;
		Qt::Edges edge = 0;
		QPoint point;

		struct Borders {
			int left = 0;
			int right = 0;
			int top = 0;
			int bottom = 0;
		} borders;
	};

	[[nodiscard]] QRectF resizedCropRect(
		const QRectF &from,
		const QRectF &to);

	void paintPoints(Painter &p);

	void updateEdges();
	[[nodiscard]] QPoint pointOfEdge(Qt::Edges e) const;
	void setCropRect(QRect &&rect);
	void setCropRectPaint(QRect &&rect);
	void rotate(bool clockwise = true);

	void computeDownState(const QPoint &p);
	void clearDownState();
	[[nodiscard]] Qt::Edges mouseState(const QPoint &p);
	void performCrop(const QPoint &pos);
	void performMove(const QPoint &pos);

	const int _pointSize;
	const float _pointSizeH;
	const style::margins _innerMargins;
	const QPoint _offset;
	const QMarginsF _edgePointMargins;

	base::flat_map<Qt::Edges, QRectF> _edges;

	// Is translated with the inner indentation.
	QRect _cropRectPaint;
	// Is not.
	QRect _cropRect;

	QRect _innerRect;

	QPainterPath _painterPath;

	InfoAtDown _down;

	int _angle = 0;
	bool _flipped = false;

	bool _keepAspectRatio = false;

};

} // namespace Editor
