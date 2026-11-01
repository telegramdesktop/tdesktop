/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_keys_legend.h"

#include "lang/lang_keys.h"
#include "ui/controls/keys_legend.h"
#include "ui/ui_utility.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_editor.h"

namespace Editor {
namespace {

using Section = Ui::KeysLegend::Section;
using Row = Ui::KeysLegend::Row;

using Ui::KeysLegend::CtrlKey;
using Ui::KeysLegend::ShiftKey;
using Ui::KeysLegend::DeleteKey;

[[nodiscard]] QString Gesture(
		const QString &modifier,
		const QString &gesture) {
	return tr::lng_photo_editor_keys_gesture(
		tr::now,
		lt_modifier,
		modifier,
		lt_gesture,
		gesture);
}

[[nodiscard]] QString DragEdge() {
	return tr::lng_photo_editor_keys_gesture_drag_edge(tr::now);
}

[[nodiscard]] QString DragHandle() {
	return tr::lng_photo_editor_keys_gesture_drag_handle(tr::now);
}

[[nodiscard]] QString Drag() {
	return tr::lng_photo_editor_keys_gesture_drag(tr::now);
}

[[nodiscard]] QString Click() {
	return tr::lng_photo_editor_keys_gesture_click(tr::now);
}

[[nodiscard]] QString Wheel() {
	return tr::lng_photo_editor_keys_gesture_wheel(tr::now);
}

[[nodiscard]] QString WheelKey() {
	return tr::lng_photo_editor_keys_wheel(tr::now);
}

[[nodiscard]] QString MiddleDragKey() {
	return tr::lng_photo_editor_keys_middle_drag(tr::now);
}

[[nodiscard]] QString DoubleClickKey() {
	return tr::lng_photo_editor_keys_double_click(tr::now);
}

[[nodiscard]] std::vector<Row> FixedCropRows() {
	return {
		{ { WheelKey() }, tr::lng_photo_editor_keys_fixed_zoom(tr::now) },
		{
			{ Gesture(ShiftKey(), Wheel()) },
			tr::lng_photo_editor_keys_fixed_zoom_fine(tr::now),
		},
		{
			{ MiddleDragKey() },
			tr::lng_photo_editor_keys_fixed_pan(tr::now),
		},
	};
}

[[nodiscard]] Section CropSection(const KeysLegendContext &context) {
	auto result = Section{ .title = tr::lng_photo_editor_keys_crop(tr::now) };
	if (context.fixedCrop) {
		result.rows = FixedCropRows();
	} else {
		result.rows = {
			{ { CtrlKey() }, tr::lng_photo_editor_keys_crop_room(tr::now) },
			{
				{ Gesture(CtrlKey(), DragEdge()) },
				tr::lng_photo_editor_keys_crop_expand(tr::now),
			},
		};
	}
	return result;
}

[[nodiscard]] Section CanvasSection(const KeysLegendContext &context) {
	auto result = Section{
		.title = tr::lng_photo_editor_keys_canvas(tr::now),
	};
	if (context.fixedCrop) {
		result.rows = FixedCropRows();
	} else {
		result.rows = {
			{
				{ WheelKey() },
				tr::lng_photo_editor_keys_canvas_zoom(tr::now),
			},
			{
				{ MiddleDragKey() },
				tr::lng_photo_editor_keys_canvas_pan(tr::now),
			},
		};
	}
	result.rows.push_back({
		{ CtrlKey(), u"V"_q },
		tr::lng_photo_editor_keys_canvas_paste(tr::now),
	});
	result.rows.push_back({
		{ CtrlKey(), u"Z"_q },
		tr::lng_photo_editor_keys_canvas_undo(tr::now),
	});
	result.rows.push_back({
		{ CtrlKey(), ShiftKey(), u"Z"_q },
		tr::lng_photo_editor_keys_canvas_redo(tr::now),
	});
	return result;
}

[[nodiscard]] Section ShapesSection() {
	return {
		.title = tr::lng_photo_editor_keys_shapes(tr::now),
		.rows = {
			{
				{ Gesture(ShiftKey(), Drag()) },
				tr::lng_photo_editor_keys_shapes_proportions(tr::now),
			},
			{
				{ Gesture(ShiftKey(), Click()) },
				tr::lng_photo_editor_keys_shapes_instant(tr::now),
			},
		},
	};
}

[[nodiscard]] Section ObjectsSection() {
	return {
		.title = tr::lng_photo_editor_keys_objects(tr::now),
		.rows = {
			{
				{ Gesture(ShiftKey(), Drag()) },
				tr::lng_photo_editor_keys_objects_snap(tr::now),
			},
			{
				{ Gesture(ShiftKey(), DragHandle()) },
				tr::lng_photo_editor_keys_objects_rotate(tr::now),
			},
			{
				{ Gesture(CtrlKey(), DragHandle()) },
				tr::lng_photo_editor_keys_objects_stretch(tr::now),
			},
			{
				{ CtrlKey(), u"D"_q },
				tr::lng_photo_editor_keys_objects_duplicate(tr::now),
			},
			{
				{ CtrlKey(), u"S"_q },
				tr::lng_photo_editor_keys_objects_flip(tr::now),
			},
			{
				{ DeleteKey() },
				tr::lng_photo_editor_keys_objects_delete(tr::now),
			},
			{
				{ DoubleClickKey() },
				tr::lng_photo_editor_keys_objects_text(tr::now),
			},
		},
	};
}

[[nodiscard]] Section TimelineSection() {
	return {
		.title = tr::lng_photo_editor_keys_timeline(tr::now),
		.rows = {
			{
				{ WheelKey() },
				tr::lng_photo_editor_keys_timeline_zoom(tr::now),
			},
			{
				{ Gesture(ShiftKey(), Wheel()) },
				tr::lng_photo_editor_keys_timeline_scroll(tr::now),
			},
			{
				{ Gesture(ShiftKey(), Drag()) },
				tr::lng_photo_editor_keys_timeline_window(tr::now),
			},
		},
	};
}

[[nodiscard]] std::vector<Section> Sections(
		const KeysLegendContext &context) {
	auto result = (context.mode == PhotoEditorMode::Mode::Transform)
		? std::vector<Section>{ CropSection(context) }
		: std::vector<Section>{
			CanvasSection(context),
			ShapesSection(),
			ObjectsSection(),
		};
	if (context.timeline
		&& (context.mode == PhotoEditorMode::Mode::Paint)) {
		result.push_back(TimelineSection());
	}
	return result;
}

[[nodiscard]] Section LegendSection() {
	return {
		.rows = {
			{ { u"?"_q }, tr::lng_photo_editor_keys_legend(tr::now) },
		},
	};
}

[[nodiscard]] Ui::KeysLegend::St LegendSt() {
	return {
		.menu = &st::photoEditorKeysMenu,
		.titleStyle = &st::photoEditorKeysTitleStyle,
		.titleFg = &st::photoEditorKeysTitleFg,
		.titleSkip = st::photoEditorKeysTitleSkip,
		.rowSkip = st::photoEditorKeysRowSkip,
		.capStyle = &st::photoEditorKeysCapStyle,
		.capHeight = st::photoEditorKeysCapHeight,
		.capMinWidth = st::photoEditorKeysCapMinWidth,
		.capMaxWidth = st::photoEditorKeysCapMaxWidth,
		.capPadding = st::photoEditorKeysCapPadding,
		.capRadius = st::photoEditorKeysCapRadius,
		.capSkip = st::photoEditorKeysCapSkip,
		.capBg = &st::photoEditorKeysCapBg,
		.capBorder = &st::photoEditorKeysCapBorder,
		.capFg = &st::photoEditorKeysCapFg,
		.textSkip = st::photoEditorKeysTextSkip,
		.textWidth = st::photoEditorKeysTextWidth,
		.textMinWidth = st::photoEditorKeysTextMinWidth,
		.textFg = &st::photoEditorKeysTextFg,
		.skip = st::photoEditorKeysMenuSkip,
	};
}

} // namespace

KeysLegendButton::KeysLegendButton(
	not_null<QWidget*> parent,
	Fn<KeysLegendContext()> context)
: IconButton(parent, st::photoEditorKeysButton)
, _context(std::move(context)) {
	events(
	) | rpl::on_next([=](not_null<QEvent*> event) {
		if (event->type() == QEvent::Enter) {
			Ui::Tooltip::Show(1000, this);
		} else if (event->type() == QEvent::Leave) {
			Ui::Tooltip::Hide();
		}
	}, lifetime());
	setClickedCallback([=] { toggle(); });
}

void KeysLegendButton::toggle() {
	if (_menu) {
		_menu->hideMenu();
	} else {
		showLegend();
	}
}

QString KeysLegendButton::tooltipText() const {
	return tr::lng_photo_editor_keys_tooltip(tr::now);
}

QPoint KeysLegendButton::tooltipPos() const {
	return QCursor::pos();
}

bool KeysLegendButton::tooltipWindowActive() const {
	return Ui::AppInFocus() && Ui::InFocusChain(window());
}

void KeysLegendButton::showLegend() {
	Ui::Tooltip::Hide();
	auto sections = Sections(_context());
	sections.push_back(LegendSection());
	_menu = Ui::KeysLegend::ShowLegendMenu(
		this,
		LegendSt(),
		std::move(sections));
}

} // namespace Editor
