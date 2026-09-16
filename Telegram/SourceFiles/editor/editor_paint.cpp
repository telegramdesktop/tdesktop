/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_paint.h"

#include "apiwrap.h"
#include "base/platform/base_platform_haptic.h"
#include "base/qthelp_url.h"
#include "chat_helpers/compose/compose_show.h"
#include "core/file_utilities.h"
#include "core/mime_type.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "editor/controllers/controllers.h"
#include "editor/editor_link_box.h"
#include "editor/editor_message_render.h"
#include "editor/editor_message_source.h"
#include "editor/scene/scene_item_canvas.h"
#include "editor/scene/scene_item_image.h"
#include "editor/scene/scene_item_link.h"
#include "editor/scene/scene_item_message.h"
#include "editor/scene/scene_item_shape.h"
#include "editor/scene/scene_item_sticker.h"
#include "editor/scene/scene_item_text.h"
#include "editor/scene/scene_item_video.h"
#include "editor/scene/scene.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_single_player.h"
#include "main/main_session.h"
#include "platform/platform_file_utilities.h"
#include "storage/storage_media_prepare.h"
#include "ui/boxes/confirm_box.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/rect.h"
#include "ui/ui_utility.h"
#include "window/themes/window_theme.h"
#include "styles/style_editor.h"

#include <QGraphicsView>
#include <QNativeGestureEvent>
#include <QScrollBar>
#include <QWheelEvent>
#include <QtCore/QMimeData>

namespace Editor {
namespace {

constexpr auto kMaxBrush = 25.;
constexpr auto kMinBrush = 1.;
constexpr auto kShapeSizeRatio = 2. / 5.;
constexpr auto kMediaSizeRatio = 1. / 2.;
constexpr auto kImageMaxSizeRatio = 4.;

constexpr auto kMessageMaxWidthRatio = 0.88;
constexpr auto kMessageMaxHeightRatio = 0.7;
constexpr auto kMessagesCascadeRatio = 1. / 20.;

[[nodiscard]] bool IsForwardMimeData(not_null<const QMimeData*> data) {
	return data->hasFormat(u"application/x-td-forward"_q);
}

[[nodiscard]] QString MimeLinkUrl(not_null<const QMimeData*> data) {
	const auto urls = Core::ReadMimeUrls(data);
	const auto text = (urls.size() == 1 && !urls.front().isLocalFile())
		? urls.front().toString()
		: urls.isEmpty()
		? Core::ReadMimeText(data).trimmed()
		: QString();
	if (text.isEmpty() || text.contains('\n') || text.contains(' ')) {
		return QString();
	}
	return qthelp::validate_url(text);
}

[[nodiscard]] float64 BrushSize(const Brush &brush) {
	return kMinBrush + float64(kMaxBrush - kMinBrush) * brush.sizeRatio;
}

[[nodiscard]] int DefaultShapeSize(const QSize &imageSize) {
	return std::max(
		int(std::min(imageSize.width(), imageSize.height())
			* kShapeSizeRatio),
		1);
}

constexpr auto kMinCanvasZoom = 1.;
constexpr auto kMaxCanvasZoom = 8.;
constexpr auto kCanvasZoomStep = 1.15;
constexpr auto kZoomEpsilon = 0.0001;
constexpr auto kMinItemZoom = 0.1;
constexpr auto kMaxItemZoom = 10.;
constexpr auto kCanvasZoomStepFine = 1.015;
constexpr auto kZoomSmoothTau = 60.;
constexpr auto kZoomMaxFrameDelta = crl::time(64);
constexpr auto kTextBakeDelay = crl::time(300);

std::shared_ptr<Scene> EnsureScene(
		PhotoModifications &mods,
		const QSize &size) {
	if (!mods.paint) {
		mods.paint = std::make_shared<Scene>(QRectF(QPointF(), size));
	}
	return mods.paint;
}

} // namespace

using ItemPtr = std::shared_ptr<QGraphicsItem>;

Paint::Paint(
	not_null<Ui::RpWidget*> parent,
	PhotoModifications &modifications,
	const QSize &imageSize,
	std::shared_ptr<Controllers> controllers,
	Fn<QImage(QRect)> blurSource,
	const EditorData &data)
: RpWidget(parent)
, _controllers(controllers)
, _scene(EnsureScene(modifications, imageSize))
, _view(base::make_unique_q<QGraphicsView>(_scene.get(), this))
, _viewport(_view->viewport())
, _imageSize(imageSize)
, _fixedCrop(data.fixedCrop)
, _composeAnimated(data.composeAnimated)
, _composeSound(data.composeSound) {
	Expects(modifications.paint != nullptr);

	_scene->setBlurSource(std::move(blurSource));

	{
		constexpr auto kDefaultFontSizeDivisor = 15.;
		const auto shortSide = std::min(
			imageSize.width(),
			imageSize.height());
		_scene->setTextDefaults(
			QColor(255, 255, 255),
			shortSide / kDefaultFontSizeDivisor,
			TextStyle::Plain,
			TextTypeface::Default,
			TextAlignment::Center);
	}

	keepResult();

	_view->show();
	_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	_view->setFrameStyle(int(QFrame::NoFrame) | QFrame::Plain);
	_view->setBackgroundBrush(Qt::transparent);
	_view->setAttribute(Qt::WA_TranslucentBackground, true);
	_viewport->setAutoFillBackground(false);
	_viewport->setAttribute(Qt::WA_TranslucentBackground, true);
	_viewport->installEventFilter(this);
	_view->setAcceptDrops(false);
	_viewport->setAcceptDrops(false);

	_scene->textEditStates(
	) | rpl::on_next([=](bool editing) {
		_textEditing = editing;
		if (editing) {
			_textBakeTimer.cancel();
		}
	}, lifetime());

	_textBakeTimer.setCallback([=] { bakeTextScales(); });

	// Undo / Redo.
	controllers->undoController->performRequestChanges(
	) | rpl::on_next([=](const Undo &command) {
		if (_textEditing.current()) {
			return;
		}
		if (command == Undo::Undo) {
			_scene->performUndo();
		} else {
			_scene->performRedo();
		}

		_hasUndo = _scene->hasUndo();
		_hasRedo = _scene->hasRedo();
	}, lifetime());

	controllers->undoController->setCanPerformChanges(rpl::merge(
		rpl::combine(
			_hasUndo.value(),
			_textEditing.value()
		) | rpl::map([](bool enable, bool editing) {
			return UndoController::EnableRequest{
				.command = Undo::Undo,
				.enable = enable && !editing,
			};
		}),
		rpl::combine(
			_hasRedo.value(),
			_textEditing.value()
		) | rpl::map([](bool enable, bool editing) {
			return UndoController::EnableRequest{
				.command = Undo::Redo,
				.enable = enable && !editing,
			};
		})));

	if (controllers->stickersPanelController) {
		using ShowRequest = StickersPanelController::ShowRequest;

		controllers->stickersPanelController->setShowRequestChanges(
			rpl::merge(
				controllers->stickersPanelController->stickerChosen(
				) | rpl::map_to(ShowRequest::HideAnimated),
				controllers->stickersPanelController->photoRequests(
				) | rpl::map_to(ShowRequest::HideAnimated),
				controllers->stickersPanelController->audioRequests(
				) | rpl::map_to(ShowRequest::HideAnimated),
				controllers->stickersPanelController->linkRequests(
				) | rpl::map_to(ShowRequest::HideAnimated)));

		controllers->stickersPanelController->stickerChosen(
		) | rpl::on_next([=](not_null<DocumentData*> document) {
			addMediaItem(std::make_shared<ItemSticker>(
				document,
				itemBaseData()));
		}, lifetime());

		controllers->stickersPanelController->photoRequests(
		) | rpl::on_next([=] {
			choosePhotoFile();
		}, lifetime());

		controllers->stickersPanelController->audioRequests(
		) | rpl::on_next([=] {
			chooseAudioFile();
		}, lifetime());

		controllers->stickersPanelController->linkRequests(
		) | rpl::on_next([=] {
			chooseLink(QString());
		}, lifetime());
	}

	_scene->pendingShapeStates(
	) | rpl::on_next([=](bool armed) {
		if (!_viewport) {
			return;
		} else if (armed) {
			_viewport->setCursor(Qt::CrossCursor);
		} else {
			_viewport->unsetCursor();
		}
	}, lifetime());

	rpl::merge(
		controllers->stickersPanelController
			? controllers->stickersPanelController->stickerChosen(
				) | rpl::to_empty
			: rpl::never<>() | rpl::type_erased,
		_scene->addsItem()
	) | rpl::on_next([=] {
		clearRedoList();
		updateUndoState();
	}, lifetime());

	_scene->removesItem(
	) | rpl::on_next([=] {
		updateUndoState();
	}, lifetime());

	_zoomAnimation.init([=](crl::time now) {
		return zoomAnimationStep(now);
	});

}

bool Paint::zoomSceneItems(float64 wheelDelta, bool fine) {
	if (!wheelDelta) {
		return false;
	}
	const auto step = wheelDelta
		/ float64(QWheelEvent::DefaultDeltasPerStep);
	const auto base = fine ? kCanvasZoomStepFine : kCanvasZoomStep;
	return zoomSceneItemsByFactor(std::pow(base, step));
}

bool Paint::zoomSceneItemsByFactor(float64 factor) {
	const auto center = rect::center(_scene->canvasRect());
	auto applied = false;
	for (const auto &item : _scene->items()) {
		const auto raw = item.get();
		const auto oldScale = raw->scale();
		const auto newScale = std::clamp(
			oldScale * factor,
			kMinItemZoom,
			kMaxItemZoom);
		if (std::abs(newScale - oldScale) < kZoomEpsilon) {
			continue;
		}
		const auto ratio = newScale / oldScale;
		raw->setScale(newScale);
		const auto pos = raw->pos();
		raw->setPos(center + (pos - center) * ratio);
		applied = true;
	}
	if (!applied && std::abs(factor - 1.) > kZoomEpsilon) {
		if (!_zoomAtLimit) {
			_zoomAtLimit = true;
			base::Platform::Haptic();
		}
	} else if (applied) {
		_zoomAtLimit = false;
		_textBakeTimer.callOnce(kTextBakeDelay);
	}
	return applied;
}

void Paint::bakeTextScales() {
	for (const auto &item : _scene->items()) {
		if (item->isNormalStatus()
			&& (item->type() == ItemText::Type)
			&& item->isVisible()) {
			static_cast<ItemText*>(item.get())->bakeScale();
		}
	}
}

void Paint::zoomCanvas(float64 factor, QPoint viewportPoint, bool animated) {
	const auto view = _view.get();
	if (!view || !_viewport) {
		return;
	}
	const auto current = _zoomAnimation.animating()
		? _zoomTarget
		: _transform.userZoom;
	const auto raw = current * factor;
	const auto newTarget = std::clamp(raw, kMinCanvasZoom, kMaxCanvasZoom);
	const auto pushingPastLimit
		= (factor > 1. && raw > kMaxCanvasZoom + kZoomEpsilon)
		|| (factor < 1. && raw < kMinCanvasZoom - kZoomEpsilon);
	if (std::abs(newTarget - current) < kZoomEpsilon) {
		if (pushingPastLimit && !_zoomAtLimit) {
			_zoomAtLimit = true;
			base::Platform::Haptic();
		}
		return;
	}
	_zoomAtLimit = false;
	_zoomTarget = newTarget;
	_zoomFocus = viewportPoint;
	_zoomAnchorScene = view->mapToScene(viewportPoint);
	if (animated) {
		if (!_zoomAnimation.animating()) {
			_zoomLastFrame = crl::now();
			_zoomAnimation.start();
		}
	} else {
		_zoomAnimation.stop();
		applyCanvasZoom(newTarget, false);
	}
}

void Paint::applyCanvasZoom(float64 zoom, bool subpixel) {
	const auto view = _view.get();
	if (!view || !_viewport) {
		return;
	}
	_transform.userZoom = zoom;
	updateViewGeometry();
	applyViewTransform();
	const auto sceneAtFocus = view->mapToScene(_zoomFocus);
	const auto center = view->mapToScene(rect::center(_viewport->rect()));
	view->centerOn(center - (sceneAtFocus - _zoomAnchorScene));
	if (subpixel) {
		const auto landed = view->viewportTransform().map(_zoomAnchorScene);
		auto residual = QPointF(_zoomFocus) - landed;
		residual.setX(std::clamp(residual.x(), -1., 1.));
		residual.setY(std::clamp(residual.y(), -1., 1.));
		view->setTransform(view->transform()
			* QTransform().translate(residual.x(), residual.y()));
	}
	if (const auto parent = parentWidget()) {
		parent->update(geometry());
	}
}

bool Paint::zoomAnimationStep(crl::time now) {
	const auto delta = std::clamp(
		now - _zoomLastFrame,
		crl::time(0),
		kZoomMaxFrameDelta);
	_zoomLastFrame = now;

	const auto shown = _transform.userZoom;
	const auto ratio = 1. - std::exp(-float64(delta) / kZoomSmoothTau);
	auto next = shown + (_zoomTarget - shown) * ratio;
	const auto finished = std::abs(next - _zoomTarget) < kZoomEpsilon;
	if (finished) {
		next = _zoomTarget;
	}
	applyCanvasZoom(next, !finished);
	return !finished;
}

void Paint::panSceneItems(QPointF sceneDelta) {
	if (sceneDelta.isNull()) {
		return;
	}
	for (const auto &item : _scene->items()) {
		item->setPos(item->pos() + sceneDelta);
	}
}

QPointF Paint::mapWidgetDeltaToScene(QPoint delta) const {
	if (!_view) {
		return QPointF(delta);
	}
	return _view->mapToScene(delta) - _view->mapToScene(QPoint());
}

Paint::~Paint() {
	_scene->setPendingShape(std::nullopt);
	_scene->cancelTextEditing();
	_scene->releaseAnimations();
	if (_viewport) {
		_viewport->removeEventFilter(this);
	}
}

void Paint::updateViewGeometry() {
	if (_canvasGeometry.isEmpty()) {
		return;
	}
	const auto target = (_transform.userZoom - kMinCanvasZoom) > kZoomEpsilon
		? _outerGeometry
		: _canvasGeometry;
	if (geometry() != target) {
		setGeometry(target);
	}
	_view->setGeometry(rect());
}

void Paint::applyTransform(
		QRect geometry,
		QRect canvasGeometry,
		QRectF canvas,
		int angle,
		bool flipped) {
	if (geometry.isEmpty() || canvasGeometry.isEmpty()) {
		return;
	}
	_canvasGeometry = canvasGeometry;
	_canvas = canvas;
	_outerGeometry = parentWidget() ? parentWidget()->rect() : geometry;
	_view->setSceneRect((canvas == _scene->sceneRect()) ? QRectF() : canvas);
	_scene->setCanvasRect(canvas);

	const auto center = (_transform.fitZoom <= 0.)
		|| _view->viewport()->rect().isEmpty()
		|| (_transform.userZoom == kMinCanvasZoom)
		? rect::center(canvas)
		: _view->mapToScene(_view->viewport()->rect().center());
	const auto size = geometry.size();

	const auto rotatedImageSize = QTransform()
		.rotate(angle)
		.mapRect(QRect(QPoint(), _imageSize));

	const auto ratioW = size.width() / float64(rotatedImageSize.width())
		* (flipped ? -1 : 1);
	const auto ratioH = size.height() / float64(rotatedImageSize.height());

	_view->setGeometry(rect());

	_transform = {
		.angle = angle,
		.flipped = flipped,
		.fitZoom = ((std::abs(ratioW) + std::abs(ratioH)) / 2.),
		.ratioW = ratioW,
		.ratioH = ratioH,
		.userZoom = _transform.userZoom,
	};
	updateViewGeometry();
	applyViewTransform();
	_view->centerOn(center);
	if (const auto parent = parentWidget()) {
		parent->update();
	}
}

std::shared_ptr<Scene> Paint::saveScene() const {
	_scene->save(SaveState::Save);
	return (_scene->items().empty() && !_scene->audio())
		? nullptr
		: _scene;
}

void Paint::restoreScene() {
	_scene->restore(SaveState::Save);
}

void Paint::cancel() {
	_scene->restore(SaveState::Keep);
}

void Paint::keepResult() {
	_scene->save(SaveState::Keep);
}

void Paint::clearRedoList() {
	_scene->clearRedoList();

	_hasRedo = false;
}

void Paint::updateUndoState() {
	_hasUndo = _scene->hasUndo();
	_hasRedo = _scene->hasRedo();
}

void Paint::applyBrush(const Brush &brush) {
	_scene->applyBrush(
		brush.color,
		BrushSize(brush),
		brush.tool);
	_scene->updatePendingShapeBrush(brush.color, BrushSize(brush));
}

void Paint::applyBrushToSelectedShape(const Brush &brush) {
	_scene->setSelectedShapeBrush(brush.color, BrushSize(brush));
}

void Paint::createTextItem() {
	disarmShapeTool();
	_scene->createTextAtCenter(-_transform.angle, _transform.flipped);
}

void Paint::createShapeItem(ShapeType shape, const Brush &brush, bool fill) {
	disarmShapeTool();
	auto data = itemBaseData();
	data.size = DefaultShapeSize(_imageSize);
	const auto item = std::make_shared<ItemShape>(
		shape,
		brush.color,
		BrushSize(brush),
		fill,
		std::move(data));
	_scene->addItem(item);
	_scene->clearSelection();
	item->setSelected(true);
	item->setFocus();
	_view->setFocus();
}

void Paint::armShapeTool(ShapeType shape, const Brush &brush, bool fill) {
	_scene->setPendingShape(Scene::PendingShape{
		.shape = shape,
		.color = brush.color,
		.strokeWidth = BrushSize(brush),
		.defaultSize = DefaultShapeSize(_imageSize),
		.fill = fill,
		.rotation = -_transform.angle,
		.flipped = _transform.flipped,
	});
}

void Paint::disarmShapeTool() {
	_scene->setPendingShape(std::nullopt);
}

bool Paint::handleKeyPress(not_null<QKeyEvent*> e) {
	const auto key = e->key();
	if ((key == Qt::Key_Escape) && _scene->hasPendingShape()) {
		disarmShapeTool();
		return true;
	} else if (!_scene->audioSelected()) {
		return false;
	} else if (key == Qt::Key_Escape) {
		_scene->setAudioSelected(false);
		return true;
	} else if ((key == Qt::Key_Delete) || (key == Qt::Key_Backspace)) {
		removeAudio();
		return true;
	}
	return false;
}

void Paint::clearSelection() {
	_scene->clearSelection();
}

void Paint::removeAudio() {
	_scene->setAudio(nullptr);
}

void Paint::setAudioSelected(bool selected) {
	_scene->setAudioSelected(selected);
}

bool Paint::canEqualizeDurations() const {
	return _scene->canEqualizeDurations();
}

void Paint::matchDurations(crl::time duration) {
	_scene->matchDurations(duration);
}

bool Paint::durationsLinked() const {
	return _scene->durationsLinked();
}

void Paint::setDurationsLinked(bool linked) {
	_scene->setDurationsLinked(linked);
}

rpl::producer<> Paint::durationsLinkChanges() const {
	return _scene->durationsLinkChanges();
}

std::shared_ptr<AudioTrack> Paint::audio() const {
	return _scene->audio();
}

bool Paint::audioSelected() const {
	return _scene->audioSelected();
}

rpl::producer<> Paint::audioChanges() const {
	return _scene->audioChanges();
}

rpl::producer<bool> Paint::audioSelectedChanges() const {
	return _scene->audioSelectedChanges();
}

void Paint::applyTextPrefs(const TextPrefs &prefs) {
	_scene->applyTextPrefs(prefs);
}

void Paint::setTextColor(const QColor &color) {
	_scene->setTextColor(color);
}

void Paint::setSelectedTextColor(const QColor &color) {
	_scene->setSelectedTextColor(color);
}

rpl::producer<QColor> Paint::textColorRequests() const {
	return _scene->textColorRequests();
}

rpl::producer<TextPrefs> Paint::textPrefsUsed() const {
	return _scene->textPrefsUsed();
}

rpl::producer<QColor> Paint::textItemSelections() const {
	return _scene->textItemSelections();
}

rpl::producer<> Paint::textItemDeselections() const {
	return _scene->textItemDeselections();
}

rpl::producer<bool> Paint::textEditStates() const {
	return _scene->textEditStates();
}

rpl::producer<QColor> Paint::shapeItemSelections() const {
	return _scene->shapeItemSelections();
}

auto Paint::videoClipSelections() const
-> rpl::producer<std::shared_ptr<VideoClip>> {
	return _scene->videoClipSelections();
}

rpl::producer<> Paint::shapeItemDeselections() const {
	return _scene->shapeItemDeselections();
}

rpl::producer<bool> Paint::shapeToolStates() const {
	return _scene->pendingShapeStates();
}

bool Paint::canHandleMimeData(const QMimeData *data) const {
	if (!data || _textEditing.current()) {
		return false;
	} else if (session()
		&& (IsForwardMimeData(data) || !MimeLinkUrl(data).isEmpty())) {
		return true;
	}
	return Storage::ValidatePhotoEditorMediaDragData(
		data,
		_composeAnimated,
		_composeSound);
}

void Paint::handleMimeData(const QMimeData *data) {
	if (IsForwardMimeData(data)) {
		if (const auto session = Paint::session()) {
			addMessages(session->data().takeMimeForwardIds());
		}
		return;
	}
	const auto urls = Core::ReadMimeUrls(data);
	if (urls.size() == 1 && urls.front().isLocalFile()) {
		readMediaFile(
			Platform::File::UrlToLocal(urls.front()),
			QByteArray());
	} else if (auto read = Core::ReadMimeImage(data)) {
		addMedia({ .image = std::move(read.image) });
	} else if (const auto url = MimeLinkUrl(data); !url.isEmpty()) {
		chooseLink(url);
	} else {
		addMedia({});
	}
}

Main::Session *Paint::session() const {
	const auto &show = _controllers->sessionShow;
	return show ? &show->session() : nullptr;
}

void Paint::addMessages(const MessageIdsList &ids) {
	const auto session = Paint::session();
	if (!session) {
		return;
	}
	auto added = base::flat_set<not_null<HistoryItem*>>();
	auto forbidden = (HistoryItem*)nullptr;
	auto index = 0;
	for (const auto &id : ids) {
		const auto item = session->data().message(id);
		if (!item) {
			continue;
		}
		const auto render = MessageToRender(item);
		if (!added.emplace(render).second) {
			continue;
		} else if (MessageForbidsRender(render)) {
			forbidden = render;
			continue;
		} else if (!CanRenderMessage(render)) {
			if (render->hasDirectLink()) {
				addLinkItem({
					.url = session->api().exportDirectMessageLink(
						render,
						false),
					.preview = false,
				});
			}
			continue;
		}
		addMessageItem(std::make_shared<MessageSource>(render), index++);
	}
	if (forbidden) {
		_controllers->show->showBox(Ui::MakeInformBox(
			forbidden->history()->peer->isBroadcast()
				? tr::lng_error_noforwards_channel()
				: tr::lng_error_noforwards_group()));
	}
}

void Paint::addMessageItem(
		std::shared_ptr<MessageSource> source,
		int index,
		std::optional<bool> dark,
		std::optional<QPointF> position) {
	auto renderer = std::make_unique<MessageRenderer>(source);
	renderer->setDark(dark);
	auto data = messageItemData(renderer->size());
	const auto scene = _scene->sceneRect().size();
	const auto shift = int(std::min(scene.width(), scene.height())
		* kMessagesCascadeRatio) * index;
	data.x = position ? int(position->x()) : (data.x + shift);
	data.y = position ? int(position->y()) : (data.y + shift);
	const auto item = std::make_shared<ItemMessage>(
		std::move(source),
		std::move(renderer),
		std::move(data),
		dark,
		MessageVideoOptions{
			.play = _composeAnimated,
			.sound = _composeSound,
		});
	item->setEditCallback(crl::guard(this, [=](
			not_null<ItemMessage*> item) {
		const auto &link = item->source()->link();
		chooseLink(link ? link->url : QString(), item);
	}));
	addMediaItem(item);
}

void Paint::addLinkItem(LinkPreview link, std::optional<QPointF> position) {
	auto data = itemBaseData();
	data.size = std::max(
		int(std::ceil(ItemLink::MakePill(link, _imageSize).size().width())),
		1);
	if (position) {
		data.x = int(position->x());
		data.y = int(position->y());
	}
	const auto item = std::make_shared<ItemLink>(std::move(link), data);
	item->setEditCallback(crl::guard(this, [=](not_null<ItemLink*> item) {
		chooseLink(item->link().url, item);
	}));
	addMediaItem(item);
}

void Paint::chooseLink(const QString &url, ItemBase *editing) {
	const auto &show = _controllers->sessionShow;
	if (!show) {
		return;
	}
	auto link = std::optional<LinkPreview>();
	if (!editing) {
	} else if (editing->type() == ItemMessage::Type) {
		const auto item = static_cast<ItemMessage*>(editing);
		link = item->source()->link();
		if (link) {
			link->dark = item->dark().value_or(
				Window::Theme::IsNightMode());
		}
	} else if (editing->type() == ItemLink::Type) {
		link = static_cast<ItemLink*>(editing)->link();
	}
	const auto weak = editing
		? std::weak_ptr(_scene->itemShared(editing))
		: std::weak_ptr<NumberedItem>();
	_controllers->layerShow->showBox(LinkBox({
		.show = show,
		.url = url,
		.editing = link,
		.done = crl::guard(this, [=](LinkBoxResult &&result) {
			applyLinkResult(std::move(result), weak);
		}),
	}));
}

void Paint::applyLinkResult(
		LinkBoxResult &&result,
		std::weak_ptr<NumberedItem> editing) {
	const auto strong = editing.lock();
	const auto raw = (strong && strong->isNormalStatus())
		? strong.get()
		: nullptr;
	if (raw && result.message && raw->type() == ItemMessage::Type) {
		const auto item = static_cast<ItemMessage*>(raw);
		item->setSource(std::move(result.message));
		item->setDark(result.dark);
		return;
	} else if (raw && result.pill && raw->type() == ItemLink::Type) {
		static_cast<ItemLink*>(raw)->setLink(std::move(*result.pill));
		return;
	}
	const auto position = raw
		? std::make_optional(raw->scenePos())
		: std::nullopt;
	if (raw) {
		_scene->removeItem(strong);
	}
	if (result.message) {
		addMessageItem(std::move(result.message), 0, result.dark, position);
	} else if (result.pill) {
		addLinkItem(std::move(*result.pill), position);
	}
}

void Paint::readMediaFile(const QString &path, const QByteArray &content) {
	Storage::ReadPhotoEditorMediaAsync(path, content, crl::guard(this, [=](
			Storage::PhotoEditorMedia &&media) {
		addMedia(std::move(media));
	}));
}

void Paint::choosePhotoFile() {
	const auto callback = [=](FileDialog::OpenResult &&result) {
		if (result.paths.isEmpty() && result.remoteContent.isEmpty()) {
			return;
		}
		readMediaFile(
			result.paths.isEmpty() ? QString() : result.paths.front(),
			result.remoteContent);
	};
	FileDialog::GetOpenPath(
		this,
		tr::lng_choose_image(tr::now),
		(_composeAnimated
			? FileDialog::PhotoVideoFilesFilter()
			: FileDialog::ImagesFilter()),
		crl::guard(this, callback));
}

void Paint::chooseAudioFile() {
	const auto callback = [=](FileDialog::OpenResult &&result) {
		if (result.paths.isEmpty() && result.remoteContent.isEmpty()) {
			return;
		}
		readAudioFile(
			result.paths.isEmpty() ? QString() : result.paths.front(),
			result.remoteContent);
	};
	FileDialog::GetOpenPath(
		this,
		tr::lng_choose_audio(tr::now),
		FileDialog::AudioFilesFilter(),
		crl::guard(this, callback));
}

void Paint::readAudioFile(const QString &path, const QByteArray &content) {
	const auto done = crl::guard(this, [=](AudioTrack &&track) {
		addAudio(std::move(track));
	});
	crl::async([=] {
		auto track = Storage::ReadPhotoEditorAudio(path, content);
		crl::on_main([=, track = std::move(track)]() mutable {
			done(std::move(track));
		});
	});
}

void Paint::addAudio(AudioTrack &&track) {
	if (track.empty() || (track.duration <= 0)) {
		_controllers->show->showBox(
			Ui::MakeInformBox(tr::lng_edit_media_invalid_file()));
		return;
	}
	disarmShapeTool();
	_scene->setAudio(std::make_shared<AudioTrack>(std::move(track)));
	_scene->setAudioSelected(true);
}

void Paint::addMedia(Storage::PhotoEditorMedia &&media) {
	const auto &image = media.image;
	if (!media.audio.empty() && _composeSound) {
		addAudio(std::move(media.audio));
		return;
	} else if (!media
		|| !media.audio.empty()
		|| (media.video() && !_composeAnimated)
		|| !Ui::ValidateThumbDimensions(image.width(), image.height())) {
		_controllers->show->showBox(
			Ui::MakeInformBox(tr::lng_edit_media_invalid_file()));
		return;
	} else if (media.video()) {
		addVideoItem(std::move(media));
	} else {
		addImageItem(std::move(media.image));
	}
}

void Paint::addVideoItem(Storage::PhotoEditorMedia &&media) {
	const auto data = mediaItemData(media.image.size());
	addMediaItem(std::make_shared<ItemVideo>(
		std::make_shared<VideoClipSource>(VideoClipSource{
			.path = std::move(media.videoPath),
			.content = std::move(media.videoContent),
			.thumbnail = std::move(media.image),
			.duration = media.videoDuration,
			.hasAudio = media.videoHasAudio && _composeSound,
		}),
		data));
}

void Paint::addImageItem(QImage &&image) {
	const auto maxSide = std::max(_imageSize.width(), _imageSize.height());
	if (image.width() > maxSide || image.height() > maxSide) {
		image = image.scaled(
			maxSide,
			maxSide,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
	}
	auto data = mediaItemData(image.size());
	data.maxSizeRatio = kImageMaxSizeRatio;
	addMediaItem(std::make_shared<ItemImage>(
		Ui::PixmapFromImage(std::move(image)),
		data));
}

void Paint::addMediaItem(std::shared_ptr<ItemBase> item) {
	disarmShapeTool();
	_scene->addItem(item);
	_scene->clearSelection();
	item->setSelected(true);
	item->setFocus();
	_view->setFocus();
}

void Paint::setCanvasBackground(
		const Media::Encode::CanvasBackground &background) {
	_background = background;
}

void Paint::setCropRect(QRectF crop) {
	_cropRect = crop;
}

void Paint::paintCanvas(QPainter &p) const {
	const auto image = QRectF(Rect(_imageSize));
	if (_view->geometry().isEmpty()
		|| !_background.valid()
		|| image.contains(_canvas)) {
		return;
	}
	const auto transform = _view->viewportTransform();
	const auto imageDisplay = transform.mapRect(image).toRect();
	const auto cropDisplay = transform.mapRect(_cropRect).toAlignedRect();
	p.save();
	p.translate(pos());
	p.setClipRegion(
		QRegion(rect()) - QRegion(imageDisplay),
		Qt::IntersectClip);
	Media::Encode::PaintCanvasBackground(p, cropDisplay, _background);
	p.restore();
}

void Paint::paintImage(QPainter &p, const QPixmap &image) const {
	if (_view->geometry().isEmpty()) {
		return;
	}
	p.save();
	p.setClipRect(geometry(), Qt::IntersectClip);
	p.translate(pos());
	p.setTransform(_view->viewportTransform(), true);
	p.drawPixmap(Rect(_imageSize), image);
	p.restore();
}

void Paint::resetView() {
	_zoomAnimation.stop();
	_zoomTarget = kMinCanvasZoom;
	if (_transform.userZoom == kMinCanvasZoom) {
		return;
	}
	_transform.userZoom = kMinCanvasZoom;
	updateViewGeometry();
	applyViewTransform();
	_view->centerOn(rect::center(_canvas));
	if (const auto parent = parentWidget()) {
		parent->update(geometry());
	}
}

ItemBase::Data Paint::itemBaseData() const {
	const auto s = _scene->sceneRect().toRect().size();
	const auto size = std::min(s.width(), s.height()) / 2;
	const auto center = rect::center(_scene->canvasRect().toRect());
	return ItemBase::Data{
		.initialZoom = _transform.zoom,
		.zPtr = _scene->lastZ(),
		.size = size,
		.x = center.x(),
		.y = center.y(),
		.flipped = _transform.flipped,
		.rotation = -_transform.angle,
		.imageSize = _imageSize,
	};
}

ItemBase::Data Paint::messageItemData(QSize bubbleSize) const {
	auto result = itemBaseData();
	if (bubbleSize.isEmpty()) {
		return result;
	}
	const auto scene = _scene->sceneRect().size();
	const auto scale = scene.width()
		/ float64(st::photoEditorMessageReferenceWidth);
	const auto width = bubbleSize.width() * scale;
	const auto height = bubbleSize.height() * scale;
	const auto fit = std::min({
		1.,
		scene.width() * kMessageMaxWidthRatio / width,
		scene.height() * kMessageMaxHeightRatio / height,
	});
	result.size = std::max(int(std::ceil(width * fit)), 1);
	return result;
}

ItemBase::Data Paint::mediaItemData(QSize mediaSize) const {
	auto result = itemBaseData();
	if (mediaSize.isEmpty()) {
		return result;
	}
	const auto scene = _scene->sceneRect().size();
	const auto aspect = mediaSize.width() / float64(mediaSize.height());
	const auto width = (aspect > 1.)
		? std::floor(scene.width() * kMediaSizeRatio)
		: std::floor(scene.height() * kMediaSizeRatio) * aspect;
	result.size = int(std::min({
		width,
		scene.width(),
		scene.height() * aspect,
	}));
	return result;
}

void Paint::applyViewTransform() {
	_view->setTransform(QTransform()
		.scale(
			_transform.ratioW * _transform.userZoom,
			_transform.ratioH * _transform.userZoom)
		.rotate(_transform.angle));
	_transform.zoom = _transform.fitZoom * _transform.userZoom;
	_scene->updateZoom(_transform.zoom);
}

bool Paint::eventFilter(QObject *obj, QEvent *e) {
	if (obj != _viewport) {
		return RpWidget::eventFilter(obj, e);
	}
	const auto view = _view.get();
	if (!view || !_viewport) {
		return true;
	}
	if (e->type() == QEvent::Wheel) {
		const auto wheel = static_cast<QWheelEvent*>(e);
		const auto raw = wheel->angleDelta();
		const auto delta = raw.y() ? raw.y() : raw.x();
		if (!delta) {
			return true;
		}

		if (_fixedCrop) {
			zoomSceneItems(
				delta,
				wheel->modifiers().testFlag(Qt::ShiftModifier));
			return true;
		}
		const auto step = delta / float64(QWheelEvent::DefaultDeltasPerStep);
		zoomCanvas(
			std::pow(kCanvasZoomStep, step),
			wheel->position().toPoint(),
			false);
		return true;
	} else if (e->type() == QEvent::NativeGesture) {
		const auto gesture = static_cast<QNativeGestureEvent*>(e);
		if (gesture->gestureType() != Qt::ZoomNativeGesture) {
			return RpWidget::eventFilter(obj, e);
		}
		const auto factor = 1. + gesture->value();
		if (_fixedCrop) {
			zoomSceneItemsByFactor(factor);
		} else {
			zoomCanvas(factor, gesture->pos(), true);
		}
		return true;
	} else if (e->type() == QEvent::MouseButtonPress) {
		const auto mouse = static_cast<QMouseEvent*>(e);
		if (mouse->button() == Qt::MiddleButton) {
			_pan = {
				.active = (_fixedCrop
					|| _transform.userZoom > kMinCanvasZoom),
				.point = mouse->pos(),
			};
			if (_pan.active) {
				_viewport->setCursor(Qt::ClosedHandCursor);
			}
			return true;
		}
	} else if (e->type() == QEvent::MouseMove) {
		const auto mouse = static_cast<QMouseEvent*>(e);
		if (_pan.active) {
			const auto point = mouse->pos();
			const auto delta = point - _pan.point;
			_pan.point = point;

			if (_fixedCrop) {
				panSceneItems(mapWidgetDeltaToScene(delta));
			} else if (_transform.userZoom > kMinCanvasZoom) {
				view->horizontalScrollBar()->setValue(
					view->horizontalScrollBar()->value() - delta.x());
				view->verticalScrollBar()->setValue(
					view->verticalScrollBar()->value() - delta.y());
				if (const auto parent = parentWidget()) {
					parent->update(geometry());
				}
			}
			return true;
		}
	} else if (e->type() == QEvent::MouseButtonRelease) {
		const auto mouse = static_cast<QMouseEvent*>(e);
		if (mouse->button() == Qt::MiddleButton) {
			if (_pan.active) {
				_viewport->unsetCursor();
			}
			_pan.active = false;
			return true;
		}
	}
	return RpWidget::eventFilter(obj, e);
}

} // namespace Editor
