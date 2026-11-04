/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"

#include "editor/editor_enhance.h"
#include "editor/photo_editor_common.h"
#include "editor/photo_editor_inner_common.h"
#include "media/media_video_canvas.h"
#include "ui/image/image.h"

#include <QtGui/QPixmap>

namespace Editor {

class AudioDiscButton;
class EnhanceIndicator;
class VideoLayerButton;
class Crop;
class VideoClip;
class Paint;
struct Controllers;

class PhotoEditorContent final : public Ui::RpWidget {
public:
	PhotoEditorContent(
		not_null<Ui::RpWidget*> parent,
		std::shared_ptr<Image> photo,
		PhotoModifications modifications,
		std::shared_ptr<Controllers> controllers,
		EditorData data);
	~PhotoEditorContent();

	void applyModifications(PhotoModifications modifications);
	void applyMode(const PhotoEditorMode &mode);
	void applyBrush(const Brush &brush);
	void createTextItem();
	void createShapeItem(ShapeType shape, const Brush &brush, bool fill);
	void armShapeTool(ShapeType shape, const Brush &brush, bool fill);
	void disarmShapeTool();
	void clearSelection();
	void applyTextPrefs(const TextPrefs &prefs);
	void setTextColor(const QColor &color);
	void setSelectedTextColor(const QColor &color);
	void applyBrushToSelectedShape(const Brush &brush);

	[[nodiscard]] rpl::producer<QColor> textColorRequests() const;
	[[nodiscard]] rpl::producer<TextPrefs> textPrefsUsed() const;
	[[nodiscard]] rpl::producer<QColor> textItemSelections() const;
	[[nodiscard]] rpl::producer<> textItemDeselections() const;
	[[nodiscard]] rpl::producer<bool> textEditStates() const;
	[[nodiscard]] rpl::producer<QColor> shapeItemSelections() const;
	[[nodiscard]] rpl::producer<> shapeItemDeselections() const;
	[[nodiscard]] rpl::producer<bool> shapeToolStates() const;
	[[nodiscard]] rpl::producer<> paintModeRequests() const;
	[[nodiscard]] auto videoClipSelections() const
		-> rpl::producer<std::shared_ptr<VideoClip>>;
	[[nodiscard]] rpl::producer<> audioChanges() const;
	[[nodiscard]] rpl::producer<bool> audioSelectedChanges() const;
	[[nodiscard]] rpl::producer<> audioVolumeChanges() const;
	[[nodiscard]] std::shared_ptr<AudioTrack> audio() const;
	[[nodiscard]] bool audioSelected() const;
	void removeAudio();
	[[nodiscard]] bool canEqualizeDurations() const;
	void matchDurations(crl::time duration);
	[[nodiscard]] bool durationsLinked() const;
	void setDurationsLinked(bool linked);
	[[nodiscard]] rpl::producer<> durationsLinkChanges() const;
	void applyAspectRatio(float64 ratio);
	void setExpansionRoom(bool room);
	void save(PhotoModifications &modifications);

	[[nodiscard]] bool edited() const;

	bool handleKeyPress(not_null<QKeyEvent*> e);

	void setupDragArea();
	void addMimeData(not_null<const QMimeData*> data);
	bool pasteFromClipboard();
	void addVideoObject(StoryVideoObject &&object);
	void addRoundVideo(QByteArray content, crl::time duration);

	[[nodiscard]] rpl::producer<QRect> innerRect() const {
		return _innerRect.value();
	}

private:
	void updateCanvas();
	void updateRoom();
	[[nodiscard]] QRectF layoutTarget(
		const PhotoModifications &mods,
		QSize size,
		QRect canvas,
		bool room) const;
	void applyLayout(QRectF imageRect);
	bool layoutAnimationStep(crl::time now);
	void paintCanvasFill(QPainter &p) const;
	void updateAudioDisc();
	void updateAudioDiscGeometry();

	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	bool eventHook(QEvent *e) override;

	[[nodiscard]] bool canvasMode() const;
	[[nodiscard]] bool canvasGesturesActive() const;
	[[nodiscard]] QRectF canvasBasePlacement() const;
	[[nodiscard]] QRectF canvasMediaRect() const;
	[[nodiscard]] float64 canvasScale() const;
	[[nodiscard]] bool canvasMoved() const;
	void restoreCanvasPlacement(QRect rect);
	void layoutCanvasMedia(QRect canvas);
	void clampCanvasShift();
	void zoomCanvas(float64 factor, QPoint about);
	void canvasChanged();
	void adjustEnhance(int delta);
	[[nodiscard]] const QPixmap &enhancedCanvasPix(QSize size);
	void recomposeCanvas();
	void paintCanvas(QPainter &p);
	void updateVideoLayerButton();
	void updateVideoLayerButtonGeometry();

	const QSize _photoSize;
	const bool _fixedCrop = false;
	const bool _composeAnimated = false;
	std::optional<StoryVideoObject> _pendingVideo;
	bool _selectVideoInPaint = false;
	const base::unique_qptr<Paint> _paint;
	const base::unique_qptr<Crop> _crop;
	const base::unique_qptr<AudioDiscButton> _audioDisc;
	const base::unique_qptr<VideoLayerButton> _videoLayerButton;
	std::shared_ptr<Image> _photo;
	const Media::Encode::CanvasBackground _background;

	rpl::variable<QRect> _innerRect;
	rpl::variable<PhotoModifications> _modifications;
	rpl::variable<QRect> _canvas;
	rpl::variable<bool> _room;
	rpl::event_stream<int> _keyPresses;
	rpl::event_stream<> _paintModeRequests;

	QRect _imageRect;
	QRectF _imageRectF;
	QRectF _layoutTarget;
	Ui::Animations::Basic _layoutAnimation;
	crl::time _layoutLastFrame = 0;
	QTransform _imageMatrix;
	PhotoEditorMode _mode;
	bool _roomRequested = false;
	bool _dragging = false;
	bool _animateLayout = false;

	const std::shared_ptr<Image> _canvasMedia;
	const Media::Encode::CanvasBackground _canvasBackground;
	QRect _canvasMediaDisplay;
	QRect _canvasFrameRect;
	QTransform _canvasMediaMatrix;
	float64 _canvasZoom = 1.;
	QPointF _canvasShift;
	float64 _initialCanvasZoom = 1.;
	QPointF _initialCanvasShift;
	std::optional<QRect> _canvasRestored;
	QPointF _canvasDragShift;
	std::optional<QPoint> _canvasPress;
	bool _canvasMoved = false;
	bool _canvasDirty = false;

	bool _storyPhoto = false;
	int _enhance = 0;
	int _initialEnhance = 0;
	EnhanceLut _enhanceLut;
	std::unique_ptr<EnhanceIndicator> _enhanceIndicator;
	QPixmap _enhancedPixmap;
	int _enhancedPixmapAmount = -1;
	QSize _enhancedPixmapSize;

};

} // namespace Editor
