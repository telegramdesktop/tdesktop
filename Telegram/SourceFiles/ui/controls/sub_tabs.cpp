/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/sub_tabs.h"

#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "styles/style_basic.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_credits.h"

#include <QApplication>

namespace Ui {

SubTabs::SubTabs(
	QWidget *parent,
	Options options,
	std::vector<Tab> tabs,
	Text::MarkedContext context)
: RpWidget(parent)
, _centered(options.centered) {
	setMouseTracking(true);
	setTabs(std::move(tabs), context);
	if (!options.selected.isEmpty()) {
		setActiveTab(options.selected);
	}
}

void SubTabs::setTabs(
		std::vector<Tab> tabs,
		Text::MarkedContext context) {
	auto x = st::giftBoxTabsMargin.left();
	auto y = st::giftBoxTabsMargin.top();

	setSelected(-1);
	_buttons.resize(tabs.size());
	const auto padding = st::giftBoxTabPadding;
	auto activeId = (_active >= 0
		&& ranges::contains(tabs, _buttons[_active].tab.id, &Tab::id))
		? _buttons[_active].tab.id
		: QString();
	_active = -1;
	context.repaint = [=] { update(); };
	for (auto i = 0, count = int(tabs.size()); i != count; ++i) {
		auto &tab = tabs[i];
		Assert(!tab.id.isEmpty());

		auto &button = _buttons[i];
		button.active = (tab.id == activeId);
		if (button.tab != tab) {
			button.text = Text::String();
			button.text.setMarkedText(
				st::semiboldTextStyle,
				tab.text,
				kMarkupTextOptions,
				context);
			button.tab = std::move(tab);
		}
		if (button.active) {
			_active = i;
		}
		const auto width = button.text.maxWidth();
		const auto height = st::giftBoxTabStyle.font->height;
		const auto r = QRect(0, 0, width, height).marginsAdded(padding);
		button.geometry = QRect(QPoint(x, y), r.size());
		x += r.width() + st::giftBoxTabSkip;
	}
	const auto width = x
		- st::giftBoxTabSkip
		+ st::giftBoxTabsMargin.right();
	_fullWidth = width;
	resizeToWidth(this->width());
	update();
}

void SubTabs::setActiveTab(const QString &id) {
	if (id.isEmpty()) {
		setActive(-1);
		return;
	}
	const auto i = ranges::find(
		_buttons,
		id,
		[](const Button &button) { return button.tab.id; });
	Assert(i != end(_buttons));
	setActive(i - begin(_buttons));
}

rpl::producer<QString> SubTabs::activated() const {
	return _activated.events();
}

void SubTabs::setSelected(int index) {
	const auto was = (_selected >= 0);
	const auto now = (index >= 0);
	_selected = index;
	if (was != now) {
		setCursor(now ? style::cur_pointer : style::cur_default);
	}
}

void SubTabs::setActive(int index) {
	const auto was = _active;
	if (was == index) {
		return;
	}
	if (was >= 0 && was < _buttons.size()) {
		_buttons[was].active = false;
	}
	_active = index;
	_buttons[index].active = true;
	update();
}

int SubTabs::resizeGetHeight(int newWidth) {
	if (_centered) {
		update();
		const auto fullWidth = _fullWidth;
		_fullShift = (fullWidth < newWidth) ? (newWidth - fullWidth) / 2 : 0;
	}
	_scrollMax = (_fullWidth > newWidth) ? (_fullWidth - newWidth) : 0;
	return _buttons.empty()
		? 0
		: (st::giftBoxTabsMargin.top()
			+ _buttons.back().geometry.height()
			+ st::giftBoxTabsMargin.bottom());
}

bool SubTabs::eventHook(QEvent *e) {
	if (e->type() == QEvent::Leave) {
		setSelected(-1);
	}
	return RpWidget::eventHook(e);
}

void SubTabs::mouseMoveEvent(QMouseEvent *e) {
	const auto mousex = e->pos().x();
	const auto drag = QApplication::startDragDistance();
	if (_dragx > 0) {
		_scroll = std::clamp(
			_dragscroll + _dragx - mousex,
			0.,
			_scrollMax * 1.);
		update();
		return;
	} else if (_pressx > 0 && std::abs(_pressx - mousex) > drag) {
		_dragx = _pressx;
		_dragscroll = _scroll;
	}
	auto selected = -1;
	const auto position = e->pos() + scroll();
	for (auto i = 0, c = int(_buttons.size()); i != c; ++i) {
		if (_buttons[i].geometry.contains(position)) {
			selected = i;
			break;
		}
	}
	setSelected(selected);
}

void SubTabs::wheelEvent(QWheelEvent *e) {
	const auto delta = ScrollDeltaF(e);
	if (std::abs(delta.x()) > std::abs(delta.y())) {
		e->accept();
	}
	_scroll = std::clamp(_scroll - delta.x(), 0., _scrollMax * 1.);
	update();
}

void SubTabs::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	_pressed = _selected;
	_pressx = e->pos().x();
}

void SubTabs::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	const auto dragx = std::exchange(_dragx, 0);
	const auto pressed = std::exchange(_pressed, -1);
	_pressx = 0;
	if (!dragx
		&& pressed >= 0
		&& _selected == pressed
		&& pressed < _buttons.size()) {
		_activated.fire_copy(_buttons[pressed].tab.id);
	}
}

void SubTabs::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto padding = st::giftBoxTabPadding;
	const auto shift = -scroll();
	for (const auto &button : _buttons) {
		const auto geometry = button.geometry.translated(shift);
		if (button.active) {
			p.setBrush(st::giftBoxTabBgActive);
			p.setPen(Qt::NoPen);
			const auto radius = geometry.height() / 2.;
			p.drawRoundedRect(geometry, radius, radius);
			p.setPen(st::giftBoxTabFgActive);
		} else {
			p.setPen(st::giftBoxTabFg);
		}
		button.text.draw(p, {
			.position = geometry.marginsRemoved(padding).topLeft(),
			.availableWidth = button.text.maxWidth(),
		});
	}
	if (_fullWidth > width()) {
		const auto &icon = st::defaultEmojiSuggestions;
		const auto w = icon.fadeRight.width();
		const auto &c = st::boxDividerBg->c;
		const auto r = QRect(0, 0, w, height());
		const auto s = std::abs(float64(shift.x()));
		constexpr auto kF = 0.5;
		const auto opacityRight = (_scrollMax - s)
			/ (icon.fadeRight.width() * kF);
		p.setOpacity(std::clamp(std::abs(opacityRight), 0., 1.));
		icon.fadeRight.fill(p, r.translated(width() - w, 0), c);

		const auto opacityLeft = s / (icon.fadeLeft.width() * kF);
		p.setOpacity(std::clamp(std::abs(opacityLeft), 0., 1.));
		icon.fadeLeft.fill(p, r, c);
	}

}

QPoint SubTabs::scroll() const {
	return QPoint(int(base::SafeRound(_scroll)) - _fullShift, 0);
}

} // namespace Ui

