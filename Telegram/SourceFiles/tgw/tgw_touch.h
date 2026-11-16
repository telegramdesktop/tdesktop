/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/weak_ptr.h"
#include "twidget/gesture_policy.h"
#include "ui/controls/swipe_handler.h"

class HistoryInner;
class QContextMenuEvent;
class QMouseEvent;

namespace HistoryView {
class Element;
class HtmlWidget;
class ListWidget;
} // namespace HistoryView

namespace Tgw {

class RendererCore;

// While a list's mouse code runs for a finger, no widget core takes it.
class TouchBridge final {
public:
	TouchBridge();
	TouchBridge(const TouchBridge &other) = delete;
	TouchBridge &operator=(const TouchBridge &other) = delete;
	~TouchBridge();

	[[nodiscard]] static bool Active();

};

struct WidgetTouchArgs {
	not_null<QWidget*> list;
	Fn<HistoryView::Element*(QPoint point)> viewAt;
	Fn<QPoint(QPoint point, const HistoryView::Element *view)> mapToItem;
	Fn<bool(QPoint point)> covered;
	Fn<void(not_null<QContextMenuEvent*> e)> menu;
	Fn<void()> resetSpeed;
	Fn<void()> dropTouch;
};

class WidgetTouch final : public base::has_weak_ptr {
public:
	explicit WidgetTouch(WidgetTouchArgs &&args);
	~WidgetTouch();

	[[nodiscard]] bool begin(QPoint global);
	[[nodiscard]] std::optional<bool> update(QPoint global);
	[[nodiscard]] std::optional<bool> end(QPoint global);
	void cancel();
	void retire();

	void mousePress(Qt::MouseButton button);
	[[nodiscard]] bool mouseMenu(not_null<QContextMenuEvent*> e);
	void mouseRelease(Qt::MouseButton button);

	[[nodiscard]] Ui::Controls::SwipeHandoff swipeHandoff();

private:
	enum class Owner : uchar {
		None,
		Pending,
		Widget,
		Menu,
		Swipe,
		Scroll,
		Lost,
	};
	enum class MenuState : uchar {
		Refused,
		Waiting,
		Taken,
		Dropped,
	};
	struct MenuEvent {
		QPoint global;
		Qt::KeyboardModifiers modifiers;
	};
	struct Move {
		QPointF lead;
		QPointF css;
		double at = 0.;
		int modifiers = 0;
	};

	[[nodiscard]] bool handoff(not_null<QTouchEvent*> e);
	[[nodiscard]] RendererCore *held() const;
	[[nodiscard]] QPoint mediaPoint(
		not_null<HistoryView::HtmlWidget*> widget,
		QPoint global) const;
	void step(QPoint global);
	void release(QPoint global);
	void claimed(uint64 serial, twidget::WidgetGestureClaim claim);
	void moveTo(not_null<RendererCore*> core, const Move &move);
	void up(not_null<RendererCore*> core, const Move &move);
	void conclude(QPoint global);
	void decide(const twidget::GestureVerdict &verdict);
	void scheduleHold();
	void hold();
	void lose();
	void finish();
	void decideMenu();
	[[nodiscard]] bool deferMenu(not_null<QContextMenuEvent*> e);
	void menuDecided(uint64 serial, bool taken);
	void menuPressFiltered(not_null<QMouseEvent*> e);
	void releaseMenu();

	const not_null<QWidget*> _list;
	const Fn<HistoryView::Element*(QPoint)> _viewAt;
	const Fn<QPoint(QPoint, const HistoryView::Element*)> _mapToItem;
	const Fn<bool(QPoint)> _covered;
	const Fn<void(not_null<QContextMenuEvent*>)> _menu;
	const Fn<void()> _resetSpeed;
	const Fn<void()> _dropTouch;
	twidget::GestureArbiter _arbiter;
	base::Timer _holdTimer;
	base::weak_ptr<HistoryView::HtmlWidget> _widget;
	base::weak_ptr<RendererCore> _core;
	base::weak_ptr<RendererCore> _menuCore;
	base::weak_ptr<HistoryView::HtmlWidget> _menuWidget;
	std::optional<MenuEvent> _menuEvent;
	std::optional<int> _menuRelease;
	std::vector<Move> _moves;
	std::optional<Move> _releaseMove;
	std::optional<QPoint> _endGlobal;
	QPointF _pressLead;
	double _pressAt = 0.;
	uint64 _claimSerial = 0;
	uint64 _menuSerial = 0;
	QPoint _last;
	Owner _owner = Owner::None;
	MenuState _menuState = MenuState::Refused;
	bool _released = false;
	bool _claiming : 1 = false;
	bool _menuPending : 1 = false;
	bool _menuReplay : 1 = false;
	bool _menuAhead : 1 = false;

};

class ListTouch final {
public:
	using Floating = Fn<bool(
		not_null<HistoryView::Element*> view,
		int itemtop,
		int top)>;

	[[nodiscard]] static std::unique_ptr<WidgetTouch> Create(
		not_null<HistoryInner*> list);
	[[nodiscard]] static std::unique_ptr<WidgetTouch> Create(
		not_null<HistoryView::ListWidget*> list);

private:
	template <typename List>
	[[nodiscard]] static bool Covered(
		not_null<List*> list,
		QPoint point,
		Fn<int(not_null<HistoryView::Element*> view)> available,
		Fn<bool(not_null<HistoryView::Element*> view)> clickable);

};

} // namespace Tgw
