/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

#include "editor/photo_editor_common.h"

class QGraphicsItemGroup;
class QGraphicsView;

namespace Editor {

// Paint control.
class Paint final : public Ui::RpWidget {
public:
	Paint(
		not_null<Ui::RpWidget*> parent,
		PhotoModifications &modifications,
		const QSize &imageSize);

	[[nodiscard]] std::shared_ptr<QGraphicsScene> saveScene() const;

	void applyTransform(QRect geometry, int angle, bool flipped);
	void cancel();
	void keepResult();

private:
	void initDrawing();
	int itemsCount() const;

	const std::shared_ptr<QGraphicsScene> _scene;
	const base::unique_qptr<QGraphicsView> _view;
	const QSize _imageSize;

	int _startItemsCount = 0;

	struct {
		QPointF lastPoint;

		float size = 1.;
		QColor color;
		QGraphicsItemGroup *group;
	} _brushData;

};

} // namespace Editor
