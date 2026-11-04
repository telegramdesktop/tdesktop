/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/photo_editor.h"

#include "calls/calls_instance.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "api/api_stories.h"
#include "editor/color_picker.h"
#include "editor/controllers/controllers.h"
#include "editor/editor_caption_bar.h"
#include "editor/photo_editor_content.h"
#include "editor/photo_editor_controls.h"
#include "lang/lang_keys.h"
#include "media/audio/media_audio_capture.h"
#include "ui/controls/round_video_recorder.h"
#include "webrtc/webrtc_video_track.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "ui/layers/layer_manager.h"
#include "ui/layers/layer_widget.h"
#include "ui/wrap/fade_wrap.h"
#include "styles/style_calls.h"
#include "styles/style_editor.h"

#include <tgcalls/VideoCaptureInterface.h>

namespace Editor {
namespace {

constexpr auto kPrecision = 100000;
constexpr auto kBrushesVersion = -2;
constexpr auto kDefaultBrushSizeRatio = 0.9;

constexpr auto kMaxRecordingAmplitude = 1800.;

[[nodiscard]] float64 RecordingAmplitude(
		const ::Media::Capture::Chunk &chunk) {
	const auto &samples = chunk.samples;
	const auto count = int(samples.size() / sizeof(int16));
	if (!count) {
		return 0.;
	}
	const auto data = reinterpret_cast<const int16*>(samples.constData());
	auto sum = 0.;
	for (const auto value : gsl::make_span(data, count)) {
		sum += float64(value) * value;
	}
	const auto amplitude = std::sqrt(sum / count) / 2.;
	return std::clamp(amplitude / kMaxRecordingAmplitude, 0., 1.);
}

[[nodiscard]] std::unique_ptr<Ui::LayerManager> MakeLayers(
		not_null<Ui::RpWidget*> parent) {
	auto result = std::make_unique<Ui::LayerManager>(parent);
	result->setStyleOverrides(&st::groupCallBox, &st::groupCallLayerBox);
	result->setHideByBackgroundClick(true);
	return result;
}

[[nodiscard]] int ToolIndex(Brush::Tool tool) {
	switch (tool) {
	case Brush::Tool::Pen: return 0;
	case Brush::Tool::Arrow: return 1;
	case Brush::Tool::Marker: return 2;
	case Brush::Tool::Blur: return 3;
	case Brush::Tool::Eraser: return 4;
	}
	return 0;
}

[[nodiscard]] Brush::Tool ToolFromIndex(int index) {
	switch (index) {
	case 0: return Brush::Tool::Pen;
	case 1: return Brush::Tool::Arrow;
	case 2: return Brush::Tool::Marker;
	case 3: return Brush::Tool::Blur;
	case 4: return Brush::Tool::Eraser;
	}
	return Brush::Tool::Pen;
}

[[nodiscard]] Brush::Tool ToolFromSerialized(qint32 value) {
	switch (value) {
	case int(Brush::Tool::Pen): return Brush::Tool::Pen;
	case int(Brush::Tool::Arrow): return Brush::Tool::Arrow;
	case int(Brush::Tool::Marker): return Brush::Tool::Marker;
	case int(Brush::Tool::Eraser): return Brush::Tool::Eraser;
	case int(Brush::Tool::Blur): return Brush::Tool::Blur;
	}
	return Brush::Tool::Pen;
}

[[nodiscard]] QColor DefaultBrushColor(Brush::Tool tool) {
	switch (tool) {
	case Brush::Tool::Pen: return QColor(234, 39, 57);
	case Brush::Tool::Arrow: return QColor(252, 150, 77);
	case Brush::Tool::Marker: return QColor(252, 222, 101);
	case Brush::Tool::Eraser: return QColor(0, 0, 0);
	case Brush::Tool::Blur: return QColor(0, 0, 0);
	}
	return QColor(234, 39, 57);
}

[[nodiscard]] Brush DefaultBrush(Brush::Tool tool) {
	auto result = Brush();
	result.sizeRatio = kDefaultBrushSizeRatio;
	result.color = DefaultBrushColor(tool);
	result.tool = tool;
	return result;
}

[[nodiscard]] std::array<Brush, 5> DefaultBrushes() {
	auto result = std::array<Brush, 5>();
	for (auto i = 0; i != int(result.size()); ++i) {
		const auto tool = ToolFromIndex(i);
		result[i] = DefaultBrush(tool);
	}
	return result;
}

struct BrushState {
	std::array<Brush, 5> brushes = DefaultBrushes();
	Brush::Tool tool = Brush::Tool::Pen;
	bool fillShapes = false;
	TextPrefs textPrefs;
};

[[nodiscard]] TextStyle TextStyleFromSerialized(qint32 value) {
	switch (value) {
	case int(TextStyle::Framed): return TextStyle::Framed;
	case int(TextStyle::SemiTransparent): return TextStyle::SemiTransparent;
	case int(TextStyle::Plain): return TextStyle::Plain;
	case int(TextStyle::Opaque): return TextStyle::Opaque;
	}
	return TextStyle::Plain;
}

[[nodiscard]] TextTypeface TypefaceFromSerialized(qint32 value) {
	switch (value) {
	case int(TextTypeface::Default): return TextTypeface::Default;
	case int(TextTypeface::Italic): return TextTypeface::Italic;
	case int(TextTypeface::Serif): return TextTypeface::Serif;
	case int(TextTypeface::Condensed): return TextTypeface::Condensed;
	case int(TextTypeface::Monospace): return TextTypeface::Monospace;
	}
	return TextTypeface::Default;
}

[[nodiscard]] TextAlignment AlignmentFromSerialized(qint32 value) {
	switch (value) {
	case int(TextAlignment::Center): return TextAlignment::Center;
	case int(TextAlignment::Left): return TextAlignment::Left;
	case int(TextAlignment::Right): return TextAlignment::Right;
	}
	return TextAlignment::Center;
}

[[nodiscard]] QByteArray Serialize(
		const std::array<Brush, 5> &brushes,
		Brush::Tool tool,
		bool fillShapes,
		const TextPrefs &textPrefs) {
	auto result = QByteArray();
	auto stream = QDataStream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_3);
	stream
		<< qint32(kBrushesVersion)
		<< qint32(int(tool))
		<< qint32(brushes.size());
	for (auto i = 0; i != int(brushes.size()); ++i) {
		const auto tool = ToolFromIndex(i);
		const auto &brush = brushes[i];
		stream
			<< qint32(int(tool))
			<< qint32(brush.sizeRatio * kPrecision)
			<< brush.color;
	}
	stream << qint32(fillShapes ? 1 : 0);
	stream
		<< qint32(int(textPrefs.style))
		<< qint32(int(textPrefs.typeface))
		<< qint32(int(textPrefs.alignment))
		<< qint32(textPrefs.sizeRatio * kPrecision);
	stream.device()->close();

	return result;
}

[[nodiscard]] BrushState Deserialize(const QByteArray &data) {
	auto result = BrushState();
	if (data.isEmpty()) {
		return result;
	}
	auto stream = QDataStream(data);
	auto head = qint32(0);
	stream >> head;
	if (stream.status() != QDataStream::Ok) {
		return result;
	}
	if (head < 0) {
		const auto version = head;
		auto toolValue = qint32(int(Brush::Tool::Pen));
		auto count = qint32(0);
		stream >> toolValue >> count;
		if (stream.status() != QDataStream::Ok) {
			return result;
		}
		result.tool = ToolFromSerialized(toolValue);
		auto limit = int(count);
		if (limit < 0) {
			limit = 0;
		} else if (limit > int(result.brushes.size())) {
			limit = int(result.brushes.size());
		}
		for (auto i = 0; i != limit; ++i) {
			auto entryTool = qint32(int(Brush::Tool::Pen));
			auto size = qint32(0);
			auto color = QColor();
			stream >> entryTool >> size >> color;
			if (stream.status() != QDataStream::Ok) {
				return result;
			}
			const auto tool = ToolFromSerialized(entryTool);
			const auto index = ToolIndex(tool);
			if (version == kBrushesVersion && size > 0) {
				result.brushes[index].sizeRatio = size / float64(kPrecision);
			}
			if (color.isValid()) {
				result.brushes[index].color = color;
			}
			result.brushes[index].tool = tool;
		}
		if (!stream.atEnd()) {
			auto fillShapes = qint32(0);
			stream >> fillShapes;
			if (stream.status() == QDataStream::Ok) {
				result.fillShapes = (fillShapes == 1);
			}
		}
		if (!stream.atEnd()) {
			auto style = qint32(0);
			auto typeface = qint32(0);
			auto alignment = qint32(0);
			auto sizeRatio = qint32(0);
			stream >> style >> typeface >> alignment >> sizeRatio;
			if (stream.status() == QDataStream::Ok) {
				result.textPrefs = {
					.style = TextStyleFromSerialized(style),
					.typeface = TypefaceFromSerialized(typeface),
					.alignment = AlignmentFromSerialized(alignment),
					.sizeRatio = (sizeRatio > 0)
						? (sizeRatio / float64(kPrecision))
						: 0.,
				};
			}
		}
		return result;
	}
	auto color = QColor();
	stream >> color;
	if (stream.status() != QDataStream::Ok) {
		return result;
	}
	auto toolValue = qint32(int(Brush::Tool::Pen));
	if (!stream.atEnd()) {
		stream >> toolValue;
		if (stream.status() != QDataStream::Ok) {
			toolValue = qint32(int(Brush::Tool::Pen));
		}
	}
	const auto tool = ToolFromSerialized(toolValue);
	const auto index = ToolIndex(tool);
	if (color.isValid()) {
		result.brushes[index].color = color;
	}
	result.brushes[index].tool = tool;
	result.tool = tool;
	return result;
}

} // namespace

PhotoEditor::PhotoEditor(
	not_null<QWidget*> parent,
	not_null<Window::Controller*> controller,
	std::shared_ptr<Image> photo,
	PhotoModifications modifications,
	EditorData data)
: PhotoEditor(
	parent,
	controller->uiShow(),
	(controller->sessionController()
		? controller->sessionController()->uiShow()
		: nullptr),
	std::move(photo),
	std::move(modifications),
	std::move(data)) {
}

PhotoEditor::PhotoEditor(
	not_null<QWidget*> parent,
	std::shared_ptr<Ui::Show> show,
	std::shared_ptr<ChatHelpers::Show> sessionShow,
	std::shared_ptr<Image> photo,
	PhotoModifications modifications,
	EditorData data)
: RpWidget(parent)
, _modifications(std::move(modifications))
, _initialAngle(_modifications.angle)
, _initialFlipped(_modifications.flipped)
, _layers(MakeLayers(this))
, _controllers(std::make_shared<Controllers>(
	sessionShow
		? std::make_unique<StickersPanelController>(
			this,
			sessionShow,
			data.composeSound,
			data.story.has_value())
		: nullptr,
	std::make_unique<UndoController>(),
	show,
	_layers->uiShow(),
	sessionShow))
, _content(base::make_unique_q<PhotoEditorContent>(
	this,
	photo,
	_modifications,
	_controllers,
	data))
, _controls(base::make_unique_q<PhotoEditorControls>(
	this,
	_controllers,
	_modifications,
	data,
	photo->size(),
	Deserialize(Core::App().settings().photoEditorBrush()).fillShapes))
, _captionBar(data.story
	? base::make_unique_q<Ui::FadeWrap<CaptionBar>>(
		this,
		object_ptr<CaptionBar>(this, this, *data.story))
	: nullptr)
, _brushes(Deserialize(Core::App().settings().photoEditorBrush()).brushes)
, _brushTool(Deserialize(Core::App().settings().photoEditorBrush()).tool)
, _shapesFilled(
	Deserialize(Core::App().settings().photoEditorBrush()).fillShapes)
, _textPrefs(Deserialize(Core::App().settings().photoEditorBrush()).textPrefs)
, _colorPicker(std::make_unique<ColorPicker>(
	this,
	std::move(show),
	_brushes,
	_brushTool))
, _hasSaveButton(data.hasSaveButton || data.story.has_value()) {
	_modifications.cropType = data.cropType;
	_modifications.cropMode = data.cropMode;
	if (data.story && !data.story->videoObject.empty()) {
		_content->addVideoObject(std::move(data.story->videoObject));
	}

	events(
	) | rpl::on_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::WindowDeactivate) {
			_content->setExpansionRoom(false);
		}
	}, lifetime());

	sizeValue(
	) | rpl::on_next([=](const QSize &size) {
		if (size.isEmpty()) {
			return;
		}
		layoutContent();
	}, lifetime());
	if (_captionBar) {
		const auto bar = _captionBar->entity();
		bar->heightValue() | rpl::skip(1) | rpl::on_next([=] {
			layoutContent();
		}, bar->lifetime());
		bar->submits() | rpl::on_next([=] {
			if (_mode.current().mode == PhotoEditorMode::Mode::Transform) {
				done();
			}
		}, bar->lifetime());
		bar->cancels() | rpl::on_next([=] {
			setFocus();
		}, bar->lifetime());
		bar->roundRequests() | rpl::on_next([=] {
			toggleRoundRecording();
		}, bar->lifetime());
		bar->roundStopRequests() | rpl::on_next([=] {
			stopRoundRecording(true);
		}, bar->lifetime());
		bar->roundCancelRequests() | rpl::on_next([=] {
			stopRoundRecording(false);
		}, bar->lifetime());
	}

	_content->videoClipSelections(
	) | rpl::on_next([=](std::shared_ptr<VideoClip> clip) {
		_videoClipSelected = (clip != nullptr);
		updateColorPickerVisibility(anim::type::normal);
		_controls->setVideoClip(std::move(clip));
		_controls->setTrimShortestAvailable(
			_content->canEqualizeDurations());
		_controls->setTrimShortestActive(
			_content->durationsLinked(),
			anim::type::instant);
	}, lifetime());

	rpl::merge(
		_content->audioSelectedChanges(),
		_content->audioChanges() | rpl::map([=] {
			return _content->audioSelected();
		})
	) | rpl::on_next([=](bool selected) {
		_audioSelected = selected;
		updateColorPickerVisibility(anim::type::normal);
		_controls->setAudioTrack(selected ? _content->audio() : nullptr);
		_controls->setTrimShortestAvailable(
			_content->canEqualizeDurations());
		_controls->setTrimShortestActive(
			_content->durationsLinked(),
			anim::type::instant);
	}, lifetime());

	_content->audioVolumeChanges(
	) | rpl::on_next([=] {
		_controls->refreshAudioVolume();
	}, lifetime());

	_content->durationsLinkChanges(
	) | rpl::on_next([=] {
		_controls->setTrimShortestAvailable(
			_content->canEqualizeDurations());
		_controls->setTrimShortestActive(
			_content->durationsLinked(),
			anim::type::instant);
		_controls->refreshTimelines();
	}, lifetime());

	_controls->audioRemoveRequests(
	) | rpl::on_next([=] {
		_content->removeAudio();
	}, lifetime());

	_controls->trimShortestRequests(
	) | rpl::on_next([=] {
		const auto linked = !_content->durationsLinked();
		_content->setDurationsLinked(linked);
		_controls->setTrimShortestActive(linked, anim::type::normal);
		_controls->refreshTimelines();
	}, lifetime());

	_controls->trimLengthChanges(
	) | rpl::on_next([=](crl::time length) {
		if (!_content->durationsLinked() || _matchingDurations) {
			return;
		}
		_matchingDurations = true;
		_content->matchDurations(length);
		_controls->refreshTimelines();
		_matchingDurations = false;
	}, lifetime());

	_content->innerRect(
	) | rpl::on_next([=](QRect inner) {
		if (inner.isEmpty()) {
			return;
		}
		_colorPicker->setCanvasRect(inner.translated(_content->pos()));
		const auto innerTop = _content->y() + inner.top();
		const auto skip = st::photoEditorCropPointSize;
		const auto controlsRect = rect()
			- style::margins(0, innerTop + inner.height() + skip, 0, 0);
		_controls->setGeometry(controlsRect);
	}, lifetime());

	_controls->colorLinePositionValue(
	) | rpl::on_next([=](const QPoint &p) {
		_colorPicker->moveLine(p);
	}, _controls->lifetime());

	_controls->colorLineShownValue(
	) | rpl::on_next([=](bool shown) {
		_colorLineShown = shown;
		updateColorPickerVisibility(anim::type::instant);
	}, _controls->lifetime());

	_mode.value(
	) | rpl::on_next([=](const PhotoEditorMode &mode) {
		_content->applyMode(mode);
		_controls->applyMode(mode);
		if (_captionBar) {
			_captionBar->toggle(
				(mode.mode == PhotoEditorMode::Mode::Transform),
				anim::type::normal);
		}
	}, lifetime());

	_controls->rotateRequests(
	) | rpl::on_next([=](int angle) {
		_modifications.angle += 90;
		if (_modifications.angle >= 360) {
			_modifications.angle -= 360;
		}
		_content->applyModifications(_modifications);
	}, lifetime());

	_controls->flipRequests(
	) | rpl::on_next([=] {
		_modifications.flipped = !_modifications.flipped;
		_content->applyModifications(_modifications);
	}, lifetime());

	_controls->aspectRatioChanges() | rpl::on_next([=](float64 ratio) {
		_content->applyAspectRatio(ratio);
	}, lifetime());

	_controls->cornersLevelChanges(
	) | rpl::on_next([=](RoundedCornersLevel level) {
		_modifications.cornersLevel = level;
		_content->applyModifications(_modifications);
	}, lifetime());

	rpl::merge(
		_controls->paintModeRequests(),
		_content->paintModeRequests()
	) | rpl::on_next([=] {
		if (_roundRecording) {
			return;
		}
		_mode = PhotoEditorMode{
			.mode = PhotoEditorMode::Mode::Paint,
			.action = PhotoEditorMode::Action::None,
		};
	}, lifetime());

	_controls->textRequests(
	) | rpl::on_next([=] {
		_content->createTextItem();
	}, lifetime());

	_controls->shapeRequests(
	) | rpl::on_next([=](const ShapeRequest &request) {
		if (request.action == ShapeRequest::Action::Cancel) {
			_content->disarmShapeTool();
			return;
		}
		const auto tool = ((_brushTool == Brush::Tool::Eraser)
			|| (_brushTool == Brush::Tool::Blur))
			? Brush::Tool::Pen
			: _brushTool;
		const auto &brush = _brushes[ToolIndex(tool)];
		if (request.action == ShapeRequest::Action::Immediate) {
			_content->createShapeItem(request.shape, brush, _shapesFilled);
		} else {
			_content->armShapeTool(request.shape, brush, _shapesFilled);
		}
	}, lifetime());

	_content->shapeToolStates(
	) | rpl::on_next([=](bool active) {
		_controls->setShapeToolActive(active);
	}, lifetime());

	_controls->shapesFillChanges(
	) | rpl::on_next([=](bool fill) {
		_shapesFilled = fill;
		const auto serialized = Serialize(
			_brushes,
			_brushTool,
			_shapesFilled,
			_textPrefs);
		if (Core::App().settings().photoEditorBrush() != serialized) {
			Core::App().settings().setPhotoEditorBrush(serialized);
			Core::App().saveSettingsDelayed();
		}
	}, lifetime());

	_controls->doneRequests(
	) | rpl::on_next([=] {
		_controls->commitTimelineEdits();
		done();
	}, lifetime());

	_controls->saveRequests(
	) | rpl::on_next([=] {
		_controls->commitTimelineEdits();
		_content->save(_modifications);
		_save.fire_copy(_modifications);
	}, lifetime());

	_controls->cancelRequests(
	) | rpl::on_next([=] {
		const auto mode = _mode.current().mode;
		if (mode == PhotoEditorMode::Mode::Paint) {
			_mode = PhotoEditorMode{
				.mode = PhotoEditorMode::Mode::Transform,
				.action = PhotoEditorMode::Action::Discard,
			};
		} else if (mode == PhotoEditorMode::Mode::Transform) {
			if (_captionBar) {
				_cancel.fire({});
			} else {
				_mode = PhotoEditorMode{
					.mode = PhotoEditorMode::Mode::Out,
					.action = PhotoEditorMode::Action::Discard,
				};
				_cancel.fire({});
			}
		}
	}, lifetime());

	_colorPicker->toolClicks(
	) | rpl::on_next([=] {
		_content->disarmShapeTool();
		_content->clearSelection();
	}, lifetime());

	_colorPicker->saveBrushRequests(
	) | rpl::on_next([=](const Brush &brush) {
		if (_textItemSelected || _textEditing) {
			_content->setSelectedTextColor(brush.color);
			_content->setTextColor(brush.color);
		} else if (_shapeItemSelected) {
			_content->applyBrushToSelectedShape(brush);
		} else {
			_content->applyBrush(brush);
			_content->setTextColor(brush.color);

			_brushTool = brush.tool;
			_brushes[ToolIndex(brush.tool)] = brush;
			const auto serialized = Serialize(
				_brushes,
				_brushTool,
				_shapesFilled,
				_textPrefs);
			if (Core::App().settings().photoEditorBrush() != serialized) {
				Core::App().settings().setPhotoEditorBrush(serialized);
				Core::App().saveSettingsDelayed();
			}
		}
	}, lifetime());

	_content->applyTextPrefs(_textPrefs);
	_content->textPrefsUsed(
	) | rpl::on_next([=](const TextPrefs &prefs) {
		if (_textPrefs == prefs) {
			return;
		}
		_textPrefs = prefs;
		const auto serialized = Serialize(
			_brushes,
			_brushTool,
			_shapesFilled,
			_textPrefs);
		if (Core::App().settings().photoEditorBrush() != serialized) {
			Core::App().settings().setPhotoEditorBrush(serialized);
			Core::App().saveSettingsDelayed();
		}
	}, lifetime());

	_content->textEditStates(
	) | rpl::on_next([=](bool editing) {
		_textEditing = editing;
		if (_textEditing) {
			_colorPicker->setToolSelectionVisible(false);
		} else if (!_textItemSelected && !_shapeItemSelected) {
			const auto &brush = _brushes[ToolIndex(_brushTool)];
			_colorPicker->setColor(brush.color);
			_colorPicker->setToolSelectionVisible(true);
		}
	}, lifetime());

	_content->textColorRequests(
	) | rpl::on_next([=](const QColor &color) {
		_colorPicker->setColor(color);
	}, lifetime());

	_content->textItemSelections(
	) | rpl::on_next([=](const QColor &color) {
		_textItemSelected = true;
		_colorPicker->setToolSelectionVisible(false);
		_colorPicker->setColor(color);
	}, lifetime());

	_content->textItemDeselections(
	) | rpl::on_next([=] {
		_textItemSelected = false;
		if (_textEditing || _shapeItemSelected) {
			return;
		}
		const auto &brush = _brushes[ToolIndex(_brushTool)];
		_colorPicker->setColor(brush.color);
		_colorPicker->setToolSelectionVisible(true);
	}, lifetime());

	_content->shapeItemSelections(
	) | rpl::on_next([=](const QColor &color) {
		_shapeItemSelected = true;
		_colorPicker->setToolSelectionVisible(false);
		_colorPicker->setColor(color);
	}, lifetime());

	_content->shapeItemDeselections(
	) | rpl::on_next([=] {
		_shapeItemSelected = false;
		if (_textEditing || _textItemSelected) {
			return;
		}
		const auto &brush = _brushes[ToolIndex(_brushTool)];
		_colorPicker->setColor(brush.color);
		_colorPicker->setToolSelectionVisible(true);
	}, lifetime());
}

PhotoEditor::~PhotoEditor() {
	// ~Paint fires shape tool states, don't handle them in a dying widget.
	lifetime().destroy();
	stopRoundRecording(false);
}

void PhotoEditor::keyPressEvent(QKeyEvent *e) {
	if (_roundRecording && e->key() == Qt::Key_Escape) {
		stopRoundRecording(false);
		return;
	}
	if (_hasSaveButton
		&& _saveEnabled
		&& !_roundRecording
		&& e->matches(QKeySequence::Save)) {
		_controls->commitTimelineEdits();
		_content->save(_modifications);
		_save.fire_copy(_modifications);
		return;
	}
	_content->setExpansionRoom(e->modifiers().testFlag(Qt::ControlModifier));
	if (!_colorPicker->preventHandleKeyPress()) {
		_content->handleKeyPress(e) || _controls->handleKeyPress(e);
	}
}

void PhotoEditor::keyReleaseEvent(QKeyEvent *e) {
	_content->setExpansionRoom(e->modifiers().testFlag(Qt::ControlModifier));
}

void PhotoEditor::updateColorPickerVisibility(anim::type animated) {
	const auto painting
		= (_mode.current().mode == PhotoEditorMode::Mode::Paint);
	_colorPicker->setVisible(
		painting
			&& _colorLineShown
			&& !_videoClipSelected
			&& !_audioSelected,
		animated);
}

void PhotoEditor::save() {
	_content->save(_modifications);
	_done.fire_copy(_modifications);
}

void PhotoEditor::done() {
	const auto mode = _mode.current().mode;
	if (_roundRecording) {
		return;
	} else if (mode == PhotoEditorMode::Mode::Paint) {
		_mode = PhotoEditorMode{
			.mode = PhotoEditorMode::Mode::Transform,
			.action = PhotoEditorMode::Action::Save,
		};
	} else if (mode != PhotoEditorMode::Mode::Transform) {
		return;
	} else if (_captionBar) {
		_content->save(_modifications);
		_done.fire_copy(_modifications);
	} else {
		_mode = PhotoEditorMode{
			.mode = PhotoEditorMode::Mode::Out,
			.action = PhotoEditorMode::Action::Save,
		};
		save();
	}
}

TextWithTags PhotoEditor::caption() const {
	return _captionBar ? _captionBar->entity()->caption() : TextWithTags();
}

int PhotoEditor::period() const {
	return _captionBar ? _captionBar->entity()->period() : 0;
}

void PhotoEditor::toggleRoundRecording() {
	if (_roundRecording) {
		stopRoundRecording(true);
	} else {
		startRoundRecording();
	}
}

void PhotoEditor::startRoundRecording() {
	if (_roundRecorder || !_captionBar) {
		return;
	}
	const auto hidden = [=](not_null<Ui::RoundVideoRecorder*> which) {
		if (_roundRecorder.get() == which) {
			_roundRecorder = nullptr;
		}
	};
	auto capturer = Core::App().calls().getVideoCapture();
	auto track = std::make_shared<Webrtc::VideoTrack>(
		Webrtc::VideoState::Active);
	capturer->setOutput(track->sink());
	capturer->setPreferredAspectRatio(1.);
	_roundLifetime = track->stateValue(
	) | rpl::on_next([=](Webrtc::VideoState state) {
		capturer->setState((state == Webrtc::VideoState::Active)
			? tgcalls::VideoState::Active
			: tgcalls::VideoState::Inactive);
	});
	_roundRecorder = std::make_unique<Ui::RoundVideoRecorder>(
		Ui::RoundVideoRecorderDescriptor{
			.container = this,
			.hiding = [](not_null<Ui::RoundVideoRecorder*>) {},
			.hidden = hidden,
			.capturer = std::move(capturer),
			.track = std::move(track),
		});
	_roundRecording = true;
	_captionBar->entity()->setRoundRecording(true);
	const auto processor = _roundRecorder->audioChunkProcessor();
	const auto weak = base::make_weak(this);
	::Media::Capture::instance()->start([=](::Media::Capture::Chunk chunk) {
		const auto amplitude = RecordingAmplitude(chunk);
		crl::on_main(weak, [=] {
			if (_captionBar) {
				_captionBar->entity()->setRoundRecordingAmplitude(amplitude);
			}
		});
		processor(std::move(chunk));
	});
	_roundRecorder->updated(
	) | rpl::on_next_error([=](const Ui::RoundVideoRecorder::Update &update) {
		if (update.finished) {
			stopRoundRecording(true);
		}
	}, [=](Ui::RoundVideoRecorder::Error) {
		stopRoundRecording(false);
		_controllers->show->showToast(
			tr::lng_story_round_failed(tr::now));
	}, _roundLifetime);
}

void PhotoEditor::stopRoundRecording(bool save) {
	if (!_roundRecording) {
		return;
	}
	_roundRecording = false;
	if (_captionBar) {
		_captionBar->entity()->setRoundRecording(false);
	}
	if (const auto recorder = _roundRecorder.get()) {
		if (save) {
			const auto weak = base::make_weak(this);
			recorder->hide([=](Ui::RoundVideoResult data) {
				crl::on_main(weak, [=, data = std::move(data)]() mutable {
					if (data.content.isEmpty()) {
						return;
					}
					_mode = PhotoEditorMode{
						.mode = PhotoEditorMode::Mode::Paint,
						.action = PhotoEditorMode::Action::None,
					};
					_content->addRoundVideo(
						std::move(data.content),
						data.duration);
				});
			});
		} else {
			recorder->hide();
		}
	}
	::Media::Capture::instance()->stop();
}

bool PhotoEditor::edited() const {
	return (_modifications.angle != _initialAngle)
		|| (_modifications.flipped != _initialFlipped)
		|| _content->edited();
}

PhotoModifications PhotoEditor::collect() {
	_content->save(_modifications);
	return _modifications;
}

void PhotoEditor::layoutContent() {
	_content->setGeometry(rect() - contentMargins());
	if (!_captionBar) {
		return;
	}
	const auto barWidth = std::min(
		int(st::photoEditorButtonBarWidth),
		width());
	_captionBar->resizeToWidth(barWidth);
	const auto top = height()
		- st::photoEditorControlsBottomSkip
		- st::photoEditorButtonBarHeight
		- st::photoEditorControlsCenterSkip
		- _captionBar->height();
	_captionBar->moveToLeft((width() - barWidth) / 2, top);
	_captionBar->entity()->updateRecordButtonGeometry();
}

QMargins PhotoEditor::contentMargins() const {
	auto result = QMargins(st::photoEditorContentMargins);
	if (_captionBar) {
		const auto slot = st::photoEditorControlsHeight
			- st::photoEditorControlsBottomSkip
			- st::photoEditorButtonBarHeight
			- st::photoEditorControlsCenterSkip;
		result.setBottom(result.bottom()
			+ std::max(_captionBar->height() - slot, 0));
	}
	return result;
}

rpl::producer<PhotoModifications> PhotoEditor::doneRequests() const {
	return _done.events();
}

rpl::producer<> PhotoEditor::cancelRequests() const {
	return _cancel.events();
}

rpl::producer<PhotoModifications> PhotoEditor::saveRequests() const {
	return _save.events();
}

void PhotoEditor::setSaveEnabled(bool enabled) {
	_saveEnabled = enabled;
	_controls->setSaveEnabled(enabled);
}

void InitEditorLayer(
		not_null<Ui::LayerWidget*> layer,
		not_null<PhotoEditor*> editor,
		Fn<void(PhotoModifications)> doneCallback) {
	editor->cancelRequests(
	) | rpl::on_next([=] {
		layer->closeLayer();
	}, editor->lifetime());

	const auto weak = base::make_weak(layer.get());
	editor->doneRequests(
	) | rpl::on_next([=, done = std::move(doneCallback)](
			const PhotoModifications &mods) {
		done(mods);
		if (const auto strong = weak.get()) {
			strong->closeLayer();
		}
	}, editor->lifetime());
}

} // namespace Editor
