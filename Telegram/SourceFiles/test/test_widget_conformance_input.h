/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "base/timer.h"
#include "test/test_chat_menu.h"

#include <QtCore/QJsonObject>
#include <QtCore/QPoint>
#include <QtCore/QPointer>
#include <QtCore/QString>

#include <optional>
#include <vector>

class HistoryItem;
class QWidget;

// The widget conformance runner's timed pointer sequences: the manifest's
// press, drag, dblclick and rightclick actions as mouse events sent to the
// chat's own HistoryInner, spaced in real time by a timer. A held button
// needs the widget core's clock to run while it is down, and TwidgetCore
// keeps only the latest move of a pointer until its next frame, so a
// synchronous burst (Test::Drag) reaches a widget as one move. Taps, the
// wheel and keys stay with test_widgets.h. Internal to the runner's action
// stages; reference: docs/helpers/test_widget_conformance.md.

namespace Test {

// The manifest's action names: "press" (hold for its ms), "drag",
// "dblclick" and "rightclick".
enum class ConformancePointerKind {
	Press,
	Drag,
	DoubleClick,
	RightClick,
};

// press, move, release, dblclick (Qt's MouseButtonDblClick), menu
// (Test::OpenChatListContextMenu: its enter, context-menu event and leave)
// and leave.
enum class ConformancePointerEventType {
	Press,
	Move,
	Release,
	DoubleClick,
	Menu,
	Leave,
};

// CLIENTS.md's "about 10 steps", for a drag that names none.
inline constexpr auto kConformanceDragSteps = 10;

// |list| is the chat's HistoryInner. |from| is the press point and |to| a
// drag's release point (other kinds ignore it), both in the list's own
// logical px, as the show helper's hover point is. |hold| is a press's ms,
// |steps| a drag's moves (0 or less is kConformanceDragSteps). |item|
// answers the message a right click's menu must belong to, called in the
// turn the menu is opened. |observe|, when given, is called right before
// and right after every event to read the caller's state (the widget
// core's pressed() during a hold, say); it must only read.
struct ConformancePointerArgs {
	ConformancePointerKind kind = ConformancePointerKind::Press;
	QWidget *list = nullptr;
	QPoint from;
	QPoint to;
	crl::time hold = 0;
	int steps = 0;
	Fn<HistoryItem*()> item;
	Fn<QString()> observe;
};

// One planned event: the least |gap| in ms after the previous event's
// delivery, and for a mouse event its button (Qt::NoButton for a move).
struct ConformancePointerStep {
	ConformancePointerEventType type = ConformancePointerEventType::Press;
	Qt::MouseButton button = Qt::NoButton;
	QPoint local;
	crl::time gap = 0;
};

// One event as it was sent. |due|, |at| and |handled| are whole ms of
// crl::now() from the first event's send: the previous event's |handled|
// plus |gap|, when it was sent - only once the clock is past |due|, so no
// gap the list sees is ever shorter than planned - and when its handling
// (Test::Settle's drain included) returned. |turn| numbers the event-loop
// turns the sequence delivered in: an event with a gap always has a turn
// of its own. |button|
// and |buttons| are the mouse event's; |held| is what the list holds after
// it. A leave's |local| and |global| are the point the pointer left from.
// |before| and |after| are the caller's observations. |aborting| marks a
// release or leave abort() sent, which the plan did not have.
struct ConformancePointerEvent {
	QString before;
	QString after;
	QPoint local;
	QPoint global;
	crl::time gap = 0;
	crl::time due = 0;
	crl::time at = 0;
	crl::time handled = 0;
	int turn = 0;
	ConformancePointerEventType type = ConformancePointerEventType::Press;
	Qt::MouseButton button = Qt::NoButton;
	Qt::MouseButtons buttons = Qt::NoButton;
	Qt::MouseButtons held = Qt::NoButton;
	bool delivered = false;
	bool accepted = false;
	bool aborting = false;
};

// Refused before anything is sent: no-list (null, or destroyed before the
// start), not-history-inner (another widget; the runner shows its cards
// only in the legacy list), point-outside (|from|, or a drag's |to|,
// outside the list's rect()). Later: receiver-gone (the list was destroyed
// before an event, or by its own handler; whatever it held stays in
// |heldAtEnd|) and aborted (abort() ended it).
enum class ConformancePointerRefusal {
	None,
	NoList,
	NotHistoryInner,
	PointOutside,
	ReceiverGone,
	Aborted,
};

// The sequence's reading. |startedAt| is the crl::now() of the first send
// and |lastAt| the one at which the latest event's handling returned, so
// the caller can keep its gap to the next action. |elapsed| runs from the
// first press's |handled| to the last release's |at|, the least time the
// list had a button down; |minGap| is the shortest time from an event's
// |handled| to the send of a planned event with a gap after it, |maxLate|
// the most a planned event came after its |due|. |moves| counts the moves
// sent. |trigger| is "press" or "release"
// for a right click: the context-menu trigger tgw_touch.cpp's
// MenuOnRelease() reads, which decides whether the menu follows the press
// or the release. |menu| is that reader's own reading; |menuHidden| says
// this sequence hid the menu it built and attributed, and |menuAfter| is
// that menu's lifecycle right after. |left| means the last event the list
// got was a leave. |complete| means every planned event was sent to a live
// list; delivered() is that, finished and unrefused.
struct ConformancePointerReading {
	std::vector<ConformancePointerEvent> events;
	ChatListMenuReading menu;
	QString list;
	QString trigger;
	QString menuAfter;
	QString abortReason;
	QString reason;
	QPoint from;
	QPoint to;
	crl::time hold = 0;
	crl::time startedAt = 0;
	crl::time lastAt = 0;
	crl::time elapsed = 0;
	crl::time minGap = 0;
	crl::time maxLate = 0;
	int steps = 0;
	int moves = 0;
	ConformancePointerKind kind = ConformancePointerKind::Press;
	ConformancePointerRefusal refusal = ConformancePointerRefusal::None;
	Qt::MouseButtons heldAtEnd = Qt::NoButton;
	bool started = false;
	bool menuOpened = false;
	bool menuHidden = false;
	bool left = false;
	bool complete = false;
	bool aborted = false;
	bool finished = false;

	[[nodiscard]] bool delivered() const {
		return finished
			&& complete
			&& (refusal == ConformancePointerRefusal::None);
	}
};

// One timed sequence, owned by the caller's stage state.
//
// start() sends the first event at once - call it in the run of the stage
// right after the show helper's hover - and a base::Timer of the sequence
// sends the rest, each event wrapped in Test::Settle with its local and
// global position, Qt's button and buttons and no modifiers:
// - press: press, then the release once |hold| ms passed since the press
//   was handled;
// - drag: press, |steps| moves from |from| towards |to| with the left
//   button held, each at least 24 ms after the event before it, then the
//   release at |to| 24 ms after the last move. Each move has its own
//   event-loop turn: an overdue move is sent late, never with the next one
//   in one burst. A drag on a widget card presses the card's own link,
//   which has no drag text, so HistoryInner::prepareDrag() makes no mime
//   data and no platform drag starts;
// - dblclick: press, release 40 ms later, MouseButtonDblClick 60 ms after
//   that and its release 40 ms later: the order QWidgetWindow gives a
//   widget, whose second press becomes the double click;
// - rightclick: the right press and, 60 ms later, its release, with
//   Test::OpenChatListContextMenu for |item| at |from| in the turn of the
//   press on a press trigger and of the release on a release trigger, as
//   QWidgetWindow sends the context-menu event; a menu that reader built
//   and attributed is hidden at once (Ui::PopupMenu::hideMenu(true)), and a
//   refused one is left to the reader, which hides only what it built. The
//   reader's enter resolves the real cursor first (HistoryInner's
//   enterEventHook) and its leave ends the list's hover even while the
//   right button is still down.
// Every gap is measured from the end of the handling of the event before,
// and a timer that fires early is re-armed for the rest, so a late timer
// may lengthen a hold or a drag and never shortens one. Every completed
// sequence ends with a leave: its own, or on a release trigger the
// reader's.
//
// abort() cancels the timer, sends a release for every button still held
// and a leave, and ends the sequence aborted; with buttons held and the
// list already gone it ends receiver-gone. The caller aborts before it
// removes the fixture's messages. Destroying a sequence cancels its timer
// and sends nothing.
class ConformancePointer final {
public:
	explicit ConformancePointer(ConformancePointerArgs args);

	void start();
	void abort(const QString &reason);

	[[nodiscard]] bool finished() const;
	[[nodiscard]] const ConformancePointerReading &reading() const;

private:
	void step();
	void deliver(
		ConformancePointerStep planned,
		crl::time due,
		bool aborting);
	void deliverMouse(
		not_null<QWidget*> list,
		const ConformancePointerStep &planned,
		ConformancePointerEvent &record);
	void deliverMenu(
		not_null<QWidget*> list,
		ConformancePointerEvent &record);
	void finish(ConformancePointerRefusal refusal, QString reason);
	[[nodiscard]] QString observe() const;

	const ConformancePointerArgs _args;
	const QPointer<QWidget> _list;
	std::vector<ConformancePointerStep> _plan;
	ConformancePointerReading _reading;
	base::Timer _timer;
	QPoint _point;
	crl::time _last = 0;
	Qt::MouseButtons _held = Qt::NoButton;
	int _next = 0;
	int _turn = 0;

};

// The manifest action name's kind, or none for any other action.
[[nodiscard]] std::optional<ConformancePointerKind> ConformancePointerKindOf(
	const QString &action);
[[nodiscard]] QString ConformancePointerKindName(ConformancePointerKind kind);
[[nodiscard]] QString ConformancePointerEventName(
	ConformancePointerEventType type);
// none, no-list, not-history-inner, point-outside, receiver-gone, aborted.
[[nodiscard]] QString ConformancePointerRefusalName(
	ConformancePointerRefusal refusal);

// The sum of a sequence's planned gaps, the least time it takes: for the
// caller's stage timeout.
[[nodiscard]] crl::time ConformancePointerPlannedMs(
	ConformancePointerKind kind,
	crl::time hold,
	int steps);

// The whole reading, every event included, for the run file's action
// reading and the runner's rows; a right click's has its "menu".
[[nodiscard]] QJsonObject ConformancePointerJson(
	const ConformancePointerReading &reading);

} // namespace Test
