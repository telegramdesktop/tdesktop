/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

#include "editor/photo_editor_common.h"
#include "editor/photo_editor_inner_common.h"
#include "editor/scene/scene_item_base.h"

class QGraphicsItem;
class QGraphicsView;

namespace Editor {

struct Controllers;
class Scene;

// Paint control.
class Paint final : public Ui::RpWidget {
public:
	Paint(
		not_null<Ui::RpWidget*> parent,
		PhotoModifications &modifications,
		const QSize &imageSize,
		std::shared_ptr<Controllers> controllers);
	~Paint() override;

	[[nodiscard]] std::shared_ptr<Scene> saveScene() const;
	void restoreScene();

	void applyTransform(QRect geometry, int angle, bool flipped);
	void applyBrush(const Brush &brush);
	void cancel();
	void keepResult();
	void updateUndoState();

	void handleMimeData(const QMimeData *data);
	void paintImage(QPainter &p, const QPixmap &image) const;
	void resetView();

private:
	bool eventFilter(QObject *obj, QEvent *e) override;
	void updateViewGeometry();

	struct SavedItem {
		std::shared_ptr<QGraphicsItem> item;
		bool undid = false;
	};

	ItemBase::Data itemBaseData() const;
	void applyViewTransform();

	void clearRedoList();

	const std::shared_ptr<Controllers> _controllers;
	const std::shared_ptr<Scene> _scene;
	const base::unique_qptr<QGraphicsView> _view;
	QPointer<QWidget> _viewport;
	const QSize _imageSize;
	QRect _imageGeometry;
	QRect _outerGeometry;

	struct {
		int angle = 0;
		bool flipped = false;
		float64 zoom = 0.;
		float64 fitZoom = 0.;
		float64 ratioW = 0.;
		float64 ratioH = 0.;
		float64 userZoom = 1.;
	} _transform;

	struct {
		bool active = false;
		QPoint point;
	} _pan;

	rpl::variable<bool> _hasUndo = true;
	rpl::variable<bool> _hasRedo = true;


};

} // namespace Editor
