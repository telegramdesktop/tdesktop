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
#include "styles/style_editor.h"

namespace Editor {
namespace {

constexpr auto kPrecision = 100000;

[[nodiscard]] QByteArray Serialize(const Brush &brush) {
	auto result = QByteArray();
	auto stream = QDataStream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_3);
	stream << qint32(brush.sizeRatio * kPrecision) << brush.color;
	stream.device()->close();

	return result;
}

[[nodiscard]] Brush Deserialize(const QByteArray &data) {
	auto stream = QDataStream(data);
	auto result = Brush();
	auto size = qint32(0);
	stream >> size >> result.color;
	result.sizeRatio = size / float(kPrecision);
	return (stream.status() != QDataStream::Ok)
		? Brush()
		: result;
}

} // namespace

PhotoEditor::PhotoEditor(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::Controller*> controller,
	std::shared_ptr<Image> photo,
	PhotoModifications modifications,
	EditorData data)
: RpWidget(parent)
, _modifications(std::move(modifications))
, _controllers(std::make_shared<Controllers>(
	controller->sessionController()
		? std::make_unique<StickersPanelController>(
			this,
			controller->sessionController())
		: nullptr,
	std::make_unique<UndoController>(),
	[=] (object_ptr<Ui::BoxContent> c) { controller->show(std::move(c)); }))
, _content(base::make_unique_q<PhotoEditorContent>(
	this,
	photo,
	_modifications,
	_controllers,
	std::move(data)))
, _controls(base::make_unique_q<PhotoEditorControls>(
	this,
	_controllers,
	_modifications))
, _colorPicker(std::make_unique<ColorPicker>(
	this,
	Deserialize(Core::App().settings().photoEditorBrush()))) {

	sizeValue(
	) | rpl::start_with_next([=](const QSize &size) {
		if (size.isEmpty()) {
			return;
		}
		const auto geometry = QRect(QPoint(), size);
		const auto contentRect = geometry - st::photoEditorContentMargins;
		_content->setGeometry(contentRect);
		const auto contentBottom = contentRect.top() + contentRect.height();
		const auto controlsRect = geometry
			- style::margins(0, contentBottom, 0, 0);
		_controls->setGeometry(controlsRect);
	}, lifetime());

	_controls->colorLinePositionValue(
	) | rpl::start_with_next([=](const QPoint &p) {
		_colorPicker->moveLine(p);
	}, _controls->lifetime());

	_controls->colorLineShownValue(
	) | rpl::start_with_next([=](bool shown) {
		_colorPicker->setVisible(shown);
	}, _controls->lifetime());

	_mode.value(
	) | rpl::start_with_next([=](const PhotoEditorMode &mode) {
		_content->applyMode(mode);
		_controls->applyMode(mode);
	}, lifetime());

	_controls->rotateRequests(
	) | rpl::start_with_next([=](int angle) {
		_modifications.angle += 90;
		if (_modifications.angle >= 360) {
			_modifications.angle -= 360;
		}
		_content->applyModifications(_modifications);
	}, lifetime());

	_controls->flipRequests(
	) | rpl::start_with_next([=] {
		_modifications.flipped = !_modifications.flipped;
		_content->applyModifications(_modifications);
	}, lifetime());

	_controls->paintModeRequests(
	) | rpl::start_with_next([=] {
		_mode = PhotoEditorMode{
			.mode = PhotoEditorMode::Mode::Paint,
			.action = PhotoEditorMode::Action::None,
		};
	}, lifetime());

	_controls->doneRequests(
	) | rpl::start_with_next([=] {
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
	) | rpl::start_with_next([=] {
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
	) | rpl::start_with_next([=](const Brush &brush) {
		_content->applyBrush(brush);

		const auto serialized = Serialize(brush);
		if (Core::App().settings().photoEditorBrush() != serialized) {
			Core::App().settings().setPhotoEditorBrush(serialized);
			Core::App().saveSettingsDelayed();
		}
	}, lifetime());
}

void PhotoEditor::handleKeyPress(not_null<QKeyEvent*> e) {
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

} // namespace Editor
