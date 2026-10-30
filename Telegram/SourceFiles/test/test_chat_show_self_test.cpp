/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_chat_show.h"

#include "base/invoke_queued.h"
#include "base/options.h"
#include "base/unixtime.h"
#include "base/weak_ptr.h"
#include "core/application.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "dialogs/dialogs_key.h"
#include "history/admin_log/history_admin_log_inner.h"
#include "history/admin_log/history_admin_log_item.h"
#include "history/admin_log/history_admin_log_section.h"
#include "history/view/history_view_chat_section.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_list_widget.h"
#include "history/history.h"
#include "history/history_inner_widget.h"
#include "history/history_item.h"
#include "history/history_widget.h"
#include "lang/lang_text_entity.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "test/test_capture.h"
#include "test/test_log.h"
#include "test/test_marking_read.h"
#include "test/test_runner.h"
#include "test/test_widgets.h"
#include "test/test_window_exposure.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/ui_utility.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include <QtCore/QStringList>
#include <QtWidgets/QApplication>

#include "styles/style_chat.h"

namespace Test {
namespace {

using Element = HistoryView::Element;
using ModernList = HistoryView::ListWidget;
using LogList = AdminLog::InnerWidget;
using ListKind = ChatListShowKind;
using Way = Window::SectionShow::Way;

// The self-test's own private reads, through its own copy of
// test_chat_show.cpp's test-only accessor: the template and every tag
// here live in this file's anonymous namespace, and no member is
// instantiated in both files. The visible message area, the pending
// layout and the mouse-active gate come from the helper's own reads
// instead, through the self-test access of test_chat_show.h. Its judge
// resolves every target again without the helper's lookups: through the
// element vectors the ListWidget and the admin log keep, and through each
// list's itemTop() and selection offset for the origin. The admin log's
// reads gate its first page; flushNewEvents() injects the self-test's
// local events, the one write here.
template <typename Tag, typename Tag::type Member>
struct PrivateMember {
	friend typename Tag::type Get(Tag) {
		return Member;
	}
};

struct InnerSelectionOffsetTag {
	using type = int (*)(
		not_null<const HistoryInner*>,
		not_null<const Element*>);
	friend type Get(InnerSelectionOffsetTag);
};

template struct PrivateMember<
	InnerSelectionOffsetTag,
	&HistoryInner::SelectionViewOffset>;

struct ListSelectionOffsetTag {
	using type = int (*)(
		not_null<const ModernList*>,
		not_null<const Element*>);
	friend type Get(ListSelectionOffsetTag);
};

template struct PrivateMember<
	ListSelectionOffsetTag,
	&ModernList::SelectionViewOffset>;

struct ListItemTopTag {
	using type = int (ModernList::*)(not_null<const Element*>) const;
	friend type Get(ListItemTopTag);
};

template struct PrivateMember<ListItemTopTag, &ModernList::itemTop>;

struct ListItemsTag {
	using type = std::vector<not_null<Element*>> ModernList::*;
	friend type Get(ListItemsTag);
};

template struct PrivateMember<ListItemsTag, &ModernList::_items>;

struct LogItemTopTag {
	using type = int (LogList::*)(not_null<const Element*>) const;
	friend type Get(LogItemTopTag);
};

template struct PrivateMember<LogItemTopTag, &LogList::itemTop>;

struct LogItemsTag {
	using type = std::vector<AdminLog::OwnedItem> LogList::*;
	friend type Get(LogItemsTag);
};

template struct PrivateMember<LogItemsTag, &LogList::_items>;

struct LogFlushTag {
	using type = void (LogList::*)(const QVector<MTPChannelAdminLogEvent>&);
	friend type Get(LogFlushTag);
};

template struct PrivateMember<LogFlushTag, &LogList::flushNewEvents>;

struct LogFilterChangedTag {
	using type = bool LogList::*;
	friend type Get(LogFilterChangedTag);
};

template struct PrivateMember<
	LogFilterChangedTag,
	&LogList::_filterChanged>;

struct LogPreloadUpTag {
	using type = mtpRequestId LogList::*;
	friend type Get(LogPreloadUpTag);
};

template struct PrivateMember<
	LogPreloadUpTag,
	&LogList::_preloadUpRequestId>;

struct LogPreloadDownTag {
	using type = mtpRequestId LogList::*;
	friend type Get(LogPreloadDownTag);
};

template struct PrivateMember<
	LogPreloadDownTag,
	&LogList::_preloadDownRequestId>;

// Starting fixture counts, not success assumptions: each far leg measures
// its own one-viewport premise and gates itself by name when the list is
// too short for it.
constexpr auto kFixtureCount = 32;
constexpr auto kEventCount = 24;

// How long a fixture wait's sample must read the same: the helper's own
// settle (kSettle in test_chat_show.cpp), which outlasts the reveal and
// selection-mode animations.
constexpr auto kSettle = crl::time(400);

// The self-test's own bounded waits. Each is shorter than its stage's
// timeout, so a fixture that never becomes ready ends in a named
// fixture-gate FAIL with its last reading, never in a stage timeout.
constexpr auto kWindowBound = crl::time(4000);
constexpr auto kOpenBound = crl::time(20000);
constexpr auto kLayoutBound = crl::time(8000);
constexpr auto kPrepareBound = crl::time(6000);
constexpr auto kRestoreBound = crl::time(6000);

// The README's teardown rule: a main-window state change is waited out,
// plus this, so no chat-background re-cache it started still paints at
// the quit (test_chat_menu.cpp's kRepaintSettle).
constexpr auto kRepaintSettle = crl::time(1500);

// The ReadyProbe's delay, counted from its first eligible call.
constexpr auto kProbeDelay = crl::time(300);

// The helper's deadline for the success legs, and the short one of the two
// controls that refuse only when it passes.
constexpr auto kShowTimeout = crl::time(10000);
constexpr auto kControlTimeout = crl::time(3000);

// How many more turns the later-layout leg reads its grown target while
// the product still has its resize pending.
constexpr auto kGrowthReadRetries = 5;

// Ids of the self-test's local admin-log events, far above ordinary event
// ids, so flushNewEvents() never skips one as already known; the injection
// still checks that every body appeared.
constexpr auto kLogEventIdBase = uint64(7730000000000000ULL);

enum class LegType {
	FarAbove,
	FarBelow,
	LaterLayout,
	CannotFit,
	OutOfView,
	NotReady,
	NoView,
};

// The self-test's readiness predicate: the first eligible call answers
// false and starts the delay, later calls answer true once the delay has
// passed, or never with |never|. It writes only this bookkeeping; |view|
// is an address, compared and never dereferenced.
struct ReadyProbe {
	QString viewIdentity;
	crl::time delay = 0;
	crl::time firstAt = 0;
	crl::time acceptedAt = 0;
	quintptr view = 0;
	int height = 0;
	int calls = 0;
	int falseAnswers = 0;
	bool never = false;
};

// Where a message sits in a list, read without the helper: the own view
// from the list's own element storage, its origin from the list's
// itemTop() and selection offset, the list's visible message area and the
// owning scroll's rectangle mapped into the list (|clip|). In list
// coordinates; |local| is element-local. |view| is an address, compared and
// never dereferenced.
struct Placement {
	QString viewIdentity;
	QString mappedRefusal;
	QRect element;
	QRect local;
	QRect rect;
	QRect visible;
	QRect clip;
	quintptr view = 0;
	int scrollTop = 0;
	bool listAlive = false;
	bool scrollOwned = false;
	bool itemAlive = false;
	bool viewShown = false;
	bool pending = false;
	bool inVisible = false;
	bool inClip = false;
	bool mapped = false;
};

// The later-layout leg's one-shot text growth: armed in its prepare stage,
// fired by the helper's first scroll through the owning scroll's
// scrollTopChanges(), applied in the next turn, read right after the
// product's layout ran.
struct Growth {
	rpl::lifetime lifetime;
	Placement before;
	Placement after;
	crl::time at = 0;
	int lines = 0;
	int lineHeight = 0;
	int generation = 0;
	int scrollRequests = 0;
	int retries = 0;
	bool armed = false;
	bool queued = false;
	bool applied = false;
};

struct WaitState {
	QString sample;
	QString details;
	crl::time started = 0;
	crl::time stableSince = 0;
};

// One prepare / helper / judge leg. Messages are named by id only; the
// list is a QPointer.
struct ShowLeg {
	QString name;
	QString tag;
	QString gate;
	QPointer<QWidget> list;
	FullMsgId target;
	Placement prepared;
	ReadyProbe probe;
	Growth growth;
	WaitState wait;
	std::shared_ptr<const ChatListShow> shown;
	crl::time timeout = 0;
	ListKind kind = ListKind::None;
	LegType type = LegType::FarAbove;
	bool fullView = false;
	bool hover = false;
	bool scrolledForPremise = false;
};

struct ShowRelease {
	QString path;
	QStringList done;
	QString items;
	QString lists;
	QString option;
	QString chat;
	QString window;
	int destroyed = 0;
	bool itemsGone = false;
	bool listsLeft = false;
	bool scrollEnabled = false;
	bool optionRestored = false;
	bool chatRestored = false;
};

// The one State of the self-test. The window, its session and the channel
// history are held weakly, the lists as QPointers and every fixture by its
// id, so nothing it holds dangles across turns; the admin log's items are
// never touched after its section is replaced.
struct ShowSelfTest {
	std::weak_ptr<ShowSelfTest> self;
	PeerId channel = 0;
	base::weak_ptr<Window::Controller> controller;
	base::weak_ptr<Window::SessionController> navigation;
	base::weak_ptr<Main::Session> session;
	base::weak_ptr<History> history;

	// The baseline, read before anything is changed.
	Qt::WindowStates windowState;
	WindowExposure exposure;
	QString baselineChat;
	PeerId baselinePeer = 0;
	bool windowMinimized = false;
	bool windowHidden = false;
	bool optionFound = false;
	bool captured = false;

	// Every mutation is marked the moment it is made.
	bool shownNormal = false;
	bool exposureKept = false;
	bool optionChanged = false;
	bool chatOpened = false;

	QPointer<QWidget> listH;
	QPointer<QWidget> listL;
	QPointer<QWidget> listA;
	std::vector<FullMsgId> fixturesH;
	std::vector<FullMsgId> fixturesL;
	std::vector<FullMsgId> bodiesA;
	std::vector<FullMsgId> logItems;
	FullMsgId shortL;
	FullMsgId tallL;
	FullMsgId unattached;
	int tallLines = 0;

	std::vector<std::unique_ptr<ShowLeg>> legs;
	QPointer<Ui::ElasticScroll> disabledScroll;
	WaitState wait;

	QString gate;
	QString gateH;
	QString gateL;
	QString gateA;

	// The release, idempotent across the teardown stage and onFinish.
	ShowRelease release;
	crl::time restoredAt = 0;
	bool itemsReleased = false;
	bool optionRestored = false;
	bool chatClosed = false;
	bool exposureRestored = false;
	bool windowRestored = false;
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

[[nodiscard]] QString IdText(FullMsgId id) {
	return id
		? u"%1:%2"_q.arg(
			NumberText(qint64(id.peer.value)),
			NumberText(qint64(id.msg.bare)))
		: u"none"_q;
}

[[nodiscard]] QString OrNone(const QString &value) {
	return value.isEmpty() ? u"none"_q : value;
}

void DeliverLeave(not_null<QWidget*> list) {
	auto leave = QEvent(QEvent::Leave);
	Settle([&] {
		QApplication::sendEvent(list.get(), &leave);
	});
}

[[nodiscard]] QString LegTypeName(LegType type) {
	switch (type) {
	case LegType::FarAbove: return u"far-above"_q;
	case LegType::FarBelow: return u"far-below"_q;
	case LegType::LaterLayout: return u"later-layout"_q;
	case LegType::CannotFit: return u"cannot-fit"_q;
	case LegType::OutOfView: return u"out-of-view"_q;
	case LegType::NotReady: return u"not-ready"_q;
	case LegType::NoView: return u"no-view"_q;
	}
	Unexpected("Type in Test::LegTypeName.");
}

[[nodiscard]] QString LegPrefix(const ShowLeg &leg) {
	return u"chat-list show self-test: "_q + leg.name + u" - "_q;
}

// Qt::WindowActive is a reading, not a state to restore: setting it back
// through QWidget::setWindowState would activate the window.
[[nodiscard]] Qt::WindowStates WithoutActive(Qt::WindowStates states) {
	auto result = states;
	result.setFlag(Qt::WindowActive, false);
	return result;
}

[[nodiscard]] QString WindowStateText(Qt::WindowStates states) {
	return u"0x"_q + QString::number(WithoutActive(states).toInt(), 16);
}

[[nodiscard]] ::MainWindow *WindowOf(const ShowSelfTest &state) {
	const auto controller = state.controller.get();
	return controller ? controller->widget().get() : nullptr;
}

[[nodiscard]] MainWidget *ContentOf(const ShowSelfTest &state) {
	const auto window = WindowOf(state);
	return window ? window->sessionContent() : nullptr;
}

[[nodiscard]] HistoryItem *Message(const ShowSelfTest &state, FullMsgId id) {
	const auto session = state.session.get();
	return (session && id) ? session->data().message(id) : nullptr;
}

[[nodiscard]] QWidget *ListFor(const ShowSelfTest &state, ListKind kind) {
	switch (kind) {
	case ListKind::History: return state.listH.data();
	case ListKind::Modern: return state.listL.data();
	case ListKind::Log: return state.listA.data();
	case ListKind::None: break;
	}
	return nullptr;
}

[[nodiscard]] const std::vector<FullMsgId> &FixturesFor(
		const ShowSelfTest &state,
		ListKind kind) {
	static const auto None = std::vector<FullMsgId>();
	switch (kind) {
	case ListKind::History: return state.fixturesH;
	case ListKind::Modern: return state.fixturesL;
	case ListKind::Log: return state.bodiesA;
	case ListKind::None: break;
	}
	return None;
}

[[nodiscard]] QString KindGate(const ShowSelfTest &state, ListKind kind) {
	if (!state.gate.isEmpty()) {
		return state.gate;
	}
	switch (kind) {
	case ListKind::History: return state.gateH;
	case ListKind::Modern: return state.gateL;
	case ListKind::Log: return state.gateA;
	case ListKind::None: break;
	}
	return QString();
}

void SetKindGate(ShowSelfTest &state, ListKind kind, const QString &gate) {
	switch (kind) {
	case ListKind::History: state.gateH = gate; break;
	case ListKind::Modern: state.gateL = gate; break;
	case ListKind::Log: state.gateA = gate; break;
	case ListKind::None: state.gate = gate; break;
	}
}

[[nodiscard]] QString LegGate(const ShowSelfTest &state, const ShowLeg &leg) {
	const auto gate = KindGate(state, leg.kind);
	return gate.isEmpty() ? leg.gate : gate;
}

// The list's own view of |item|, found through the list's own element
// storage rather than the helper's lookups: the history's blocks for a
// HistoryInner, whose public itemTop() answers at least 0 only for a view
// it shows, and the _items of a ListWidget or of the admin log, with the
// list as the element's delegate.
[[nodiscard]] Element *FindOwnView(QWidget *list, HistoryItem *item) {
	if (!list || !item) {
		return nullptr;
	} else if (const auto inner = dynamic_cast<HistoryInner*>(list)) {
		for (const auto &block : item->history()->blocks) {
			for (const auto &view : block->messages) {
				if (view->data() == item) {
					return (inner->itemTop(view.get()) >= 0)
						? view.get()
						: nullptr;
				}
			}
		}
	} else if (const auto modern = dynamic_cast<ModernList*>(list)) {
		const auto delegate = static_cast<HistoryView::ElementDelegate*>(
			modern);
		for (const auto &view : modern->*Get(ListItemsTag())) {
			if ((view->data() == item)
				&& (view->delegate().get() == delegate)) {
				return view.get();
			}
		}
	} else if (const auto log = dynamic_cast<LogList*>(list)) {
		const auto delegate = static_cast<HistoryView::ElementDelegate*>(
			log);
		for (const auto &owned : log->*Get(LogItemsTag())) {
			const auto view = owned.get();
			if (view
				&& (view->data() == item)
				&& (view->delegate().get() == delegate)) {
				return view;
			}
		}
	}
	return nullptr;
}

// The view's top-left in the list, from the list's own itemTop() and the
// selection-mode offset, not through its mapPointToItem().
[[nodiscard]] QPoint ViewOrigin(
		QWidget *list,
		not_null<const Element*> view) {
	if (const auto inner = dynamic_cast<HistoryInner*>(list)) {
		return QPoint(
			Get(InnerSelectionOffsetTag())(inner, view),
			inner->itemTop(view.get()));
	} else if (const auto modern = dynamic_cast<ModernList*>(list)) {
		return QPoint(
			Get(ListSelectionOffsetTag())(modern, view),
			(modern->*Get(ListItemTopTag()))(view));
	} else if (const auto log = dynamic_cast<LogList*>(list)) {
		return QPoint(0, (log->*Get(LogItemTopTag()))(view));
	}
	return QPoint();
}

// |fullView| reads the view's whole bounds, otherwise its
// Element::innerGeometry(), the two rectangles the legs hand the helper.
[[nodiscard]] Placement ReadPlacement(
		QWidget *list,
		HistoryItem *item,
		bool fullView) {
	auto result = Placement();
	if (!list) {
		return result;
	}
	result.listAlive = true;
	result.itemAlive = (item != nullptr);
	const auto scroll = ChatListShowOwningScroll(list);
	result.scrollOwned = (scroll != nullptr);
	if (ChatListShowKindOf(list) != ListKind::None) {
		result.visible = ChatListShowVisibleArea(list);
		result.pending = ChatListShowPendingResize(list);
	}
	if (scroll) {
		result.scrollTop = scroll->scrollTop();
		result.clip = Ui::MapFrom(
			list,
			scroll,
			scroll->rect()).intersected(list->rect());
	}
	const auto view = FindOwnView(list, item);
	if (!view) {
		return result;
	}
	const auto bounds = QRect(QPoint(), QSize(view->width(), view->height()));
	const auto origin = ViewOrigin(list, view);
	result.viewShown = true;
	result.view = reinterpret_cast<quintptr>(view);
	result.viewIdentity = ChatListShowViewIdentity(view);
	result.element = bounds.translated(origin);
	result.local = fullView ? bounds : view->innerGeometry();
	result.rect = result.local.translated(origin);
	result.pending = result.pending || view->pendingResize();
	result.inVisible = !result.rect.isEmpty()
		&& result.visible.contains(result.rect);
	result.inClip = !result.rect.isEmpty()
		&& result.clip.contains(result.rect);
	if (scroll) {
		const auto mapped = ReadMappedTarget(scroll, list, result.rect);
		result.mapped = mapped.resolved();
		result.mappedRefusal = mapped.refusal;
	}
	return result;
}

[[nodiscard]] QString PlacementText(const Placement &placement) {
	const auto fields = QStringList{
		u"list=%1"_q.arg(FlagText(placement.listAlive)),
		u"scrollOwned=%1"_q.arg(FlagText(placement.scrollOwned)),
		u"item=%1"_q.arg(FlagText(placement.itemAlive)),
		u"view=%1"_q.arg(placement.viewShown
			? placement.viewIdentity
			: u"none"_q),
		u"element=%1"_q.arg(RectText(placement.element)),
		u"local=%1"_q.arg(RectText(placement.local)),
		u"rect=%1"_q.arg(RectText(placement.rect)),
		u"visible=%1"_q.arg(RectText(placement.visible)),
		u"clip=%1"_q.arg(RectText(placement.clip)),
		u"scrollTop=%1"_q.arg(NumberText(placement.scrollTop)),
		u"pending=%1"_q.arg(FlagText(placement.pending)),
		u"inVisible=%1"_q.arg(FlagText(placement.inVisible)),
		u"inClip=%1"_q.arg(FlagText(placement.inClip)),
		u"mapped=%1"_q.arg(placement.mapped
			? u"resolved"_q
			: OrNone(placement.mappedRefusal)),
	};
	return fields.join(u" "_q);
}

// How far the rect lies above or below the visible message area: the gap
// between the rect's bottom and the area's top, or between the area's
// bottom and the rect's top. Negative while they overlap.
[[nodiscard]] int DistanceAbove(QRect rect, QRect visible) {
	return visible.y() - (rect.y() + rect.height());
}

[[nodiscard]] int DistanceBelow(QRect rect, QRect visible) {
	return rect.y() - (visible.y() + visible.height());
}

[[nodiscard]] int FarDistance(LegType type, QRect rect, QRect visible) {
	return (type == LegType::FarAbove)
		? DistanceAbove(rect, visible)
		: DistanceBelow(rect, visible);
}

[[nodiscard]] int OutsideDistance(const Placement &placement) {
	return placement.viewShown
		? std::max(
			DistanceAbove(placement.rect, placement.visible),
			DistanceBelow(placement.rect, placement.visible))
		: -1;
}

void StartWait(WaitState &wait) {
	wait = WaitState{ .started = crl::now() };
}

// True once |sample| read the same for kSettle. Only the wait's own
// bookkeeping is written.
[[nodiscard]] bool SampleSettled(WaitState &wait, const QString &sample) {
	const auto now = crl::now();
	if (sample != wait.sample) {
		wait.sample = sample;
		wait.stableSince = now;
		return false;
	}
	return (now - wait.stableSince) >= kSettle;
}

[[nodiscard]] bool BoundReached(const WaitState &wait, crl::time bound) {
	return (crl::now() - wait.started) >= bound;
}

[[nodiscard]] bool AskProbe(ReadyProbe &probe, ChatListShowView view) {
	const auto now = crl::now();
	const auto first = !probe.calls;
	++probe.calls;
	probe.view = reinterpret_cast<quintptr>(view.get());
	probe.viewIdentity = ChatListShowViewIdentity(view);
	probe.height = view->height();
	if (first) {
		probe.firstAt = now;
	}
	const auto ready = !first
		&& !probe.never
		&& ((now - probe.firstAt) >= probe.delay);
	if (!ready) {
		++probe.falseAnswers;
	} else if (!probe.acceptedAt) {
		probe.acceptedAt = now;
	}
	return ready;
}

[[nodiscard]] QString ProbeText(const ReadyProbe &probe) {
	const auto fields = QStringList{
		u"calls=%1"_q.arg(NumberText(probe.calls)),
		u"false=%1"_q.arg(NumberText(probe.falseAnswers)),
		u"delay=%1"_q.arg(NumberText(probe.delay)),
		u"never=%1"_q.arg(FlagText(probe.never)),
		u"waited=%1"_q.arg(probe.acceptedAt
			? NumberText(probe.acceptedAt - probe.firstAt)
			: u"none"_q),
		u"view=%1"_q.arg(OrNone(probe.viewIdentity)),
		u"height=%1"_q.arg(NumberText(probe.height)),
	};
	return u"probe{"_q + fields.join(u" "_q) + u"}"_q;
}

[[nodiscard]] QString FixtureName(const QString &tag, int index) {
	return u"Harness chat-list show %1 %2"_q.arg(
		tag,
		NumberText(index).rightJustified(2, QChar('0')));
}

// Three short lines, no link, mention, hashtag or command, so nothing
// wraps or parses into an entity.
[[nodiscard]] QString FixtureText(const QString &tag, int index) {
	const auto name = FixtureName(tag, index);
	return name
		+ u"\nsecond line of "_q
		+ name
		+ u"\nthird line of "_q
		+ name;
}

[[nodiscard]] QString LinesText(const QString &tag, int lines) {
	auto result = QStringList();
	for (auto i = 0; i != lines; ++i) {
		result.push_back(FixtureName(tag, i));
	}
	return result.join(u"\n"_q);
}

// An outgoing local client-id text message, the shape the product's
// streamed drafts add (history_streamed_drafts.cpp): not isRegular(), so
// Histories::readInboxTill walks back past it, and never sent.
[[nodiscard]] not_null<HistoryItem*> InjectLocalText(
		not_null<History*> history,
		const QString &text) {
	return history->addNewLocalMessage({
		.id = history->owner().nextLocalMessageId(),
		.flags = (MessageFlag::Local
			| MessageFlag::Outgoing
			| MessageFlag::HasFromId),
		.from = history->session().userPeerId(),
		.date = base::unixtime::now(),
	}, tr::marked(text), MTP_messageMediaEmpty());
}

// Registered in the session and added to no history block, slice or log,
// so no list shows a view of it. MessageFlag::FakeHistoryItem keeps it off
// History::registerClientSideMessage, the route a ListWidget slice shows
// a local message through.
[[nodiscard]] not_null<HistoryItem*> MakeUnattached(
		not_null<History*> history) {
	return history->makeMessage({
		.id = history->owner().nextLocalMessageId(),
		.flags = (MessageFlag::Local
			| MessageFlag::FakeHistoryItem
			| MessageFlag::Outgoing
			| MessageFlag::HasFromId),
		.from = history->session().userPeerId(),
		.date = base::unixtime::now(),
	}, tr::marked(FixtureName(u"unattached"_q, 0)), MTP_messageMediaEmpty());
}

[[nodiscard]] bool LocalShape(not_null<HistoryItem*> item) {
	return item->isLocal()
		&& !item->isRegular()
		&& item->out()
		&& !item->isService()
		&& IsClientMsgId(item->id);
}

[[nodiscard]] QString FixtureRow(
		not_null<HistoryItem*> item,
		const QString &tag) {
	const auto fields = QStringList{
		u"tag=%1"_q.arg(tag),
		u"id=%1"_q.arg(IdText(item->fullId())),
		u"clientMsgId=%1"_q.arg(FlagText(IsClientMsgId(item->id))),
		u"isLocal=%1"_q.arg(FlagText(item->isLocal())),
		u"isRegular=%1"_q.arg(FlagText(item->isRegular())),
		u"isHistoryEntry=%1"_q.arg(FlagText(item->isHistoryEntry())),
		u"isService=%1"_q.arg(FlagText(item->isService())),
		u"out=%1"_q.arg(FlagText(item->out())),
		u"mainView=%1"_q.arg(FlagText(item->mainView() != nullptr)),
	};
	return u"FIXTURE_SHOW: "_q + fields.join(u" "_q);
}

// The legacy list recounts a pending history layout in HistoryWidget's
// paint, and History::addNewItem leaves it to whoever calls
// Data::Session::sendHistoryChangeNotifications next. This is that call,
// made only in fixture action stages, never from an until.
void ProcessPendingLayout(History *history, const QString &where) {
	if (!history || !history->hasPendingResizedItems()) {
		return;
	}
	history->owner().notifyHistoryChangeDelayed(history);
	history->owner().sendHistoryChangeNotifications();
	Note(u"chat-list show self-test: processed a pending history layout "
		u"%1 through Data::Session::notifyHistoryChangeDelayed and "
		u"sendHistoryChangeNotifications: pendingAfter=%2"_q.arg(
			where,
			FlagText(history->hasPendingResizedItems())));
}

[[nodiscard]] Window::SectionShow Background() {
	return Window::SectionShow(
		Way::ClearStack,
		anim::type::instant,
		anim::activation::background);
}

[[nodiscard]] FullMsgId EnsureUnattached(ShowSelfTest &state) {
	if (Message(state, state.unattached)) {
		return state.unattached;
	}
	const auto history = state.history.get();
	if (!history) {
		return FullMsgId();
	}
	const auto item = MakeUnattached(history);
	state.unattached = item->fullId();
	LogRaw(FixtureRow(item, u"chat-list show unattached"_q));
	return state.unattached;
}

// One fixture-gate Check: on failure it gates |kind|'s stages, or every
// stage with ListKind::None.
void GateCheck(
		ShowSelfTest &state,
		ListKind kind,
		const QString &what,
		const QStringList &missing,
		const QString &details) {
	const auto full = u"fixture gate: chat-list show self-test: "_q + what;
	Check(
		missing.isEmpty(),
		full,
		missing.isEmpty()
			? details
			: (missing.join(u"; "_q) + u" - "_q + details));
	if (!missing.isEmpty()) {
		SetKindGate(state, kind, full + u" - "_q + missing.join(u"; "_q));
	}
}

[[nodiscard]] QString BaselineText(const ShowSelfTest &state) {
	const auto history = state.history.get();
	const auto known = history && history->unreadCountKnown();
	const auto fields = QStringList{
		u"channel=%1"_q.arg(NumberText(qint64(state.channel.value))),
		u"windowState=%1"_q.arg(WindowStateText(state.windowState)),
		u"minimized=%1"_q.arg(FlagText(state.windowMinimized)),
		u"hidden=%1"_q.arg(FlagText(state.windowHidden)),
		u"use-new-chat-view=%1"_q.arg(FlagText(state.optionFound)),
		u"chat={%1}"_q.arg(state.baselineChat),
		u"folderKnown=%1"_q.arg(FlagText(history && history->folderKnown())),
		u"unread=%1"_q.arg(known
			? NumberText(history->unreadCount())
			: u"unknown"_q),
		u"loadedAtBottom=%1"_q.arg(
			FlagText(history && history->loadedAtBottom())),
	};
	return fields.join(u" "_q);
}

// Reads the baseline and the fixture's prerequisites; changes nothing.
// Answers what is missing, or an empty string.
[[nodiscard]] QStringList CaptureBaseline(ShowSelfTest &state) {
	const auto controller = Core::App().activePrimaryWindow();
	const auto navigation = controller
		? controller->sessionController()
		: nullptr;
	if (!controller) {
		return { u"Core::App().activePrimaryWindow() is null"_q };
	} else if (!navigation) {
		return { u"the primary window has no session controller"_q };
	}
	const auto window = controller->widget().get();
	if (!window->sessionContent()) {
		return { u"the primary window shows no session content"_q };
	}
	const auto session = &navigation->session();
	state.controller = base::make_weak(controller);
	state.navigation = base::make_weak(navigation);
	state.session = base::make_weak(session);
	state.windowState = WithoutActive(window->windowState());
	state.windowMinimized = window->isMinimized();
	state.windowHidden = window->isHidden();
	state.optionFound = base::options::lookup<bool>(
		kOptionUseNewChatView).value();
	const auto peer = navigation->activeChatCurrent().peer();
	state.baselinePeer = peer ? peer->id : PeerId();
	state.baselineChat = peer
		? (u"peer="_q + NumberText(qint64(peer->id.value)))
		: u"none"_q;
	state.captured = true;
	const auto channel = peerIsChannel(state.channel)
		? session->data().channelLoaded(peerToChannel(state.channel))
		: nullptr;
	if (!channel) {
		return { u"the channel is not loaded in this session"_q };
	}
	const auto history = session->data().history(channel);
	state.history = base::make_weak(history.get());
	auto missing = QStringList();
	if (!channel->amIn()) {
		missing.push_back(u"the account is not a member"_q);
	}
	if (!channel->amCreator() && !channel->hasAdminRights()) {
		missing.push_back(u"the account has no Recent Actions access: "
			u"neither its creator nor an admin"_q);
	}
	if (!history->folderKnown()) {
		missing.push_back(u"its folder is not known, so "
			u"History::newItemAdded would request a dialog entry"_q);
	}
	if (!history->unreadCountKnown() || history->unreadCount()) {
		missing.push_back(u"it may hold unread messages, which a shown "
			u"window marking read would send a read request for"_q);
	}
	if (state.windowHidden) {
		missing.push_back(u"the primary window is hidden"_q);
	}
	return missing;
}

void StartBaseline(ShowSelfTest &state) {
	StartWait(state.wait);
	const auto missing = CaptureBaseline(state);
	GateCheck(
		state,
		ListKind::None,
		u"a session window, the test channel loaded with Recent Actions "
		u"access, its folder known, no unread messages, and the window "
		u"not hidden"_q,
		missing,
		BaselineText(state));
	if (!missing.isEmpty()) {
		return;
	}
	const auto window = WindowOf(state);
	if (window && window->isMinimized()) {
		state.shownNormal = true;
		window->showNormal();
		Note(u"chat-list show self-test: the primary window was "
			u"minimized, so showNormal() was called; nothing activates "
			u"it"_q);
	}
}

[[nodiscard]] QString WindowText(const ShowSelfTest &state) {
	const auto window = WindowOf(state);
	if (!window) {
		return u"window=gone"_q;
	}
	return u"captured{state=%1 hidden=%2} now{state=%3 minimized=%4 "
		u"hidden=%5}"_q.arg(
			WindowStateText(state.windowState),
			FlagText(state.windowHidden),
			WindowStateText(window->windowState()),
			FlagText(window->isMinimized()),
			FlagText(window->isHidden()));
}

[[nodiscard]] bool WindowShown(ShowSelfTest &state) {
	if (!state.gate.isEmpty()) {
		return true;
	}
	const auto window = WindowOf(state);
	state.wait.details = WindowText(state);
	return (window && !window->isMinimized() && !window->isHidden())
		|| BoundReached(state.wait, kWindowBound);
}

void CheckWindowShown(ShowSelfTest &state) {
	if (!state.gate.isEmpty()) {
		return;
	}
	const auto window = WindowOf(state);
	auto missing = QStringList();
	if (!window || window->isMinimized() || window->isHidden()) {
		missing.push_back(u"the primary window is not shown normally"_q);
	}
	GateCheck(
		state,
		ListKind::None,
		u"the primary window is shown and not minimized, so its lists "
		u"can paint"_q,
		missing,
		state.wait.details);
}

// KeepWindowExposed only sets a stacking hint: it never activates the
// window, and the self-test activates nothing at all.
void KeepExposed(ShowSelfTest &state) {
	StartWait(state.wait);
	state.exposure = KeepWindowExposed(WindowOf(state));
	state.exposureKept = true;
	Note(u"chat-list show self-test: KeepWindowExposed: "_q
		+ WindowExposureText(state.exposure));
}

[[nodiscard]] bool WindowExposed(ShowSelfTest &state) {
	if (!state.gate.isEmpty() || !state.exposure.refusal.isEmpty()) {
		return true;
	}
	const auto reading = ReadWindowExposure(WindowOf(state));
	state.wait.details = WindowExposureText(reading);
	return reading.exposed || BoundReached(state.wait, kWindowBound);
}

void CheckWindowExposed(ShowSelfTest &state) {
	if (!state.gate.isEmpty()) {
		return;
	}
	const auto reading = ReadWindowExposure(WindowOf(state));
	auto missing = QStringList();
	if (!state.exposure.refusal.isEmpty()) {
		missing.push_back(u"KeepWindowExposed refused: "_q
			+ state.exposure.refusal);
	} else if (!reading.exposed) {
		missing.push_back(u"the primary window is not exposed"_q);
	}
	GateCheck(
		state,
		ListKind::None,
		u"the primary window is exposed, so its lists get real paints"_q,
		missing,
		WindowExposureText(reading));
	Note(u"chat-list show self-test: marking read with the window "
		u"shown: "_q
		+ MarkingReadDetails(ReadMainWindowMarking(
			state.controller.get())));
}

// Shows the channel in the background at its end: in the legacy
// HistoryWidget with |modern| false and in a HistoryView::ChatWidget with
// it true, through the existing use-new-chat-view option. Background, so
// nothing activates the window.
void OpenChannel(ShowSelfTest &state, bool modern) {
	StartWait(state.wait);
	auto &option = base::options::lookup<bool>(kOptionUseNewChatView);
	if (option.value() != modern) {
		state.optionChanged = true;
		option.set(modern);
	}
	if (const auto navigation = state.navigation.get()) {
		state.chatOpened = true;
		navigation->showPeerHistory(
			state.channel,
			Background(),
			ShowAtTheEndMsgId);
	}
}

void OpenRecentActions(ShowSelfTest &state) {
	StartWait(state.wait);
	const auto history = state.history.get();
	const auto channel = history ? history->peer->asChannel() : nullptr;
	const auto navigation = state.navigation.get();
	if (navigation && channel) {
		state.chatOpened = true;
		navigation->showSection(
			std::make_shared<AdminLog::SectionMemento>(channel),
			Background());
	}
}

[[nodiscard]] Element *LastView(not_null<History*> history) {
	if (history->blocks.empty()) {
		return nullptr;
	}
	const auto &block = history->blocks.back();
	return block->messages.empty() ? nullptr : block->messages.back().get();
}

// The HistoryInner of the legacy HistoryWidget that shows the channel,
// found by RTTI and accepted only as its scroll's owned widget there and,
// when the history has views, as the list itemTop() places its last one
// in.
[[nodiscard]] HistoryInner *ResolveLegacyInner(const ShowSelfTest &state) {
	const auto content = ContentOf(state);
	const auto history = state.history.get();
	if (!content || !history) {
		return nullptr;
	}
	const auto last = LastView(history);
	for (const auto widget : FindAll<HistoryWidget>(content)) {
		if ((widget->history() != history) || widget->isHidden()) {
			continue;
		}
		for (const auto inner : FindAll<HistoryInner>(widget)) {
			if (ChatListShowOwningScroll(inner)
				&& inner->isVisible()
				&& (!last || (inner->itemTop(last) >= 0))) {
				return inner;
			}
		}
	}
	return nullptr;
}

// The ListWidget of the History-mode ChatWidget that shows the channel:
// a ChatWidget for the channel's history itself (no replies root, no
// sublist), a list with a window controller, owned by a scroll the
// ChatWidget holds directly.
[[nodiscard]] ModernList *ResolveModernList(const ShowSelfTest &state) {
	const auto content = ContentOf(state);
	const auto history = state.history.get();
	if (!content || !history) {
		return nullptr;
	}
	for (const auto chat : FindAll<HistoryView::ChatWidget>(content)) {
		const auto id = chat->id();
		if ((id.history.get() != history)
			|| id.repliesRootId
			|| id.sublist
			|| chat->isHidden()) {
			continue;
		}
		for (const auto list : FindAll<ModernList>(chat)) {
			const auto scroll = ChatListShowOwningScroll(list);
			if (scroll
				&& (scroll->parentWidget() == chat)
				&& list->controllerOrNull()
				&& list->isVisible()) {
				return list;
			}
		}
	}
	return nullptr;
}

[[nodiscard]] LogList *ResolveLogList(const ShowSelfTest &state) {
	const auto content = ContentOf(state);
	const auto history = state.history.get();
	const auto channel = history ? history->peer->asChannel() : nullptr;
	if (!content || !channel) {
		return nullptr;
	}
	for (const auto log : FindAll<LogList>(content)) {
		if ((log->channel().get() == channel)
			&& log->isVisible()
			&& ChatListShowOwningScroll(log)) {
			return log;
		}
	}
	return nullptr;
}

[[nodiscard]] QString ListText(QWidget *list) {
	if (!list) {
		return u"list=none"_q;
	}
	const auto scroll = ChatListShowOwningScroll(list);
	return u"list=%1 visible=%2 scrollHeight=%3"_q.arg(
		WidgetDescription(list),
		RectText(ChatListShowVisibleArea(list)),
		scroll ? NumberText(scroll->height()) : u"none"_q);
}

[[nodiscard]] bool LegacyOpened(ShowSelfTest &state) {
	if (!state.gate.isEmpty()) {
		return true;
	}
	const auto inner = ResolveLegacyInner(state);
	const auto history = state.history.get();
	const auto ready = inner
		&& history
		&& history->loadedAtBottom()
		&& !history->hasPendingResizedItems()
		&& (inner->height() > 0);
	const auto sample = u"ready=%1 loadedAtBottom=%2 pending=%3 %4"_q.arg(
		FlagText(ready),
		FlagText(history && history->loadedAtBottom()),
		FlagText(history && history->hasPendingResizedItems()),
		ListText(inner));
	state.wait.details = sample;
	return (SampleSettled(state.wait, sample) && ready)
		|| BoundReached(state.wait, kOpenBound);
}

void CheckLegacyOpened(ShowSelfTest &state) {
	const auto inner = ResolveLegacyInner(state);
	const auto history = state.history.get();
	state.listH = inner;
	auto missing = QStringList();
	if (!inner) {
		missing.push_back(u"no HistoryInner of a legacy HistoryWidget "
			u"shows the channel as its scroll's owned widget"_q);
	}
	if (!history || !history->loadedAtBottom()) {
		missing.push_back(u"the history is not loaded at its bottom, so "
			u"History::addNewItem would keep the fixtures out of its "
			u"blocks"_q);
	} else if (history->hasPendingResizedItems()) {
		missing.push_back(u"it still has pending resized items"_q);
	}
	GateCheck(
		state,
		ListKind::History,
		u"the channel is shown by its own HistoryInner in the legacy "
		u"HistoryWidget, loaded at its bottom with nothing pending"_q,
		missing,
		state.wait.details);
}

[[nodiscard]] bool ModernOpened(ShowSelfTest &state) {
	if (!state.gate.isEmpty()) {
		return true;
	}
	const auto list = ResolveModernList(state);
	const auto history = state.history.get();
	const auto last = history ? LastView(history) : nullptr;
	const auto lastShown = !last || FindOwnView(list, last->data());
	const auto ready = list && (list->height() > 0) && lastShown;
	const auto sample = u"ready=%1 lastShown=%2 %3"_q.arg(
		FlagText(ready),
		FlagText(lastShown),
		ListText(list));
	state.wait.details = sample;
	return (SampleSettled(state.wait, sample) && ready)
		|| BoundReached(state.wait, kOpenBound);
}

void CheckModernOpened(ShowSelfTest &state) {
	const auto list = ResolveModernList(state);
	const auto history = state.history.get();
	const auto last = history ? LastView(history) : nullptr;
	state.listL = list;
	auto missing = QStringList();
	if (!list) {
		missing.push_back(u"no ListWidget of a History-mode ChatWidget "
			u"shows the channel as its scroll's owned widget"_q);
	} else if (last && !FindOwnView(list, last->data())) {
		missing.push_back(u"the list shows no view of its own for the "
			u"history's last message"_q);
	}
	GateCheck(
		state,
		ListKind::Modern,
		u"the channel is shown by the ListWidget of its own History-mode "
		u"ChatWidget, its last message in the list's own view"_q,
		missing,
		state.wait.details);
}

[[nodiscard]] QString LogLoadText(LogList *log) {
	if (!log) {
		return u"log=none"_q;
	}
	return u"filterChanged=%1 preloadUp=%2 preloadDown=%3 items=%4 "
		u"loadedAtTop=%5 %6"_q.arg(
			FlagText(log->*Get(LogFilterChangedTag())),
			FlagText((log->*Get(LogPreloadUpTag())) != 0),
			FlagText((log->*Get(LogPreloadDownTag())) != 0),
			NumberText(int((log->*Get(LogItemsTag())).size())),
			FlagText(log->loadedAtTop()),
			ListText(log));
}

// The first page has arrived, or the log is known to be empty, and no
// page or filter change is still loading: flushNewEvents() ignores events
// while _filterChanged is set.
[[nodiscard]] bool LogLoaded(LogList *log) {
	return log
		&& !(log->*Get(LogFilterChangedTag()))
		&& !(log->*Get(LogPreloadUpTag()))
		&& !(log->*Get(LogPreloadDownTag()))
		&& (!(log->*Get(LogItemsTag())).empty() || log->loadedAtTop())
		&& (log->height() > 0);
}

[[nodiscard]] bool LogOpened(ShowSelfTest &state) {
	if (!state.gate.isEmpty()) {
		return true;
	}
	const auto log = ResolveLogList(state);
	const auto ready = LogLoaded(log);
	const auto sample = u"ready=%1 %2"_q.arg(
		FlagText(ready),
		LogLoadText(log));
	state.wait.details = sample;
	return (SampleSettled(state.wait, sample) && ready)
		|| BoundReached(state.wait, kOpenBound);
}

void CheckLogOpened(ShowSelfTest &state) {
	const auto log = ResolveLogList(state);
	state.listA = log;
	auto missing = QStringList();
	if (!log) {
		missing.push_back(u"no AdminLog::InnerWidget of the channel is "
			u"shown as its scroll's owned widget"_q);
	} else if (!LogLoaded(log)) {
		missing.push_back(u"its first page or a filter change is still "
			u"loading"_q);
	}
	GateCheck(
		state,
		ListKind::Log,
		u"the channel's Recent Actions are shown with their first page "
		u"loaded and nothing loading"_q,
		missing,
		state.wait.details);
}

void InjectHistoryFixtures(ShowSelfTest &state) {
	StartWait(state.wait);
	const auto history = state.history.get();
	const auto inner = state.listH.data();
	auto missing = QStringList();
	if (!history || !history->loadedAtBottom()) {
		missing.push_back(u"the history is gone or not loaded at its "
			u"bottom"_q);
	}
	if (!inner || !inner->isVisible()) {
		missing.push_back(u"the HistoryInner is not shown"_q);
	}
	if (!missing.isEmpty()) {
		GateCheck(
			state,
			ListKind::History,
			u"the HistoryInner injection preconditions hold in the turn "
			u"that injects"_q,
			missing,
			ListText(inner));
		return;
	}
	for (auto i = 0; i != kFixtureCount; ++i) {
		const auto item = InjectLocalText(history, FixtureText(u"H"_q, i));
		state.fixturesH.push_back(item->fullId());
	}
	ProcessPendingLayout(history, u"after the HistoryInner injection"_q);
}

[[nodiscard]] std::vector<FullMsgId> ModernIds(const ShowSelfTest &state) {
	auto result = state.fixturesL;
	result.push_back(state.tallL);
	result.push_back(state.shortL);
	return result;
}

// The ListWidget's fixtures, top to bottom: one message taller than one
// and a half visible message areas, by the measured area and the message
// text's font height, then three-line messages with one short one-line
// target in their middle.
void InjectModernFixtures(ShowSelfTest &state) {
	StartWait(state.wait);
	const auto history = state.history.get();
	const auto list = state.listL.data();
	const auto visible = list
		? ChatListShowVisibleArea(list).height()
		: 0;
	const auto line = std::max(st::messageTextStyle.font->height, 1);
	auto missing = QStringList();
	if (!history) {
		missing.push_back(u"the history is gone"_q);
	}
	if (!list || !list->isVisible()) {
		missing.push_back(u"the ListWidget is not shown"_q);
	} else if (visible <= 0) {
		missing.push_back(u"the list has no visible message area"_q);
	}
	if (!missing.isEmpty()) {
		GateCheck(
			state,
			ListKind::Modern,
			u"the ListWidget injection preconditions hold in the turn "
			u"that injects"_q,
			missing,
			ListText(list));
		return;
	}
	state.tallLines = ((3 * visible) / (2 * line)) + 4;
	state.tallL = InjectLocalText(
		history,
		LinesText(u"tall"_q, state.tallLines))->fullId();
	for (auto i = 0; i != kFixtureCount; ++i) {
		if (i == kFixtureCount / 2) {
			state.shortL = InjectLocalText(
				history,
				FixtureName(u"short"_q, 0))->fullId();
		}
		const auto item = InjectLocalText(history, FixtureText(u"L"_q, i));
		state.fixturesL.push_back(item->fullId());
	}
	Note(u"chat-list show self-test: ListWidget fixtures injected while "
		u"it is shown: visible=%1 lineHeight=%2 tallLines=%3 tall=%4 "
		u"short=%5 first=%6 last=%7"_q.arg(
			NumberText(visible),
			NumberText(line),
			NumberText(state.tallLines),
			IdText(state.tallL),
			IdText(state.shortL),
			IdText(state.fixturesL.front()),
			IdText(state.fixturesL.back())));
	ProcessPendingLayout(history, u"after the ListWidget injection"_q);
}

// Delivers plain ChangeAbout events through the log's own new-events
// path, newest first as the server sends them, so the first fixture ends
// up on top; each adds a service line and a text body, found by its
// unique text.
void InjectLogEvents(ShowSelfTest &state) {
	StartWait(state.wait);
	const auto log = dynamic_cast<LogList*>(state.listA.data());
	const auto session = state.session.get();
	auto missing = QStringList();
	if (!log || !session) {
		missing.push_back(u"the Recent Actions list is gone"_q);
	} else if (!LogLoaded(log)) {
		missing.push_back(u"a page or a filter change is loading, and "
			u"flushNewEvents() ignores events while _filterChanged is "
			u"set"_q);
	}
	if (!missing.isEmpty()) {
		GateCheck(
			state,
			ListKind::Log,
			u"the Recent Actions injection preconditions hold in the turn "
			u"that injects"_q,
			missing,
			LogLoadText(log));
		return;
	}
	auto before = base::flat_set<FullMsgId>();
	for (const auto &owned : log->*Get(LogItemsTag())) {
		if (const auto view = owned.get()) {
			before.emplace(view->data()->fullId());
		}
	}
	const auto now = base::unixtime::now();
	const auto user = int64(session->userId().bare);
	auto texts = std::vector<QString>(kEventCount);
	auto events = QVector<MTPChannelAdminLogEvent>();
	events.reserve(kEventCount);
	for (auto i = kEventCount; i != 0;) {
		--i;
		texts[i] = FixtureText(u"A"_q, i);
		events.push_back(MTP_channelAdminLogEvent(
			MTP_long(int64(kLogEventIdBase + i)),
			MTP_int(now - (kEventCount - i)),
			MTP_long(user),
			MTP_channelAdminLogEventActionChangeAbout(
				MTP_string(QString()), // prev_value
				MTP_string(texts[i])))); // new_value
	}
	(log->*Get(LogFlushTag()))(events);
	log->resetNewEventsCount();
	state.bodiesA.assign(kEventCount, FullMsgId());
	for (const auto &owned : log->*Get(LogItemsTag())) {
		const auto view = owned.get();
		if (!view || before.contains(view->data()->fullId())) {
			continue;
		}
		const auto item = view->data();
		state.logItems.push_back(item->fullId());
		const auto text = item->originalText().text;
		for (auto i = 0; i != kEventCount; ++i) {
			if (text == texts[i]) {
				state.bodiesA[i] = item->fullId();
			}
		}
	}
	Note(u"chat-list show self-test: Recent Actions events injected: "
		u"events=%1 newItems=%2 first=%3 last=%4 %5"_q.arg(
			NumberText(kEventCount),
			NumberText(int(state.logItems.size())),
			IdText(state.bodiesA.front()),
			IdText(state.bodiesA.back()),
			LogLoadText(log)));
}

// Pure but for the wait's own bookkeeping: every fixture in |kind|'s list
// with an own view, nothing pending, the first and last placements the
// same for kSettle.
[[nodiscard]] bool FixturesLaidOut(
		ShowSelfTest &state,
		ListKind kind,
		const std::vector<FullMsgId> &ids) {
	if (!KindGate(state, kind).isEmpty()) {
		return true;
	}
	const auto list = ListFor(state, kind);
	auto shown = 0;
	auto pending = list && ChatListShowPendingResize(list);
	for (const auto &id : ids) {
		if (const auto view = FindOwnView(list, Message(state, id))) {
			++shown;
			pending = pending || view->pendingResize();
		}
	}
	const auto first = ids.empty()
		? Placement()
		: ReadPlacement(list, Message(state, ids.front()), true);
	const auto last = ids.empty()
		? Placement()
		: ReadPlacement(list, Message(state, ids.back()), true);
	const auto ready = !ids.empty()
		&& (shown == int(ids.size()))
		&& !pending;
	const auto sample = u"ready=%1 shown=%2/%3 pending=%4 first={%5} "
		u"last={%6}"_q.arg(
			FlagText(ready),
			NumberText(shown),
			NumberText(int(ids.size())),
			FlagText(pending),
			PlacementText(first),
			PlacementText(last));
	state.wait.details = sample;
	return (SampleSettled(state.wait, sample) && ready)
		|| BoundReached(state.wait, kLayoutBound);
}

void CheckFixturesLaidOut(
		ShowSelfTest &state,
		ListKind kind,
		const std::vector<FullMsgId> &ids,
		int expected,
		const QString &tag) {
	if (!KindGate(state, kind).isEmpty()) {
		return;
	}
	const auto list = ListFor(state, kind);
	const auto log = (kind == ListKind::Log);
	auto shown = 0;
	auto shaped = 0;
	auto pending = false;
	auto bad = QStringList();
	for (const auto &id : ids) {
		const auto item = Message(state, id);
		const auto view = FindOwnView(list, item);
		if (view) {
			++shown;
			pending = pending || view->pendingResize();
		}
		const auto shape = item
			&& (log
				? (item->isAdminLogEntry() && !item->isService())
				: LocalShape(item));
		if (shape) {
			++shaped;
		} else if (bad.size() < 3) {
			bad.push_back(item ? FixtureRow(item, tag) : IdText(id));
		}
	}
	const auto count = int(ids.size());
	auto missing = QStringList();
	if (!count || (count != expected)) {
		missing.push_back(u"%1 fixtures for %2 expected"_q.arg(
			NumberText(count),
			NumberText(expected)));
	}
	if (shown != count) {
		missing.push_back(u"only %1 have the list's own view"_q.arg(
			NumberText(shown)));
	}
	if (shaped != count) {
		missing.push_back((log
			? u"not every one is an admin-log text body: "_q
			: u"not every one is an outgoing, local, non-regular "
				u"client-id text message: "_q) + bad.join(u", "_q));
	}
	if (log && (int(state.logItems.size()) != 2 * kEventCount)) {
		missing.push_back(u"%1 new admin-log items for %2 expected"_q.arg(
			NumberText(int(state.logItems.size())),
			NumberText(2 * kEventCount)));
	}
	if (pending) {
		missing.push_back(u"a fixture's layout is still pending"_q);
	}
	GateCheck(
		state,
		kind,
		u"the %1 fixtures are injected while the list is shown and laid "
		u"out in its own views"_q.arg(tag),
		missing,
		u"count=%1 shaped=%2 shown=%3 first=%4 last=%5 | %6"_q.arg(
			NumberText(count),
			NumberText(shaped),
			NumberText(shown),
			count ? IdText(ids.front()) : u"none"_q,
			count ? IdText(ids.back()) : u"none"_q,
			state.wait.details));
}

// Destroys the local messages of one list by id, while it is shown, so
// the next list only ever shows its own fixtures.
void DestroyLocal(
		const ShowSelfTest &state,
		const std::vector<FullMsgId> &ids) {
	for (auto i = ids.size(); i != 0;) {
		if (const auto item = Message(state, ids[--i])) {
			item->destroy();
		}
	}
}

void CheckDestroyed(
		const ShowSelfTest &state,
		const std::vector<FullMsgId> &ids,
		const QString &tag) {
	auto left = 0;
	for (const auto &id : ids) {
		if (Message(state, id)) {
			++left;
		}
	}
	Check(
		!left,
		u"chat-list show self-test: the %1 fixtures are destroyed by id "
		u"before the next list opens"_q.arg(tag),
		u"ids=%1 left=%2"_q.arg(
			NumberText(int(ids.size())),
			NumberText(left)));
}

// The out-of-view control's target: the first or the last ListWidget
// fixture, whichever lies farther outside the visible message area now.
[[nodiscard]] FullMsgId PickFarTarget(
		const ShowSelfTest &state,
		QWidget *list) {
	const auto &ids = state.fixturesL;
	if (ids.empty()) {
		return FullMsgId();
	}
	const auto first = ReadPlacement(list, Message(state, ids.front()), true);
	const auto last = ReadPlacement(list, Message(state, ids.back()), true);
	return (OutsideDistance(last) > OutsideDistance(first))
		? ids.back()
		: ids.front();
}

// The not-ready control's target: the ListWidget fixture wholly in view
// nearest the middle of the visible message area.
[[nodiscard]] FullMsgId PickNearTarget(
		const ShowSelfTest &state,
		QWidget *list) {
	auto result = FullMsgId();
	auto best = -1;
	for (const auto &id : ModernIds(state)) {
		const auto placement = ReadPlacement(list, Message(state, id), true);
		if (!placement.viewShown
			|| placement.pending
			|| !placement.inVisible
			|| !placement.inClip
			|| !placement.mapped) {
			continue;
		}
		const auto distance = std::abs(placement.rect.center().y()
			- placement.visible.center().y());
		if ((best < 0) || (distance < best)) {
			best = distance;
			result = id;
		}
	}
	return result;
}

// The scroll position that establishes a leg's premise, or -1 when it
// holds already or the leg has none: the list's far end for a far target,
// the end farther from the target otherwise.
[[nodiscard]] int PremiseScroll(
		const ShowLeg &leg,
		const Placement &placement,
		not_null<QWidget*> list,
		not_null<Ui::ElasticScroll*> scroll) {
	const auto height = placement.visible.height();
	const auto alreadyFar = placement.viewShown
		&& (height > 0)
		&& (FarDistance(leg.type, placement.rect, placement.visible)
			>= height);
	const auto away = (placement.rect.center().y() < (list->height() / 2))
		? scroll->scrollTopMax()
		: 0;
	switch (leg.type) {
	case LegType::FarAbove: return alreadyFar ? -1 : scroll->scrollTopMax();
	case LegType::FarBelow: return alreadyFar ? -1 : 0;
	case LegType::LaterLayout:
		return (placement.viewShown && placement.inVisible) ? away : -1;
	case LegType::OutOfView:
		return (OutsideDistance(placement) >= height) ? -1 : away;
	case LegType::CannotFit:
	case LegType::NotReady:
	case LegType::NoView: break;
	}
	return -1;
}

// The leg's fixture action: picks its target, resets its probe and, only
// when its premise does not hold yet, scrolls the owning scroll once. The
// probe's delay starts at its first eligible call, never here.
void PrepareLeg(ShowSelfTest &state, ShowLeg &leg) {
	StartWait(leg.wait);
	leg.list = ListFor(state, leg.kind);
	leg.probe = ReadyProbe{
		.delay = kProbeDelay,
		.never = (leg.type == LegType::NotReady),
	};
	const auto list = leg.list.data();
	const auto &fixtures = FixturesFor(state, leg.kind);
	switch (leg.type) {
	case LegType::FarAbove:
		leg.target = fixtures.empty() ? FullMsgId() : fixtures.front();
		break;
	case LegType::FarBelow:
		leg.target = fixtures.empty() ? FullMsgId() : fixtures.back();
		break;
	case LegType::LaterLayout: leg.target = state.shortL; break;
	case LegType::CannotFit: leg.target = state.tallL; break;
	case LegType::OutOfView: leg.target = PickFarTarget(state, list); break;
	case LegType::NotReady: leg.target = PickNearTarget(state, list); break;
	case LegType::NoView: leg.target = EnsureUnattached(state); break;
	}
	const auto scroll = list ? ChatListShowOwningScroll(list) : nullptr;
	if (!scroll) {
		return;
	}
	const auto placement = ReadPlacement(
		list,
		Message(state, leg.target),
		leg.fullView);
	const auto to = PremiseScroll(leg, placement, list, scroll);
	if (to < 0) {
		return;
	}
	const auto from = scroll->scrollTop();
	leg.scrolledForPremise = true;
	scroll->scrollToY(to);
	Note(LegPrefix(leg) + u"prepare scrolled from %1 to %2 (now %3) to "
		u"establish the premise: %4"_q.arg(
			NumberText(from),
			NumberText(to),
			NumberText(scroll->scrollTop()),
			PlacementText(placement)));
}

// Pure but for the wait's own bookkeeping: the target's placement the
// same for kSettle with nothing pending. A list or target already gone
// ends the wait at once, for the gate to name.
[[nodiscard]] bool LegPrepared(ShowSelfTest &state, ShowLeg &leg) {
	if (leg.type == LegType::NoView) {
		return true;
	}
	const auto placement = ReadPlacement(
		leg.list.data(),
		Message(state, leg.target),
		leg.fullView);
	if (!placement.listAlive || !placement.itemAlive) {
		leg.wait.details = PlacementText(placement);
		return true;
	}
	const auto sample = PlacementText(placement);
	leg.wait.details = sample;
	const auto settled = SampleSettled(leg.wait, sample);
	return (settled && placement.viewShown && !placement.pending)
		|| BoundReached(leg.wait, kPrepareBound);
}

[[nodiscard]] QString GrowthText(const ShowLeg &leg) {
	const auto &growth = leg.growth;
	const auto fields = QStringList{
		u"leg=%1"_q.arg(leg.name),
		u"applied=%1"_q.arg(FlagText(growth.applied)),
		u"lines=%1"_q.arg(NumberText(growth.lines)),
		u"lineHeight=%1"_q.arg(NumberText(growth.lineHeight)),
		u"helperGeneration=%1"_q.arg(NumberText(growth.generation)),
		u"helperScrollRequests=%1"_q.arg(NumberText(growth.scrollRequests)),
		u"retries=%1"_q.arg(NumberText(growth.retries)),
		u"heightBefore=%1"_q.arg(NumberText(growth.before.element.height())),
		u"heightAfter=%1"_q.arg(NumberText(growth.after.element.height())),
		u"before={%1}"_q.arg(PlacementText(growth.before)),
		u"after={%1}"_q.arg(PlacementText(growth.after)),
	};
	return u"CHAT_LIST_SHOW_SELFTEST_GROWTH: "_q + fields.join(u" "_q);
}

// Reads the grown target once the product's layout ran: right away when
// its resize was synchronous, otherwise a few turns later.
void ReadGrowth(ShowSelfTest &state, ShowLeg &leg) {
	auto &growth = leg.growth;
	const auto list = leg.list.data();
	growth.after = ReadPlacement(
		list,
		Message(state, leg.target),
		leg.fullView);
	if (growth.after.pending
		&& list
		&& (growth.retries < kGrowthReadRetries)) {
		++growth.retries;
		const auto weak = state.self;
		const auto raw = &leg;
		InvokeQueued(list, [=] {
			if (const auto strong = weak.lock()) {
				ReadGrowth(*strong, *raw);
			}
		});
		return;
	}
	Note(GrowthText(leg));
}

// The one-shot later layout change: the target's text grows to about four
// fifths of the visible message area, by the measured area, the target's
// height and the message text's font height, so it still fits but its
// bottom leaves the area at the centred placement the helper's first
// scroll gave it. HistoryItem::setText already refreshes an item that had
// text; the explicit request keeps the growth independent of that.
void ApplyGrowth(ShowSelfTest &state, ShowLeg &leg) {
	auto &growth = leg.growth;
	growth.lifetime.destroy();
	if (!base::take(growth.queued)) {
		return;
	}
	const auto list = leg.list.data();
	const auto item = Message(state, leg.target);
	growth.before = ReadPlacement(list, item, leg.fullView);
	if (!item
		|| !growth.before.viewShown
		|| !leg.shown
		|| leg.shown->finished) {
		Note(LegPrefix(leg) + u"the queued text growth found no shown "
			u"target and changed nothing: "_q
			+ PlacementText(growth.before));
		return;
	}
	growth.lineHeight = std::max(st::messageTextStyle.font->height, 1);
	const auto wanted = (4 * growth.before.visible.height()) / 5;
	const auto extra = (wanted - growth.before.element.height())
		/ growth.lineHeight;
	growth.lines = 1 + std::max(extra, 2);
	growth.generation = leg.shown->generation;
	growth.scrollRequests = leg.shown->scrollRequests;
	growth.at = crl::now();
	item->setText(tr::marked(LinesText(u"grown"_q, growth.lines)));
	item->history()->owner().requestItemTextRefresh(item);
	growth.applied = true;
	ReadGrowth(state, leg);
}

// Runs inside the owning scroll's scrollTopChanges(), in the helper's
// first scroll: it only disarms and queues, so the growth comes after
// that scroll's handlers, in the next turn, which also ends the
// subscription.
void QueueGrowth(
		const std::weak_ptr<ShowSelfTest> &weak,
		not_null<ShowLeg*> leg) {
	auto &growth = leg->growth;
	const auto list = leg->list.data();
	if (!base::take(growth.armed) || !list) {
		return;
	}
	growth.queued = true;
	InvokeQueued(list, [=] {
		if (const auto strong = weak.lock()) {
			ApplyGrowth(*strong, *leg);
		}
	});
}

void ArmGrowth(ShowSelfTest &state, ShowLeg &leg) {
	const auto list = leg.list.data();
	const auto scroll = list ? ChatListShowOwningScroll(list) : nullptr;
	if (!scroll) {
		return;
	}
	auto &growth = leg.growth;
	growth.armed = true;
	const auto weak = state.self;
	const auto raw = &leg;
	scroll->scrollTopChanges() | rpl::on_next([=](int) {
		QueueGrowth(weak, raw);
	}, growth.lifetime);
	Note(LegPrefix(leg) + u"armed the one-shot text growth on the owning "
		u"scroll's next scroll, the helper's first"_q);
}

void DisableLegScroll(ShowSelfTest &state, ShowLeg &leg) {
	const auto list = leg.list.data();
	const auto scroll = list ? ChatListShowOwningScroll(list) : nullptr;
	if (!scroll) {
		return;
	}
	scroll->disableScroll(true);
	state.disabledScroll = scroll;
	Note(LegPrefix(leg) + u"disabled the owning scroll at scrollTop=%1 "
		u"until this leg's judge"_q.arg(NumberText(scroll->scrollTop())));
}

// Re-enables the scroll the out-of-view control disabled. Idempotent;
// answers whether a scroll was enabled again.
bool EnableLegScroll(ShowSelfTest &state) {
	const auto scroll = state.disabledScroll.data();
	state.disabledScroll = nullptr;
	if (!scroll) {
		return false;
	}
	scroll->disableScroll(false);
	return true;
}

void CheckPrepared(ShowSelfTest &state, ShowLeg &leg) {
	const auto list = leg.list.data();
	const auto item = Message(state, leg.target);
	const auto placement = ReadPlacement(list, item, leg.fullView);
	leg.prepared = placement;
	const auto height = placement.visible.height();
	const auto laidOut = placement.viewShown
		&& !placement.pending
		&& (height > 0);
	auto ok = false;
	auto what = QString();
	auto extra = QString();
	switch (leg.type) {
	case LegType::FarAbove:
	case LegType::FarBelow: {
		const auto distance = FarDistance(
			leg.type,
			placement.rect,
			placement.visible);
		ok = laidOut
			&& (distance >= height)
			&& (placement.rect.height() <= height);
		what = u"the target is laid out, fits, and lies at least one "
			u"visible message area height %1 that area"_q.arg(
				(leg.type == LegType::FarAbove)
					? u"above"_q
					: u"below"_q);
		extra = u"distance=%1"_q.arg(NumberText(distance));
	} break;
	case LegType::LaterLayout:
		ok = laidOut
			&& !placement.inVisible
			&& ((3 * placement.rect.height()) < height);
		what = u"the short target is laid out, not wholly in view and "
			u"under a third of the visible message area tall"_q;
		break;
	case LegType::CannotFit:
		ok = laidOut && (placement.rect.height() > height);
		what = u"the tall target is laid out taller than the visible "
			u"message area"_q;
		extra = u"tallLines=%1 lineHeight=%2"_q.arg(
			NumberText(state.tallLines),
			NumberText(st::messageTextStyle.font->height));
		break;
	case LegType::OutOfView: {
		const auto distance = OutsideDistance(placement);
		ok = laidOut
			&& (distance >= height)
			&& (placement.rect.height() <= height);
		what = u"the target is laid out, fits, and lies at least one "
			u"visible message area height outside that area"_q;
		extra = u"distance=%1"_q.arg(NumberText(distance));
	} break;
	case LegType::NotReady:
		ok = laidOut
			&& placement.inVisible
			&& placement.inClip
			&& placement.mapped;
		what = u"the near target is laid out wholly in view"_q;
		break;
	case LegType::NoView:
		ok = placement.listAlive
			&& placement.scrollOwned
			&& item
			&& !placement.viewShown
			&& !item->mainView()
			&& !item->isHistoryEntry()
			&& LocalShape(item);
		what = u"the unattached local message is registered in the "
			u"session, in no history block, and the list has no view of "
			u"its own for it"_q;
		extra = item ? FixtureRow(item, leg.tag) : u"item=gone"_q;
		break;
	}
	const auto full = LegPrefix(leg) + u"fixture gate: "_q + what;
	Check(
		ok,
		full,
		u"target=%1 scrolledForPremise=%2 %3 %4 | %5"_q.arg(
			IdText(leg.target),
			FlagText(leg.scrolledForPremise),
			extra,
			PlacementText(placement),
			leg.wait.details));
	if (!ok) {
		leg.gate = full;
		return;
	} else if (leg.type == LegType::LaterLayout) {
		ArmGrowth(state, leg);
	} else if (leg.type == LegType::OutOfView) {
		DisableLegScroll(state, leg);
	}
}

// The judge's one row: the prepared and the current independent readings,
// the probe and the helper's own row.
[[nodiscard]] QString SelfTestRow(const ShowLeg &leg, const Placement &now) {
	return u"CHAT_LIST_SHOW_SELFTEST: leg=%1 type=%2 kind=%3 target=%4 "
		u"scrolledForPremise=%5 prepared={%6} judge={%7} %8 | %9"_q.arg(
			leg.name,
			LegTypeName(leg.type),
			ChatListShowKindName(leg.kind),
			IdText(leg.target),
			FlagText(leg.scrolledForPremise),
			PlacementText(leg.prepared),
			PlacementText(now),
			ProbeText(leg.probe),
			ChatListShowText(*leg.shown));
}

// The owning scroll's visible message area, grabbed through the section
// that paints the chat background behind the list, the scroll's parent.
void CaptureLeg(const ShowLeg &leg) {
	const auto list = leg.list.data();
	const auto scroll = list ? ChatListShowOwningScroll(list) : nullptr;
	const auto owner = scroll ? scroll->parentWidget() : nullptr;
	const auto name = u"chat_show_"_q + leg.tag;
	if (!owner) {
		Fail(
			u"capture %1"_q.arg(name),
			u"no list, owning scroll or section to capture"_q);
		return;
	}
	const auto visible = ChatListShowVisibleArea(list);
	const auto inScroll = Ui::MapFrom(
		scroll,
		list,
		visible).intersected(scroll->rect());
	CaptureMappedRect(owner, scroll, inScroll, name);
}

struct HoverJudgement {
	QString text;
	bool ok = false;
};

// The hover re-read now: the point the helper hovered, mapped through the
// view's current origin, is the view's Element::innerGeometry() centre,
// inside the shown rect, and the list resolves exactly this view there.
[[nodiscard]] HoverJudgement JudgeHover(
		const ShowLeg &leg,
		const Placement &now,
		Element *view) {
	const auto &reading = *leg.shown;
	const auto list = leg.list.data();
	if (!view || !list) {
		return { u"no own view to judge the hover on"_q };
	}
	const auto expected = view->innerGeometry().center();
	const auto local = reading.hover - now.element.topLeft();
	const auto inside = (view->pointState(local)
		!= HistoryView::PointState::Outside);
	const auto under = view->isUnderCursor();
	const auto ok = reading.hoverSent
		&& reading.hovered
		&& (reading.hoverLocal == expected)
		&& (local == expected)
		&& now.rect.contains(reading.hover)
		&& under
		&& inside;
	const auto fields = QStringList{
		u"hover=%1"_q.arg(PointText(reading.hover)),
		u"hoverLocal=%1"_q.arg(PointText(reading.hoverLocal)),
		u"nowLocal=%1"_q.arg(PointText(local)),
		u"expectedLocal=%1"_q.arg(PointText(expected)),
		u"underCursor=%1"_q.arg(FlagText(under)),
		u"pointState=%1"_q.arg(inside ? u"inside"_q : u"outside"_q),
		u"moused=%1"_q.arg(FlagText(Element::Moused() == view)),
		u"hovered=%1"_q.arg(FlagText(Element::Hovered() == view)),
		u"mouseActive=%1"_q.arg(ChatListShowMouseActiveText(list)),
		u"activeWindow=%1"_q.arg(
			FlagText(list->window()->isActiveWindow())),
		u"helper={%1}"_q.arg(reading.hoverDetails),
	};
	return { fields.join(u" "_q), ok };
}

void JudgeGrowth(const ShowLeg &leg, const Placement &now) {
	const auto &reading = *leg.shown;
	const auto &growth = leg.growth;
	const auto prefix = LegPrefix(leg);
	const auto before = growth.before.element.height();
	const auto after = growth.after.element.height();
	Check(
		growth.applied
			&& growth.before.viewShown
			&& growth.after.viewShown
			&& !growth.after.pending
			&& (after > before)
			&& (growth.after.rect.height() < growth.after.visible.height())
			&& !growth.after.inVisible
			&& (growth.scrollRequests > 0),
		prefix + u"after the helper's first scroll the queued text growth "
			u"laid the target out taller, still shorter than the visible "
			u"message area, and moved it out of view"_q,
		GrowthText(leg));
	Check(
		(reading.scrollRequests > growth.scrollRequests)
			&& (reading.scrolls >= 2)
			&& (reading.generation > growth.generation)
			&& now.viewShown
			&& (now.element.height() == after)
			&& (leg.probe.height == after)
			&& (leg.probe.firstAt >= growth.at),
		prefix + u"the helper scrolled again from the displaced geometry "
			u"and accepted the grown view after a fresh paint and "
			u"readiness"_q,
		u"scrollRequests=%1 atGrowth=%2 scrolls=%3 generation=%4 "
			u"atGrowth=%5 nowHeight=%6 grownHeight=%7 "
			u"probeAfterGrowth=%8 %9"_q.arg(
				NumberText(reading.scrollRequests),
				NumberText(growth.scrollRequests),
				NumberText(reading.scrolls),
				NumberText(reading.generation),
				NumberText(growth.generation),
				NumberText(now.element.height()),
				NumberText(after),
				FlagText(leg.probe.firstAt >= growth.at),
				ProbeText(leg.probe)));
}

void JudgeFar(const ShowLeg &leg) {
	const auto &reading = *leg.shown;
	const auto initialHeight = reading.initialViewport.height();
	const auto initial = FarDistance(
		leg.type,
		reading.initialRect,
		reading.initialViewport);
	const auto prepared = FarDistance(
		leg.type,
		leg.prepared.rect,
		leg.prepared.visible);
	Check(
		(initialHeight > 0)
			&& (initial >= initialHeight)
			&& (prepared >= leg.prepared.visible.height())
			&& (reading.scrolls > 0),
		LegPrefix(leg) + u"the target started at least one visible message "
			u"area height %1 that area, and the helper scrolled it "
			u"in"_q.arg((leg.type == LegType::FarAbove)
				? u"above"_q
				: u"below"_q),
		u"initialDistance=%1 initialRect=%2 initialViewport=%3 "
			u"preparedDistance=%4 scrollRequests=%5 scrolls=%6"_q.arg(
				NumberText(initial),
				RectText(reading.initialRect),
				RectText(reading.initialViewport),
				NumberText(prepared),
				NumberText(reading.scrollRequests),
				NumberText(reading.scrolls)));
}

// A success leg's judge, in the run right after the helper's then: reads
// everything again first, then captures, then leaves the list.
void JudgeShown(ShowSelfTest &state, ShowLeg &leg) {
	const auto &reading = *leg.shown;
	const auto list = leg.list.data();
	const auto item = Message(state, leg.target);
	const auto now = ReadPlacement(list, item, leg.fullView);
	const auto view = FindOwnView(list, item);
	const auto prefix = LegPrefix(leg);
	Note(SelfTestRow(leg, now));
	Check(
		reading.shown() && reading.hovered,
		prefix + u"the helper accepted the target with no refusal and "
			u"resolved its hover"_q,
		ChatListShowText(reading));
	if (leg.type == LegType::LaterLayout) {
		JudgeGrowth(leg, now);
	} else {
		JudgeFar(leg);
	}
	const auto same = now.viewShown
		&& (now.viewIdentity == reading.viewIdentity)
		&& (now.element == reading.element)
		&& (now.local == reading.local)
		&& (now.rect == reading.rect)
		&& (now.visible == reading.viewport);
	Check(
		same && now.inVisible && now.inClip && now.mapped && !now.pending,
		prefix + u"re-read independently, the list's own view, origin and "
			u"rect are the accepted ones, and the whole rect lies inside "
			u"the visible message area and the owning scroll with nothing "
			u"pending"_q,
		u"now={%1} accepted={view=%2 element=%3 local=%4 rect=%5 "
			u"viewport=%6}"_q.arg(
				PlacementText(now),
				OrNone(reading.viewIdentity),
				RectText(reading.element),
				RectText(reading.local),
				RectText(reading.rect),
				RectText(reading.viewport)));
	Check(
		same
			&& reading.painted
			&& (reading.paints > 0)
			&& (reading.paintRequests > 0),
		prefix + u"completed list paints covered the accepted rect at the "
			u"geometry it still has"_q,
		u"painted=%1 paints=%2 paintRequests=%3 ignoredPaints=%4 "
			u"generation=%5 sameGeometry=%6"_q.arg(
				FlagText(reading.painted),
				NumberText(reading.paints),
				NumberText(reading.paintRequests),
				NumberText(reading.ignoredPaints),
				NumberText(reading.generation),
				FlagText(same)));
	const auto &probe = leg.probe;
	const auto waited = probe.acceptedAt
		? (probe.acceptedAt - probe.firstAt)
		: crl::time(-1);
	Check(
		reading.ready
			&& (reading.readyFalse > 0)
			&& (probe.falseAnswers > 0)
			&& (reading.readyChecks == probe.calls)
			&& (waited >= probe.delay)
			&& now.viewShown
			&& (probe.view == now.view)
			&& (probe.height == now.element.height()),
		prefix + u"the readiness wait was real: the probe answered false "
			u"first, true only after its delay, and last saw the accepted "
			u"own view at its current height"_q,
		u"%1 readyChecks=%2 readyFalse=%3 nowView=%4 nowHeight=%5"_q.arg(
			ProbeText(probe),
			NumberText(reading.readyChecks),
			NumberText(reading.readyFalse),
			OrNone(now.viewIdentity),
			NumberText(now.element.height())));
	const auto hover = JudgeHover(leg, now, view);
	Check(
		hover.ok,
		prefix + u"re-read now, the list resolves exactly the target's "
			u"content under the hover point"_q,
		hover.text);
	CaptureLeg(leg);
	if (const auto widget = leg.list.data()) {
		DeliverLeave(widget);
	}
}

// A refusal leg judge's first read, before any of its Checks: the
// target's current placement, Noted with the leg's row.
[[nodiscard]] Placement ReadRefusalLeg(
		const ShowSelfTest &state,
		const ShowLeg &leg) {
	const auto now = ReadPlacement(
		leg.list.data(),
		Message(state, leg.target),
		leg.fullView);
	Note(SelfTestRow(leg, now));
	return now;
}

void JudgeNoView(ShowSelfTest &state, ShowLeg &leg) {
	const auto &reading = *leg.shown;
	const auto now = ReadRefusalLeg(state, leg);
	const auto item = Message(state, leg.target);
	const auto prefix = LegPrefix(leg);
	Check(
		reading.finished
			&& (reading.refusal == ChatListShowRefusal::NoView)
			&& (ChatListShowRefusalName(reading.refusal) == u"no-view"_q)
			&& !reading.reason.isEmpty()
			&& (reading.kind == ChatListShowKindName(leg.kind))
			&& (reading.item == leg.target)
			&& !reading.listIdentity.isEmpty()
			&& !reading.viewport.isEmpty(),
		prefix + u"the helper refused no-view by name, naming the list "
			u"kind, the message and the list's visible message area"_q,
		ChatListShowText(reading));
	Check(
		now.listAlive
			&& now.itemAlive
			&& !now.viewShown
			&& item
			&& !item->mainView(),
		prefix + u"re-read now, the message is still registered and the "
			u"list still has no view of its own for it"_q,
		PlacementText(now));
	Check(
		!reading.scrollRequests
			&& !reading.scrolls
			&& (reading.scrollTop == leg.prepared.scrollTop)
			&& (now.scrollTop == leg.prepared.scrollTop)
			&& (reading.elapsedMs < reading.timeout),
		prefix + u"nothing was scrolled, the refusal came before the "
			u"helper's deadline, and this judge ran with no stage "
			u"timeout"_q,
		u"scrollRequests=%1 scrolls=%2 scrollTop=%3 preparedScrollTop=%4 "
			u"nowScrollTop=%5 elapsedMs=%6 timeout=%7"_q.arg(
				NumberText(reading.scrollRequests),
				NumberText(reading.scrolls),
				NumberText(reading.scrollTop),
				NumberText(leg.prepared.scrollTop),
				NumberText(now.scrollTop),
				NumberText(reading.elapsedMs),
				NumberText(reading.timeout)));
}

void JudgeCannotFit(ShowSelfTest &state, ShowLeg &leg) {
	const auto &reading = *leg.shown;
	const auto now = ReadRefusalLeg(state, leg);
	const auto prefix = LegPrefix(leg);
	Check(
		reading.finished
			&& (reading.refusal == ChatListShowRefusal::CannotFit)
			&& (ChatListShowRefusalName(reading.refusal)
				== u"cannot-fit"_q)
			&& !reading.reason.isEmpty(),
		prefix + u"the helper refused cannot-fit by name"_q,
		ChatListShowText(reading));
	Check(
		now.viewShown
			&& !now.pending
			&& (now.rect == reading.rect)
			&& (now.rect.height() > now.visible.height())
			&& (reading.rect.height() > reading.viewport.height())
			&& reading.settled
			&& !reading.pending,
		prefix + u"re-read now, the settled and laid-out full view is "
			u"taller than the visible message area the refusal names"_q,
		u"tallLines=%1 lineHeight=%2 now={%3}"_q.arg(
			NumberText(state.tallLines),
			NumberText(st::messageTextStyle.font->height),
			PlacementText(now)));
	Check(
		reading.viewShown
			&& !reading.rect.isEmpty()
			&& !reading.viewport.isEmpty()
			&& (reading.elapsedMs < reading.timeout),
		prefix + u"the refusal carries the last rect, viewport and layout "
			u"state and came before the helper's deadline; this judge ran "
			u"with no stage timeout"_q,
		u"rect=%1 viewport=%2 pending=%3 settled=%4 elapsedMs=%5 "
			u"timeout=%6"_q.arg(
				RectText(reading.rect),
				RectText(reading.viewport),
				FlagText(reading.pending),
				FlagText(reading.settled),
				NumberText(reading.elapsedMs),
				NumberText(reading.timeout)));
}

// Reads first, then enables the scroll again at once.
void JudgeOutOfView(ShowSelfTest &state, ShowLeg &leg) {
	const auto &reading = *leg.shown;
	const auto now = ReadRefusalLeg(state, leg);
	const auto enabled = EnableLegScroll(state);
	const auto prefix = LegPrefix(leg);
	Check(
		reading.finished
			&& (reading.refusal == ChatListShowRefusal::OutOfView)
			&& (ChatListShowRefusalName(reading.refusal)
				== u"out-of-view"_q)
			&& !reading.reason.isEmpty(),
		prefix + u"the helper refused out-of-view by name"_q,
		ChatListShowText(reading));
	Check(
		now.viewShown
			&& !now.inVisible
			&& (now.rect.height() <= now.visible.height())
			&& (OutsideDistance(leg.prepared)
				>= leg.prepared.visible.height()),
		prefix + u"the fitting target lay at least one visible message "
			u"area height outside that area and stayed outside"_q,
		u"preparedDistance=%1 now={%2}"_q.arg(
			NumberText(OutsideDistance(leg.prepared)),
			PlacementText(now)));
	Check(
		(reading.scrollRequests > 0)
			&& !reading.scrolls
			&& (reading.scrollTop == leg.prepared.scrollTop)
			&& (now.scrollTop == leg.prepared.scrollTop),
		prefix + u"the helper asked the disabled scroll for corrections "
			u"and it never moved"_q,
		u"scrollRequests=%1 scrolls=%2 scrollTop=%3 preparedScrollTop=%4 "
			u"nowScrollTop=%5"_q.arg(
				NumberText(reading.scrollRequests),
				NumberText(reading.scrolls),
				NumberText(reading.scrollTop),
				NumberText(leg.prepared.scrollTop),
				NumberText(now.scrollTop)));
	Check(
		(reading.elapsedMs >= reading.timeout)
			&& !reading.rect.isEmpty()
			&& !reading.viewport.isEmpty()
			&& enabled,
		prefix + u"the refusal came at the helper's deadline with the last "
			u"geometry, this judge ran with no stage timeout, and the "
			u"scroll is enabled again"_q,
		u"elapsedMs=%1 timeout=%2 rect=%3 viewport=%4 reenabled=%5"_q.arg(
			NumberText(reading.elapsedMs),
			NumberText(reading.timeout),
			RectText(reading.rect),
			RectText(reading.viewport),
			FlagText(enabled)));
}

void JudgeNotReady(ShowSelfTest &state, ShowLeg &leg) {
	const auto &reading = *leg.shown;
	const auto now = ReadRefusalLeg(state, leg);
	const auto prefix = LegPrefix(leg);
	Check(
		reading.finished
			&& (reading.refusal == ChatListShowRefusal::NotReady)
			&& (ChatListShowRefusalName(reading.refusal)
				== u"not-ready"_q)
			&& reading.reason.contains(
				u"the caller's readiness stayed false"_q),
		prefix + u"the helper refused not-ready by name, naming the "
			u"caller's readiness as the gate still false"_q,
		ChatListShowText(reading));
	Check(
		reading.inView
			&& reading.settled
			&& reading.painted
			&& !reading.pending
			&& (reading.readyChecks > 0)
			&& (reading.readyFalse == reading.readyChecks)
			&& (leg.probe.calls == reading.readyChecks)
			&& leg.probe.never
			&& now.inVisible,
		prefix + u"in view, settled and painted, so the caller's readiness "
			u"was asked, and it answered false every time"_q,
		u"%1 inView=%2 settled=%3 painted=%4 readyChecks=%5 "
			u"readyFalse=%6 now={%7}"_q.arg(
				ProbeText(leg.probe),
				FlagText(reading.inView),
				FlagText(reading.settled),
				FlagText(reading.painted),
				NumberText(reading.readyChecks),
				NumberText(reading.readyFalse),
				PlacementText(now)));
	Check(
		reading.readyDetails.startsWith(u"probe{"_q)
			&& (reading.elapsedMs >= reading.timeout),
		prefix + u"the refusal carries the caller's readiness details and "
			u"came at the helper's deadline; this judge ran with no stage "
			u"timeout"_q,
		u"readiness={%1} elapsedMs=%2 timeout=%3"_q.arg(
			reading.readyDetails,
			NumberText(reading.elapsedMs),
			NumberText(reading.timeout)));
}

void JudgeLeg(ShowSelfTest &state, ShowLeg &leg) {
	switch (leg.type) {
	case LegType::FarAbove:
	case LegType::FarBelow:
	case LegType::LaterLayout: JudgeShown(state, leg); break;
	case LegType::CannotFit: JudgeCannotFit(state, leg); break;
	case LegType::OutOfView: JudgeOutOfView(state, leg); break;
	case LegType::NotReady: JudgeNotReady(state, leg); break;
	case LegType::NoView: JudgeNoView(state, leg); break;
	}
}

[[nodiscard]] std::vector<FullMsgId> LocalIds(const ShowSelfTest &state) {
	auto result = state.fixturesH;
	result.insert(end(result), begin(state.fixturesL), end(state.fixturesL));
	for (const auto &id : { state.tallL, state.shortL, state.unattached }) {
		if (id) {
			result.push_back(id);
		}
	}
	return result;
}

[[nodiscard]] bool WindowMatches(const ShowSelfTest &state) {
	const auto window = WindowOf(state);
	return window
		&& (WithoutActive(window->windowState()) == state.windowState)
		&& (window->isHidden() == state.windowHidden);
}

[[nodiscard]] bool LogItemsGone(const ShowSelfTest &state) {
	for (const auto &id : state.logItems) {
		if (Message(state, id)) {
			return false;
		}
	}
	return true;
}

// Puts the found chat back: the found peer's history, or no chat at all
// when none was open. Background, so nothing is activated. Replacing the
// Recent Actions section lets its AdminLog::OwnedItem owners destroy the
// admin-log items.
void RestoreChat(ShowSelfTest &state) {
	const auto navigation = state.navigation.get();
	if (!navigation) {
		return;
	} else if (state.baselinePeer) {
		navigation->showPeerHistory(
			state.baselinePeer,
			Background(),
			ShowAtUnreadMsgId);
	} else {
		navigation->clearSectionStack(Background());
	}
}

// The one release, run by the teardown stage and again by Runner::onFinish.
// Idempotent: a step already done is only read again. In order: a leave
// on every list the self-test opened, the disabled scroll and the growth
// subscription, the local messages by id, the option, the chat, the
// exposure hint and the window state.
[[nodiscard]] ShowRelease Release(ShowSelfTest &state, const QString &path) {
	auto result = ShowRelease{ .path = path };
	if (!state.captured) {
		result.done.push_back(u"nothing was captured or changed"_q);
		result.itemsGone = true;
		result.listsLeft = true;
		result.scrollEnabled = true;
		result.optionRestored = true;
		result.chatRestored = true;
		return result;
	}
	auto lists = QStringList();
	result.listsLeft = true;
	for (const auto &list : { state.listH, state.listL, state.listA }) {
		const auto widget = list.data();
		if (!widget) {
			continue;
		}
		DeliverLeave(widget);
		if (!list) {
			continue;
		}
		const auto active = ChatListShowMouseActiveText(widget);
		result.listsLeft = result.listsLeft && (active != u"1"_q);
		lists.push_back(u"%1{mouseActive=%2}"_q.arg(
			ChatListShowKindName(ChatListShowKindOf(widget)),
			active));
	}
	result.lists = lists.isEmpty() ? u"none alive"_q : lists.join(u" "_q);
	if (EnableLegScroll(state)) {
		result.done.push_back(u"scroll enabled again"_q);
	}
	result.scrollEnabled = !state.disabledScroll;
	for (const auto &leg : state.legs) {
		leg->growth.armed = false;
		leg->growth.queued = false;
		leg->growth.lifetime.destroy();
	}

	const auto ids = LocalIds(state);
	if (!state.itemsReleased) {
		state.itemsReleased = true;
		for (auto i = ids.size(); i != 0;) {
			if (const auto item = Message(state, ids[--i])) {
				item->destroy();
				++result.destroyed;
			}
		}
		result.done.push_back(u"local messages destroyed"_q);
	}
	auto left = 0;
	for (const auto &id : ids) {
		if (Message(state, id)) {
			++left;
		}
	}
	result.itemsGone = !left;
	result.items = u"ids=%1 destroyed=%2 left=%3 logItems=%4"_q.arg(
		NumberText(int(ids.size())),
		NumberText(result.destroyed),
		NumberText(left),
		NumberText(int(state.logItems.size())));

	auto &option = base::options::lookup<bool>(kOptionUseNewChatView);
	if (state.optionChanged && !state.optionRestored) {
		state.optionRestored = true;
		option.set(state.optionFound);
		result.done.push_back(u"option restored"_q);
	}
	result.optionRestored = (option.value() == state.optionFound);
	result.option = u"use-new-chat-view found=%1 now=%2 changed=%3"_q.arg(
		FlagText(state.optionFound),
		FlagText(option.value()),
		FlagText(state.optionChanged));

	if (state.chatOpened && !state.chatClosed) {
		state.chatClosed = true;
		RestoreChat(state);
		result.done.push_back(u"chat restored"_q);
	}
	const auto navigation = state.navigation.get();
	const auto peer = navigation
		? navigation->activeChatCurrent().peer()
		: nullptr;
	const auto active = peer ? peer->id : PeerId();
	result.chatRestored = navigation && (active == state.baselinePeer);
	result.chat = u"found={%1} now=peer=%2 opened=%3"_q.arg(
		state.baselineChat,
		NumberText(qint64(active.value)),
		FlagText(state.chatOpened));

	if (state.exposureKept && !state.exposureRestored) {
		state.exposureRestored = true;
		RestoreWindowExposure(state.exposure);
		result.done.push_back(u"exposure hint restored"_q);
	}
	const auto window = WindowOf(state);
	if (state.shownNormal && !state.windowRestored && window) {
		state.windowRestored = true;
		state.restoredAt = crl::now();
		window->setWindowState(state.windowState);
		result.done.push_back(u"window state restored"_q);
	}
	result.window = WindowText(state);
	return result;
}

[[nodiscard]] QString ReleaseText(const ShowRelease &release) {
	const auto fields = QStringList{
		u"path=%1"_q.arg(release.path),
		u"done=[%1]"_q.arg(release.done.join(u", "_q)),
		u"items={%1}"_q.arg(release.items),
		u"lists={%1}"_q.arg(release.lists),
		u"scrollEnabled=%1"_q.arg(FlagText(release.scrollEnabled)),
		u"option={%1}"_q.arg(release.option),
		u"chat={%1}"_q.arg(release.chat),
		u"window={%1}"_q.arg(release.window),
	};
	return fields.join(u" "_q);
}

void CheckRelease(const ShowSelfTest &state) {
	const auto &r = state.release;
	const auto prefix = u"chat-list show self-test: teardown - "_q;
	Check(
		r.itemsGone,
		prefix + u"every local message the self-test made, the unattached "
			u"one included, is destroyed by id"_q,
		r.items);
	Check(
		r.listsLeft && r.scrollEnabled,
		prefix + u"no list the self-test opened keeps a mouse-active hover, "
			u"and no scroll stays disabled"_q,
		r.lists + u" scrollEnabled="_q + FlagText(r.scrollEnabled));
	Check(
		r.optionRestored,
		prefix + u"the use-new-chat-view option is back to the value it "
			u"was found with"_q,
		r.option);
	Check(
		r.chatRestored,
		prefix + u"the chat found at the baseline (or none) is shown "
			u"again"_q,
		r.chat);
	Note(prefix + ReleaseText(r));
}

[[nodiscard]] QString SettleText(const ShowSelfTest &state) {
	const auto exposure = ReadWindowExposure(WindowOf(state));
	return u"%1 logItemsGone=%2 restored=%3 stayOnTop{found=%4 now=%5}"_q
		.arg(
			WindowText(state),
			FlagText(LogItemsGone(state)),
			FlagText(state.windowRestored),
			FlagText(state.exposure.stayOnTop),
			FlagText(exposure.stayOnTop));
}

[[nodiscard]] bool RestoreSettled(ShowSelfTest &state) {
	const auto since = state.restoredAt
		? state.restoredAt
		: state.wait.started;
	state.wait.details = SettleText(state);
	return (((crl::now() - since) >= kRepaintSettle)
			&& WindowMatches(state)
			&& LogItemsGone(state))
		|| BoundReached(state.wait, kRestoreBound);
}

void CheckSettled(const ShowSelfTest &state) {
	const auto prefix = u"chat-list show self-test: settle - "_q;
	const auto details = SettleText(state);
	Check(
		LogItemsGone(state),
		prefix + u"replacing the Recent Actions section let its "
			u"AdminLog::OwnedItem owners destroy every admin-log item the "
			u"self-test's events made"_q,
		details);
	Check(
		WindowMatches(state),
		prefix + u"the main window's state is the captured one again and "
			u"held it through the repaint settle"_q,
		details);
	const auto window = WindowOf(state);
	Check(
		!state.exposureKept
			|| (window
				&& (ReadWindowExposure(window).stayOnTop
					== state.exposure.stayOnTop)),
		prefix + u"the window's stacking hint is the one it was found "
			u"with"_q,
		details);
}

struct LegSpec {
	QString name;
	QString tag;
	ListKind kind = ListKind::None;
	LegType type = LegType::FarAbove;
	bool fullView = false;
	crl::time timeout = kShowTimeout;
};

// One leg: a prepare stage that establishes and measures the premise, the
// helper's stage, and a judge in the very next run. The success legs ask
// for a hover at their Element::innerGeometry() centre; the refusal legs
// send no input.
void AppendLeg(
		not_null<Runner*> runner,
		const std::shared_ptr<ShowSelfTest> &state,
		LegSpec spec) {
	auto owned = std::make_unique<ShowLeg>();
	const auto leg = owned.get();
	leg->name = std::move(spec.name);
	leg->tag = std::move(spec.tag);
	leg->kind = spec.kind;
	leg->type = spec.type;
	leg->fullView = spec.fullView;
	leg->timeout = spec.timeout;
	leg->hover = (spec.type == LegType::FarAbove)
		|| (spec.type == LegType::FarBelow)
		|| (spec.type == LegType::LaterLayout);
	state->legs.push_back(std::move(owned));

	const auto kindGate = [=] {
		return KindGate(*state, leg->kind);
	};
	const auto legGate = [=] {
		return LegGate(*state, *leg);
	};
	runner->add({
		.name = LegPrefix(*leg) + u"prepare"_q,
		.skipReason = kindGate,
		.run = [=] {
			PrepareLeg(*state, *leg);
		},
		.until = [=] {
			return LegPrepared(*state, *leg);
		},
		.then = [=] {
			CheckPrepared(*state, *leg);
		},
		.timeout = kPrepareBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return leg->wait.details;
		},
	});
	auto rect = Fn<QRect(ChatListShowView)>();
	if (!leg->fullView) {
		rect = [](ChatListShowView view) {
			return view->innerGeometry();
		};
	}
	auto hover = Fn<QPoint(ChatListShowView)>();
	if (leg->hover) {
		hover = [](ChatListShowView view) {
			return view->innerGeometry().center();
		};
	}
	leg->shown = AppendShowInChatList(runner, {
		.name = u"chat-list show self-test: "_q + leg->name,
		.resolve = [=] {
			return ChatListShowTarget{ leg->list.data(), leg->target };
		},
		.rect = std::move(rect),
		.ready = [=](ChatListShowView view) {
			return AskProbe(leg->probe, view);
		},
		.readyDetails = [=](ChatListShowView) {
			return ProbeText(leg->probe);
		},
		.hover = std::move(hover),
		.skipReason = legGate,
		.timeout = leg->timeout,
	});
	runner->add({
		.name = LegPrefix(*leg) + u"judge"_q,
		.skipReason = legGate,
		.run = [=] {
			JudgeLeg(*state, *leg);
		},
	});
}

} // namespace

void AppendChatListShowSelfTest(not_null<Runner*> runner, PeerId channel) {
	const auto state = std::make_shared<ShowSelfTest>();
	state->self = state;
	state->channel = channel;

	// Registered before anything is changed: every path that reaches
	// Runner::finish() runs the same idempotent release, which after a
	// normal teardown only reads again.
	runner->onFinish([=] {
		const auto reading = Release(*state, u"onFinish"_q);
		Note(u"chat-list show self-test: onFinish release: "_q
			+ ReleaseText(reading));
	});
	const auto gate = [=] {
		return state->gate;
	};
	const auto gateOf = [=](ListKind kind) {
		return [=] {
			return KindGate(*state, kind);
		};
	};
	const auto captured = [=] {
		return state->captured
			? QString()
			: u"nothing was captured, so nothing was changed"_q;
	};

	runner->add({
		.name = u"chat-list show self-test: capture the baseline and show "
			u"the window normally"_q,
		.run = [=] {
			StartBaseline(*state);
		},
		.until = [=] {
			return WindowShown(*state);
		},
		.then = [=] {
			CheckWindowShown(*state);
		},
		.timeout = kWindowBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return state->wait.details;
		},
	});
	runner->add({
		.name = u"chat-list show self-test: keep the window exposed"_q,
		.skipReason = gate,
		.run = [=] {
			KeepExposed(*state);
		},
		.until = [=] {
			return WindowExposed(*state);
		},
		.then = [=] {
			CheckWindowExposed(*state);
		},
		.timeout = kWindowBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return state->wait.details;
		},
	});

	runner->add({
		.name = u"chat-list show self-test: open the channel in the "
			u"legacy HistoryWidget"_q,
		.skipReason = gate,
		.run = [=] {
			OpenChannel(*state, false);
		},
		.until = [=] {
			return LegacyOpened(*state);
		},
		.then = [=] {
			CheckLegacyOpened(*state);
		},
		.timeout = kOpenBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return state->wait.details;
		},
	});
	runner->add({
		.name = u"chat-list show self-test: inject the HistoryInner "
			u"fixtures"_q,
		.skipReason = gateOf(ListKind::History),
		.run = [=] {
			InjectHistoryFixtures(*state);
		},
		.until = [=] {
			return FixturesLaidOut(
				*state,
				ListKind::History,
				state->fixturesH);
		},
		.then = [=] {
			CheckFixturesLaidOut(
				*state,
				ListKind::History,
				state->fixturesH,
				kFixtureCount,
				u"HistoryInner"_q);
		},
		.timeout = kLayoutBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return state->wait.details;
		},
	});
	AppendLeg(runner, state, {
		.name = u"HistoryInner far above"_q,
		.tag = u"H_far_above"_q,
		.kind = ListKind::History,
		.type = LegType::FarAbove,
		.fullView = true,
	});
	AppendLeg(runner, state, {
		.name = u"HistoryInner far below"_q,
		.tag = u"H_far_below"_q,
		.kind = ListKind::History,
		.type = LegType::FarBelow,
	});
	AppendLeg(runner, state, {
		.name = u"HistoryInner no view"_q,
		.tag = u"H_no_view"_q,
		.kind = ListKind::History,
		.type = LegType::NoView,
		.fullView = true,
	});
	runner->add({
		.name = u"chat-list show self-test: destroy the HistoryInner "
			u"fixtures"_q,
		.skipReason = gate,
		.run = [=] {
			DestroyLocal(*state, state->fixturesH);
		},
		.then = [=] {
			CheckDestroyed(*state, state->fixturesH, u"HistoryInner"_q);
		},
	});

	runner->add({
		.name = u"chat-list show self-test: open the channel in a "
			u"HistoryView::ChatWidget"_q,
		.skipReason = gate,
		.run = [=] {
			OpenChannel(*state, true);
		},
		.until = [=] {
			return ModernOpened(*state);
		},
		.then = [=] {
			CheckModernOpened(*state);
		},
		.timeout = kOpenBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return state->wait.details;
		},
	});
	runner->add({
		.name = u"chat-list show self-test: inject the ListWidget "
			u"fixtures"_q,
		.skipReason = gateOf(ListKind::Modern),
		.run = [=] {
			InjectModernFixtures(*state);
		},
		.until = [=] {
			return FixturesLaidOut(
				*state,
				ListKind::Modern,
				ModernIds(*state));
		},
		.then = [=] {
			CheckFixturesLaidOut(
				*state,
				ListKind::Modern,
				ModernIds(*state),
				kFixtureCount + 2,
				u"ListWidget"_q);
		},
		.timeout = kLayoutBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return state->wait.details;
		},
	});
	AppendLeg(runner, state, {
		.name = u"ListWidget far above"_q,
		.tag = u"L_far_above"_q,
		.kind = ListKind::Modern,
		.type = LegType::FarAbove,
		.fullView = true,
	});
	AppendLeg(runner, state, {
		.name = u"ListWidget far below"_q,
		.tag = u"L_far_below"_q,
		.kind = ListKind::Modern,
		.type = LegType::FarBelow,
	});
	AppendLeg(runner, state, {
		.name = u"ListWidget later layout"_q,
		.tag = u"L_later_layout"_q,
		.kind = ListKind::Modern,
		.type = LegType::LaterLayout,
		.fullView = true,
	});
	AppendLeg(runner, state, {
		.name = u"ListWidget cannot fit"_q,
		.tag = u"L_cannot_fit"_q,
		.kind = ListKind::Modern,
		.type = LegType::CannotFit,
		.fullView = true,
	});
	AppendLeg(runner, state, {
		.name = u"ListWidget out of view"_q,
		.tag = u"L_out_of_view"_q,
		.kind = ListKind::Modern,
		.type = LegType::OutOfView,
		.fullView = true,
		.timeout = kControlTimeout,
	});
	AppendLeg(runner, state, {
		.name = u"ListWidget not ready"_q,
		.tag = u"L_not_ready"_q,
		.kind = ListKind::Modern,
		.type = LegType::NotReady,
		.fullView = true,
		.timeout = kControlTimeout,
	});
	AppendLeg(runner, state, {
		.name = u"ListWidget no view"_q,
		.tag = u"L_no_view"_q,
		.kind = ListKind::Modern,
		.type = LegType::NoView,
		.fullView = true,
	});

	runner->add({
		.name = u"chat-list show self-test: open the channel's Recent "
			u"Actions"_q,
		.skipReason = gate,
		.run = [=] {
			OpenRecentActions(*state);
		},
		.until = [=] {
			return LogOpened(*state);
		},
		.then = [=] {
			CheckLogOpened(*state);
		},
		.timeout = kOpenBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return state->wait.details;
		},
	});
	runner->add({
		.name = u"chat-list show self-test: inject the Recent Actions "
			u"events"_q,
		.skipReason = gateOf(ListKind::Log),
		.run = [=] {
			InjectLogEvents(*state);
		},
		.until = [=] {
			return FixturesLaidOut(*state, ListKind::Log, state->bodiesA);
		},
		.then = [=] {
			CheckFixturesLaidOut(
				*state,
				ListKind::Log,
				state->bodiesA,
				kEventCount,
				u"Recent Actions"_q);
		},
		.timeout = kLayoutBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return state->wait.details;
		},
	});
	AppendLeg(runner, state, {
		.name = u"Recent Actions far above"_q,
		.tag = u"A_far_above"_q,
		.kind = ListKind::Log,
		.type = LegType::FarAbove,
		.fullView = true,
	});
	AppendLeg(runner, state, {
		.name = u"Recent Actions far below"_q,
		.tag = u"A_far_below"_q,
		.kind = ListKind::Log,
		.type = LegType::FarBelow,
	});
	AppendLeg(runner, state, {
		.name = u"Recent Actions no view"_q,
		.tag = u"A_no_view"_q,
		.kind = ListKind::Log,
		.type = LegType::NoView,
		.fullView = true,
	});

	runner->add({
		.name = u"chat-list show self-test: teardown"_q,
		.skipReason = captured,
		.run = [=] {
			state->release = Release(*state, u"teardown"_q);
		},
		.then = [=] {
			CheckRelease(*state);
		},
	});
	runner->add({
		.name = u"chat-list show self-test: settle after the teardown"_q,
		.skipReason = captured,
		.run = [=] {
			StartWait(state->wait);
		},
		.until = [=] {
			return RestoreSettled(*state);
		},
		.then = [=] {
			CheckSettled(*state);
		},
		.timeout = kRestoreBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return state->wait.details;
		},
	});
}

} // namespace Test

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
