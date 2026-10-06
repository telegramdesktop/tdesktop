/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_post_paint.h"

#include "base/event_filter.h"
#include "base/invoke_queued.h"
#include "base/timer.h"
#include "base/unique_qptr.h"
#include "core/application.h"
#include "test/test_capture.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "window/window_controller.h"

#include <QtCore/QCoreApplication>
#include <QtGui/QPainter>
#include <QtGui/QWindow>

#include "styles/palette.h"
#include "styles/style_widgets.h"

namespace Test {
namespace {

constexpr auto kGrabFreeLead = crl::time(40);

[[nodiscard]] bool AcceptWindow(
		const QString &name,
		const QString &kind,
		crl::time from,
		crl::time till) {
	if (from < till) {
		return true;
	}
	Note(u"%1: ignored an empty %2 window from=%3 till=%4"_q
		.arg(name)
		.arg(kind)
		.arg(from)
		.arg(till));
	return false;
}

} // namespace

QString PostPaintSampleText(const PostPaintSample &sample) {
	return u"kind=post seq=%1 paint=%2 last=%3 covered=%4 lo=%5 hi=%6"_q
		.arg(sample.seq)
		.arg(sample.paintAt)
		.arg(sample.lastPaintAt)
		.arg(sample.covered)
		.arg(sample.lo)
		.arg(sample.hi);
}

PostPaintSampler::PostPaintSampler(
	QString name,
	not_null<QWidget*> owner,
	Fn<void(int seq)> sample)
: _name(std::move(name))
, _owner(owner.get())
, _sample(std::move(sample)) {
	const auto filter = base::install_event_filter(owner, [=](
			not_null<QEvent*> e) {
		if (e->type() == QEvent::Paint) {
			paintReceived();
		}
		return base::EventFilterResult::Continue;
	});
	_filter = filter.get();
	QObject::connect(owner.get(), &QObject::destroyed, filter.get(), [=] {
		finish(u"owner-destroyed"_q);
	});
}

PostPaintSampler::~PostPaintSampler() {
	delete _filter.data();
}

void PostPaintSampler::sampleDuring(crl::time from, crl::time till) {
	if (AcceptWindow(_name, u"sampling"_q, from, till)) {
		_windows.push_back({ from, till });
	}
}

void PostPaintSampler::grabFreeDuring(crl::time from, crl::time till) {
	if (AcceptWindow(_name, u"grab-free"_q, from, till)) {
		_grabFree.push_back({ from, till });
	}
}

void PostPaintSampler::paintReceived() {
	if (!_stopReason.isEmpty()) {
		return;
	}
	if (_sampling) {
		++_ignored;
		return;
	}
	const auto now = crl::now();
	++_paints;
	if (!sampling(now) || grabFree(now)) {
		return;
	}
	if (_pending) {
		++_pendingCovered;
		_pendingLastAt = now;
		return;
	}
	_pending = true;
	_pendingCovered = 1;
	_pendingFirstAt = now;
	_pendingLastAt = now;
	post();
}

void PostPaintSampler::post() {
	InvokeQueued(QCoreApplication::instance(), [weak = base::make_weak(this)] {
		if (const auto strong = weak.get()) {
			strong->runPending();
		}
	});
}

void PostPaintSampler::runPending() {
	if (!base::take(_pending) || !_stopReason.isEmpty()) {
		return;
	}
	if (!_owner) {
		finish(u"owner-destroyed"_q);
		return;
	}
	if (grabFree(crl::now())) {
		++_dropped;
		return;
	}
	auto row = PostPaintSample{
		.seq = int(_samples.size()) + 1,
		.paintAt = base::take(_pendingFirstAt),
		.lastPaintAt = base::take(_pendingLastAt),
		.covered = base::take(_pendingCovered),
	};
	const auto weak = base::make_weak(this);
	_sampling = true;
	row.lo = crl::now();
	_sample(row.seq);
	row.hi = crl::now();
	if (!weak) {
		return;
	}
	_sampling = false;
	_longest = std::max(_longest, row.hi - row.lo);
	_samples.push_back(row);
	Note(_name + u": "_q + PostPaintSampleText(row));
}

void PostPaintSampler::finish(const QString &reason) {
	if (!_stopReason.isEmpty()) {
		return;
	}
	_stopReason = reason;
	const auto pending = base::take(_pending);
	Note(u"%1: kind=stop reason=%2 seq=%3 pending=%4 samples=%5 paints=%6 "
		"ignored=%7 dropped=%8"_q
			.arg(_name)
			.arg(_stopReason)
			.arg(_samples.empty() ? 0 : _samples.back().seq)
			.arg(pending ? 1 : 0)
			.arg(int(_samples.size()))
			.arg(_paints)
			.arg(_ignored)
			.arg(_dropped));
}

bool PostPaintSampler::sampling(crl::time now) const {
	return ranges::any_of(_windows, [&](const Window &window) {
		return (window.from <= now) && (now < window.till);
	});
}

bool PostPaintSampler::grabFree(crl::time now) const {
	const auto lead = std::max(kGrabFreeLead, _longest);
	return ranges::any_of(_grabFree, [&](const Window &window) {
		return (now < window.till) && (now + lead > window.from);
	});
}

int PostPaintSampler::mark() const {
	return int(_samples.size());
}

std::vector<PostPaintSample> PostPaintSampler::samplesSince(int mark) const {
	auto result = std::vector<PostPaintSample>();
	for (const auto &sample : _samples) {
		if (sample.seq > mark) {
			result.push_back(sample);
		}
	}
	return result;
}

bool PostPaintSampler::pending() const {
	return _pending;
}

QString PostPaintSampler::stopReason() const {
	return _stopReason;
}

QString PostPaintSampler::details() const {
	const auto owner = _stopReason.isEmpty() ? _owner.data() : nullptr;
	return u"owner=%1 pending=%2 stop=%3 samples=%4 paints=%5 ignored=%6 "
		"dropped=%7 windows=%8 grabFree=%9 longest=%10"_q
			.arg(owner ? WidgetDescription(owner) : u"destroyed"_q)
			.arg(_pending ? 1 : 0)
			.arg(_stopReason.isEmpty() ? u"-"_q : _stopReason)
			.arg(int(_samples.size()))
			.arg(_paints)
			.arg(_ignored)
			.arg(_dropped)
			.arg(int(_windows.size()))
			.arg(int(_grabFree.size()))
			.arg(_longest);
}

namespace {

constexpr auto kPaintCost = crl::time(18);
constexpr auto kWarmupPaints = 5;
constexpr auto kWarmupTimeout = crl::time(3000);
constexpr auto kControlCadence = crl::time(16);
constexpr auto kStressWindow = crl::time(1500);
constexpr auto kMinStressPaints = 20;
constexpr auto kDrain = crl::time(150);
constexpr auto kSettleQuiet = crl::time(100);
constexpr auto kIdleSampleWindow = crl::time(1200);
constexpr auto kIdleWindowMin = crl::time(500);
constexpr auto kGrabFreeAt = crl::time(400);
constexpr auto kGrabFreeLength = crl::time(300);
constexpr auto kGrabFreeSampleWindow = crl::time(1200);
constexpr auto kMinGrabFreePaints = 3;
constexpr auto kDestroyWindow = crl::time(1000);
constexpr auto kDestroyObserve = crl::time(300);
constexpr auto kReceiptSkew = crl::time(1);
constexpr auto kWidthFactor = 4;
constexpr auto kHeightFactor = 2;
constexpr auto kRowsShown = 3;
constexpr auto kIdleRowsShown = 5;
constexpr auto kSettleTimeout = crl::time(3000);

// One paint of the synthetic owner, stamped by its own paint callback.
// |grab| is the test's own flag, set only by TakeGrab around its GrabRect,
// so the records tell product paints from grab paints without trusting the
// sampler's recursion guard.
struct PaintRecord {
	crl::time start = 0;
	crl::time end = 0;
	bool grab = false;
};

// One sample of the timer-only control: when it was due, when its timer
// fired, and the brackets around its grab.
struct ControlTick {
	crl::time due = 0;
	crl::time fired = 0;
	crl::time lo = 0;
	crl::time hi = 0;
};

struct PaintFixture {
	base::unique_qptr<Ui::RpWidget> widget;
	std::vector<PaintRecord> paints;
	int frame = 0;
	int sampleCalls = 0;
	int controlCalls = 0;
	int nullImages = 0;
	bool continuous = false;
	bool grabbing = false;
};

struct GateReading {
	QString details;
	bool ok = false;
};

struct Gap {
	crl::time length = 0;
	crl::time hi = 0;
	crl::time lo = 0;
	bool found = false;
};

struct Lateness {
	crl::time paintMs = 0;
	int inside = 0;
	int overdueStarts = 0;
};

struct Coverage {
	QString firstUncovered;
	QString worst;
	crl::time delayMax = 0;
	int uncovered = 0;
};

struct ReceiptSkew {
	crl::time max = 0;
	int unmatched = 0;
};

// One reading of the owner-destroyed stage: the product paints since its
// mark from the fixture's own records, the sampler's rows since its mark,
// the action invocations the test counted itself, and whether the sampler
// held a pending sample.
struct DestroyReading {
	int productPaints = 0;
	int rows = 0;
	int sampleCalls = 0;
	bool pending = false;
};

// Every paint costs about kPaintCost, like the source's 16-20 ms Debug
// paints. A product paint of a continuous fixture requests the next one
// from inside the paint, so Qt posts a QUpdateLaterEvent after the sample
// the sampler's filter posted for this paint; a grab paint requests
// nothing.
void PaintFixtureFrame(
		not_null<PaintFixture*> fixture,
		not_null<Ui::RpWidget*> widget,
		QPainter &p) {
	const auto start = crl::now();
	const auto grab = fixture->grabbing;
	const auto odd = ((++fixture->frame) % 2) != 0;
	const auto &color = odd ? st::windowBgActive : st::windowBg;
	p.fillRect(widget->rect(), color);
	while (crl::now() - start < kPaintCost) {
	}
	if (fixture->continuous && !grab) {
		widget->update();
	}
	fixture->paints.push_back({
		.start = start,
		.end = crl::now(),
		.grab = grab,
	});
}

[[nodiscard]] bool BuildPaintFixture(not_null<PaintFixture*> fixture) {
	const auto window = Core::App().activePrimaryWindow();
	if (!window) {
		return false;
	}
	const auto row = st::defaultActiveButton.height;
	fixture->widget = base::make_unique_q<Ui::RpWidget>(
		window->widget().get());
	const auto raw = fixture->widget.get();
	raw->setAttribute(Qt::WA_OpaquePaintEvent);
	raw->setGeometry(0, 0, row * kWidthFactor, row * kHeightFactor);
	raw->paintOn([=](QPainter &p) {
		PaintFixtureFrame(fixture, raw, p);
	});
	raw->show();
	raw->raise();
	Ui::SendPendingMoveResizeEvents(raw);
	return true;
}

// The one sample action of both the sampler and the timer-only control.
// The call is counted first: an action invoked after the owner died must
// still be counted, because the destroyed case counts invocations, not
// successful grabs.
void TakeGrab(not_null<PaintFixture*> fixture, int &calls) {
	++calls;
	const auto widget = fixture->widget.get();
	if (!widget) {
		return;
	}
	fixture->grabbing = true;
	const auto image = GrabRect(widget, widget->rect());
	fixture->grabbing = false;
	if (image.isNull()) {
		++fixture->nullImages;
	}
}

[[nodiscard]] ControlTick TakeControlTick(
		not_null<PaintFixture*> fixture,
		crl::time due) {
	auto result = ControlTick{
		.due = due,
		.fired = crl::now(),
	};
	result.lo = crl::now();
	TakeGrab(fixture, fixture->controlCalls);
	result.hi = crl::now();
	return result;
}

[[nodiscard]] std::vector<PaintRecord> PaintsSince(
		const PaintFixture &fixture,
		int fromIndex,
		bool grab) {
	auto result = std::vector<PaintRecord>();
	const auto count = int(fixture.paints.size());
	for (auto i = std::max(fromIndex, 0); i < count; ++i) {
		if (fixture.paints[i].grab == grab) {
			result.push_back(fixture.paints[i]);
		}
	}
	return result;
}

[[nodiscard]] std::vector<PaintRecord> ProductPaints(
		const PaintFixture &fixture,
		int fromIndex) {
	return PaintsSince(fixture, fromIndex, false);
}

[[nodiscard]] std::vector<PaintRecord> GrabPaints(
		const PaintFixture &fixture,
		int fromIndex) {
	return PaintsSince(fixture, fromIndex, true);
}

// Paints whose own start stamp lies in [from, till).
[[nodiscard]] std::vector<PaintRecord> StartingIn(
		const std::vector<PaintRecord> &paints,
		crl::time from,
		crl::time till) {
	auto result = std::vector<PaintRecord>();
	for (const auto &paint : paints) {
		if (paint.start >= from && paint.start < till) {
			result.push_back(paint);
		}
	}
	return result;
}

[[nodiscard]] QString PaintText(const PaintRecord &paint) {
	return u"%1..%2"_q.arg(paint.start).arg(paint.end);
}

[[nodiscard]] crl::time PaintCostMax(
		const std::vector<PaintRecord> &paints) {
	auto result = crl::time(0);
	for (const auto &paint : paints) {
		result = std::max(result, paint.end - paint.start);
	}
	return result;
}

[[nodiscard]] QString CostText(const std::vector<PaintRecord> &paints) {
	if (paints.empty()) {
		return u"costMin=- costMax=-"_q;
	}
	auto min = paints.front().end - paints.front().start;
	for (const auto &paint : paints) {
		min = std::min(min, paint.end - paint.start);
	}
	return u"costMin=%1 costMax=%2"_q.arg(min).arg(PaintCostMax(paints));
}

[[nodiscard]] crl::time GrabCostMax(
		const std::vector<PostPaintSample> &rows) {
	auto result = crl::time(0);
	for (const auto &row : rows) {
		result = std::max(result, row.hi - row.lo);
	}
	return result;
}

// One product paint plus one grab, both read from the same stage's
// records: the longest paint and the longest sample action.
[[nodiscard]] crl::time Bound(
		const std::vector<PaintRecord> &paints,
		const std::vector<PostPaintSample> &rows) {
	return PaintCostMax(paints) + GrabCostMax(rows);
}

// Product paints p_i with no sample lo in [p_i.end, p_(i+1).start), the
// last one against |windowEnd|. The same function counts the helper's and
// the timer-only control's misses, so the two numbers printed side by side
// are computed the same way.
template <typename Row>
[[nodiscard]] int MissedPaints(
		const std::vector<PaintRecord> &paints,
		const std::vector<Row> &rows,
		crl::time windowEnd) {
	auto result = 0;
	const auto count = int(paints.size());
	for (auto i = 0; i < count; ++i) {
		const auto from = paints[i].end;
		const auto till = (i + 1 < count) ? paints[i + 1].start : windowEnd;
		const auto followed = ranges::any_of(rows, [&](const Row &row) {
			return (row.lo >= from) && (row.lo < till);
		});
		if (!followed) {
			++result;
		}
	}
	return result;
}

// The largest lo(n + 1) - hi(n) over consecutive rows, with its pair.
template <typename Row>
[[nodiscard]] Gap GapMax(const std::vector<Row> &rows) {
	auto result = Gap();
	for (auto i = 1; i < int(rows.size()); ++i) {
		const auto length = rows[i].lo - rows[i - 1].hi;
		if (!result.found || length > result.length) {
			result = Gap{
				.length = length,
				.hi = rows[i - 1].hi,
				.lo = rows[i].lo,
				.found = true,
			};
		}
	}
	return result;
}

[[nodiscard]] QString GapText(const Gap &gap) {
	return gap.found
		? u"gapMax=%1 (hi=%2 -> lo=%3)"_q
			.arg(gap.length)
			.arg(gap.hi)
			.arg(gap.lo)
		: u"gapMax=-"_q;
}

[[nodiscard]] QString RowsText(
		const std::vector<PostPaintSample> &rows,
		int limit) {
	auto list = QStringList();
	for (const auto &row : rows) {
		if (int(list.size()) >= limit) {
			break;
		}
		list.push_back(PostPaintSampleText(row));
	}
	return list.join(u"; "_q);
}

// A child widget has no window handle of its own: exposure is read from
// the top-level window that composes the fixture's backing store.
[[nodiscard]] GateReading ReadFixtureGate(
		const PaintFixture &fixture,
		crl::time startedAt) {
	const auto widget = fixture.widget.get();
	if (!widget) {
		return {
			.details = u"window=none: Core::App().activePrimaryWindow() "
				"is null"_q,
		};
	}
	const auto window = widget->window();
	const auto handle = window->windowHandle();
	const auto exposed = handle && handle->isExposed();
	const auto minimized = window->isMinimized();
	const auto hidden = window->isHidden();
	const auto widgetHidden = widget->isHidden();
	const auto paints = ProductPaints(fixture, 0);
	const auto painted = (int(paints.size()) >= kWarmupPaints);
	return {
		.details = u"exposed=%1 minimized=%2 hidden=%3 widgetHidden=%4 "
			"widget=%5 window=%6 productPaints=%7 (need %8) %9 "
			"elapsed=%10"_q
				.arg(exposed ? 1 : 0)
				.arg(minimized ? 1 : 0)
				.arg(hidden ? 1 : 0)
				.arg(widgetHidden ? 1 : 0)
				.arg(WidgetDescription(widget))
				.arg(WidgetDescription(window))
				.arg(int(paints.size()))
				.arg(kWarmupPaints)
				.arg(CostText(paints))
				.arg(crl::now() - startedAt),
		.ok = exposed && !minimized && !hidden && !widgetHidden && painted,
	};
}

// The product paints overlapping one tick's [due, fired], their summed
// duration, and how many of them the loop started while the tick was
// already overdue - the direct reading of "paints starve the timer".
[[nodiscard]] Lateness ReadLateness(
		const std::vector<PaintRecord> &paints,
		const ControlTick &tick) {
	auto result = Lateness();
	for (const auto &paint : paints) {
		if (paint.end <= tick.due || paint.start >= tick.fired) {
			continue;
		}
		++result.inside;
		result.paintMs += paint.end - paint.start;
		if (paint.start > tick.due + kReceiptSkew) {
			++result.overdueStarts;
		}
	}
	return result;
}

// control{cadence= window=[a,b) samples= gapMax= (hi= -> lo=) missed=M/P
// lateMax= (due= fired= paintsInside= paintMs= overdueStarts=)
// overdueStartsTotal= lateMedian= lateNoPaintMax=}, where |paints| are the
// product paints since the control stage began.
[[nodiscard]] QString ControlSummary(
		const std::vector<PaintRecord> &paints,
		const std::vector<ControlTick> &ticks,
		crl::time from,
		crl::time till) {
	const auto inside = StartingIn(paints, from, till);
	const auto missed = MissedPaints(inside, ticks, till + kDrain);
	auto latest = ControlTick();
	auto latestReading = Lateness();
	auto lateMax = crl::time(0);
	auto lateNoPaintMax = crl::time(0);
	auto noPaintTicks = 0;
	auto overdueStartsTotal = 0;
	auto lateness = std::vector<crl::time>();
	for (const auto &tick : ticks) {
		const auto late = tick.fired - tick.due;
		const auto reading = ReadLateness(paints, tick);
		if (lateness.empty() || late > lateMax) {
			lateMax = late;
			latest = tick;
			latestReading = reading;
		}
		if (!reading.inside) {
			lateNoPaintMax = noPaintTicks
				? std::max(lateNoPaintMax, late)
				: late;
			++noPaintTicks;
		}
		lateness.push_back(late);
		overdueStartsTotal += reading.overdueStarts;
	}
	ranges::sort(lateness);
	const auto median = lateness.empty()
		? u"-"_q
		: QString::number(lateness[lateness.size() / 2]);
	const auto latestText = ticks.empty()
		? u"lateMax=-"_q
		: u"lateMax=%1 (due=%2 fired=%3 paintsInside=%4 paintMs=%5 "
			"overdueStarts=%6)"_q
				.arg(lateMax)
				.arg(latest.due)
				.arg(latest.fired)
				.arg(latestReading.inside)
				.arg(latestReading.paintMs)
				.arg(latestReading.overdueStarts);
	return u"control{cadence=%1 window=[%2,%3) samples=%4 %5 missed=%6/%7 "
		"%8 overdueStartsTotal=%9 lateMedian=%10 lateNoPaintMax=%11}"_q
			.arg(kControlCadence)
			.arg(from)
			.arg(till)
			.arg(int(ticks.size()))
			.arg(GapText(GapMax(ticks)))
			.arg(missed)
			.arg(int(inside.size()))
			.arg(latestText)
			.arg(overdueStartsTotal)
			.arg(median)
			.arg(noPaintTicks
				? QString::number(lateNoPaintMax)
				: u"-"_q);
}

// For each paint, its covering sample is the first row taken after the
// paint returned (lo >= end); the delay is lo - end.
[[nodiscard]] Coverage ReadCoverage(
		const std::vector<PaintRecord> &paints,
		const std::vector<PostPaintSample> &rows) {
	auto result = Coverage{
		.firstUncovered = u"-"_q,
		.worst = u"-"_q,
	};
	for (const auto &paint : paints) {
		const auto i = ranges::find_if(rows, [&](const PostPaintSample &row) {
			return row.lo >= paint.end;
		});
		if (i == end(rows)) {
			if (!result.uncovered++) {
				result.firstUncovered = PaintText(paint);
			}
			continue;
		}
		const auto delay = i->lo - paint.end;
		if (result.worst == u"-"_q || delay > result.delayMax) {
			result.delayMax = delay;
			result.worst = u"paint=%1 -> seq=%2 lo=%3 hi=%4"_q
				.arg(PaintText(paint))
				.arg(i->seq)
				.arg(i->lo)
				.arg(i->hi);
		}
	}
	return result;
}

// Each row's receipt stamp against the paint callback's own start stamp of
// the paint it was received for; crl::now() truncation allows kReceiptSkew.
[[nodiscard]] ReceiptSkew ReadReceiptSkew(
		const std::vector<PaintRecord> &paints,
		const std::vector<PostPaintSample> &rows) {
	auto result = ReceiptSkew();
	for (const auto &row : rows) {
		const auto i = ranges::find_if(paints, [&](const PaintRecord &paint) {
			const auto skew = paint.start - row.paintAt;
			return (skew >= 0) && (skew <= kReceiptSkew);
		});
		if (i == end(paints)) {
			++result.unmatched;
		} else {
			result.max = std::max(result.max, i->start - row.paintAt);
		}
	}
	return result;
}

// |paints| are the product paints since the stressed stage began and
// |rows| the samples since its mark.
void CheckStressed(
		const std::vector<PaintRecord> &paints,
		const std::vector<PostPaintSample> &rows,
		crl::time from,
		crl::time till,
		const QString &controlSummary) {
	const auto inside = StartingIn(paints, from + kReceiptSkew, till);
	const auto count = int(inside.size());
	Check(
		count >= kMinStressPaints,
		u"post-paint sampler self-test: stressed: fixture: the widget "
		"repainted back to back through the window"_q,
		u"paints=%1 (need %2) %3 window=[%4,%5)"_q
			.arg(count)
			.arg(kMinStressPaints)
			.arg(CostText(inside))
			.arg(from)
			.arg(till));

	const auto coverage = ReadCoverage(inside, rows);
	const auto bound = Bound(inside, rows);
	const auto skew = ReadReceiptSkew(paints, rows);
	auto coveredSum = 0;
	auto badRows = 0;
	for (const auto &row : rows) {
		coveredSum += row.covered;
		if (row.covered < 1 || row.lo < row.lastPaintAt) {
			++badRows;
		}
	}
	const auto details = QStringList{
		u"window=[%1,%2) paints=%3 samples=%4"_q
			.arg(from)
			.arg(till)
			.arg(count)
			.arg(int(rows.size())),
		u"uncovered=%1 firstUncovered=%2"_q
			.arg(coverage.uncovered)
			.arg(coverage.firstUncovered),
		u"delayMax=%1 (%2)"_q.arg(coverage.delayMax).arg(coverage.worst),
		u"paintCostMax=%1 grabCostMax=%2 bound=%3"_q
			.arg(PaintCostMax(inside))
			.arg(GrabCostMax(rows))
			.arg(bound),
		u"coveredSum=%1 shared=%2 badRows=%3"_q
			.arg(coveredSum)
			.arg(coveredSum - int(rows.size()))
			.arg(badRows),
		u"helper{%1 missed=%2/%3}"_q
			.arg(GapText(GapMax(rows)))
			.arg(MissedPaints(inside, rows, till + kDrain))
			.arg(count),
		u"receiptSkewMax=%1 unmatched=%2"_q
			.arg(skew.max)
			.arg(skew.unmatched),
		u"rows=[%1]"_q.arg(RowsText(rows, kRowsShown)),
		controlSummary,
	}.join(u" "_q);
	Check(
		(coverage.uncovered == 0)
			&& (coverage.delayMax <= bound)
			&& (badRows == 0),
		u"post-paint sampler self-test: stressed: every product paint is "
		"covered by a sample taken after it returned, within one product "
		"paint plus one grab"_q,
		details);
}

// True once the last paint record, product or grab, ended kSettleQuiet ago.
[[nodiscard]] bool Settled(const PaintFixture &fixture) {
	return fixture.paints.empty()
		|| (crl::now() - fixture.paints.back().end >= kSettleQuiet);
}

[[nodiscard]] QString LastPaintText(const PaintFixture &fixture) {
	if (fixture.paints.empty()) {
		return u"last=-"_q;
	}
	const auto &last = fixture.paints.back();
	return u"last=%1 grab=%2 ago=%3"_q
		.arg(PaintText(last))
		.arg(last.grab ? 1 : 0)
		.arg(crl::now() - last.end);
}

[[nodiscard]] QString PaintsText(
		const std::vector<PaintRecord> &paints,
		int limit) {
	auto list = QStringList();
	for (const auto &paint : paints) {
		if (int(list.size()) >= limit) {
			break;
		}
		list.push_back(PaintText(paint));
	}
	return list.join(u"; "_q);
}

// The product paint a row was received for: the paint callback's own start
// stamp lies within kReceiptSkew after the row's receipt stamp.
[[nodiscard]] const PaintRecord *ReceivedPaint(
		const std::vector<PaintRecord> &paints,
		const PostPaintSample &row) {
	const auto i = ranges::find_if(paints, [&](const PaintRecord &paint) {
		const auto skew = paint.start - row.paintAt;
		return (skew >= 0) && (skew <= kReceiptSkew);
	});
	return (i != end(paints)) ? &*i : nullptr;
}

// |paints| and |grabs| are the product and the grab paints since the
// stage's mark and |rows| the samples since its mark, all read from the
// fixture's own records and the sampler's rows. The idle window runs from
// the first sample's hi to the end of the sampling window [from, till).
void CheckRecursionGuard(
		const std::vector<PaintRecord> &paints,
		const std::vector<PaintRecord> &grabs,
		const std::vector<PostPaintSample> &rows,
		crl::time from,
		crl::time till,
		const QString &samplerDetails) {
	const auto first = rows.empty() ? nullptr : &rows.front();
	const auto further = rows.empty() ? 0 : (int(rows.size()) - 1);
	const auto idle = first ? (till - first->hi) : crl::time(0);
	const auto premise = (paints.size() > 1)
		? u"stray-paint (%1 product paints, the stage requested one)"_q
			.arg(int(paints.size()))
		: paints.empty()
		? u"no-product-paint"_q
		: u"one-product-paint"_q;
	const auto delay = (first && !paints.empty())
		? QString::number(first->lo - paints.front().end)
		: u"-"_q;
	const auto details = QStringList{
		u"window=[%1,%2) premise=%3"_q.arg(from).arg(till).arg(premise),
		u"productPaints=%1 [%2]"_q
			.arg(int(paints.size()))
			.arg(PaintsText(paints, kIdleRowsShown)),
		u"grabPaints=%1 [%2]"_q
			.arg(int(grabs.size()))
			.arg(PaintsText(grabs, kRowsShown)),
		u"samples=%1 further=%2 delay=%3"_q
			.arg(int(rows.size()))
			.arg(further)
			.arg(delay),
		(first
			? u"idle=[%1,%2) length=%3 (need %4)"_q
				.arg(first->hi)
				.arg(till)
				.arg(idle)
				.arg(kIdleWindowMin)
			: u"idle=- (no sample)"_q),
		u"rows=[%1]"_q.arg(RowsText(rows, kIdleRowsShown)),
		u"sampler{%1}"_q.arg(samplerDetails),
	}.join(u" "_q);
	Check(
		(paints.size() == 1)
			&& (rows.size() == 1)
			&& (first->covered == 1)
			&& (first->lo >= paints.front().end),
		u"post-paint sampler self-test: recursion guard: one product paint "
		"in an enabled window yields exactly one sample, taken after it "
		"returned"_q,
		details);
	Check(
		!grabs.empty()
			&& (first != nullptr)
			&& (further == 0)
			&& (idle >= kIdleWindowMin),
		u"post-paint sampler self-test: recursion guard: no further sample "
		"over an idle window of at least 500 ms although the grab painted "
		"the widget"_q,
		details);
}

// |paints| are the product paints since the stage's mark and |rows| the
// samples since its mark; [gfFrom, gfTill) is the grab-free window inside
// the sampling window [from, till) and |lead| the sampler's lead now.
void CheckGrabFree(
		const std::vector<PaintRecord> &paints,
		const std::vector<PostPaintSample> &rows,
		crl::time from,
		crl::time till,
		crl::time gfFrom,
		crl::time gfTill,
		crl::time lead,
		const QString &samplerDetails) {
	const auto inside = StartingIn(paints, gfFrom, gfTill);
	auto overlapping = std::vector<PostPaintSample>();
	auto before = 0;
	for (const auto &row : rows) {
		if (row.hi > gfFrom && row.lo < gfTill) {
			overlapping.push_back(row);
		} else if (row.hi <= gfFrom) {
			++before;
		}
	}

	// The first sample taken after the window. A paint received inside it,
	// lead included, posts nothing, so that sample must follow a paint
	// received at or after |gfTill|.
	const auto i = ranges::find_if(rows, [&](const PostPaintSample &row) {
		return row.lo >= gfTill;
	});
	const auto resumed = (i != end(rows)) ? &*i : nullptr;
	const auto paint = resumed ? ReceivedPaint(paints, *resumed) : nullptr;

	// Product paints the sampler certainly received after the window but
	// before the paint that first sample follows: any of them should have
	// been it.
	const auto sampledAt = paint
		? paint->start
		: resumed
		? resumed->paintAt
		: till;
	auto skipped = std::vector<PaintRecord>();
	for (const auto &record : paints) {
		if (record.start >= gfTill + 2 * kReceiptSkew
			&& record.start < sampledAt) {
			skipped.push_back(record);
		}
	}
	const auto bound = Bound(paints, rows);
	const auto delay = (resumed && paint)
		? QString::number(resumed->lo - paint->end)
		: u"-"_q;
	const auto details = QStringList{
		u"window=[%1,%2) grabFree=[%3,%4) lead=%5"_q
			.arg(from)
			.arg(till)
			.arg(gfFrom)
			.arg(gfTill)
			.arg(lead),
		u"inside=%1 (need %2) [%3]"_q
			.arg(int(inside.size()))
			.arg(kMinGrabFreePaints)
			.arg(PaintsText(inside, kRowsShown)),
		u"overlapping=%1 [%2] before=%3 samples=%4"_q
			.arg(int(overlapping.size()))
			.arg(RowsText(overlapping, kRowsShown))
			.arg(before)
			.arg(int(rows.size())),
		u"resumed={%1} resumedPaintAt=%2 (need >= %3) paint=%4 delay=%5"_q
			.arg(resumed ? PostPaintSampleText(*resumed) : u"-"_q)
			.arg(resumed ? QString::number(resumed->paintAt) : u"-"_q)
			.arg(gfTill)
			.arg(paint ? PaintText(*paint) : u"-"_q)
			.arg(delay),
		u"skipped=%1 [%2]"_q
			.arg(int(skipped.size()))
			.arg(PaintsText(skipped, kRowsShown)),
		u"paintCostMax=%1 grabCostMax=%2 bound=%3"_q
			.arg(PaintCostMax(paints))
			.arg(GrabCostMax(rows))
			.arg(bound),
		u"sampler{%1}"_q.arg(samplerDetails),
	}.join(u" "_q);
	Check(
		(int(inside.size()) >= kMinGrabFreePaints)
			&& overlapping.empty()
			&& (before > 0),
		u"post-paint sampler self-test: grab-free window: product paints "
		"continue inside it and no sample is taken there"_q,
		details);
	Check(
		(resumed != nullptr)
			&& (resumed->paintAt >= gfTill)
			&& (paint != nullptr)
			&& skipped.empty()
			&& (resumed->lo >= paint->end)
			&& (resumed->lo - paint->end <= bound),
		u"post-paint sampler self-test: grab-free window: the first product "
		"paint after it is followed by a sample taken after it returned"_q,
		details);
}

// Taken by the destroy call D right before it destroys the owner, and again
// by the stage's checks, so both readings are made the same way.
[[nodiscard]] DestroyReading ReadDestroy(
		const PaintFixture &fixture,
		const PostPaintSampler *sampler,
		int paintMark,
		int sampleMark) {
	return {
		.productPaints = int(ProductPaints(fixture, paintMark).size()),
		.rows = sampler ? int(sampler->samplesSince(sampleMark).size()) : 0,
		.sampleCalls = fixture.sampleCalls,
		.pending = sampler && sampler->pending(),
	};
}

[[nodiscard]] QString DestroyReadingText(const DestroyReading &reading) {
	return u"productPaints=%1 rows=%2 sampleCalls=%3 pending=%4"_q
		.arg(reading.productPaints)
		.arg(reading.rows)
		.arg(reading.sampleCalls)
		.arg(reading.pending ? 1 : 0);
}

// |paints| are the product paints since the stage's mark. The premise row
// makes "no grab afterwards" non-vacuous: a sample was pending when the
// owner died, and none had run yet.
void CheckOwnerDestroyed(
		const std::vector<PaintRecord> &paints,
		const DestroyReading &atDestroy,
		const DestroyReading &after,
		bool posted,
		crl::time destroyedAt,
		const PostPaintSampler &sampler) {
	const auto stop = sampler.stopReason();
	const auto details = QStringList{
		u"destroyPosted=%1 destroyedAt=%2 paints=[%3]"_q
			.arg(posted ? 1 : 0)
			.arg(destroyedAt)
			.arg(PaintsText(paints, kRowsShown)),
		u"atDestroy{%1} after{%2}"_q
			.arg(DestroyReadingText(atDestroy))
			.arg(DestroyReadingText(after)),
		u"stop=%1"_q.arg(stop.isEmpty() ? u"-"_q : stop),
		u"sampler{%1}"_q.arg(sampler.details()),
	}.join(u" "_q);
	Check(
		posted
			&& (atDestroy.productPaints == 1)
			&& atDestroy.pending
			&& (atDestroy.rows == 0),
		u"post-paint sampler self-test: owner destroyed: fixture: one "
		"product paint left a sample pending when the owner was "
		"destroyed"_q,
		details);
	Check(
		(after.sampleCalls == atDestroy.sampleCalls)
			&& (after.rows == atDestroy.rows)
			&& (stop == u"owner-destroyed"_q)
			&& !after.pending,
		u"post-paint sampler self-test: owner destroyed: no grab is taken "
		"afterwards and sampling stops with the reason logged"_q,
		details);
}

} // namespace

void AppendPostPaintSamplerSelfTest(not_null<Runner*> runner) {
	struct State {
		PaintFixture fixture;
		std::unique_ptr<PostPaintSampler> sampler;
		base::Timer control;
		std::vector<ControlTick> ticks;
		QString fixtureGate;
		QString controlSummary;
		crl::time controlDue = 0;
		crl::time controlFrom = 0;
		crl::time controlTill = 0;
		crl::time from = 0;
		crl::time till = 0;
		crl::time gfFrom = 0;
		crl::time gfTill = 0;
		crl::time destroyedAt = 0;
		int paintMark = 0;
		int sampleMark = 0;
		DestroyReading atDestroy;
		bool destroyPosted = false;
	};
	const auto state = std::make_shared<State>();
	// The timer and the sampler's action are owned by |state| itself, so
	// they hold it by a plain pointer and keep no ownership cycle.
	const auto raw = state.get();

	// The sampler goes before the widget: its destructor removes its filter
	// from the owner while the owner still lives.
	runner->onFinish([=] {
		state->control.cancel();
		state->sampler = nullptr;
		state->fixture.widget = nullptr;
	});

	state->control.setCallback([=] {
		raw->ticks.push_back(TakeControlTick(&raw->fixture, raw->controlDue));
		const auto now = crl::now();
		if (now < raw->controlTill) {
			raw->controlDue = now + kControlCadence;
			raw->control.callOnce(kControlCadence);
		}
	});

	const auto fixtureGate = [=] {
		return state->fixtureGate;
	};
	const auto progress = [=] {
		return u"paints=%1 ticks=%2 sampleCalls=%3 controlCalls=%4 "
			"sampler{%5}"_q
				.arg(int(state->fixture.paints.size()))
				.arg(int(state->ticks.size()))
				.arg(state->fixture.sampleCalls)
				.arg(state->fixture.controlCalls)
				.arg(state->sampler
					? state->sampler->details()
					: u"none"_q);
	};
	const auto gate = u"post-paint sampler self-test: fixture gate: the "
		"synthetic widget is shown in an exposed primary window and "
		"repaints through the backing store"_q;

	runner->add({
		.name = u"post-paint sampler self-test: fixture and warm-up"_q,
		.run = [=] {
			state->from = crl::now();
			if (!BuildPaintFixture(&state->fixture)) {
				state->fixtureGate = u"fixture gate: no primary window"_q;
				return;
			}
			const auto fixture = &state->fixture;
			const auto widget = fixture->widget.get();
			state->sampler = std::make_unique<PostPaintSampler>(
				u"post-paint self-test"_q,
				widget,
				[=](int) {
					TakeGrab(fixture, fixture->sampleCalls);
				});
			fixture->continuous = true;
			widget->update();
		},
		.until = [=] {
			const auto paints = ProductPaints(state->fixture, 0);
			return !state->fixture.widget
				|| (int(paints.size()) >= kWarmupPaints)
				|| (crl::now() - state->from >= kWarmupTimeout);
		},
		.then = [=] {
			const auto reading = ReadFixtureGate(state->fixture, state->from);
			Check(reading.ok, gate, reading.details);
			if (reading.ok) {
				return;
			}
			if (state->fixtureGate.isEmpty()) {
				state->fixtureGate = gate;
			}
			state->fixture.continuous = false;
			state->sampler = nullptr;
			state->fixture.widget = nullptr;
		},
		.timeoutDetails = progress,
	});

	runner->add({
		.name = u"post-paint sampler self-test: timer-only control at a "
			"16 ms cadence"_q,
		.skipReason = fixtureGate,
		.run = [=] {
			const auto now = crl::now();
			state->paintMark = int(state->fixture.paints.size());
			state->ticks.clear();
			state->controlFrom = now;
			state->controlTill = now + kStressWindow;
			state->controlDue = now + kControlCadence;
			state->control.callOnce(kControlCadence);
		},
		.until = [=] {
			return crl::now() >= state->controlTill + kDrain;
		},
		.then = [=] {
			state->control.cancel();
			state->controlSummary = ControlSummary(
				ProductPaints(state->fixture, state->paintMark),
				state->ticks,
				state->controlFrom,
				state->controlTill);
			Note(u"post-paint sampler self-test: "_q + state->controlSummary);
		},
		.timeoutDetails = progress,
	});

	runner->add({
		.name = u"post-paint sampler self-test: stressed: every product "
			"paint is followed by its own sample"_q,
		.skipReason = fixtureGate,
		.run = [=] {
			state->sampleMark = state->sampler->mark();
			state->paintMark = int(state->fixture.paints.size());
			state->from = crl::now();
			state->till = state->from + kStressWindow;
			state->sampler->sampleDuring(state->from, state->till);
		},
		.until = [=] {
			return crl::now() >= state->till + kDrain;
		},
		.then = [=] {
			CheckStressed(
				ProductPaints(state->fixture, state->paintMark),
				state->sampler->samplesSince(state->sampleMark),
				state->from,
				state->till,
				state->controlSummary);
		},
		.timeoutDetails = progress,
	});

	// Stops the continuous repaint and waits until no paint ended for
	// kSettleQuiet, so the next stage's one product paint is its own.
	const auto addSettle = [=](const QString &name) {
		runner->add({
			.name = name,
			.skipReason = fixtureGate,
			.run = [=] {
				state->fixture.continuous = false;
			},
			.until = [=] {
				return Settled(state->fixture);
			},
			.timeout = kSettleTimeout,
			.timeoutDetails = [=] {
				return LastPaintText(state->fixture) + u" "_q + progress();
			},
		});
	};

	addSettle(u"post-paint sampler self-test: recursion guard: settle"_q);

	runner->add({
		.name = u"post-paint sampler self-test: recursion guard: one "
			"product paint, then an idle window"_q,
		.skipReason = fixtureGate,
		.run = [=] {
			state->sampleMark = state->sampler->mark();
			state->paintMark = int(state->fixture.paints.size());
			state->from = crl::now();
			state->till = state->from + kIdleSampleWindow;
			state->sampler->sampleDuring(state->from, state->till);
			state->fixture.widget->update();
		},
		.until = [=] {
			return crl::now() >= state->till + kDrain;
		},
		.then = [=] {
			CheckRecursionGuard(
				ProductPaints(state->fixture, state->paintMark),
				GrabPaints(state->fixture, state->paintMark),
				state->sampler->samplesSince(state->sampleMark),
				state->from,
				state->till,
				state->sampler->details());
		},
		.timeoutDetails = progress,
	});

	runner->add({
		.name = u"post-paint sampler self-test: grab-free window"_q,
		.skipReason = fixtureGate,
		.run = [=] {
			const auto sampler = state->sampler.get();
			state->sampleMark = sampler->mark();
			state->paintMark = int(state->fixture.paints.size());
			state->from = crl::now();
			state->till = state->from + kGrabFreeSampleWindow;
			state->gfFrom = state->from + kGrabFreeAt;
			state->gfTill = state->gfFrom + kGrabFreeLength;
			sampler->sampleDuring(state->from, state->till);
			sampler->grabFreeDuring(state->gfFrom, state->gfTill);
			state->fixture.continuous = true;
			state->fixture.widget->update();
		},
		.until = [=] {
			return crl::now() >= state->till + kDrain;
		},
		.then = [=] {
			const auto sampler = state->sampler.get();

			// The lead the sampler applies now: max(kGrabFreeLead, the
			// longest action so far), read from all of its rows.
			const auto lead = std::max(
				kGrabFreeLead,
				GrabCostMax(sampler->samplesSince(0)));
			CheckGrabFree(
				ProductPaints(state->fixture, state->paintMark),
				sampler->samplesSince(state->sampleMark),
				state->from,
				state->till,
				state->gfFrom,
				state->gfTill,
				lead,
				sampler->details());
		},
		.timeoutDetails = progress,
	});

	addSettle(u"post-paint sampler self-test: owner destroyed: settle"_q);

	// The destroy call D. It reads the premise, then destroys the owner
	// while the sampler's own sample S for the same paint is still queued.
	// The stage's filter, which the widget owns, holds it, so it captures
	// |state| by the plain pointer; the posted call checks a weak pointer.
	const auto destroyOwner = [=] {
		raw->atDestroy = ReadDestroy(
			raw->fixture,
			raw->sampler.get(),
			raw->paintMark,
			raw->sampleMark);
		raw->fixture.widget = nullptr;
		raw->destroyedAt = crl::now();
	};
	const auto readDestroy = [=] {
		return ReadDestroy(
			state->fixture,
			state->sampler.get(),
			state->paintMark,
			state->sampleMark);
	};

	runner->add({
		.name = u"post-paint sampler self-test: owner destroyed with a "
			"sample pending"_q,
		.skipReason = fixtureGate,
		.run = [=] {
			const auto widget = state->fixture.widget.get();
			state->sampleMark = state->sampler->mark();
			state->paintMark = int(state->fixture.paints.size());
			const auto now = crl::now();
			state->sampler->sampleDuring(now, now + kDestroyWindow);

			// Installed after the sampler's filter, so Qt calls it first:
			// D is posted before the sampler posts S for the same paint,
			// and posted calls of one priority run in order, so D runs
			// first whatever path delivered the paint. The filter is a
			// child of the widget and dies with it.
			const auto weak = std::weak_ptr<State>(state);
			base::install_event_filter(widget, [=](not_null<QEvent*> e) {
				if (e->type() == QEvent::Paint
					&& !raw->fixture.grabbing
					&& !raw->destroyPosted) {
					raw->destroyPosted = true;
					InvokeQueued(QCoreApplication::instance(), [=] {
						if (const auto strong = weak.lock()) {
							destroyOwner();
						}
					});
				}
				return base::EventFilterResult::Continue;
			});
			widget->update();
		},
		.until = [=] {
			return (state->destroyedAt != 0)
				&& (crl::now() >= state->destroyedAt + kDestroyObserve);
		},
		.then = [=] {
			CheckOwnerDestroyed(
				ProductPaints(state->fixture, state->paintMark),
				state->atDestroy,
				readDestroy(),
				state->destroyPosted,
				state->destroyedAt,
				*state->sampler);
		},
		.timeoutDetails = [=] {
			return u"destroyPosted=%1 now{%2} %3"_q
				.arg(state->destroyPosted ? 1 : 0)
				.arg(DestroyReadingText(readDestroy()))
				.arg(progress());
		},
	});

	runner->add({
		.name = u"post-paint sampler self-test: the stage after the owner "
			"was destroyed runs"_q,
		.skipReason = fixtureGate,
		.then = [=] {
			// Releasing a sampler whose owner already died runs its
			// destructor with the filter pointer already null.
			const auto reason = state->sampler
				? state->sampler->stopReason()
				: u"no-sampler"_q;
			state->sampler = nullptr;
			Check(
				(reason == u"owner-destroyed"_q)
					&& !state->sampler
					&& !state->fixture.widget,
				u"post-paint sampler self-test: the next stage runs after "
				"the owner was destroyed and the stopped sampler is "
				"released"_q,
				u"ran=1 stop=%1 widget=%2 nullImages=%3"_q
					.arg(reason)
					.arg(state->fixture.widget ? u"alive"_q : u"released"_q)
					.arg(state->fixture.nullImages));
		},
	});
}

} // namespace Test

#endif // _DEBUG
