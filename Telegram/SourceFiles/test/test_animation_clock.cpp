/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_animation_clock.h"

#include "base/unique_qptr.h"
#include "core/application.h"
#include "test/test_capture.h"
#include "test/test_ink.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "test/test_widgets.h"
#include "ui/effects/animation_value.h"
#include "ui/effects/animations.h"
#include "ui/style/style_core_scale.h"
#include "ui/rp_widget.h"
#include "window/window_controller.h"

#include <QtCore/QPointer>
#include <QtGui/QPainter>
#include <QtGui/QWindow>

#include <algorithm>
#include <chrono>
#include <thread>

#include "styles/style_widgets.h"

namespace Test {
namespace {

// The probe's own record: how many times the manager called it, and the
// |now| of its last call.
struct Probe {
	int calls = 0;
	crl::time now = -1;
};

[[nodiscard]] QString ElapsedText(const std::vector<crl::time> &elapsed) {
	auto parts = QStringList();
	for (const auto value : elapsed) {
		parts.push_back(QString::number(value));
	}
	return parts.isEmpty() ? u"none"_q : parts.join(u',');
}

[[nodiscard]] QString RequestRefusal(const ClockedRequest &request) {
	if (request.name.isEmpty()) {
		return u"the request has no name"_q;
	} else if (bool(request.action) == bool(request.handler)) {
		return u"exactly one of action and handler must be set "
			"(action=%1 handler=%2)"_q
				.arg(request.action ? 1 : 0)
				.arg(request.handler ? 1 : 0);
	} else if (!request.render) {
		return u"no render was supplied"_q;
	} else if (request.elapsed.empty()) {
		return u"no elapsed time was requested"_q;
	}
	auto previous = crl::time(-1);
	for (const auto value : request.elapsed) {
		if (value <= previous) {
			return u"elapsed times must be non-negative and strictly "
				"increasing, read %1"_q.arg(ElapsedText(request.elapsed));
		}
		previous = value;
	}
	return QString();
}

// One requested time: gate, wait, drain, tick, prove, bound, gate, render.
void TakeFrame(
		const ClockedRequest &request,
		const ClockedRun &run,
		const Probe &probe,
		not_null<ClockedFrame*> frame) {
	const auto end = request.animationEnd;
	if (end > 0 && frame->requested >= end) {
		frame->gate = u"requested %1 ms is at or after the declared "
			"animation end %2 ms: no in-progress frame exists to judge, so "
			"nothing was waited for, ticked or rendered"_q
				.arg(frame->requested)
				.arg(end);
		return;
	}
	const auto deadline = run.actionStarted + frame->requested;
	const auto waitFrom = crl::now();
	while (crl::now() < deadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	frame->waited = crl::now() - waitFrom;

	SettlePostponedCalls();
	const auto beforeTick = probe.calls;
	Core::App().animationManager().update();
	frame->probeTicks = probe.calls - beforeTick;
	if (!frame->probeTicks) {
		frame->refusal = u"manager-did-not-tick: "
			"Core::App().animationManager().update() did not call the "
			"probe animation (probe calls %1 before and after it, %2 ms "
			"after the action started, waited %3 ms): the manager skipped "
			"the tick - a schedule callback still pending (_scheduled; "
			"under Manager::SetScheduleWithInvokeQueued(true) it is an "
			"InvokeQueued call the postponed-call drain does not run) or "
			"an update in progress (_updating); the frame was not "
			"rendered"_q
				.arg(beforeTick)
				.arg(crl::now() - run.actionStarted)
				.arg(frame->waited);
		return;
	}
	frame->tick = probe.now;
	frame->reachedLo = frame->tick - run.actionFinished;
	frame->reachedHi = frame->tick - run.actionStarted;
	if (end > 0 && frame->reachedHi >= end) {
		frame->gate = u"the tick reached [%1,%2] ms, at or after the "
			"declared animation end %3 ms: the animation may have "
			"finished, so no in-progress frame exists to judge; the frame "
			"was not rendered"_q
				.arg(frame->reachedLo)
				.arg(frame->reachedHi)
				.arg(end);
		return;
	}

	const auto beforeRender = probe.calls;
	request.render(*frame);
	frame->renderTicks = probe.calls - beforeRender;
	if (frame->renderTicks) {
		frame->refusal = u"render-advanced-the-manager: the probe "
			"animation was called %1 time(s) during the render, so the "
			"frame may show a value past its reached elapsed bounds; "
			"render synchronously (Test::GrabWidget / GrabRect), never "
			"through repaint() or processEvents()"_q
				.arg(frame->renderTicks);
	}
}

void ReportFrame(const QString &name, const ClockedFrame &frame) {
	const auto what = u"%1: frame %2 at %3 ms"_q
		.arg(name)
		.arg(frame.index + 1)
		.arg(frame.requested);
	if (!frame.gate.isEmpty()) {
		Skipped(what, frame.gate);
	} else if (!frame.refusal.isEmpty()) {
		Fail(u"fixture gate: "_q + what, frame.refusal);
	} else {
		Note(name + u": "_q + ClockedFrameText(frame));
	}
}

} // namespace

bool ClockedFrame::taken() const {
	return gate.isEmpty() && refusal.isEmpty() && (tick >= 0);
}

int ClockedRun::taken() const {
	return int(ranges::count_if(frames, &ClockedFrame::taken));
}

ClockedRun RunClockedFrames(const ClockedRequest &request) {
	auto result = ClockedRun{ .animationEnd = request.animationEnd };
	result.refusal = RequestRefusal(request);
	if (!result.refusal.isEmpty()) {
		const auto name = request.name.isEmpty()
			? u"(unnamed)"_q
			: request.name;
		Fail(
			u"fixture gate: %1: clocked frames refused"_q.arg(name),
			result.refusal);
		return result;
	}

	auto probe = Probe();
	auto probeAnimation = Ui::Animations::Basic([&](crl::time now) {
		++probe.calls;
		probe.now = now;
		return true;
	});
	probeAnimation.start();

	Settle([&] {
		result.actionStarted = crl::now();
		if (request.handler) {
			request.handler->onClick(request.context);
		} else {
			request.action();
		}
	});
	result.actionFinished = crl::now();

	const auto count = int(request.elapsed.size());
	result.frames.reserve(count);
	for (auto index = 0; index != count; ++index) {
		auto frame = ClockedFrame{
			.index = index,
			.requested = request.elapsed[index],
		};
		TakeFrame(request, result, probe, &frame);
		ReportFrame(request.name, frame);
		result.frames.push_back(std::move(frame));
	}
	return result;
}

QString ClockedFrameText(const ClockedFrame &frame) {
	const auto ticked = (frame.tick >= 0);
	auto result = u"kind=frame index=%1 requested=%2 reached=%3 tick=%4 "
		"waited=%5 probeTicks=%6 renderTicks=%7"_q
			.arg(frame.index + 1)
			.arg(frame.requested)
			.arg(ticked
				? u"[%1,%2]"_q.arg(frame.reachedLo).arg(frame.reachedHi)
				: u"none"_q)
			.arg(ticked ? QString::number(frame.tick) : u"none"_q)
			.arg(frame.waited)
			.arg(frame.probeTicks)
			.arg(frame.renderTicks);
	if (!frame.gate.isEmpty()) {
		result += u" gate="_q + frame.gate;
	}
	if (!frame.refusal.isEmpty()) {
		result += u" refused="_q + frame.refusal;
	}
	return result;
}

namespace {

constexpr auto kDuration = crl::time(200);
constexpr auto kWidthFactor = 8;
constexpr auto kBarTolerance = 8;
constexpr auto kMaxOtherPixels = 2;
constexpr auto kInProgressFrames = 3;
constexpr auto kSheetZoom = 2;

// Owned by the bar's lifetime(), so the Simple dies with the bar, before
// the animation manager (see the header). |rendering| is set by the test's
// own render around its grab, so the paint counts tell grab paints from any
// other paint without trusting the helper.
struct BarFixture {
	Ui::Animations::Simple animation;
	crl::time handlerAt = 0;
	int handlerCalls = 0;
	int paints = 0;
	int renderPaints = 0;
	bool rendering = false;
	bool clicked = false;
};

// The bar's own visibility and its top-level window's state, printed beside
// the frames.
struct WindowReading {
	QString window;
	bool bar = false;
	bool barHidden = false;
	bool barVisible = false;
	bool minimized = false;
	bool windowHidden = false;
	bool exposed = false;
	bool screenLocked = false;
};

// The middle device row of one rendered frame: |filled| bar pixels and
// |other| pixels matching neither colour, of |width|.
struct BarReading {
	int filled = 0;
	int width = 0;
	int other = 0;
	double value = 0.;
	QString refusal;
};

// What the test's render kept of one frame, keyed by the frame's |index|.
// |simple| is the Simple's own value at the render, context only.
struct FrameShot {
	int index = 0;
	QImage image;
	BarReading bar;
	double simple = 0.;
};

struct State {
	base::unique_qptr<Ui::RpWidget> widget;
	BarFixture *fixture = nullptr;
	ClickHandlerPtr handler;
	QString fixtureGate;
	crl::time duration = 0;
};

[[nodiscard]] QColor BarColor() {
	return QColor(0x20, 0x60, 0xd0);
}

[[nodiscard]] QColor TrackColor() {
	return QColor(0xff, 0xff, 0xff);
}

[[nodiscard]] QString ColorHex(QColor color) {
	return u"#%1%2%3"_q
		.arg(color.red(), 2, 16, QChar('0'))
		.arg(color.green(), 2, 16, QChar('0'))
		.arg(color.blue(), 2, 16, QChar('0'));
}

[[nodiscard]] QString GateText() {
	return u"animation clock self-test: fixture gate: a never-shown "
		"harness-owned bar widget in the primary window, with animations "
		"enabled"_q;
}

[[nodiscard]] double Linear(crl::time elapsed, crl::time duration) {
	return std::clamp(double(elapsed) / duration, 0., 1.);
}

[[nodiscard]] WindowReading ReadBarWindow(QWidget *bar) {
	auto result = WindowReading();
	result.screenLocked = Core::App().screenIsLocked();
	if (!bar) {
		return result;
	}
	const auto window = bar->window();
	const auto handle = window->windowHandle();
	result.bar = true;
	result.barHidden = bar->isHidden();
	result.barVisible = bar->isVisible();
	result.minimized = window->isMinimized();
	result.windowHidden = window->isHidden();
	result.exposed = handle && handle->isExposed();
	result.window = WidgetDescription(window);
	return result;
}

[[nodiscard]] QString WindowReadingText(const WindowReading &reading) {
	if (!reading.bar) {
		return u"bar=none screenLocked=%1"_q
			.arg(reading.screenLocked ? 1 : 0);
	}
	return u"bar{hidden=%1 visible=%2} window{minimized=%3 hidden=%4 "
		"exposed=%5 screenLocked=%6 %7}"_q
			.arg(reading.barHidden ? 1 : 0)
			.arg(reading.barVisible ? 1 : 0)
			.arg(reading.minimized ? 1 : 0)
			.arg(reading.windowHidden ? 1 : 0)
			.arg(reading.exposed ? 1 : 0)
			.arg(reading.screenLocked ? 1 : 0)
			.arg(reading.window);
}

[[nodiscard]] bool HiddenBar(const WindowReading &reading) {
	return reading.bar && reading.barHidden && !reading.barVisible;
}

[[nodiscard]] BarReading ReadBar(const QImage &image) {
	auto result = BarReading();
	if (image.isNull()) {
		result.refusal = u"unreadable-frame: the render returned a null "
			"image"_q;
		return result;
	}
	result.width = image.width();
	const auto y = image.height() / 2;
	for (auto x = 0; x != result.width; ++x) {
		const auto color = image.pixelColor(x, y);
		if (ChannelDelta(color, BarColor()) <= kBarTolerance) {
			++result.filled;
		} else if (ChannelDelta(color, TrackColor()) > kBarTolerance) {
			++result.other;
		}
	}
	result.value = double(result.filled) / result.width;
	if (result.other > kMaxOtherPixels) {
		result.refusal = u"unreadable-frame: %1 of %2 pixels of the middle "
			"row match neither the bar %3 nor the track %4 (at most %5)"_q
				.arg(result.other)
				.arg(result.width)
				.arg(ColorHex(BarColor()))
				.arg(ColorHex(TrackColor()))
				.arg(kMaxOtherPixels);
	}
	return result;
}

[[nodiscard]] QString BarReadingText(const BarReading &reading) {
	if (!reading.refusal.isEmpty()) {
		return u"painted=none (%1)"_q.arg(reading.refusal);
	}
	return u"painted=%1 (%2/%3, other %4)"_q
		.arg(reading.value, 0, 'f', 4)
		.arg(reading.filled)
		.arg(reading.width)
		.arg(reading.other);
}

[[nodiscard]] const ClockedFrame *FindFrame(
		const ClockedRun &run,
		int index) {
	const auto i = ranges::find(run.frames, index, &ClockedFrame::index);
	return (i != end(run.frames)) ? &*i : nullptr;
}

[[nodiscard]] const FrameShot *FindShot(
		const std::vector<FrameShot> &shots,
		int index) {
	const auto i = ranges::find(shots, index, &FrameShot::index);
	return (i != end(shots)) ? &*i : nullptr;
}

// The painted value of a taken frame with a readable bar, or nullptr.
[[nodiscard]] const BarReading *TakenBar(
		const ClockedFrame *frame,
		const FrameShot *shot) {
	return (frame && frame->taken() && shot && shot->bar.refusal.isEmpty())
		? &shot->bar
		: nullptr;
}

void PaintBar(
		not_null<BarFixture*> fixture,
		not_null<Ui::RpWidget*> widget,
		QPainter &p) {
	++fixture->paints;
	if (fixture->rendering) {
		++fixture->renderPaints;
	}
	const auto value = fixture->animation.value(fixture->clicked ? 1. : 0.);
	p.fillRect(widget->rect(), TrackColor());
	p.fillRect(
		QRectF(0., 0., value * widget->width(), widget->height()),
		BarColor());
}

void BuildBar(
		not_null<State*> state,
		not_null<Window::Controller*> controller) {
	const auto row = st::defaultActiveButton.height;
	state->widget = base::make_unique_q<Ui::RpWidget>(
		controller->widget().get());
	const auto raw = state->widget.get();
	const auto fixture = raw->lifetime().make_state<BarFixture>();
	state->fixture = fixture;
	raw->setAttribute(Qt::WA_OpaquePaintEvent);
	raw->setGeometry(0, 0, row * kWidthFactor, row);
	raw->paintOn([=](QPainter &p) {
		PaintBar(fixture, raw, p);
	});

	const auto weak = QPointer<QWidget>(raw);
	state->handler = std::make_shared<LambdaClickHandler>([=] {
		if (!weak) {
			return;
		}
		++fixture->handlerCalls;
		fixture->handlerAt = crl::now();
		fixture->clicked = true;
		fixture->animation.start([=] {
			if (weak) {
				weak->update();
			}
		}, 0., 1., kDuration, anim::linear);
	});
}

void PrepareFixture(not_null<State*> state) {
	state->duration = kDuration * anim::SlowMultiplier();
	const auto controller = Core::App().activePrimaryWindow();
	if (controller) {
		BuildBar(state, controller);
	}
	const auto widget = state->widget.get();
	const auto reading = ReadBarWindow(widget);
	const auto disabled = anim::Disabled();
	const auto ok = controller && HiddenBar(reading) && !disabled;
	Check(
		ok,
		GateText(),
		u"controller=%1 %2 dpr=%3 slowMultiplier=%4 duration=%5 "
		"animationsDisabled=%6 bar=%7 track=%8 size=%9"_q
			.arg(controller ? 1 : 0)
			.arg(WindowReadingText(reading))
			.arg(style::DevicePixelRatio())
			.arg(anim::SlowMultiplier())
			.arg(state->duration)
			.arg(disabled ? 1 : 0)
			.arg(ColorHex(BarColor()))
			.arg(ColorHex(TrackColor()))
			.arg(widget
				? u"%1x%2"_q.arg(widget->width()).arg(widget->height())
				: u"none"_q));
	if (!ok) {
		state->fixtureGate = GateText();
		state->widget = nullptr;
		state->fixture = nullptr;
	}
}

[[nodiscard]] QString ActionText(
		const ClockedRun &run,
		not_null<const BarFixture*> fixture) {
	return u"handlerCalls=%1 handlerAt=%2 action=[%3,%4]%5"_q
		.arg(fixture->handlerCalls)
		.arg(fixture->handlerAt)
		.arg(run.actionStarted)
		.arg(run.actionFinished)
		.arg(run.refusal.isEmpty()
			? QString()
			: (u" refused="_q + run.refusal));
}

void CheckHandler(
		const ClockedRun &run,
		not_null<const BarFixture*> fixture) {
	Check(
		(fixture->handlerCalls == 1)
			&& (run.actionStarted <= fixture->handlerAt)
			&& (fixture->handlerAt <= run.actionFinished),
		u"animation clock self-test: the click handler ran inside the "
		"synchronous action"_q,
		ActionText(run, fixture));
}

void CheckFrameValue(
		const ClockedRun &run,
		const std::vector<FrameShot> &shots,
		int index,
		crl::time duration,
		const WindowReading &window) {
	const auto frame = FindFrame(run, index);
	const auto shot = FindShot(shots, index);
	const auto bar = TakenBar(frame, shot);
	const auto what = u"animation clock self-test: frame at %1 % of the "
		"%2 ms linear animation: the painted value lies within the "
		"transition at its reached elapsed bounds"_q
			.arg((index + 1) * 25)
			.arg(duration);
	const auto frameText = frame
		? ClockedFrameText(*frame)
		: u"frame=none"_q;
	const auto shotText = shot
		? u"%1 simple=%2"_q
			.arg(BarReadingText(shot->bar))
			.arg(shot->simple, 0, 'f', 4)
		: u"painted=none (no image)"_q;
	if (!bar) {
		Check(
			false,
			what,
			u"%1 %2 %3"_q
				.arg(frameText)
				.arg(shotText)
				.arg(WindowReadingText(window)));
		return;
	}
	const auto tolerance = 1.5 / bar->width;
	const auto lo = Linear(frame->reachedLo, duration);
	const auto hi = Linear(frame->reachedHi, duration);
	Check(
		(lo - tolerance <= bar->value) && (bar->value <= hi + tolerance),
		what,
		u"%1 %2 bounds=[%3,%4] tol=%5 %6"_q
			.arg(frameText)
			.arg(shotText)
			.arg(lo, 0, 'f', 4)
			.arg(hi, 0, 'f', 4)
			.arg(tolerance, 0, 'f', 4)
			.arg(WindowReadingText(window)));
}

void CheckIncreasing(
		const ClockedRun &run,
		const std::vector<FrameShot> &shots) {
	auto parts = QStringList();
	auto values = std::vector<double>();
	for (auto index = 0; index != kInProgressFrames; ++index) {
		const auto bar = TakenBar(
			FindFrame(run, index),
			FindShot(shots, index));
		if (bar) {
			values.push_back(bar->value);
			parts.push_back(u"%1 (%2/%3)"_q
				.arg(bar->value, 0, 'f', 4)
				.arg(bar->filled)
				.arg(bar->width));
		} else {
			parts.push_back(u"none"_q);
		}
	}
	const auto increasing = (int(values.size()) == kInProgressFrames)
		&& (ranges::adjacent_find(values, std::greater_equal<>())
			== end(values));
	Check(
		increasing,
		u"animation clock self-test: the painted values strictly "
		"increase"_q,
		u"values=%1"_q.arg(parts.join(u", "_q)));
}

void CheckPastEnd(
		const ClockedRun &run,
		const std::vector<FrameShot> &shots,
		int requested) {
	const auto index = kInProgressFrames;
	const auto frame = FindFrame(run, index);
	const auto shot = FindShot(shots, index);
	Check(
		frame
			&& !frame->gate.isEmpty()
			&& !shot
			&& (int(run.frames.size()) == requested),
		u"animation clock self-test: the requested time past the "
		"animation end is an N/A gate, not a frame"_q,
		u"frames=%1 (requested %2) %3 image=%4"_q
			.arg(int(run.frames.size()))
			.arg(requested)
			.arg(frame ? ClockedFrameText(*frame) : u"frame=none"_q)
			.arg(shot ? u"present"_q : u"none"_q));
}

void CheckNoCompositorPaint(
		const ClockedRun &run,
		const WindowReading &before,
		const WindowReading &after,
		int outsidePaints,
		int renderPaints) {
	const auto taken = run.taken();
	Check(
		HiddenBar(before)
			&& HiddenBar(after)
			&& (outsidePaints == 0)
			&& (renderPaints == taken)
			&& (taken == kInProgressFrames),
		u"animation clock self-test: the frames advanced on a widget no "
		"compositor paint reached"_q,
		u"before{%1} after{%2} paintsOutsideRenders=%3 renderPaints=%4 "
		"framesTaken=%5 (need %6)"_q
			.arg(WindowReadingText(before))
			.arg(WindowReadingText(after))
			.arg(outsidePaints)
			.arg(renderPaints)
			.arg(taken)
			.arg(kInProgressFrames));
}

void SaveFrames(const std::vector<FrameShot> &shots) {
	auto zoomed = std::vector<QImage>();
	for (const auto &shot : shots) {
		zoomed.push_back(Zoom(shot.image, kSheetZoom));
	}
	const auto sheet = ContactSheet(zoomed);
	if (!sheet.isNull()) {
		SaveImage(sheet, u"animation_clock_frames"_q);
	}
}

void CheckClockedFrames(not_null<State*> state) {
	const auto widget = state->widget.get();
	const auto fixture = state->fixture;
	const auto duration = state->duration;
	const auto before = ReadBarWindow(widget);
	const auto paintsBefore = fixture->paints;
	const auto renderPaintsBefore = fixture->renderPaints;
	auto shots = std::vector<FrameShot>();
	const auto elapsed = std::vector<crl::time>{
		duration / 4,
		duration / 2,
		3 * duration / 4,
		duration + duration / 4,
	};
	const auto run = RunClockedFrames({
		.name = u"animation clock self-test"_q,
		.handler = state->handler,
		.context = ClickContext{ Qt::LeftButton },
		.elapsed = elapsed,
		.animationEnd = duration,
		.render = [&](const ClockedFrame &frame) {
			fixture->rendering = true;
			auto image = GrabWidget(widget);
			fixture->rendering = false;
			shots.push_back({
				.index = frame.index,
				.image = std::move(image),
				.simple = fixture->animation.value(-1.),
			});
		},
	});
	const auto after = ReadBarWindow(widget);
	const auto renderPaints = fixture->renderPaints - renderPaintsBefore;
	const auto outsidePaints = fixture->paints
		- paintsBefore
		- renderPaints;
	for (auto &shot : shots) {
		shot.bar = ReadBar(shot.image);
	}

	CheckHandler(run, fixture);
	for (auto index = 0; index != kInProgressFrames; ++index) {
		CheckFrameValue(run, shots, index, duration, after);
	}
	CheckIncreasing(run, shots);
	CheckPastEnd(run, shots, int(elapsed.size()));
	CheckNoCompositorPaint(run, before, after, outsidePaints, renderPaints);

	SaveFrames(shots);
	state->widget = nullptr;
	state->fixture = nullptr;
}

} // namespace

void AppendAnimationClockSelfTest(not_null<Runner*> runner) {
	const auto state = std::make_shared<State>();

	runner->add({
		.name = u"animation clock self-test: fixture"_q,
		.run = [=] {
			PrepareFixture(state.get());
		},
	});
	runner->add({
		.name = u"animation clock self-test: clocked frames from a click "
			"handler"_q,
		.skipReason = [=] {
			if (!state->fixtureGate.isEmpty()) {
				return state->fixtureGate;
			} else if (!state->widget) {
				return u"fixture gate: the bar widget was destroyed with "
					"its window before the clocked stage"_q;
			}
			return QString();
		},
		.run = [=] {
			CheckClockedFrames(state.get());
		},
	});
}

} // namespace Test

#endif // _DEBUG
