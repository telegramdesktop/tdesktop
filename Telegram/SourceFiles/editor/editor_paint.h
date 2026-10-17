/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "data/data_types.h"
#include "ui/rp_widget.h"
#include "ui/effects/animations.h"

#include "editor/photo_editor_common.h"
#include "editor/photo_editor_inner_common.h"
#include "editor/scene/scene_item_base.h"
#include "media/media_video_canvas.h"

class QGraphicsItem;
class QGraphicsView;
class QKeyEvent;

namespace Main {
class Session;
} // namespace Main

namespace Storage {
struct PhotoEditorMedia;
} // namespace Storage

namespace Editor {

struct Controllers;
struct LinkBoxResult;
struct LinkPreview;
class MessageSource;
class Scene;
class VideoClip;

// Paint control.
class Paint final : public Ui::RpWidget {
public:
	Paint(
		not_null<Ui::RpWidget*> parent,
		PhotoModifications &modifications,
		const QSize &imageSize,
		std::shared_ptr<Controllers> controllers,
		Fn<QImage(QRect)> blurSource,
		const EditorData &data);
	~Paint() override;

	[[nodiscard]] std::shared_ptr<Scene> saveScene() const;
	void restoreScene();

	void applyTransform(
		QRect geometry,
		QRect canvasGeometry,
		QRectF canvas,
		int angle,
		bool flipped);
	void applyBrush(const Brush &brush);
	void cancel();
	void keepResult();
	void updateUndoState();

	void createTextItem();
	void createShapeItem(ShapeType shape, const Brush &brush, bool fill);
	void armShapeTool(ShapeType shape, const Brush &brush, bool fill);
	void disarmShapeTool();
	void clearSelection();
	void removeAudio();
	void setAudioSelected(bool selected);
	[[nodiscard]] bool canEqualizeDurations() const;
	void matchDurations(crl::time duration);
	[[nodiscard]] bool durationsLinked() const;
	void setDurationsLinked(bool linked);
	[[nodiscard]] rpl::producer<> durationsLinkChanges() const;
	[[nodiscard]] std::shared_ptr<AudioTrack> audio() const;
	[[nodiscard]] bool audioSelected() const;
	[[nodiscard]] rpl::producer<> audioChanges() const;
	[[nodiscard]] rpl::producer<bool> audioSelectedChanges() const;
	void applyTextPrefs(const TextPrefs &prefs);
	void setTextColor(const QColor &color);
	void setSelectedTextColor(const QColor &color);
	void applyBrushToSelectedShape(const Brush &brush);

	[[nodiscard]] bool handleKeyPress(not_null<QKeyEvent*> e);

	[[nodiscard]] rpl::producer<QColor> textColorRequests() const;
	[[nodiscard]] rpl::producer<TextPrefs> textPrefsUsed() const;
	[[nodiscard]] rpl::producer<QColor> textItemSelections() const;
	[[nodiscard]] rpl::producer<> textItemDeselections() const;
	[[nodiscard]] rpl::producer<bool> textEditStates() const;
	[[nodiscard]] rpl::producer<QColor> shapeItemSelections() const;
	[[nodiscard]] rpl::producer<> shapeItemDeselections() const;
	[[nodiscard]] rpl::producer<bool> shapeToolStates() const;
	[[nodiscard]] auto videoClipSelections() const
		-> rpl::producer<std::shared_ptr<VideoClip>>;

	[[nodiscard]] bool canHandleMimeData(const QMimeData *data) const;
	void handleMimeData(const QMimeData *data);
	void setCanvasBackground(
		const Media::Encode::CanvasBackground &background);
	void setCropRect(QRectF crop);
	void paintCanvas(QPainter &p) const;
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

	[[nodiscard]] Main::Session *session() const;
	ItemBase::Data itemBaseData() const;
	ItemBase::Data mediaItemData(QSize mediaSize) const;
	ItemBase::Data messageItemData(QSize bubbleSize) const;
	void addMediaItem(std::shared_ptr<ItemBase> item);
	void addMessages(const MessageIdsList &ids);
	void addMessageItem(
		std::shared_ptr<MessageSource> source,
		int index = 0,
		std::optional<bool> dark = std::nullopt,
		std::optional<QPointF> position = std::nullopt);
	void addLinkItem(
		LinkPreview link,
		std::optional<QPointF> position = std::nullopt);
	void chooseLink(const QString &url, ItemBase *editing = nullptr);
	void applyLinkResult(
		LinkBoxResult &&result,
		std::weak_ptr<NumberedItem> editing);
	void addMedia(Storage::PhotoEditorMedia &&media);
	void readMediaFile(const QString &path, const QByteArray &content);
	void addImageItem(QImage &&image);
	void addVideoItem(Storage::PhotoEditorMedia &&media);
	void choosePhotoFile();
	void chooseAudioFile();
	void readAudioFile(const QString &path, const QByteArray &content);
	void addAudio(AudioTrack &&track);
	void applyViewTransform();
	void bakeTextScales();

	void clearRedoList();

	const std::shared_ptr<Controllers> _controllers;
	const std::shared_ptr<Scene> _scene;
	const base::unique_qptr<QGraphicsView> _view;
	QPointer<QWidget> _viewport;
	const QSize _imageSize;
	const bool _fixedCrop = false;
	const bool _composeAnimated = false;
	const bool _composeSound = false;
	QRect _canvasGeometry;
	QRect _outerGeometry;
	QRectF _canvas;
	QRectF _cropRect;
	Media::Encode::CanvasBackground _background;

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
	base::Timer _textBakeTimer;


};

} // namespace Editor
