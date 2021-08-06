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
		const QSize &imageSize,
		EditorData type);

	void applyTransform(
		const QRect &geometry,
		int angle,
		bool flipped,
		const QSizeF &scaledImageSize);
	[[nodiscard]] QRect saveCropRect();
	[[nodiscard]] style::margins cropMargins() const;

protected:
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;

private:
	struct InfoAtDown {
		QRectF rect;
		Qt::Edges edge;
		QPoint point;
		float64 cropRatio = 0.;

		struct Borders {
			int left = 0;
			int right = 0;
			int top = 0;
			int bottom = 0;
		} borders;
	};

	void paintPoints(Painter &p);

	void updateEdges();
	[[nodiscard]] QPoint pointOfEdge(Qt::Edges e) const;
	void setCropPaint(QRectF &&rect);
	void convertCropPaintToOriginal();

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
	const QSize _imageSize;
	const EditorData _data;

	base::flat_map<Qt::Edges, QRectF> _edges;

	struct {
		float64 w = 0.;
		float64 h = 0.;
	} _ratio;

	QRectF _cropPaint;
	QRectF _cropOriginal;
	QRectF _innerRect;

	QPainterPath _painterPath;

	InfoAtDown _down;

	int _angle = 0;
	bool _flipped = false;

	bool _keepAspectRatio = false;

};

} // namespace Editor
