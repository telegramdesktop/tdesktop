/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"

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
		QPoint imagePosition,
		int angle,
		bool flipped,
		const QSizeF &scaledImageSize);
	[[nodiscard]] QRect saveCropRect();
	[[nodiscard]] QRect cropRect() const;
	[[nodiscard]] rpl::producer<> changes() const {
		return _changes.events();
	}
	[[nodiscard]] rpl::producer<bool> dragChanges() const {
		return _dragChanges.events();
	}
	[[nodiscard]] QRect paintRect() const;
	[[nodiscard]] QPainterPath cropPath() const;
	[[nodiscard]] style::margins cropMargins() const;
	void setAspectRatio(float64 ratio);
	void setCornersLevel(RoundedCornersLevel level);
	void setExpansionAllowed(bool allowed);

protected:
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void hideEvent(QHideEvent *e) override;

private:
	struct InfoAtDown {
		QRectF rect;
		Qt::Edges edge;
		QPoint point;
		float64 cropRatio = 0.;
		QRectF bounds;

		struct Borders {
			int left = 0;
			int right = 0;
			int top = 0;
			int bottom = 0;
		} borders;
	};

	void paintFrame(QPainter &p);
	void paintGrid(QPainter &p, float64 opacity);
	void setGridVisible(bool visible, bool animated);

	void updateEdges();
	void updatePainterPath();
	[[nodiscard]] QPoint pointOfEdge(Qt::Edges e) const;
	[[nodiscard]] QRectF expansionBounds() const;
	void setCropPaint(QRectF &&rect);
	void convertCropPaintToOriginal();

	void computeDownState(const QPoint &p, Qt::Edges edge, bool expanding);
	void clearDownState();
	[[nodiscard]] Qt::Edges mouseState(const QPoint &p);
	void performCrop(const QPoint &pos);
	void performMove(const QPoint &pos);
	void finishDrag(bool animated);

	rpl::event_stream<> _changes;
	rpl::event_stream<bool> _dragChanges;

	const int _pointSize;
	const float _pointSizeH;
	const style::margins _innerMargins;
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
	Ui::Animations::Simple _gridOpacityAnimation;

	int _angle = 0;
	bool _flipped = false;
	bool _gridVisible = false;

	bool _keepAspectRatio = false;
	bool _expansionAllowed = false;

	RoundedCornersLevel _cornersLevel = RoundedCornersLevel::Large;

};

} // namespace Editor
