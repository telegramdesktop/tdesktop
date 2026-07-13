/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/tabs/info_profile_tabs_strip.h"

#include "ui/effects/animation_value.h"
#include "ui/effects/animation_value_f.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/ui_utility.h"
#include "styles/style_basic.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"

#include <QApplication>

namespace Info::Profile {
namespace {

void PaintIslandOutline(
		QPainter &p,
		const QRectF &island,
		float64 radius,
		const style::color &bg) {
	const auto light = (bg->c.lightness() >= 128);
	const auto width = light ? (0.6 * st::lineWidth) : double(st::lineWidth);
	if (light) {
		p.setPen(QPen(QColor(0, 0, 0, 20), width));
	} else {
		auto grad = QLinearGradient(0, island.top(), 0, island.bottom());
		grad.setColorAt(0., QColor(255, 255, 255, 40));
		grad.setColorAt(0.5, QColor(255, 255, 255, 0));
		grad.setColorAt(1., QColor(255, 255, 255, 20));
		p.setPen(QPen(QBrush(grad), width));
	}
	p.setBrush(Qt::NoBrush);
	const auto half = 0.5 * width;
	const auto stroke = light
		? island.marginsAdded(QMarginsF(half, half, half, half))
		: island.adjusted(half, half, -half, -half);
	const auto strokeRadius = light ? (radius + half) : (radius - half);
	p.drawRoundedRect(stroke, strokeRadius, strokeRadius);
}

} // namespace

TabsStrip::TabsStrip(QWidget *parent, const style::ProfileTabsStrip &st)
: RpWidget(parent)
, _st(st)
, _shadow(st::infoProfileTabsShadow) {
	setObjectName(u"profileTabsStrip"_q);
	setMouseTracking(true);
}

void TabsStrip::setTabs(std::vector<StripTab> tabs) {
	const auto activeId = (_active >= 0 && _active < int(_buttons.size()))
		? _buttons[_active].tab.id
		: QString();
	setSelected(-1);
	const auto pillHeight = _st.height
		- _st.margin.top()
		- _st.margin.bottom();
	auto buttons = std::vector<Button>();
	buttons.reserve(tabs.size());
	_active = -1;
	_wasActive = -1;
	auto x = 0;
	for (auto &tab : tabs) {
		Assert(!tab.id.isEmpty());

		auto button = Button();
		button.text.setText(_st.style, tab.text);
		const auto width = _st.tabPadding.left()
			+ button.text.maxWidth()
			+ _st.tabPadding.right();
		button.geometry = QRect(x, 0, width, pillHeight);
		if (tab.id == activeId) {
			_active = int(buttons.size());
		}
		button.tab = std::move(tab);
		buttons.push_back(std::move(button));
		x += width;
	}
	_buttons = std::move(buttons);
	_fullWidth = x;
	_activeAnimation.stop();
	if (_active >= 0) {
		_activeFrom = _activeTo = highlightRect(_active);
	}
	setNaturalWidth(_buttons.empty()
		? 0
		: (_st.margin.left()
			+ _st.skip
			+ _fullWidth
			+ _st.skip
			+ _st.margin.right()));
	resizeToWidth(width());
	update();
}

void TabsStrip::setActiveTab(const QString &id) {
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

rpl::producer<QString> TabsStrip::activated() const {
	return _activated.events();
}

QRect TabsStrip::islandRect() const {
	return rect() - _st.margin;
}

int TabsStrip::islandInteriorWidth() const {
	return islandRect().width() - 2 * _st.skip;
}

QRect TabsStrip::highlightRect(int index) const {
	Expects(index >= 0 && index < int(_buttons.size()));

	return _buttons[index].geometry - Margins(_st.activeSkip);
}

QRectF TabsStrip::currentHighlightRect() const {
	const auto progress = _activeAnimation.value(1.);
	const auto from = QRectF(_activeFrom);
	const auto to = QRectF(_activeTo);
	return QRectF(
		anim::interpolateF(from.x(), to.x(), progress),
		anim::interpolateF(from.y(), to.y(), progress),
		anim::interpolateF(from.width(), to.width(), progress),
		anim::interpolateF(from.height(), to.height(), progress));
}

int TabsStrip::scrollValue() const {
	return int(base::SafeRound(_scroll));
}

void TabsStrip::setSelected(int index) {
	const auto was = (_selected >= 0);
	const auto now = (index >= 0);
	_selected = index;
	if (was != now) {
		setCursor(now ? style::cur_pointer : style::cur_default);
	}
}

void TabsStrip::setActive(int index) {
	if (_active == index) {
		return;
	}
	const auto previous = std::exchange(_active, index);
	if (index < 0) {
		_activeAnimation.stop();
		_wasActive = -1;
		update();
		return;
	}
	const auto to = highlightRect(index);
	if (previous < 0 || previous >= int(_buttons.size())) {
		_activeAnimation.stop();
		_activeFrom = _activeTo = to;
		_wasActive = -1;
	} else {
		const auto current = currentHighlightRect().toRect();
		_activeAnimation.stop();
		_activeFrom = current;
		_activeTo = to;
		_wasActive = previous;
		_activeAnimation.start(
			[=] { update(); },
			0.,
			1.,
			_st.duration,
			anim::easeOutQuint);
	}
	scrollToTab(index);
	update();
}

void TabsStrip::scrollToTab(int index) {
	const auto interior = islandInteriorWidth();
	const auto geometry = _buttons[index].geometry;
	if (interior <= 0 || _scrollMax <= 0 || geometry.isEmpty()) {
		return;
	}
	const auto added = std::max(
		std::min(interior / 8, (interior - geometry.width()) / 2),
		0);
	const auto visibleFrom = scrollValue();
	const auto visibleTill = visibleFrom + interior;
	if ((visibleTill < geometry.x() + geometry.width() + added)
		|| (visibleFrom + added > geometry.x())) {
		_scrollTo = std::clamp(
			geometry.x() + (geometry.width() / 2) - (interior / 2),
			0,
			_scrollMax);
		_scrollAnimation.start([=] {
			_scroll = _scrollAnimation.value(_scrollTo);
			update();
		}, _scroll, _scrollTo, crl::time(150), anim::easeOutCirc);
	}
}

int TabsStrip::resizeGetHeight(int newWidth) {
	const auto interior = newWidth
		- _st.margin.left()
		- _st.margin.right()
		- 2 * _st.skip;
	_scrollMax = std::max(_fullWidth - interior, 0);
	if (_scroll > _scrollMax) {
		_scroll = _scrollMax;
		_scrollAnimation.stop();
	}
	return _buttons.empty() ? 0 : _st.height;
}

bool TabsStrip::eventHook(QEvent *e) {
	if (e->type() == QEvent::Leave) {
		setSelected(-1);
	}
	return RpWidget::eventHook(e);
}

void TabsStrip::mouseMoveEvent(QMouseEvent *e) {
	const auto mousex = e->pos().x();
	const auto drag = QApplication::startDragDistance();
	if (_dragx > 0) {
		_scrollAnimation.stop();
		_scroll = std::clamp(
			_dragscroll + _dragx - mousex,
			0.,
			_scrollMax * 1.);
		update();
		return;
	} else if (_pressx > 0 && std::abs(_pressx - mousex) > drag) {
		_dragx = _pressx;
		_dragscroll = _scroll;
		stopPressedRipple();
	}
	setSelected(indexAt(e->pos()));
}

int TabsStrip::indexAt(QPoint position) const {
	const auto shifted = position - contentOrigin();
	for (auto i = 0, c = int(_buttons.size()); i != c; ++i) {
		if (_buttons[i].geometry.contains(shifted)) {
			return i;
		}
	}
	return -1;
}

QPoint TabsStrip::contentOrigin() const {
	const auto island = islandRect();
	return QPoint(island.x() + _st.skip - scrollValue(), island.y());
}

void TabsStrip::addRipple(int index, QPoint position) {
	auto &button = _buttons[index];
	if (!button.ripple) {
		const auto size = highlightRect(index).size();
		button.ripple = std::make_unique<Ui::RippleAnimation>(
			_st.ripple,
			Ui::RippleAnimation::RoundRectMask(size, size.height() / 2),
			[=] { update(); });
	}
	const auto highlight = highlightRect(index).translated(
		contentOrigin());
	button.ripple->add(position - highlight.topLeft());
}

void TabsStrip::stopPressedRipple() {
	if (_pressed >= 0
		&& _pressed < int(_buttons.size())
		&& _buttons[_pressed].ripple) {
		_buttons[_pressed].ripple->lastStop();
	}
}

void TabsStrip::wheelEvent(QWheelEvent *e) {
	const auto delta = Ui::ScrollDeltaF(e);

	const auto phase = e->phase();
	const auto horizontal = (std::abs(delta.x()) > std::abs(delta.y()));
	if (phase == Qt::NoScrollPhase) {
		_locked = std::nullopt;
	} else if (phase == Qt::ScrollBegin) {
		_locked = std::nullopt;
	} else if (!_locked) {
		_locked = horizontal ? Qt::Horizontal : Qt::Vertical;
	}
	if (horizontal) {
		if (_locked == Qt::Vertical) {
			return;
		}
		e->accept();
	} else {
		if (_locked == Qt::Horizontal) {
			e->accept();
		} else {
			e->ignore();
		}
		return;
	}
	_scrollAnimation.stop();
	_scroll = std::clamp(_scroll - delta.x(), 0., _scrollMax * 1.);
	update();
}

void TabsStrip::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	setSelected(indexAt(e->pos()));
	_pressed = _selected;
	_pressx = e->pos().x();
	if (_pressed >= 0) {
		addRipple(_pressed, e->pos());
	}
}

void TabsStrip::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	stopPressedRipple();
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

void TabsStrip::paintEvent(QPaintEvent *e) {
	if (_buttons.empty()) {
		return;
	}
	auto p = QPainter(this);
	const auto island = islandRect();
	const auto radius = island.height() / 2.;
	_shadow.paint(p, island, int(radius));

	auto hq = PainterHighQualityEnabler(p);
	p.setBrush(_st.bg);
	p.setPen(Qt::NoPen);
	p.drawRoundedRect(island, radius, radius);
	PaintIslandOutline(p, QRectF(island), radius, _st.bg);

	auto clip = QPainterPath();
	clip.addRoundedRect(QRectF(island), radius, radius);
	p.setClipPath(clip);

	const auto origin = contentOrigin();
	const auto progress = _activeAnimation.value(1.);
	const auto animating = _activeAnimation.animating();
	if (_active >= 0) {
		const auto highlight = currentHighlightRect().translated(
			origin.x(),
			origin.y());
		const auto highlightRadius = highlight.height() / 2.;
		p.setBrush(_st.bgActive);
		p.setPen(Qt::NoPen);
		p.drawRoundedRect(highlight, highlightRadius, highlightRadius);
	}
	for (auto i = 0, c = int(_buttons.size()); i != c; ++i) {
		auto &button = _buttons[i];
		if (!button.ripple) {
			continue;
		}
		const auto highlight = highlightRect(i).translated(origin);
		button.ripple->paint(p, highlight.x(), highlight.y(), width());
		if (button.ripple->empty()) {
			button.ripple.reset();
		}
	}
	const auto textTop = (_buttons.front().geometry.height()
		- _st.style.font->height) / 2;
	for (auto i = 0, c = int(_buttons.size()); i != c; ++i) {
		const auto &button = _buttons[i];
		if (i == _active) {
			p.setPen(animating
				? QPen(anim::color(_st.fg, _st.fgActive, progress))
				: QPen(_st.fgActive->c));
		} else if (animating && i == _wasActive) {
			p.setPen(anim::color(_st.fgActive, _st.fg, progress));
		} else {
			p.setPen(_st.fg->c);
		}
		button.text.draw(p, {
			.position = origin
				+ button.geometry.topLeft()
				+ QPoint(_st.tabPadding.left(), textTop),
			.availableWidth = button.text.maxWidth(),
		});
	}

	if (_scrollMax > 0) {
		const auto &icon = st::defaultEmojiSuggestions;
		const auto &c = _st.bg->c;
		constexpr auto kF = 0.5;
		const auto rightWidth = icon.fadeRight.width();
		const auto opacityRight = (_scrollMax - _scroll)
			/ (rightWidth * kF);
		p.setOpacity(std::clamp(opacityRight, 0., 1.));
		icon.fadeRight.fill(
			p,
			QRect(
				island.x() + island.width() - rightWidth,
				island.y(),
				rightWidth,
				island.height()),
			c);

		const auto leftWidth = icon.fadeLeft.width();
		const auto opacityLeft = _scroll / (leftWidth * kF);
		p.setOpacity(std::clamp(opacityLeft, 0., 1.));
		icon.fadeLeft.fill(
			p,
			QRect(island.x(), island.y(), leftWidth, island.height()),
			c);
	}
}

} // namespace Info::Profile
