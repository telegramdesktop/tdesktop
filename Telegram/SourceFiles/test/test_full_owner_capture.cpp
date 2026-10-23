/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_full_owner_capture.h"

#include "base/event_filter.h"
#include "base/invoke_queued.h"
#include "base/object_ptr.h"
#include "base/unique_qptr.h"
#include "test/test_capture.h"
#include "test/test_console_lock.h"
#include "test/test_log.h"
#include "test/test_panel.h"
#include "test/test_post_paint.h"
#include "test/test_runner.h"
#include "test/test_window_exposure.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/separate_panel.h"
#include "ui/qt_object_factory.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"

#include <QtCore/QCoreApplication>
#include <QtGui/QLinearGradient>
#include <QtGui/QPainter>

#include <array>

#include "styles/palette.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

namespace Test {
namespace {

// Every size is a multiple of one row, st::defaultActiveButton.height.
constexpr auto kPanelSide = 6;
constexpr auto kChildWide = 3;
constexpr auto kGradientLeft = 1;
constexpr auto kGradientTop = 1;
constexpr auto kGradientMinWide = 4;
constexpr auto kGradientMaxWide = 5;
constexpr auto kScrollTop = 2;
constexpr auto kOverlayTall = 1;
constexpr auto kRows = 5;
constexpr auto kVisibleRow = 1;
constexpr auto kClippedRow = 2;

// The raster linear-gradient fetch steps a truncated fixed-point increment,
// int(inc * 256) with inc scaled to the 1024-entry stop table, from each
// span start; a fraction near one half makes the shifted-span error large.
constexpr auto kStopTableSteps = 1023.;
constexpr auto kFixedPointSize = 256.;
constexpr auto kFractionLow = 0.25;
constexpr auto kFractionHigh = 0.75;

constexpr auto kFixtureSettle = crl::time(3000);

// Group A: the coordinated samplers on the two exact child paint owners.
constexpr auto kSettleQuiet = crl::time(100);
constexpr auto kSettleTimeout = crl::time(3000);
constexpr auto kIdleControl = crl::time(500);
constexpr auto kFirstReceiptWindow = crl::time(3000);
constexpr auto kCoordinatedWindow = crl::time(1200);
constexpr auto kLocalWindow = crl::time(600);
constexpr auto kCoalescedWindow = crl::time(1500);
constexpr auto kReplacementWindow = crl::time(1000);
constexpr auto kDrain = crl::time(150);
// Above one display-link frame plus a blocking flush, below the idle tail.
constexpr auto kSampleBound = crl::time(250);
constexpr auto kIdleTailMin = crl::time(500);
constexpr auto kControlCaptures = 4;
constexpr auto kMinChildCaptures = 2;
constexpr auto kCoalescedPaints = 3;
constexpr auto kCoalescedRows = 2;
constexpr auto kListShown = 8;

// Group B: the strict crop skips the first third of the gradient, so both
// arms' gradient spans start at different device pixels.
constexpr auto kCropSkipDivisor = 3;
constexpr auto kNumberPrecision = 4;
constexpr auto kInvertRgb = QRgb(0x00FFFFFF);

// Group C: native panel geometry settles asynchronously on macOS, so every
// sizing wait is bounded and its stage then reports what it read.
constexpr auto kSizingSettle = crl::time(3000);

const auto kPanelName = u"harness-full-owner-panel"_q;
const auto kPanelTitle = u"Harness full-owner capture panel"_q;
const auto kViewportRefusal = u"not fully inside the relevant viewport"_q;
const auto kCropFixtureGone = u"the panel, the composite owner, its scroll "
	u"or its rows are gone"_q;

enum class Child {
	First,
	Second,
};

// One paint of a child, stamped by its own paint callback. |receipt| is the
// index of the receipt the sampler observing the child recorded for this
// paint (0 while none observes it), |capture| the fixture's own flag that a
// group A action sets around its composite grab, so the records tell
// product paints from capture paints without trusting any sampler.
// |startSeq| and |endSeq| are the fixture sequence (Stamp) taken beside
// |start| and |end|: they, not the milliseconds, order a paint against an
// action call or an arm's marks.
struct PaintRecord {
	Child child = Child::First;
	int generation = 0;
	int receipt = 0;
	int startSeq = 0;
	crl::time start = 0;
	crl::time end = 0;
	int endSeq = 0;
	bool capture = false;
};

struct Fixture {
	base::unique_qptr<Ui::SeparatePanel> panel;
	QPointer<Ui::RpWidget> owner;
	QPointer<Ui::RpWidget> first;
	base::unique_qptr<Ui::RpWidget> second;
	Ui::RpWidget *gradient = nullptr;
	int gradientWidth = 0;
	Ui::ScrollArea *scroll = nullptr;
	Ui::RpWidget *content = nullptr;
	std::array<Ui::RpWidget*, kRows> rows = {};
	Ui::RpWidget *overlay = nullptr;
	std::array<int, 2> generation = {};
	std::array<PostPaintSampler*, 2> observed = {};
	std::vector<PaintRecord> records;
	int sequence = 0;
	bool capturing = false;
	bool repaintOnCapture = false;
};

// One call of a group A sampler's action: the |row| seq the sampler passed
// it, which is that row's PostPaintSample::seq, and the fixture sequence at
// the action's entry and at its exit. The sampler runs the action right
// after it reads the row's lo, so |entry| is "the sample ran".
struct ActionCall {
	int row = 0;
	int entry = 0;
	int exit = 0;
};

// One group A sampler, the child generation it observes and the calls its
// action received, counted and recorded before the action looks at
// anything.
struct Observer {
	std::unique_ptr<PostPaintSampler> sampler;
	QPointer<QWidget> owner;
	std::vector<ActionCall> actions;
	Child child = Child::First;
	int generation = 0;
	int calls = 0;
	int grabs = 0;
	int nullImages = 0;
	bool capped = false;
};

// Taken immediately before an arm's update() calls: the arm's sampling
// window, the request time and fixture sequence, and the ends of the record
// history and of the row and receipt histories of the observers of both
// children.
struct ArmMarks {
	crl::time from = 0;
	crl::time till = 0;
	crl::time requestedAt = 0;
	int requestSeq = 0;
	int records = 0;
	std::array<int, 2> rows = {};
	std::array<int, 2> receipts = {};
};

// What arm A5's destroy call read right before it destroyed the first
// generation of the second child.
struct ReplaceReading {
	int products = 0;
	int rows = 0;
	int calls = 0;
	bool pending = false;
};

// One child generation's view of an arm: its records since the arm's
// mark, the rows, receipts and action calls of its observer since the same
// mark, the reconciliation of records and receipts (empty when they agree)
// and the named refusal of a row without an action call (empty when every
// row has one).
struct ChildReading {
	std::vector<PaintRecord> records;
	std::vector<PaintRecord> products;
	std::vector<PaintRecord> captures;
	std::vector<PostPaintSample> rows;
	std::vector<PostPaintReceipt> receipts;
	std::vector<ActionCall> actions;
	QString reconcile;
	QString unacted;
};

struct PaintGroup {
	std::shared_ptr<PostPaintCaptureScope> scope;
	Observer first;
	Observer second;
	Observer replacement;
	Observer localFirst;
	Observer localSecond;
	ArmMarks marks;
	ReplaceReading atDestroy;
	QPointer<QObject> armFilter;
	QString paintGate;
	crl::time destroyedAt = 0;
	crl::time replacementFrom = 0;
	crl::time replacementTill = 0;
	int stoppedReceipts = 0;
	bool armPosted = false;
};

// The self-test's own pixel count of two images through QImage::pixel(),
// independent of CompareStrict: differing pixels, the largest channel
// delta and the pixels whose largest delta is one level. -1 is "not
// counted".
struct PixelCount {
	int compared = 0;
	int differing = -1;
	int maxChannelDelta = -1;
	int oneLevel = -1;
	QString refusal;
};

// Group B: the gradient crop K in owner coordinates, the full-owner and the
// shifted direct-region renders of B1, their independent count and the
// premise reason B2 skips with when that count is zero.
struct StrictGroup {
	QRect crop;
	StrictRender full;
	StrictRender shifted;
	PixelCount measured;
	QString premise;
};

// Group C: the requested crop E and the clipped row's mapping, both taken
// at the original owner height and reused unchanged after the growth; the
// measured growth, the first sizing's record (the original frame) and what
// the last growth read.
struct CropGroup {
	QRect requested;
	QRect row;
	int ownerHeight = 0;
	int bottomReserve = 0;
	int extra = 0;
	PanelSizing original;
	QRect grownOuter;
	QRect grownBody;
	crl::time waitFrom = 0;
};

// The live group C widgets, all null unless the panel and the owner still
// live: the raw fixture pointers are descendants of the panel.
struct CropWidgets {
	Ui::SeparatePanel *panel = nullptr;
	Ui::RpWidget *owner = nullptr;
	Ui::ScrollArea *scroll = nullptr;
	Ui::RpWidget *visible = nullptr;
	Ui::RpWidget *clipped = nullptr;
};

// A frame a pure C4 arm reads the sizing gate on: the outer size and the
// constraints PanelSizeModeStates decides the effective state from.
struct GateFrame {
	QSize outer;
	QSize minimum;
	QSize maximum;
};

// One pure C4 reading: the effective state of a raw state on a GateFrame,
// the refusal the sizing setup gives it, and both printed.
struct GateReading {
	Qt::WindowStates effective = Qt::WindowNoState;
	QString refusal;
	QString text;
};

// The arms of one C4 Check: their readings and whether every one held.
struct GateArms {
	QStringList texts;
	bool held = true;
};

struct State {
	Fixture fixture;
	PaintGroup paint;
	StrictGroup strict;
	CropGroup crop;
	PanelSizing sizing;
	WindowExposure exposure;
	WindowExposure restoredExposure;
	QString fixtureGate;
	crl::time shownAt = 0;
	bool built = false;
	bool restoredRead = false;
};

[[nodiscard]] int Row() {
	return st::defaultActiveButton.height;
}

[[nodiscard]] int ChildIndex(Child child) {
	return (child == Child::First) ? 0 : 1;
}

[[nodiscard]] QRect ChildGeometry(Child child) {
	const auto row = Row();
	const auto wide = row * kChildWide;
	return QRect(ChildIndex(child) * wide, 0, wide, row);
}

// The smallest width in [4row, 5row] whose device span leaves a truncated
// fraction in [0.25, 0.75], else 4row. Part B still measures the mismatch.
[[nodiscard]] int ChooseGradientWidth(int row) {
	const auto dpr = style::DevicePixelRatio();
	const auto from = row * kGradientMinWide;
	const auto till = row * kGradientMaxWide;
	for (auto width = from; width <= till; ++width) {
		const auto increment = kStopTableSteps
			* kFixedPointSize
			/ (width * dpr);
		const auto fraction = increment - std::floor(increment);
		if (fraction >= kFractionLow && fraction <= kFractionHigh) {
			return width;
		}
	}
	return from;
}

void PaintFill(not_null<Ui::RpWidget*> widget, const style::color &color) {
	widget->setAttribute(Qt::WA_OpaquePaintEvent);
	widget->paintOn([=](QPainter &p) {
		p.fillRect(widget->rect(), color);
	});
}

// The fixture sequence: one counter, incremented at each child paint's
// start and end and at each group A action's entry and exit, and read by
// each arm's marks. It is the exact causal order of those events. crl::now()
// milliseconds decide bounds, windows and quiet periods only: two causally
// ordered events microseconds apart often share one millisecond, so a
// strict comparison of their stamps fails a correct history and a
// non-strict one cannot refuse a wrong order.
[[nodiscard]] int Stamp(not_null<Fixture*> fixture) {
	return ++fixture->sequence;
}

void PaintGradient(not_null<Ui::RpWidget*> widget) {
	widget->setAttribute(Qt::WA_OpaquePaintEvent);
	widget->paintOn([=](QPainter &p) {
		auto gradient = QLinearGradient(0, 0, widget->width(), 0);
		gradient.setColorAt(0., st::windowBgActive->c);
		gradient.setColorAt(1., st::attentionButtonFg->c);
		p.fillRect(widget->rect(), QBrush(gradient));
	});
}

// One exact child paint owner of the composite owner. Its generation is
// fixed when it is set up, so a replacement child stamps its own records.
// It is not shown here: a replacement is shown only after its observer
// exists. The observing sampler's filter runs before this paintEvent, so
// its receiptMark() read here is this paint's receipt index; the fixture
// sequence is stamped at the paint's start and end.
void SetupChild(
		not_null<Fixture*> fixture,
		not_null<Ui::RpWidget*> widget,
		Child child) {
	const auto generation = ++fixture->generation[ChildIndex(child)];
	const auto &color = (child == Child::First)
		? st::windowBgActive
		: st::attentionButtonFg;
	widget->setAttribute(Qt::WA_OpaquePaintEvent);
	widget->setGeometry(ChildGeometry(child));
	widget->paintOn([=](QPainter &p) {
		const auto start = crl::now();
		const auto startSeq = Stamp(fixture);
		const auto observed = fixture->observed[ChildIndex(child)];
		const auto receipt = observed ? observed->receiptMark() : 0;
		p.fillRect(widget->rect(), color);
		const auto capture = fixture->capturing;
		if (capture && base::take(fixture->repaintOnCapture)) {
			widget->update();
		}
		fixture->records.push_back(PaintRecord{
			.child = child,
			.generation = generation,
			.receipt = receipt,
			.startSeq = startSeq,
			.start = start,
			.end = crl::now(),
			.endSeq = Stamp(fixture),
			.capture = capture,
		});
	});
}

// The MakePanel() recipe of test_window_exposure.cpp: the stay-on-top hint
// is cleared before any native window exists, the panel shows without
// activating and never quits the run on close. Never showAndActivate(),
// showInner(), setHideOnDeactivate(false), raise() or activateWindow(): each
// activates the application. The inner size includes the title, so the
// body is kPanelSide rows square.
[[nodiscard]] base::unique_qptr<Ui::SeparatePanel> BuildPanel() {
	const auto row = Row();
	auto result = base::make_unique_q<Ui::SeparatePanel>();
	const auto raw = result.get();
	raw->setWindowFlag(Qt::WindowStaysOnTopHint, false);
	raw->setObjectName(kPanelName);
	raw->setAttribute(Qt::WA_ShowWithoutActivating);
	raw->setAttribute(Qt::WA_QuitOnClose, false);
	raw->setTitle(rpl::single(kPanelTitle));
	raw->setInnerSize(QSize(
		row * kPanelSide,
		st::separatePanelTitleHeight + row * kPanelSide));
	return result;
}

// The composite owner P is a child of the panel itself, never of its
// private body (showInner would activate). Its children: two exact child
// paint owners in the first row, a static horizontal gradient in the
// second, a real Ui::ScrollArea of kRows rows below and a bottom overlay.
// LayoutFixture places P, the scroll and the overlay.
[[nodiscard]] bool BuildFixture(not_null<Fixture*> fixture) {
	const auto row = Row();
	const auto half = row / 2;
	const auto side = row * kPanelSide;

	fixture->panel = BuildPanel();
	const auto panel = fixture->panel.get();
	if (!panel) {
		return false;
	}

	const auto owner = Ui::CreateChild<Ui::RpWidget>(panel);
	PaintFill(owner, st::windowBg);
	fixture->owner = owner;

	const auto first = Ui::CreateChild<Ui::RpWidget>(owner);
	SetupChild(fixture, first, Child::First);
	first->show();
	fixture->first = first;

	fixture->second = base::make_unique_q<Ui::RpWidget>(owner);
	SetupChild(fixture, fixture->second.get(), Child::Second);
	fixture->second->show();

	fixture->gradientWidth = ChooseGradientWidth(row);
	const auto gradient = Ui::CreateChild<Ui::RpWidget>(owner);
	gradient->setGeometry(
		row * kGradientLeft,
		row * kGradientTop,
		fixture->gradientWidth,
		row);
	PaintGradient(gradient);
	gradient->show();
	fixture->gradient = gradient;

	const auto scroll = Ui::CreateChild<Ui::ScrollArea>(
		owner,
		st::defaultScrollArea);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	auto content = object_ptr<Ui::RpWidget>(scroll);
	const auto inner = content.data();
	PaintFill(inner, st::windowBg);
	inner->resize(side, half + kRows * row);
	for (auto i = 0; i != kRows; ++i) {
		const auto item = Ui::CreateChild<Ui::RpWidget>(inner);
		item->setGeometry(0, half + i * row, side, row);
		PaintFill(item, (i % 2) ? st::windowBgOver : st::windowBgActive);
		item->show();
		fixture->rows[i] = item;
	}
	scroll->setOwnedWidget(std::move(content));
	scroll->show();
	fixture->scroll = scroll;
	fixture->content = inner;

	const auto overlay = Ui::CreateChild<Ui::RpWidget>(owner);
	PaintFill(overlay, st::attentionButtonFg);
	overlay->show();
	fixture->overlay = overlay;

	owner->show();
	return true;
}

// Run after the first show and after every sizing or restore settled, never
// from the panel's sizeValue(): that stream fires before
// SeparatePanel::resizeEvent lays out the body, so innerGeometry() would be
// stale there.
void LayoutFixture(not_null<Fixture*> fixture) {
	const auto panel = fixture->panel.get();
	const auto owner = fixture->owner.data();
	const auto scroll = fixture->scroll;
	const auto overlay = fixture->overlay;
	if (!panel || !owner || !scroll || !overlay) {
		return;
	}
	const auto row = Row();
	const auto side = row * kPanelSide;
	owner->setGeometry(panel->innerGeometry());
	const auto height = owner->height();
	scroll->setGeometry(
		0,
		row * kScrollTop,
		side,
		height - row * (kScrollTop + kOverlayTall));
	overlay->setGeometry(
		0,
		height - row * kOverlayTall,
		side,
		row * kOverlayTall);
	Ui::SendPendingMoveResizeEvents(owner);
	Ui::SendPendingMoveResizeEvents(scroll);
	if (const auto content = scroll->widget()) {
		Ui::SendPendingMoveResizeEvents(content);
	}
}

// The sampler goes before the widget it observes: its destructor removes
// its filter from the owner while the owner still lives.
void ReleaseObserver(Fixture &fixture, Observer &observer) {
	const auto sampler = observer.sampler.get();
	for (auto &observed : fixture.observed) {
		if (observed == sampler) {
			observed = nullptr;
		}
	}
	observer.sampler = nullptr;
}

void ReleaseSamplers(State &state) {
	auto &paint = state.paint;
	const auto observers = {
		&paint.localFirst,
		&paint.localSecond,
		&paint.replacement,
		&paint.second,
		&paint.first,
	};
	for (const auto observer : observers) {
		ReleaseObserver(state.fixture, *observer);
	}
	state.fixture.observed = {};
}

// Idempotent, so the teardown stage and Runner::onFinish both run it. The
// order is fixed: samplers before the panel sizing, the sizing before the
// exposure undo, the panel last. The hint is read back after the undo while
// the panel still exists, so the teardown can prove it was cleared.
void ReleaseFixture(State &state) {
	ReleaseSamplers(state);
	auto &fixture = state.fixture;
	RestorePanelSizing(state.sizing);
	RestoreWindowExposure(state.exposure);
	if (const auto panel = fixture.panel.get()) {
		state.restoredExposure = ReadWindowExposure(panel);
		state.restoredRead = true;
	}
	fixture.panel = nullptr;
	fixture.gradient = nullptr;
	fixture.scroll = nullptr;
	fixture.content = nullptr;
	fixture.rows = {};
	fixture.overlay = nullptr;
}

[[nodiscard]] QString RowDetails(const Fixture &fixture) {
	const auto owner = fixture.owner.data();
	const auto visible = fixture.rows[kVisibleRow];
	const auto clipped = fixture.rows[kClippedRow];
	if (!owner || !visible || !clipped) {
		return u"rows: <none>"_q;
	}
	return u"row%1 {%2} row%3 {%4}"_q
		.arg(kVisibleRow)
		.arg(MappedTargetDetails(
			ReadMappedTarget(owner, visible, visible->rect())))
		.arg(kClippedRow)
		.arg(MappedTargetDetails(
			ReadMappedTarget(owner, clipped, clipped->rect())));
}

[[nodiscard]] QString FixtureDetails(const State &state) {
	const auto &fixture = state.fixture;
	const auto panel = fixture.panel.get();
	const auto owner = fixture.owner.data();
	return u"panel=%1 inner=%2 owner=%3 gradientWidth=%4 dpr=%5 "
		u"kept={%6} now={%7} %8"_q
		.arg(panel ? WidgetDescription(panel) : u"<none>"_q)
		.arg(panel ? RectText(panel->innerGeometry()) : u"<none>"_q)
		.arg(owner ? WidgetDescription(owner) : u"<none>"_q)
		.arg(fixture.gradientWidth)
		.arg(style::DevicePixelRatio())
		.arg(WindowExposureText(state.exposure))
		.arg(WindowExposureText(ReadWindowExposure(panel)))
		.arg(RowDetails(fixture));
}

// Exposure is not part of the gate: groups B to D render in-process and
// need only a shown panel, and group A decides paints by its own receipts.
[[nodiscard]] QString FixtureRefusal(const State &state) {
	const auto &fixture = state.fixture;
	const auto panel = fixture.panel.get();
	const auto owner = fixture.owner.data();
	const auto side = Row() * kPanelSide;
	if (!state.built) {
		return u"the fixture was not built"_q;
	} else if (!panel || panel->isHidden()) {
		return u"the panel is gone or hidden"_q;
	} else if (!owner || owner->size() != QSize(side, side)) {
		return u"the composite owner is not %1x%1"_q.arg(side);
	}
	const auto visible = fixture.rows[kVisibleRow];
	const auto clipped = fixture.rows[kClippedRow];
	if (!visible || !clipped) {
		return u"the scroll rows were not built"_q;
	}
	const auto shown = ReadMappedTarget(owner, visible, visible->rect());
	if (!shown.resolved()) {
		return u"row %1 is not wholly visible"_q.arg(kVisibleRow);
	}
	const auto cut = ReadMappedTarget(owner, clipped, clipped->rect());
	if (cut.resolved() || !cut.refusal.contains(kViewportRefusal)) {
		return u"row %1 is not refused by its scroll viewport"_q.arg(
			kClippedRow);
	} else if (!owner->rect().contains(cut.mapped)
		|| (cut.mapped.size() != clipped->size())) {
		return u"row %1 does not map whole inside the composite owner"_q.arg(
			kClippedRow);
	}
	return QString();
}

void AppendFixtureStage(not_null<Runner*> runner, not_null<State*> state) {
	runner->add({
		.name = u"full-owner capture self-test: fixture"_q,
		.run = [=] {
			Note(u"full-owner capture self-test: no session, account, "
				u"network or primary window - one typed Ui::SeparatePanel "
				u"this self-test builds, shows without activating and "
				u"releases itself"_q);
			state->built = BuildFixture(&state->fixture);
			if (!state->built) {
				return;
			}
			const auto panel = state->fixture.panel.get();
			panel->show();
			LayoutFixture(&state->fixture);
			state->exposure = KeepWindowExposed(panel);
			state->shownAt = crl::now();
		},
		.until = [=] {
			if (!state->built
				|| ((crl::now() - state->shownAt) >= kFixtureSettle)) {
				return true;
			}
			const auto panel = state->fixture.panel.get();
			const auto owner = state->fixture.owner.data();
			const auto side = Row() * kPanelSide;
			return panel
				&& owner
				&& ReadWindowExposure(panel).exposed
				&& (owner->size() == QSize(side, side));
		},
		.then = [=] {
			const auto refusal = FixtureRefusal(*state);
			const auto details = FixtureDetails(*state);
			Check(
				refusal.isEmpty(),
				u"full-owner capture self-test: fixture gate: the typed "
				u"panel is shown, the composite owner is %1 rows square, "
				u"row %2 is wholly visible and row %3 maps whole inside the "
				u"owner but is clipped by its scroll viewport"_q
					.arg(kPanelSide)
					.arg(kVisibleRow)
					.arg(kClippedRow),
				refusal.isEmpty() ? details : (refusal + u" - "_q + details));
			if (!refusal.isEmpty()) {
				state->fixtureGate = u"the full-owner capture fixture gate "
					u"failed: "_q + refusal;
			}
		},
		.timeoutDetails = [=] {
			return FixtureDetails(*state);
		},
	});
}

[[nodiscard]] QString ChildName(Child child) {
	return (child == Child::First) ? u"first"_q : u"second"_q;
}

[[nodiscard]] QString RecordText(const PaintRecord &record) {
	return u"%1/g%2 receipt=%3 paint=%4..%5 seq=%6..%7 capture=%8"_q
		.arg(ChildName(record.child))
		.arg(record.generation)
		.arg(record.receipt)
		.arg(record.start)
		.arg(record.end)
		.arg(record.startSeq)
		.arg(record.endSeq)
		.arg(record.capture ? 1 : 0);
}

// "<row>:<entry>..<exit>", the fixture sequence of one action call.
[[nodiscard]] QString ActionText(const ActionCall &action) {
	return u"%1:%2..%3"_q
		.arg(action.row)
		.arg(action.entry)
		.arg(action.exit);
}

[[nodiscard]] QString NoActionText(int rowSeq) {
	return u"no action call for row seq=%1"_q.arg(rowSeq);
}

[[nodiscard]] QString SampleText(const PostPaintSample &sample) {
	return PostPaintSampleText(sample);
}

// "[a; b; ...]" with at most kListShown entries and a "+<n> more" tail.
template <typename Value, typename Format>
[[nodiscard]] QString ListText(
		const std::vector<Value> &list,
		Format format) {
	auto parts = QStringList();
	for (const auto &value : list) {
		if (int(parts.size()) == kListShown) {
			parts.push_back(u"+%1 more"_q.arg(int(list.size()) - kListShown));
			break;
		}
		parts.push_back(format(value));
	}
	return u"["_q + parts.join(u"; "_q) + u"]"_q;
}

// Every record from |mark| on, in paint order.
[[nodiscard]] std::vector<PaintRecord> RecordsSince(
		const Fixture &fixture,
		int mark) {
	const auto from = std::clamp(mark, 0, int(fixture.records.size()));
	return std::vector<PaintRecord>(
		fixture.records.begin() + from,
		fixture.records.end());
}

// The records of one child generation from |mark| on, capture paints
// included, in paint order.
[[nodiscard]] std::vector<PaintRecord> GenerationRecords(
		const Fixture &fixture,
		int mark,
		Child child,
		int generation) {
	auto result = std::vector<PaintRecord>();
	for (const auto &record : RecordsSince(fixture, mark)) {
		if ((record.child == child) && (record.generation == generation)) {
			result.push_back(record);
		}
	}
	return result;
}

[[nodiscard]] std::vector<PaintRecord> RecordsOfKind(
		const std::vector<PaintRecord> &records,
		bool capture) {
	auto result = std::vector<PaintRecord>();
	for (const auto &record : records) {
		if (record.capture == capture) {
			result.push_back(record);
		}
	}
	return result;
}

[[nodiscard]] int RowMark(const Observer &observer) {
	return observer.sampler ? observer.sampler->mark() : 0;
}

[[nodiscard]] int ReceiptMark(const Observer &observer) {
	return observer.sampler ? observer.sampler->receiptMark() : 0;
}

[[nodiscard]] std::vector<PostPaintSample> RowsSince(
		const Observer &observer,
		int mark) {
	return observer.sampler
		? observer.sampler->samplesSince(mark)
		: std::vector<PostPaintSample>();
}

[[nodiscard]] std::vector<PostPaintReceipt> ReceiptsSince(
		const Observer &observer,
		int mark) {
	return observer.sampler
		? observer.sampler->receiptsSince(mark)
		: std::vector<PostPaintReceipt>();
}

[[nodiscard]] PostPaintFirstReceipt FirstReceipt(
		const Observer &observer,
		int mark) {
	if (const auto sampler = observer.sampler.get()) {
		return sampler->firstReceiptSince(mark);
	}
	return { .refusal = u"no sampler"_q };
}

// The observer's receipt |index|, or a receipt with index 0 when it has
// none.
[[nodiscard]] PostPaintReceipt FindReceipt(
		const Observer &observer,
		int index) {
	if (index > 0) {
		const auto list = ReceiptsSince(observer, index - 1);
		if (!list.empty() && (list.front().index == index)) {
			return list.front();
		}
	}
	return PostPaintReceipt{ .index = 0 };
}

[[nodiscard]] bool HasReceipt(
		const Observer &observer,
		int index,
		PostPaintReceiptKind kind) {
	const auto receipt = FindReceipt(observer, index);
	return (receipt.index > 0) && (receipt.kind == kind);
}

// The call the observer's action received for its row |rowSeq|, or null
// when it recorded none.
[[nodiscard]] const ActionCall *FindAction(
		const Observer &observer,
		int rowSeq) {
	const auto i = ranges::find(observer.actions, rowSeq, &ActionCall::row);
	return (i != end(observer.actions)) ? &*i : nullptr;
}

// The action calls since a row mark: a call made after mark() was taken
// receives a row seq above it.
[[nodiscard]] std::vector<ActionCall> ActionsSince(
		const Observer &observer,
		int mark) {
	auto result = std::vector<ActionCall>();
	for (const auto &action : observer.actions) {
		if (action.row > mark) {
			result.push_back(action);
		}
	}
	return result;
}

// The named refusal of the first of |rows| without an action call, empty
// when every row has one.
[[nodiscard]] QString MissingActionRefusal(
		const Observer &observer,
		const std::vector<PostPaintSample> &rows) {
	for (const auto &row : rows) {
		if (!FindAction(observer, row.seq)) {
			return NoActionText(row.seq);
		}
	}
	return QString();
}

[[nodiscard]] QString RowActionText(
		const Observer &observer,
		const PostPaintSample &row) {
	const auto action = FindAction(observer, row.seq);
	return action
		? (u"action="_q + ActionText(*action))
		: (u"action={"_q + NoActionText(row.seq) + u"}"_q);
}

[[nodiscard]] QString ObserverText(const Observer &observer) {
	return u"%1/g%2 calls=%3 grabs=%4 nullImages=%5 sampler{%6}"_q
		.arg(ChildName(observer.child))
		.arg(observer.generation)
		.arg(observer.calls)
		.arg(observer.grabs)
		.arg(observer.nullImages)
		.arg(observer.sampler ? observer.sampler->details() : u"released"_q);
}

// One child generation's records since a mark against its observer's
// receipts since the same mark: one to one in paint order, with the same
// index, and a capture record exactly when the receipt is a Capture one.
// Empty when they agree, otherwise the first mismatch.
[[nodiscard]] QString ReconcileRefusal(
		const std::vector<PaintRecord> &records,
		const std::vector<PostPaintReceipt> &receipts) {
	const auto count = int(std::max(records.size(), receipts.size()));
	for (auto i = 0; i != count; ++i) {
		if (i >= int(records.size())) {
			return u"receipt without a record: "_q
				+ PostPaintReceiptText(receipts[i]);
		} else if (i >= int(receipts.size())) {
			return u"record without a receipt: "_q + RecordText(records[i]);
		}
		const auto &record = records[i];
		const auto &receipt = receipts[i];
		const auto capture = (receipt.kind == PostPaintReceiptKind::Capture);
		if ((record.receipt != receipt.index) || (record.capture != capture)) {
			return u"mismatch: "_q
				+ RecordText(record)
				+ u" vs "_q
				+ PostPaintReceiptText(receipt);
		}
	}
	return QString();
}

[[nodiscard]] ChildReading ReadChild(
		const Fixture &fixture,
		const Observer &observer,
		const ArmMarks &marks) {
	const auto index = ChildIndex(observer.child);
	auto result = ChildReading{
		.records = GenerationRecords(
			fixture,
			marks.records,
			observer.child,
			observer.generation),
		.rows = RowsSince(observer, marks.rows[index]),
		.receipts = ReceiptsSince(observer, marks.receipts[index]),
		.actions = ActionsSince(observer, marks.rows[index]),
	};
	result.products = RecordsOfKind(result.records, false);
	result.captures = RecordsOfKind(result.records, true);
	result.reconcile = ReconcileRefusal(result.records, result.receipts);
	result.unacted = MissingActionRefusal(observer, result.rows);
	return result;
}

[[nodiscard]] QString ChildReadingText(const ChildReading &reading) {
	return QStringList{
		u"products=%1"_q.arg(ListText(reading.products, RecordText)),
		u"captures=%1"_q.arg(int(reading.captures.size())),
		u"rows=%1"_q.arg(ListText(reading.rows, SampleText)),
		u"actions=%1"_q.arg(ListText(reading.actions, ActionText)),
		u"receipts=%1"_q.arg(
			ListText(reading.receipts, PostPaintReceiptText)),
		u"reconcile=%1"_q.arg(reading.reconcile.isEmpty()
			? u"ok"_q
			: reading.reconcile),
		u"acted=%1"_q.arg(reading.unacted.isEmpty()
			? u"ok"_q
			: reading.unacted),
	}.join(u" "_q);
}

[[nodiscard]] ArmMarks TakeMarks(
		const Fixture &fixture,
		const Observer &first,
		const Observer &second,
		crl::time from,
		crl::time till) {
	return {
		.from = from,
		.till = till,
		.requestedAt = crl::now(),
		.requestSeq = fixture.sequence,
		.records = int(fixture.records.size()),
		.rows = { RowMark(first), RowMark(second) },
		.receipts = { ReceiptMark(first), ReceiptMark(second) },
	};
}

[[nodiscard]] QString MarksText(const ArmMarks &marks) {
	return u"window=[%1,%2) requestedAt=%3 requestSeq=%4 marks{records=%5 "
		u"rows=%6,%7 receipts=%8,%9}"_q
		.arg(marks.from)
		.arg(marks.till)
		.arg(marks.requestedAt)
		.arg(marks.requestSeq)
		.arg(marks.records)
		.arg(marks.rows[0])
		.arg(marks.rows[1])
		.arg(marks.receipts[0])
		.arg(marks.receipts[1]);
}

[[nodiscard]] QString ExposureText(const State &state) {
	return u"kept={"_q
		+ WindowExposureText(state.exposure)
		+ u"} now={"_q
		+ WindowExposureText(ReadWindowExposure(state.fixture.panel.get()))
		+ u"}"_q;
}

// True once the last record, product or capture, ended kSettleQuiet ago.
[[nodiscard]] bool Settled(const Fixture &fixture) {
	return fixture.records.empty()
		|| (crl::now() - fixture.records.back().end >= kSettleQuiet);
}

[[nodiscard]] QString LastRecordText(const Fixture &fixture) {
	if (fixture.records.empty()) {
		return u"last=-"_q;
	}
	const auto &last = fixture.records.back();
	return u"last={"_q
		+ RecordText(last)
		+ u"} ago=%1"_q.arg(crl::now() - last.end);
}

[[nodiscard]] QString PaintProgress(const State &state) {
	const auto &paint = state.paint;
	return QStringList{
		u"records=%1"_q.arg(int(state.fixture.records.size())),
		LastRecordText(state.fixture),
		u"first{"_q + ObserverText(paint.first) + u"}"_q,
		u"second{"_q + ObserverText(paint.second) + u"}"_q,
		u"replacement{"_q + ObserverText(paint.replacement) + u"}"_q,
	}.join(u" "_q);
}

// Every group A sampler's action. The call is recorded first, with the row
// seq it was given and the fixture sequence at its entry and, on every
// return, at its exit, and counted, so a call made after the observed
// generation died is still recorded and counted; then, while that
// generation and the composite owner live, one GrabWidget of the owner,
// which synchronously paints both children. A capped observer grabs for
// its first kControlCaptures calls only: the bound on the recursion the
// instance-local guard lets through.
void TakeCompositeCapture(
		not_null<Fixture*> fixture,
		not_null<Observer*> observer,
		int seq) {
	const auto call = int(observer->actions.size());
	observer->actions.push_back({ .row = seq, .entry = Stamp(fixture) });
	const auto leave = gsl::finally([&] {
		observer->actions[call].exit = Stamp(fixture);
	});
	++observer->calls;
	const auto owner = fixture->owner.data();
	const auto spent = observer->capped
		&& (observer->grabs >= kControlCaptures);
	if (!owner || !observer->owner || spent) {
		return;
	}
	++observer->grabs;
	fixture->capturing = true;
	const auto image = Test::GrabWidget(owner);
	fixture->capturing = false;
	if (image.isNull()) {
		++observer->nullImages;
	}
}

// Observes the current generation of |child| from now on: the paint
// callback stamps this sampler's receipts from here.
void StartObserver(
		not_null<Fixture*> fixture,
		not_null<Observer*> observer,
		not_null<Ui::RpWidget*> widget,
		Child child,
		const QString &name,
		std::shared_ptr<PostPaintCaptureScope> scope,
		bool capped) {
	ReleaseObserver(*fixture, *observer);
	*observer = Observer{
		.owner = widget.get(),
		.child = child,
		.generation = fixture->generation[ChildIndex(child)],
		.capped = capped,
	};
	observer->sampler = std::make_unique<PostPaintSampler>(
		name,
		widget.get(),
		[=](int seq) {
			TakeCompositeCapture(fixture, observer, seq);
		},
		std::move(scope));
	fixture->observed[ChildIndex(child)] = observer->sampler.get();
}

// The coordinated pair: one sampler on each child, sharing one scope.
void StartCoordinated(not_null<State*> state) {
	auto &fixture = state->fixture;
	auto &paint = state->paint;
	const auto first = fixture.first.data();
	const auto second = fixture.second.get();
	if (!first || !second || paint.first.sampler) {
		return;
	}
	paint.scope = std::make_shared<PostPaintCaptureScope>();
	StartObserver(
		&fixture,
		&paint.first,
		first,
		Child::First,
		u"full-owner A first"_q,
		paint.scope,
		false);
	StartObserver(
		&fixture,
		&paint.second,
		second,
		Child::Second,
		u"full-owner A second"_q,
		paint.scope,
		false);
}

void OpenWindow(const Observer &observer, crl::time from, crl::time till) {
	if (const auto sampler = observer.sampler.get()) {
		sampler->sampleDuring(from, till);
	}
}

// Waits until no record ended for kSettleQuiet, so the next arm's paints
// are its own; a fixture that keeps painting times the stage out.
void AppendSettleStage(
		not_null<Runner*> runner,
		not_null<State*> state,
		const QString &arm,
		Fn<void()> run = nullptr) {
	runner->add({
		.name = u"full-owner capture self-test: %1: settle"_q.arg(arm),
		.run = std::move(run),
		.until = [=] {
			return Settled(state->fixture);
		},
		.timeout = kSettleTimeout,
		.timeoutDetails = [=] {
			return PaintProgress(*state);
		},
	});
}

// Both samplers have a sampling window open, so a stray paint would be a
// product receipt and is named; nothing requests one.
void CheckIdleControl(const State &state) {
	const auto &paint = state.paint;
	const auto &marks = paint.marks;
	auto absent = true;
	auto readings = QStringList();
	for (const auto observer : { &paint.first, &paint.second }) {
		const auto mark = marks.receipts[ChildIndex(observer->child)];
		const auto first = FirstReceipt(*observer, mark);
		absent = absent
			&& !first.received()
			&& first.refusal.startsWith(u"absent-receipt"_q);
		readings.push_back(ChildName(observer->child)
			+ u"{"_q
			+ (first.received()
				? PostPaintReceiptText(first.receipt)
				: first.refusal)
			+ u"}"_q);
	}
	const auto stray = RecordsSince(state.fixture, marks.records);
	Check(
		absent,
		u"full-owner capture self-test: A1 first receipt: idle control: "
		u"an exposed idle panel gives neither sampler a product receipt "
		u"over %1 ms without a request"_q.arg(kIdleControl),
		QStringList{
			MarksText(marks),
			readings.join(u" "_q),
			u"strayPaints=%1"_q.arg(ListText(stray, RecordText)),
			ConsoleLockText(ReadConsoleLock()),
			ExposureText(state),
		}.join(u" "_q));
}

// The exact first receipt after one update(): a product receipt, the very
// next index, the first child's own record of that paint, started after
// the marks, and the row sampling it, whose action began after the paint
// ended, both in the fixture sequence. Any failure sets the paint gate.
void CheckFirstReceipt(not_null<State*> state) {
	auto &paint = state->paint;
	const auto &marks = paint.marks;
	const auto mark = marks.receipts[0];
	const auto first = FirstReceipt(paint.first, mark);
	const auto &receipt = first.receipt;
	const auto received = first.received();
	const auto records = GenerationRecords(
		state->fixture,
		marks.records,
		Child::First,
		paint.first.generation);
	const auto record = ranges::find_if(records, [&](
			const PaintRecord &entry) {
		return !entry.capture && (entry.receipt == receipt.index);
	});
	const auto rows = RowsSince(paint.first, marks.rows[0]);
	const auto row = ranges::find_if(rows, [&](const PostPaintSample &entry) {
		return (entry.paintAt == receipt.at);
	});
	const auto matched = received && (record != end(records));
	const auto sampled = received && (row != end(rows));
	const auto action = sampled ? FindAction(paint.first, row->seq) : nullptr;
	const auto ok = received
		&& (receipt.index == mark + 1)
		&& matched
		&& (record->startSeq > marks.requestSeq)
		&& sampled
		&& action
		&& (record->endSeq < action->entry);
	Check(
		ok,
		u"full-owner capture self-test: A1 first receipt: one update() "
		u"gives the exact first receipt, a product paint of the first "
		u"child within %1 ms, sampled after it returned"_q.arg(
			kFirstReceiptWindow),
		QStringList{
			MarksText(marks),
			u"first{"_q
				+ (received ? PostPaintReceiptText(receipt) : first.refusal)
				+ u"}"_q,
			u"record{"_q
				+ (matched ? RecordText(*record) : u"-"_q)
				+ u"}"_q,
			u"row{"_q
				+ (sampled
					? (SampleText(*row)
						+ u" "_q
						+ RowActionText(paint.first, *row))
					: u"-"_q)
				+ u"}"_q,
			u"records=%1"_q.arg(ListText(records, RecordText)),
			u"actions=%1"_q.arg(ListText(
				ActionsSince(paint.first, marks.rows[0]),
				ActionText)),
			ConsoleLockText(ReadConsoleLock()),
			ExposureText(*state),
			u"sampler{"_q + ObserverText(paint.first) + u"}"_q,
		}.join(u" "_q));
	if (ok) {
		return;
	}
	paint.paintGate = received
		? (u"the first product paint receipt did not reconcile with the "
			u"first child's record and row: "_q
			+ PostPaintReceiptText(receipt))
		: (u"no product paint receipt within %1 ms: "_q.arg(
			kFirstReceiptWindow) + first.refusal);
}

// One sample covering exactly the child's one product paint: that paint's
// receipt is the row's paintAt and lastPaintAt, the row's action began
// after the paint ended in the fixture sequence, and the row's lo is within
// kSampleBound of the paint's end.
[[nodiscard]] bool SampledOnce(
		const ChildReading &reading,
		const Observer &observer) {
	if ((reading.products.size() != 1) || (reading.rows.size() != 1)) {
		return false;
	}
	const auto &product = reading.products.front();
	const auto &row = reading.rows.front();
	const auto receipt = FindReceipt(observer, product.receipt);
	const auto action = FindAction(observer, row.seq);
	return (receipt.index > 0)
		&& (receipt.kind == PostPaintReceiptKind::Product)
		&& (row.covered == 1)
		&& (row.paintAt == receipt.at)
		&& (row.lastPaintAt == receipt.at)
		&& action
		&& (product.endSeq < action->entry)
		&& (row.lo - product.end <= kSampleBound);
}

[[nodiscard]] bool CapturesExcluded(
		const ChildReading &reading,
		const Observer &observer) {
	return (int(reading.captures.size()) >= kMinChildCaptures)
		&& ranges::all_of(reading.captures, [&](const PaintRecord &record) {
			return HasReceipt(
				observer,
				record.receipt,
				PostPaintReceiptKind::Capture);
		});
}

void CheckCoordinated(const State &state) {
	const auto &paint = state.paint;
	const auto &marks = paint.marks;
	const auto first = ReadChild(state.fixture, paint.first, marks);
	const auto second = ReadChild(state.fixture, paint.second, marks);
	auto lastHi = crl::time(0);
	for (const auto reading : { &first, &second }) {
		for (const auto &row : reading->rows) {
			lastHi = std::max(lastHi, row.hi);
		}
	}
	const auto tail = lastHi ? (marks.till - lastHi) : crl::time(0);
	const auto details = QStringList{
		MarksText(marks),
		u"first{"_q + ChildReadingText(first) + u"}"_q,
		u"second{"_q + ChildReadingText(second) + u"}"_q,
		u"tail=%1 (need %2) bound=%3"_q
			.arg(tail)
			.arg(kIdleTailMin)
			.arg(kSampleBound),
		u"samplers{"_q
			+ ObserverText(paint.first)
			+ u"; "_q
			+ ObserverText(paint.second)
			+ u"}"_q,
	}.join(u" "_q);
	Check(
		SampledOnce(first, paint.first) && SampledOnce(second, paint.second),
		u"full-owner capture self-test: A2 coordinated: one real update on "
		u"each child yields exactly one sample per sampler, covering that "
		u"paint alone, taken after it returned and within %1 ms"_q.arg(
			kSampleBound),
		details);
	Check(
		CapturesExcluded(first, paint.first)
			&& CapturesExcluded(second, paint.second)
			&& (lastHi > 0)
			&& (tail >= kIdleTailMin),
		u"full-owner capture self-test: A2 coordinated: each composite "
		u"capture painted both children, every such paint is a Capture "
		u"receipt, and no further sample follows over an idle tail of at "
		u"least %1 ms"_q.arg(kIdleTailMin),
		details);
	Check(
		first.reconcile.isEmpty() && second.reconcile.isEmpty(),
		u"full-owner capture self-test: A2 coordinated: every receipt since "
		u"the mark reconciles one to one to the child's own paint "
		u"records"_q,
		details);
}

// Passes when the defect is exposed: without a shared scope a composite
// capture paint is a product receipt to the other child's sampler, and
// one real paint feeds a recursion of samples.
void CheckInstanceLocal(const State &state) {
	const auto &paint = state.paint;
	const auto &marks = paint.marks;
	const auto first = ReadChild(state.fixture, paint.localFirst, marks);
	const auto second = ReadChild(state.fixture, paint.localSecond, marks);
	const auto falseProducts = [](
			const ChildReading &reading,
			const Observer &observer) {
		return int(ranges::count_if(
			reading.captures,
			[&](const PaintRecord &record) {
				return HasReceipt(
					observer,
					record.receipt,
					PostPaintReceiptKind::Product);
			}));
	};
	const auto falseFirst = falseProducts(first, paint.localFirst);
	const auto falseSecond = falseProducts(second, paint.localSecond);
	const auto products = int(first.products.size() + second.products.size());
	const auto rows = int(first.rows.size() + second.rows.size());
	const auto details = QStringList{
		MarksText(marks),
		u"falseProducts{first=%1 second=%2} products=%3 rows=%4"_q
			.arg(falseFirst)
			.arg(falseSecond)
			.arg(products)
			.arg(rows),
		u"first{"_q + ChildReadingText(first) + u"}"_q,
		u"second{"_q + ChildReadingText(second) + u"}"_q,
		u"samplers{"_q
			+ ObserverText(paint.localFirst)
			+ u"; "_q
			+ ObserverText(paint.localSecond)
			+ u"}"_q,
	}.join(u" "_q);
	Check(
		falseSecond > 0,
		u"full-owner capture self-test: A3 instance-local control: without "
		u"a shared scope the second child's sampler takes the first "
		u"sampler's composite capture paint for a product paint"_q,
		details);
	Check(
		(products == 1) && (rows > products),
		u"full-owner capture self-test: A3 instance-local control: without "
		u"a shared scope one real paint yields recursive extra samples"_q,
		details);
}

// Every order decision of arm A4 is in the fixture sequence: the second
// product paint ended before the first row's action began (the premise),
// and the third started after that action returned and ended before the
// second row's action began. A row without an action call refuses by name.
void CheckCoalesced(const State &state) {
	const auto &paint = state.paint;
	const auto &marks = paint.marks;
	const auto first = ReadChild(state.fixture, paint.first, marks);
	const auto second = ReadChild(state.fixture, paint.second, marks);
	const auto &products = first.products;
	const auto &rows = first.rows;
	const auto receipt = [&](int i) {
		return (i < int(products.size()))
			? FindReceipt(paint.first, products[i].receipt)
			: PostPaintReceipt{ .index = 0 };
	};
	const auto product = [&](int i) {
		const auto found = receipt(i);
		return (found.index > 0)
			&& (found.kind == PostPaintReceiptKind::Product);
	};
	const auto action = [&](int i) {
		return (i < int(rows.size()))
			? FindAction(paint.first, rows[i].seq)
			: nullptr;
	};
	const auto a0 = action(0);
	const auto a1 = action(1);
	const auto details = QStringList{
		MarksText(marks),
		u"repaintPosted=%1"_q.arg(paint.armPosted ? 1 : 0),
		u"first{"_q + ChildReadingText(first) + u"}"_q,
		u"second{"_q + ChildReadingText(second) + u"}"_q,
		u"sampler{"_q + ObserverText(paint.first) + u"}"_q,
	}.join(u" "_q);
	const auto premise = paint.armPosted
		&& (products.size() >= 2)
		&& a0
		&& (products[1].endSeq < a0->entry);
	const auto refusal = (!rows.empty() && !a0)
		? NoActionText(rows.front().seq)
		: (u"the second product paint did not end before the first "
			u"sample's action began - repaint() deferred"_q);
	Check(
		premise,
		u"full-owner capture self-test: A4 coalesced: premise: the posted "
		u"repaint() painted the first child again before its pending "
		u"sample ran"_q,
		premise
			? details
			: (u"coalescing premise: "_q + refusal + u" - "_q + details));
	const auto coalesced = (int(products.size()) == kCoalescedPaints)
		&& (int(rows.size()) == kCoalescedRows)
		&& a0
		&& a1
		&& product(0)
		&& product(1)
		&& product(2)
		&& (rows[0].covered == 2)
		&& (rows[0].paintAt == receipt(0).at)
		&& (rows[0].lastPaintAt == receipt(1).at)
		&& (products[1].endSeq < a0->entry)
		&& (rows[1].covered == 1)
		&& (rows[1].paintAt == receipt(2).at)
		&& (rows[1].lastPaintAt == receipt(2).at)
		&& (products[2].startSeq > a0->exit)
		&& (products[2].endSeq < a1->entry);
	Check(
		coalesced,
		u"full-owner capture self-test: A4 coalesced: the two coalesced real "
		u"paints share the first sample and the genuine later paint the "
		u"capture requested gets its own"_q,
		details);
	Check(
		first.reconcile.isEmpty() && second.reconcile.isEmpty(),
		u"full-owner capture self-test: A4 coalesced: every record of both "
		u"children reconciles to its receipt and kind"_q,
		details);
}

[[nodiscard]] ReplaceReading ReadReplace(const State &state) {
	const auto &paint = state.paint;
	const auto &observer = paint.second;
	const auto records = GenerationRecords(
		state.fixture,
		paint.marks.records,
		Child::Second,
		observer.generation);
	return {
		.products = int(RecordsOfKind(records, false).size()),
		.rows = int(RowsSince(observer, paint.marks.rows[1]).size()),
		.calls = observer.calls,
		.pending = observer.sampler && observer.sampler->pending(),
	};
}

[[nodiscard]] QString ReplaceReadingText(const ReplaceReading &reading) {
	return u"products=%1 rows=%2 calls=%3 pending=%4"_q
		.arg(reading.products)
		.arg(reading.rows)
		.arg(reading.calls)
		.arg(reading.pending ? 1 : 0);
}

// Arm A5's destroy call D: posted by the arm's filter before the second
// sampler's own sample S for the same paint, so it runs first. It reads the
// premise, destroys the first generation while S is queued, then observes
// a replacement generation before it is shown.
void ReplaceSecond(not_null<State*> state) {
	auto &fixture = state->fixture;
	auto &paint = state->paint;
	const auto owner = fixture.owner.data();
	const auto stopped = paint.second.sampler.get();
	if (!owner || !fixture.second || !stopped) {
		return;
	}
	paint.atDestroy = ReadReplace(*state);
	fixture.second = nullptr;
	paint.destroyedAt = crl::now();
	paint.stoppedReceipts = stopped->receiptMark();
	fixture.second = base::make_unique_q<Ui::RpWidget>(owner);
	const auto replacement = fixture.second.get();
	SetupChild(&fixture, replacement, Child::Second);
	StartObserver(
		&fixture,
		&paint.replacement,
		replacement,
		Child::Second,
		u"full-owner A second replacement"_q,
		paint.scope,
		false);
	paint.replacementFrom = crl::now();
	paint.replacementTill = paint.replacementFrom + kReplacementWindow;
	OpenWindow(paint.replacement, paint.replacementFrom, paint.replacementTill);
	replacement->show();
}

void CheckReplacement(const State &state) {
	const auto &paint = state.paint;
	const auto &marks = paint.marks;
	const auto &atDestroy = paint.atDestroy;
	const auto old = ReadChild(state.fixture, paint.second, marks);
	const auto replaced = ReadChild(
		state.fixture,
		paint.replacement,
		ArmMarks{ .records = marks.records });
	const auto stopped = paint.second.sampler.get();
	const auto stop = stopped ? stopped->stopReason() : u"no-sampler"_q;
	const auto receipts = stopped ? stopped->receiptMark() : -1;
	auto productAt = std::vector<crl::time>();
	for (const auto &record : replaced.products) {
		productAt.push_back(FindReceipt(paint.replacement, record.receipt).at);
	}
	auto covered = 0;
	for (const auto &row : replaced.rows) {
		covered += row.covered;
	}
	const auto ownRows = ranges::all_of(
		replaced.rows,
		[&](const PostPaintSample &row) {
			return ranges::contains(productAt, row.paintAt)
				&& ranges::contains(productAt, row.lastPaintAt);
		});
	const auto details = QStringList{
		MarksText(marks),
		u"destroyPosted=%1 destroyedAt=%2 replacementWindow=[%3,%4)"_q
			.arg(paint.armPosted ? 1 : 0)
			.arg(paint.destroyedAt)
			.arg(paint.replacementFrom)
			.arg(paint.replacementTill),
		u"atDestroy{"_q + ReplaceReadingText(atDestroy) + u"}"_q,
		u"gen1{calls=%1 stop=%2 receiptMark=%3 atStop=%4 pending=%5}"_q
			.arg(paint.second.calls)
			.arg(stop.isEmpty() ? u"-"_q : stop)
			.arg(receipts)
			.arg(paint.stoppedReceipts)
			.arg((stopped && stopped->pending()) ? 1 : 0),
		u"old{"_q + ChildReadingText(old) + u"}"_q,
		u"gen2{"_q + ChildReadingText(replaced) + u"}"_q,
		u"covered=%1"_q.arg(covered),
		u"samplers{"_q
			+ ObserverText(paint.second)
			+ u"; "_q
			+ ObserverText(paint.replacement)
			+ u"}"_q,
	}.join(u" "_q);
	Check(
		paint.armPosted
			&& (atDestroy.products == 1)
			&& atDestroy.pending
			&& (atDestroy.rows == 0),
		u"full-owner capture self-test: A5 replacement: premise: the first "
		u"generation's one product paint left a sample pending when it was "
		u"destroyed"_q,
		details);
	Check(
		stopped
			&& (paint.destroyedAt != 0)
			&& (paint.second.calls == atDestroy.calls)
			&& (int(old.rows.size()) == atDestroy.rows)
			&& (stop == u"owner-destroyed"_q)
			&& (receipts == paint.stoppedReceipts)
			&& !stopped->pending()
			&& old.reconcile.isEmpty(),
		u"full-owner capture self-test: A5 replacement: the destroyed "
		u"generation's queued sample never runs, and its sampler stops as "
		u"owner-destroyed and records nothing more"_q,
		details);
	Check(
		!replaced.products.empty()
			&& !replaced.rows.empty()
			&& replaced.reconcile.isEmpty()
			&& ownRows
			&& (covered == int(replaced.products.size())),
		u"full-owner capture self-test: A5 replacement: every record of the "
		u"replacement generation indexes its own sampler's receipts from 1, "
		u"and its rows cover only its own product paints"_q,
		details);
}

void AppendIdleControlStage(
		not_null<Runner*> runner,
		not_null<State*> state) {
	runner->add({
		.name = u"full-owner capture self-test: A1 first receipt: idle "
			u"control"_q,
		.run = [=] {
			auto &paint = state->paint;
			const auto from = crl::now();
			const auto till = from + kIdleControl;
			OpenWindow(paint.first, from, till);
			OpenWindow(paint.second, from, till);
			paint.marks = TakeMarks(
				state->fixture,
				paint.first,
				paint.second,
				from,
				till);
		},
		.until = [=] {
			return crl::now() >= state->paint.marks.till;
		},
		.then = [=] {
			CheckIdleControl(*state);
		},
		.timeoutDetails = [=] {
			return PaintProgress(*state);
		},
	});
}

// The stage waits out the whole window even after the receipt arrived: a
// sampling window cannot be closed early, and one left open on the first
// sampler would sample the unscoped captures of arm A3.
void AppendFirstReceiptStage(
		not_null<Runner*> runner,
		not_null<State*> state) {
	runner->add({
		.name = u"full-owner capture self-test: A1 first receipt: one "
			u"requested paint"_q,
		.run = [=] {
			auto &paint = state->paint;
			const auto from = crl::now();
			const auto till = from + kFirstReceiptWindow;
			OpenWindow(paint.first, from, till);
			paint.marks = TakeMarks(
				state->fixture,
				paint.first,
				paint.second,
				from,
				till);
			if (const auto first = state->fixture.first.data()) {
				first->update();
			}
		},
		.until = [=] {
			return crl::now() >= state->paint.marks.till + kDrain;
		},
		.then = [=] {
			CheckFirstReceipt(state);
		},
		.timeoutDetails = [=] {
			return PaintProgress(*state);
		},
	});
}

void AppendCoordinatedStage(
		not_null<Runner*> runner,
		not_null<State*> state) {
	runner->add({
		.name = u"full-owner capture self-test: A2 coordinated: one real "
			u"update on each child"_q,
		.run = [=] {
			auto &fixture = state->fixture;
			auto &paint = state->paint;
			const auto from = crl::now();
			const auto till = from + kCoordinatedWindow;
			OpenWindow(paint.first, from, till);
			OpenWindow(paint.second, from, till);
			paint.marks = TakeMarks(
				fixture,
				paint.first,
				paint.second,
				from,
				till);
			if (const auto first = fixture.first.data()) {
				first->update();
			}
			if (const auto second = fixture.second.get()) {
				second->update();
			}
		},
		.until = [=] {
			return crl::now() >= state->paint.marks.till + kDrain;
		},
		.then = [=] {
			CheckCoordinated(*state);
		},
		.timeoutDetails = [=] {
			return PaintProgress(*state);
		},
	});
}

void AppendInstanceLocalStage(
		not_null<Runner*> runner,
		not_null<State*> state) {
	runner->add({
		.name = u"full-owner capture self-test: A3 instance-local control: "
			u"the same composite capture without a shared scope"_q,
		.run = [=] {
			auto &fixture = state->fixture;
			auto &paint = state->paint;
			const auto from = crl::now();
			const auto till = from + kLocalWindow;
			paint.marks.till = till;
			const auto first = fixture.first.data();
			const auto second = fixture.second.get();
			if (!first || !second) {
				return;
			}
			StartObserver(
				&fixture,
				&paint.localFirst,
				first,
				Child::First,
				u"full-owner A3 local first"_q,
				nullptr,
				true);
			StartObserver(
				&fixture,
				&paint.localSecond,
				second,
				Child::Second,
				u"full-owner A3 local second"_q,
				nullptr,
				true);
			OpenWindow(paint.localFirst, from, till);
			OpenWindow(paint.localSecond, from, till);
			paint.marks = TakeMarks(
				fixture,
				paint.localFirst,
				paint.localSecond,
				from,
				till);
			first->update();
		},
		.until = [=] {
			return crl::now() >= state->paint.marks.till + kDrain;
		},
		.then = [=] {
			CheckInstanceLocal(*state);
			auto &fixture = state->fixture;
			auto &paint = state->paint;
			ReleaseObserver(fixture, paint.localFirst);
			ReleaseObserver(fixture, paint.localSecond);
			fixture.observed = {
				paint.first.sampler.get(),
				paint.second.sampler.get(),
			};
		},
		.timeoutDetails = [=] {
			return PaintProgress(*state);
		},
	});
}

void AppendCoalescedStage(
		not_null<Runner*> runner,
		not_null<State*> state) {
	runner->add({
		.name = u"full-owner capture self-test: A4 coalesced: two real "
			u"paints before one sample, then a genuine later paint"_q,
		.run = [=] {
			auto &fixture = state->fixture;
			auto &paint = state->paint;
			const auto from = crl::now();
			const auto till = from + kCoalescedWindow;
			paint.armPosted = false;
			paint.marks.till = till;
			const auto first = fixture.first.data();
			if (!first || !paint.first.sampler) {
				return;
			}

			// Installed after the first sampler's filter, so Qt calls it
			// first: R is posted before the sample S of the same paint, and
			// posted calls of one priority run in order. R repaints the
			// child synchronously, in this top level without texture
			// children, so that paint joins the pending S.
			paint.armFilter = base::install_event_filter(first, [=](
					not_null<QEvent*> e) {
				if ((e->type() == QEvent::Paint)
					&& !state->fixture.capturing
					&& !state->paint.armPosted) {
					state->paint.armPosted = true;
					InvokeQueued(QCoreApplication::instance(), [=] {
						if (const auto child = state->fixture.first.data()) {
							child->repaint();
						}
					});
				}
				return base::EventFilterResult::Continue;
			}).get();
			fixture.repaintOnCapture = true;
			OpenWindow(paint.first, from, till);
			paint.marks = TakeMarks(
				fixture,
				paint.first,
				paint.second,
				from,
				till);
			first->update();
		},
		.until = [=] {
			return crl::now() >= state->paint.marks.till + kDrain;
		},
		.then = [=] {
			CheckCoalesced(*state);
			delete state->paint.armFilter.data();
			state->fixture.repaintOnCapture = false;
		},
		.timeoutDetails = [=] {
			return PaintProgress(*state);
		},
	});
}

void AppendReplacementStage(
		not_null<Runner*> runner,
		not_null<State*> state) {
	runner->add({
		.name = u"full-owner capture self-test: A5 replacement: destruction "
			u"with a sample queued, then a replacement generation"_q,
		.run = [=] {
			auto &fixture = state->fixture;
			auto &paint = state->paint;
			const auto from = crl::now();
			const auto till = from + kReplacementWindow;
			paint.armPosted = false;
			paint.destroyedAt = 0;
			paint.marks.till = till;
			const auto second = fixture.second.get();
			if (!second || !paint.second.sampler) {
				return;
			}
			OpenWindow(paint.second, from, till);

			// Installed after the second sampler's filter, so Qt calls it
			// first and D is posted before that sampler's S. The filter is
			// a child of the first generation and dies with it.
			base::install_event_filter(second, [=](not_null<QEvent*> e) {
				if ((e->type() == QEvent::Paint)
					&& !state->fixture.capturing
					&& !state->paint.armPosted) {
					state->paint.armPosted = true;
					InvokeQueued(QCoreApplication::instance(), [=] {
						ReplaceSecond(state);
					});
				}
				return base::EventFilterResult::Continue;
			});
			paint.marks = TakeMarks(
				fixture,
				paint.first,
				paint.second,
				from,
				till);
			second->update();
		},
		.until = [=] {
			const auto &paint = state->paint;
			const auto till = paint.destroyedAt
				? paint.replacementTill
				: paint.marks.till;
			return crl::now() >= till + kDrain;
		},
		.then = [=] {
			CheckReplacement(*state);
		},
		.timeoutDetails = [=] {
			return u"destroyPosted=%1 destroyedAt=%2 "_q
				.arg(state->paint.armPosted ? 1 : 0)
				.arg(state->paint.destroyedAt)
				+ PaintProgress(*state);
		},
	});
}

// As AppendPostPaintSamplerSelfTest's last stage: the run goes on after the
// owner died, and the stopped sampler, whose filter died with it, is
// released.
void AppendReleaseStoppedStage(
		not_null<Runner*> runner,
		not_null<State*> state) {
	runner->add({
		.name = u"full-owner capture self-test: A5 replacement: the stage "
			u"after the destruction runs"_q,
		.then = [=] {
			auto &paint = state->paint;
			const auto stopped = paint.second.sampler.get();
			const auto reason = stopped
				? stopped->stopReason()
				: u"no-sampler"_q;
			ReleaseObserver(state->fixture, paint.second);
			Check(
				(reason == u"owner-destroyed"_q) && !paint.second.sampler,
				u"full-owner capture self-test: A5 replacement: the next "
				u"stage runs after the destruction and the stopped sampler "
				u"is released"_q,
				u"ran=1 stop=%1 replacement{"_q.arg(reason)
					+ ObserverText(paint.replacement)
					+ u"}"_q);
		},
	});
}

// Outside the paint gate, so the samplers are released on that gate's
// refusal too; the widgets they observed are still alive here.
void AppendPaintGroupEndStage(
		not_null<Runner*> runner,
		not_null<State*> state) {
	runner->add({
		.name = u"full-owner capture self-test: group A: release the "
			u"samplers"_q,
		.then = [=] {
			ReleaseSamplers(*state);
			const auto &fixture = state->fixture;
			const auto &paint = state->paint;
			const auto observers = {
				&paint.first,
				&paint.second,
				&paint.replacement,
				&paint.localFirst,
				&paint.localSecond,
			};
			auto alive = 0;
			for (const auto observer : observers) {
				if (observer->sampler) {
					++alive;
				}
			}
			const auto observed = int(ranges::count_if(
				fixture.observed,
				[](const PostPaintSampler *sampler) {
					return sampler != nullptr;
				}));
			const auto widgets = fixture.first && fixture.second;
			Check(
				!alive && !observed && widgets,
				u"full-owner capture self-test: group A released every "
				u"sampler while the children it observed were alive"_q,
				u"alive=%1 observed=%2 first=%3 second=%4 paintGate=%5"_q
					.arg(alive)
					.arg(observed)
					.arg(fixture.first ? 1 : 0)
					.arg(fixture.second ? 1 : 0)
					.arg(paint.paintGate.isEmpty()
						? u"-"_q
						: paint.paintGate));
		},
	});
}

// Group A, Part A: the coordinated samplers. Its own gate is the first
// receipt A1 proves; a locked console never gates, receipts decide.
void AppendPaintGroup(not_null<Runner*> runner, not_null<State*> state) {
	runner->addGated([=] { return state->paint.paintGate; }, [&] {
		AppendSettleStage(runner, state, u"A1 first receipt"_q, [=] {
			StartCoordinated(state);
		});
		AppendIdleControlStage(runner, state);
		AppendFirstReceiptStage(runner, state);
		AppendSettleStage(runner, state, u"A2 coordinated"_q);
		AppendCoordinatedStage(runner, state);
		AppendSettleStage(runner, state, u"A3 instance-local control"_q);
		AppendInstanceLocalStage(runner, state);
		AppendSettleStage(runner, state, u"A4 coalesced"_q);
		AppendCoalescedStage(runner, state);
		AppendSettleStage(runner, state, u"A5 replacement"_q);
		AppendReplacementStage(runner, state);
		AppendReleaseStoppedStage(runner, state);
	});
	AppendPaintGroupEndStage(runner, state);
}

// K in owner coordinates: from a third into the gradient to its right edge,
// its full height, so the crop starts inside the gradient child.
[[nodiscard]] QRect StrictCrop(const Fixture &fixture) {
	const auto gradient = fixture.gradient;
	if (!gradient) {
		return QRect();
	}
	const auto bounds = gradient->geometry();
	const auto skip = bounds.width() / kCropSkipDivisor;
	return QRect(
		bounds.x() + skip,
		bounds.y(),
		bounds.width() - skip,
		bounds.height());
}

[[nodiscard]] PixelCount CountPixels(const QImage &a, const QImage &b) {
	if (a.isNull() || b.isNull() || (a.size() != b.size())) {
		return {
			.refusal = u"not comparable: %1x%2 vs %3x%4"_q
				.arg(a.width())
				.arg(a.height())
				.arg(b.width())
				.arg(b.height()),
		};
	}
	auto result = PixelCount{
		.compared = a.width() * a.height(),
		.differing = 0,
		.maxChannelDelta = 0,
		.oneLevel = 0,
	};
	for (auto y = 0; y != a.height(); ++y) {
		for (auto x = 0; x != a.width(); ++x) {
			const auto first = a.pixel(x, y);
			const auto second = b.pixel(x, y);
			if (first == second) {
				continue;
			}
			const auto delta = std::max({
				std::abs(qRed(first) - qRed(second)),
				std::abs(qGreen(first) - qGreen(second)),
				std::abs(qBlue(first) - qBlue(second)),
				std::abs(qAlpha(first) - qAlpha(second)),
			});
			++result.differing;
			result.maxChannelDelta = std::max(result.maxChannelDelta, delta);
			if (delta == 1) {
				++result.oneLevel;
			}
		}
	}
	return result;
}

[[nodiscard]] QString PixelCountText(const PixelCount &count) {
	return u"compared=%1 differing=%2 maxChannelDelta=%3 oneLevel=%4%5"_q
		.arg(count.compared)
		.arg(count.differing)
		.arg(count.maxChannelDelta)
		.arg(count.oneLevel)
		.arg(count.refusal.isEmpty()
			? QString()
			: u" refusal=%1"_q.arg(count.refusal));
}

// What the shifted-span mechanism predicts from: the stop-table increment
// per device pixel of the gradient's span in 1/256 steps, its truncated
// fraction, and the index offset the full-owner span accumulates by the
// crop's left edge, which the direct-region span starts at.
[[nodiscard]] QString GradientNumbersText(const State &state) {
	const auto &fixture = state.fixture;
	const auto gradient = fixture.gradient;
	const auto dpr = style::DevicePixelRatio();
	const auto span = fixture.gradientWidth * dpr;
	const auto increment = span
		? (kStopTableSteps * kFixedPointSize / span)
		: 0.;
	const auto fraction = increment - std::floor(increment);
	const auto offset = gradient
		? ((state.strict.crop.x() - gradient->x()) * dpr)
		: 0;
	const auto predicted = offset * fraction / kFixedPointSize;
	return u"dpr=%1 gradientWidth=%2 gradient=%3 crop=%4 span=%5 "
		u"increment=%6 fraction=%7 offset=%8px predictedIndexOffset=%9"_q
		.arg(dpr)
		.arg(fixture.gradientWidth)
		.arg(gradient ? RectText(gradient->geometry()) : u"<none>"_q)
		.arg(RectText(state.strict.crop))
		.arg(span)
		.arg(QString::number(increment, 'f', kNumberPrecision))
		.arg(QString::number(fraction, 'f', kNumberPrecision))
		.arg(offset)
		.arg(QString::number(predicted, 'f', kNumberPrecision));
}

// B1: the full-owner arm renders the whole owner, the shifted arm renders
// K directly, and both apply the same crop K.
void MeasureStrict(not_null<State*> state) {
	auto &strict = state->strict;
	const auto owner = state->fixture.owner.data();
	const auto bounds = owner ? owner->rect() : QRect();
	strict.crop = StrictCrop(state->fixture);
	strict.full = RenderStrict(owner, bounds, strict.crop);
	strict.shifted = RenderStrict(owner, strict.crop, strict.crop);
	strict.measured = CountPixels(strict.full.image, strict.shifted.image);
	const auto numbers = GradientNumbersText(*state);
	const auto details = QStringList{
		u"full={"_q + StrictRenderText(strict.full) + u"}"_q,
		u"shifted={"_q + StrictRenderText(strict.shifted) + u"}"_q,
		u"independent={"_q + PixelCountText(strict.measured) + u"}"_q,
		numbers,
	}.join(u" "_q);
	Note(u"full-owner capture self-test: B1 strict: "_q + details);
	const auto dpr = style::DevicePixelRatio();
	const auto expected = strict.crop.size() * dpr;
	const auto recorded = [&](
			const StrictRender &render,
			const QRect &rendered) {
		return render.valid()
			&& (render.owner.data() == owner)
			&& (render.rendered == rendered)
			&& (render.crop == strict.crop)
			&& (render.image.size() == expected)
			&& (render.image.devicePixelRatio() == dpr);
	};
	Check(
		owner
			&& !strict.crop.isEmpty()
			&& recorded(strict.full, bounds)
			&& recorded(strict.shifted, strict.crop)
			&& (strict.full.image.format() == strict.shifted.image.format()),
		u"full-owner capture self-test: B1 strict: both arms render the "
		u"composite owner and record its rendered rect, the gradient crop "
		u"and the cropped image's size, ratio and format"_q,
		details);
	if (strict.measured.differing <= 0) {
		strict.premise = u"premise not demonstrated: the independent count "
			u"found no pixel of the shifted direct-region render that "
			u"differs from the full-owner render - independent={%1} %2"_q.arg(
				PixelCountText(strict.measured),
				numbers);
	}
}

void CheckStrictMismatch(const State &state) {
	const auto &strict = state.strict;
	const auto comparison = CompareStrict(strict.full, strict.shifted);
	const auto details = QStringList{
		u"comparison={"_q + StrictComparisonText(comparison) + u"}"_q,
		u"independent={"_q + PixelCountText(strict.measured) + u"}"_q,
		GradientNumbersText(state),
	}.join(u" "_q);
	Check(
		(strict.measured.differing > 0)
			&& comparison.refusal.startsWith(u"render-path-differs"_q)
			&& !comparison.equal(),
		u"full-owner capture self-test: B2 strict: mismatch control: the "
		u"shifted direct-region render of the static gradient differs from "
		u"the full-owner render with unchanged geometry, and the strict "
		u"comparison refuses the render path"_q,
		details);
	Check(
		comparison.differing == strict.measured.differing,
		u"full-owner capture self-test: B2 strict: mismatch control: the "
		u"refused comparison's diagnostic count equals the independent "
		u"count"_q,
		details);
}

void CheckStrictRepeated(const State &state) {
	const auto &strict = state.strict;
	const auto owner = state.fixture.owner.data();
	const auto bounds = owner ? owner->rect() : QRect();
	const auto again = RenderStrict(owner, bounds, strict.crop);
	const auto comparison = CompareStrict(strict.full, again);
	const auto count = CountPixels(strict.full.image, again.image);
	Check(
		comparison.equal() && (count.differing == 0),
		u"full-owner capture self-test: B3 strict: repeated same path: a "
		u"second full-owner render with the same owner, rendered rect and "
		u"crop agrees with the first exactly, with zero tolerance and no "
		u"mask"_q,
		u"comparison={%1} independent={%2} again={%3}"_q.arg(
			StrictComparisonText(comparison),
			PixelCountText(count),
			StrictRenderText(again)));
}

void CheckStrictChangedPixel(const State &state) {
	const auto &strict = state.strict;
	auto changed = strict.full;
	changed.image.detach();
	const auto at = QPoint(
		changed.image.width() / 2,
		changed.image.height() / 2);
	if (!changed.image.isNull()) {
		changed.image.setPixel(at, changed.image.pixel(at) ^ kInvertRgb);
	}
	const auto comparison = CompareStrict(strict.full, changed);
	const auto count = CountPixels(strict.full.image, changed.image);
	Check(
		comparison.refusal.isEmpty()
			&& (comparison.differing == 1)
			&& (comparison.first == at)
			&& !comparison.equal()
			&& (count.differing == 1),
		u"full-owner capture self-test: B4 strict: changed pixel: one "
		u"inverted pixel inside the gradient crop is the one differing "
		u"pixel of an unrefused comparison, which fails"_q,
		u"at=%1,%2 comparison={%3} independent={%4}"_q
			.arg(at.x())
			.arg(at.y())
			.arg(StrictComparisonText(comparison))
			.arg(PixelCountText(count)));
}

void CheckStrictRefusals(const State &state) {
	const auto &strict = state.strict;
	const auto owner = state.fixture.owner.data();
	const auto bounds = owner ? owner->rect() : QRect();

	const auto extent = RenderStrict(
		owner,
		bounds.adjusted(0, 0, 0, -Row()),
		strict.crop);
	const auto byExtent = CompareStrict(extent, strict.full);
	Check(
		byExtent.refusal.startsWith(u"render-path-differs"_q)
			&& !byExtent.equal(),
		u"full-owner capture self-test: B5 strict: refusals: a render of "
		u"another extent with the same crop is refused as "
		u"render-path-differs"_q,
		StrictComparisonText(byExtent));

	auto converted = strict.full;
	converted.image = strict.full.image.convertToFormat(
		QImage::Format_RGB32);
	const auto byFormat = CompareStrict(strict.full, converted);
	Check(
		byFormat.refusal.startsWith(u"format-differs"_q)
			&& !byFormat.equal(),
		u"full-owner capture self-test: B5 strict: refusals: the same "
		u"record with its image converted to another format is refused as "
		u"format-differs"_q,
		StrictComparisonText(byFormat));

	auto scaled = strict.full;
	scaled.image.setDevicePixelRatio(scaled.image.devicePixelRatio() + 1);
	const auto byRatio = CompareStrict(strict.full, scaled);
	Check(
		byRatio.refusal.startsWith(u"dpr-differs"_q) && !byRatio.equal(),
		u"full-owner capture self-test: B5 strict: refusals: the same "
		u"record with another device pixel ratio is refused as "
		u"dpr-differs"_q,
		StrictComparisonText(byRatio));

	const auto outside = QRect(
		strict.crop.x(),
		strict.crop.y(),
		strict.crop.width(),
		bounds.height());
	const auto refused = RenderStrict(owner, bounds, outside);
	Check(
		!refused.valid()
			&& refused.image.isNull()
			&& refused.refusal.startsWith(
				u"crop is not fully inside the rendered rect"_q)
			&& refused.refusal.contains(
				u"requested="_q + RectText(outside)),
		u"full-owner capture self-test: B5 strict: refusals: a crop "
		u"leaving the rendered rect is refused with the requested rect "
		u"named and no image, never shortened"_q,
		StrictRenderText(refused));

	SaveImage(strict.full.image, u"strict_full"_q);
	SaveImage(strict.shifted.image, u"strict_shifted"_q);
}

// Group B, Part B: strict same-state comparisons. It renders in-process,
// so it needs the shown fixture only, never a paint.
void AppendStrictGroup(not_null<Runner*> runner, not_null<State*> state) {
	runner->add({
		.name = u"full-owner capture self-test: B1 strict: measure the "
			u"full-owner and the shifted direct-region render"_q,
		.then = [=] {
			MeasureStrict(state);
		},
	});
	runner->add({
		.name = u"full-owner capture self-test: B2 strict: mismatch "
			u"control"_q,
		.skipReason = [=] {
			return state->strict.premise;
		},
		.then = [=] {
			CheckStrictMismatch(*state);
		},
	});
	runner->add({
		.name = u"full-owner capture self-test: B3 strict: repeated same "
			u"path"_q,
		.then = [=] {
			CheckStrictRepeated(*state);
		},
	});
	runner->add({
		.name = u"full-owner capture self-test: B4 strict: changed pixel"_q,
		.then = [=] {
			CheckStrictChangedPixel(*state);
		},
	});
	runner->add({
		.name = u"full-owner capture self-test: B5 strict: refusals"_q,
		.then = [=] {
			CheckStrictRefusals(*state);
		},
	});
}

[[nodiscard]] CropWidgets ReadCropWidgets(const Fixture &fixture) {
	const auto panel = fixture.panel.get();
	const auto owner = fixture.owner.data();
	const auto visible = fixture.rows[kVisibleRow];
	const auto clipped = fixture.rows[kClippedRow];
	if (!panel || !owner || !fixture.scroll || !visible || !clipped) {
		return {};
	}
	return {
		.panel = panel,
		.owner = owner,
		.scroll = fixture.scroll,
		.visible = visible,
		.clipped = clipped,
	};
}

void CheckCropFixtureGone(const QString &stage) {
	Check(
		false,
		u"full-owner capture self-test: %1: the fixture is alive"_q.arg(stage),
		kCropFixtureGone);
}

// E in owner coordinates: from the gradient's top to half a row past the
// owner's bottom, the owner's full width.
[[nodiscard]] QRect RequestedCrop(const Fixture &fixture) {
	const auto owner = fixture.owner.data();
	const auto gradient = fixture.gradient;
	if (!owner || !gradient) {
		return QRect();
	}
	const auto top = gradient->y();
	return QRect(0, top, owner->width(), owner->height() - top + Row() / 2);
}

// The clipped row's whole rect mapped into the owner: it fits the owner
// while its scroll viewport and the bottom overlay cut it.
[[nodiscard]] QRect ClippedRowCrop(const Fixture &fixture) {
	const auto owner = fixture.owner.data();
	const auto clipped = fixture.rows[kClippedRow];
	return (owner && clipped)
		? Ui::MapFrom(owner, clipped, clipped->rect())
		: QRect();
}

[[nodiscard]] QString CaptureText(
		const PreparedWidgetCapture &capture,
		bool accepted) {
	const auto widget = capture.widget();
	const auto &image = capture.image();
	const auto reason = capture.pendingReason();
	return u"accepted=%1 widget=%2 image=%3x%4 dpr=%5 reason=%6"_q
		.arg(accepted ? 1 : 0)
		.arg(widget ? WidgetDescription(widget) : u"<none>"_q)
		.arg(image.width())
		.arg(image.height())
		.arg(image.devicePixelRatio())
		.arg(reason.isEmpty() ? u"-"_q : reason);
}

// A refusal accepted nothing: no widget, no image and a stored reason.
[[nodiscard]] bool RefusedCapture(
		const PreparedWidgetCapture &capture,
		bool accepted) {
	return !accepted
		&& !capture.widget()
		&& capture.image().isNull()
		&& !capture.pendingReason().isEmpty();
}

// C1, at the original height: E is taken here, and the clipped row's
// mapping with it, so the growth arms reuse the very same rects.
void CheckCropBeyondOwner(not_null<State*> state) {
	auto &crop = state->crop;
	crop.requested = RequestedCrop(state->fixture);
	crop.row = ClippedRowCrop(state->fixture);
	const auto widgets = ReadCropWidgets(state->fixture);
	if (!widgets.panel) {
		CheckCropFixtureGone(u"C1 crop beyond owner"_q);
		return;
	}
	const auto owner = widgets.owner;
	const auto bounds = owner->rect();
	const auto &requested = crop.requested;
	const auto inside = bounds.intersected(requested);
	const auto failuresBefore = FailureCount();
	auto capture = PreparedWidgetCapture();
	const auto accepted = capture.prepareCrop(
		owner,
		widgets.visible,
		requested);
	const auto failuresAfter = FailureCount();
	const auto reason = capture.pendingReason();
	Check(
		RefusedCapture(capture, accepted)
			&& !requested.isEmpty()
			&& !bounds.contains(requested)
			&& reason.startsWith(u"crop: "_q)
			&& reason.contains(u"requested="_q + RectText(requested))
			&& reason.contains(u"widget="_q + RectText(bounds))
			&& reason.contains(u"rows=%1/%2"_q
				.arg(inside.height())
				.arg(requested.height()))
			&& (failuresAfter == failuresBefore),
		u"full-owner capture self-test: C1 crop beyond owner: a requested "
		u"crop reaching half a row past the painted owner is refused before "
		u"any image is accepted, naming the requested and the owner "
		u"extents, and logs no failure"_q,
		u"requested=%1 owner=%2 row=%3 failures=%4->%5 capture={%6}"_q
			.arg(RectText(requested))
			.arg(RectText(bounds))
			.arg(RectText(crop.row))
			.arg(failuresBefore)
			.arg(failuresAfter)
			.arg(CaptureText(capture, accepted)));

	const auto full = Test::GrabWidget(owner);
	const auto pixels = DevicePixelRect(requested, full.devicePixelRatio());
	const auto raw = Crop(full, pixels);
	Check(
		!raw.isNull() && (raw.height() < pixels.height()),
		u"full-owner capture self-test: C1 crop beyond owner: pitfall "
		u"premise: raw Crop of a full-owner grab returns a non-null image "
		u"shorter than the requested device height"_q,
		u"requested=%1 pixels=%2 raw=%3x%4 full=%5x%6 dpr=%7"_q
			.arg(RectText(requested))
			.arg(RectText(pixels))
			.arg(raw.width())
			.arg(raw.height())
			.arg(full.width())
			.arg(full.height())
			.arg(full.devicePixelRatio()));
}

// C2: owner-local containment and an owner-as-origin reading both accept
// the clipped row, so only the descendant's own reading refuses it.
void CheckViewportClip(const State &state) {
	const auto &row = state.crop.row;
	const auto widgets = ReadCropWidgets(state.fixture);
	if (!widgets.panel) {
		CheckCropFixtureGone(u"C2 viewport clip"_q);
		return;
	}
	const auto owner = widgets.owner;
	const auto viewport = widgets.scroll->viewport();
	const auto substituted = ReadMappedTarget(owner, owner, row);
	Check(
		!row.isEmpty()
			&& owner->rect().contains(row)
			&& (row.size() == widgets.clipped->size())
			&& substituted.resolved(),
		u"full-owner capture self-test: C2 viewport clip: premise: the "
		u"clipped row's whole mapping fits the painted owner, so owner-local "
		u"containment alone accepts it, and an owner-as-origin reading "
		u"resolves it"_q,
		u"row=%1 owner=%2 substituted={%3}"_q
			.arg(RectText(row))
			.arg(RectText(owner->rect()))
			.arg(MappedTargetDetails(substituted)));

	const auto failuresBefore = FailureCount();
	auto clipped = PreparedWidgetCapture();
	const auto clippedAccepted = clipped.prepareCrop(
		owner,
		widgets.clipped,
		row);
	auto substitution = PreparedWidgetCapture();
	const auto substitutionAccepted = substitution.prepareCrop(
		owner,
		owner,
		row);
	const auto failuresAfter = FailureCount();
	const auto reason = clipped.pendingReason();
	Check(
		RefusedCapture(clipped, clippedAccepted)
			&& reason.startsWith(u"descendant: "_q)
			&& reason.contains(kViewportRefusal)
			&& reason.contains(WidgetDescription(viewport))
			&& (failuresAfter == failuresBefore),
		u"full-owner capture self-test: C2 viewport clip: the checked crop "
		u"refuses the row its scroll viewport and the bottom overlay cut, "
		u"naming the scroll viewport, before any image is accepted"_q,
		u"viewport=%1 failures=%2->%3 capture={%4}"_q
			.arg(WidgetDescription(viewport))
			.arg(failuresBefore)
			.arg(failuresAfter)
			.arg(CaptureText(clipped, clippedAccepted)));
	Check(
		RefusedCapture(substitution, substitutionAccepted)
			&& substitution.pendingReason().startsWith(
				u"owner-as-origin"_q)
			&& (failuresAfter == failuresBefore),
		u"full-owner capture self-test: C2 viewport clip: the painted owner "
		u"handed as its own descendant is refused as an owner-as-origin "
		u"substitution"_q,
		CaptureText(substitution, substitutionAccepted));
}

// C3, the joint Part C x Part D reading, deleted with either part: Part D's
// typed vocabulary with the owner as the coordinate origin still resolves
// the clipped row, so the descendant-origin rule stays necessary, and only
// the typed reading of the row itself refuses it by its viewport.
void CheckJointTypedReading(const State &state) {
	const auto &row = state.crop.row;
	const auto widgets = ReadCropWidgets(state.fixture);
	if (!widgets.panel) {
		CheckCropFixtureGone(u"C3 joint typed reading"_q);
		return;
	}
	const auto owner = widgets.owner;
	const auto clipped = widgets.clipped;
	const auto substituted = ReadMappedTarget(
		owner,
		MappedRect::Local(owner, row));
	Check(
		!row.isEmpty() && substituted.resolved(),
		u"full-owner capture self-test: C3 joint typed reading: a typed "
		u"owner-local rect of the clipped row's mapping resolves, so the "
		u"typed vocabulary with the owner as origin cannot certify the row "
		u"either"_q,
		u"row=%1 substituted={%2}"_q.arg(
			RectText(row),
			MappedTargetDetails(substituted)));
	const auto descendant = ReadMappedTarget(
		owner,
		MappedRect::Local(clipped, clipped->rect()));
	Check(
		!descendant.resolved()
			&& descendant.refusal.contains(kViewportRefusal)
			&& (descendant.mapped == row),
		u"full-owner capture self-test: C3 joint typed reading: the typed "
		u"rect of the clipped row itself is refused by its scroll viewport, "
		u"with its whole mapping kept"_q,
		MappedTargetDetails(descendant));
}

[[nodiscard]] QString SizeText(QSize size) {
	return u"%1x%2"_q.arg(size.width()).arg(size.height());
}

[[nodiscard]] GateReading ReadGate(
		Qt::WindowStates states,
		const GateFrame &frame) {
	auto result = GateReading{
		.effective = PanelSizeModeStates(
			states,
			frame.outer,
			frame.minimum,
			frame.maximum),
	};
	result.refusal = PanelSizeModeRefusal(result.effective);
	result.text = u"%1 outer=%2 min=%3 max=%4 effective=%5: %6"_q
		.arg(WindowStatesText(states))
		.arg(SizeText(frame.outer))
		.arg(SizeText(frame.minimum))
		.arg(SizeText(frame.maximum))
		.arg(WindowStatesText(result.effective))
		.arg(result.refusal.isEmpty() ? u"sizable"_q : result.refusal);
	return result;
}

// An arm expected sizable holds when the gate clears the Maximized bit
// alone and accepts; one expected refused, when it keeps every bit and
// refuses.
void ReadGateArm(
		GateArms &arms,
		Qt::WindowStates states,
		const GateFrame &frame,
		bool sizable) {
	const auto reading = ReadGate(states, frame);
	auto cleared = states;
	cleared.setFlag(Qt::WindowMaximized, false);
	const auto held = sizable
		? (reading.refusal.isEmpty() && (reading.effective == cleared))
		: (!reading.refusal.isEmpty() && (reading.effective == states));
	arms.held = arms.held && held;
	arms.texts.push_back(reading.text);
}

// C4: the live panel's raw state, the effective state the sizing setup
// reads from it on its current frame, and that setup's decision.
void NoteLiveSizeMode(const State &state) {
	const auto panel = state.fixture.panel.get();
	if (!panel) {
		Note(u"full-owner capture self-test: C4 size modes: live panel "
			u"state <gone>"_q);
		return;
	}
	const auto raw = panel->windowState();
	const auto frame = GateFrame{
		.outer = panel->geometry().size(),
		.minimum = panel->minimumSize(),
		.maximum = panel->maximumSize(),
	};
	const auto reading = ReadGate(raw, frame);
	Note(u"full-owner capture self-test: C4 size modes: live panel state %1 "
		u"effective=%2 outer=%3 min=%4 max=%5 refusal=%6"_q
		.arg(WindowStatesText(raw))
		.arg(WindowStatesText(reading.effective))
		.arg(SizeText(frame.outer))
		.arg(SizeText(frame.minimum))
		.arg(SizeText(frame.maximum))
		.arg(reading.refusal.isEmpty() ? u"none"_q : reading.refusal));
}

// C4, pure: the classifier the sizing setup refuses through.
void CheckSizeModeClassifier() {
	const auto sizable = std::array<Qt::WindowStates, 2>{
		Qt::WindowNoState,
		Qt::WindowActive,
	};
	const auto modes = std::array{
		Qt::WindowMinimized,
		Qt::WindowMaximized,
		Qt::WindowFullScreen,
	};
	auto accepted = true;
	auto acceptedText = QStringList();
	for (const auto states : sizable) {
		const auto refusal = PanelSizeModeRefusal(states);
		accepted = accepted && refusal.isEmpty();
		acceptedText.push_back(WindowStatesText(states)
			+ u": "_q
			+ (refusal.isEmpty() ? u"sizable"_q : refusal));
	}
	auto refused = true;
	auto refusedText = QStringList();
	for (const auto mode : modes) {
		const auto variants = std::array<Qt::WindowStates, 2>{
			Qt::WindowStates(mode),
			Qt::WindowStates(mode) | Qt::WindowActive,
		};
		for (const auto states : variants) {
			const auto refusal = PanelSizeModeRefusal(states);
			refused = refused && !refusal.isEmpty();
			refusedText.push_back(WindowStatesText(states)
				+ u": "_q
				+ (refusal.isEmpty() ? u"sizable"_q : refusal));
		}
	}
	Check(
		accepted,
		u"full-owner capture self-test: C4 size modes: a normal and an "
		u"active normal panel are sizable - Qt::WindowActive is independent "
		u"of the size mode"_q,
		acceptedText.join(u"; "_q));
	Check(
		refused,
		u"full-owner capture self-test: C4 size modes: minimized, maximized "
		u"and fullscreen are each refused, alone and with "
		u"Qt::WindowActive"_q,
		refusedText.join(u"; "_q));
}

// C4, pure: the effective state the sizing setup hands that classifier.
// Only on macOS, and only for a fixed-size frame at its fixed size, is the
// Maximized bit cleared, so every arm here fails if that rule widens,
// narrows or reaches another platform.
void CheckFixedSizeZoom() {
	const auto row = Row();
	const auto fixed = QSize(row * kPanelSide, row * kPanelSide);
	const auto atSize = GateFrame{
		.outer = fixed,
		.minimum = fixed,
		.maximum = fixed,
	};
	const auto zoomed = std::array<Qt::WindowStates, 2>{
		Qt::WindowMaximized,
		Qt::WindowMaximized | Qt::WindowActive,
	};
#ifdef Q_OS_MAC
	const auto taller = fixed + QSize(0, row);
	const auto resizable = GateFrame{
		.outer = fixed,
		.minimum = fixed,
		.maximum = taller,
	};
	const auto elsewhere = GateFrame{
		.outer = taller,
		.minimum = fixed,
		.maximum = fixed,
	};
	const auto modes = std::array<Qt::WindowStates, 4>{
		Qt::WindowMinimized,
		Qt::WindowMinimized | Qt::WindowActive,
		Qt::WindowFullScreen,
		Qt::WindowFullScreen | Qt::WindowActive,
	};
	auto cleared = GateArms();
	auto zoom = GateArms();
	for (const auto states : zoomed) {
		ReadGateArm(cleared, states, atSize, true);
		ReadGateArm(zoom, states, resizable, false);
		ReadGateArm(zoom, states, elsewhere, false);
	}
	auto kept = GateArms();
	for (const auto states : modes) {
		ReadGateArm(kept, states, atSize, false);
	}
	Check(
		cleared.held,
		u"full-owner capture self-test: C4 size modes: on macOS a fixed-size "
		u"panel at its fixed size reading maximized, alone or with "
		u"Qt::WindowActive, is sizable with its Active bit kept - Qt cocoa "
		u"reports NSWindow.zoomed, which such a window always is, as "
		u"Qt::WindowMaximized"_q,
		cleared.texts.join(u"; "_q));
	Check(
		zoom.held,
		u"full-owner capture self-test: C4 size modes: on macOS the same "
		u"maximized readings are refused when the minimum differs from the "
		u"maximum or the outer size from the fixed size, where a zoom does "
		u"change the frame"_q,
		zoom.texts.join(u"; "_q));
	Check(
		kept.held,
		u"full-owner capture self-test: C4 size modes: on macOS minimized and "
		u"fullscreen are never cleared - each is refused on a fixed frame at "
		u"its size, alone and with Qt::WindowActive"_q,
		kept.texts.join(u"; "_q));
#else // Q_OS_MAC
	auto refused = GateArms();
	for (const auto states : zoomed) {
		ReadGateArm(refused, states, atSize, false);
	}
	Check(
		refused.held,
		u"full-owner capture self-test: C4 size modes: off macOS no bit is "
		u"cleared - maximized, alone and with Qt::WindowActive, is refused "
		u"on a fixed frame at its size"_q,
		refused.texts.join(u"; "_q));
#endif // Q_OS_MAC
}

// C4, pure apart from its NOTE: the classifier, then the effective state.
void CheckSizeModes(const State &state) {
	NoteLiveSizeMode(state);
	CheckSizeModeClassifier();
	CheckFixedSizeZoom();
}

// The extra height that fits both the clipped row with the bottom reserve
// the overlay keeps below the scroll, and E: both measured, not assumed.
void MeasureGrowth(not_null<State*> state) {
	auto &crop = state->crop;
	const auto widgets = ReadCropWidgets(state->fixture);
	if (!widgets.panel) {
		return;
	}
	const auto owner = widgets.owner;
	const auto scroll = widgets.scroll;
	crop.ownerHeight = owner->height();
	crop.bottomReserve = owner->height() - (scroll->y() + scroll->height());
	crop.extra = std::max(
		crop.row.y() + crop.row.height() + crop.bottomReserve,
		crop.requested.y() + crop.requested.height()) - owner->height();
}

// Grows by the measured extra. Nothing is owed here: the normal restore
// runs whenever a growth is owed, before any later growth.
void GrowPanel(not_null<State*> state) {
	state->crop.waitFrom = crl::now();
	const auto widgets = ReadCropWidgets(state->fixture);
	state->sizing = widgets.panel
		? GrowPanelInnerHeight(widgets.panel, state->crop.extra)
		: PanelSizing{ .refusal = kCropFixtureGone };
}

[[nodiscard]] bool SizingWaitOver(const State &state) {
	return (crl::now() - state.crop.waitFrom) >= kSizingSettle;
}

[[nodiscard]] bool GrowthSettled(const State &state) {
	const auto &sizing = state.sizing;
	return !sizing.owed
		|| !sizing.panel
		|| PanelSizingApplied(sizing)
		|| SizingWaitOver(state);
}

[[nodiscard]] bool RestoreSettled(const State &state) {
	const auto &sizing = state.sizing;
	return !sizing.panel
		|| PanelSizingRestored(sizing)
		|| SizingWaitOver(state);
}

[[nodiscard]] QString OwedReason(const State &state) {
	const auto &sizing = state.sizing;
	return sizing.owed
		? QString()
		: u"no panel sizing is owed: %1"_q.arg(sizing.refusal.isEmpty()
			? u"nothing is grown"_q
			: sizing.refusal);
}

// The effective state a sizing froze, the one the sizing helpers compare.
[[nodiscard]] Qt::WindowStates FrozenEffectiveState(
		const PanelSizing &sizing) {
	return PanelSizeModeStates(
		sizing.state,
		sizing.outer.size(),
		sizing.minimum,
		sizing.maximum);
}

// Two sizings froze the same frame of the same panel. The state compared is
// the effective one: the raw macOS zoom reading of a fixed-size panel flips
// on the helper's own resize, so C8 can freeze 0 where C5 froze 2.
[[nodiscard]] bool SameFrozenFrame(
		const PanelSizing &a,
		const PanelSizing &b) {
	return (a.panel == b.panel)
		&& (a.outer == b.outer)
		&& (a.body == b.body)
		&& (a.request == b.request)
		&& (a.minimum == b.minimum)
		&& (a.maximum == b.maximum)
		&& (FrozenEffectiveState(a) == FrozenEffectiveState(b));
}

[[nodiscard]] QString GrowthText(const State &state) {
	const auto &crop = state.crop;
	return u"extra=%1 rowBottom=%2 reserve=%3 cropBottom=%4 ownerHeight=%5 "
		u"grown={outer=%6 body=%7} sameOriginal=%8 sizing={%9}"_q
		.arg(crop.extra)
		.arg(crop.row.y() + crop.row.height())
		.arg(crop.bottomReserve)
		.arg(crop.requested.y() + crop.requested.height())
		.arg(crop.ownerHeight)
		.arg(RectText(crop.grownOuter))
		.arg(RectText(crop.grownBody))
		.arg(SameFrozenFrame(state.sizing, crop.original) ? 1 : 0)
		.arg(PanelSizingText(state.sizing));
}

// The growth was applied through setInnerSize with full telemetry: every
// frozen value recorded, the request grown by exactly the extra, the body
// grown by it and the effective window state unchanged. It froze the
// original frame: trivially for C5, which records it, and measured for C8.
void CheckGrowth(not_null<State*> state, const QString &what) {
	auto &crop = state->crop;
	const auto &sizing = state->sizing;
	if (const auto panel = sizing.panel.data()) {
		crop.grownOuter = panel->geometry();
		crop.grownBody = panel->innerGeometry();
	}
	const auto telemetry = !sizing.outer.isEmpty()
		&& !sizing.body.isEmpty()
		&& !sizing.request.isEmpty()
		&& !sizing.minimum.isEmpty()
		&& !sizing.maximum.isEmpty();
	Check(
		sizing.refusal.isEmpty()
			&& sizing.owed
			&& (crop.extra > 0)
			&& telemetry
			&& (sizing.applied == sizing.request + QSize(0, crop.extra))
			&& SameFrozenFrame(sizing, crop.original)
			&& PanelSizingApplied(sizing),
		what,
		GrowthText(*state));
}

// C5, after the relayout: the composite owner fills the grown body.
void CheckGrownOwner(const State &state) {
	const auto &crop = state.crop;
	const auto widgets = ReadCropWidgets(state.fixture);
	if (!widgets.panel) {
		CheckCropFixtureGone(u"C5 grow"_q);
		return;
	}
	const auto owner = widgets.owner;
	const auto clipped = widgets.clipped;
	Check(
		(crop.extra > 0)
			&& (owner->height() == crop.ownerHeight + crop.extra)
			&& (owner->geometry() == widgets.panel->innerGeometry()),
		u"full-owner capture self-test: C5 grow: the composite owner laid "
		u"out in the grown body is exactly the measured extra taller"_q,
		u"owner=%1 body=%2 scroll=%3 %4 row={%5}"_q
			.arg(RectText(owner->geometry()))
			.arg(RectText(widgets.panel->innerGeometry()))
			.arg(RectText(widgets.scroll->geometry()))
			.arg(GrowthText(state))
			.arg(MappedTargetDetails(
				ReadMappedTarget(owner, clipped, clipped->rect()))));
}

// C6: the same E and the same row mapping C1 and C2 refused, now accepted
// whole; the expected device extent is the self-test's own product of the
// requested size and the ratio of its own full-owner grab.
void CheckGrownAccepted(const State &state) {
	const auto &crop = state.crop;
	const auto widgets = ReadCropWidgets(state.fixture);
	if (!widgets.panel) {
		CheckCropFixtureGone(u"C6 accept"_q);
		return;
	}
	const auto owner = widgets.owner;
	const auto full = Test::GrabWidget(owner);
	const auto ratio = full.devicePixelRatio();
	const auto expected = crop.requested.size() * ratio;
	const auto expectedRow = crop.row.size() * ratio;
	auto grown = PreparedWidgetCapture();
	const auto grownAccepted = grown.prepareCrop(
		owner,
		widgets.clipped,
		crop.requested);
	auto visible = PreparedWidgetCapture();
	const auto visibleAccepted = visible.prepareCrop(
		owner,
		widgets.visible,
		crop.requested);
	auto row = PreparedWidgetCapture();
	const auto rowAccepted = row.prepareCrop(
		owner,
		widgets.clipped,
		crop.row);
	const auto saved = grownAccepted
		&& grown.save(u"full_owner_crop_grown"_q);
	const auto liveRow = ClippedRowCrop(state.fixture);
	const auto whole = [&](
			const PreparedWidgetCapture &capture,
			bool accepted,
			const QSize &size) {
		return accepted
			&& (capture.widget() == owner)
			&& (capture.image().size() == size)
			&& (capture.image().devicePixelRatio() == ratio);
	};
	const auto details = QStringList{
		u"requested=%1 row=%2 liveRow=%3 owner=%4"_q
			.arg(RectText(crop.requested))
			.arg(RectText(crop.row))
			.arg(RectText(liveRow))
			.arg(RectText(owner->rect())),
		u"full=%1x%2 dpr=%3 expected=%4x%5 expectedRow=%6x%7 saved=%8"_q
			.arg(full.width())
			.arg(full.height())
			.arg(ratio)
			.arg(expected.width())
			.arg(expected.height())
			.arg(expectedRow.width())
			.arg(expectedRow.height())
			.arg(saved ? 1 : 0),
		u"grown={"_q + CaptureText(grown, grownAccepted) + u"}"_q,
		u"visible={"_q + CaptureText(visible, visibleAccepted) + u"}"_q,
		u"row={"_q + CaptureText(row, rowAccepted) + u"}"_q,
	}.join(u" "_q);
	Check(
		whole(grown, grownAccepted, expected) && saved,
		u"full-owner capture self-test: C6 accept: after the growth the same "
		u"requested region, with the row its viewport clipped before as its "
		u"descendant, is accepted with its entire requested device extent "
		u"at the full render's ratio and saved"_q,
		details);
	Check(
		whole(visible, visibleAccepted, expected)
			&& whole(row, rowAccepted, expectedRow)
			&& (liveRow == crop.row),
		u"full-owner capture self-test: C6 accept: the wholly visible row "
		u"with the same region and the formerly clipped row with its own "
		u"unchanged mapping are accepted whole too"_q,
		details);
}

[[nodiscard]] QString RestoreText(const State &state) {
	const auto &crop = state.crop;
	return u"grown={outer=%1 body=%2} sameOriginal=%3 sizing={%4}"_q
		.arg(RectText(crop.grownOuter))
		.arg(RectText(crop.grownBody))
		.arg(SameFrozenFrame(state.sizing, crop.original) ? 1 : 0)
		.arg(PanelSizingText(state.sizing));
}

// C7: the frozen values are the original frame, so restored means the
// exact original outer and body geometry, request, constraints and
// effective state.
void CheckNormalRestore(not_null<State*> state) {
	LayoutFixture(&state->fixture);
	Check(
		PanelSizingRestored(state->sizing)
			&& SameFrozenFrame(state->sizing, state->crop.original),
		u"full-owner capture self-test: C7 normal restore: the panel's exact "
		u"original outer and body geometry, inner request, minimum and "
		u"maximum size and effective window state are restored"_q,
		RestoreText(*state));
	const auto widgets = ReadCropWidgets(state->fixture);
	if (!widgets.panel) {
		CheckCropFixtureGone(u"C7 normal restore"_q);
		return;
	}
	const auto owner = widgets.owner;
	const auto clipped = widgets.clipped;
	const auto side = Row() * kPanelSide;
	const auto reading = ReadMappedTarget(owner, clipped, clipped->rect());
	Check(
		(owner->size() == QSize(side, side))
			&& !reading.resolved()
			&& reading.refusal.contains(kViewportRefusal),
		u"full-owner capture self-test: C7 normal restore: the relaid "
		u"composite owner is %1 rows square again and its scroll viewport "
		u"clips row %2 again"_q.arg(kPanelSide).arg(kClippedRow),
		u"owner=%1 row={%2}"_q.arg(
			RectText(owner->geometry()),
			MappedTargetDetails(reading)));
}

// C8: restored by the very call ReleaseFixture makes on every finish path,
// with neither a normal restore nor a relayout before it, back to the
// original frame C5 froze.
void CheckAbortRestore(const State &state) {
	Check(
		PanelSizingRestored(state.sizing)
			&& SameFrozenFrame(state.sizing, state.crop.original),
		u"full-owner capture self-test: C8 controlled abort: the finish-path "
		u"release restores the panel's exact original outer and body "
		u"geometry, inner request, minimum and maximum size and effective "
		u"window state"_q,
		RestoreText(state));
}

void AppendCropBeyondOwnerStage(
		not_null<Runner*> runner,
		not_null<State*> state) {
	runner->add({
		.name = u"full-owner capture self-test: C1 crop beyond owner"_q,
		.then = [=] {
			CheckCropBeyondOwner(state);
		},
	});
}

void AppendViewportClipStage(
		not_null<Runner*> runner,
		not_null<State*> state) {
	runner->add({
		.name = u"full-owner capture self-test: C2 viewport clip"_q,
		.then = [=] {
			CheckViewportClip(*state);
		},
	});
}

// Joint Part C x Part D, reading only: deleted with either part.
void AppendJointTypedStage(
		not_null<Runner*> runner,
		not_null<State*> state) {
	runner->add({
		.name = u"full-owner capture self-test: C3 joint typed reading"_q,
		.then = [=] {
			CheckJointTypedReading(*state);
		},
	});
}

void AppendSizeModesStage(
		not_null<Runner*> runner,
		not_null<State*> state) {
	runner->add({
		.name = u"full-owner capture self-test: C4 size modes"_q,
		.then = [=] {
			CheckSizeModes(*state);
		},
	});
}

void AppendGrowStages(not_null<Runner*> runner, not_null<State*> state) {
	runner->add({
		.name = u"full-owner capture self-test: C5 grow: the typed panel "
			u"grows by the measured extra"_q,
		.run = [=] {
			MeasureGrowth(state);
			GrowPanel(state);
			state->crop.original = state->sizing;
		},
		.until = [=] {
			return GrowthSettled(*state);
		},
		.then = [=] {
			CheckGrowth(
				state,
				u"full-owner capture self-test: C5 grow: the typed panel's "
				u"inner height grows by the measured extra through "
				u"setInnerSize, with full telemetry and its effective window "
				u"state unchanged"_q);
		},
		.timeoutDetails = [=] {
			return GrowthText(*state);
		},
	});
	runner->add({
		.name = u"full-owner capture self-test: C5 grow: lay the composite "
			u"owner out in the grown body"_q,
		.run = [=] {
			LayoutFixture(&state->fixture);
			state->crop.waitFrom = crl::now();
		},
		.until = [=] {
			const auto owner = state->fixture.owner.data();
			const auto &crop = state->crop;
			return !owner
				|| (owner->height() == crop.ownerHeight + crop.extra)
				|| SizingWaitOver(*state);
		},
		.then = [=] {
			CheckGrownOwner(*state);
		},
		.timeoutDetails = [=] {
			return GrowthText(*state);
		},
	});
}

void AppendGrownAcceptStage(
		not_null<Runner*> runner,
		not_null<State*> state) {
	runner->add({
		.name = u"full-owner capture self-test: C6 accept"_q,
		.then = [=] {
			CheckGrownAccepted(*state);
		},
	});
}

// A recovery stage, run whenever a growth is owed: a refused stage before
// it never leaves the panel grown.
void AppendNormalRestoreStage(
		not_null<Runner*> runner,
		not_null<State*> state) {
	runner->addRecovery({
		.name = u"full-owner capture self-test: C7 normal restore"_q,
		.skipReason = [=] {
			return OwedReason(*state);
		},
		.run = [=] {
			RestorePanelSizing(state->sizing);
			state->crop.waitFrom = crl::now();
		},
		.until = [=] {
			return RestoreSettled(*state);
		},
		.then = [=] {
			CheckNormalRestore(state);
		},
		.timeoutDetails = [=] {
			return RestoreText(*state);
		},
	});
}

// The growth stage is gated with the group; the restore is a recovery
// stage, and if the run stops in between, the teardown's ReleaseFixture
// restores what is owed.
void AppendAbortStages(not_null<Runner*> runner, not_null<State*> state) {
	runner->add({
		.name = u"full-owner capture self-test: C8 controlled abort: grow "
			u"again by the same extra"_q,
		.run = [=] {
			GrowPanel(state);
		},
		.until = [=] {
			return GrowthSettled(*state);
		},
		.then = [=] {
			CheckGrowth(
				state,
				u"full-owner capture self-test: C8 controlled abort: the "
				u"same growth is applied again from the original frame"_q);
		},
		.timeoutDetails = [=] {
			return GrowthText(*state);
		},
	});
	runner->addRecovery({
		.name = u"full-owner capture self-test: C8 controlled abort: restore "
			u"through the finish-path release"_q,
		.skipReason = [=] {
			return OwedReason(*state);
		},
		.run = [=] {
			RestorePanelSizing(state->sizing);
			state->crop.waitFrom = crl::now();
		},
		.until = [=] {
			return RestoreSettled(*state);
		},
		.then = [=] {
			CheckAbortRestore(*state);
		},
		.timeoutDetails = [=] {
			return RestoreText(*state);
		},
	});
}

// Group C, Part C: the checked full-owner crop and the typed panel sizing
// that makes the clipped row measurable. It renders in-process, so it needs
// the shown fixture only, never a paint.
void AppendCropGroup(not_null<Runner*> runner, not_null<State*> state) {
	AppendCropBeyondOwnerStage(runner, state);
	AppendViewportClipStage(runner, state);
	AppendJointTypedStage(runner, state);
	AppendSizeModesStage(runner, state);
	AppendGrowStages(runner, state);
	AppendGrownAcceptStage(runner, state);
	AppendNormalRestoreStage(runner, state);
	AppendAbortStages(runner, state);
}

void AppendTeardownStage(not_null<Runner*> runner, not_null<State*> state) {
	runner->addRecovery({
		.name = u"full-owner capture self-test: teardown"_q,
		.run = [=] {
			ReleaseFixture(*state);
		},
		.then = [=] {
			const auto &fixture = state->fixture;
			const auto released = !fixture.panel
				&& !fixture.owner
				&& !fixture.first
				&& !fixture.second;
			Check(
				released,
				u"full-owner capture self-test: teardown released the panel "
				u"and every fixture widget"_q,
				u"panel=%1 owner=%2 first=%3 second=%4"_q
					.arg(fixture.panel ? 1 : 0)
					.arg(fixture.owner ? 1 : 0)
					.arg(fixture.first ? 1 : 0)
					.arg(fixture.second ? 1 : 0));
			Check(
				!state->sizing.owed,
				u"full-owner capture self-test: teardown owes no panel "
				u"sizing: every growth was restored before the panel was "
				u"released"_q,
				PanelSizingText(state->sizing));
			Check(
				!state->restoredRead || !state->restoredExposure.stayOnTop,
				u"full-owner capture self-test: teardown cleared the "
				u"exposure hint before releasing the panel"_q,
				state->restoredRead
					? (u"kept={%1} restored={%2}"_q
						.arg(WindowExposureText(state->exposure))
						.arg(WindowExposureText(state->restoredExposure)))
					: u"no panel was alive at release"_q);
		},
	});
}

} // namespace

void AppendFullOwnerCaptureSelfTest(not_null<Runner*> runner) {
	// Leaked on purpose, as this directory's other self-tests leak theirs:
	// the stages outlive this call.
	const auto state = new State();
	runner->onFinish([=] {
		ReleaseFixture(*state);
	});
	AppendFixtureStage(runner, state);
	// Every group below needs the fixture the first stage certified.
	runner->addGated([=] { return state->fixtureGate; }, [&] {
		AppendPaintGroup(runner, state);
		AppendStrictGroup(runner, state);
		AppendCropGroup(runner, state);
	});
	AppendTeardownStage(runner, state);
}

} // namespace Test

#endif // _DEBUG
