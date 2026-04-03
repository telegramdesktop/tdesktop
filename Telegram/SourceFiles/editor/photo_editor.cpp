/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/photo_editor.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "editor/color_picker.h"
#include "editor/controllers/controllers.h"
#include "editor/photo_editor_content.h"
#include "editor/photo_editor_controls.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "ui/layers/layer_widget.h"
#include "styles/style_editor.h"

namespace Editor {
namespace {

constexpr auto kPrecision = 100000;
constexpr auto kBrushesVersion = -2;
constexpr auto kDefaultBrushSizeRatio = 0.9;

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
};

[[nodiscard]] QByteArray Serialize(
		const std::array<Brush, 5> &brushes,
		Brush::Tool tool) {
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
				result.brushes[index].sizeRatio = size / float(kPrecision);
			}
			if (color.isValid()) {
				result.brushes[index].color = color;
			}
			result.brushes[index].tool = tool;
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
, _controllers(std::make_shared<Controllers>(
	sessionShow
		? std::make_unique<StickersPanelController>(
			this,
			std::move(sessionShow))
		: nullptr,
	std::make_unique<UndoController>(),
	show))
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
	photo->size()))
, _brushes(Deserialize(Core::App().settings().photoEditorBrush()).brushes)
, _brushTool(Deserialize(Core::App().settings().photoEditorBrush()).tool)
, _colorPicker(std::make_unique<ColorPicker>(
	this,
	std::move(show),
	_brushes,
	_brushTool)) {

	sizeValue(
	) | rpl::on_next([=](const QSize &size) {
		if (size.isEmpty()) {
			return;
		}
		_content->setGeometry(rect() - st::photoEditorContentMargins);
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
		_colorPicker->setVisible(shown);
	}, _controls->lifetime());

	_mode.value(
	) | rpl::on_next([=](const PhotoEditorMode &mode) {
		_content->applyMode(mode);
		_controls->applyMode(mode);
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

	_controls->paintModeRequests(
	) | rpl::on_next([=] {
		_mode = PhotoEditorMode{
			.mode = PhotoEditorMode::Mode::Paint,
			.action = PhotoEditorMode::Action::None,
		};
	}, lifetime());

	_controls->doneRequests(
	) | rpl::on_next([=] {
		const auto mode = _mode.current().mode;
		if (mode == PhotoEditorMode::Mode::Paint) {
			_mode = PhotoEditorMode{
				.mode = PhotoEditorMode::Mode::Transform,
				.action = PhotoEditorMode::Action::Save,
			};
		} else if (mode == PhotoEditorMode::Mode::Transform) {
			_mode = PhotoEditorMode{
				.mode = PhotoEditorMode::Mode::Out,
				.action = PhotoEditorMode::Action::Save,
			};
			save();
		}
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
			_mode = PhotoEditorMode{
				.mode = PhotoEditorMode::Mode::Out,
				.action = PhotoEditorMode::Action::Discard,
			};
			_cancel.fire({});
		}
	}, lifetime());

	_colorPicker->saveBrushRequests(
	) | rpl::on_next([=](const Brush &brush) {
		_content->applyBrush(brush);

		_brushTool = brush.tool;
		_brushes[ToolIndex(brush.tool)] = brush;
		const auto serialized = Serialize(_brushes, _brushTool);
		if (Core::App().settings().photoEditorBrush() != serialized) {
			Core::App().settings().setPhotoEditorBrush(serialized);
			Core::App().saveSettingsDelayed();
		}
	}, lifetime());
}

void PhotoEditor::keyPressEvent(QKeyEvent *e) {
	if (!_colorPicker->preventHandleKeyPress()) {
		_content->handleKeyPress(e) || _controls->handleKeyPress(e);
	}
}

void PhotoEditor::save() {
	_content->save(_modifications);
	_done.fire_copy(_modifications);
}

rpl::producer<PhotoModifications> PhotoEditor::doneRequests() const {
	return _done.events();
}

rpl::producer<> PhotoEditor::cancelRequests() const {
	return _cancel.events();
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
