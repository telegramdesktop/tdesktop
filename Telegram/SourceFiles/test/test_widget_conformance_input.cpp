/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_widget_conformance_input.h"

#include "base/platform/base_platform_info.h"
#include "test/test_capture.h"
#include "test/test_chat_show.h"
#include "test/test_widgets.h"
#include "ui/widgets/popup_menu.h"

#include <QtCore/QEvent>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonValue>
#include <QtCore/QStringList>
#include <QtGui/QGuiApplication>
#include <QtGui/QMouseEvent>
#include <QtGui/QStyleHints>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>

#include <algorithm>
#include <array>
#include <utility>

namespace Test {
namespace {

using Kind = ConformancePointerKind;
using Type = ConformancePointerEventType;
using Refusal = ConformancePointerRefusal;

// More than one 60 Hz frame between two drag events, so the core's
// per-frame move coalescing keeps every move.
constexpr auto kDragStep = crl::time(24);

// The double click: release 40 ms after the press, the second press (the
// double-click event) 100 ms after it and its release at 140 ms.
constexpr auto kDoubleRelease = crl::time(40);
constexpr auto kDoublePress = crl::time(60);

// A right click's release after its press.
constexpr auto kRightRelease = crl::time(60);

// The rule tgw_touch.cpp's MenuOnRelease() applies, Qt guard included: on
// a release trigger the context-menu event follows the release.
[[nodiscard]] bool MenuOnRelease() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
	return (QGuiApplication::styleHints()->contextMenuTrigger()
		== Qt::ContextMenuTrigger::Release);
#else // Qt >= 6.8.0
	return Platform::IsWindows();
#endif // Qt < 6.8.0
}

[[nodiscard]] int DragSteps(int steps) {
	return (steps > 0) ? steps : kConformanceDragSteps;
}

[[nodiscard]] std::vector<ConformancePointerStep> Plan(
		const ConformancePointerArgs &args,
		bool menuOnRelease) {
	auto result = std::vector<ConformancePointerStep>();
	const auto add = [&](
			Type type,
			Qt::MouseButton button,
			QPoint local,
			crl::time gap) {
		result.push_back({
			.type = type,
			.button = button,
			.local = local,
			.gap = gap,
		});
	};
	const auto from = args.from;
	auto last = from;
	switch (args.kind) {
	case Kind::Press:
		add(Type::Press, Qt::LeftButton, from, 0);
		add(
			Type::Release,
			Qt::LeftButton,
			from,
			std::max(args.hold, crl::time(0)));
		break;
	case Kind::Drag: {
		const auto steps = DragSteps(args.steps);
		add(Type::Press, Qt::LeftButton, from, 0);
		for (auto i = 1; i <= steps; ++i) {
			const auto local = from + ((args.to - from) * i) / steps;
			add(Type::Move, Qt::NoButton, local, kDragStep);
		}
		add(Type::Release, Qt::LeftButton, args.to, kDragStep);
		last = args.to;
	} break;
	case Kind::DoubleClick:
		add(Type::Press, Qt::LeftButton, from, 0);
		add(Type::Release, Qt::LeftButton, from, kDoubleRelease);
		add(Type::DoubleClick, Qt::LeftButton, from, kDoublePress);
		add(Type::Release, Qt::LeftButton, from, kDoubleRelease);
		break;
	case Kind::RightClick:
		add(Type::Press, Qt::RightButton, from, 0);
		if (!menuOnRelease) {
			add(Type::Menu, Qt::RightButton, from, 0);
		}
		add(Type::Release, Qt::RightButton, from, kRightRelease);
		if (menuOnRelease) {
			add(Type::Menu, Qt::RightButton, from, 0);
			return result;
		}
		break;
	}
	add(Type::Leave, Qt::NoButton, last, 0);
	return result;
}

[[nodiscard]] QEvent::Type MouseEventType(Type type) {
	switch (type) {
	case Type::Press: return QEvent::MouseButtonPress;
	case Type::Move: return QEvent::MouseMove;
	case Type::Release: return QEvent::MouseButtonRelease;
	case Type::DoubleClick: return QEvent::MouseButtonDblClick;
	case Type::Menu:
	case Type::Leave: break;
	}
	return QEvent::None;
}

// The buttons a mouse event of |type| carries, which are also what the
// list holds after it: Qt's press and double click include their button,
// a release excludes it.
[[nodiscard]] Qt::MouseButtons HeldAfter(
		Type type,
		Qt::MouseButton button,
		Qt::MouseButtons held) {
	auto result = held;
	if (type == Type::Press || type == Type::DoubleClick) {
		result.setFlag(button, true);
	} else if (type == Type::Release) {
		result.setFlag(button, false);
	}
	return result;
}

[[nodiscard]] QString ButtonsText(Qt::MouseButtons buttons) {
	constexpr auto kNames = std::array{
		std::pair{ Qt::LeftButton, "left" },
		std::pair{ Qt::RightButton, "right" },
		std::pair{ Qt::MiddleButton, "middle" },
	};
	auto names = QStringList();
	auto rest = buttons;
	for (const auto &[button, name] : kNames) {
		if (buttons.testFlag(button)) {
			names.push_back(QString::fromLatin1(name));
			rest.setFlag(button, false);
		}
	}
	if (rest) {
		names.push_back(u"other"_q);
	}
	return names.isEmpty() ? u"none"_q : names.join(u'+');
}

[[nodiscard]] QString PointText(QPoint point) {
	return u"%1,%2"_q.arg(point.x()).arg(point.y());
}

[[nodiscard]] QJsonArray PointJson(QPoint point) {
	return QJsonArray{ point.x(), point.y() };
}

[[nodiscard]] QString HeldText(Qt::MouseButtons held) {
	return held ? (u" with "_q + ButtonsText(held) + u" held"_q) : QString();
}

// The last event the list got was a leave: the sequence's own, or the
// one the context-menu reader ends with.
[[nodiscard]] bool EndsWithLeave(const ConformancePointerReading &reading) {
	if (reading.events.empty() || !reading.events.back().delivered) {
		return false;
	}
	const auto type = reading.events.back().type;
	return (type == Type::Leave)
		|| ((type == Type::Menu) && reading.menu.leaveSent);
}

// The figures of a finished sequence, from its events alone.
void Summarize(ConformancePointerReading &reading, int planned) {
	auto firstPress = std::optional<crl::time>();
	auto lastRelease = std::optional<crl::time>();
	auto minGap = std::optional<crl::time>();
	auto previous = std::optional<crl::time>();
	auto sent = 0;
	reading.moves = 0;
	reading.maxLate = 0;
	for (const auto &event : reading.events) {
		if (!event.delivered) {
			previous = std::nullopt;
			continue;
		}
		if ((event.type == Type::Press) && !firstPress) {
			firstPress = event.handled;
		} else if (event.type == Type::Release) {
			lastRelease = event.at;
		} else if (event.type == Type::Move) {
			++reading.moves;
		}
		if (!event.aborting) {
			++sent;
			if ((event.gap > 0) && previous) {
				const auto gap = event.at - *previous;
				minGap = minGap ? std::min(*minGap, gap) : gap;
			}
			reading.maxLate = std::max(
				reading.maxLate,
				event.at - event.due);
		}
		previous = event.handled;
	}
	reading.elapsed = (firstPress && lastRelease)
		? (*lastRelease - *firstPress)
		: 0;
	reading.minGap = minGap.value_or(0);
	reading.complete = !reading.aborted && (sent == planned);
	reading.left = EndsWithLeave(reading);
}

[[nodiscard]] QJsonObject MenuJson(const ConformancePointerReading &reading) {
	const auto &menu = reading.menu;
	auto texts = QJsonArray();
	for (const auto &text : menu.menu.texts) {
		texts.push_back(text);
	}
	return QJsonObject{
		{ u"opened"_q, reading.menuOpened },
		{ u"trigger"_q, reading.trigger },
		{ u"built"_q, menu.built() },
		{ u"refusal"_q, ChatListMenuRefusalName(menu.refusal) },
		{ u"reason"_q, menu.reason },
		{ u"attributed"_q, menu.attributed },
		{ u"listMenu"_q, menu.listMenu },
		{ u"enterSent"_q, menu.enterSent },
		{ u"menuRead"_q, menu.menuRead },
		{ u"leaveSent"_q, menu.leaveSent },
		{ u"readerHid"_q, menu.menuHidden },
		{ u"hidden"_q, reading.menuHidden },
		{ u"after"_q, reading.menuAfter },
		{ u"texts"_q, texts },
		{ u"details"_q, ChatListMenuDetails(menu) },
	};
}

} // namespace

ConformancePointer::ConformancePointer(ConformancePointerArgs args)
: _args(std::move(args))
, _list(_args.list) {
	const auto onRelease = MenuOnRelease();
	_plan = Plan(_args, onRelease);
	_reading.kind = _args.kind;
	_reading.from = _args.from;
	_reading.to = (_args.kind == Kind::Drag) ? _args.to : _args.from;
	_reading.hold = (_args.kind == Kind::Press) ? _args.hold : 0;
	_reading.steps = (_args.kind == Kind::Drag)
		? DragSteps(_args.steps)
		: 0;
	if (_args.kind == Kind::RightClick) {
		_reading.trigger = onRelease ? u"release"_q : u"press"_q;
	}
	_point = _args.from;
	_timer.setCallback([=] {
		step();
	});
}

void ConformancePointer::start() {
	if (_reading.started || _reading.finished) {
		return;
	}
	_reading.started = true;
	const auto list = _list.data();
	if (!list) {
		finish(
			Refusal::NoList,
			u"no list was handed to the sequence, or it was destroyed "
			u"before the start; nothing was sent"_q);
		return;
	}
	_reading.list = WidgetDescription(list);
	if (ChatListShowKindOf(list) != ChatListShowKind::History) {
		finish(
			Refusal::NotHistoryInner,
			u"%1 is a %2, not the legacy HistoryInner the runner shows its "
			u"cards in; nothing was sent"_q.arg(
				_reading.list,
				ChatListShowKindName(ChatListShowKindOf(list))));
		return;
	}
	const auto rect = list->rect();
	auto outside = std::optional<QPoint>();
	if (!rect.contains(_reading.from)) {
		outside = _reading.from;
	} else if (!rect.contains(_reading.to)) {
		outside = _reading.to;
	}
	if (outside) {
		finish(
			Refusal::PointOutside,
			u"the point %1 is outside the list's rect %2, where no "
			u"pointer reaches it; nothing was sent"_q.arg(
				PointText(*outside),
				RectText(rect)));
		return;
	}
	_reading.startedAt = crl::now();
	step();
}

void ConformancePointer::abort(const QString &reason) {
	if (_reading.finished) {
		return;
	}
	_timer.cancel();
	_reading.aborted = true;
	_reading.abortReason = reason;
	if (!_reading.started) {
		finish(
			Refusal::Aborted,
			u"aborted before the start; nothing was sent"_q);
		return;
	} else if (!_list) {
		finish(
			_held ? Refusal::ReceiverGone : Refusal::Aborted,
			u"aborted after the list was destroyed%1; nothing more could "
			u"be sent"_q.arg(HeldText(_held)));
		return;
	}
	++_turn;
	const auto now = crl::now() - _reading.startedAt;
	for (const auto button : { Qt::LeftButton, Qt::RightButton }) {
		if (_held.testFlag(button) && !_reading.finished) {
			deliver({
				.type = Type::Release,
				.button = button,
				.local = _point,
			}, now, true);
		}
	}
	if (!_reading.finished && !EndsWithLeave(_reading)) {
		deliver({
			.type = Type::Leave,
			.local = _point,
		}, now, true);
	}
	if (!_reading.finished) {
		finish(
			Refusal::Aborted,
			u"aborted after %1 of %2 planned events; every held button "
			u"was released and the list was left"_q
				.arg(_next)
				.arg(int(_plan.size())));
	}
}

bool ConformancePointer::finished() const {
	return _reading.finished;
}

const ConformancePointerReading &ConformancePointer::reading() const {
	return _reading;
}

// One timed event per turn: an event with a gap waits for a turn of its
// own even when it is overdue, and the events without one follow it in
// the same turn, as a context menu follows its press.
void ConformancePointer::step() {
	++_turn;
	auto sentThisTurn = false;
	while (!_reading.finished && (_next < int(_plan.size()))) {
		const auto planned = _plan[_next];
		const auto due = _last + planned.gap;
		const auto now = crl::now() - _reading.startedAt;
		const auto wait = due + 1 - now; // a whole-ms clock past |due|
		if ((planned.gap > 0) && (sentThisTurn || (wait > 0))) {
			_timer.callOnce(std::max(wait, crl::time(1)), Qt::PreciseTimer);
			return;
		}
		++_next;
		deliver(planned, due, false);
		sentThisTurn = true;
	}
	if (!_reading.finished) {
		finish(Refusal::None, QString());
	}
}

void ConformancePointer::deliver(
		ConformancePointerStep planned,
		crl::time due,
		bool aborting) {
	auto record = ConformancePointerEvent{
		.local = planned.local,
		.gap = planned.gap,
		.due = due,
		.turn = _turn,
		.type = planned.type,
		.button = planned.button,
		.aborting = aborting,
	};
	const auto list = _list.data();
	if (!list) {
		record.at = crl::now() - _reading.startedAt;
		record.handled = record.at;
		_reading.events.push_back(std::move(record));
		finish(
			Refusal::ReceiverGone,
			u"the list was destroyed before event %1 (%2)%3; it was not "
			u"sent"_q
				.arg(int(_reading.events.size()))
				.arg(ConformancePointerEventName(planned.type))
				.arg(HeldText(_held)));
		return;
	}
	record.before = observe();
	record.at = crl::now() - _reading.startedAt;
	if (planned.type == Type::Menu) {
		deliverMenu(list, record);
	} else if (planned.type == Type::Leave) {
		record.global = list->mapToGlobal(planned.local);
		auto leave = QEvent(QEvent::Leave);
		Settle([&] {
			QApplication::sendEvent(list, &leave);
		});
		record.delivered = true;
		record.accepted = leave.isAccepted();
	} else {
		deliverMouse(list, planned, record);
	}
	record.held = _held;
	record.handled = crl::now() - _reading.startedAt;
	_last = record.handled;
	_reading.lastAt = _reading.startedAt + record.handled;
	const auto alive = (_list.data() != nullptr);
	record.after = alive ? observe() : u"list destroyed"_q;
	_reading.events.push_back(std::move(record));
	if (!alive) {
		finish(
			Refusal::ReceiverGone,
			u"the list was destroyed while it handled event %1 (%2)%3"_q
				.arg(int(_reading.events.size()))
				.arg(ConformancePointerEventName(planned.type))
				.arg(HeldText(_held)));
	}
}

void ConformancePointer::deliverMouse(
		not_null<QWidget*> list,
		const ConformancePointerStep &planned,
		ConformancePointerEvent &record) {
	const auto buttons = HeldAfter(planned.type, planned.button, _held);
	record.global = list->mapToGlobal(planned.local);
	record.buttons = buttons;
	auto event = QMouseEvent(
		MouseEventType(planned.type),
		QPointF(planned.local),
		QPointF(record.global),
		planned.button,
		buttons,
		Qt::NoModifier);
	Settle([&] {
		QApplication::sendEvent(list.get(), &event);
	});
	record.delivered = true;
	record.accepted = event.isAccepted();
	_held = buttons;
	_point = planned.local;
}

void ConformancePointer::deliverMenu(
		not_null<QWidget*> list,
		ConformancePointerEvent &record) {
	const auto item = _args.item ? _args.item() : nullptr;
	_reading.menuOpened = true;
	_reading.menu = OpenChatListContextMenu(list, item, record.local);
	const auto &menu = _reading.menu;
	record.global = menu.menu.global;
	record.delivered = menu.menuRead && menu.menu.delivered;
	record.accepted = menu.menu.accepted;
	if (menu.built()) {
		const auto popup = dynamic_cast<Ui::PopupMenu*>(
			menu.menu.menu.data());
		if (popup) {
			Settle([&] {
				popup->hideMenu(true);
			});
			_reading.menuHidden = true;
		}
	}
	_reading.menuAfter = ContextMenuLifecycleText(
		ReadContextMenuLifecycle(menu.menu));
}

void ConformancePointer::finish(Refusal refusal, QString reason) {
	_timer.cancel();
	if (_reading.finished) {
		return;
	}
	_reading.finished = true;
	_reading.refusal = refusal;
	_reading.reason = std::move(reason);
	_reading.heldAtEnd = _held;
	Summarize(_reading, int(_plan.size()));
}

QString ConformancePointer::observe() const {
	return _args.observe ? _args.observe() : QString();
}

std::optional<ConformancePointerKind> ConformancePointerKindOf(
		const QString &action) {
	constexpr auto kKinds = std::array{
		Kind::Press,
		Kind::Drag,
		Kind::DoubleClick,
		Kind::RightClick,
	};
	for (const auto kind : kKinds) {
		if (action == ConformancePointerKindName(kind)) {
			return kind;
		}
	}
	return std::nullopt;
}

QString ConformancePointerKindName(ConformancePointerKind kind) {
	switch (kind) {
	case Kind::Press: return u"press"_q;
	case Kind::Drag: return u"drag"_q;
	case Kind::DoubleClick: return u"dblclick"_q;
	case Kind::RightClick: return u"rightclick"_q;
	}
	return u"unknown"_q;
}

QString ConformancePointerEventName(ConformancePointerEventType type) {
	switch (type) {
	case Type::Press: return u"press"_q;
	case Type::Move: return u"move"_q;
	case Type::Release: return u"release"_q;
	case Type::DoubleClick: return u"dblclick"_q;
	case Type::Menu: return u"menu"_q;
	case Type::Leave: return u"leave"_q;
	}
	return u"unknown"_q;
}

QString ConformancePointerRefusalName(ConformancePointerRefusal refusal) {
	switch (refusal) {
	case Refusal::None: return u"none"_q;
	case Refusal::NoList: return u"no-list"_q;
	case Refusal::NotHistoryInner: return u"not-history-inner"_q;
	case Refusal::PointOutside: return u"point-outside"_q;
	case Refusal::ReceiverGone: return u"receiver-gone"_q;
	case Refusal::Aborted: return u"aborted"_q;
	}
	return u"unknown"_q;
}

crl::time ConformancePointerPlannedMs(
		ConformancePointerKind kind,
		crl::time hold,
		int steps) {
	auto result = crl::time(0);
	const auto plan = Plan({
		.kind = kind,
		.hold = hold,
		.steps = steps,
	}, MenuOnRelease());
	for (const auto &planned : plan) {
		result += planned.gap;
	}
	return result;
}

QJsonObject ConformancePointerJson(const ConformancePointerReading &reading) {
	auto events = QJsonArray();
	for (const auto &event : reading.events) {
		events.push_back(QJsonObject{
			{ u"type"_q, ConformancePointerEventName(event.type) },
			{ u"button"_q, ButtonsText(event.button) },
			{ u"buttons"_q, ButtonsText(event.buttons) },
			{ u"held"_q, ButtonsText(event.held) },
			{ u"local"_q, PointJson(event.local) },
			{ u"global"_q, PointJson(event.global) },
			{ u"gap"_q, qint64(event.gap) },
			{ u"due"_q, qint64(event.due) },
			{ u"at"_q, qint64(event.at) },
			{ u"handled"_q, qint64(event.handled) },
			{ u"turn"_q, event.turn },
			{ u"delivered"_q, event.delivered },
			{ u"accepted"_q, event.accepted },
			{ u"aborting"_q, event.aborting },
			{ u"before"_q, event.before },
			{ u"after"_q, event.after },
		});
	}
	auto result = QJsonObject{
		{ u"kind"_q, ConformancePointerKindName(reading.kind) },
		{ u"list"_q, reading.list },
		{ u"from"_q, PointJson(reading.from) },
		{ u"to"_q, PointJson(reading.to) },
		{ u"hold"_q, qint64(reading.hold) },
		{ u"steps"_q, reading.steps },
		{ u"events"_q, events },
		{ u"elapsed"_q, qint64(reading.elapsed) },
		{ u"minGap"_q, qint64(reading.minGap) },
		{ u"maxLate"_q, qint64(reading.maxLate) },
		{ u"moves"_q, reading.moves },
		{ u"heldAtEnd"_q, ButtonsText(reading.heldAtEnd) },
		{ u"left"_q, reading.left },
		{ u"complete"_q, reading.complete },
		{ u"aborted"_q, reading.aborted },
		{ u"abortReason"_q, reading.abortReason },
		{ u"refusal"_q, ConformancePointerRefusalName(reading.refusal) },
		{ u"reason"_q, reading.reason },
		{ u"finished"_q, reading.finished },
		{ u"delivered"_q, reading.delivered() },
	};
	if (reading.kind == Kind::RightClick) {
		result.insert(u"menu"_q, MenuJson(reading));
	}
	return result;
}

} // namespace Test

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
