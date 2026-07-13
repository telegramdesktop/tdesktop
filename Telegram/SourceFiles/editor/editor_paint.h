/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"

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
		std::shared_ptr<Controllers> controllers,
		Fn<QImage(QRect)> blurSource,
		bool fixedCrop = false);
	~Paint() override;

	[[nodiscard]] std::shared_ptr<Scene> saveScene() const;
	void restoreScene();

	void applyTransform(QRect geometry, int angle, bool flipped);
	void applyBrush(const Brush &brush);
	void cancel();
	void keepResult();
	void updateUndoState();

	void createTextItem();
	void clearSelection();
	void setTextColor(const QColor &color);
	void setSelectedTextColor(const QColor &color);

	[[nodiscard]] rpl::producer<QColor> textColorRequests() const;
	[[nodiscard]] rpl::producer<QColor> textItemSelections() const;
	[[nodiscard]] rpl::producer<> textItemDeselections() const;
	[[nodiscard]] rpl::producer<bool> textEditStates() const;

	void handleMimeData(const QMimeData *data);
	void paintImage(QPainter &p, const QPixmap &image) const;
	void resetView();

	bool zoomSceneItems(float64 wheelDelta, bool fine = false);
	bool zoomSceneItemsByFactor(float64 factor);
	void panSceneItems(QPointF sceneDelta);
	[[nodiscard]] QPointF mapWidgetDeltaToScene(QPoint delta) const;

private:
	bool eventFilter(QObject *obj, QEvent *e) override;
	void updateViewGeometry();
	void zoomCanvas(float64 factor, QPoint viewportPoint, bool animated);
	void applyCanvasZoom(float64 zoom, bool subpixel);
	bool zoomAnimationStep(crl::time now);

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
	const bool _fixedCrop = false;
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

	bool _zoomAtLimit = false;
	float64 _zoomTarget = 1.;
	QPoint _zoomFocus;
	QPointF _zoomAnchorScene;
	crl::time _zoomLastFrame = 0;
	Ui::Animations::Basic _zoomAnimation;

	rpl::variable<bool> _hasUndo = true;
	rpl::variable<bool> _hasRedo = true;
	rpl::variable<bool> _textEditing = false;


};

} // namespace Editor
