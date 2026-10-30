/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_chat_show_place.h"

#include "base/timer.h"
#include "data/data_session.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_inner_widget.h"
#include "history/history_item.h"
#include "test/test_capture.h"
#include "test/test_chat_show.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "test/test_widget_fixture.h"
#include "tgw/tgw_runtime.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/ui_utility.h"

#include <QtCore/QStringList>

#include "styles/style_chat.h"

namespace Test {
namespace {

using Element = HistoryView::Element;
using Placement = ChatListPlacement;
using Refusal = ChatListPlaceRefusal;

// A placement is judged only on geometry that read the same this long, as
// in test_chat_show.cpp: longer than HistoryWidget::newItemAdded's reveal
// animation.
constexpr auto kSettle = crl::time(400);

// How often the helper's own timer acts on what its until last flagged.
constexpr auto kActionInterval = crl::time(50);

// The record's sampling period, the source overlay's sampler period: a
// window is judged on every sample inside it, never on its two ends.
constexpr auto kRecordInterval = crl::time(100);

// How far past the visible area's edge an off placement scrolls, in
// logical px, so the views stay within the two pages the lists keep loaded
// (kUnloadHeavyPartsPages in history_inner_widget.cpp), as the conformance
// runner's kOffscreenMargin does.
constexpr auto kOffMargin = 8;

// What one message must read unchanged while a placement counts as
// settled. |view| is the view's address, compared and never dereferenced,
// so a replaced view is a new generation.
struct ItemGeometry {
	quintptr view = 0;
	QRect element;
	bool alive = false;
	bool viewPending = false;

	friend bool operator==(
		const ItemGeometry &,
		const ItemGeometry &) = default;
};

// What a sample must read unchanged while a placement counts as settled:
// any change starts a new generation and restarts the settle.
struct Geometry {
	std::vector<ItemGeometry> items;
	QRect viewport;
	QRect effective;
	int scrollTop = 0;
	int scrollTopMax = 0;
	bool listPending = false;

	friend bool operator==(const Geometry &, const Geometry &) = default;
};

// One message in one synchronous sample. |item| and |view| are valid only
// inside the call that took it, never across an event turn or a scroll;
// |inScroll| is the view's bounds mapped into the owning scroll.
struct ItemSample {
	HistoryItem *item = nullptr;
	Element *view = nullptr;
	QRect inScroll;
	bool placed = false;
};

// One synchronous reading of the list, in the target's message order. A
// dead list leaves |items| empty. |band| is the union of the shown views
// in the list's coordinates; |itemsAlive|, |viewsShown| and |placed| hold
// for every message.
struct Sample {
	Geometry geometry;
	std::vector<ItemSample> items;
	QRect band;
	bool listAlive = false;
	bool scrollOwned = false;
	bool itemsAlive = false;
	bool viewsShown = false;
	bool placed = false;
};

// Everything one appended placement owns. The stage lambdas and the
// onFinish release hold it by std::shared_ptr; its timers hold it by a
// plain pointer, because it owns them, and the record's queued sample by a
// std::weak_ptr.
struct PlaceDriver {
	ChatListPlaceArgs args;
	std::shared_ptr<ChatListPlace> reading;
	QPointer<QWidget> list;
	std::vector<FullMsgId> items;
	base::Timer actionTimer;
	base::Timer recordTimer;
	rpl::lifetime recordLifetime;
	Geometry geometry;
	crl::time started = 0;
	crl::time deadline = 0;
	crl::time stableSince = 0;
	bool sampled = false;
	bool wantCorrection = false;
	bool decided = false;
	bool samplePosted = false;
};

// Everything one appended stay owns. It only reads its placement's record,
// which that placement's driver keeps filling.
struct StayDriver {
	ChatListStayArgs args;
	std::shared_ptr<ChatListStay> reading;
	crl::time started = 0;
};

[[nodiscard]] QString FlagText(bool value) {
	return value ? u"1"_q : u"0"_q;
}

[[nodiscard]] QString NumberText(qint64 value) {
	return QString::number(value);
}

[[nodiscard]] QString IdText(FullMsgId id) {
	return u"%1:%2"_q.arg(
		NumberText(qint64(id.peer.value)),
		NumberText(qint64(id.msg.bare)));
}

[[nodiscard]] QString OrNone(const QString &value) {
	return value.isEmpty() ? u"none"_q : value;
}

// A mark's reading as 1 or 0, "none" when it was never asked.
[[nodiscard]] QString VisibleText(const ChatListPlaceMark &mark) {
	return mark.asked ? FlagText(mark.visible) : u"none"_q;
}

[[nodiscard]] QString ElementText(const ChatListPlaceMark &mark) {
	return mark.viewShown ? RectText(mark.element) : u"none"_q;
}

// Whether one message is where |placement| asks, by the lists' own
// visibility rule; an off placement counts a message without an own view
// as off.
[[nodiscard]] bool PlacedNow(
		Placement placement,
		bool viewShown,
		QRect element,
		QRect viewport,
		QRect inScroll,
		QRect effective) {
	switch (placement) {
	case Placement::OnScreen:
		return viewShown
			&& !element.isEmpty()
			&& viewport.contains(element)
			&& effective.contains(inScroll);
	case Placement::OffAbove:
		return !viewShown
			|| (element.y() + element.height() <= viewport.y());
	case Placement::OffBelow:
		return !viewShown
			|| (element.y() >= viewport.y() + viewport.height());
	}
	Unexpected("Placement in Test::PlacedNow.");
}

// Reads the list and every message of the target through test_chat_show's
// seam, so lists, messages and own views resolve as that helper resolves
// them. Keeps no pointer past the call.
[[nodiscard]] Sample TakeSample(const PlaceDriver &driver) {
	auto result = Sample();
	const auto list = driver.list.data();
	if (!list) {
		return result;
	}
	result.listAlive = true;
	const auto scroll = ChatListShowOwningScroll(list);
	result.scrollOwned = (scroll != nullptr);
	auto &geometry = result.geometry;
	geometry.viewport = ChatListShowVisibleArea(list);
	geometry.listPending = ChatListShowPendingResize(list);
	if (scroll) {
		geometry.scrollTop = scroll->scrollTop();
		geometry.scrollTopMax = scroll->scrollTopMax();
		geometry.effective = Ui::MapFrom(
			scroll,
			list,
			geometry.viewport).intersected(scroll->rect());
	}
	const auto placement = driver.reading->placement;
	result.itemsAlive = true;
	result.viewsShown = true;
	result.placed = true;
	for (const auto &id : driver.items) {
		auto &state = geometry.items.emplace_back();
		auto &item = result.items.emplace_back();
		item.item = ChatListShowItem(list, id);
		item.view = item.item
			? ChatListShowOwnView(list, item.item)
			: nullptr;
		state.alive = (item.item != nullptr);
		if (item.view) {
			state.view = reinterpret_cast<quintptr>(item.view);
			state.element = ChatListShowElementRect(list, item.view);
			state.viewPending = item.view->pendingResize();
			if (scroll) {
				item.inScroll = Ui::MapFrom(scroll, list, state.element);
			}
			result.band = result.band.united(state.element);
		}
		item.placed = state.alive && PlacedNow(
			placement,
			(item.view != nullptr),
			state.element,
			geometry.viewport,
			item.inScroll,
			geometry.effective);
		result.itemsAlive = result.itemsAlive && state.alive;
		result.viewsShown = result.viewsShown && (item.view != nullptr);
		result.placed = result.placed && item.placed;
	}
	return result;
}

[[nodiscard]] bool ViewPending(const Geometry &geometry) {
	return ranges::any_of(geometry.items, &ItemGeometry::viewPending);
}

[[nodiscard]] bool Pending(const Geometry &geometry) {
	return geometry.listPending || ViewPending(geometry);
}

// A shown view that has no laid-out bounds yet.
[[nodiscard]] bool EmptyView(const Geometry &geometry) {
	return ranges::any_of(geometry.items, [](const ItemGeometry &item) {
		return item.view && item.element.isEmpty();
	});
}

// A layout the placement can be judged on: the list in its owning scroll,
// every message alive, a visible message area inside the scroll, nothing
// pending and every shown view laid out. An on placement also needs every
// view shown.
[[nodiscard]] bool Usable(Placement placement, const Sample &sample) {
	const auto &geometry = sample.geometry;
	return sample.listAlive
		&& sample.scrollOwned
		&& sample.itemsAlive
		&& (sample.viewsShown || (placement != Placement::OnScreen))
		&& !geometry.effective.isEmpty()
		&& !Pending(geometry)
		&& !EmptyView(geometry);
}

// A shown view reaching past the visible area's sides in the scroll; the
// helper scrolls only vertically.
[[nodiscard]] bool OutsideHorizontally(
		const ItemSample &item,
		QRect effective) {
	return item.view
		&& ((item.inScroll.left() < effective.left())
			|| (item.inScroll.right() > effective.right()));
}

// For an on placement: the views' band is no taller than the visible
// area and no view reaches past its sides.
[[nodiscard]] bool Fits(const Sample &sample) {
	const auto &effective = sample.geometry.effective;
	return (sample.band.height() <= effective.height())
		&& ranges::none_of(sample.items, [&](const ItemSample &item) {
			return OutsideHorizontally(item, effective);
		});
}

// The scroll top that puts the views where |placement| asks: an on
// placement centres their band in the visible area, clamped to the
// scroll's range; an off placement puts the area's edge kOffMargin past
// the nearest shown view, unclamped, so Reachable() can judge it.
[[nodiscard]] int TargetTop(Placement placement, const Sample &sample) {
	const auto &geometry = sample.geometry;
	const auto &viewport = geometry.viewport;
	const auto &band = sample.band;
	const auto areaTop = [&] {
		switch (placement) {
		case Placement::OnScreen:
			return band.y() - (viewport.height() - band.height()) / 2;
		case Placement::OffAbove:
			return band.y() + band.height() + kOffMargin;
		case Placement::OffBelow:
			return band.y() - kOffMargin - viewport.height();
		}
		Unexpected("Placement in Test::TargetTop.");
	}();
	const auto target = geometry.scrollTop + (areaTop - viewport.y());
	return (placement == Placement::OnScreen)
		? std::clamp(target, 0, std::max(geometry.scrollTopMax, 0))
		: target;
}

// Whether a scroll can place the views: an on placement when they fit, an
// off one when its target lies inside [0, scrollTopMax()]. Ask it only of
// a sample that is not placed: the target asks for kOffMargin past the
// edge, while a placement needs only the edge, so views already off by
// less near the list's end must be accepted, never refused.
[[nodiscard]] bool Reachable(Placement placement, const Sample &sample) {
	if (placement == Placement::OnScreen) {
		return Fits(sample);
	}
	const auto target = TargetTop(placement, sample);
	return (target >= 0) && (target <= sample.geometry.scrollTopMax);
}

// What |sample| saw of the message at |index|, without the reading. Only
// for a sample of a live list.
[[nodiscard]] ChatListPlaceMark MarkOf(
		const Sample &sample,
		int index,
		crl::time now) {
	const auto &geometry = sample.geometry;
	const auto &item = sample.items[index];
	return {
		.at = now,
		.element = geometry.items[index].element,
		.viewport = geometry.viewport,
		.scrollTop = geometry.scrollTop,
		.alive = (item.item != nullptr),
		.viewShown = (item.view != nullptr),
		.placed = item.placed,
	};
}

// Asks the caller's reading of the message at |index| into |mark|; a
// destroyed message is not asked.
void AskReading(
		const PlaceDriver &driver,
		const Sample &sample,
		int index,
		ChatListPlaceMark &mark) {
	const auto &item = sample.items[index];
	if (!item.item) {
		return;
	}
	const auto view = static_cast<const Element*>(item.view);
	mark.visible = driver.args.visible(item.item, view);
	mark.details = driver.args.visibleDetails
		? driver.args.visibleDetails(item.item, view)
		: QString();
	mark.asked = true;
}

// Takes a sample into the driver's bookkeeping: a changed geometry starts
// a new generation and restarts the settle. Each message's last mark keeps
// its previous reading until the reading is asked again, and a dead list
// leaves the last geometry in the reading.
void Observe(PlaceDriver &driver, const Sample &sample, crl::time now) {
	auto &reading = *driver.reading;
	if (!driver.sampled || (sample.geometry != driver.geometry)) {
		driver.sampled = true;
		driver.geometry = sample.geometry;
		driver.stableSince = now;
		driver.wantCorrection = false;
		++reading.generation;
	}
	if (!sample.listAlive) {
		return;
	}
	reading.viewport = sample.geometry.viewport;
	reading.effective = sample.geometry.effective;
	reading.band = sample.band;
	reading.scrollTop = sample.geometry.scrollTop;
	reading.scrollTopMax = sample.geometry.scrollTopMax;
	reading.pending = Pending(sample.geometry);
	for (auto i = 0, count = int(reading.items.size()); i != count; ++i) {
		auto &last = reading.items[i].last;
		auto mark = MarkOf(sample, i, now);
		mark.visible = last.visible;
		mark.asked = last.asked;
		mark.details = std::move(last.details);
		last = std::move(mark);
	}
}

// The placement gate's readings, counted, on a settled placed sample; true
// when every message reads as the placement expects.
[[nodiscard]] bool AskReadings(PlaceDriver &driver, const Sample &sample) {
	auto &reading = *driver.reading;
	const auto expected = (reading.placement == Placement::OnScreen);
	auto agree = true;
	for (auto i = 0, count = int(reading.items.size()); i != count; ++i) {
		auto &item = reading.items[i];
		AskReading(driver, sample, i, item.last);
		++item.readings;
		if (item.last.visible != expected) {
			++item.disagreements;
			agree = false;
		}
	}
	return agree;
}

// Asks every alive message's reading once more, uncounted, so a refusal
// carries the reading of the sample that decided it.
void ReadAll(PlaceDriver &driver, const Sample &sample) {
	if (!sample.listAlive) {
		return;
	}
	auto &reading = *driver.reading;
	for (auto i = 0, count = int(reading.items.size()); i != count; ++i) {
		AskReading(driver, sample, i, reading.items[i].last);
	}
}

void Decide(PlaceDriver &driver, Refusal refusal, QString reason) {
	driver.decided = true;
	driver.wantCorrection = false;
	auto &reading = *driver.reading;
	reading.refusal = refusal;
	reading.reason = std::move(reason);
	reading.elapsedMs = crl::now() - driver.started;
}

// The acceptance: the accepting sample's marks are the record's first.
void Accept(PlaceDriver &driver, crl::time now) {
	Decide(driver, Refusal::None, QString());
	auto &reading = *driver.reading;
	reading.placedAt = now;
	for (auto &item : reading.items) {
		item.changes = { item.last };
	}
}

// Whether a message's last mark is against the placement: not placed, or
// read against it.
[[nodiscard]] bool Wrong(
		const ChatListPlace &reading,
		const ChatListPlaceMark &mark) {
	const auto expected = (reading.placement == Placement::OnScreen);
	return !mark.placed || (mark.asked && (mark.visible != expected));
}

// One message's last geometry and reading, for a reason.
[[nodiscard]] QString ItemText(const ChatListPlaceItem &item) {
	const auto &mark = item.last;
	return QStringList{
		IdText(item.id),
		u"placed=%1"_q.arg(FlagText(mark.placed)),
		u"visible=%1"_q.arg(VisibleText(mark)),
		u"element=%1"_q.arg(ElementText(mark)),
		u"details={%1}"_q.arg(mark.details),
	}.join(u" "_q);
}

// Every message's ItemText, or with |onlyWrong| only those against the
// placement.
[[nodiscard]] QString ItemsText(
		const ChatListPlace &reading,
		bool onlyWrong) {
	auto list = QStringList();
	for (const auto &item : reading.items) {
		if (!onlyWrong || Wrong(reading, item.last)) {
			list.push_back(ItemText(item));
		}
	}
	return list.isEmpty() ? u"none"_q : list.join(u"; "_q);
}

// The ids of the messages the list's session does not know, or with
// |views| of those the list shows no own view of.
[[nodiscard]] QString MissingIds(const ChatListPlace &reading, bool views) {
	auto list = QStringList();
	for (const auto &item : reading.items) {
		if (views ? !item.last.viewShown : !item.last.alive) {
			list.push_back(IdText(item.id));
		}
	}
	return list.join(u", "_q);
}

[[nodiscard]] QString ScrollCountText(const ChatListPlace &reading) {
	return u"%1 scroll requests, %2 of them moved the scroll"_q.arg(
		NumberText(reading.scrollRequests),
		NumberText(reading.scrolls));
}

// Why |list| is not a list the helper can place messages in, or empty.
[[nodiscard]] QString NoListReason(QWidget *list) {
	if (!list) {
		return u"the resolver returned no list widget"_q;
	} else if (ChatListShowKindOf(list) == ChatListShowKind::None) {
		return u"the widget is not a HistoryInner, a HistoryView::ListWidget "
			u"or an AdminLog::InnerWidget"_q;
	} else if (!ChatListShowOwningScroll(list)) {
		const auto parent = list->parentWidget();
		return u"the list is not the owned widget of its Ui::ElasticScroll "
			u"parent (parent=%1)"_q.arg(parent
				? WidgetDescription(parent)
				: u"none"_q);
	}
	return QString();
}

[[nodiscard]] QString CannotFitReason(
		const ChatListPlace &reading,
		const Sample &sample) {
	const auto &geometry = sample.geometry;
	const auto &effective = geometry.effective;
	const auto items = u" - "_q + ItemsText(reading, false);
	if (sample.band.height() > effective.height()) {
		return u"the views' band %1 is %2 px tall and the visible message "
			u"area %3 holds only %4 px; enlarge the window first or place "
			u"fewer messages"_q.arg(
				RectText(sample.band),
				NumberText(sample.band.height()),
				RectText(geometry.viewport),
				NumberText(effective.height()))
			+ items;
	}
	for (auto i = 0, count = int(sample.items.size()); i != count; ++i) {
		const auto &item = sample.items[i];
		if (OutsideHorizontally(item, effective)) {
			return u"message %1 spans x %2..%3 in the scroll, past the "
				u"visible message area's %4..%5, and the helper scrolls "
				u"only vertically"_q.arg(
					IdText(reading.items[i].id),
					NumberText(item.inScroll.left()),
					NumberText(item.inScroll.right()),
					NumberText(effective.left()),
					NumberText(effective.right()))
				+ items;
		}
	}
	return u"the views do not fit the visible message area"_q + items;
}

// The scroll top an off placement needs, outside the scroll's range, and
// the loaded content it lacks on that side of the band.
[[nodiscard]] QString CannotMoveOffReason(
		const ChatListPlace &reading,
		const Sample &sample) {
	const auto &geometry = sample.geometry;
	const auto &viewport = geometry.viewport;
	const auto &band = sample.band;
	const auto below = (reading.placement == Placement::OffBelow);
	const auto room = below
		? (band.y() - (viewport.y() - geometry.scrollTop))
		: ((viewport.y()
			+ viewport.height()
			+ (geometry.scrollTopMax - geometry.scrollTop))
			- (band.y() + band.height()));
	return u"%1 needs the scroll top at %2, outside [0, %3]: the list's "
		u"loaded content %4 the views' band %5 is %6 px and the placement "
		u"needs %7 (the visible area's %8 px and %9 px past its edge); "
		u"nothing was scrolled, add messages there or navigate first"_q.arg(
			ChatListPlacementName(reading.placement),
			NumberText(TargetTop(reading.placement, sample)),
			NumberText(geometry.scrollTopMax),
			below ? u"above"_q : u"below"_q,
			RectText(band),
			NumberText(room),
			NumberText(viewport.height() + kOffMargin),
			NumberText(viewport.height()),
			NumberText(kOffMargin))
		+ u" - "_q
		+ ItemsText(reading, false);
}

[[nodiscard]] QString LostViewReason(const ChatListPlace &reading) {
	return u"the %1's own view of message(s) %2 was lost and not shown "
		u"again within %3 ms (%4)"_q.arg(
			reading.kind,
			MissingIds(reading, true),
			NumberText(reading.timeout),
			ScrollCountText(reading))
		+ u" - "_q
		+ ItemsText(reading, false);
}

// The first placement gate still false at the deadline.
[[nodiscard]] QString GateText(
		const PlaceDriver &driver,
		const Sample &sample,
		crl::time now) {
	const auto &reading = *driver.reading;
	const auto &geometry = sample.geometry;
	const auto pending = Pending(geometry);
	if (!pending && !Usable(reading.placement, sample)) {
		return u"no usable layout: scrollOwned=%1 viewsShown=%2 "
			u"effective=%3 emptyView=%4"_q.arg(
				FlagText(sample.scrollOwned),
				FlagText(sample.viewsShown),
				RectText(geometry.effective),
				FlagText(EmptyView(geometry)));
	} else if (pending) {
		return u"the layout stayed pending: listPending=%1 "
			u"viewPending=%2"_q.arg(
				FlagText(geometry.listPending),
				FlagText(ViewPending(geometry)));
	} else if (!sample.placed) {
		return u"not every message was where the placement asks - "_q
			+ ItemsText(reading, true);
	}
	return u"the placed geometry read the same for only %1 of %2 ms "
		u"(generation=%3)"_q.arg(
			NumberText(now - driver.stableSince),
			NumberText(kSettle),
			NumberText(reading.generation));
}

[[nodiscard]] QString OutOfPlaceReason(
		const PlaceDriver &driver,
		const Sample &sample,
		crl::time now) {
	const auto &reading = *driver.reading;
	return u"the messages were not all %1 and settled within %2 ms "
		u"(%3): "_q.arg(
			ChatListPlacementName(reading.placement),
			NumberText(reading.timeout),
			ScrollCountText(reading))
		+ GateText(driver, sample, now);
}

[[nodiscard]] QString DisagreeReason(const ChatListPlace &reading) {
	auto list = QStringList();
	for (const auto &item : reading.items) {
		if (Wrong(reading, item.last)) {
			list.push_back(ItemText(item)
				+ u" readings=%1 disagreements=%2"_q.arg(
					NumberText(item.readings),
					NumberText(item.disagreements)));
		}
	}
	return u"placed and settled at the deadline (%1 ms), but the caller's "
		u"reading disagreed with the %2 placement (expected "
		u"visible=%3) - "_q.arg(
			NumberText(reading.timeout),
			ChatListPlacementName(reading.placement),
			FlagText(reading.placement == Placement::OnScreen))
		+ (list.isEmpty()
			? u"none on the deciding sample"_q
			: list.join(u"; "_q));
}

// The cannot-move-off refusal; |target| keeps the scroll top the placement
// would have asked, outside the range. Ask the readings first.
void RefuseMoveOff(
		PlaceDriver &driver,
		const Sample &sample,
		const QString &prefix) {
	auto &reading = *driver.reading;
	reading.target = TargetTop(reading.placement, sample);
	Decide(
		driver,
		Refusal::CannotMoveOff,
		prefix + CannotMoveOffReason(reading, sample));
}

[[nodiscard]] QString CorrectionText(
		const ChatListPlace &reading,
		int from,
		int to,
		const Sample &after) {
	const auto now = after.geometry.scrollTop;
	const auto fields = QStringList{
		u"name=%1"_q.arg(reading.name),
		u"request=%1"_q.arg(NumberText(reading.scrollRequests)),
		u"scrollTop=%1"_q.arg(NumberText(from)),
		u"asked=%1"_q.arg(NumberText(to)),
		u"now=%1"_q.arg(NumberText(now)),
		u"moved=%1"_q.arg(FlagText(now != from)),
		u"band=%1"_q.arg(RectText(after.band)),
		u"viewport=%1"_q.arg(RectText(after.geometry.viewport)),
	};
	return u"CHAT_LIST_PLACE_SCROLL: "_q + fields.join(u" "_q);
}

// One scroll toward the placement, from |sample|, which must have been
// taken in this same call. The scroll runs the list's scroll handlers
// synchronously, which may load, lay out or replace views, so a fresh
// sample is taken right after it.
void Correct(PlaceDriver &driver, const Sample &sample) {
	const auto list = driver.list.data();
	const auto scroll = list ? ChatListShowOwningScroll(list) : nullptr;
	if (!scroll) {
		return;
	}
	auto &reading = *driver.reading;
	const auto from = scroll->scrollTop();
	const auto to = TargetTop(reading.placement, sample);
	++reading.scrollRequests;
	reading.target = to;
	scroll->scrollToY(to);
	const auto after = TakeSample(driver);
	if (after.listAlive && (after.geometry.scrollTop != from)) {
		++reading.scrolls;
	}
	const auto now = crl::now();
	Observe(driver, after, now);
	driver.stableSince = now;
	Note(CorrectionText(reading, from, to, after));
}

void OnActionTimer(PlaceDriver &driver) {
	if (driver.decided || !base::take(driver.wantCorrection)) {
		return;
	}
	const auto placement = driver.reading->placement;
	const auto sample = TakeSample(driver);
	if (!Usable(placement, sample) || (sample.geometry != driver.geometry)) {
		return;
	} else if (!sample.placed && Reachable(placement, sample)) {
		Correct(driver, sample);
	}
}

// Names the last state when the deadline passed with no decision. The
// readings are asked once more on this sample first, so every refusal
// carries them.
void Classify(PlaceDriver &driver, const Sample &sample, crl::time now) {
	ReadAll(driver, sample);
	const auto &reading = *driver.reading;
	const auto placement = reading.placement;
	const auto on = (placement == Placement::OnScreen);
	const auto usable = Usable(placement, sample);
	const auto unsettled = u"at the deadline, on a layout that never read "
		u"the same for %1 ms: "_q.arg(NumberText(kSettle));
	if (on && !sample.viewsShown) {
		Decide(driver, Refusal::NoView, LostViewReason(reading));
	} else if (usable && on && !Fits(sample)) {
		Decide(
			driver,
			Refusal::CannotFit,
			unsettled + CannotFitReason(reading, sample));
	} else if (usable
		&& !on
		&& !sample.placed
		&& !Reachable(placement, sample)) {
		RefuseMoveOff(driver, sample, unsettled);
	} else if (!usable || !sample.placed || !reading.settled) {
		Decide(
			driver,
			Refusal::OutOfPlace,
			OutOfPlaceReason(driver, sample, now));
	} else {
		Decide(driver, Refusal::ReadingDisagrees, DisagreeReason(reading));
	}
}

// The stage's until: observes, flags what the timer should do, and
// decides. Product state is only read.
[[nodiscard]] bool ObservePlace(PlaceDriver &driver) {
	if (driver.decided) {
		return true;
	}
	const auto sample = TakeSample(driver);
	const auto now = crl::now();
	Observe(driver, sample, now);
	if (!sample.listAlive) {
		Decide(
			driver,
			Refusal::NoList,
			u"the list was destroyed while the placement waited"_q);
		return true;
	}
	auto &reading = *driver.reading;
	if (!sample.itemsAlive) {
		ReadAll(driver, sample);
		auto reason = u"message(s) %1 destroyed while the placement "
			u"waited - "_q.arg(MissingIds(reading, false))
			+ ItemsText(reading, false);
		Decide(driver, Refusal::NoTarget, std::move(reason));
		return true;
	}
	const auto placement = reading.placement;
	reading.settled = (now - driver.stableSince) >= kSettle;
	if (reading.settled && Usable(placement, sample)) {
		if ((placement == Placement::OnScreen) && !Fits(sample)) {
			ReadAll(driver, sample);
			Decide(
				driver,
				Refusal::CannotFit,
				CannotFitReason(reading, sample));
			return true;
		} else if (sample.placed) {
			// A posted product refresh may still be due: keep asking.
			if (AskReadings(driver, sample)) {
				Accept(driver, now);
				return true;
			}
		} else if ((placement != Placement::OnScreen)
			&& !Reachable(placement, sample)) {
			ReadAll(driver, sample);
			RefuseMoveOff(driver, sample, QString());
			return true;
		} else {
			driver.wantCorrection = true;
		}
	}
	if (now >= driver.deadline) {
		Classify(driver, sample, now);
		return true;
	}
	return false;
}

// The stage's run: resolves the target, refuses what it cannot place,
// reads each message's initial mark and makes the first scroll.
void StartPlace(const std::shared_ptr<PlaceDriver> &driver) {
	auto &reading = *driver->reading;
	driver->started = crl::now();
	driver->deadline = driver->started + reading.timeout;
	const auto target = driver->args.resolve();
	driver->items = target.items;
	for (const auto &id : target.items) {
		reading.items.push_back({ .id = id });
	}
	if (target.list) {
		reading.list = target.list;
		reading.kind = ChatListShowKindName(ChatListShowKindOf(target.list));
		reading.listIdentity = WidgetDescription(target.list);
	}
	const auto noList = NoListReason(target.list);
	if (!noList.isEmpty()) {
		Decide(
			*driver,
			Refusal::NoList,
			noList + u"; nothing was scrolled"_q);
		return;
	} else if (target.items.empty()) {
		Decide(
			*driver,
			Refusal::NoTarget,
			u"the resolver named no message; nothing was scrolled"_q);
		return;
	}
	driver->list = target.list;
	const auto sample = TakeSample(*driver);
	Observe(*driver, sample, driver->started);
	for (auto &item : reading.items) {
		item.initial = item.last;
	}
	if (!sample.itemsAlive) {
		ReadAll(*driver, sample);
		Decide(
			*driver,
			Refusal::NoTarget,
			u"message(s) %1 not in the list's session; nothing was "
			u"scrolled"_q.arg(MissingIds(reading, false)));
		return;
	} else if (!sample.viewsShown) {
		ReadAll(*driver, sample);
		Decide(
			*driver,
			Refusal::NoView,
			u"the %1 shows no view of its own for message(s) %2; nothing "
			u"was scrolled"_q.arg(reading.kind, MissingIds(reading, true)));
		return;
	}
	const auto raw = driver.get();
	driver->actionTimer.setCallback([raw] {
		OnActionTimer(*raw);
	});
	driver->actionTimer.callEach(kActionInterval);
	const auto placement = reading.placement;
	if (Usable(placement, sample)
		&& !sample.placed
		&& Reachable(placement, sample)) {
		Correct(*driver, sample);
	}
}

// Idempotent: a stay reads what the record holds up to here.
void EndRecord(PlaceDriver &driver, const QString &why) {
	auto &reading = *driver.reading;
	if (!reading.recording) {
		return;
	}
	reading.recording = false;
	reading.recordEnded = true;
	reading.recordEndedAt = crl::now();
	reading.recordEnd = why;
	driver.recordTimer.cancel();
	driver.recordLifetime.destroy();
}

// Whether a record mark differs from the last one in what the record
// keeps: alive, view shown, placed and the reading.
[[nodiscard]] bool Changed(
		const ChatListPlaceMark &was,
		const ChatListPlaceMark &now) {
	return (was.alive != now.alive)
		|| (was.viewShown != now.viewShown)
		|| (was.placed != now.placed)
		|| (was.visible != now.visible);
}

// One record sample of every message. It writes no row: records of earlier
// placements keep sampling while later stages scroll, so a row per change
// would be noise; a stay's row lists the changes inside its window.
void RecordSample(PlaceDriver &driver) {
	auto &reading = *driver.reading;
	if (!reading.recording) {
		return;
	}
	const auto sample = TakeSample(driver);
	if (!sample.listAlive) {
		EndRecord(driver, u"the list was destroyed"_q);
		return;
	}
	const auto now = crl::now();
	auto anyAlive = false;
	for (auto i = 0, count = int(reading.items.size()); i != count; ++i) {
		auto &item = reading.items[i];
		auto mark = MarkOf(sample, i, now);
		if (mark.alive) {
			AskReading(driver, sample, i, mark);
			anyAlive = true;
		} else {
			mark.details = u"message destroyed"_q;
		}
		if (Changed(item.last, mark)) {
			item.changes.push_back(mark);
		}
		item.last = std::move(mark);
	}
	++reading.samples;
	reading.maxGapMs = std::max(reading.maxGapMs, now - reading.lastSampleAt);
	reading.lastSampleAt = now;
	if (!anyAlive) {
		// A fixture's teardown ends the records it leaves behind.
		EndRecord(driver, u"every placed message was destroyed"_q);
	}
}

// Queues one record sample after a scroll signal, coalesced, so a sample
// never reads a list mid-update inside the product's own handler; the
// product posts its Runtime::Check() the same way.
void PostRecordSample(const std::weak_ptr<PlaceDriver> &weak) {
	const auto driver = weak.lock();
	if (!driver || driver->samplePosted) {
		return;
	}
	driver->samplePosted = true;
	crl::on_main([weak] {
		if (const auto strong = weak.lock()) {
			strong->samplePosted = false;
			RecordSample(*strong);
		}
	});
}

// Runs in the turn the until accepted; the accepting sample's marks are
// already the record's first. Without an owning scroll it subscribes to
// nothing, and the first sample of a dead list ends the record.
void StartRecord(const std::shared_ptr<PlaceDriver> &driver) {
	auto &reading = *driver->reading;
	reading.recording = true;
	reading.lastSampleAt = reading.placedAt;
	const auto raw = driver.get();
	driver->recordTimer.setCallback([raw] {
		RecordSample(*raw);
	});
	driver->recordTimer.callEach(kRecordInterval);
	const auto list = driver->list.data();
	const auto scroll = list ? ChatListShowOwningScroll(list) : nullptr;
	if (!scroll) {
		return;
	}
	const auto weak = std::weak_ptr<PlaceDriver>(driver);
	rpl::merge(
		scroll->scrolls(),
		scroll->innerResizes(),
		scroll->geometryChanged()
	) | rpl::on_next([=] {
		PostRecordSample(weak);
	}, driver->recordLifetime);
}

// The stage's then. An accepted placement starts its record before the
// row is written, so the row says it records.
void FinishPlace(const std::shared_ptr<PlaceDriver> &driver) {
	driver->actionTimer.cancel();
	auto &reading = *driver->reading;
	reading.finished = true;
	if (reading.refusal == Refusal::None) {
		StartRecord(driver);
	}
	Note(ChatListPlaceText(reading));
}

// Idempotent: Runner::onFinish runs it on every finish path, whether the
// stage finished or not.
void ReleasePlace(PlaceDriver &driver) {
	driver.actionTimer.cancel();
	EndRecord(driver, u"released at the run's finish"_q);
}

// One message's segment of the CHAT_LIST_PLACE row; changes= counts the
// record's changes after the accepted mark.
[[nodiscard]] QString ItemRowText(const ChatListPlaceItem &item) {
	const auto &last = item.last;
	const auto changes = std::max(int(item.changes.size()) - 1, 0);
	const auto fields = QStringList{
		IdText(item.id),
		u"alive=%1"_q.arg(FlagText(last.alive)),
		u"viewShown=%1"_q.arg(FlagText(last.viewShown)),
		u"element=%1"_q.arg(ElementText(last)),
		u"initialElement=%1"_q.arg(ElementText(item.initial)),
		u"placed=%1"_q.arg(FlagText(last.placed)),
		u"visible=%1"_q.arg(VisibleText(last)),
		u"readings=%1"_q.arg(NumberText(item.readings)),
		u"disagreements=%1"_q.arg(NumberText(item.disagreements)),
		u"changes=%1"_q.arg(NumberText(changes)),
		u"details={%1}"_q.arg(last.details),
	};
	return fields.join(u" "_q);
}

[[nodiscard]] bool ExpectedVisible(Placement placement) {
	return (placement == Placement::OnScreen);
}

// A record mark against the placement by geometry: the message destroyed
// or not where the placement asks.
[[nodiscard]] bool LeftPlacement(const ChatListPlaceMark &mark) {
	return !mark.alive || !mark.placed;
}

// A record mark against the placement by geometry or by its reading.
[[nodiscard]] bool Deviates(const ChatListPlaceMark &mark, bool expected) {
	return LeftPlacement(mark) || (mark.visible != expected);
}

// Whether |mark| decides before |than|: earlier, or on the same sample by
// geometry against a reading, since on one sample geometry wins.
[[nodiscard]] bool DecidesBefore(
		const ChatListPlaceMark &mark,
		const ChatListPlaceMark &than) {
	return (mark.at < than.at)
		|| ((mark.at == than.at)
			&& LeftPlacement(mark)
			&& !LeftPlacement(than));
}

// The earliest record mark in [from, till] against the placement, or 0.
// The record outlives the stay, so every read of it is bounded by |till|.
[[nodiscard]] crl::time FirstDeviation(
		const ChatListPlace &place,
		crl::time from,
		crl::time till) {
	const auto expected = ExpectedVisible(place.placement);
	auto result = crl::time(0);
	for (const auto &item : place.items) {
		for (const auto &mark : item.changes) {
			if (mark.at > till) {
				break;
			} else if ((mark.at >= from) && Deviates(mark, expected)) {
				if (!result || (mark.at < result)) {
					result = mark.at;
				}
				break;
			}
		}
	}
	return result;
}

// The stay's run: fixes the window, or refuses a placement that was not
// accepted.
void StartStay(StayDriver &driver) {
	auto &reading = *driver.reading;
	const auto &place = *driver.args.place;
	driver.started = crl::now();
	reading.placeName = place.name;
	reading.placement = place.placement;
	reading.hold = std::max(driver.args.hold, crl::time());
	if (!place.placed()) {
		reading.refusal = ChatListStayRefusal::NotPlaced;
		reading.reason = place.finished
			? u"the placement '%1' refused: %2 - %3"_q.arg(
				place.name,
				ChatListPlaceRefusalName(place.refusal),
				place.reason)
			: u"the placement stage '%1' did not finish (skipped or not "
				u"run)"_q.arg(place.name);
		return;
	}
	reading.from = place.placedAt;
	reading.windowEnd = std::max(driver.started, place.placedAt + reading.hold);
}

// The stay's until; only reads the record.
[[nodiscard]] bool ObserveStay(const StayDriver &driver) {
	const auto &reading = *driver.reading;
	if (reading.refusal == ChatListStayRefusal::NotPlaced) {
		return true;
	}
	const auto &place = *driver.args.place;
	return place.recordEnded
		|| (FirstDeviation(place, reading.from, place.lastSampleAt) != 0)
		|| (place.lastSampleAt >= reading.windowEnd);
}

// One message's record marks in [from, till], its changes after the
// accepted mark and its first deviation.
[[nodiscard]] ChatListStayItem JudgeItem(
		const ChatListPlaceItem &item,
		crl::time from,
		crl::time till,
		bool expected) {
	auto result = ChatListStayItem{
		.id = item.id,
		.last = item.last,
		.expectedVisible = expected,
	};
	for (auto i = 0, count = int(item.changes.size()); i != count; ++i) {
		const auto &mark = item.changes[i];
		if (mark.at > till) {
			break;
		} else if (mark.at < from) {
			continue;
		}
		result.window.push_back(mark);
		if (i > 0) {
			++result.changes;
		}
		if (!result.deviation.at && Deviates(mark, expected)) {
			result.deviation = mark;
		}
	}
	return result;
}

// One mark of a stay, its time relative to the window's start.
[[nodiscard]] QString MarkText(const ChatListPlaceMark &mark, crl::time from) {
	const auto fields = QStringList{
		u"sinceMs=%1"_q.arg(NumberText(mark.at - from)),
		u"alive=%1"_q.arg(FlagText(mark.alive)),
		u"viewShown=%1"_q.arg(FlagText(mark.viewShown)),
		u"placed=%1"_q.arg(FlagText(mark.placed)),
		u"visible=%1"_q.arg(VisibleText(mark)),
		u"element=%1"_q.arg(ElementText(mark)),
		u"viewport=%1"_q.arg(RectText(mark.viewport)),
		u"scrollTop=%1"_q.arg(NumberText(mark.scrollTop)),
		u"details={%1}"_q.arg(mark.details),
	};
	return fields.join(u" "_q);
}

// A window mark as <sinceMs>:<alive><viewShown><placed><visible>, with "-"
// for a reading that was not asked (a destroyed message).
[[nodiscard]] QString WindowMarkText(
		const ChatListPlaceMark &mark,
		crl::time from) {
	return u"%1:%2%3%4%5"_q.arg(
		NumberText(mark.at - from),
		FlagText(mark.alive),
		FlagText(mark.viewShown),
		FlagText(mark.placed),
		mark.asked ? FlagText(mark.visible) : u"-"_q);
}

[[nodiscard]] QString DeviationReason(
		const ChatListStay &reading,
		const ChatListStayItem &item) {
	const auto &mark = item.deviation;
	const auto placement = ChatListPlacementName(reading.placement);
	const auto what = !mark.alive
		? u"was destroyed"_q
		: !mark.placed
		? u"left the %1 placement"_q.arg(placement)
		: u"held the %1 placement, but its reading disagreed (expected "
			u"visible=%2)"_q.arg(placement, FlagText(item.expectedVisible));
	return u"message %1 %2 at +%3 ms of the window's %4 ms: "_q.arg(
		IdText(item.id),
		what,
		NumberText(mark.at - reading.from),
		NumberText(reading.windowEnd - reading.from))
		+ MarkText(mark, reading.from);
}

// A record that ended before it sampled the window's end proves nothing
// about the rest of the window.
[[nodiscard]] QString RecordEndReason(
		const ChatListStay &reading,
		const ChatListPlace &place) {
	return u"the record ended at +%1 ms, its last sample at +%2 ms, before "
		u"the window's end at +%3 ms: %4"_q.arg(
			NumberText(place.recordEndedAt - reading.from),
			NumberText(place.lastSampleAt - reading.from),
			NumberText(reading.windowEnd - reading.from),
			OrNone(place.recordEnd));
}

// The stay's judgement, in the turn its until accepted, over the record's
// marks up to the sample it decided on.
void JudgeStay(StayDriver &driver) {
	auto &reading = *driver.reading;
	const auto &place = *driver.args.place;
	reading.till = place.recordEnded
		? std::min(place.recordEndedAt, place.lastSampleAt)
		: place.lastSampleAt;
	reading.samples = place.samples;
	reading.maxGapMs = place.maxGapMs;
	const auto expected = ExpectedVisible(reading.placement);
	const auto sampledThrough = (reading.till >= reading.windowEnd);
	reading.items.clear();
	for (const auto &item : place.items) {
		auto stay = JudgeItem(item, reading.from, reading.till, expected);
		stay.stayed = !stay.deviation.at && sampledThrough;
		reading.items.push_back(std::move(stay));
	}
	auto decider = (const ChatListStayItem*)nullptr;
	for (const auto &item : reading.items) {
		if (!item.deviation.at) {
			continue;
		} else if (!decider
			|| DecidesBefore(item.deviation, decider->deviation)) {
			decider = &item;
		}
	}
	if (decider) {
		reading.refusal = LeftPlacement(decider->deviation)
			? ChatListStayRefusal::DidNotStay
			: ChatListStayRefusal::ReadingDisagrees;
		reading.reason = DeviationReason(reading, *decider);
	} else if (!sampledThrough) {
		reading.refusal = ChatListStayRefusal::DidNotStay;
		reading.reason = RecordEndReason(reading, place);
	}
}

// The stay's then. The record keeps running for later stays.
void FinishStay(StayDriver &driver) {
	auto &reading = *driver.reading;
	if (reading.refusal != ChatListStayRefusal::NotPlaced) {
		JudgeStay(driver);
	}
	reading.finished = true;
	Note(ChatListStayText(reading));
}

[[nodiscard]] QString WindowText(
		const ChatListStay &reading,
		const ChatListStayItem &item) {
	auto marks = QStringList();
	for (const auto &mark : item.window) {
		marks.push_back(WindowMarkText(mark, reading.from));
	}
	return u"[%1]"_q.arg(marks.join(u", "_q));
}

[[nodiscard]] QString DeviationText(
		const ChatListStay &reading,
		const ChatListStayItem &item) {
	return item.deviation.at
		? (u"{"_q + MarkText(item.deviation, reading.from) + u"}"_q)
		: u"none"_q;
}

// One message's segment of the CHAT_LIST_STAY row.
[[nodiscard]] QString StayItemText(
		const ChatListStay &reading,
		const ChatListStayItem &item) {
	const auto fields = QStringList{
		IdText(item.id),
		u"expected=%1"_q.arg(item.expectedVisible ? u"on"_q : u"off"_q),
		u"changes=%1"_q.arg(NumberText(item.changes)),
		u"stayed=%1"_q.arg(FlagText(item.stayed)),
		u"window=%1"_q.arg(WindowText(reading, item)),
		u"deviation=%1"_q.arg(DeviationText(reading, item)),
		u"last={%1}"_q.arg(MarkText(item.last, reading.from)),
	};
	return fields.join(u" "_q);
}

} // namespace

QString ChatListPlacementName(ChatListPlacement placement) {
	switch (placement) {
	case ChatListPlacement::OnScreen: return u"on"_q;
	case ChatListPlacement::OffAbove: return u"off-above"_q;
	case ChatListPlacement::OffBelow: return u"off-below"_q;
	}
	Unexpected("Placement in Test::ChatListPlacementName.");
}

QString ChatListPlaceRefusalName(ChatListPlaceRefusal refusal) {
	switch (refusal) {
	case ChatListPlaceRefusal::None: return u"none"_q;
	case ChatListPlaceRefusal::NoList: return u"no-list"_q;
	case ChatListPlaceRefusal::NoTarget: return u"no-target"_q;
	case ChatListPlaceRefusal::NoView: return u"no-view"_q;
	case ChatListPlaceRefusal::CannotFit: return u"cannot-fit"_q;
	case ChatListPlaceRefusal::CannotMoveOff: return u"cannot-move-off"_q;
	case ChatListPlaceRefusal::OutOfPlace: return u"out-of-place"_q;
	case ChatListPlaceRefusal::ReadingDisagrees:
		return u"reading-disagrees"_q;
	}
	Unexpected("Refusal in Test::ChatListPlaceRefusalName.");
}

QString ChatListStayRefusalName(ChatListStayRefusal refusal) {
	switch (refusal) {
	case ChatListStayRefusal::None: return u"none"_q;
	case ChatListStayRefusal::NotPlaced: return u"not-placed"_q;
	case ChatListStayRefusal::DidNotStay: return u"did-not-stay"_q;
	case ChatListStayRefusal::ReadingDisagrees:
		return u"reading-disagrees"_q;
	}
	Unexpected("Refusal in Test::ChatListStayRefusalName.");
}

QString ChatListPlaceText(const ChatListPlace &reading) {
	auto items = QStringList();
	for (const auto &item : reading.items) {
		items.push_back(ItemRowText(item));
	}
	// One substitution per field, so a caller's text that carries a %N
	// marker is never re-substituted by a later arg().
	const auto fields = QStringList{
		u"name=%1"_q.arg(reading.name),
		u"finished=%1"_q.arg(FlagText(reading.finished)),
		u"placed=%1"_q.arg(FlagText(reading.placed())),
		u"placement=%1"_q.arg(ChatListPlacementName(reading.placement)),
		u"kind=%1"_q.arg(OrNone(reading.kind)),
		u"list=%1"_q.arg(OrNone(reading.listIdentity)),
		u"viewport=%1"_q.arg(RectText(reading.viewport)),
		u"effective=%1"_q.arg(RectText(reading.effective)),
		u"band=%1"_q.arg(RectText(reading.band)),
		u"scrollTop=%1"_q.arg(NumberText(reading.scrollTop)),
		u"scrollTopMax=%1"_q.arg(NumberText(reading.scrollTopMax)),
		u"target=%1"_q.arg(NumberText(reading.target)),
		u"scrollRequests=%1"_q.arg(NumberText(reading.scrollRequests)),
		u"scrolls=%1"_q.arg(NumberText(reading.scrolls)),
		u"generation=%1"_q.arg(NumberText(reading.generation)),
		u"settled=%1"_q.arg(FlagText(reading.settled)),
		u"pending=%1"_q.arg(FlagText(reading.pending)),
		u"placedAt=%1"_q.arg(NumberText(reading.placedAt)),
		u"elapsedMs=%1"_q.arg(NumberText(reading.elapsedMs)),
		u"timeout=%1"_q.arg(NumberText(reading.timeout)),
		u"refusal=%1"_q.arg(ChatListPlaceRefusalName(reading.refusal)),
		u"samples=%1"_q.arg(NumberText(reading.samples)),
		u"recording=%1"_q.arg(FlagText(reading.recording)),
		u"recordEnd={%1}"_q.arg(reading.recordEnd),
		u"items=[%1]"_q.arg(items.join(u"; "_q)),
	};
	const auto line = u"CHAT_LIST_PLACE: "_q + fields.join(u" "_q);
	return reading.reason.isEmpty()
		? line
		: (line + u" - "_q + reading.reason);
}

std::shared_ptr<const ChatListPlace> AppendPlaceInChatList(
		not_null<Runner*> runner,
		ChatListPlaceArgs args) {
	Expects(args.resolve && args.visible);

	const auto driver = std::make_shared<PlaceDriver>();
	driver->args = std::move(args);
	driver->reading = std::make_shared<ChatListPlace>();
	auto &reading = *driver->reading;
	reading.name = driver->args.name;
	reading.placement = driver->args.placement;
	reading.timeout = std::max(driver->args.timeout, crl::time());
	runner->add({
		.name = u"place in chat list: %1"_q.arg(reading.name),
		.skipReason = driver->args.skipReason,
		.run = [=] {
			StartPlace(driver);
		},
		.until = [=] {
			return ObservePlace(*driver);
		},
		.then = [=] {
			FinishPlace(driver);
		},
		.timeout = reading.timeout + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return ChatListPlaceText(*driver->reading);
		},
	});
	runner->onFinish([=] {
		ReleasePlace(*driver);
	});
	return driver->reading;
}

QString ChatListStayText(const ChatListStay &reading) {
	auto items = QStringList();
	for (const auto &item : reading.items) {
		items.push_back(StayItemText(reading, item));
	}
	const auto windowMs = std::max(reading.till - reading.from, crl::time());
	const auto fields = QStringList{
		u"name=%1"_q.arg(reading.name),
		u"place=%1"_q.arg(reading.placeName),
		u"placement=%1"_q.arg(ChatListPlacementName(reading.placement)),
		u"finished=%1"_q.arg(FlagText(reading.finished)),
		u"stayed=%1"_q.arg(FlagText(reading.stayed())),
		u"from=%1"_q.arg(NumberText(reading.from)),
		u"till=%1"_q.arg(NumberText(reading.till)),
		u"windowMs=%1"_q.arg(NumberText(windowMs)),
		u"hold=%1"_q.arg(NumberText(reading.hold)),
		u"windowEnd=%1"_q.arg(NumberText(reading.windowEnd)),
		u"samples=%1"_q.arg(NumberText(reading.samples)),
		u"maxGapMs=%1"_q.arg(NumberText(reading.maxGapMs)),
		u"refusal=%1"_q.arg(ChatListStayRefusalName(reading.refusal)),
		u"items=[%1]"_q.arg(items.join(u"; "_q)),
	};
	const auto line = u"CHAT_LIST_STAY: "_q + fields.join(u" "_q);
	return reading.reason.isEmpty()
		? line
		: (line + u" - "_q + reading.reason);
}

std::shared_ptr<const ChatListStay> AppendStayedInChatList(
		not_null<Runner*> runner,
		ChatListStayArgs args) {
	Expects(args.place != nullptr);

	const auto driver = std::make_shared<StayDriver>();
	driver->args = std::move(args);
	driver->reading = std::make_shared<ChatListStay>();
	driver->reading->name = driver->args.name;
	const auto hold = std::max(driver->args.hold, crl::time());
	runner->add({
		.name = u"stayed in chat list: %1"_q.arg(driver->args.name),
		.skipReason = driver->args.skipReason,
		.run = [=] {
			StartStay(*driver);
		},
		.until = [=] {
			return ObserveStay(*driver);
		},
		.then = [=] {
			FinishStay(*driver);
		},
		.timeout = hold + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return ChatListStayText(*driver->reading);
		},
	});
	return driver->reading;
}

namespace {

// The self-test. Its fixture is test_widget_fixture's: a Test chat in its
// legacy HistoryInner, two local widget cards between two tall text
// fillers. Every judge re-reads on its own, through the history's blocks,
// HistoryInner::itemTop() and the owning scroll, never through the seam
// the helper samples.

// The test-only private-member accessor of test_chat_show.cpp, kept
// file-private here and read-only: Runtime::queryVisible() is the fresh
// reading the helper's consumers pass for widget cards, so the self-test
// proves the helper's geometry and that exact reading agree.
template <typename Tag, typename Tag::type Member>
struct PrivateMember {
	friend typename Tag::type Get(Tag) {
		return Member;
	}
};

struct RuntimeQueryVisibleTag {
	using type = bool (Tgw::Runtime::*)() const;
	friend type Get(RuntimeQueryVisibleTag);
};

template struct PrivateMember<
	RuntimeQueryVisibleTag,
	&Tgw::Runtime::queryVisible>;

// A held stay's least window: about fifteen record samples.
constexpr auto kHold = crl::time(1500);

// The accepted placements' deadline: a scroll and the settle need far less.
constexpr auto kPlaceTimeout = crl::time(10000);

// The refusal legs' deadline. The stale reading is refused only there, and
// the settled refusals must come well before it.
constexpr auto kControlTimeout = crl::time(3000);

// How long the excursion stays away and the flipped reading lasts before
// the next stage reads the record: three record samples.
constexpr auto kExcursion = crl::time(300);

// The cards' height anchor, in CSS px.
constexpr auto kCardHeight = 64;

// Lines past the visible area's height, so each filler is taller than it
// with both off margins.
constexpr auto kFillerExtraLines = 8;

// A held window counts only with this many record samples in it, the
// DiscriminatingScan rule: a "no change" needs samples through the window.
constexpr auto kMinSamples = 5;

constexpr auto kPrefix = "chat-list place self-test: ";

using Record = std::shared_ptr<WidgetFixtureMessage>;

enum class ReadingKind {
	Fresh,
	Snapshot,
	Flip,
};

struct PlaceLeg {
	QString name;
	Placement placement = Placement::OnScreen;
	std::vector<Record> messages;
	ReadingKind reading = ReadingKind::Fresh;
	Refusal expected = Refusal::None;
	crl::time timeout = kPlaceTimeout;
};

struct StayLeg {
	QString name;
	std::shared_ptr<const ChatListPlace> place;
	crl::time hold = 0;
	ChatListStayRefusal expected = ChatListStayRefusal::None;
};

// |snapshot| holds the cards' fresh readings taken off below, the stale
// reading; |stepStarted| the excursion's or the flip's start; |returnTop|
// the accepted scroll top the excursion returns to; |inverted| flips the
// flip reading.
struct PlaceSelfTest {
	std::shared_ptr<WidgetFixture> fixture;
	Record above;
	Record first;
	Record second;
	Record below;
	base::flat_map<FullMsgId, bool> snapshot;
	WidgetFixtureWait wait;
	QString gate;
	QString layout;
	crl::time stepStarted = 0;
	int returnTop = 0;
	int fillerLines = 0;
	bool inverted = false;
};

// One message as the judge reads it: its view's bounds in the HistoryInner
// by itemTop(), the owning scroll's own range (HistoryWidget's visibility
// range), and both fresh product readings.
struct Seen {
	QRect element;
	int scrollTop = 0;
	int scrollHeight = 0;
	bool view = false;
	bool wholly = false;
	bool above = false;
	bool below = false;
	bool fresh = false;
	bool listed = false;
};

struct CardsSeen {
	QString text;
	bool as = false;
};

[[nodiscard]] QString SelfText(const QString &text) {
	return QString::fromLatin1(kPrefix) + text;
}

[[nodiscard]] QString LegText(const QString &leg, const QString &text) {
	return SelfText(leg + u": "_q + text);
}

// One fixture-gate Check; the first failure gates the rest of the group.
void GateCheck(
		PlaceSelfTest &state,
		bool ok,
		const QString &what,
		const QString &details) {
	const auto full = u"fixture gate: "_q + SelfText(what);
	Check(ok, full, details);
	if (!ok && state.gate.isEmpty()) {
		state.gate = full + u" - "_q + details;
	}
}

[[nodiscard]] QString SelfTestGate(const PlaceSelfTest &state) {
	const auto fixture = WidgetFixtureGate(*state.fixture);
	return !fixture.isEmpty() ? fixture : state.gate;
}

// A placement's dependants do not apply when it was not accepted, so its
// judge is the one Check that fails.
[[nodiscard]] Fn<QString()> PlacedSkip(
		std::shared_ptr<const ChatListPlace> place) {
	return [=] {
		return place->placed()
			? QString()
			: !place->finished
			? u"the placement '%1' did not finish"_q.arg(place->name)
			: u"the placement '%1' refused: %2"_q.arg(
				place->name,
				ChatListPlaceRefusalName(place->refusal));
	};
}

[[nodiscard]] std::vector<Record> Records(const PlaceSelfTest &state) {
	return { state.above, state.first, state.second, state.below };
}

[[nodiscard]] QString NameOf(const PlaceSelfTest &state, FullMsgId id) {
	for (const auto &message : Records(state)) {
		if (message->id == id) {
			return message->name;
		}
	}
	return IdText(id);
}

[[nodiscard]] Ui::ElasticScroll *ListScroll(const PlaceSelfTest &state) {
	return FixtureListScroll(WidgetFixtureList(*state.fixture));
}

// The consumers' reading: a widget card's runtime answers
// Runtime::queryVisible(), any other message the list's own query.
[[nodiscard]] bool FreshVisible(
		not_null<HistoryItem*> item,
		const Element *view) {
	if (const auto runtime = view ? Tgw::FindRuntime(view) : nullptr) {
		return (runtime->*Get(RuntimeQueryVisibleTag()))();
	}
	return item->history()->owner().queryItemVisibility(item, view);
}

[[nodiscard]] QString FreshDetails(
		not_null<HistoryItem*> item,
		const Element *view) {
	const auto runtime = view ? Tgw::FindRuntime(view) : nullptr;
	return u"source=%1 state=%2 fresh=%3"_q.arg(
		runtime ? u"runtime"_q : u"item"_q,
		runtime ? RuntimeStateName(runtime->state()) : u"none"_q,
		FlagText(FreshVisible(item, view)));
}

// The stale reading, the cached value's failure under harness control: a
// value taken off screen and never refreshed. Absent reads false.
[[nodiscard]] bool SnapshotVisible(
		const PlaceSelfTest &state,
		not_null<HistoryItem*> item) {
	const auto i = state.snapshot.find(item->fullId());
	return (i != end(state.snapshot)) && i->second;
}

[[nodiscard]] QString SnapshotDetails(
		const PlaceSelfTest &state,
		not_null<HistoryItem*> item,
		const Element *view) {
	const auto i = state.snapshot.find(item->fullId());
	return u"snapshot=%1 fresh=%2"_q.arg(
		(i != end(state.snapshot)) ? FlagText(i->second) : u"none"_q,
		FlagText(FreshVisible(item, view)));
}

[[nodiscard]] QString FlipDetails(
		const PlaceSelfTest &state,
		not_null<HistoryItem*> item,
		const Element *view) {
	return u"inverted=%1 fresh=%2"_q.arg(
		FlagText(state.inverted),
		FlagText(FreshVisible(item, view)));
}

void SetReading(
		ChatListPlaceArgs &args,
		const std::shared_ptr<PlaceSelfTest> &state,
		ReadingKind kind) {
	switch (kind) {
	case ReadingKind::Fresh:
		args.visible = FreshVisible;
		args.visibleDetails = FreshDetails;
		return;
	case ReadingKind::Snapshot:
		args.visible = [=](not_null<HistoryItem*> item, const Element *) {
			return SnapshotVisible(*state, item);
		};
		args.visibleDetails = [=](
				not_null<HistoryItem*> item,
				const Element *view) {
			return SnapshotDetails(*state, item, view);
		};
		return;
	case ReadingKind::Flip:
		args.visible = [=](
				not_null<HistoryItem*> item,
				const Element *view) {
			return FreshVisible(item, view) != state->inverted;
		};
		args.visibleDetails = [=](
				not_null<HistoryItem*> item,
				const Element *view) {
			return FlipDetails(*state, item, view);
		};
		return;
	}
	Unexpected("Reading in Test::SetReading.");
}

[[nodiscard]] Seen ReadSeen(const PlaceSelfTest &state, FullMsgId id) {
	auto result = Seen();
	const auto &fixture = *state.fixture;
	const auto list = WidgetFixtureList(fixture);
	const auto inner = dynamic_cast<HistoryInner*>(list);
	const auto scroll = FixtureListScroll(list);
	const auto item = WidgetFixtureItem(fixture, id);
	const auto view = FixtureOwnView(fixture, id);
	if (scroll) {
		result.scrollTop = scroll->scrollTop();
		result.scrollHeight = scroll->height();
	}
	if (!inner || !scroll || !item || !view) {
		return result;
	}
	const auto top = inner->itemTop(view);
	const auto height = view->height();
	const auto bottom = result.scrollTop + result.scrollHeight;
	result.view = true;
	result.element = QRect(0, top, view->width(), height);
	result.wholly = (top >= result.scrollTop) && (top + height <= bottom);
	result.above = (top + height <= result.scrollTop);
	result.below = (top >= bottom);
	result.fresh = FreshVisible(item, view);
	result.listed = item->history()->owner().queryItemVisibility(item, view);
	return result;
}

[[nodiscard]] QString SeenText(const QString &name, const Seen &seen) {
	const auto fields = QStringList{
		u"{%1}"_q.arg(name),
		u"view=%1"_q.arg(FlagText(seen.view)),
		u"element=%1"_q.arg(seen.view ? RectText(seen.element) : u"none"_q),
		u"scrollTop=%1"_q.arg(NumberText(seen.scrollTop)),
		u"scrollHeight=%1"_q.arg(NumberText(seen.scrollHeight)),
		u"wholly=%1"_q.arg(FlagText(seen.wholly)),
		u"above=%1"_q.arg(FlagText(seen.above)),
		u"below=%1"_q.arg(FlagText(seen.below)),
		u"fresh=%1"_q.arg(FlagText(seen.fresh)),
		u"listed=%1"_q.arg(FlagText(seen.listed)),
	};
	return fields.join(u" "_q);
}

// The judge's own placement rule, with both fresh readings agreeing.
[[nodiscard]] bool SeenAs(const Seen &seen, Placement placement) {
	if (!seen.view) {
		return false;
	}
	switch (placement) {
	case Placement::OnScreen:
		return seen.wholly && seen.fresh && seen.listed;
	case Placement::OffAbove:
		return seen.above && !seen.fresh && !seen.listed;
	case Placement::OffBelow:
		return seen.below && !seen.fresh && !seen.listed;
	}
	Unexpected("Placement in Test::SeenAs.");
}

[[nodiscard]] CardsSeen ReadCards(
		const PlaceSelfTest &state,
		Placement placement) {
	auto result = CardsSeen{ .as = true };
	auto texts = QStringList();
	for (const auto &card : { state.first, state.second }) {
		const auto seen = ReadSeen(state, card->id);
		result.as = result.as && SeenAs(seen, placement);
		texts.push_back(SeenText(card->name, seen));
	}
	result.text = texts.join(u"; "_q);
	return result;
}

void CheckSeen(
		const QString &leg,
		const QString &name,
		const Seen &seen,
		Placement placement) {
	Check(
		SeenAs(seen, placement),
		LegText(leg, u"%1 is %2 by the judge's own geometry, both fresh "
			u"readings agreeing"_q.arg(
				name,
				ChatListPlacementName(placement))),
		SeenText(name, seen));
}

[[nodiscard]] QByteArray CardHtml(const QString &name) {
	return u"<!doctype html><html><body style=\"margin:0;"
		u"background:#3a7bd5;color:#fff;font:14px sans-serif\">"
		u"chat-list place %1</body></html>"_q.arg(name).toUtf8();
}

// Short numbered lines, so none wraps and the height is the line count's.
[[nodiscard]] QString FillerText(int lines) {
	auto result = QStringList();
	for (auto i = 1; i <= lines; ++i) {
		result.push_back(u"chat-list place filler %1"_q.arg(NumberText(i)));
	}
	return result.join(QChar('\n'));
}

// Top to bottom: the above filler, the two cards, the below filler. The
// chat is at its bottom, so the list keeps showing the below filler and
// both cards start above the visible area.
void InjectFixtures(PlaceSelfTest &state) {
	StartFixtureWait(state.wait);
	state.layout = QString();
	auto &fixture = *state.fixture;
	const auto scroll = ListScroll(state);
	if (!scroll) {
		GateCheck(
			state,
			false,
			u"inject the fixtures"_q,
			u"the chat's list has no owning Ui::ElasticScroll"_q);
		return;
	}
	const auto line = std::max(st::messageTextStyle.font->height, 1);
	state.fillerLines = (scroll->height() / line) + kFillerExtraLines;
	const auto text = FillerText(state.fillerLines);
	const auto order = std::array{
		std::pair{ state.above, QByteArray() },
		std::pair{ state.first, CardHtml(u"first"_q) },
		std::pair{ state.second, CardHtml(u"second"_q) },
		std::pair{ state.below, QByteArray() },
	};
	for (const auto &[message, html] : order) {
		const auto refusal = html.isEmpty()
			? InjectTextMessage(fixture, *message, text)
			: InjectWidgetMessage(
				fixture,
				*message,
				html,
				0, // width
				kCardHeight);
		if (!refusal.isEmpty()) {
			GateCheck(
				state,
				false,
				u"inject the fixtures"_q,
				message->name + u": "_q + refusal);
			return;
		}
	}
}

// The four views' tops and heights, the scroll's top and height and the
// pending layout: the sample the injection settles on.
[[nodiscard]] QString LayoutText(const PlaceSelfTest &state) {
	const auto &fixture = *state.fixture;
	const auto list = WidgetFixtureList(fixture);
	const auto inner = dynamic_cast<HistoryInner*>(list);
	const auto scroll = FixtureListScroll(list);
	const auto history = WidgetFixtureHistory(fixture);
	auto fields = QStringList();
	for (const auto &message : Records(state)) {
		const auto view = FixtureOwnView(fixture, message->id);
		fields.push_back(u"{%1}=%2"_q.arg(
			message->name,
			(inner && view)
				? u"%1+%2%3"_q.arg(
					NumberText(inner->itemTop(view)),
					NumberText(view->height()),
					view->pendingResize() ? u"*"_q : QString())
				: u"none"_q));
	}
	fields.push_back(u"scrollTop=%1"_q.arg(
		scroll ? NumberText(scroll->scrollTop()) : u"none"_q));
	fields.push_back(u"scrollHeight=%1"_q.arg(
		scroll ? NumberText(scroll->height()) : u"none"_q));
	fields.push_back(u"historyPending=%1"_q.arg(
		FlagText(!history || history->hasPendingResizedItems())));
	return fields.join(u" "_q);
}

[[nodiscard]] bool FixtureLaidOut(const PlaceSelfTest &state) {
	const auto &fixture = *state.fixture;
	const auto history = WidgetFixtureHistory(fixture);
	if (!history || history->hasPendingResizedItems()) {
		return false;
	}
	return ranges::all_of(Records(state), [&](const Record &message) {
		const auto view = FixtureOwnView(fixture, message->id);
		return view && !view->pendingResize() && (view->height() > 0);
	});
}

// The injection's until: both cards laid out, all four views laid out and
// an unchanged layout for the settle time, or the layout bound.
[[nodiscard]] bool InjectSettled(PlaceSelfTest &state) {
	if (!state.gate.isEmpty()) {
		return true;
	}
	auto &fixture = *state.fixture;
	const auto first = WidgetMessageLaidOut(fixture, *state.first);
	const auto second = WidgetMessageLaidOut(fixture, *state.second);
	const auto layout = LayoutText(state);
	const auto now = crl::now();
	if (layout != state.layout) {
		state.layout = layout;
		state.wait.stableSince = now;
	}
	state.wait.details = layout;
	const auto settled = (now - state.wait.stableSince) >= kSettle;
	return (first && second && FixtureLaidOut(state) && settled)
		|| FixtureBoundReached(state.wait, kWidgetLayoutBound);
}

void CheckInjected(PlaceSelfTest &state) {
	if (!state.gate.isEmpty()) {
		return;
	}
	auto &fixture = *state.fixture;
	for (const auto &card : { state.first, state.second }) {
		const auto refusal = CheckWidgetMessage(fixture, *card);
		GateCheck(
			state,
			refusal.isEmpty(),
			u"the %1 is a laid-out local widget card"_q.arg(card->name),
			refusal.isEmpty() ? card->shape : refusal);
	}
	GateCheck(
		state,
		FixtureLaidOut(state),
		u"all four fixture views are laid out"_q,
		state.layout);
	const auto scroll = ListScroll(state);
	const auto height = scroll ? scroll->height() : 0;
	for (const auto &filler : { state.above, state.below }) {
		const auto seen = ReadSeen(state, filler->id);
		GateCheck(
			state,
			seen.view && (seen.element.height() > height + 2 * kOffMargin),
			u"the %1 is taller than the visible area and both off "
			u"margins"_q.arg(filler->name),
			u"height=%1 scrollHeight=%2 lines=%3"_q.arg(
				NumberText(seen.element.height()),
				NumberText(height),
				NumberText(state.fillerLines)));
	}
	const auto first = ReadSeen(state, state.first->id);
	const auto second = ReadSeen(state, state.second->id);
	const auto band = first.element.united(second.element);
	GateCheck(
		state,
		first.view
			&& second.view
			&& (band.height() <= height - 2 * kOffMargin),
		u"the two cards fit the visible area together, with both off "
		u"margins"_q,
		u"band=%1 scrollHeight=%2"_q.arg(
			RectText(band),
			NumberText(height)));
	Note(u"CHAT_LIST_PLACE_SELFTEST: leg={inject} fillerLines=%1 "
		u"lineHeight=%2 layout=%3"_q.arg(
			NumberText(state.fillerLines),
			NumberText(st::messageTextStyle.font->height),
			state.layout));
}

void AppendInject(
		not_null<Runner*> runner,
		const std::shared_ptr<PlaceSelfTest> &state) {
	runner->add({
		.name = SelfText(u"inject the fixtures"_q),
		.run = [=] {
			InjectFixtures(*state);
		},
		.until = [=] {
			return InjectSettled(*state);
		},
		.then = [=] {
			CheckInjected(*state);
		},
		.timeout = kWidgetLayoutBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return state->wait.details;
		},
	});
}

void SelfRow(
		const QString &leg,
		const QString &type,
		const QString &expected,
		const QStringList &seen,
		const QString &reading) {
	const auto fields = QStringList{
		u"leg={%1}"_q.arg(leg),
		u"type=%1"_q.arg(type),
		u"expected=%1"_q.arg(expected),
		u"seen=[%1]"_q.arg(seen.join(u"; "_q)),
	};
	Note(u"CHAT_LIST_PLACE_SELFTEST: "_q
		+ fields.join(u" "_q)
		+ u" | "_q
		+ reading);
}

[[nodiscard]] QString PlaceRefusalText(const ChatListPlace &place) {
	return u"refusal=%1"_q.arg(ChatListPlaceRefusalName(place.refusal))
		+ (place.reason.isEmpty() ? QString() : (u" - "_q + place.reason));
}

void JudgeAccepted(
		const PlaceLeg &leg,
		const ChatListPlace &place,
		const std::vector<Seen> &seen) {
	Check(
		place.placed(),
		LegText(leg.name, u"accepted %1"_q.arg(
			ChatListPlacementName(leg.placement))),
		PlaceRefusalText(place));
	auto initial = QStringList();
	auto started = true;
	for (const auto &item : place.items) {
		started = started && !item.initial.placed;
		initial.push_back(u"%1=%2"_q.arg(
			IdText(item.id),
			FlagText(item.initial.placed)));
	}
	Check(
		started && (place.scrollRequests >= 1) && (place.scrolls >= 1),
		LegText(leg.name, u"every message started out of place and a "
			u"scroll moved the list"_q),
		u"initialPlaced=[%1] scrollRequests=%2 scrolls=%3"_q.arg(
			initial.join(u", "_q),
			NumberText(place.scrollRequests),
			NumberText(place.scrolls)));
	for (auto i = 0, count = int(seen.size()); i != count; ++i) {
		CheckSeen(leg.name, leg.messages[i]->name, seen[i], leg.placement);
	}
}

// Placed and settled, yet only the stale reading disagreed: the product's
// fresh readings agree with the geometry.
void JudgeStale(
		const PlaceLeg &leg,
		const ChatListPlace &place,
		const std::vector<Seen> &seen) {
	const auto expected = ExpectedVisible(leg.placement);
	const auto against = ranges::all_of(
		place.items,
		[&](const ChatListPlaceItem &item) {
			const auto &last = item.last;
			return last.placed && last.asked && (last.visible != expected);
		});
	Check(
		(place.elapsedMs >= place.timeout) && against,
		LegText(leg.name, u"decided at the deadline, every message placed "
			u"and read against the placement"_q),
		u"elapsedMs=%1 timeout=%2 - %3"_q.arg(
			NumberText(place.elapsedMs),
			NumberText(place.timeout),
			ItemsText(place, false)));
	for (auto i = 0, count = int(seen.size()); i != count; ++i) {
		CheckSeen(leg.name, leg.messages[i]->name, seen[i], leg.placement);
	}
}

void JudgeRefused(
		const PlaceLeg &leg,
		const ChatListPlace &place,
		const std::vector<Seen> &seen) {
	Check(
		place.finished && (place.refusal == leg.expected),
		LegText(leg.name, u"refused by name: %1"_q.arg(
			ChatListPlaceRefusalName(leg.expected))),
		PlaceRefusalText(place));
	const auto carried = ranges::all_of(
		place.items,
		[&](const ChatListPlaceItem &item) {
			const auto &last = item.last;
			return last.asked
				&& last.viewShown
				&& !last.element.isEmpty()
				&& place.reason.contains(ItemText(item));
		});
	Check(
		carried,
		LegText(leg.name, u"the reason carries every message's last "
			u"geometry and reading"_q),
		ItemsText(place, false));
	const auto early = (place.scrollRequests == 0)
		&& (place.elapsedMs < place.timeout);
	const auto counts = u"scrollRequests=%1 elapsedMs=%2 timeout=%3"_q.arg(
		NumberText(place.scrollRequests),
		NumberText(place.elapsedMs),
		NumberText(place.timeout));
	if (leg.expected == Refusal::CannotFit) {
		Check(
			early && (place.band.height() > place.effective.height()),
			LegText(leg.name, u"decided on settled geometry, before the "
				u"deadline and without a scroll: the band is taller than "
				u"the visible area"_q),
			counts + u" band=%1 effective=%2"_q.arg(
				RectText(place.band),
				RectText(place.effective)));
	} else if (leg.expected == Refusal::CannotMoveOff) {
		Check(
			early && (place.target > place.scrollTopMax),
			LegText(leg.name, u"decided on settled geometry, before the "
				u"deadline and without a scroll: the target is past the "
				u"scroll's range"_q),
			counts + u" target=%1 scrollTopMax=%2"_q.arg(
				NumberText(place.target),
				NumberText(place.scrollTopMax)));
	} else if (leg.expected == Refusal::ReadingDisagrees) {
		JudgeStale(leg, place, seen);
	}
}

void JudgePlaceLeg(
		const PlaceSelfTest &state,
		const PlaceLeg &leg,
		const ChatListPlace &place) {
	auto seen = std::vector<Seen>();
	auto texts = QStringList();
	for (const auto &message : leg.messages) {
		seen.push_back(ReadSeen(state, message->id));
		texts.push_back(SeenText(message->name, seen.back()));
	}
	if (leg.expected == Refusal::None) {
		JudgeAccepted(leg, place, seen);
	} else {
		JudgeRefused(leg, place, seen);
	}
	SelfRow(
		leg.name,
		u"place"_q,
		ChatListPlaceRefusalName(leg.expected),
		texts,
		ChatListPlaceText(place));
}

// The placement stage and its judge in the very next stage, nothing
// scrolled between them.
std::shared_ptr<const ChatListPlace> AppendPlaceLeg(
		not_null<Runner*> runner,
		const std::shared_ptr<PlaceSelfTest> &state,
		PlaceLeg leg) {
	const auto messages = leg.messages;
	auto args = ChatListPlaceArgs{
		.name = SelfText(leg.name),
		.placement = leg.placement,
		.resolve = [=] {
			auto result = ChatListPlaceTarget{
				.list = WidgetFixtureList(*state->fixture),
			};
			for (const auto &message : messages) {
				result.items.push_back(message->id);
			}
			return result;
		},
		.timeout = leg.timeout,
	};
	SetReading(args, state, leg.reading);
	const auto place = AppendPlaceInChatList(runner, std::move(args));
	runner->add({
		.name = SelfText(leg.name + u" - judge"_q),
		.then = [=] {
			JudgePlaceLeg(*state, leg, *place);
		},
	});
	return place;
}

[[nodiscard]] QString StayRefusalText(const ChatListStay &stay) {
	return u"refusal=%1"_q.arg(ChatListStayRefusalName(stay.refusal))
		+ (stay.reason.isEmpty() ? QString() : (u" - "_q + stay.reason));
}

void JudgeHeld(
		const PlaceSelfTest &state,
		const StayLeg &leg,
		const ChatListStay &stay,
		const std::vector<Seen> &seen) {
	Check(
		stay.stayed(),
		LegText(leg.name, u"stayed %1 for the window"_q.arg(
			ChatListPlacementName(stay.placement))),
		StayRefusalText(stay));
	const auto windowMs = stay.till - stay.from;
	Check(
		(windowMs >= leg.hold) && (stay.samples >= kMinSamples),
		LegText(leg.name, u"the record sampled through a window as long "
			u"as the hold, with at least %1 samples"_q.arg(
				NumberText(kMinSamples))),
		u"windowMs=%1 hold=%2 samples=%3 maxGapMs=%4"_q.arg(
			NumberText(windowMs),
			NumberText(leg.hold),
			NumberText(stay.samples),
			NumberText(stay.maxGapMs)));
	for (auto i = 0, count = int(stay.items.size()); i != count; ++i) {
		const auto &item = stay.items[i];
		const auto name = NameOf(state, item.id);
		Check(
			item.stayed && !item.changes && !item.deviation.at,
			LegText(leg.name, u"%1 read %2 from the window's start to its "
				u"end"_q.arg(name, item.expectedVisible
					? u"on"_q
					: u"off"_q)),
			u"changes=%1 window=%2 deviation=%3"_q.arg(
				NumberText(item.changes),
				WindowText(stay, item),
				DeviationText(stay, item)));
		CheckSeen(leg.name, name, seen[i], stay.placement);
	}
}

// Both ends placed, so only continuity can refuse: an ends-only record
// would pass here.
void JudgeExcursion(
		const PlaceSelfTest &state,
		const StayLeg &leg,
		const ChatListStay &stay,
		const std::vector<Seen> &seen) {
	Check(
		stay.finished && (stay.refusal == ChatListStayRefusal::DidNotStay),
		LegText(leg.name, u"refused by name: did-not-stay"_q),
		StayRefusalText(stay));
	auto decider = (const ChatListStayItem*)nullptr;
	for (auto i = 0, count = int(stay.items.size()); i != count; ++i) {
		const auto &item = stay.items[i];
		const auto &deviation = item.deviation;
		const auto name = NameOf(state, item.id);
		const auto inside = (deviation.at > stay.from)
			&& (deviation.at < stay.till)
			&& (deviation.at >= state.stepStarted)
			&& !deviation.placed;
		Check(
			inside && item.last.placed,
			LegText(leg.name, u"%1 left the placement inside the window, "
				u"after the excursion began, and was placed at its "
				u"end"_q.arg(name)),
			u"excursionMs=%1 tillMs=%2 lastPlaced=%3 deviation=%4"_q.arg(
				NumberText(state.stepStarted - stay.from),
				NumberText(stay.till - stay.from),
				FlagText(item.last.placed),
				DeviationText(stay, item)));
		CheckSeen(leg.name, name, seen[i], Placement::OnScreen);
		if (deviation.at
			&& (!decider || (deviation.at < decider->deviation.at))) {
			decider = &item;
		}
	}
	const auto named = decider
		&& stay.reason.contains(IdText(decider->id))
		&& stay.reason.contains(
			u"element=%1"_q.arg(ElementText(decider->deviation)))
		&& stay.reason.contains(
			u"visible=%1"_q.arg(VisibleText(decider->deviation)));
	Check(
		named,
		LegText(leg.name, u"the reason names the deviating message's "
			u"element and reading"_q),
		stay.reason);
}

// The geometry held through the window and only the reading turned, after
// the flip.
void JudgeFlip(
		const PlaceSelfTest &state,
		const StayLeg &leg,
		const ChatListStay &stay,
		const std::vector<Seen> &seen) {
	Check(
		stay.finished
			&& (stay.refusal == ChatListStayRefusal::ReadingDisagrees),
		LegText(leg.name, u"refused by name: reading-disagrees"_q),
		StayRefusalText(stay));
	auto held = true;
	auto windows = QStringList();
	for (const auto &item : stay.items) {
		for (const auto &mark : item.window) {
			held = held && mark.alive && mark.placed;
		}
		windows.push_back(NameOf(state, item.id) + u": "_q
			+ WindowText(stay, item));
	}
	Check(
		held,
		LegText(leg.name, u"no mark inside the window left the "
			u"placement"_q),
		windows.join(u"; "_q));
	for (auto i = 0, count = int(stay.items.size()); i != count; ++i) {
		const auto &item = stay.items[i];
		const auto &deviation = item.deviation;
		const auto name = NameOf(state, item.id);
		const auto flipped = deviation.at
			&& (deviation.at >= state.stepStarted)
			&& deviation.placed
			&& deviation.asked
			&& (deviation.visible != item.expectedVisible);
		Check(
			flipped,
			LegText(leg.name, u"%1's first deviation is its reading, at or "
				u"after the flip, with the geometry held"_q.arg(name)),
			u"flipMs=%1 deviation=%2"_q.arg(
				NumberText(state.stepStarted - stay.from),
				DeviationText(stay, item)));
		CheckSeen(leg.name, name, seen[i], stay.placement);
	}
}

void JudgeStayLeg(
		const PlaceSelfTest &state,
		const StayLeg &leg,
		const ChatListStay &stay) {
	auto seen = std::vector<Seen>();
	auto texts = QStringList();
	for (const auto &item : stay.items) {
		seen.push_back(ReadSeen(state, item.id));
		texts.push_back(SeenText(NameOf(state, item.id), seen.back()));
	}
	if (leg.expected == ChatListStayRefusal::None) {
		JudgeHeld(state, leg, stay, seen);
	} else if (leg.expected == ChatListStayRefusal::DidNotStay) {
		JudgeExcursion(state, leg, stay, seen);
	} else if (leg.expected == ChatListStayRefusal::ReadingDisagrees) {
		JudgeFlip(state, leg, stay, seen);
	}
	SelfRow(
		leg.name,
		u"stay"_q,
		ChatListStayRefusalName(leg.expected),
		texts,
		ChatListStayText(stay));
}

void AppendStayLeg(
		not_null<Runner*> runner,
		const std::shared_ptr<PlaceSelfTest> &state,
		StayLeg leg) {
	const auto skip = PlacedSkip(leg.place);
	const auto stay = AppendStayedInChatList(runner, {
		.name = SelfText(leg.name),
		.place = leg.place,
		.hold = leg.hold,
		.skipReason = skip,
	});
	runner->add({
		.name = SelfText(leg.name + u" - judge"_q),
		.skipReason = skip,
		.then = [=] {
			JudgeStayLeg(*state, leg, *stay);
		},
	});
}

void TakeSnapshot(PlaceSelfTest &state) {
	const auto &fixture = *state.fixture;
	for (const auto &card : { state.first, state.second }) {
		if (const auto item = WidgetFixtureItem(fixture, card->id)) {
			state.snapshot[card->id] = FreshVisible(
				item,
				FixtureOwnView(fixture, card->id));
		}
	}
}

void CheckSnapshot(PlaceSelfTest &state) {
	auto ok = true;
	auto texts = QStringList();
	for (const auto &card : { state.first, state.second }) {
		const auto i = state.snapshot.find(card->id);
		const auto known = (i != end(state.snapshot));
		ok = ok && known && !i->second;
		texts.push_back(u"{%1}=%2"_q.arg(
			card->name,
			known ? FlagText(i->second) : u"none"_q));
	}
	GateCheck(
		state,
		ok,
		u"both cards' fresh readings, taken off below for the stale "
		u"reading, are false"_q,
		texts.join(u" "_q));
}

void AppendSnapshot(
		not_null<Runner*> runner,
		const std::shared_ptr<PlaceSelfTest> &state) {
	runner->add({
		.name = SelfText(u"snapshot the cards' fresh readings"_q),
		.run = [=] {
			TakeSnapshot(*state);
		},
		.then = [=] {
			CheckSnapshot(*state);
		},
	});
}

// Away to the list's bottom, which puts both cards wholly above the
// visible area, from the accepted scroll top it returns to.
void StartExcursion(PlaceSelfTest &state) {
	state.stepStarted = crl::now();
	StartFixtureWait(state.wait);
	if (const auto scroll = ListScroll(state)) {
		state.returnTop = scroll->scrollTop();
		scroll->scrollToY(scroll->scrollTopMax());
	}
}

void EndExcursion(PlaceSelfTest &state) {
	const auto cards = ReadCards(state, Placement::OffAbove);
	GateCheck(
		state,
		cards.as,
		u"the excursion put both cards wholly above the visible area"_q,
		cards.text);
	if (const auto scroll = ListScroll(state)) {
		scroll->scrollToY(state.returnTop);
	}
}

[[nodiscard]] bool BackOn(const PlaceSelfTest &state) {
	return (FixtureBoundReached(state.wait, kExcursion)
			&& ReadCards(state, Placement::OnScreen).as)
		|| FixtureBoundReached(state.wait, kControlTimeout);
}

void CheckBackOn(PlaceSelfTest &state) {
	const auto cards = ReadCards(state, Placement::OnScreen);
	const auto scroll = ListScroll(state);
	const auto top = scroll ? scroll->scrollTop() : -1;
	GateCheck(
		state,
		cards.as && (top == state.returnTop),
		u"both cards are back wholly on screen at the accepted scroll "
		u"top"_q,
		u"returnTop=%1 scrollTop=%2 - %3"_q.arg(
			NumberText(state.returnTop),
			NumberText(top),
			cards.text));
}

void AppendExcursion(
		not_null<Runner*> runner,
		const std::shared_ptr<PlaceSelfTest> &state,
		std::shared_ptr<const ChatListPlace> place) {
	const auto skip = PlacedSkip(place);
	runner->add({
		.name = SelfText(u"move the list away"_q),
		.skipReason = skip,
		.run = [=] {
			StartExcursion(*state);
		},
		.until = [=] {
			return FixtureBoundReached(state->wait, kExcursion);
		},
		.then = [=] {
			EndExcursion(*state);
		},
	});
	runner->add({
		.name = SelfText(u"back on the placement"_q),
		.skipReason = skip,
		.run = [=] {
			StartFixtureWait(state->wait);
		},
		.until = [=] {
			return BackOn(*state);
		},
		.then = [=] {
			CheckBackOn(*state);
		},
		.timeout = kControlTimeout + kDefaultStageTimeout,
	});
}

void AppendFlip(
		not_null<Runner*> runner,
		const std::shared_ptr<PlaceSelfTest> &state,
		std::shared_ptr<const ChatListPlace> place) {
	runner->add({
		.name = SelfText(u"flip the reading"_q),
		.skipReason = PlacedSkip(std::move(place)),
		.run = [=] {
			state->stepStarted = crl::now();
			state->inverted = true;
			StartFixtureWait(state->wait);
		},
		.until = [=] {
			return FixtureBoundReached(state->wait, kExcursion);
		},
	});
}

// The excursion and the flip run on records that just passed a held stay,
// so one record passes and then refuses.
void AppendLegs(
		not_null<Runner*> runner,
		const std::shared_ptr<PlaceSelfTest> &state) {
	const auto cards = std::vector{ state->first, state->second };
	AppendInject(runner, state);
	const auto on = AppendPlaceLeg(runner, state, {
		.name = u"on together"_q,
		.placement = Placement::OnScreen,
		.messages = cards,
	});
	AppendStayLeg(runner, state, {
		.name = u"stayed on"_q,
		.place = on,
		.hold = kHold,
	});
	AppendExcursion(runner, state, on);
	AppendStayLeg(runner, state, {
		.name = u"moved away and back"_q,
		.place = on,
		.expected = ChatListStayRefusal::DidNotStay,
	});
	const auto offBelow = AppendPlaceLeg(runner, state, {
		.name = u"off below"_q,
		.placement = Placement::OffBelow,
		.messages = cards,
	});
	AppendStayLeg(runner, state, {
		.name = u"stayed off below"_q,
		.place = offBelow,
		.hold = kHold,
	});
	AppendSnapshot(runner, state);
	AppendPlaceLeg(runner, state, {
		.name = u"stale reading"_q,
		.placement = Placement::OnScreen,
		.messages = cards,
		.reading = ReadingKind::Snapshot,
		.expected = Refusal::ReadingDisagrees,
		.timeout = kControlTimeout,
	});
	const auto offAbove = AppendPlaceLeg(runner, state, {
		.name = u"off above"_q,
		.placement = Placement::OffAbove,
		.messages = cards,
		.reading = ReadingKind::Flip,
	});
	AppendStayLeg(runner, state, {
		.name = u"stayed off above"_q,
		.place = offAbove,
		.hold = kHold,
	});
	AppendFlip(runner, state, offAbove);
	AppendStayLeg(runner, state, {
		.name = u"reading flipped"_q,
		.place = offAbove,
		.expected = ChatListStayRefusal::ReadingDisagrees,
	});
	AppendPlaceLeg(runner, state, {
		.name = u"cannot fit"_q,
		.placement = Placement::OnScreen,
		.messages = { state->above, state->below },
		.expected = Refusal::CannotFit,
		.timeout = kControlTimeout,
	});
	AppendPlaceLeg(runner, state, {
		.name = u"cannot move off"_q,
		.placement = Placement::OffAbove,
		.messages = { state->below },
		.expected = Refusal::CannotMoveOff,
		.timeout = kControlTimeout,
	});
}

} // namespace

void AppendChatListPlaceSelfTest(not_null<Runner*> runner, PeerId chat) {
	const auto state = std::make_shared<PlaceSelfTest>();
	state->fixture = MakeWidgetFixture({
		.chat = chat,
		.prefix = QString::fromLatin1(kPrefix),
		.marker = u"CHAT_LIST_PLACE_SELFTEST"_q,
	});
	auto &fixture = *state->fixture;
	state->above = AddWidgetFixtureMessage(fixture, u"above filler"_q);
	state->first = AddWidgetFixtureMessage(fixture, u"first card"_q);
	state->second = AddWidgetFixtureMessage(fixture, u"second card"_q);
	state->below = AddWidgetFixtureMessage(fixture, u"below filler"_q);

	// Registered before anything changes. The placements' own releases,
	// registered after it, end whatever record its release leaves running.
	runner->onFinish([=] {
		Note(SelfText(u"onFinish release: "_q)
			+ ReleaseWidgetFixture(*state->fixture, u"onFinish"_q));
	});
	AppendWidgetFixtureOpen(runner, state->fixture);
	runner->addGated([=] {
		return SelfTestGate(*state);
	}, [&] {
		AppendLegs(runner, state);
	});
	AppendWidgetFixtureClose(runner, state->fixture);
}

} // namespace Test

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
