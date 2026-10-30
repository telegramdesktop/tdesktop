/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_chat_show.h"

#include "base/event_filter.h"
#include "base/invoke_queued.h"
#include "base/timer.h"
#include "data/data_messages.h"
#include "data/data_session.h"
#include "history/admin_log/history_admin_log_inner.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_list_widget.h"
#include "history/history_inner_widget.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "test/test_capture.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "test/test_widgets.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"

#include <QtCore/QStringList>
#include <QtGui/QEnterEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QPaintEvent>
#include <QtGui/QRegion>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>

namespace Test {
namespace {

using Element = HistoryView::Element;
using ModernList = HistoryView::ListWidget;
using LogList = AdminLog::InnerWidget;
using ListKind = ChatListShowKind;
using Refusal = ChatListShowRefusal;

// A sample has to read the same for this long before its geometry is
// judged: HistoryWidget::newItemAdded's reveal animation and the selection
// mode animation are both shorter (test_chat_menu.cpp's kLayoutSettle).
constexpr auto kSettle = crl::time(400);

// How often the helper's own timer acts on what its until last flagged.
constexpr auto kActionInterval = crl::time(50);

// The test-only private-member accessor of test_notify_override.cpp and
// test_chat_menu.cpp, kept file-private here. Access checking does not
// apply to the names in an explicit instantiation, so instantiating
// PrivateMember with a private member pointer defines Get(Tag) returning
// it. Every use below only reads: the visible message area, the element
// origin, the own-view lookup, the pending-resize and paint gates and the
// hover gate. Nothing here writes product state.
template <typename Tag, typename Tag::type Member>
struct PrivateMember {
	friend typename Tag::type Get(Tag) {
		return Member;
	}
};

struct InnerVisibleTopTag {
	using type = int HistoryInner::*;
	friend type Get(InnerVisibleTopTag);
};

template struct PrivateMember<
	InnerVisibleTopTag,
	&HistoryInner::_visibleAreaTop>;

struct InnerVisibleBottomTag {
	using type = int HistoryInner::*;
	friend type Get(InnerVisibleBottomTag);
};

template struct PrivateMember<
	InnerVisibleBottomTag,
	&HistoryInner::_visibleAreaBottom>;

struct InnerMapPointTag {
	using type = QPoint (HistoryInner::*)(QPoint, const Element*) const;
	friend type Get(InnerMapPointTag);
};

template struct PrivateMember<
	InnerMapPointTag,
	&HistoryInner::mapPointToItem>;

struct InnerPendingResizeTag {
	using type = bool (HistoryInner::*)() const;
	friend type Get(InnerPendingResizeTag);
};

template struct PrivateMember<
	InnerPendingResizeTag,
	&HistoryInner::hasPendingResizedItems>;

struct InnerControllerTag {
	using type = const not_null<Window::SessionController*> HistoryInner::*;
	friend type Get(InnerControllerTag);
};

template struct PrivateMember<
	InnerControllerTag,
	&HistoryInner::_controller>;

struct InnerMouseActiveTag {
	using type = bool HistoryInner::*;
	friend type Get(InnerMouseActiveTag);
};

template struct PrivateMember<
	InnerMouseActiveTag,
	&HistoryInner::_mouseActive>;

struct ListVisibleTopTag {
	using type = int ModernList::*;
	friend type Get(ListVisibleTopTag);
};

template struct PrivateMember<ListVisibleTopTag, &ModernList::_visibleTop>;

struct ListVisibleBottomTag {
	using type = int ModernList::*;
	friend type Get(ListVisibleBottomTag);
};

template struct PrivateMember<
	ListVisibleBottomTag,
	&ModernList::_visibleBottom>;

struct ListMapPointTag {
	using type = QPoint (ModernList::*)(QPoint, const Element*) const;
	friend type Get(ListMapPointTag);
};

template struct PrivateMember<ListMapPointTag, &ModernList::mapPointToItem>;

struct ListMouseActiveTag {
	using type = bool ModernList::*;
	friend type Get(ListMouseActiveTag);
};

template struct PrivateMember<ListMouseActiveTag, &ModernList::_mouseActive>;

struct LogVisibleTopTag {
	using type = int LogList::*;
	friend type Get(LogVisibleTopTag);
};

template struct PrivateMember<LogVisibleTopTag, &LogList::_visibleTop>;

struct LogVisibleBottomTag {
	using type = int LogList::*;
	friend type Get(LogVisibleBottomTag);
};

template struct PrivateMember<LogVisibleBottomTag, &LogList::_visibleBottom>;

struct LogMapPointTag {
	using type = QPoint (LogList::*)(QPoint, const Element*) const;
	friend type Get(LogMapPointTag);
};

template struct PrivateMember<LogMapPointTag, &LogList::mapPointToItem>;

// The admin log's own lookup, through _itemsByData: an event hidden in a
// collapsed delete group has no entry there.
struct LogViewForItemTag {
	using type = Element *(LogList::*)(const HistoryItem*);
	friend type Get(LogViewForItemTag);
};

template struct PrivateMember<LogViewForItemTag, &LogList::viewForItem>;

struct LogControllerTag {
	using type = const not_null<Window::SessionController*> LogList::*;
	friend type Get(LogControllerTag);
};

template struct PrivateMember<LogControllerTag, &LogList::_controller>;

// The one value adapter over the three lists. |widget| guards the
// lifetime; the typed pointer matching |kind| is used only while it is
// alive.
struct ChatList {
	QPointer<QWidget> widget;
	HistoryInner *inner = nullptr;
	ModernList *modern = nullptr;
	LogList *log = nullptr;
	ListKind kind = ListKind::None;
};

// What a sample must read unchanged while the layout counts as settled.
// Any change starts a new geometry generation, which restarts the settle
// and drops the paint coverage taken for the previous one. |view| is the
// view's address, compared and never dereferenced, so a replaced view is a
// new generation.
struct Geometry {
	quintptr view = 0;
	QRect element;
	QRect local;
	QRect viewport;
	QRect inScroll;
	QRect effective;
	int scrollTop = 0;
	bool viewPending = false;
	bool listPending = false;

	friend bool operator==(const Geometry &, const Geometry &) = default;
};

// One synchronous reading of the list. |view| is valid only inside the
// call that took the sample: never keep a sample across an event turn or
// past a scroll. |rect| is |geometry.local| in the list's coordinates;
// |geometry.effective| is the visible message area mapped into the owning
// scroll and clipped to it.
struct Sample {
	Geometry geometry;
	QRect rect;
	QString viewIdentity;
	QString mappedRefusal;
	Element *view = nullptr;
	bool listAlive = false;
	bool scrollOwned = false;
	bool itemAlive = false;
	bool viewShown = false;
	bool localInside = false;
	bool inViewport = false;
	bool mappedResolved = false;
};

// Everything one appended stage owns. The timer holds the driver by a
// plain pointer, because the driver owns it; the paint filter belongs to
// the list and holds a weak pointer, and its queued confirmations die
// with it.
struct ShowDriver {
	ChatListShowArgs args;
	std::shared_ptr<ChatListShow> reading;
	ChatList list;
	FullMsgId item;
	base::Timer timer;
	QPointer<QObject> filter;
	Geometry geometry;
	QRegion coverage;
	crl::time started = 0;
	crl::time deadline = 0;
	crl::time stableSince = 0;
	int paintRequestedFor = 0;
	bool sampled = false;
	bool wantCorrection = false;
	bool wantPaint = false;
	bool decided = false;
};

[[nodiscard]] QString FlagText(bool value) {
	return value ? u"1"_q : u"0"_q;
}

[[nodiscard]] QString NumberText(qint64 value) {
	return QString::number(value);
}

[[nodiscard]] QString PointText(QPoint point) {
	return u"%1,%2"_q.arg(NumberText(point.x()), NumberText(point.y()));
}

[[nodiscard]] QString OrNone(const QString &value) {
	return value.isEmpty() ? u"none"_q : value;
}

[[nodiscard]] QString IdText(FullMsgId id) {
	return id
		? u"%1:%2"_q.arg(
			NumberText(qint64(id.peer.value)),
			NumberText(qint64(id.msg.bare)))
		: u"none"_q;
}

// An address, printed and never dereferenced, so two readings can say
// whether they name the same view.
[[nodiscard]] QString ElementIdentity(const Element *view) {
	return view
		? u"element@0x%1"_q.arg(
			qulonglong(reinterpret_cast<quintptr>(view)),
			0,
			16)
		: u"none"_q;
}

[[nodiscard]] QString KindName(ListKind kind) {
	switch (kind) {
	case ListKind::None: return u"none"_q;
	case ListKind::History: return u"HistoryInner"_q;
	case ListKind::Modern: return u"HistoryView::ListWidget"_q;
	case ListKind::Log: return u"AdminLog::InnerWidget"_q;
	}
	Unexpected("Kind in Test::KindName.");
}

[[nodiscard]] ChatList ResolveChatList(not_null<QWidget*> widget) {
	auto result = ChatList{ .widget = widget.get() };
	if (const auto inner = dynamic_cast<HistoryInner*>(widget.get())) {
		result.inner = inner;
		result.kind = ListKind::History;
	} else if (const auto modern = dynamic_cast<ModernList*>(widget.get())) {
		result.modern = modern;
		result.kind = ListKind::Modern;
	} else if (const auto log = dynamic_cast<LogList*>(widget.get())) {
		result.log = log;
		result.kind = ListKind::Log;
	}
	return result;
}

// The scroll that owns |list| directly, as all three sections build it.
[[nodiscard]] Ui::ElasticScroll *OwningScroll(not_null<QWidget*> list) {
	const auto scroll = dynamic_cast<Ui::ElasticScroll*>(
		list->parentWidget());
	return (scroll && (scroll->widget() == list.get())) ? scroll : nullptr;
}

// The functions below dereference the typed pointer: call them only while
// |list.widget| is alive.
[[nodiscard]] Main::Session &ListSession(const ChatList &list) {
	switch (list.kind) {
	case ListKind::History: return list.inner->session();
	case ListKind::Modern: return list.modern->session();
	case ListKind::Log: return list.log->session();
	case ListKind::None: break;
	}
	Unexpected("Kind in Test::ListSession.");
}

// The list's own view of |item|, by chat-lists.md's ownership rule.
[[nodiscard]] Element *OwnView(const ChatList &list, HistoryItem *item) {
	if (!item) {
		return nullptr;
	}
	switch (list.kind) {
	case ListKind::History: {
		// itemTop() answers -2 for a view of another history and -1 for
		// one that is not the item's main view or not shown.
		const auto view = list.inner->viewByItem(item);
		return (view && (list.inner->itemTop(view) >= 0)) ? view : nullptr;
	}
	case ListKind::Modern: {
		const auto view = list.modern->viewByPosition(item->position());
		const auto delegate = static_cast<HistoryView::ElementDelegate*>(
			list.modern);
		return (view
			&& (view->data() == item)
			&& (view->delegate().get() == delegate))
			? view
			: nullptr;
	}
	case ListKind::Log: {
		const auto view = (list.log->*Get(LogViewForItemTag()))(item);
		const auto delegate = static_cast<HistoryView::ElementDelegate*>(
			list.log);
		return (view
			&& (view->data() == item)
			&& (view->delegate().get() == delegate))
			? view
			: nullptr;
	}
	case ListKind::None: break;
	}
	return nullptr;
}

[[nodiscard]] QString NoViewHint(ListKind kind) {
	switch (kind) {
	case ListKind::History:
		return u"HistoryInner::viewByItem() answered no view whose "
			u"itemTop() is at least 0: none, another history's, not the "
			u"message's main view or not shown"_q;
	case ListKind::Modern:
		return u"HistoryView::ListWidget::viewByPosition() answered no view "
			u"whose data() is the message and whose delegate() is this "
			u"list; a message outside the loaded slice is shown with "
			u"showAtPosition() first"_q;
	case ListKind::Log:
		return u"AdminLog::InnerWidget::viewForItem() answered no view of "
			u"this list; an event hidden in a collapsed delete group has "
			u"none"_q;
	case ListKind::None: break;
	}
	return QString();
}

// The view's top-left in the list: the inverse of the list's own
// mapPointToItem(), so it carries the selection-mode offset.
[[nodiscard]] QPoint ElementOrigin(
		const ChatList &list,
		not_null<const Element*> view) {
	switch (list.kind) {
	case ListKind::History:
		return -(list.inner->*Get(InnerMapPointTag()))(QPoint(), view);
	case ListKind::Modern:
		return -(list.modern->*Get(ListMapPointTag()))(QPoint(), view);
	case ListKind::Log:
		return -(list.log->*Get(LogMapPointTag()))(QPoint(), view);
	case ListKind::None: break;
	}
	return QPoint();
}

// The visible message area the list clamps its mouse resolution to, in
// its own coordinates.
[[nodiscard]] QRect VisibleArea(const ChatList &list) {
	auto top = 0;
	auto bottom = 0;
	switch (list.kind) {
	case ListKind::History:
		top = list.inner->*Get(InnerVisibleTopTag());
		bottom = list.inner->*Get(InnerVisibleBottomTag());
		break;
	case ListKind::Modern:
		top = list.modern->*Get(ListVisibleTopTag());
		bottom = list.modern->*Get(ListVisibleBottomTag());
		break;
	case ListKind::Log:
		top = list.log->*Get(LogVisibleTopTag());
		bottom = list.log->*Get(LogVisibleBottomTag());
		break;
	case ListKind::None: break;
	}
	return QRect(0, top, list.widget->width(), std::max(bottom - top, 0));
}

// HistoryInner's pending resized items, for its history or the migrated
// one: its paintEvent and mouseActionUpdate() return early while set.
[[nodiscard]] bool ListPending(const ChatList &list) {
	return (list.kind == ListKind::History)
		&& (list.inner->*Get(InnerPendingResizeTag()))();
}

// The early return each list's paintEvent takes before it draws anything.
[[nodiscard]] bool PaintSkipped(
		const ChatList &list,
		not_null<QPaintEvent*> e) {
	switch (list.kind) {
	case ListKind::History:
		return (list.inner->*Get(InnerControllerTag()))->contentOverlapped(
			list.inner,
			e)
			|| ListPending(list);
	case ListKind::Modern:
		return list.modern->delegate()->listIgnorePaintEvent(list.modern, e);
	case ListKind::Log:
		return (list.log->*Get(LogControllerTag()))->contentOverlapped(
			list.log,
			e);
	case ListKind::None: break;
	}
	return true;
}

// The half of the lists' hover gate that is not the window's activation;
// the admin log resolves the pointer without a gate.
[[nodiscard]] QString MouseActiveText(const ChatList &list) {
	switch (list.kind) {
	case ListKind::History:
		return FlagText(list.inner->*Get(InnerMouseActiveTag()));
	case ListKind::Modern:
		return FlagText(list.modern->*Get(ListMouseActiveTag()));
	case ListKind::Log: return u"ungated"_q;
	case ListKind::None: break;
	}
	return u"none"_q;
}

[[nodiscard]] Sample TakeSample(const ShowDriver &driver) {
	auto result = Sample();
	const auto widget = driver.list.widget.data();
	if (!widget) {
		return result;
	}
	result.listAlive = true;
	const auto &list = driver.list;
	const auto scroll = OwningScroll(widget);
	result.scrollOwned = (scroll != nullptr);
	auto &geometry = result.geometry;
	geometry.viewport = VisibleArea(list);
	geometry.listPending = ListPending(list);
	if (scroll) {
		geometry.scrollTop = scroll->scrollTop();
		geometry.effective = Ui::MapFrom(
			scroll,
			widget,
			geometry.viewport).intersected(scroll->rect());
	}
	const auto item = driver.item
		? ListSession(list).data().message(driver.item)
		: nullptr;
	if (!item) {
		return result;
	}
	result.itemAlive = true;
	const auto view = OwnView(list, item);
	if (!view) {
		return result;
	}
	result.view = view;
	result.viewShown = true;
	result.viewIdentity = ElementIdentity(view);
	const auto bounds = QRect(QPoint(), QSize(view->width(), view->height()));
	const auto origin = ElementOrigin(list, view);
	geometry.view = reinterpret_cast<quintptr>(view);
	geometry.element = bounds.translated(origin);
	geometry.local = driver.args.rect ? driver.args.rect(view) : bounds;
	geometry.viewPending = view->pendingResize();
	result.rect = geometry.local.translated(origin);
	result.localInside = !geometry.local.isEmpty()
		&& bounds.contains(geometry.local);
	result.inViewport = !result.rect.isEmpty()
		&& geometry.viewport.contains(result.rect);
	if (scroll) {
		geometry.inScroll = Ui::MapFrom(scroll, widget, result.rect);
		const auto mapped = ReadMappedTarget(scroll, widget, result.rect);
		result.mappedResolved = mapped.resolved();
		result.mappedRefusal = mapped.refusal;
	}
	return result;
}

[[nodiscard]] bool Pending(const Geometry &geometry) {
	return geometry.viewPending || geometry.listPending;
}

// A layout the fit can be judged on: a laid-out view, a visible message
// area inside its owning scroll and nothing pending.
[[nodiscard]] bool Usable(const Sample &sample) {
	const auto &geometry = sample.geometry;
	return sample.viewShown
		&& sample.scrollOwned
		&& !geometry.element.isEmpty()
		&& !geometry.effective.isEmpty()
		&& !Pending(geometry);
}

[[nodiscard]] bool Fits(const Sample &sample) {
	const auto &geometry = sample.geometry;
	const auto &rect = geometry.inScroll;
	const auto &area = geometry.effective;
	return sample.localInside
		&& (rect.height() <= area.height())
		&& (rect.left() >= area.left())
		&& (rect.right() <= area.right());
}

[[nodiscard]] bool InView(const Sample &sample) {
	return sample.localInside
		&& sample.inViewport
		&& sample.mappedResolved;
}

[[nodiscard]] bool Covered(const ShowDriver &driver, const Sample &sample) {
	return !sample.rect.isEmpty()
		&& QRegion(sample.rect).subtracted(driver.coverage).isEmpty();
}

// Whether the helper has asked for the repaint of the current geometry
// generation. Only paints received after that request are queued for
// confirmation, so a natural paint of the same geometry that came first,
// such as the one right after a scroll, can neither cover the rectangle
// nor stand in for the request.
[[nodiscard]] bool Requested(const ShowDriver &driver) {
	return (driver.paintRequestedFor == driver.reading->generation);
}

// Takes a sample into the driver's bookkeeping: a changed geometry starts a
// new generation. The reading keeps the last geometry of a shown view.
void Observe(ShowDriver &driver, const Sample &sample, crl::time now) {
	auto &reading = *driver.reading;
	if (!driver.sampled || (sample.geometry != driver.geometry)) {
		driver.sampled = true;
		driver.geometry = sample.geometry;
		driver.stableSince = now;
		driver.coverage = QRegion();
		driver.wantCorrection = false;
		driver.wantPaint = false;
		++reading.generation;
	}
	reading.viewShown = sample.viewShown;
	reading.viewport = sample.geometry.viewport;
	reading.scrollTop = sample.geometry.scrollTop;
	reading.pending = Pending(sample.geometry);
	reading.inView = InView(sample);
	reading.painted = sample.viewShown
		&& Requested(driver)
		&& Covered(driver, sample);
	if (sample.viewShown) {
		reading.viewIdentity = sample.viewIdentity;
		reading.local = sample.geometry.local;
		reading.element = sample.geometry.element;
		reading.rect = sample.rect;
		reading.mapped = sample.geometry.inScroll;
	}
}

void Decide(ShowDriver &driver, Refusal refusal, QString reason) {
	driver.decided = true;
	driver.wantCorrection = false;
	driver.wantPaint = false;
	auto &reading = *driver.reading;
	reading.refusal = refusal;
	reading.reason = std::move(reason);
	reading.elapsedMs = crl::now() - driver.started;
}

[[nodiscard]] QString CorrectionText(
		const ChatListShow &reading,
		int from,
		int to,
		const Sample &before,
		const Sample &after) {
	const auto fields = QStringList{
		u"name=%1"_q.arg(reading.name),
		u"request=%1"_q.arg(NumberText(reading.scrollRequests)),
		u"scrollTop=%1"_q.arg(NumberText(from)),
		u"asked=%1"_q.arg(NumberText(to)),
		u"now=%1"_q.arg(NumberText(after.geometry.scrollTop)),
		u"moved=%1"_q.arg(FlagText(after.geometry.scrollTop != from)),
		u"before=%1"_q.arg(RectText(before.geometry.inScroll)),
		u"after=%1"_q.arg(after.viewShown
			? RectText(after.geometry.inScroll)
			: u"none"_q),
		u"effective=%1"_q.arg(RectText(before.geometry.effective)),
		u"view=%1"_q.arg(after.viewShown
			? ((after.geometry.view == before.geometry.view)
				? u"same"_q
				: after.viewIdentity)
			: u"lost"_q),
	};
	return u"CHAT_LIST_SHOW_SCROLL: "_q + fields.join(u" "_q);
}

// One scroll that centres the rectangle in the visible message area, from
// |sample|, which must have been taken in this same call. The scroll runs
// the list's scroll handlers synchronously, which may load, lay out or
// replace views, so the driver takes a fresh sample right after it.
void Correct(ShowDriver &driver, const Sample &sample) {
	const auto widget = driver.list.widget.data();
	const auto scroll = widget ? OwningScroll(widget) : nullptr;
	if (!scroll) {
		return;
	}
	auto &reading = *driver.reading;
	const auto &geometry = sample.geometry;
	const auto top = geometry.effective.y()
		+ (geometry.effective.height() - geometry.inScroll.height()) / 2;
	const auto from = scroll->scrollTop();
	const auto to = from + (geometry.inScroll.y() - top);
	++reading.scrollRequests;
	scroll->scrollToY(to);
	const auto after = TakeSample(driver);
	if (after.listAlive && (after.geometry.scrollTop != from)) {
		++reading.scrolls;
	}
	const auto now = crl::now();
	Observe(driver, after, now);
	driver.stableSince = now;
	Note(CorrectionText(reading, from, to, sample, after));
}

void OnActionTimer(ShowDriver &driver) {
	if (driver.decided) {
		return;
	}
	const auto correction = base::take(driver.wantCorrection);
	const auto paint = base::take(driver.wantPaint);
	if (!correction && !paint) {
		return;
	}
	const auto sample = TakeSample(driver);
	if (!Usable(sample) || (sample.geometry != driver.geometry)) {
		return;
	}
	auto &reading = *driver.reading;
	if (correction && Fits(sample) && !InView(sample)) {
		Correct(driver, sample);
	} else if (paint && InView(sample) && !Requested(driver)) {
		driver.paintRequestedFor = reading.generation;
		++reading.paintRequests;
		driver.list.widget->update(sample.rect);
	}
}

void PaintCompleted(
		ShowDriver &driver,
		int generation,
		const Geometry &geometry,
		const QRegion &region) {
	if (driver.decided) {
		return;
	}
	auto &reading = *driver.reading;
	const auto sample = TakeSample(driver);
	if ((reading.generation != generation)
		|| !sample.viewShown
		|| (sample.geometry != geometry)) {
		++reading.ignoredPaints;
		return;
	}
	driver.coverage += region;
	++reading.paints;
}

// Runs in the list's event filter, before its paintEvent. A paint counts
// only when it arrives after the helper's repaint request for the current
// geometry generation, the list's own early return will not skip it and
// the geometry is the one the helper last observed, before and after the
// paint ran. A paint received before that request is not even queued, so
// its confirmation can never run after the request and count then.
void PaintReceived(
		ShowDriver &driver,
		not_null<QPaintEvent*> e,
		const std::weak_ptr<ShowDriver> &weak) {
	if (driver.decided
		|| !driver.sampled
		|| !driver.filter
		|| !Requested(driver)) {
		return;
	}
	const auto sample = TakeSample(driver);
	const auto region = e->region();
	if (!sample.viewShown || !region.intersects(sample.rect)) {
		return;
	} else if (!Usable(sample)
		|| (sample.geometry != driver.geometry)
		|| PaintSkipped(driver.list, e)) {
		++driver.reading->ignoredPaints;
		return;
	}
	const auto generation = driver.reading->generation;
	const auto geometry = sample.geometry;
	InvokeQueued(driver.filter.data(), [=] {
		if (const auto strong = weak.lock()) {
			PaintCompleted(*strong, generation, geometry, region);
		}
	});
}

[[nodiscard]] bool AskReady(ShowDriver &driver, const Sample &sample) {
	auto &reading = *driver.reading;
	const auto view = not_null<const Element*>(sample.view);
	const auto ready = !driver.args.ready || driver.args.ready(view);
	++reading.readyChecks;
	if (!ready) {
		++reading.readyFalse;
	}
	reading.ready = ready;
	if (driver.args.readyDetails) {
		reading.readyDetails = driver.args.readyDetails(view);
	}
	return ready;
}

[[nodiscard]] QString CannotFitReason(const Sample &sample) {
	const auto &geometry = sample.geometry;
	const auto bounds = QRect(QPoint(), geometry.element.size());
	if (!sample.localInside) {
		return u"the requested element-local rect %1 is empty or not "
			u"inside the view's bounds %2"_q.arg(
				RectText(geometry.local),
				RectText(bounds));
	} else if (geometry.inScroll.height() > geometry.effective.height()) {
		return u"the rect %1 is %2 px tall and the visible message area "
			u"%3 only %4 px"_q.arg(
				RectText(sample.rect),
				NumberText(geometry.inScroll.height()),
				RectText(geometry.viewport),
				NumberText(geometry.effective.height()));
	}
	return u"the rect spans x %1..%2 in the scroll and the visible "
		u"message area only %3..%4; the helper scrolls only "
		u"vertically"_q.arg(
			NumberText(geometry.inScroll.left()),
			NumberText(geometry.inScroll.right()),
			NumberText(geometry.effective.left()),
			NumberText(geometry.effective.right()));
}

[[nodiscard]] QString ScrollCountText(const ChatListShow &reading) {
	return u"%1 scroll requests, %2 of them moved the scroll"_q.arg(
		NumberText(reading.scrollRequests),
		NumberText(reading.scrolls));
}

[[nodiscard]] QString OutOfViewReason(
		const ShowDriver &driver,
		const Sample &sample) {
	const auto &reading = *driver.reading;
	const auto &geometry = sample.geometry;
	const auto mapped = sample.mappedResolved
		? u"resolved"_q
		: OrNone(sample.mappedRefusal);
	return u"the rect %2 was not wholly in view within %3 ms (%1): "
		u"inViewport=%4 viewport=%5 inScroll=%6 effective=%7 pending=%8 "
		u"mapped: %9"_q.arg(
			ScrollCountText(reading),
			RectText(sample.rect),
			NumberText(reading.timeout),
			FlagText(sample.inViewport),
			RectText(geometry.viewport),
			RectText(geometry.inScroll),
			RectText(geometry.effective),
			FlagText(Pending(geometry)),
			mapped);
}

[[nodiscard]] QString PaintGateText(
		const ShowDriver &driver,
		const Sample &sample) {
	const auto &reading = *driver.reading;
	const auto window = driver.list.widget->window();
	const auto handle = window->windowHandle();
	const auto uncovered = QRegion(sample.rect).subtracted(driver.coverage);
	return u"no list paint received after the repaint request covered the "
		u"rect at its current geometry: paintRequests=%1 paints=%2 "
		u"ignoredPaints=%3 uncovered=%4 windowMinimized=%5 "
		u"windowExposed=%6"_q.arg(
			NumberText(reading.paintRequests),
			NumberText(reading.paints),
			NumberText(reading.ignoredPaints),
			RectText(uncovered.boundingRect()),
			FlagText(window->isMinimized()),
			FlagText(handle && handle->isExposed()));
}

// The first gate still false on an in-view rectangle.
[[nodiscard]] QString NotReadyReason(
		const ShowDriver &driver,
		const Sample &sample,
		crl::time now) {
	const auto &reading = *driver.reading;
	const auto &geometry = sample.geometry;
	const auto gate = Pending(geometry)
		? u"the layout stayed pending: viewPending=%1 listPending=%2"_q.arg(
			FlagText(geometry.viewPending),
			FlagText(geometry.listPending))
		: ((now - driver.stableSince) < kSettle)
		? u"the geometry had not read the same for %1 ms by the deadline "
			u"(generation=%2)"_q.arg(
				NumberText(kSettle),
				NumberText(reading.generation))
		: !reading.painted
		? PaintGateText(driver, sample)
		: u"the caller's readiness stayed false: readyChecks=%1 "
			u"readyFalse=%2"_q.arg(
				NumberText(reading.readyChecks),
				NumberText(reading.readyFalse));
	return u"in view (%1) but not ready within %2 ms: "_q.arg(
		ScrollCountText(reading),
		NumberText(reading.timeout)) + gate;
}

// Names the last state when the deadline passed with no decision. The
// caller's details are read once more on the current view, so a refusal
// carries them even when readiness was never asked.
void Classify(ShowDriver &driver, const Sample &sample, crl::time now) {
	const auto &geometry = sample.geometry;
	auto &reading = *driver.reading;
	if (sample.viewShown && driver.args.readyDetails) {
		reading.readyDetails = driver.args.readyDetails(
			not_null<const Element*>(sample.view));
	}
	if (!sample.viewShown) {
		Decide(
			driver,
			Refusal::NoView,
			u"the list's own view of message %1 was lost and not shown "
			u"again within %3 ms (%2); the reading keeps its last "
			u"geometry - %4"_q.arg(
				IdText(driver.item),
				ScrollCountText(reading),
				NumberText(reading.timeout),
				NoViewHint(driver.list.kind)));
	} else if (!sample.scrollOwned
		|| geometry.element.isEmpty()
		|| geometry.effective.isEmpty()) {
		Decide(
			driver,
			Refusal::NotReady,
			u"no usable layout within %1 ms: element=%2 effective=%3 "
			u"scrollOwned=%4"_q.arg(
				NumberText(reading.timeout),
				RectText(geometry.element),
				RectText(geometry.effective),
				FlagText(sample.scrollOwned)));
	} else if (!Pending(geometry) && !Fits(sample)) {
		Decide(
			driver,
			Refusal::CannotFit,
			CannotFitReason(sample)
				+ u"; the layout had not read the same for %1 ms by the "
				u"deadline"_q.arg(NumberText(kSettle)));
	} else if (!InView(sample)) {
		Decide(driver, Refusal::OutOfView, OutOfViewReason(driver, sample));
	} else {
		Decide(
			driver,
			Refusal::NotReady,
			NotReadyReason(driver, sample, now));
	}
}

// The stage's until: observes, flags what the timer should do, and
// decides. Product state is only read.
[[nodiscard]] bool ObserveShow(ShowDriver &driver) {
	if (driver.decided) {
		return true;
	}
	const auto now = crl::now();
	const auto sample = TakeSample(driver);
	Observe(driver, sample, now);
	auto &reading = *driver.reading;
	if (!sample.listAlive) {
		Decide(
			driver,
			Refusal::NoList,
			u"the list was destroyed while the helper waited"_q);
		return true;
	} else if (!sample.itemAlive) {
		Decide(
			driver,
			Refusal::NoTarget,
			u"message %1 was destroyed while the helper waited"_q.arg(
				IdText(driver.item)));
		return true;
	}
	const auto settled = (now - driver.stableSince) >= kSettle;
	reading.settled = settled;
	if (settled && Usable(sample)) {
		if (!Fits(sample)) {
			Decide(driver, Refusal::CannotFit, CannotFitReason(sample));
			return true;
		} else if (!InView(sample)) {
			driver.wantCorrection = true;
		} else if (!reading.painted) {
			driver.wantPaint = !Requested(driver);
		} else if (AskReady(driver, sample)) {
			Decide(driver, Refusal::None, QString());
			return true;
		}
	}
	if (now >= driver.deadline) {
		Classify(driver, sample, now);
		return true;
	}
	return false;
}

void DeliverEnter(not_null<QWidget*> list, QPoint point) {
	auto enter = QEnterEvent(
		QPointF(point),
		QPointF(list->mapTo(list->window(), point)),
		QPointF(list->mapToGlobal(point)));
	Settle([&] {
		QApplication::sendEvent(list.get(), &enter);
	});
}

void DeliverMove(not_null<QWidget*> list, QPoint point) {
	auto move = QMouseEvent(
		QEvent::MouseMove,
		QPointF(point),
		QPointF(list->mapToGlobal(point)),
		Qt::NoButton,
		Qt::NoButton,
		Qt::NoModifier);
	Settle([&] {
		QApplication::sendEvent(list.get(), &move);
	});
}

void DeliverLeave(not_null<QWidget*> list) {
	auto leave = QEvent(QEvent::Leave);
	Settle([&] {
		QApplication::sendEvent(list.get(), &leave);
	});
}

[[nodiscard]] QString HoverStateText(
		const ShowDriver &driver,
		const Sample &sample,
		QPoint local) {
	const auto widget = driver.list.widget.data();
	if (!widget) {
		return u"list destroyed"_q;
	}
	const auto view = sample.view;
	const auto identity = !view
		? u"lost"_q
		: (sample.geometry == driver.geometry)
		? u"same"_q
		: u"changed"_q;
	const auto state = !view
		? u"none"_q
		: (view->pointState(local) == HistoryView::PointState::Outside)
		? u"outside"_q
		: u"inside"_q;
	return u"mouseActive=%1 activeWindow=%2 pending=%3 view=%4 "
		u"underCursor=%5 pointState=%6"_q.arg(
			MouseActiveText(driver.list),
			FlagText(widget->window()->isActiveWindow()),
			FlagText(Pending(sample.geometry)),
			identity,
			FlagText(view && view->isUnderCursor()),
			state);
}

void RefuseHover(ShowDriver &driver, Refusal refusal, QString reason) {
	auto &reading = *driver.reading;
	reading.refusal = refusal;
	reading.reason = std::move(reason);
}

// Runs in the stage's then, in the turn the until accepted, so the next
// stage's run receives the hover before any other event.
void DeliverHover(ShowDriver &driver) {
	auto &reading = *driver.reading;
	const auto before = TakeSample(driver);
	if (!before.viewShown || (before.geometry != driver.geometry)) {
		RefuseHover(
			driver,
			Refusal::HoverUnresolved,
			u"the accepted view or its geometry changed before the hover; "
			u"no input was sent"_q);
		return;
	}
	const auto local = driver.args.hover(not_null<const Element*>(
		before.view));
	const auto point = local + before.geometry.element.topLeft();
	reading.hoverLocal = local;
	reading.hover = point;
	if (!before.geometry.local.contains(local)) {
		RefuseHover(
			driver,
			Refusal::HoverOutside,
			u"the hover point %1 (element-local %2) is outside the shown "
			u"rect %3 (element-local %4); no input was sent"_q.arg(
				PointText(point),
				PointText(local),
				RectText(before.rect),
				RectText(before.geometry.local)));
		return;
	}
	DeliverEnter(driver.list.widget.data(), point);
	if (const auto widget = driver.list.widget.data()) {
		DeliverMove(widget, point);
	}
	reading.hoverSent = true;
	const auto after = TakeSample(driver);
	reading.hoverDetails = HoverStateText(driver, after, local);
	const auto same = after.viewShown
		&& (after.geometry == driver.geometry);
	if (same
		&& after.view->isUnderCursor()
		&& (after.view->pointState(local)
			!= HistoryView::PointState::Outside)) {
		reading.hovered = true;
		return;
	}
	RefuseHover(
		driver,
		Refusal::HoverUnresolved,
		!after.listAlive
			? u"the list was destroyed while the hover's postponed calls "
				u"drained"_q
			: !same
			? u"the view or its geometry changed while the hover's "
				u"postponed calls drained, so the point no longer names "
				u"the accepted content; a leave was sent"_q
			: u"after the enter and the move the list did not resolve the "
				u"view's content at %1; a leave was sent"_q.arg(
					PointText(point)));
	if (const auto widget = driver.list.widget.data()) {
		DeliverLeave(widget);
	}
}

// Idempotent: the stage's then and Runner::onFinish both run it. Deleting
// the filter also drops the paint confirmations queued to it.
void ReleaseShow(ShowDriver &driver) {
	driver.timer.cancel();
	delete base::take(driver.filter).data();
}

void FinishShow(ShowDriver &driver) {
	ReleaseShow(driver);
	auto &reading = *driver.reading;
	if ((reading.refusal == Refusal::None) && driver.args.hover) {
		DeliverHover(driver);
	}
	reading.finished = true;
	Note(ChatListShowText(reading));
}

void StartShow(const std::shared_ptr<ShowDriver> &driver) {
	auto &reading = *driver->reading;
	driver->started = crl::now();
	driver->deadline = driver->started + reading.timeout;
	const auto target = driver->args.resolve
		? driver->args.resolve()
		: ChatListShowTarget();
	driver->item = reading.item = target.item;
	if (!target.list) {
		Decide(
			*driver,
			Refusal::NoList,
			u"the resolver returned no list widget; nothing was "
			u"scrolled"_q);
		return;
	}
	reading.list = target.list;
	reading.listIdentity = WidgetDescription(target.list);
	driver->list = ResolveChatList(target.list);
	reading.kind = KindName(driver->list.kind);
	if (driver->list.kind == ListKind::None) {
		Decide(
			*driver,
			Refusal::NoList,
			u"the widget is neither a HistoryInner, a "
			u"HistoryView::ListWidget nor an AdminLog::InnerWidget; "
			u"nothing was scrolled"_q);
		return;
	} else if (!OwningScroll(target.list)) {
		const auto parent = target.list->parentWidget();
		Decide(
			*driver,
			Refusal::NoList,
			u"the list is not the owned widget of a Ui::ElasticScroll "
			u"parent (parent=%1); nothing was scrolled"_q.arg(parent
				? WidgetDescription(parent)
				: u"none"_q));
		return;
	} else if (!target.item) {
		Decide(
			*driver,
			Refusal::NoTarget,
			u"the resolver named no message; nothing was scrolled"_q);
		return;
	}
	const auto sample = TakeSample(*driver);
	Observe(*driver, sample, driver->started);
	if (!sample.itemAlive) {
		Decide(
			*driver,
			Refusal::NoTarget,
			u"message %1 is not in the list's session; nothing was "
			u"scrolled"_q.arg(IdText(target.item)));
		return;
	} else if (!sample.viewShown) {
		Decide(
			*driver,
			Refusal::NoView,
			u"the list shows no view of its own for message %1; nothing "
			u"was scrolled - %2"_q.arg(
				IdText(target.item),
				NoViewHint(driver->list.kind)));
		return;
	}
	reading.initialRect = sample.rect;
	reading.initialViewport = sample.geometry.viewport;
	const auto weak = std::weak_ptr<ShowDriver>(driver);
	driver->filter = base::install_event_filter(target.list, [=](
			not_null<QEvent*> e) {
		if (e->type() == QEvent::Paint) {
			if (const auto strong = weak.lock()) {
				PaintReceived(
					*strong,
					static_cast<QPaintEvent*>(e.get()),
					weak);
			}
		}
		return base::EventFilterResult::Continue;
	}).get();
	const auto raw = driver.get();
	driver->timer.setCallback([raw] {
		OnActionTimer(*raw);
	});
	driver->timer.callEach(kActionInterval);
	if (Usable(sample) && Fits(sample) && !InView(sample)) {
		Correct(*driver, sample);
	}
}

} // namespace

QString ChatListShowRefusalName(ChatListShowRefusal refusal) {
	switch (refusal) {
	case Refusal::None: return u"none"_q;
	case Refusal::NoList: return u"no-list"_q;
	case Refusal::NoTarget: return u"no-target"_q;
	case Refusal::NoView: return u"no-view"_q;
	case Refusal::CannotFit: return u"cannot-fit"_q;
	case Refusal::OutOfView: return u"out-of-view"_q;
	case Refusal::NotReady: return u"not-ready"_q;
	case Refusal::HoverOutside: return u"hover-outside"_q;
	case Refusal::HoverUnresolved: return u"hover-unresolved"_q;
	}
	Unexpected("Refusal in Test::ChatListShowRefusalName.");
}

QString ChatListShowText(const ChatListShow &reading) {
	const auto computed = reading.hoverSent
		|| (reading.refusal == ChatListShowRefusal::HoverOutside);
	const auto hover = [&](QPoint point) {
		return computed ? PointText(point) : u"none"_q;
	};
	// One substitution per field, so a caller's text that carries a %N
	// marker is never re-substituted by a later arg().
	const auto fields = QStringList{
		u"name=%1"_q.arg(reading.name),
		u"finished=%1"_q.arg(FlagText(reading.finished)),
		u"shown=%1"_q.arg(FlagText(reading.shown())),
		u"kind=%1"_q.arg(OrNone(reading.kind)),
		u"list=%1"_q.arg(OrNone(reading.listIdentity)),
		u"item=%1"_q.arg(IdText(reading.item)),
		u"view=%1"_q.arg(OrNone(reading.viewIdentity)),
		u"viewShown=%1"_q.arg(FlagText(reading.viewShown)),
		u"local=%1"_q.arg(RectText(reading.local)),
		u"element=%1"_q.arg(RectText(reading.element)),
		u"rect=%1"_q.arg(RectText(reading.rect)),
		u"viewport=%1"_q.arg(RectText(reading.viewport)),
		u"mapped=%1"_q.arg(RectText(reading.mapped)),
		u"initialRect=%1"_q.arg(RectText(reading.initialRect)),
		u"initialViewport=%1"_q.arg(RectText(reading.initialViewport)),
		u"scrollTop=%1"_q.arg(NumberText(reading.scrollTop)),
		u"scrollRequests=%1"_q.arg(NumberText(reading.scrollRequests)),
		u"scrolls=%1"_q.arg(NumberText(reading.scrolls)),
		u"generation=%1"_q.arg(NumberText(reading.generation)),
		u"settled=%1"_q.arg(FlagText(reading.settled)),
		u"pending=%1"_q.arg(FlagText(reading.pending)),
		u"inView=%1"_q.arg(FlagText(reading.inView)),
		u"paintRequests=%1"_q.arg(NumberText(reading.paintRequests)),
		u"paints=%1"_q.arg(NumberText(reading.paints)),
		u"ignoredPaints=%1"_q.arg(NumberText(reading.ignoredPaints)),
		u"painted=%1"_q.arg(FlagText(reading.painted)),
		u"readyChecks=%1"_q.arg(NumberText(reading.readyChecks)),
		u"readyFalse=%1"_q.arg(NumberText(reading.readyFalse)),
		u"ready=%1"_q.arg(FlagText(reading.ready)),
		u"hover=%1"_q.arg(hover(reading.hover)),
		u"hoverLocal=%1"_q.arg(hover(reading.hoverLocal)),
		u"hoverSent=%1"_q.arg(FlagText(reading.hoverSent)),
		u"hovered=%1"_q.arg(FlagText(reading.hovered)),
		u"elapsedMs=%1"_q.arg(NumberText(reading.elapsedMs)),
		u"timeout=%1"_q.arg(NumberText(reading.timeout)),
		u"refusal=%1"_q.arg(ChatListShowRefusalName(reading.refusal)),
		u"readiness={%1}"_q.arg(reading.readyDetails),
		u"hoverState={%1}"_q.arg(reading.hoverDetails),
	};
	const auto line = u"CHAT_LIST_SHOW: "_q + fields.join(u" "_q);
	return reading.reason.isEmpty()
		? line
		: (line + u" - "_q + reading.reason);
}

std::shared_ptr<const ChatListShow> AppendShowInChatList(
		not_null<Runner*> runner,
		ChatListShowArgs args) {
	const auto driver = std::make_shared<ShowDriver>();
	driver->args = std::move(args);
	driver->reading = std::make_shared<ChatListShow>();
	auto &reading = *driver->reading;
	reading.name = driver->args.name;
	reading.timeout = std::max(driver->args.timeout, crl::time());
	reading.hoverRequested = bool(driver->args.hover);
	runner->add({
		.name = u"show in chat list: %1"_q.arg(reading.name),
		.skipReason = driver->args.skipReason,
		.run = [=] {
			StartShow(driver);
		},
		.until = [=] {
			return ObserveShow(*driver);
		},
		.then = [=] {
			FinishShow(*driver);
		},
		.timeout = reading.timeout + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return ChatListShowText(*driver->reading);
		},
	});
	runner->onFinish([=] {
		ReleaseShow(*driver);
	});
	return driver->reading;
}

// The self-test access of test_chat_show.h: the helper's own reads, for
// test_chat_show_self_test.cpp.
ChatListShowKind ChatListShowKindOf(not_null<QWidget*> list) {
	return ResolveChatList(list).kind;
}

QString ChatListShowKindName(ChatListShowKind kind) {
	return KindName(kind);
}

Ui::ElasticScroll *ChatListShowOwningScroll(not_null<QWidget*> list) {
	return OwningScroll(list);
}

QRect ChatListShowVisibleArea(not_null<QWidget*> list) {
	return VisibleArea(ResolveChatList(list));
}

bool ChatListShowPendingResize(not_null<QWidget*> list) {
	return ListPending(ResolveChatList(list));
}

QString ChatListShowMouseActiveText(not_null<QWidget*> list) {
	return MouseActiveText(ResolveChatList(list));
}

QString ChatListShowViewIdentity(const HistoryView::Element *view) {
	return ElementIdentity(view);
}

// The same reads for test_chat_show_place.cpp, so the placement helper
// resolves lists, messages and views exactly as this helper does.
HistoryItem *ChatListShowItem(not_null<QWidget*> list, FullMsgId id) {
	const auto resolved = ResolveChatList(list);
	if ((resolved.kind == ListKind::None) || !id) {
		return nullptr;
	}
	return ListSession(resolved).data().message(id);
}

HistoryView::Element *ChatListShowOwnView(
		not_null<QWidget*> list,
		not_null<HistoryItem*> item) {
	return OwnView(ResolveChatList(list), item.get());
}

QRect ChatListShowElementRect(
		not_null<QWidget*> list,
		not_null<const HistoryView::Element*> view) {
	return QRect(
		ElementOrigin(ResolveChatList(list), view),
		QSize(view->width(), view->height()));
}

} // namespace Test

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
