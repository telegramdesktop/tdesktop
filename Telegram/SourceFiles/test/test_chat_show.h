/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "data/data_msg_id.h"

#include <QtCore/QPoint>
#include <QtCore/QPointer>
#include <QtCore/QRect>
#include <QtCore/QString>

#include <memory>

class HistoryItem;
class QWidget;

namespace HistoryView {
class Element;
} // namespace HistoryView

namespace Ui {
class ElasticScroll;
} // namespace Ui

namespace Test {

class Runner;

// Shows one message of a chat list before the caller's input: its view, or
// a rectangle inside it, wholly in the list's viewport, laid out, painted
// and ready, then optionally hovered. Usage, limits, rows and refusals:
// docs/helpers/test_chat_show.md.

// |list| is a HistoryInner, a HistoryView::ListWidget or an
// AdminLog::InnerWidget, the owned widget of its Ui::ElasticScroll. |item|
// names the message; it is looked up again through the list's session on
// every sample, and so is the list's own view of it (never
// item->mainView() taken on trust).
struct ChatListShowTarget {
	QWidget *list = nullptr;
	FullMsgId item;
};

// The list's own current view of the message, valid only during the
// synchronous call that receives it. Keep no pointer to it.
using ChatListShowView = not_null<const HistoryView::Element*>;

// |name| labels the stage ("show in chat list: <name>") and its rows.
// |resolve| runs once, when the stage begins, so a list or message from an
// earlier stage can be named. The four callbacks run on many samples, in
// the helper's paint filter included, so they must only read:
// - |rect|: the element-local rectangle to show, in logical pixels with
//   (0, 0) at the view's top-left (selection-mode offset included); null
//   means the whole view, QRect(0, 0, view->width(), view->height());
// - |ready|: the caller's readiness, asked only once the rectangle is in
//   view, its layout settled and list paints after the helper's repaint
//   request covered it; null means ready;
// - |readyDetails|: the caller's state, read with every readiness answer
//   and once more on the current view when the deadline passes;
// - |hover|: the element-local point to hover after acceptance, inside
//   |rect|; null sends no input at all.
// |timeout| is the helper's own deadline, in ms from the stage's run.
struct ChatListShowArgs {
	QString name;
	Fn<ChatListShowTarget()> resolve;
	Fn<QRect(ChatListShowView)> rect;
	Fn<bool(ChatListShowView)> ready;
	Fn<QString(ChatListShowView)> readyDetails;
	Fn<QPoint(ChatListShowView)> hover;
	Fn<QString()> skipReason;
	crl::time timeout = crl::time(10000);
};

// Every refusal's reason names the values that decided it, and the reading
// keeps the last geometry it saw even when the view is gone by then.
// - no-list: no list, not one of the three lists, not owned by a
//   Ui::ElasticScroll, or destroyed while the helper waited;
// - no-target: no message id, no such message in the list's session, or
//   the message was destroyed while the helper waited;
// - no-view: the list showed no view of its own when the stage began
//   (nothing is scrolled), or lost it after a scroll and had not shown it
//   again by the deadline;
// - cannot-fit: with a usable layout that read the same for the settle
//   time, the rectangle is not inside the view's bounds, or is taller or
//   wider than the visible message area (the helper scrolls only
//   vertically);
// - out-of-view: the rectangle fits, but was not wholly in view at the
//   deadline;
// - not-ready: in view at the deadline, but the layout, the paint or the
//   caller's readiness had not completed; the reason names which;
// - hover-outside: the hover point is outside the shown rectangle; no
//   input was sent;
// - hover-unresolved: after the enter and the move the list did not
//   resolve the view at the point, or the view or its geometry changed
//   while their postponed calls drained; a leave was sent.
enum class ChatListShowRefusal {
	None,
	NoList,
	NoTarget,
	NoView,
	CannotFit,
	OutOfView,
	NotReady,
	HoverOutside,
	HoverUnresolved,
};

// The reading the stage fills. Read it from a later stage. |name|,
// |timeout| and |hoverRequested| are filled when the stage is appended, the
// rest while it runs; a skipped stage leaves |finished| false.
// Coordinates, all logical pixels: |local| and |hoverLocal| element-local;
// |element| (the view's bounds), |rect|, |viewport| (the list's visible
// message area), |initialRect|, |initialViewport| and |hover| in the list's
// own coordinates; |mapped| in the owning scroll's. |initial*| are read when
// the stage begins, before any scroll; the others are the last reading.
// |scrollRequests| counts corrections asked of the scroll and |scrolls|
// those that moved it. |generation| counts geometry changes seen and
// |paintRequests| the repaints the helper asked for, at most one per
// generation, once it read settled and in view. |paints| counts list paints
// received after their generation's request that completed at an unchanged
// geometry, and |ignoredPaints| the ones over the rectangle received after
// the request that could not count (the list's own paint gate skipped them,
// or the geometry differed); a paint received before the request is in
// neither. |painted| means such paints of the current generation covered
// the whole current rectangle.
// |readyChecks| and |readyFalse| count the readiness answers. |hover| and
// |hoverLocal| hold the point once it was computed (|hoverSent|, or a
// hover-outside refusal). |hovered| means the list resolved the view at
// |hover| after the move; that hover is left in place for the next
// stage's run.
struct ChatListShow {
	QString name;
	QPointer<QWidget> list;
	FullMsgId item;
	QString kind;
	QString listIdentity;
	QString viewIdentity;
	QString readyDetails;
	QString hoverDetails;
	QString reason;
	QRect local;
	QRect element;
	QRect rect;
	QRect viewport;
	QRect mapped;
	QRect initialRect;
	QRect initialViewport;
	QPoint hover;
	QPoint hoverLocal;
	crl::time timeout = 0;
	crl::time elapsedMs = 0;
	int scrollTop = 0;
	int scrollRequests = 0;
	int scrolls = 0;
	int generation = 0;
	int paintRequests = 0;
	int paints = 0;
	int ignoredPaints = 0;
	int readyChecks = 0;
	int readyFalse = 0;
	ChatListShowRefusal refusal = ChatListShowRefusal::None;
	bool viewShown = false;
	bool pending = false;
	bool settled = false;
	bool inView = false;
	bool painted = false;
	bool ready = false;
	bool hoverRequested = false;
	bool hoverSent = false;
	bool hovered = false;
	bool finished = false;

	[[nodiscard]] bool shown() const {
		return finished && (refusal == ChatListShowRefusal::None);
	}
};

// none, no-list, no-target, no-view, cannot-fit, out-of-view, not-ready,
// hover-outside, hover-unresolved.
[[nodiscard]] QString ChatListShowRefusalName(ChatListShowRefusal refusal);

// The one CHAT_LIST_SHOW row the stage Notes, in ordinary integers; its
// fields are listed in the module reference.
[[nodiscard]] QString ChatListShowText(const ChatListShow &reading);

// Appends the stage "show in chat list: <name>" and returns its reading.
// The stage's run resolves the target, refuses what it cannot show and
// makes the first scroll. Its until only observes: it accepts once the
// rectangle lies inside the view's bounds, the list's visible message area
// and its owning scroll (Test::ReadMappedTarget), the view, its geometry,
// the scroll and the pending layout read the same for 400 ms, list paints
// received after the helper's repaint request for that settled geometry,
// and completed at it, cover the rectangle (an earlier paint never counts),
// and |ready| answers true, all on the current own view. A helper-owned
// timer makes the later scroll corrections and the repaint request, each
// from a fresh sample. Its then sends the hover and checks
// it, then Notes the row. The helper's deadline is max(timeout, 0) after
// the run, and the stage's runner timeout kDefaultStageTimeout more, so a
// refusal is a named reading, never a stage timeout. It never Passes or
// Fails: treat a refusal as a fixture gate. A successful hover belongs to
// the next stage: press there in its run, or leave the list.
[[nodiscard]] std::shared_ptr<const ChatListShow> AppendShowInChatList(
	not_null<Runner*> runner,
	ChatListShowArgs args);

// The module's behavioral self-test, appended by an overlay's
// SetupScenario after its chats-loaded wait. |channel| is a megagroup or
// channel of the Test account that the account administers (Recent
// Actions access), with its folder known and no unread messages. It shows
// the primary window normally and keeps it exposed, activating nothing,
// opens the channel in the legacy HistoryInner (use-new-chat-view off), a
// History-mode ChatWidget's ListWidget (on) and its Recent Actions, and
// injects only local fixtures there: outgoing client-id text messages and
// local ChangeAbout admin-log events; nothing is sent. In each list it
// shows a target from far above and one from far below, with the whole
// view on one and Element::innerGeometry() on the other, each with a
// delayed-true readiness and a hover the next stage judges independently
// before a viewport capture, plus a no-view refusal; in the ListWidget
// also a later text growth the helper must correct again, and the
// cannot-fit, out-of-view (a disabled scroll) and not-ready controls. A
// failed fixture gate is a named FAIL and makes its dependent stages N/A.
// Its release is registered with Runner::onFinish before anything
// changes; stages, rows and teardown: docs/helpers/test_chat_show_self_test.md.
void AppendChatListShowSelfTest(not_null<Runner*> runner, PeerId channel);

// Self-test access, not for overlays: the helper's own reads of a list,
// shared by test_chat_show.cpp, test_chat_show_self_test.cpp and
// test_chat_show_place.cpp so neither the self-test's judge nor the
// placement helper copies them. Those taking |list| read it only
// during the call; a widget that is none of the three lists is None, with
// an empty visible area, nothing pending and mouse-active text "none".
enum class ChatListShowKind {
	None,
	History,
	Modern,
	Log,
};

[[nodiscard]] ChatListShowKind ChatListShowKindOf(not_null<QWidget*> list);

// The text ChatListShow::kind holds: HistoryInner,
// HistoryView::ListWidget, AdminLog::InnerWidget or none.
[[nodiscard]] QString ChatListShowKindName(ChatListShowKind kind);

// The Ui::ElasticScroll that owns |list| directly, or null.
[[nodiscard]] Ui::ElasticScroll *ChatListShowOwningScroll(
	not_null<QWidget*> list);

// The visible message area the list clamps its mouse resolution to, in
// its own coordinates.
[[nodiscard]] QRect ChatListShowVisibleArea(not_null<QWidget*> list);

// HistoryInner's pending resized items; false on the other lists.
[[nodiscard]] bool ChatListShowPendingResize(not_null<QWidget*> list);

// The list's private _mouseActive as 1 or 0, "ungated" on the admin log.
[[nodiscard]] QString ChatListShowMouseActiveText(not_null<QWidget*> list);

// |view|'s address as ChatListShow::viewIdentity prints it, never
// dereferenced; "none" for null.
[[nodiscard]] QString ChatListShowViewIdentity(
	const HistoryView::Element *view);

// The message |id| as |list|'s own session knows it, or null (also for
// a widget that is none of the three lists).
[[nodiscard]] HistoryItem *ChatListShowItem(
	not_null<QWidget*> list,
	FullMsgId id);

// The list's own current view of |item| by chat-lists.md's ownership
// rule (never item->mainView() on trust), or null.
[[nodiscard]] HistoryView::Element *ChatListShowOwnView(
	not_null<QWidget*> list,
	not_null<HistoryItem*> item);

// |view|'s bounds in |list|'s own coordinates: its origin as the list's
// mapPointToItem() places it, selection-mode offset included.
[[nodiscard]] QRect ChatListShowElementRect(
	not_null<QWidget*> list,
	not_null<const HistoryView::Element*> view);

} // namespace Test
