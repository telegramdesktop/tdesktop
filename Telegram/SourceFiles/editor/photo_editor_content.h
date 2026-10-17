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
#include "media/media_video_canvas.h"
#include "ui/image/image.h"

namespace Editor {

class AudioDiscButton;
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

	bool handleKeyPress(not_null<QKeyEvent*> e);

	void setupDragArea();
	void addMimeData(not_null<const QMimeData*> data);
	bool pasteFromClipboard();

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

	const QSize _photoSize;
	const bool _fixedCrop = false;
	const bool _composeAnimated = false;
	const base::unique_qptr<Paint> _paint;
	const base::unique_qptr<Crop> _crop;
	const base::unique_qptr<AudioDiscButton> _audioDisc;
	const std::shared_ptr<Image> _photo;
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

};

} // namespace Editor
