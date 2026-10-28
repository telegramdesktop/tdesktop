/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "data/data_msg_id.h"
#include "data/data_peer_id.h"

#include <QtCore/QPointer>
#include <QtCore/QRect>
#include <QtCore/QString>

#include <memory>
#include <vector>

class HistoryItem;
class QWidget;

namespace HistoryView {
class Element;
} // namespace HistoryView

namespace Test {

class Runner;

// Places several messages of one chat list wholly in view together, or
// wholly above or below it, and proves they stayed there for a window.
// Usage, limits, rows and refusals: docs/helpers/test_chat_show_place.md.

// Where every message of one call goes, judged on its whole own view, in
// the list's own coordinates and logical pixels:
// - OnScreen: wholly inside the list's visible message area
//   (ChatListShowVisibleArea) and, mapped into the owning scroll, inside
//   that area's clip there;
// - OffAbove: the view's bottom at or above the area's top;
// - OffBelow: the view's top at or below the area's bottom.
// The off placements are the lists' own visibility rule: a view is shown
// while top + height > areaTop && top < areaBottom. For them a message
// whose list shows no own view of it counts as off.
enum class ChatListPlacement {
	OnScreen,
	OffAbove,
	OffBelow,
};

// |list| follows ChatListShowTarget's rules: a HistoryInner, a
// HistoryView::ListWidget or an AdminLog::InnerWidget, the owned widget of
// its Ui::ElasticScroll. |items| name the messages; each is looked up
// again through the list's session on every sample, and so is the list's
// own view of it (never item->mainView() taken on trust).
struct ChatListPlaceTarget {
	QWidget *list = nullptr;
	std::vector<FullMsgId> items;
};

// The caller's visibility reading of one message, and its details. |view|
// is the list's own current view of |item|, or null, valid only during
// the call: keep no pointer to it. Both must only read: they are called
// on many samples, by the placement stage and by its record after it,
// across the caller's later stages until the record ends. The reading
// must be the product's fresh answer, never a cache. For a widget card
// that is Tgw::Runtime::queryVisible() (private; read it with the
// file-private accessor idiom) or membership in
// Tgw::Runtime::WatchingOnScreen(), never Tgw::Runtime::_visible.
using ChatListPlaceVisible = Fn<bool(
	not_null<HistoryItem*> item,
	const HistoryView::Element *view)>;
using ChatListPlaceDetails = Fn<QString(
	not_null<HistoryItem*> item,
	const HistoryView::Element *view)>;

// |name| labels the stage ("place in chat list: <name>") and its rows.
// |resolve| runs once, when the stage begins, so a list or messages from
// an earlier stage can be named. |visible| is the reading above, asked on
// every settled placed sample and on every record sample; true is
// expected for OnScreen and false for the off placements.
// |visibleDetails| is printed beside every answer; null prints none.
// |resolve| and |visible| are required. |timeout| is the helper's own
// deadline, in ms from the stage's run.
struct ChatListPlaceArgs {
	QString name;
	ChatListPlacement placement = ChatListPlacement::OnScreen;
	Fn<ChatListPlaceTarget()> resolve;
	ChatListPlaceVisible visible;
	ChatListPlaceDetails visibleDetails;
	Fn<QString()> skipReason;
	crl::time timeout = crl::time(10000);
};

// Every refusal's CHAT_LIST_PLACE row carries each message's last
// geometry and reading (element=none, visible=none when never read), each
// alive message's reading asked once more on the deciding sample unless
// the refusal is no-list. The reasons of cannot-fit and cannot-move-off
// carry them for every message, that of reading-disagrees for each
// message that disagreed; other reasons may carry less: read the row.
// - no-list: no list, not one of the three lists, not owned by a
//   Ui::ElasticScroll, or destroyed while the helper waited;
// - no-target: no message id, an id the list's session does not know, or
//   a message destroyed while the helper waited;
// - no-view: a message had no own view when the stage began (nothing is
//   scrolled), or an on placement's view was lost and not shown again by
//   the deadline;
// - cannot-fit (OnScreen only): on a usable layout that read the same for
//   the settle time, or at the deadline on the last usable one, the views'
//   union band is taller than the visible area, or a view lies outside it
//   horizontally;
// - cannot-move-off (off only): on such a layout, with the messages not
//   already all off on the asked side, the scroll top that puts every
//   view wholly on that side, 8 px past the edge, is outside
//   [0, scrollTopMax()]. Nothing is scrolled: the helper never scrolls to
//   an edge to load more;
// - out-of-place: the messages fit or could move, but at the deadline they
//   were not all placed, the layout stayed pending, or the placed geometry
//   had not settled;
// - reading-disagrees: placed and settled at the deadline, but some
//   message's reading disagreed (true off, false on).
enum class ChatListPlaceRefusal {
	None,
	NoList,
	NoTarget,
	NoView,
	CannotFit,
	CannotMoveOff,
	OutOfPlace,
	ReadingDisagrees,
};

// What one sample saw of one message. |element| (the view's bounds, empty
// without a view) and |viewport| (the list's visible message area) are in
// the list's own coordinates; |scrollTop| is the owning scroll's. |placed|
// means where the placement asks. |visible| is the caller's reading,
// |asked| says whether it was read on that sample and |details| are its
// details.
struct ChatListPlaceMark {
	crl::time at = 0;
	QRect element;
	QRect viewport;
	QString details;
	int scrollTop = 0;
	bool alive = false;
	bool viewShown = false;
	bool placed = false;
	bool visible = false;
	bool asked = false;
};

// One message of the placement. |initial| is read when the stage begins,
// before any scroll; |last| is the latest sample, from the placement or
// from the record after it. |changes| starts with the accepted mark and
// gets one more mark each time |alive|, |viewShown|, |placed| or
// |visible| changes. |readings| and |disagreements| count the placement
// gate's answers and the ones against the placement.
struct ChatListPlaceItem {
	FullMsgId id;
	ChatListPlaceMark initial;
	ChatListPlaceMark last;
	std::vector<ChatListPlaceMark> changes;
	int readings = 0;
	int disagreements = 0;
};

// The reading the placement stage fills; read it from a later stage.
// |name|, |placement| and |timeout| are filled when the stage is appended,
// the rest while it runs; a skipped stage leaves |finished| false.
// Coordinates, all logical pixels: |band| (the union of the shown views)
// and |viewport| in the list's own, |effective| (the visible area mapped
// into the owning scroll and clipped to it) in the scroll's. |target| is
// the last scroll top asked; |scrollRequests| counts corrections asked of
// the scroll, |scrolls| those that moved it and |generation| the geometry
// changes seen. |placedAt| is the acceptance time (crl::now()) and the
// record's start; |samples| counts record samples since then, |maxGapMs|
// is the longest gap between them and |lastSampleAt| the latest one.
// The record keeps filling after the stage finished, until
// Runner::onFinish, the list's destruction or the destruction of every
// placed message; then |recording| is false and |recordEnded| set, with
// |recordEndedAt| and |recordEnd| saying when and which. Stays only read
// it.
struct ChatListPlace {
	QString name;
	QPointer<QWidget> list;
	QString kind;
	QString listIdentity;
	QString reason;
	QString recordEnd;
	std::vector<ChatListPlaceItem> items;
	QRect band;
	QRect viewport;
	QRect effective;
	ChatListPlacement placement = ChatListPlacement::OnScreen;
	ChatListPlaceRefusal refusal = ChatListPlaceRefusal::None;
	crl::time timeout = 0;
	crl::time elapsedMs = 0;
	crl::time placedAt = 0;
	crl::time lastSampleAt = 0;
	crl::time recordEndedAt = 0;
	crl::time maxGapMs = 0;
	int scrollTop = 0;
	int scrollTopMax = 0;
	int target = 0;
	int scrollRequests = 0;
	int scrolls = 0;
	int generation = 0;
	int samples = 0;
	bool settled = false;
	bool pending = false;
	bool finished = false;
	bool recording = false;
	bool recordEnded = false;

	[[nodiscard]] bool placed() const {
		return finished && (refusal == ChatListPlaceRefusal::None);
	}
};

// on, off-above, off-below.
[[nodiscard]] QString ChatListPlacementName(ChatListPlacement placement);

// none, no-list, no-target, no-view, cannot-fit, cannot-move-off,
// out-of-place, reading-disagrees.
[[nodiscard]] QString ChatListPlaceRefusalName(ChatListPlaceRefusal refusal);

// The one CHAT_LIST_PLACE row the stage Notes, in ordinary integers; its
// fields are listed in the module reference.
[[nodiscard]] QString ChatListPlaceText(const ChatListPlace &reading);

// Appends the stage "place in chat list: <name>" and returns its reading.
// The stage's run resolves the target, refuses what it cannot place and
// makes the first scroll. Its until only observes, and accepts once, on
// one sample: every message is placed on the asked side; the views, their
// geometry, the visible area, the scroll top and range and the pending
// layout read the same for 400 ms; and the caller's reading agrees for
// every message. A helper-owned timer makes the later scroll corrections,
// each from a fresh sample. The helper's deadline is max(timeout, 0) after
// the run, and the stage's runner timeout kDefaultStageTimeout more, so a
// refusal is a named reading, never a stage timeout. It never Passes or
// Fails: treat a refusal as a fixture gate. An accepted placement starts
// the record, which calls the reading on each of its samples until
// Runner::onFinish, the list's destruction or the destruction of every
// placed message.
[[nodiscard]] std::shared_ptr<const ChatListPlace> AppendPlaceInChatList(
	not_null<Runner*> runner,
	ChatListPlaceArgs args);

// - not-placed: the placement did not finish accepted; the reason names
//   its refusal, or says it did not finish (skipped or not run);
// - did-not-stay: inside the window a message left its placement or was
//   destroyed, or the record ended before the window's end (the list or
//   every message destroyed, or the run finishing);
// - reading-disagrees: the geometry held, but a message's reading
//   disagreed inside the window.
// The earliest deviation decides; on one sample, geometry wins.
enum class ChatListStayRefusal {
	None,
	NotPlaced,
	DidNotStay,
	ReadingDisagrees,
};

// |name| labels the stage ("stayed in chat list: <name>") and its row.
// |place| is a reading AppendPlaceInChatList returned; it is required.
// |hold| is the window's least length, in ms from the placement's
// acceptance; 0 ends the window when this stage runs.
struct ChatListStayArgs {
	QString name;
	std::shared_ptr<const ChatListPlace> place;
	crl::time hold = 0;
	Fn<QString()> skipReason;
};

// One message over the window. |deviation| is its first mark inside the
// window that left the placement, was destroyed or read against
// |expectedVisible|; |deviation.at| 0 means none. |last| is its record
// mark at the decision. |window| holds its record's marks in
// [from, till], the accepted mark first, and |changes| counts the ones
// after it: its changes in (from, till].
struct ChatListStayItem {
	FullMsgId id;
	ChatListPlaceMark deviation;
	ChatListPlaceMark last;
	std::vector<ChatListPlaceMark> window;
	int changes = 0;
	bool expectedVisible = false;
	bool stayed = false;
};

// The reading the stay stage fills; read it from a later stage. The
// window is [from, windowEnd]: |from| is the placement's acceptance and
// |windowEnd| max(this stage's run, from + hold). |till| is the record
// sample the stay decided on, at or after |windowEnd| unless a deviation
// or the record's end decided first; the judgement reads only marks up
// to it. |from| and |till| are crl::now() values, so a consumer brackets
// its own product rows with them. |samples| and |maxGapMs| are the
// record's at the decision.
struct ChatListStay {
	QString name;
	QString placeName;
	QString reason;
	std::vector<ChatListStayItem> items;
	ChatListPlacement placement = ChatListPlacement::OnScreen;
	ChatListStayRefusal refusal = ChatListStayRefusal::None;
	crl::time hold = 0;
	crl::time from = 0;
	crl::time till = 0;
	crl::time windowEnd = 0;
	crl::time maxGapMs = 0;
	int samples = 0;
	bool finished = false;

	[[nodiscard]] bool stayed() const {
		return finished && (refusal == ChatListStayRefusal::None);
	}
};

// none, not-placed, did-not-stay, reading-disagrees.
[[nodiscard]] QString ChatListStayRefusalName(ChatListStayRefusal refusal);

// The one CHAT_LIST_STAY row the stage Notes; its window= lists every
// mark inside the window, and its fields are listed in the module
// reference.
[[nodiscard]] QString ChatListStayText(const ChatListStay &reading);

// Appends the stage "stayed in chat list: <name>" and returns its reading.
// The window is [placedAt, max(this stage's run, placedAt + hold)]: right
// after the placement with |hold| it is a fixed-length window, and after
// the consumer's own stages with |hold| 0 it spans them. The stage decides
// at the record's first sample at or after the window's end, or at once
// on a deviation already recorded. It only reads the record, so several
// stays may judge one placement, each window starting at its acceptance.
// The stage's runner timeout is max(hold, 0) plus kDefaultStageTimeout.
// It never Passes or Fails: treat a refusal as a fixture gate, and judge
// the consumer's own product rows between |from| and |till|.
[[nodiscard]] std::shared_ptr<const ChatListStay> AppendStayedInChatList(
	not_null<Runner*> runner,
	ChatListStayArgs args);

// The module's behavioral self-test, appended by an overlay's
// SetupScenario after its session and chats-loaded waits. |chat| is a
// chat test_widget_fixture accepts (its requirements are the fixture's),
// opened in its legacy HistoryInner. Only local messages are injected
// there: two widget cards and one tall text filler above and one below
// them; nothing is sent. The legs: three accepted placements (on, off
// below, off above) with a held stay each, two continuity refusals on
// those same records (an excursion and a flipped reading) and three
// placement refusals (a stale reading, cannot fit, cannot move off). The
// fixture's teardown follows the legs, and its release is registered
// with Runner::onFinish before anything changes; stages, rows and
// verdict: docs/helpers/test_chat_show_place.md.
void AppendChatListPlaceSelfTest(not_null<Runner*> runner, PeerId chat);

} // namespace Test
