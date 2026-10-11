/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/keys_legend.h"

#include "base/platform/base_platform_info.h"
#include "ui/effects/panel_animation.h"
#include "ui/painter.h"
#include "ui/qt_object_factory.h"
#include "ui/rect.h"
#include "ui/text/text.h"
#include "ui/widgets/menu/menu.h"
#include "ui/widgets/menu/menu_item_base.h"
#include "ui/widgets/popup_menu.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>

#include "styles/style_widgets.h"

namespace Ui::KeysLegend {

QString CtrlKey() {
	return Platform::IsMac() ? QString(QChar(0x2318)) : u"Ctrl"_q;
}

QString ControlKey() {
	return Platform::IsMac() ? QString(QChar(0x2303)) : u"Ctrl"_q;
}

QString ShiftKey() {
	return Platform::IsMac() ? QString(QChar(0x21E7)) : u"Shift"_q;
}

QString AltKey() {
	return Platform::IsMac() ? QString(QChar(0x2325)) : u"Alt"_q;
}

QString DeleteKey() {
	return Platform::IsMac() ? QString(QChar(0x232B)) : u"Del"_q;
}

QString EscKey() {
	return u"Esc"_q;
}

QString SpaceKey() {
	return u"Space"_q;
}

Cap MakeCap(const QString &text, const St &st) {
	return {
		.text = text,
		.width = std::max(
			st.capStyle->font->width(text) + 2 * st.capPadding,
			st.capMinWidth),
	};
}

void PaintCap(QPainter &p, const Cap &cap, int x, int y, const St &st) {
	const auto rect = QRect(x, y, cap.width, st.capHeight);
	const auto radius = st.capRadius;
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(QPen(*st.capBorder, st::lineWidth));
	p.setBrush(*st.capBg);
	p.drawRoundedRect(
		QRectF(rect).adjusted(.5, .5, -.5, -.5),
		radius,
		radius);
	p.setPen(*st.capFg);
	p.setFont(st.capStyle->font);
	p.drawText(rect, cap.text, style::al_center);
}

namespace {

[[nodiscard]] int CapsWidth(const std::vector<Cap> &caps, const St &st) {
	auto result = 0;
	for (const auto &cap : caps) {
		result += cap.width;
	}
	return result + (int(caps.size()) - 1) * st.capSkip;
}

class SectionItem final : public Ui::Menu::ItemBase {
public:
	SectionItem(
		not_null<Ui::Menu::Menu*> parent,
		const style::Menu &st,
		const St &legend,
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

	const style::Menu &_st;
	const St _legend;
	const int _keysWidth;
	const Ui::Text::String _title;
	std::vector<PreparedRow> _rows;
	int _textWidth = 0;
	const not_null<QAction*> _dummyAction;

};

SectionItem::SectionItem(
	not_null<Ui::Menu::Menu*> parent,
	const style::Menu &st,
	const St &legend,
	const Section &section,
	int keysWidth)
: ItemBase(parent, st)
, _st(st)
, _legend(legend)
, _keysWidth(keysWidth)
, _title(*legend.titleStyle, section.title)
, _dummyAction(Ui::CreateChild<QAction>(parent.get())) {
	setPointerCursor(false);
	auto textWidth = 0;
	for (const auto &row : section.rows) {
		auto caps = std::vector<Cap>();
		for (const auto &key : row.keys) {
			caps.push_back(MakeCap(key, legend));
		}
		auto text = Ui::Text::String(
			_st.itemStyle,
			row.text,
			kDefaultTextOptions,
			legend.textMinWidth);
		textWidth = std::max(textWidth, text.maxWidth());
		_rows.push_back({ std::move(caps), std::move(text) });
	}
	const auto available = _st.widthMax
		- rect::m::sum::h(_st.itemPadding)
		- _keysWidth
		- legend.textSkip;
	_textWidth = std::max(
		std::min({ textWidth, legend.textWidth, available }),
		legend.textMinWidth);
	setMinWidth(rect::m::sum::h(_st.itemPadding)
		+ _keysWidth
		+ legend.textSkip
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
		result += _title.minHeight() + _legend.titleSkip;
	}
	for (const auto &row : _rows) {
		result += std::max(
			_legend.capHeight,
			row.text.countHeight(_textWidth));
	}
	return result + (int(_rows.size()) - 1) * _legend.rowSkip;
}

void SectionItem::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	p.fillRect(rect(), _st.itemBg);

	const auto &padding = _st.itemPadding;
	const auto textLeft = padding.left()
		+ _keysWidth
		+ _legend.textSkip;
	auto top = padding.top();
	if (!_title.isEmpty()) {
		p.setPen(*_legend.titleFg);
		_title.draw(
			p,
			padding.left(),
			top,
			width() - rect::m::sum::h(padding));
		top += _title.minHeight() + _legend.titleSkip;
	}

	for (const auto &row : _rows) {
		const auto textHeight = row.text.countHeight(_textWidth);
		const auto rowHeight = std::max(
			_legend.capHeight,
			textHeight);
		auto capLeft = padding.left();
		const auto capTop = top
			+ (rowHeight - _legend.capHeight) / 2;
		for (const auto &cap : row.caps) {
			PaintCap(p, cap, capLeft, capTop, _legend);
			capLeft += cap.width + _legend.capSkip;
		}
		p.setPen(*_legend.textFg);
		row.text.draw(
			p,
			textLeft,
			top + (rowHeight - textHeight) / 2,
			_textWidth);
		top += rowHeight + _legend.rowSkip;
	}
}

void FillLegendMenu(
		not_null<Ui::PopupMenu*> menu,
		const St &st,
		std::vector<Section> sections) {
	auto keysWidth = 0;
	for (const auto &section : sections) {
		for (const auto &row : section.rows) {
			auto caps = std::vector<Cap>();
			for (const auto &key : row.keys) {
				caps.push_back(MakeCap(key, st));
			}
			keysWidth = std::max(keysWidth, CapsWidth(caps, st));
		}
	}
	for (auto &section : sections) {
		if (section.title.isEmpty()) {
			menu->addSeparator();
		}
		menu->addAction(base::make_unique_q<SectionItem>(
			menu->menu(),
			menu->st().menu,
			st,
			std::move(section),
			keysWidth));
	}
}

} // namespace

base::unique_qptr<Ui::PopupMenu> ShowLegendMenu(
		not_null<QWidget*> anchor,
		const St &st,
		std::vector<Section> sections) {
	auto menu = base::make_unique_q<Ui::PopupMenu>(
		anchor,
		*st.menu);
	FillLegendMenu(menu.get(), st, std::move(sections));
	const auto above = anchor->mapToGlobal(QPoint(0, -st.skip));
	const auto screen = QGuiApplication::screenAt(above);
	const auto fitsAbove = !screen
		|| (above.y() - menu->height()
			>= screen->availableGeometry().y());
	if (fitsAbove) {
		menu->setForcedOrigin(Ui::PanelAnimation::Origin::BottomLeft);
		menu->popup(above);
	} else {
		menu->setForcedOrigin(Ui::PanelAnimation::Origin::TopLeft);
		menu->popup(anchor->mapToGlobal(QPoint(0, anchor->height())));
	}
	return menu;
}

} // namespace Ui::KeysLegend
