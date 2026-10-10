/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_keys_legend.h"

#include "base/platform/base_platform_info.h"
#include "lang/lang_keys.h"
#include "ui/effects/panel_animation.h"
#include "ui/painter.h"
#include "ui/qt_object_factory.h"
#include "ui/rect.h"
#include "ui/text/text.h"
#include "ui/ui_utility.h"
#include "ui/widgets/menu/menu.h"
#include "ui/widgets/menu/menu_item_base.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_editor.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>

namespace Editor {
namespace {

struct Row {
	std::vector<QString> keys;
	QString text;
};

struct Section {
	QString title;
	std::vector<Row> rows;
};

[[nodiscard]] QString CtrlKey() {
	return Platform::IsMac() ? QString(QChar(0x2318)) : u"Ctrl"_q;
}

[[nodiscard]] QString ShiftKey() {
	return Platform::IsMac() ? QString(QChar(0x21E7)) : u"Shift"_q;
}

[[nodiscard]] QString DeleteKey() {
	return Platform::IsMac() ? QString(QChar(0x232B)) : u"Del"_q;
}

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

struct Cap {
	QString text;
	int width = 0;
};

[[nodiscard]] Cap MakeCap(const QString &text) {
	const auto &st = st::photoEditorKeysCapStyle;
	return {
		.text = text,
		.width = std::max(
			st.font->width(text) + 2 * st::photoEditorKeysCapPadding,
			st::photoEditorKeysCapMinWidth),
	};
}

[[nodiscard]] int CapsWidth(const std::vector<Cap> &caps) {
	auto result = 0;
	for (const auto &cap : caps) {
		result += cap.width;
	}
	return result + (int(caps.size()) - 1) * st::photoEditorKeysCapSkip;
}

class SectionItem final : public Ui::Menu::ItemBase {
public:
	SectionItem(
		not_null<Ui::Menu::Menu*> parent,
		const style::Menu &st,
		const Section &section,
		int keysWidth);

	not_null<QAction*> action() const override;
	bool isEnabled() const override;

private:
	struct PreparedRow {
		std::vector<Cap> caps;
		Ui::Text::String text;
	};

	int contentHeight() const override;
	void paintEvent(QPaintEvent *e) override;
	void paintCap(QPainter &p, const Cap &cap, int x, int y) const;

	const style::Menu &_st;
	const int _keysWidth;
	const Ui::Text::String _title;
	std::vector<PreparedRow> _rows;
	int _textWidth = 0;
	const not_null<QAction*> _dummyAction;

};

SectionItem::SectionItem(
	not_null<Ui::Menu::Menu*> parent,
	const style::Menu &st,
	const Section &section,
	int keysWidth)
: ItemBase(parent, st)
, _st(st)
, _keysWidth(keysWidth)
, _title(st::photoEditorKeysTitleStyle, section.title)
, _dummyAction(Ui::CreateChild<QAction>(parent.get())) {
	setPointerCursor(false);
	auto textWidth = 0;
	for (const auto &row : section.rows) {
		auto caps = std::vector<Cap>();
		for (const auto &key : row.keys) {
			caps.push_back(MakeCap(key));
		}
		auto text = Ui::Text::String(
			_st.itemStyle,
			row.text,
			kDefaultTextOptions,
			st::photoEditorKeysTextMinWidth);
		textWidth = std::max(textWidth, text.maxWidth());
		_rows.push_back({ std::move(caps), std::move(text) });
	}
	const auto available = _st.widthMax
		- rect::m::sum::h(_st.itemPadding)
		- _keysWidth
		- st::photoEditorKeysTextSkip;
	_textWidth = std::max(
		std::min({ textWidth, st::photoEditorKeysTextWidth, available }),
		st::photoEditorKeysTextMinWidth);
	setMinWidth(rect::m::sum::h(_st.itemPadding)
		+ _keysWidth
		+ st::photoEditorKeysTextSkip
		+ _textWidth);
	parent->widthValue() | rpl::on_next([=](int width) {
		resize(width, contentHeight());
	}, lifetime());
}

not_null<QAction*> SectionItem::action() const {
	return _dummyAction;
}

bool SectionItem::isEnabled() const {
	return false;
}

int SectionItem::contentHeight() const {
	auto result = rect::m::sum::v(_st.itemPadding);
	if (!_title.isEmpty()) {
		result += _title.minHeight() + st::photoEditorKeysTitleSkip;
	}
	for (const auto &row : _rows) {
		result += std::max(
			st::photoEditorKeysCapHeight,
			row.text.countHeight(_textWidth));
	}
	return result + (int(_rows.size()) - 1) * st::photoEditorKeysRowSkip;
}

void SectionItem::paintCap(
		QPainter &p,
		const Cap &cap,
		int x,
		int y) const {
	const auto rect = QRect(x, y, cap.width, st::photoEditorKeysCapHeight);
	const auto radius = st::photoEditorKeysCapRadius;
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(QPen(st::photoEditorKeysCapBorder, st::lineWidth));
	p.setBrush(st::photoEditorKeysCapBg);
	p.drawRoundedRect(
		QRectF(rect).adjusted(.5, .5, -.5, -.5),
		radius,
		radius);
	p.setPen(st::photoEditorKeysCapFg);
	p.setFont(st::photoEditorKeysCapStyle.font);
	p.drawText(rect, cap.text, style::al_center);
}

void SectionItem::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	p.fillRect(rect(), _st.itemBg);

	const auto &padding = _st.itemPadding;
	const auto textLeft = padding.left()
		+ _keysWidth
		+ st::photoEditorKeysTextSkip;
	auto top = padding.top();
	if (!_title.isEmpty()) {
		p.setPen(st::photoEditorKeysTitleFg);
		_title.draw(
			p,
			padding.left(),
			top,
			width() - rect::m::sum::h(padding));
		top += _title.minHeight() + st::photoEditorKeysTitleSkip;
	}

	for (const auto &row : _rows) {
		const auto textHeight = row.text.countHeight(_textWidth);
		const auto rowHeight = std::max(
			st::photoEditorKeysCapHeight,
			textHeight);
		auto capLeft = padding.left();
		const auto capTop = top
			+ (rowHeight - st::photoEditorKeysCapHeight) / 2;
		for (const auto &cap : row.caps) {
			paintCap(p, cap, capLeft, capTop);
			capLeft += cap.width + st::photoEditorKeysCapSkip;
		}
		p.setPen(st::photoEditorKeysTextFg);
		row.text.draw(
			p,
			textLeft,
			top + (rowHeight - textHeight) / 2,
			_textWidth);
		top += rowHeight + st::photoEditorKeysRowSkip;
	}
}

void FillKeysLegend(
		not_null<Ui::PopupMenu*> menu,
		const KeysLegendContext &context) {
	auto sections = Sections(context);
	sections.push_back(LegendSection());
	auto keysWidth = 0;
	for (const auto &section : sections) {
		for (const auto &row : section.rows) {
			auto caps = std::vector<Cap>();
			for (const auto &key : row.keys) {
				caps.push_back(MakeCap(key));
			}
			keysWidth = std::max(keysWidth, CapsWidth(caps));
		}
	}
	for (const auto &section : sections) {
		if (section.title.isEmpty()) {
			menu->addSeparator();
		}
		menu->addAction(base::make_unique_q<SectionItem>(
			menu->menu(),
			menu->st().menu,
			section,
			keysWidth));
	}
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
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::photoEditorKeysMenu);
	FillKeysLegend(_menu.get(), _context());
	const auto skip = st::photoEditorKeysMenuSkip;
	const auto above = mapToGlobal(QPoint(0, -skip));
	const auto screen = QGuiApplication::screenAt(above);
	const auto fitsAbove = !screen
		|| (above.y() - _menu->height() >= screen->availableGeometry().y());
	if (fitsAbove) {
		_menu->setForcedOrigin(Ui::PanelAnimation::Origin::BottomLeft);
		_menu->popup(above);
	} else {
		_menu->setForcedOrigin(Ui::PanelAnimation::Origin::TopLeft);
		_menu->popup(mapToGlobal(QPoint(0, height())));
	}
}

} // namespace Editor
