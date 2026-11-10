/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_touch.h"

#include "base/platform/base_platform_info.h"
#include "history/view/media/history_view_html_widget.h"
#include "history/view/history_view_element.h"
#include "tgw/tgw_renderer.h"
#include "ui/click_handler.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QStyleHints>

#include <chrono>

namespace Tgw {
namespace {

constexpr auto kMinHoldDelay = crl::time(1);

auto BridgeDepth = 0;

// WHY: the same steady_clock reading as TwidgetCore's arbiter, taken after
// the core's down, so a hold awarded here is due by the core's clock and
// RendererCore::fingerHold() delivers its contextmenu before any cancel.
[[nodiscard]] double Now() {
	const auto now = std::chrono::steady_clock::now().time_since_epoch();
	return std::chrono::duration<double, std::milli>(now).count();
}

[[nodiscard]] int Modifiers() {
	return DomModifiers(QGuiApplication::keyboardModifiers());
}

[[nodiscard]] bool MenuOnRelease() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
	return (QGuiApplication::styleHints()->contextMenuTrigger()
		== Qt::ContextMenuTrigger::Release);
#else // Qt >= 6.8.0
	return Platform::IsWindows();
#endif // Qt < 6.8.0
}

} // namespace

TouchBridge::TouchBridge() {
	++BridgeDepth;
}

TouchBridge::~TouchBridge() {
	--BridgeDepth;
}

bool TouchBridge::Active() {
	return (BridgeDepth > 0);
}

WidgetTouch::WidgetTouch(WidgetTouchArgs &&args)
: _list(args.list)
, _viewAt(std::move(args.viewAt))
, _mapToItem(std::move(args.mapToItem))
, _covered(std::move(args.covered))
, _menu(std::move(args.menu))
, _resetSpeed(std::move(args.resetSpeed))
, _dropTouch(std::move(args.dropTouch))
, _holdTimer([=] { hold(); }) {
}

WidgetTouch::~WidgetTouch() {
	finish();
	releaseMenu();
}

bool WidgetTouch::begin(QPoint global) {
	finish();
	const auto local = _list->mapFromGlobal(global);
	const auto view = _viewAt(local);
	const auto widget = view
		? dynamic_cast<HistoryView::HtmlWidget*>(view->media())
		: nullptr;
	if (!widget || (_covered && _covered(local))) {
		return false;
	}
	const auto at = _mapToItem(local, view) - view->mediaTopLeft();
	const auto core = widget->touchCore(at);
	const auto claim = core
		? core->fingerPress(widget->cssPoint(at), Modifiers())
		: std::nullopt;
	if (!claim) {
		return false;
	}
	_widget = base::make_weak(widget);
	_core = base::make_weak(core);
	_last = global;
	_owner = Owner::Pending;
	// WHY: TwidgetCore runs the same policy on its own clock. Starting this
	// press after the core's and deciding later steps before forwarding
	// keeps bubble cancellation ahead of the corresponding core input.
	const auto lead = widget->gesturePoint(at);
	_arbiter.down(Now(), lead.x(), lead.y(), *claim);
	scheduleHold();
	_resetSpeed();
	return true;
}

std::optional<bool> WidgetTouch::update(QPoint global) {
	if (_owner == Owner::None) {
		return std::nullopt;
	}
	step(global);
	return (_owner == Owner::Scroll);
}

std::optional<bool> WidgetTouch::end(QPoint global) {
	if (_owner == Owner::None) {
		return std::nullopt;
	}
	release(global);
	const auto owner = _owner;
	const auto widget = _widget.get();
	const auto local = _list->mapFromGlobal(global);
	const auto menu = (owner == Owner::Menu)
		&& widget
		&& (_viewAt(local) == widget->parent().get());
	finish();
	if (menu) {
		auto e = QContextMenuEvent(QContextMenuEvent::Mouse, local, global);
		_menu(&e);
	}
	return (owner == Owner::Scroll);
}

void WidgetTouch::cancel() {
	if (_owner == Owner::None) {
		return;
	}
	_arbiter.cancel();
	finish();
}

void WidgetTouch::retire() {
	if (_owner == Owner::None) {
		return;
	}
	cancel();
	_dropTouch();
	_resetSpeed();
}

void WidgetTouch::mousePress(Qt::MouseButton button) {
	if (button != Qt::RightButton) {
		return;
	} else if (_menuAhead) {
		_menuAhead = false;
		return;
	}
	decideMenu();
	_menuPending = true;
}

bool WidgetTouch::mouseMenu(not_null<QContextMenuEvent*> e) {
	if (e->reason() != QContextMenuEvent::Mouse) {
		return false;
	} else if (_menuPending) {
		_menuPending = false;
		return _menuTaken;
	} else if (!(QGuiApplication::mouseButtons() & Qt::RightButton)) {
		return false;
	}
	// WHY: on a press trigger the second press of a right double click
	// reaches here before its double click event, which an opened menu
	// may take instead, so this early decision expires if not consumed.
	decideMenu();
	_menuAhead = true;
	crl::on_main(this, [=] {
		_menuAhead = false;
	});
	return _menuTaken;
}

void WidgetTouch::mouseRelease(Qt::MouseButton button) {
	if (button != Qt::RightButton) {
		return;
	}
	_menuAhead = false;
	if (!MenuOnRelease()) {
		_menuPending = false;
	}
	releaseMenu();
}

Ui::Controls::SwipeHandoff WidgetTouch::swipeHandoff() {
	const auto weak = base::make_weak(this);
	return {
		.owned = [=] {
			const auto strong = weak.get();
			return strong && (strong->_owner != Owner::None);
		},
		.swipe = [=](not_null<QTouchEvent*> e) {
			const auto strong = weak.get();
			return strong && strong->handoff(e);
		},
		.lost = [=] {
			const auto strong = weak.get();
			if (strong && (strong->_owner != Owner::None)) {
				strong->lose();
			}
		},
	};
}

bool WidgetTouch::handoff(not_null<QTouchEvent*> e) {
	if (_owner == Owner::None) {
		return false;
	} else if (e->type() == QEvent::TouchCancel) {
		lose();
		return false;
	}
	const auto &points = e->touchPoints();
	if (points.size() != 1
		|| (e->type() != QEvent::TouchEnd
			&& (int(points.front().state()) & int(Qt::TouchPointReleased)))) {
		lose();
		return false;
	}
	const auto global = points.front().screenPos().toPoint();
	if (e->type() == QEvent::TouchEnd) {
		release(global);
	} else {
		step(global);
	}
	return (_owner == Owner::Swipe);
}

RendererCore *WidgetTouch::held() const {
	const auto core = _core.get();
	return (core && core->fingerPressed()) ? core : nullptr;
}

QPoint WidgetTouch::mediaPoint(
		not_null<HistoryView::HtmlWidget*> widget,
		QPoint global) const {
	const auto view = widget->parent();
	return _mapToItem(_list->mapFromGlobal(global), view)
		- view->mediaTopLeft();
}

void WidgetTouch::step(QPoint global) {
	if (_released) {
		return;
	}
	if (_owner == Owner::Menu) {
		if (!_widget) {
			lose();
		}
		return;
	} else if (_owner != Owner::Pending && _owner != Owner::Widget) {
		return;
	}
	const auto widget = _widget.get();
	const auto core = held();
	if (!widget || !core) {
		lose();
		return;
	}
	if (global == _last) {
		return;
	}
	_last = global;
	const auto at = mediaPoint(widget, global);
	// A hold the widget won is still locked by its first move past the slop.
	const auto lead = widget->gesturePoint(at);
	const auto verdict = _arbiter.move(Now(), lead.x(), lead.y());
	if (!verdict) {
		lose();
		return;
	}
	decide(*verdict);
	if (_owner == Owner::Pending || _owner == Owner::Widget) {
		core->fingerMove(widget->cssPoint(at), Modifiers());
	}
}

void WidgetTouch::release(QPoint global) {
	if (_released) {
		return;
	}
	_released = true;
	_holdTimer.cancel();
	if (_owner != Owner::Pending && _owner != Owner::Widget) {
		return;
	}
	const auto widget = _widget.get();
	const auto core = held();
	if (!widget || !core) {
		lose();
		return;
	}
	_last = global;
	const auto at = mediaPoint(widget, global);
	const auto lead = widget->gesturePoint(at);
	const auto verdict = _arbiter.up(Now(), lead.x(), lead.y());
	if (!verdict) {
		lose();
	} else if (verdict->owner == twidget::GestureOwner::Bubble) {
		decide(*verdict);
	} else {
		_owner = Owner::Widget;
		core->fingerRelease(widget->cssPoint(at), Modifiers());
	}
}

void WidgetTouch::decide(const twidget::GestureVerdict &verdict) {
	using Action = twidget::BubbleAction;
	if (verdict.owner == twidget::GestureOwner::Pending) {
		return;
	}
	_holdTimer.cancel();
	if (verdict.owner == twidget::GestureOwner::Widget) {
		_owner = Owner::Widget;
		if (verdict.event == twidget::GestureEvent::ContextMenu) {
			if (const auto core = held()) {
				core->fingerHold();
			}
		}
		return;
	}
	if (const auto core = held()) {
		core->fingerCancel();
	}
	switch (verdict.action.value_or(Action::Menu)) {
	case Action::Menu: _owner = Owner::Menu; break;
	case Action::Swipe:
	case Action::Back: _owner = Owner::Swipe; break;
	case Action::Scroll: _owner = Owner::Scroll; break;
	}
}

void WidgetTouch::scheduleHold() {
	if (const auto deadline = _arbiter.holdDeadline()) {
		_holdTimer.callOnce(std::max(
			crl::time(std::ceil(*deadline - Now())),
			kMinHoldDelay));
	}
}

void WidgetTouch::hold() {
	if (_owner != Owner::Pending) {
		return;
	} else if (!_widget || !held()) {
		lose();
		return;
	}
	const auto verdict = _arbiter.tick(Now());
	if (!verdict) {
		lose();
	} else if (verdict->owner == twidget::GestureOwner::Pending) {
		scheduleHold();
	} else {
		decide(*verdict);
	}
}

void WidgetTouch::lose() {
	_holdTimer.cancel();
	_arbiter.cancel();
	if (const auto core = held()) {
		core->fingerCancel();
	}
	_owner = Owner::Lost;
}

void WidgetTouch::finish() {
	_holdTimer.cancel();
	if (const auto core = held()) {
		core->fingerCancel();
	}
	if (_owner != Owner::None
		&& !(_released && _owner == Owner::Widget)) {
		_arbiter.cancel();
	}
	_widget = nullptr;
	_core = nullptr;
	_owner = Owner::None;
	_released = false;
}

void WidgetTouch::decideMenu() {
	releaseMenu();
	const auto view = HistoryView::Element::HoveredLink();
	const auto widget = view
		? dynamic_cast<HistoryView::HtmlWidget*>(view->media())
		: nullptr;
	const auto core = widget
		? widget->menuPress(ClickHandler::getActive())
		: nullptr;
	_menuCore = base::make_weak(core);
	_menuTaken = (core != nullptr);
}

void WidgetTouch::releaseMenu() {
	if (const auto core = base::take(_menuCore).get()) {
		core->menuRelease(Modifiers());
	}
}

} // namespace Tgw
