/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_touch.h"

#include "base/platform/base_platform_info.h"
#include "base/event_filter.h"
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
constexpr auto kClaimDropMargin = crl::time(5);

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
	base::install_event_filter(_list, [weak = base::make_weak(this)](
			not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::MouseButtonPress
			|| type == QEvent::MouseButtonDblClick) {
			if (const auto strong = weak.get()) {
				strong->menuPressFiltered(
					static_cast<QMouseEvent*>(e.get()));
			}
		}
		return base::EventFilterResult::Continue;
	});
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
	const auto serial = ++_claimSerial;
	const auto accepted = core
		&& core->fingerPress(
			widget->cssPoint(at),
			Modifiers(),
			crl::guard(this, [=](twidget::WidgetGestureClaim claim) {
				claimed(serial, claim);
			}));
	if (!accepted) {
		return false;
	}
	_widget = base::make_weak(widget);
	_core = base::make_weak(core);
	_last = global;
	_owner = Owner::Pending;
	// WHY: TwidgetCore runs the same policy on its own clock. Starting this
	// press after the core's and deciding later steps before forwarding
	// keeps bubble cancellation ahead of the corresponding core input.
	_pressAt = Now();
	_pressLead = widget->gesturePoint(at);
	_claiming = true;
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
	if (_claiming) {
		_endGlobal = global;
		_holdTimer.callOnce(std::max(
			crl::time(std::ceil(
				_pressAt + kAnswerDeadline + kClaimDropMargin - Now())),
			kMinHoldDelay));
		return false;
	}
	const auto scroll = (_owner == Owner::Scroll);
	conclude(global);
	return scroll;
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
	if (e->reason() != QContextMenuEvent::Mouse || _menuReplay) {
		return false;
	} else if (_menuPending) {
		_menuPending = false;
		return deferMenu(e);
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
	return deferMenu(e);
}

void WidgetTouch::mouseRelease(Qt::MouseButton button) {
	if (button != Qt::RightButton) {
		return;
	}
	_menuAhead = false;
	if (!MenuOnRelease()) {
		_menuPending = false;
	}
	if (_menuState == MenuState::Waiting) {
		_menuRelease = Modifiers();
	} else {
		releaseMenu();
	}
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
	const auto move = Move{
		.lead = widget->gesturePoint(at),
		.css = widget->cssPoint(at),
		.at = Now(),
		.modifiers = Modifiers(),
	};
	if (_claiming) {
		_moves.push_back(move);
	} else {
		moveTo(core, move);
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
	const auto move = Move{
		.lead = widget->gesturePoint(at),
		.css = widget->cssPoint(at),
		.at = Now(),
		.modifiers = Modifiers(),
	};
	if (_claiming) {
		_releaseMove = move;
	} else {
		up(core, move);
	}
}

void WidgetTouch::claimed(uint64 serial, twidget::WidgetGestureClaim claim) {
	if (serial != _claimSerial || !_claiming) {
		return;
	}
	_claiming = false;
	const auto end = base::take(_endGlobal);
	const auto release = base::take(_releaseMove);
	const auto live = [&]() -> RendererCore* {
		if (_owner != Owner::Pending && _owner != Owner::Widget) {
			return nullptr;
		}
		const auto core = held();
		if (!_widget || !core) {
			lose();
			return nullptr;
		}
		return core;
	};
	_arbiter.down(_pressAt, _pressLead.x(), _pressLead.y(), claim);
	for (const auto &move : base::take(_moves)) {
		const auto core = live();
		if (!core) {
			break;
		}
		moveTo(core, move);
	}
	if (release) {
		if (const auto core = live()) {
			up(core, *release);
		}
	}
	if (end) {
		const auto bridge = TouchBridge();
		conclude(*end);
	} else if (_owner == Owner::Pending && !_released) {
		scheduleHold();
	}
}

void WidgetTouch::moveTo(not_null<RendererCore*> core, const Move &move) {
	// A hold the widget won is still locked by its first move past the slop.
	const auto verdict = _arbiter.move(move.at, move.lead.x(), move.lead.y());
	if (!verdict) {
		lose();
		return;
	}
	decide(*verdict);
	if (_owner == Owner::Pending || _owner == Owner::Widget) {
		core->fingerMove(move.css, move.modifiers);
	}
}

void WidgetTouch::up(not_null<RendererCore*> core, const Move &move) {
	const auto verdict = _arbiter.up(move.at, move.lead.x(), move.lead.y());
	if (!verdict) {
		lose();
	} else if (verdict->owner == twidget::GestureOwner::Bubble) {
		decide(*verdict);
	} else {
		_owner = Owner::Widget;
		core->fingerRelease(move.css, move.modifiers);
	}
}

void WidgetTouch::conclude(QPoint global) {
	const auto widget = _widget.get();
	const auto local = _list->mapFromGlobal(global);
	const auto menu = (_owner == Owner::Menu)
		&& widget
		&& (_viewAt(local) == widget->parent().get());
	finish();
	if (menu) {
		auto e = QContextMenuEvent(QContextMenuEvent::Mouse, local, global);
		_menu(&e);
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
	if (_claiming) {
		finish();
		return;
	} else if (_owner != Owner::Pending) {
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
	_claiming = false;
	_moves.clear();
	_releaseMove.reset();
	_endGlobal.reset();
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
	_claiming = false;
	_moves.clear();
	_releaseMove.reset();
	_endGlobal.reset();
}

void WidgetTouch::decideMenu() {
	releaseMenu();
	_menuEvent.reset();
	const auto view = HistoryView::Element::HoveredLink();
	const auto widget = view
		? dynamic_cast<HistoryView::HtmlWidget*>(view->media())
		: nullptr;
	const auto serial = ++_menuSerial;
	const auto core = widget
		? widget->menuPress(
			ClickHandler::getActive(),
			crl::guard(this, [=](bool taken) {
				menuDecided(serial, taken);
			}))
		: nullptr;
	_menuCore = base::make_weak(core);
	_menuWidget = core ? widget : nullptr;
	_menuState = core ? MenuState::Waiting : MenuState::Refused;
}

bool WidgetTouch::deferMenu(not_null<QContextMenuEvent*> e) {
	if (_menuState == MenuState::Waiting) {
		_menuEvent = MenuEvent{
			.global = e->globalPos(),
			.modifiers = e->modifiers(),
		};
	}
	return (_menuState != MenuState::Refused);
}

void WidgetTouch::menuDecided(uint64 serial, bool taken) {
	if (serial != _menuSerial || _menuState != MenuState::Waiting) {
		return;
	}
	_menuState = taken ? MenuState::Taken : MenuState::Refused;
	const auto event = base::take(_menuEvent);
	const auto widget = base::take(_menuWidget).get();
	if (taken) {
		if (_menuRelease) {
			releaseMenu();
		}
		return;
	}
	_menuCore = nullptr;
	_menuRelease.reset();
	if (!event || !widget) {
		return;
	}
	const auto local = _list->mapFromGlobal(event->global);
	if (_viewAt(local) != widget->parent().get()) {
		return;
	}
	auto e = QContextMenuEvent(
		QContextMenuEvent::Mouse,
		local,
		event->global,
		event->modifiers);
	const auto weak = base::make_weak(this);
	_menuReplay = true;
	QCoreApplication::sendEvent(_list, &e);
	if (weak) {
		_menuReplay = false;
	}
}

void WidgetTouch::menuPressFiltered(not_null<QMouseEvent*> e) {
	if (e->button() == Qt::RightButton
		|| _menuState != MenuState::Waiting) {
		return;
	}
	_menuState = MenuState::Dropped;
	_menuEvent.reset();
	releaseMenu();
}

void WidgetTouch::releaseMenu() {
	const auto modifiers = base::take(_menuRelease).value_or(Modifiers());
	if (const auto core = base::take(_menuCore).get()) {
		core->menuRelease(modifiers);
	}
}

} // namespace Tgw
