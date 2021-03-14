/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

#include "editor/photo_editor_common.h"

class QGraphicsItem;
class QGraphicsView;

namespace Editor {

struct Controllers;
class ItemBase;
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

	void applyTransform(QRect geometry, int angle, bool flipped);
	void applyBrush(const Brush &brush);
	void cancel();
	void keepResult();
	void updateUndoState();

private:
	struct SavedItem {
		QGraphicsItem *item;
		bool undid = false;
	};

	bool hasUndo() const;
	bool hasRedo() const;
	void clearRedoList();

	bool isItemToRemove(not_null<QGraphicsItem*> item) const;
	bool isItemHidden(not_null<QGraphicsItem*> item) const;

	const std::shared_ptr<float64> _lastZ;
	const std::shared_ptr<Scene> _scene;
	const base::unique_qptr<QGraphicsView> _view;
	const QSize _imageSize;

	std::vector<SavedItem> _previousItems;
	QList<QGraphicsItem*> _itemsToRemove;

	rpl::variable<bool> _hasUndo = true;
	rpl::variable<bool> _hasRedo = true;

};

} // namespace Editor
