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

	[[nodiscard]] std::shared_ptr<Scene> saveScene() const;
	void restoreScene();

	void applyTransform(QRect geometry, int angle, bool flipped);
	void applyBrush(const Brush &brush);
	void cancel();
	void keepResult();
	void updateUndoState();

	void handleMimeData(const QMimeData *data);

private:
	struct SavedItem {
		std::shared_ptr<QGraphicsItem> item;
		bool undid = false;
	};

	ItemBase::Data itemBaseData() const;

	void clearRedoList();

	const std::shared_ptr<Controllers> _controllers;
	const std::shared_ptr<Scene> _scene;
	const base::unique_qptr<QGraphicsView> _view;
	const QSize _imageSize;

	struct {
		int angle = 0;
		bool flipped = false;
		float64 zoom = 0.;
	} _transform;

	rpl::variable<bool> _hasUndo = true;
	rpl::variable<bool> _hasRedo = true;


};

} // namespace Editor
