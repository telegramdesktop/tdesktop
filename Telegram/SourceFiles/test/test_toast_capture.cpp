/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_toast_capture.h"

#include "base/unique_qptr.h"
#include "base/weak_ptr.h"
#include "core/application.h"
#include "test/test_capture.h"
#include "test/test_ink.h"
#include "test/test_log.h"
#include "test/test_text_reads.h"
#include "test/test_widgets.h"
#include "ui/toast/toast.h"
#include "ui/toast/toast_widget.h"
#include "ui/widgets/labels.h"
#include "ui/rp_widget.h"
#include "window/window_controller.h"

#include <QtCore/QStringList>
#include <QtGui/QPainter>
#include <QtWidgets/QApplication>

#include "styles/palette.h"
#include "styles/style_widgets.h"

namespace Test {
namespace {

constexpr auto kToastSettledSamples = 32;
constexpr auto kToastSettledMinPermille = 300;
constexpr auto kToastSettledToleranceDivisor = 16;
constexpr auto kToastSettledToleranceMin = 4;
constexpr auto kSentinelTolerance = 40;
constexpr auto kSentinelMinSeparation = 2 * kSentinelTolerance;

const auto kToastText = u"Harness toast subtree"_q;
const auto kOtherText = u"Harness toast elsewhere"_q;
const auto kFirstSentinel = QColor(255, 255, 65);
const auto kSecondSentinel = QColor(192, 0, 192);

[[nodiscard]] QColor SettledToastBackground() {
	const auto over = st::toastBg->c;
	const auto base = st::windowBg->c;
	const auto alpha = float64(over.alphaF());
	const auto blend = [&](int source, int destination) {
		return int(std::round(
			(source * alpha) + (destination * (1. - alpha))));
	};
	return QColor(
		blend(over.red(), base.red()),
		blend(over.green(), base.green()),
		blend(over.blue(), base.blue()));
}

[[nodiscard]] int SettledSeparation() {
	return ChannelDelta(SettledToastBackground(), st::windowBg->c);
}

[[nodiscard]] int SettledTolerance() {
	return std::max(
		kToastSettledToleranceMin,
		SettledSeparation() / kToastSettledToleranceDivisor);
}

struct SentinelSeparation {
	QColor blend;
	int windowBg = 0;
	int settled = 0;
	int toastFg = 0;
	int minimum = 0;
};

// Why the sentinel counter cannot be flipped by a palette, and why the
// number is measured here instead of asserted in a comment. Every colour a
// toast-rooted frame can hold is a grey or within a few channel units of
// one: Ui::GrabWidgetToImage fills the frame with st::windowBg, the toast
// paints st::toastBg's settled blend over that, its label paints
// st::toastFg ink, and every antialiased mixture lies between them.
// ChannelDelta is the maximum absolute per-channel difference
// (test_ink.cpp:96-104), so the nearest grey to a colour sits exactly half
// that colour's own channel span away: kFirstSentinel spans 190 and
// kSecondSentinel spans 192, which puts every grey at least 95 from either
// and every near-grey at least about 92, in a light palette and a dark one
// alike. An earlier pair was picked against one palette and collided with
// the other theme's harness base, which is what this gate exists to say out
// loud rather than fold into a verdict on a correct frame.
[[nodiscard]] SentinelSeparation MeasureSentinelSeparation() {
	const auto distance = [](QColor color) {
		return std::min(
			ChannelDelta(color, kFirstSentinel),
			ChannelDelta(color, kSecondSentinel));
	};
	auto result = SentinelSeparation{
		.blend = SettledToastBackground(),
	};
	result.windowBg = distance(st::windowBg->c);
	result.settled = distance(result.blend);
	result.toastFg = distance(st::toastFg->c);
	result.minimum = std::min({
		result.windowBg,
		result.settled,
		result.toastFg });
	return result;
}

[[nodiscard]] QString SentinelSeparationText(
		const SentinelSeparation &separation) {
	const auto probe = [](const QString &label, QColor color, int delta) {
		return u"%1 %2 at %3"_q.arg(label, color.name()).arg(delta);
	};
	return u"tones %1 and %2 - minimum separation %3, required at least %4 "
		u"(counter tolerance %5) - %6, %7, %8"_q
		.arg(kFirstSentinel.name(), kSecondSentinel.name())
		.arg(separation.minimum)
		.arg(kSentinelMinSeparation)
		.arg(kSentinelTolerance)
		.arg(
			probe(u"windowBg"_q, st::windowBg->c, separation.windowBg),
			probe(
				u"settled toast blend"_q,
				separation.blend,
				separation.settled),
			probe(u"toastFg"_q, st::toastFg->c, separation.toastFg));
}

struct ToastFrame {
	QImage image;
	int settledPermille = 0;
	int separation = 0;
	bool settled = false;
};

// The one statistic that separates a settled toast from a fading one, and
// the reason it is a floor on a match count rather than a mean or a
// minimum: at full show paintEvent calls paintBackground(p) directly
// (toast_widget.cpp:585-600), so the background pixels are exactly
// st::toastBg composited over the grab's st::windowBg fill, while at shown
// level s every one of them is that same colour lerped back towards the
// fill and none of them matches. The toast's transparent corners and its
// opaque label ink match at no level at all and only ever lower the count,
// so a mean or a minimum would measure them instead of the fade.
[[nodiscard]] ToastFrame ReadToastFrame(const ToastSubtreeReading &reading) {
	auto result = ToastFrame();
	result.separation = SettledSeparation();
	if (!reading.resolved()) {
		return result;
	}
	result.image = GrabRect(reading.toast.data(), reading.frame);
	const auto size = result.image.size();
	const auto columns = std::min(size.width(), kToastSettledSamples);
	const auto rows = std::min(size.height(), kToastSettledSamples);
	if (columns < 1 || rows < 1) {
		return result;
	}
	const auto settled = SettledToastBackground();
	const auto tolerance = SettledTolerance();
	auto matched = 0;
	for (auto y = 0; y != rows; ++y) {
		for (auto x = 0; x != columns; ++x) {
			const auto point = QPoint(
				((2 * x + 1) * size.width()) / (2 * columns),
				((2 * y + 1) * size.height()) / (2 * rows));
			const auto color = result.image.pixelColor(point);
			if (ChannelDelta(color, settled) <= tolerance) {
				++matched;
			}
		}
	}
	result.settledPermille = (1000 * matched) / (columns * rows);
	result.settled = (result.settledPermille >= kToastSettledMinPermille);
	return result;
}

[[nodiscard]] QString ToastSubtreeText(
		const ToastSubtreeReading &reading,
		const ToastFrame &frame) {
	return u"toast-subtree capture: target=%1 frame=%2 settled=%3/1000 "
		u"(threshold %4/1000, tolerance %5, separation %6) - the frame is "
		u"rooted at the toast, so Ui::GrabWidgetToImage fills every pixel "
		u"the toast leaves uncovered with st::windowBg and nothing the "
		u"product painted beneath it can enter%7"_q
		.arg(reading.identity.isEmpty() ? u"<none>"_q : reading.identity)
		.arg(RectText(reading.frame))
		.arg(frame.settledPermille)
		.arg(kToastSettledMinPermille)
		.arg(SettledTolerance())
		.arg(frame.separation)
		.arg(reading.refusal.isEmpty()
			? QString()
			: u" - %1"_q.arg(reading.refusal));
}

struct FrameDiff {
	int differing = 0;
	int maxDelta = 0;
	bool comparable = false;
};

[[nodiscard]] FrameDiff CompareFrames(
		const QImage &first,
		const QImage &second) {
	auto result = FrameDiff();
	if (first.isNull() || second.isNull() || first.size() != second.size()) {
		return result;
	}
	result.comparable = true;
	for (auto y = 0; y != first.height(); ++y) {
		for (auto x = 0; x != first.width(); ++x) {
			const auto delta = ChannelDelta(
				first.pixelColor(x, y),
				second.pixelColor(x, y));
			if (delta > 0) {
				++result.differing;
				result.maxDelta = std::max(result.maxDelta, delta);
			}
		}
	}
	return result;
}

[[nodiscard]] int SentinelPixels(const QImage &image) {
	auto result = 0;
	for (auto y = 0; y != image.height(); ++y) {
		for (auto x = 0; x != image.width(); ++x) {
			const auto color = image.pixelColor(x, y);
			if (ChannelDelta(color, kFirstSentinel) <= kSentinelTolerance
				|| ChannelDelta(color, kSecondSentinel)
					<= kSentinelTolerance) {
				++result;
			}
		}
	}
	return result;
}

[[nodiscard]] QString ImageText(const QString &path, const QImage &image) {
	return u"%1 (%2x%3 dpr=%4)"_q
		.arg(path)
		.arg(image.width())
		.arg(image.height())
		.arg(image.devicePixelRatio());
}

struct Fixture {
	base::unique_qptr<Ui::RpWidget> sentinel;
	base::weak_ptr<Ui::Toast::Instance> toast;
	QPointer<QWidget> toastWidget;
};

[[nodiscard]] QString FixtureDetails(const Fixture &fixture) {
	const auto toast = fixture.toastWidget.data();
	if (!toast) {
		return u"the fixture toast no longer exists"_q;
	}
	return u"toast visible=%1 sentinel=%2 - %3"_q
		.arg(toast->isVisible() ? 1 : 0)
		.arg((fixture.sentinel && !fixture.sentinel->isHidden()) ? 1 : 0)
		.arg(ToastSubtreeDetails(toast));
}

// Two tones rather than one, for the reason test_via_window.cpp:65-68
// gives: with a single tone the window-mapped control's non-blankness
// would rest on whatever the toast itself managed to paint, which is the
// thing under measurement. Their lightness is 160 against 96, a spread of
// 64 against the harness's kBlankSpreadThreshold of 6, so a window-mapped
// crop of this region clears the blank threshold on the bands alone.
void PaintSentinelBands(not_null<Ui::RpWidget*> widget) {
	const auto raw = widget.get();
	raw->paintOn([=](QPainter &p) {
		const auto half = raw->height() / 2;
		p.fillRect(QRect(0, 0, raw->width(), half), kFirstSentinel);
		p.fillRect(
			QRect(0, half, raw->width(), raw->height() - half),
			kSecondSentinel);
	});
}

[[nodiscard]] bool BuildFixture(Fixture &fixture) {
	const auto window = Core::App().activePrimaryWindow();
	if (!window) {
		return false;
	}
	const auto top = window->widget().get();

	// Created before the toast on purpose, and never raised: siblings
	// paint in child order, so the sentinel surface stays underneath it. A
	// raise() would put the fixture over the toast and the window-mapped
	// control would then measure the fixture's own paint instead of what
	// the window holds where the toast is.
	fixture.sentinel = base::make_unique_q<Ui::RpWidget>(top);
	const auto sentinel = fixture.sentinel.get();
	PaintSentinelBands(sentinel);

	fixture.toast = Ui::Toast::Show(top, {
		.text = { kToastText },
		.st = &st::defaultToast,
		.infinite = true,
	});
	const auto instance = fixture.toast.get();
	if (!instance) {
		return false;
	}
	// The toast Widget is parented to the same |top| (ui/toast/toast.cpp:
	// 36-39), so it and the sentinel are siblings sharing one origin and
	// geometry() maps between them with no conversion at all.
	const auto widget = instance->widget().get();
	fixture.toastWidget = widget;
	sentinel->setGeometry(widget->geometry());
	sentinel->show();
	return true;
}

} // namespace

std::vector<QWidget*> FindLiveToasts() {
	auto result = std::vector<QWidget*>();
	const auto append = [&](QWidget *widget) {
		for (const auto existing : result) {
			if (existing == widget) {
				return;
			}
		}
		result.push_back(widget);
	};
	for (const auto top : QApplication::topLevelWidgets()) {
		if (!top->isVisible()) {
			continue;
		}
		if (dynamic_cast<Ui::Toast::internal::Widget*>(top)) {
			append(top);
		}
		for (const auto toast
			: FindVisible<Ui::Toast::internal::Widget>(top)) {
			append(toast);
		}
	}
	return result;
}

QWidget *FindLiveToast() {
	const auto all = FindLiveToasts();
	return (all.size() == 1) ? all.front() : nullptr;
}

ToastSubtreeReading ReadToastSubtree(QWidget *widget, QRect requested) {
	if (!widget) {
		return {
			.refusal = u"no widget was handed to the toast-subtree "
				u"capture"_q,
		};
	}
	auto result = ToastSubtreeReading{
		.identity = WidgetDescription(widget),
	};
	if (!dynamic_cast<Ui::Toast::internal::Widget*>(widget)) {
		const auto live = FindLiveToast();
		result.refusal = u"target is not a live "
			u"Ui::Toast::internal::Widget, so a frame rooted at it would "
			u"compose whatever it paints over: target=%1 liveToast=%2 - a "
			u"child inside a toast is refused the same way, because the "
			u"frame is rooted at the toast or nowhere"_q
			.arg(
				result.identity,
				live ? WidgetDescription(live) : u"<none>"_q);
		return result;
	} else if (!widget->isVisible()) {
		result.refusal = u"target is not visible: %1"_q.arg(result.identity);
		return result;
	} else if (widget->size().isEmpty()) {
		result.refusal = u"target has empty geometry: %1"_q.arg(
			result.identity);
		return result;
	}
	const auto bounds = widget->rect();
	if (!requested.isEmpty()) {
		const auto misframed = MisframedDetails(widget, requested);
		if (!misframed.isEmpty()) {
			result.refusal = misframed;
			return result;
		}
	}
	result.frame = requested.isEmpty() ? bounds : requested;
	result.toast = widget;
	return result;
}

QImage GrabToastSubtree(QWidget *widget, QRect requested) {
	const auto reading = ReadToastSubtree(widget, requested);
	return reading.resolved()
		? GrabRect(reading.toast.data(), reading.frame)
		: QImage();
}

bool ToastSubtreeReady(QWidget *widget) {
	const auto reading = ReadToastSubtree(widget);
	return reading.resolved() && ReadToastFrame(reading).settled;
}

QString ToastSubtreeDetails(QWidget *widget) {
	const auto reading = ReadToastSubtree(widget);
	return ToastSubtreeText(reading, ReadToastFrame(reading));
}

bool CaptureToastSubtree(
		not_null<QWidget*> widget,
		const QString &name,
		QRect requested) {
	const auto reading = ReadToastSubtree(widget, requested);
	if (!reading.resolved()) {
		Fail(u"capture %1"_q.arg(name), reading.refusal);
		return false;
	}
	LogGeometry(name, QRect(widget->mapToGlobal(QPoint()), widget->size()));
	const auto frame = ReadToastFrame(reading);
	if (!frame.settled) {
		Note(u"capture %1 declined an unsettled frame: %2"_q
			.arg(name, ToastSubtreeText(reading, frame)));
		return false;
	} else if (LooksBlank(frame.image)) {
		Note(u"capture %1 declined a blank frame: %2"_q
			.arg(name, ToastSubtreeText(reading, frame)));
		return false;
	}
	return !SaveImage(frame.image, name).isEmpty();
}

QString ReadToastText(QWidget *widget) {
	const auto toast = dynamic_cast<Ui::Toast::internal::Widget*>(widget);
	if (!toast) {
		return QString();
	}
	auto names = QStringList();
	for (const auto label : FindAll<Ui::FlatLabel>(toast)) {
		names.push_back(label->accessibilityName());
	}
	return names.join(u" | "_q);
}

void CheckToastReads(
		QWidget *widget,
		const QString &expected,
		const QString &what) {
	CheckTextReads(ReadToastText(widget), expected, what);
}

void AppendToastSubtreeCaptureSelfTest(not_null<Runner*> runner) {
	struct State {
		Fixture fixture;
		QString midfadeDetails;
		int liveCount = 0;
		int failuresBefore = 0;
		int failuresAfter = 0;
		int singleAgainCount = 0;
		bool built = false;
		bool liveHoldsToast = false;
		bool liveResolvedSingle = false;
		bool midfadeReady = false;
		bool midfadeSaved = false;
		bool singleAgainResolvedFixture = false;
	};
	// Leaked on purpose, the way the harness's other self-tests leak
	// theirs: the stages outlive this call. The teardown stage releases
	// the fixture, after which the State holds nothing but QStrings and
	// PODs - no rpl subscription to anything the session owns.
	const auto state = new State();
	const auto details = [=] {
		return FixtureDetails(state->fixture);
	};

	runner->add({
		.name = u"toast-subtree self-test: the fade-in frame is a Note and "
			"never a FAIL, and the settled-show term is what says so"_q,
		.run = [=] {
			state->built = BuildFixture(state->fixture);
			Check(
				state->built,
				u"fixture gate: the self-test fixture was built"_q,
				state->built
					? QString()
					: u"Core::App().activePrimaryWindow() is null, or "
						u"Ui::Toast::Show answered no instance"_q);
			const auto separation = MeasureSentinelSeparation();
			Check(
				separation.minimum >= kSentinelMinSeparation,
				u"fixture gate: on this palette both sentinel tones "
				"stay clear of every colour a toast-rooted frame can "
				"hold - the st::windowBg fill, st::toastBg's settled "
				"blend over it and st::toastFg ink are all greys or "
				"beside the grey axis, and the tones are not - so the "
				"sentinel counter measures the frame and never the "
				"theme"_q,
				SentinelSeparationText(separation));
			const auto toast = state->fixture.toastWidget.data();
			if (!state->built || !toast) {
				return;
			}
			// Every reading below is taken here, in the one turn the
			// fade-in window exists: the toast's shown level is still 0,
			// so its paintEvent is on the proxy branch and a toast-rooted
			// grab holds the harness base and nothing else. A tick later
			// the fade has moved and the contrast is gone, which is why
			// |then| asserts from these snapshots instead of re-measuring.
			const auto live = FindLiveToasts();
			state->liveCount = int(live.size());
			for (const auto widget : live) {
				if (widget == toast) {
					state->liveHoldsToast = true;
				}
			}
			state->liveResolvedSingle = (FindLiveToast() == toast);
			state->midfadeDetails = ToastSubtreeDetails(toast);
			state->midfadeReady = ToastSubtreeReady(toast);
			state->failuresBefore = FailureCount();
			state->midfadeSaved = CaptureToastSubtree(
				toast,
				u"toast_subtree_midfade"_q);
			state->failuresAfter = FailureCount();
		},
		.then = [=] {
			if (!state->built || !state->fixture.toastWidget) {
				return;
			}
			Check(
				state->liveHoldsToast && state->liveResolvedSingle,
				u"the live-toast walk resolves the fixture toast across "
				"the top level widgets, which is the only honest "
				"resolution: Ui::Toast::internal::Manager keeps its "
				"_toastByWidget map private and enumerates nothing"_q,
				u"liveToasts=%1 holdsFixture=%2 resolvedSingle=%3"_q
					.arg(state->liveCount)
					.arg(state->liveHoldsToast ? 1 : 0)
					.arg(state->liveResolvedSingle ? 1 : 0));
			Check(
				!state->midfadeReady,
				u"the settled-show term answers false in the turn the "
				"toast is created, so blankness stays diagnosable: the "
				"shown level is private, and the term measures the grab "
				"against st::toastBg's own settled blend over the harness "
				"base instead of reading it"_q,
				state->midfadeDetails);
			Check(
				!state->midfadeSaved
					&& (state->failuresAfter == state->failuresBefore),
				u"the mid-fade capture is declined with a Note and never a "
				"FAIL, which is the contract a toast capture rests on"_q,
				u"saved=%1 failures before=%2 after=%3"_q
					.arg(state->midfadeSaved ? 1 : 0)
					.arg(state->failuresBefore)
					.arg(state->failuresAfter));
		},
	});

	runner->add({
		.name = u"toast-subtree self-test: the frame carries the toast and "
			"not the surface beneath it"_q,
		.until = [=] {
			const auto toast = state->fixture.toastWidget.data();
			return !state->built || !toast || ToastSubtreeReady(toast);
		},
		.then = [=] {
			const auto toast = state->fixture.toastWidget.data();
			const auto sentinel = state->fixture.sentinel.get();
			if (!state->built || !toast || !sentinel) {
				return;
			}
			// The drain is load-bearing and not hygiene: a mid-fade
			// paintEvent called disableChildrenPaintOnce(), which sets
			// Qt::WA_UpdatesDisabled on the toast's children and schedules
			// the restore as a Ui::PostponeCall (toast_widget.cpp:
			// 556-583), and every frame below must be taken after that
			// restore has run.
			sentinel->setGeometry(toast->geometry());
			SettlePostponedCalls();

			const auto subtreeName = u"toast_subtree_settled"_q;
			const auto viaName = u"toast_window_mapped_control"_q;
			const auto subtreeSaved = CaptureToastSubtree(
				toast,
				subtreeName);
			const auto viaSaved = CaptureViaWindow(toast, viaName);
			const auto subtreePath = ScreenshotsDir()
				+ subtreeName
				+ u".png"_q;
			const auto viaPath = ScreenshotsDir() + viaName + u".png"_q;
			const auto subtreeImage = QImage(subtreePath);
			const auto viaImage = QImage(viaPath);
			const auto subtreeSentinel = SentinelPixels(subtreeImage);
			const auto viaSentinel = SentinelPixels(viaImage);
			const auto separation = MeasureSentinelSeparation();
			Check(
				subtreeSaved
					&& viaSaved
					&& !subtreeImage.isNull()
					&& !viaImage.isNull()
					&& !subtreeSentinel
					&& (viaSentinel > 0),
				u"the saved toast-subtree frame holds no pixel within %1 "
				"channel units of either sentinel tone, while the "
				"window-mapped frame of the same toast in the same turn "
				"holds such pixels in the toast's transparent rounded "
				"corners, where the surface beneath shows through "
				"unblended"_q.arg(kSentinelTolerance),
				u"subtree=%1 sentinelPixels=%2 saved=%3; windowMapped=%4 "
				u"sentinelPixels=%5 saved=%6; minSentinelSeparation=%7 "
				u"against tolerance %8, so the count is not vacuous"_q
					.arg(ImageText(subtreePath, subtreeImage))
					.arg(subtreeSentinel)
					.arg(subtreeSaved ? 1 : 0)
					.arg(ImageText(viaPath, viaImage))
					.arg(viaSentinel)
					.arg(viaSaved ? 1 : 0)
					.arg(separation.minimum)
					.arg(kSentinelTolerance));

			const auto subtreeShown = GrabToastSubtree(toast);
			const auto viaShown = GrabViaWindow(toast);
			sentinel->hide();
			SettlePostponedCalls();
			const auto subtreeHidden = GrabToastSubtree(toast);
			const auto viaHidden = GrabViaWindow(toast);
			sentinel->show();
			SettlePostponedCalls();
			const auto subtreeDiff = CompareFrames(
				subtreeShown,
				subtreeHidden);
			const auto viaDiff = CompareFrames(viaShown, viaHidden);
			Check(
				subtreeDiff.comparable
					&& viaDiff.comparable
					&& !subtreeDiff.differing
					&& (viaDiff.differing > 0),
				u"hiding the surface beneath the toast moves no pixel of "
				"the toast-subtree frame and many pixels of the "
				"window-mapped frame - the corner-independent form of the "
				"same claim, and the stronger one: no pixel of the subtree "
				"frame depends on what is painted under the toast"_q,
				u"subtree differing=%1 maxDelta=%2 comparable=%3; "
				u"windowMapped differing=%4 maxDelta=%5 comparable=%6"_q
					.arg(subtreeDiff.differing)
					.arg(subtreeDiff.maxDelta)
					.arg(subtreeDiff.comparable ? 1 : 0)
					.arg(viaDiff.differing)
					.arg(viaDiff.maxDelta)
					.arg(viaDiff.comparable ? 1 : 0));

			CheckToastReads(
				toast,
				kToastText,
				u"the toast's own phrase reads back through the joined "
				"accessibilityName of its Ui::FlatLabels, compared by the "
				"shipped Test::CheckTextReads"_q);
			Note(u"toast-subtree self-test: the loud form of the negative "
				u"below is Test::CheckToastReads, which would FAIL on that "
				u"phrase; it is asserted through the shipped comparison's "
				u"own predicate instead, because test_text_reads.h is the "
				u"harness's one self-test that emits deliberate failures"_q);
			const auto read = ReadToastText(toast);
			Check(
				NormalizeSpaces(read) != NormalizeSpaces(kOtherText),
				u"the text term discriminates: the very read that matches "
				"the toast's own phrase does not match a different one"_q,
				u"read=\"%1\" other=\"%2\""_q.arg(read, kOtherText));
		},
		.timeoutDetails = details,
	});

	runner->add({
		.name = u"toast-subtree self-test: a second live toast makes the "
			"resolver refuse, and does not outlive the stage that showed "
			"it"_q,
		.run = [=] {
			const auto toast = state->fixture.toastWidget.data();
			const auto parent = toast ? toast->parentWidget() : nullptr;
			if (!state->built || !toast || !parent) {
				return;
			}
			// Shown, read and taken down inside this one turn, the way
			// test_via_window.cpp:398-518 builds, reads and destroys its
			// own fixture, so nothing this stage creates reaches the
			// stages after it. It is infinite for the reason the fixture
			// is: an infinite toast leaves _hideAt at 0 (toast.cpp:32-34)
			// and the manager then arms no hide timer for it
			// (toast_manager.cpp:75-83), so nothing takes it down
			// implicitly and the drained loop's starving of hideAnimated()
			// never comes into it. Instance::hide() - _widget->hide();
			// _widget->deleteLater(); (toast.cpp:116-119) - is the seam,
			// and its hide is synchronous while the walk reads visibility,
			// so the count drops in that same statement.
			const auto second = Ui::Toast::Show(parent, {
				.text = { kOtherText },
				.st = &st::defaultToast,
				.infinite = true,
			});
			const auto instance = second.get();
			const auto secondWidget = instance
				? instance->widget().get()
				: nullptr;
			const auto both = FindLiveToasts();
			const auto resolved = FindLiveToast();
			const auto holdsFixture = ranges::contains(both, toast);
			const auto holdsSecond = secondWidget
				&& ranges::contains(both, secondWidget);
			Check(
				(both.size() == 2)
					&& holdsFixture
					&& holdsSecond
					&& (resolved == nullptr),
				u"more than one live toast is an ambiguity the resolver "
				"refuses rather than guesses: with a second toast on "
				"screen the walk answers both of them and FindLiveToast() "
				"answers nullptr, so a resolver handing back the first of "
				"several turns this row red"_q,
				u"liveToasts=%1 holdsFixture=%2 holdsSecond=%3 resolved=%4 "
				u"fixture=%5 second=%6"_q
					.arg(int(both.size()))
					.arg(holdsFixture ? 1 : 0)
					.arg(holdsSecond ? 1 : 0)
					.arg(resolved
						? WidgetDescription(resolved)
						: u"<none>"_q)
					.arg(
						WidgetDescription(toast),
						secondWidget
							? WidgetDescription(secondWidget)
							: u"<none>"_q));
			if (instance) {
				instance->hide();
			}
			state->singleAgainCount = int(FindLiveToasts().size());
			state->singleAgainResolvedFixture = (FindLiveToast() == toast);
		},
		.then = [=] {
			const auto toast = state->fixture.toastWidget.data();
			if (!state->built || !toast) {
				return;
			}
			const auto live = FindLiveToasts();
			const auto resolved = FindLiveToast();
			Check(
				(state->singleAgainCount == 1)
					&& state->singleAgainResolvedFixture
					&& (live.size() == 1)
					&& (resolved == toast),
				u"the second toast does not outlive the stage that showed "
				"it: Instance::hide() took it out of the walk in the same "
				"statement, and the walk answers the fixture toast alone "
				"again a runner tick later, which is the single-toast "
				"fixture every stage after this one reads"_q,
				u"afterHide liveToasts=%1 resolvedFixture=%2; nextTurn "
				u"liveToasts=%3 resolvedFixture=%4 fixture=%5"_q
					.arg(state->singleAgainCount)
					.arg(state->singleAgainResolvedFixture ? 1 : 0)
					.arg(int(live.size()))
					.arg((resolved == toast) ? 1 : 0)
					.arg(WidgetDescription(toast)));
		},
	});

	runner->add({
		.name = u"toast-subtree self-test: a misframed request is refused "
			"by name, quoting both rects"_q,
		.run = [=] {
			const auto toast = state->fixture.toastWidget.data();
			if (!state->built || !toast) {
				return;
			}
			const auto window = Core::App().activePrimaryWindow();
			const auto top = window ? window->widget().get() : nullptr;
			const auto &margin = st::defaultToast.margin;
			const auto wide = toast->rect().marginsAdded(QMargins(
				margin.left(),
				margin.top(),
				margin.right(),
				margin.bottom()));
			const auto before = FailureCount();
			const auto missing = ReadToastSubtree(nullptr);
			const auto missingReady = ToastSubtreeReady(nullptr);
			const auto missingGrab = GrabToastSubtree(nullptr);
			const auto owner = top
				? ReadToastSubtree(top)
				: ToastSubtreeReading();
			const auto ownerReady = top && ToastSubtreeReady(top);
			const auto ownerGrab = top ? GrabToastSubtree(top) : QImage();
			const auto misframed = ReadToastSubtree(toast, wide);
			const auto misframedGrab = GrabToastSubtree(toast, wide);
			const auto after = FailureCount();
			Note(u"toast-subtree self-test: every refusal below is observed "
				u"through ReadToastSubtree, ToastSubtreeReady and "
				u"GrabToastSubtree, which log nothing; "
				u"Test::CaptureToastSubtree turns each of them into a loud "
				u"Fail, which is why this stage never goes through it"_q);
			Check(
				!missing.resolved()
					&& (missing.toast == nullptr)
					&& missing.refusal.contains(u"no widget was handed"_q)
					&& !missingReady
					&& missingGrab.isNull(),
				u"a null target is refused by name, is never ready and is "
				"never grabbed, so a poll around it ends in a named stage "
				"timeout rather than in a grab of something else"_q,
				missing.refusal);
			Check(
				top
					&& !owner.resolved()
					&& owner.refusal.contains(WidgetDescription(top))
					&& owner.refusal.contains(WidgetDescription(toast))
					&& !ownerReady
					&& ownerGrab.isNull(),
				u"an ancestor that paints the product is refused by name, "
				"quoting both the handed widget and the live toast the "
				"caller should grab instead, and answers neither a ready "
				"nor an image"_q,
				owner.refusal);
			Check(
				!misframed.resolved()
					&& misframed.refusal.contains(RectText(wide))
					&& misframed.refusal.contains(RectText(toast->rect()))
					&& misframedGrab.isNull(),
				u"a rect materially larger than the live toast's own "
				"geometry is refused quoting both rects and answers a null "
				"image, never a silently reframed one"_q,
				misframed.refusal);
			Note(u"toast-subtree self-test: ToastSubtreeReady speaks about "
				u"the toast and not about a requested rect, so the "
				u"misframed leg is measured by GrabToastSubtree answering a "
				u"null image; the loud form is still "
				u"Test::CaptureToastSubtree, which FAILs on that rect"_q);
			Check(
				after == before,
				u"none of these refusals logged a failure: a refusal is a "
				"returned value the caller decides about"_q,
				u"failures before=%1 after=%2"_q.arg(before).arg(after));
		},
	});

	runner->add({
		.name = u"toast-subtree self-test: teardown"_q,
		.run = [=] {
			// Last on purpose. A timed-out stage or the watchdog skips
			// every stage after it, so anything still held here would
			// outlive the run: the toast was shown infinite, so no expiry
			// timer will ever take it down, and the sentinel is parented
			// into the primary window. Instance::hide() is the product's
			// own immediate path - _widget->hide();
			// _widget->deleteLater(); (ui/toast/toast.cpp:116-119) -
			// rather than hideAnimated(), whose fade-out animation the
			// harness's drained loop starves.
			// The zero reading below is taken around that same hide, and
			// the walk one statement above it is its control: a bare
			// empty answer at an arbitrary point would be an accident of
			// ordering, while the same walk answering the fixture toast
			// alone a statement earlier makes this module's own hide()
			// what took the count to zero.
			const auto toast = state->fixture.toastWidget.data();
			const auto before = FindLiveToasts();
			const auto beforeResolved = FindLiveToast();
			if (const auto instance = state->fixture.toast.get()) {
				instance->hide();
			}
			const auto after = FindLiveToasts();
			const auto afterResolved = FindLiveToast();
			Check(
				(before.size() == 1)
					&& (beforeResolved == toast)
					&& after.empty()
					&& (afterResolved == nullptr),
				u"zero live toasts is the walk's other ambiguity, and the "
				"resolver refuses that one too: the same walk answers the "
				"fixture toast alone, this module's own Instance::hide() "
				"takes it down, and FindLiveToast() then answers nullptr "
				"for the empty list rather than guessing"_q,
				u"before=%1 resolvedFixture=%2 after=%3 resolved=%4 "
				u"fixture=%5"_q
					.arg(int(before.size()))
					.arg((beforeResolved == toast) ? 1 : 0)
					.arg(int(after.size()))
					.arg(afterResolved
						? WidgetDescription(afterResolved)
						: u"<none>"_q)
					.arg(toast ? WidgetDescription(toast) : u"<none>"_q));
			state->fixture.sentinel = nullptr;
			state->fixture.toastWidget = nullptr;
			Note(u"toast-subtree self-test: fixture released, sentinel=%1 "
				"toast=%2"_q
				.arg(state->fixture.sentinel ? 1 : 0)
				.arg(state->fixture.toast ? 1 : 0));
		},
	});
}

} // namespace Test

#endif // _DEBUG
