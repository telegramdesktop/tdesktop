/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/sub_tabs.h"

#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/effects/animation_value_f.h"
#include "styles/style_basic.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_credits.h"
#include "styles/style_info.h"

#include <QApplication>

namespace Ui {

SubTabs::SubTabs(
	QWidget *parent,
	const style::SubTabs &st,
	Options options,
	std::vector<Tab> tabs,
	Text::MarkedContext context)
: RpWidget(parent)
, _st(st)
, _centered(options.centered) {
	setMouseTracking(true);
	_reorderScrollAnimation.init([this] { updateScrollCallback(); });
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

rpl::producer<QString> SubTabs::contextMenuRequests() const {
	return _contextMenuRequests.events();
}

rpl::producer<SubTabs::ReorderUpdate> SubTabs::reorderUpdates() const {
	return _reorderUpdates.events();
}

void SubTabs::setReorderEnabled(bool enabled) {
	_reorderEnable = enabled;
	if (enabled) {
		_shakeAnimation.init([=] { update(); });
		_shakeAnimation.start();
	} else {
		_shakeAnimation.stop();
		update();
	}
}

bool SubTabs::reorderEnabled() const {
	return _reorderEnable;
}

void SubTabs::setPinnedInterval(int from, int to) {
	_pinnedIntervals.push_back({ from, to });
}

void SubTabs::clearPinnedIntervals() {
	_pinnedIntervals.clear();
}

bool SubTabs::isIndexPinned(int index) const {
	for (const auto &interval : _pinnedIntervals) {
		if (index >= interval.from && index < interval.to) {
			return true;
		}
	}
	return false;
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
	const auto geometry = _buttons[index].geometry;
	if (width() > 0
		&& _fullWidth > width()
		&& _scrollMax > 0
		&& !geometry.isEmpty()) {
		const auto added = std::max(
			std::min(width() / 8, (width() - geometry.width()) / 2),
			0);
		const auto visibleFrom = int(base::SafeRound(_scroll));
		const auto visibleTill = visibleFrom + width();
		if ((visibleTill < geometry.x() + geometry.width() + added)
			|| (visibleFrom + added > geometry.x())) {
			_scrollTo = std::clamp(
				geometry.x() + (geometry.width() / 2) - (width() / 2),
				0,
				_scrollMax);
			_scrollAnimation.start([=] {
				_scroll = _scrollAnimation.value(_scrollTo);
				update();
			}, _scroll, _scrollTo, crl::time(150), anim::easeOutCirc);
		}
	}
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

	if (_reorderEnable && _reorderIndex >= 0) {
		if (_reorderState != SubTabsReorderUpdate::State::Started) {
			const auto shift = e->globalPos().x() - _reorderStart;
			if (std::abs(shift) > drag) {
				_reorderState = SubTabsReorderUpdate::State::Started;
				_reorderStart += (shift > 0) ? drag : -drag;
				_reorderDesiredIndex = _reorderIndex;
				_reorderUpdates.fire({
					_buttons[_reorderIndex].tab.id,
					_reorderIndex,
					_reorderIndex,
					_reorderState
				});
			}
		} else {
			updateReorder(e->globalPos());
		}
		return;
	}

	if (!_reorderEnable) {
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
		}
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
	if (_reorderEnable) {
		e->ignore();
		return;
	}
	const auto delta = ScrollDeltaF(e);

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

void SubTabs::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	_pressed = _selected;
	_pressx = e->pos().x();

	if (_reorderEnable && _selected >= 0 && !isIndexPinned(_selected)) {
		startReorder(_selected, e->globalPos());
	}
}

void SubTabs::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}

	if (_reorderEnable && _reorderIndex >= 0) {
		finishReorder();
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

void SubTabs::contextMenuEvent(QContextMenuEvent *e) {
	if (_selected >= 0 && _selected < _buttons.size()) {
		_contextMenuRequests.fire_copy(_buttons[_selected].tab.id);
	}
}

void SubTabs::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto padding = st::giftBoxTabPadding;
	const auto shift = -scroll();
	const auto now = crl::now();
	const auto hasShake = _shakeAnimation.animating();
	for (auto i = 0; i < _buttons.size(); ++i) {
		const auto &button = _buttons[i];
		const auto geometry = button.geometry.translated(shift);

		if (hasShake && _reorderEnable && !isIndexPinned(i)) {
			shakeTransform(p, i, geometry.topLeft(), now);
		}

		const auto shiftedGeometry = geometry.translated(
			base::SafeRound(button.shift),
			0);
		if (button.active) {
			p.setBrush(st::giftBoxTabBgActive);
			p.setPen(Qt::NoPen);
			const auto radius = shiftedGeometry.height() / 2.;
			p.drawRoundedRect(shiftedGeometry, radius, radius);
			p.setPen(st::giftBoxTabFgActive);
		} else {
			p.setPen(st::giftBoxTabFg);
		}
		button.text.draw(p, {
			.position = shiftedGeometry.marginsRemoved(padding).topLeft(),
			.availableWidth = button.text.maxWidth(),
		});

		if (hasShake && _reorderEnable && !isIndexPinned(i)) {
			p.resetTransform();
		}
	}
	if (_fullWidth > width()) {
		const auto &icon = st::defaultEmojiSuggestions;
		const auto w = icon.fadeRight.width();
		const auto &c = _st.bg->c;
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

void SubTabs::startReorder(int index, QPoint globalPos) {
	cancelReorder();
	_reorderIndex = index;
	_reorderStart = globalPos.x();
	_reorderState = SubTabsReorderUpdate::State::Cancelled;
}

void SubTabs::updateReorder(QPoint globalPos) {
	if (_reorderIndex < 0 || isIndexPinned(_reorderIndex)) {
		return;
	}

	_reorderMousePos = mapFromGlobal(globalPos);
	const auto shift = globalPos.x() - _reorderStart;
	auto &current = _buttons[_reorderIndex];
	current.shiftAnimation.stop();
	current.shift = current.finalShift = shift;

	checkForScrollAnimation();

	const auto count = _buttons.size();
	const auto currentWidth = current.geometry.width();
	const auto currentMiddle = current.geometry.x()
		+ shift
		+ currentWidth / 2;
	_reorderDesiredIndex = _reorderIndex;

	if (shift > 0) {
		for (auto next = _reorderIndex + 1; next < count; ++next) {
			if (isIndexPinned(next)) {
				break;
			}
			const auto &e = _buttons[next];
			if (currentMiddle < e.geometry.x() + e.geometry.width() / 2) {
				moveToShift(next, 0);
			} else {
				_reorderDesiredIndex = next;
				moveToShift(next, -currentWidth);
			}
		}
		for (auto prev = _reorderIndex - 1; prev >= 0; --prev) {
			moveToShift(prev, 0);
		}
	} else {
		for (auto next = _reorderIndex + 1; next < count; ++next) {
			moveToShift(next, 0);
		}
		for (auto prev = _reorderIndex - 1; prev >= 0; --prev) {
			if (isIndexPinned(prev)) {
				break;
			}
			const auto &e = _buttons[prev];
			if (currentMiddle >= e.geometry.x() + e.geometry.width() / 2) {
				moveToShift(prev, 0);
			} else {
				_reorderDesiredIndex = prev;
				moveToShift(prev, currentWidth);
			}
		}
	}
	update();
}

void SubTabs::finishReorder() {
	_reorderScrollAnimation.stop();
	if (_reorderIndex < 0) {
		return;
	}

	const auto index = _reorderIndex;
	const auto result = _reorderDesiredIndex;
	const auto id = _buttons[index].tab.id;

	if (result == index
		|| _reorderState != SubTabsReorderUpdate::State::Started) {
		cancelReorder();
		return;
	}

	_reorderState = SubTabsReorderUpdate::State::Applied;
	_reorderIndex = -1;
	_dragx = 0;
	_pressx = 0;
	_dragscroll = 0.;

	auto &current = _buttons[index];
	const auto width = current.geometry.width();

	if (index < result) {
		auto sum = 0;
		for (auto i = index; i < result; ++i) {
			auto &entry = _buttons[i + 1];
			entry.deltaShift += width;
			updateShift(i + 1);
			sum += entry.geometry.width();
		}
		current.finalShift -= sum;
	} else if (index > result) {
		auto sum = 0;
		for (auto i = result; i < index; ++i) {
			auto &entry = _buttons[i];
			entry.deltaShift -= width;
			updateShift(i);
			sum += entry.geometry.width();
		}
		current.finalShift += sum;
	}

	if (!(current.finalShift + current.deltaShift)) {
		current.shift = 0;
	}

	base::reorder(_buttons, index, result);

	auto x = st::giftBoxTabsMargin.left();
	const auto y = st::giftBoxTabsMargin.top();
	const auto padding = st::giftBoxTabPadding;
	for (auto i = 0; i < _buttons.size(); ++i) {
		auto &button = _buttons[i];
		const auto width = button.text.maxWidth();
		const auto height = st::giftBoxTabStyle.font->height;
		const auto r = QRect(0, 0, width, height).marginsAdded(padding);
		button.geometry = QRect(QPoint(x, y), r.size());
		x += r.width() + st::giftBoxTabSkip;
	}

	for (auto i = 0; i < _buttons.size(); ++i) {
		moveToShift(i, 0);
	}

	_reorderUpdates.fire(
		{ id, index, result, ReorderUpdate::State::Applied });
}

void SubTabs::cancelReorder() {
	_reorderScrollAnimation.stop();
	if (_reorderIndex < 0) {
		return;
	}

	const auto index = _reorderIndex;
	const auto id = _buttons[index].tab.id;

	if (_reorderState == SubTabsReorderUpdate::State::Started) {
		_reorderState = SubTabsReorderUpdate::State::Cancelled;
		_reorderUpdates.fire({ id, index, index, _reorderState });
	}

	_reorderIndex = -1;
	_dragx = 0;
	_pressx = 0;
	_dragscroll = 0.;
	for (auto i = 0; i < _buttons.size(); ++i) {
		moveToShift(i, 0);
	}
}

void SubTabs::moveToShift(int index, float64 shift) {
	if (index < 0 || index >= _buttons.size()) {
		return;
	}

	auto &entry = _buttons[index];
	if (entry.finalShift + entry.deltaShift == shift) {
		return;
	}

	entry.shiftAnimation.start(
		[=, this] { updateShift(index); },
		entry.finalShift,
		shift - entry.deltaShift,
		150);
	entry.finalShift = shift - entry.deltaShift;
}

void SubTabs::updateShift(int index) {
	if (index < 0 || index >= _buttons.size()) {
		return;
	}

	auto &entry = _buttons[index];
	entry.shift = entry.shiftAnimation.value(entry.finalShift)
		+ entry.deltaShift;

	if (entry.deltaShift && !entry.shiftAnimation.animating()) {
		entry.finalShift += entry.deltaShift;
		entry.deltaShift = 0;
	}

	update();
}

void SubTabs::shakeTransform(
		QPainter &p,
		int index,
		QPoint position,
		crl::time now) const {
	constexpr auto kShakeADuration = crl::time(400);
	constexpr auto kShakeXDuration = crl::time(kShakeADuration * 1.2);
	constexpr auto kShakeYDuration = kShakeADuration;
	const auto diff = ((index % 2) ? 0 : kShakeYDuration / 2)
		+ (now - _shakeAnimation.started());
	const auto pX = (diff % kShakeXDuration)
		/ float64(kShakeXDuration);
	const auto pY = (diff % kShakeYDuration)
		/ float64(kShakeYDuration);

	constexpr auto kMaxTranslation = .5;
	constexpr auto kXStep = 1. / 5;
	constexpr auto kYStep = 1. / 4;

	// 0, kMaxTranslation, 0, -kMaxTranslation, 0.
	const auto x = (pX < kXStep)
		? anim::interpolateF(0., kMaxTranslation, pX / kXStep)
		: (pX < kXStep * 2.)
		? anim::interpolateF(kMaxTranslation, 0, (pX - kXStep) / kXStep)
		: (pX < kXStep * 3.)
		? anim::interpolateF(0, -kMaxTranslation, (pX - kXStep * 2.) / kXStep)
		: (pX < kXStep * 4.)
		? anim::interpolateF(-kMaxTranslation, 0, (pX - kXStep * 3.) / kXStep)
		: anim::interpolateF(0, 0., (pX - kXStep * 4.) / kXStep);

	// 0, kMaxTranslation, -kMaxTranslation, 0.
	const auto y = (pY < kYStep)
		? anim::interpolateF(0., kMaxTranslation, pY / kYStep)
		: (pY < kYStep * 2.)
		? anim::interpolateF(kMaxTranslation, 0, (pY - kYStep) / kYStep)
		: (pY < kYStep * 3.)
		? anim::interpolateF(0, -kMaxTranslation, (pY - kYStep * 2.) / kYStep)
		: anim::interpolateF(-kMaxTranslation, 0, (pY - kYStep * 3) / kYStep);

	p.translate(x, y);
}

void SubTabs::checkForScrollAnimation() {
	if (_reorderIndex < 0
		|| !deltaFromEdge()
		|| _reorderScrollAnimation.animating()) {
		return;
	}
	_reorderScrollAnimation.start();
}

void SubTabs::updateScrollCallback() {
	const auto delta = deltaFromEdge();
	if (!delta) {
		return;
	}

	const auto oldScroll = _scroll;
	_scroll = std::clamp(_scroll + delta * 0.1, 0., float64(_scrollMax));

	const auto scrollDelta = oldScroll - _scroll;
	_reorderStart += scrollDelta;

	if (_reorderIndex >= 0) {
		auto &current = _buttons[_reorderIndex];
		current.shift = current.finalShift -= scrollDelta;
	}

	if (_scroll == 0. || _scroll == _scrollMax) {
		_reorderScrollAnimation.stop();
	}
	update();
}

int SubTabs::deltaFromEdge() {
	if (_reorderIndex < 0) {
		return 0;
	}

	const auto mouseX = _reorderMousePos.x();
	const auto isLeftEdge = (mouseX < 0);
	const auto isRightEdge = (mouseX > width());

	if (!isLeftEdge && !isRightEdge) {
		_reorderScrollAnimation.stop();
		return 0;
	}

	const auto delta = isRightEdge ? (mouseX - width()) : mouseX;
	return std::clamp(delta, -50, 50);
}

} // namespace Ui

