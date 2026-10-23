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
#include "lang/lang_text_entity.h"
#include "test/test_capture.h"
#include "test/test_ink.h"
#include "test/test_log.h"
#include "test/test_text_reads.h"
#include "test/test_widgets.h"
#include "ui/effects/animation_value.h"
#include "ui/layers/show.h"
#include "ui/toast/toast.h"
#include "ui/toast/toast_widget.h"
#include "ui/widgets/labels.h"
#include "ui/rp_widget.h"
#include "window/window_controller.h"

#include <QtCore/QStringList>
#include <QtGui/QPainter>
#include <QtGui/QRegion>
#include <QtWidgets/QApplication>

#include <array>

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
constexpr auto kNotGoneBound = crl::time(300);

const auto kToastText = u"Harness toast subtree"_q;
const auto kOtherText = u"Harness toast elsewhere"_q;
const auto kExpiryText = u"Harness toast expiry"_q;
const auto kStateText = u"Harness toast state"_q;
const auto kAloneText = u"Harness toast left alone"_q;
const auto kNeverText = u"Harness toast never hidden"_q;
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
// (test_ink.cpp), so the nearest grey to a colour sits exactly half that
// colour's own channel span away: kFirstSentinel spans 190 and
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
// (toast_widget.cpp), so the background pixels are exactly st::toastBg
// composited over the grab's st::windowBg fill, while at shown level s
// every one of them is that same colour lerped back towards the fill and
// none of them matches. The toast's transparent corners and its opaque
// label ink match at no level at all and only ever lower the count, so a
// mean or a minimum would measure them instead of the fade.
[[nodiscard]] ToastFrame MeasureToastFrame(QImage image) {
	auto result = ToastFrame{
		.image = std::move(image),
		.separation = SettledSeparation(),
	};
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

[[nodiscard]] ToastFrame ReadToastFrame(const ToastSubtreeReading &reading) {
	if (!reading.resolved()) {
		return ToastFrame{ .separation = SettledSeparation() };
	}
	return MeasureToastFrame(GrabRect(reading.toast.data(), reading.frame));
}

// Taken before the grab on purpose: the grab of a toast below its full
// level runs disableChildrenPaintOnce() itself, so a count taken after it
// would measure this reading rather than the frame it is about to take.
[[nodiscard]] int ChildrenPaintDisabled(not_null<QWidget*> toast) {
	auto result = 0;
	for (const auto child : toast->children()) {
		if (child->isWidgetType()
			&& !static_cast<QWidget*>(child)->updatesEnabled()) {
			++result;
		}
	}
	return result;
}

[[nodiscard]] std::vector<QRect> LabelRects(
		not_null<QWidget*> toast,
		qreal ratio,
		QSize size) {
	auto result = std::vector<QRect>();
	const auto bounds = QRect(QPoint(), size);
	for (const auto label : FindAll<Ui::FlatLabel>(toast)) {
		if (label->accessibilityName().isEmpty()) {
			continue;
		}
		const auto logical = QRect(
			label->mapTo(toast.get(), QPoint()),
			label->size());
		result.push_back(DevicePixelRect(logical, ratio).intersected(bounds));
	}
	return result;
}

[[nodiscard]] int CountContentInk(
		const QImage &image,
		const QRect &rect,
		QColor background) {
	auto result = 0;
	const auto clipped = rect.intersected(image.rect());
	for (auto y = clipped.top(); y <= clipped.bottom(); ++y) {
		for (auto x = clipped.left(); x <= clipped.right(); ++x) {
			if (ChannelDelta(image.pixelColor(x, y), background)
				>= kInkDelta) {
				++result;
			}
		}
	}
	return result;
}

// Over the union of the label rects, so a pixel inside two overlapping
// labels is counted once per pen.
[[nodiscard]] std::vector<int> CountPenPixels(
		const QImage &image,
		const std::vector<QRect> &labels,
		const std::vector<InkCandidate> &pens) {
	auto result = std::vector<int>(pens.size(), 0);
	auto region = QRegion();
	for (const auto &rect : labels) {
		region += rect.intersected(image.rect());
	}
	for (const auto &rect : region) {
		for (auto y = rect.top(); y <= rect.bottom(); ++y) {
			for (auto x = rect.left(); x <= rect.right(); ++x) {
				const auto color = image.pixelColor(x, y);
				for (auto i = 0; i != int(pens.size()); ++i) {
					if (ChannelDelta(color, pens[i].color)
						<= kSameTolerance) {
						++result[i];
					}
				}
			}
		}
	}
	return result;
}

// Reads only |image|, |labels| and |pens|, so the same measurement applies
// unchanged to a saved PNG loaded back into a copy of the accepted frame.
void MeasureToastContent(ToastContentFrame &frame) {
	frame.labelInk.clear();
	frame.penPixels.clear();
	if (frame.image.isNull()) {
		frame.settledPermille = 0;
		frame.settled = false;
		frame.state = ToastContentState::Refused;
		frame.reason = u"the frame holds no image to measure"_q;
		return;
	}
	const auto measured = MeasureToastFrame(frame.image);
	frame.settledPermille = measured.settledPermille;
	frame.settled = measured.settled;
	const auto background = SettledToastBackground();
	for (const auto &rect : frame.labels) {
		frame.labelInk.push_back(
			CountContentInk(frame.image, rect, background));
	}
	frame.penPixels = CountPenPixels(frame.image, frame.labels, frame.pens);
	const auto unpainted = ranges::find_if(frame.labelInk, [](int ink) {
		return ink < kMinInkPixels;
	});
	const auto missing = ranges::find_if(frame.penPixels, [](int count) {
		return count < kMinInkPixels;
	});
	if (!frame.settled) {
		frame.state = ToastContentState::Unsettled;
		frame.reason = u"the background is not settled: %1/1000 samples "
			u"match st::toastBg's settled blend %2, below %3/1000"_q
			.arg(frame.settledPermille)
			.arg(background.name())
			.arg(kToastSettledMinPermille);
	} else if (frame.labels.empty()) {
		frame.state = ToastContentState::NoLabels;
		frame.reason = u"the toast holds no Ui::FlatLabel with a non-empty "
			u"accessibilityName(), so no painted text can be qualified - a "
			u"custom-content toast is unsupported"_q;
	} else if (unpainted != end(frame.labelInk)) {
		const auto index = int(unpainted - begin(frame.labelInk));
		frame.state = ToastContentState::NotPainted;
		frame.reason = u"label %1 at %2 holds %3 content-ink pixels (at "
			u"least %4 from the settled blend %5), below the floor %6"_q
			.arg(index)
			.arg(RectText(frame.labels[index]))
			.arg(*unpainted)
			.arg(kInkDelta)
			.arg(background.name())
			.arg(kMinInkPixels);
	} else if (missing != end(frame.penPixels)) {
		const auto &pen = frame.pens[missing - begin(frame.penPixels)];
		frame.state = ToastContentState::PenMissing;
		frame.reason = u"pen %1 %2 has %3 pixels within %4 inside the label "
			u"rects, below the floor %5"_q.arg(
				pen.name,
				pen.color.name(),
				QString::number(*missing),
				QString::number(kSameTolerance),
				QString::number(kMinInkPixels));
	} else {
		frame.state = ToastContentState::Qualified;
		frame.reason = u"none"_q;
	}
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

// Two tones rather than one, for the reason test_via_window.cpp's comment
// on PaintTwoTone gives: with a single tone the window-mapped control's
// non-blankness would rest on whatever the toast itself managed to paint,
// which is the thing under measurement. Their lightness is 160 against 96,
// a spread of 64 against the harness's kBlankSpreadThreshold of 6, so a
// window-mapped crop of this region clears the blank threshold on the bands
// alone.
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
	// The toast Widget is parented to the same |top| (Instance::Instance in
	// ui/toast/toast.cpp), so it and the sentinel are siblings sharing one
	// origin and geometry() maps between them with no conversion at all.
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

QString ToastPhaseName(ToastPhase phase) {
	switch (phase) {
	case ToastPhase::Unresolved:
		return u"toast-unresolved"_q;
	case ToastPhase::Unsettled:
		return u"toast-unsettled"_q;
	case ToastPhase::Shown:
		return u"toast-shown"_q;
	case ToastPhase::Hiding:
		return u"toast-hiding"_q;
	case ToastPhase::Gone:
		return u"toast-gone"_q;
	}
	return u"missing"_q;
}

namespace {

struct PhaseStep {
	ToastPhase phase = ToastPhase::Unresolved;
	crl::time elapsed = 0;
};

// Shown is the settled-background term ToastSubtreeReady takes, read on
// the same grab, and hiding is only ever answered after this chain has
// measured shown: a fade-in and a fade-out look alike in a single frame.
void MeasurePhase(ToastStateReading &reading) {
	const auto toast = reading.toast.data();
	if (!toast || !ranges::contains(FindLiveToasts(), toast)) {
		reading.phase = ToastPhase::Gone;
		reading.live = false;
		reading.settledPermille = -1;
		return;
	}
	reading.live = true;
	reading.identity = WidgetDescription(toast);
	const auto frame = ReadToastFrame(ReadToastSubtree(toast));
	reading.settledPermille = frame.settledPermille;
	if (frame.settled) {
		reading.phase = ToastPhase::Shown;
		reading.seenShown = true;
	} else {
		reading.phase = reading.seenShown
			? ToastPhase::Hiding
			: ToastPhase::Unsettled;
	}
}

[[nodiscard]] QString LiveToastEntry(not_null<QWidget*> widget) {
	return u"%1 \"%2\""_q.arg(
		WidgetDescription(widget),
		ReadToastText(widget));
}

[[nodiscard]] QStringList LiveToastEntries() {
	auto result = QStringList();
	for (const auto widget : FindLiveToasts()) {
		result.push_back(LiveToastEntry(widget));
	}
	return result;
}

[[nodiscard]] QString ToastStateText(const ToastStateReading &reading) {
	return u"toast-state: target=%1 text=\"%2\" phase=%3 live=%4 "
		u"seenShown=%5 settled=%6/1000 readings=%7"_q
		.arg(
			reading.identity.isEmpty() ? u"<none>"_q : reading.identity,
			reading.text,
			ToastPhaseName(reading.phase))
		.arg(reading.live ? 1 : 0)
		.arg(reading.seenShown ? 1 : 0)
		.arg((reading.settledPermille < 0)
			? u"none"_q
			: QString::number(reading.settledPermille))
		.arg(reading.readings);
}

void RecordPhase(
		std::vector<PhaseStep> &log,
		ToastPhase phase,
		crl::time elapsed) {
	if (log.empty() || log.back().phase != phase) {
		log.push_back({ .phase = phase, .elapsed = elapsed });
	}
}

[[nodiscard]] QString PhaseLogText(const std::vector<PhaseStep> &log) {
	auto steps = QStringList();
	for (const auto &step : log) {
		steps.push_back(u"%1@%2ms"_q
			.arg(ToastPhaseName(step.phase))
			.arg(step.elapsed));
	}
	return steps.isEmpty() ? u"<empty>"_q : steps.join(u" -> "_q);
}

// The log records changes only, so a toast left alone reads exactly
// Unsettled* Shown+ Hiding+ Gone when it holds these steps in this order.
[[nodiscard]] bool LeftAloneOrder(const std::vector<PhaseStep> &log) {
	auto phases = std::vector<ToastPhase>();
	for (const auto &step : log) {
		phases.push_back(step.phase);
	}
	if (!phases.empty() && phases.front() == ToastPhase::Unsettled) {
		phases.erase(begin(phases));
	}
	return (phases == std::vector{
		ToastPhase::Shown,
		ToastPhase::Hiding,
		ToastPhase::Gone,
	});
}

[[nodiscard]] crl::time PhaseElapsed(
		const std::vector<PhaseStep> &log,
		ToastPhase phase) {
	const auto i = ranges::find(log, phase, &PhaseStep::phase);
	return (i != end(log)) ? i->elapsed : crl::time(-1);
}

[[nodiscard]] crl::time ToastFadeIn() {
	return crl::time(st::defaultToast.durationFadeIn)
		* anim::SlowMultiplier();
}

[[nodiscard]] crl::time ToastFadeOut() {
	return crl::time(st::defaultToast.durationFadeOut)
		* anim::SlowMultiplier();
}

[[nodiscard]] base::weak_ptr<Ui::Toast::Instance> ShowStateToast(
		const QString &text,
		bool infinite) {
	const auto window = Core::App().activePrimaryWindow();
	if (!window) {
		return nullptr;
	}
	return Ui::Toast::Show(window->widget().get(), {
		.text = { text },
		.st = &st::defaultToast,
		.duration = Ui::Toast::kDefaultDuration,
		.infinite = infinite,
	});
}

[[nodiscard]] QWidget *InstanceWidget(
		const base::weak_ptr<Ui::Toast::Instance> &toast) {
	const auto instance = toast.get();
	return instance ? instance->widget().get() : nullptr;
}

} // namespace

ToastStateReading ReadToastState(QWidget *widget) {
	auto result = ToastStateReading{ .readings = 1 };
	if (!widget) {
		result.refusal = u"no widget was handed to the toast-state "
			u"reading"_q;
		return result;
	}
	result.identity = WidgetDescription(widget);
	if (!dynamic_cast<Ui::Toast::internal::Widget*>(widget)) {
		result.refusal = u"target is not a Ui::Toast::internal::Widget: "
			u"%1"_q.arg(result.identity);
		return result;
	}
	result.text = ReadToastText(widget);
	result.toast = widget;
	MeasurePhase(result);
	return result;
}

ToastStateReading RereadToastState(const ToastStateReading &previous) {
	auto result = previous;
	++result.readings;
	if (result.phase == ToastPhase::Unresolved) {
		if (result.refusal.isEmpty()) {
			result.refusal = u"the reading chain was not started by "
				u"ReadToastState"_q;
		}
		return result;
	} else if (result.phase == ToastPhase::Gone) {
		result.live = false;
		return result;
	}
	MeasurePhase(result);
	return result;
}

QString ToastStateDetails(const ToastStateReading &reading) {
	return ToastStateText(reading)
		+ (reading.refusal.isEmpty()
			? QString()
			: u" - %1"_q.arg(reading.refusal));
}

ToastGoneWait ReadToastGoneWait(
		const ToastStateReading &previous,
		crl::time waited,
		crl::time bound) {
	auto result = ToastGoneWait{
		.state = RereadToastState(previous),
		.waited = waited,
		.bound = bound,
	};
	const auto &state = result.state;
	if (state.phase == ToastPhase::Unresolved) {
		result.refusal = u"toast-unresolved: %1"_q.arg(state.refusal);
	} else if (!result.gone() && (waited > 0) && (waited >= bound)) {
		result.live = LiveToastEntries();
		result.refusal = u"toast-not-gone: %1 \"%2\" still %3 after %4 ms "
			u"(bound %5 ms); live toasts: [%6]"_q
			.arg(state.identity, state.text, ToastPhaseName(state.phase))
			.arg(waited)
			.arg(bound)
			.arg(result.live.join(u"; "_q));
	}
	return result;
}

QString ToastGoneWaitDetails(const ToastGoneWait &wait) {
	return ToastStateText(wait.state)
		+ u" waited=%1 bound=%2 gone=%3"_q
			.arg(wait.waited)
			.arg(wait.bound)
			.arg(wait.gone() ? 1 : 0)
		+ (wait.refusal.isEmpty()
			? QString()
			: u" - %1"_q.arg(wait.refusal));
}

QString ToastContentStateName(ToastContentState state) {
	switch (state) {
	case ToastContentState::Unread:
		return u"content-unread"_q;
	case ToastContentState::Refused:
		return u"content-refused"_q;
	case ToastContentState::Unsettled:
		return u"content-unsettled"_q;
	case ToastContentState::NoLabels:
		return u"content-no-labels"_q;
	case ToastContentState::NotPainted:
		return u"content-not-painted"_q;
	case ToastContentState::PenMissing:
		return u"content-pen-missing"_q;
	case ToastContentState::Qualified:
		return u"content-qualified"_q;
	}
	return u"missing"_q;
}

ToastContentFrame ReadToastContent(
		QWidget *widget,
		std::vector<InkCandidate> pens,
		crl::time waited,
		crl::time bound) {
	auto result = ToastContentFrame{
		.reading = ReadToastSubtree(widget),
		.pens = std::move(pens),
		.waited = waited,
		.bound = bound,
	};
	if (const auto toast = result.reading.toast.data()) {
		result.childrenPaintDisabled = ChildrenPaintDisabled(toast);
		result.image = GrabRect(toast, result.reading.frame);
		result.labels = LabelRects(
			toast,
			result.image.devicePixelRatio(),
			result.image.size());
		MeasureToastContent(result);
	} else {
		result.state = ToastContentState::Refused;
		result.reason = result.reading.refusal;
	}
	if (!result.qualified() && (waited > 0) && (waited >= bound)) {
		result.expired = true;
		result.reason = u"content-wait-expired after %1 ms (bound %2 ms): "
			u"%3 - %4"_q.arg(
				QString::number(waited),
				QString::number(bound),
				ToastContentStateName(result.state),
				result.reason);
	}
	return result;
}

QString ToastContentDetails(const ToastContentFrame &frame) {
	auto ink = QStringList();
	for (const auto count : frame.labelInk) {
		ink.push_back(QString::number(count));
	}
	auto pens = QStringList();
	for (auto i = 0; i != int(frame.pens.size()); ++i) {
		pens.push_back(u"%1=%2"_q.arg(
			frame.pens[i].name,
			(i < int(frame.penPixels.size()))
				? QString::number(frame.penPixels[i])
				: u"none"_q));
	}
	const auto &reading = frame.reading;
	return u"toast content: target=%1 frame=%2 state=%3 settled=%4/1000 "
		u"childrenPaintDisabled=%5 labels=%6 labelInk=[%7] floor=%8"_q.arg(
			reading.identity.isEmpty() ? u"<none>"_q : reading.identity,
			RectText(reading.frame),
			ToastContentStateName(frame.state),
			(frame.image.isNull()
				? u"none"_q
				: QString::number(frame.settledPermille)),
			((frame.childrenPaintDisabled < 0)
				? u"none"_q
				: QString::number(frame.childrenPaintDisabled)),
			QString::number(int(frame.labels.size())),
			ink.join(u","_q),
			QString::number(kMinInkPixels))
		+ u" pens=[%1] waited=%2 bound=%3 expired=%4 - %5"_q.arg(
			pens.join(u" "_q),
			QString::number(frame.waited),
			QString::number(frame.bound),
			frame.expired ? u"1"_q : u"0"_q,
			frame.reason.isEmpty() ? u"<none>"_q : frame.reason);
}

bool CaptureToastContent(
		const ToastContentFrame &accepted,
		const QString &name) {
	if (!accepted.qualified()) {
		Fail(
			u"capture %1"_q.arg(name),
			u"the frame is not content-qualified, so its pixels prove no "
			u"painted text or link: %1"_q.arg(ToastContentDetails(accepted)));
		return false;
	}
	if (const auto toast = accepted.reading.toast.data()) {
		const auto &frame = accepted.reading.frame;
		LogGeometry(
			name,
			QRect(toast->mapToGlobal(frame.topLeft()), frame.size()));
	}
	if (SaveImage(accepted.image, name).isEmpty()) {
		Fail(
			u"capture %1"_q.arg(name),
			u"the accepted frame could not be written: %1"_q.arg(
				ToastContentDetails(accepted)));
		return false;
	}
	return true;
}

void AppendToastSubtreeCaptureSelfTest(not_null<Runner*> runner) {
	struct State {
		Fixture fixture;
		QString midfadeDetails;
		QPointer<QWidget> expiringWidget;
		crl::time expiringShownAt = 0;
		crl::time liveInsideElapsed = 0;
		QString liveInsideDetails;
		int liveCount = 0;
		int failuresBefore = 0;
		int failuresAfter = 0;
		int singleAgainCount = 0;
		int liveInsideCount = 0;
		bool built = false;
		bool liveHoldsToast = false;
		bool liveResolvedSingle = false;
		bool midfadeReady = false;
		bool midfadeSaved = false;
		bool singleAgainResolvedFixture = false;
		bool expiringShown = false;
		bool liveInsideReady = false;
		bool liveInsideHoldsFixture = false;
		bool liveInsideHoldsExpiring = false;
	};
	// Leaked on purpose, the way the harness's other self-tests leak
	// theirs: the stages outlive this call. The teardown stage releases
	// the fixture, after which the State holds nothing but QStrings and
	// PODs - no rpl subscription to anything the session owns.
	const auto state = new State();
	const auto details = [=] {
		return FixtureDetails(state->fixture);
	};
	const auto liveWalkDetails = [=] {
		const auto all = FindLiveToasts();
		auto texts = QStringList();
		for (const auto widget : all) {
			const auto text = ReadToastText(widget);
			texts.push_back(text.isEmpty() ? u"<empty>"_q : text);
		}
		const auto resolved = FindLiveToast();
		return u"liveToasts=%1 texts=[%2] resolved=%3"_q
			.arg(int(all.size()))
			.arg(texts.join(u"; "_q))
			.arg(resolved ? WidgetDescription(resolved) : u"<none>"_q);
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
			// the restore as a Ui::PostponeCall (toggleChildrenPaint() and
			// scheduleChildrenPaintRestore() in toast_widget.cpp), and
			// every frame below must be taken after that restore has run.
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
			// AppendCaptureViaWindowSelfTest's retained-reading stage
			// (test_via_window.cpp) builds, reads and destroys its own
			// fixture, so nothing this stage creates reaches the stages
			// after it. It is infinite for the reason the fixture is: an
			// infinite toast leaves _hideAt at 0 (Instance::Instance in
			// toast.cpp) and the manager then arms no hide timer for it
			// (Manager::addToast in toast_manager.cpp), so nothing takes it
			// down implicitly and the drained loop's starving of
			// hideAnimated() never comes into it. Instance::hide() -
			// _widget->hide(); _widget->deleteLater(); (toast.cpp) - is the
			// seam, and its hide is synchronous while the walk reads
			// visibility, so the count drops in that same statement.
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
		.name = u"toast-subtree self-test: a default-duration toast is "
			"live on ToastSubtreeReady inside its lifetime"_q,
		.run = [=] {
			const auto toast = state->fixture.toastWidget.data();
			const auto parent = toast ? toast->parentWidget() : nullptr;
			if (!state->built || !toast || !parent) {
				return;
			}
			state->expiringShownAt = crl::now();
			const auto shown = Ui::Toast::Show(parent, {
				.text = { kExpiryText },
				.st = &st::defaultToast,
				.duration = Ui::Toast::kDefaultDuration,
				.infinite = false,
			});
			const auto instance = shown.get();
			state->expiringWidget = instance
				? instance->widget().get()
				: nullptr;
			state->expiringShown = (state->expiringWidget != nullptr);
		},
		.until = [=] {
			if (!state->built) {
				return true;
			}
			const auto widget = state->expiringWidget.data();
			return widget && ToastSubtreeReady(widget);
		},
		.then = [=] {
			const auto toast = state->fixture.toastWidget.data();
			const auto expiring = state->expiringWidget.data();
			if (!state->built || !toast || !expiring) {
				return;
			}
			const auto all = FindLiveToasts();
			auto texts = QStringList();
			for (const auto widget : all) {
				texts.push_back(ReadToastText(widget));
			}
			state->liveInsideCount = int(all.size());
			state->liveInsideDetails = liveWalkDetails();
			state->liveInsideReady = ToastSubtreeReady(expiring);
			state->liveInsideHoldsFixture = ranges::contains(all, toast);
			state->liveInsideHoldsExpiring = ranges::contains(
				all,
				expiring);
			state->liveInsideElapsed = crl::now() - state->expiringShownAt;
			Check(
				state->expiringShown
					&& state->liveInsideReady
					&& state->liveInsideHoldsFixture
					&& state->liveInsideHoldsExpiring
					&& (state->liveInsideCount == 2)
					&& texts.contains(kToastText)
					&& texts.contains(kExpiryText),
				u"a default-duration toast is live on ToastSubtreeReady "
				"inside its lifetime: the walk answers the fixture and "
				"the expiring toast together, so FindLiveToast() is "
				"nullptr because the count is not one"_q,
				u"%1 elapsed=%2ms ready=%3"_q
					.arg(state->liveInsideDetails)
					.arg(QString::number(state->liveInsideElapsed))
					.arg(state->liveInsideReady ? 1 : 0));
		},
		.timeout = kDefaultStageTimeout,
		.timeoutDetails = [=] {
			const auto widget = state->expiringWidget.data();
			return u"%1 elapsed=%2ms ready=%3 shown=%4"_q
				.arg(liveWalkDetails())
				.arg(QString::number(state->expiringShownAt
					? (crl::now() - state->expiringShownAt)
					: 0))
				.arg((widget && ToastSubtreeReady(widget)) ? 1 : 0)
				.arg(state->expiringShown ? 1 : 0);
		},
	});

	runner->add({
		.name = u"toast-subtree self-test: a default-duration toast is "
			"gone from the walk after its lifetime"_q,
		.until = [=] {
			if (!state->built || !state->expiringShown) {
				return true;
			}
			const auto widget = state->expiringWidget.data();
			const auto all = FindLiveToasts();
			return !widget || !ranges::contains(all, widget);
		},
		.then = [=] {
			const auto toast = state->fixture.toastWidget.data();
			if (!state->built || !toast) {
				return;
			}
			const auto all = FindLiveToasts();
			const auto resolved = FindLiveToast();
			const auto expiring = state->expiringWidget.data();
			const auto elapsed = crl::now() - state->expiringShownAt;
			auto texts = QStringList();
			for (const auto widget : all) {
				texts.push_back(ReadToastText(widget));
			}
			const auto holdsFixture = ranges::contains(all, toast);
			const auto holdsExpiring = expiring
				&& ranges::contains(all, expiring);
			Check(
				state->expiringShown
					&& state->liveInsideReady
					&& (state->liveInsideCount == 2)
					&& state->liveInsideHoldsFixture
					&& state->liveInsideHoldsExpiring
					&& (int(all.size()) == 1)
					&& holdsFixture
					&& !holdsExpiring
					&& (resolved == toast)
					&& texts.contains(kToastText)
					&& !texts.contains(kExpiryText),
				u"a non-infinite toast shown with kDefaultDuration is "
				"live inside its lifetime and gone from the walk after "
				"it, taken down by the manager hide timer rather than "
				"by this module's Instance::hide() seam"_q,
				u"inside %1 elapsed=%2ms; after %3 elapsed=%4ms "
				u"holdsFixture=%5 holdsExpiring=%6 resolvedFixture=%7"_q
					.arg(state->liveInsideDetails)
					.arg(QString::number(state->liveInsideElapsed))
					.arg(liveWalkDetails())
					.arg(QString::number(elapsed))
					.arg(holdsFixture ? 1 : 0)
					.arg(holdsExpiring ? 1 : 0)
					.arg((resolved == toast) ? 1 : 0));
		},
		.timeout = kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return u"%1 elapsed=%2ms shown=%3"_q
				.arg(liveWalkDetails())
				.arg(QString::number(state->expiringShownAt
					? (crl::now() - state->expiringShownAt)
					: 0))
				.arg(state->expiringShown ? 1 : 0);
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
			// _widget->deleteLater(); (ui/toast/toast.cpp) - rather than
			// hideAnimated(), whose fade-out animation the harness's
			// drained loop starves.
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

void AppendToastStateSelfTest(not_null<Runner*> runner) {
	struct State {
		base::weak_ptr<Ui::Toast::Instance> stateToast;
		base::weak_ptr<Ui::Toast::Instance> aloneToast;
		base::weak_ptr<Ui::Toast::Instance> neverToast;
		ToastStateReading first;
		ToastStateReading sameTurn;
		ToastStateReading stateChain;
		ToastStateReading aloneChain;
		ToastStateReading neverChain;
		ToastGoneWait stateWait;
		ToastGoneWait aloneWait;
		ToastGoneWait neverWait;
		std::vector<PhaseStep> stateLog;
		std::vector<PhaseStep> aloneLog;
		crl::time stateShownAt = 0;
		crl::time hiddenAt = 0;
		crl::time aloneShownAt = 0;
		crl::time aloneLastShown = -1;
		crl::time neverShownAt = 0;
		QString refusal;
		bool built = false;
	};
	// Leaked on purpose, as the module's other self-test leaks its own: the
	// stages outlive this call. The teardown stage hides every toast shown
	// here, after which the State holds readings, strings and PODs only.
	const auto state = new State();
	const auto walkText = [] {
		return u"liveToasts=[%1]"_q.arg(LiveToastEntries().join(u"; "_q));
	};
	const auto animationsGate = [] {
		return anim::Disabled()
			? u"animations are disabled in this process, so a fade has no "
				u"hiding phase to read"_q
			: QString();
	};
	const auto recordState = [=] {
		RecordPhase(
			state->stateLog,
			state->stateChain.phase,
			crl::now() - state->stateShownAt);
	};
	const auto recordAlone = [=](crl::time elapsed) {
		const auto phase = state->aloneChain.phase;
		RecordPhase(state->aloneLog, phase, elapsed);
		if (phase == ToastPhase::Shown) {
			state->aloneLastShown = elapsed;
		}
	};
	const auto aloneBound = [] {
		return Ui::Toast::kDefaultDuration
			+ ToastFadeIn()
			+ ToastFadeOut()
			+ kToastGoneMargin;
	};

	runner->add({
		.name = u"toast-state self-test: a fresh toast reads unsettled, then "
			"shown once its show settles"_q,
		.run = [=] {
			const auto window = Core::App().activePrimaryWindow();
			const auto live = LiveToastEntries();
			if (!window) {
				state->refusal = u"Core::App().activePrimaryWindow() is "
					u"null, so there is no window to parent the fixture "
					u"toasts to"_q;
			} else if (!live.isEmpty()) {
				state->refusal = u"%1 toast(s) already live, which the walk "
					u"would count beside this self-test's own: [%2]"_q
					.arg(int(live.size()))
					.arg(live.join(u"; "_q));
			} else {
				state->stateShownAt = crl::now();
				state->stateToast = ShowStateToast(kStateText, true);
				if (!InstanceWidget(state->stateToast)) {
					state->refusal = u"Ui::Toast::Show answered no "
						u"instance"_q;
				}
			}
			Check(
				state->refusal.isEmpty(),
				u"fixture gate: a primary window parents the toast-state "
				"fixtures and no other toast is live, so the walk counts "
				"only this self-test's own toasts"_q,
				state->refusal.isEmpty() ? walkText() : state->refusal);
			if (!state->refusal.isEmpty()) {
				return;
			}
			state->built = true;
			state->first = ReadToastState(InstanceWidget(state->stateToast));
			state->stateChain = state->first;
			recordState();
		},
		.until = [=] {
			if (!state->refusal.isEmpty()) {
				return true;
			}
			auto &chain = state->stateChain;
			chain = RereadToastState(chain);
			recordState();
			return (chain.phase == ToastPhase::Shown)
				|| (chain.phase == ToastPhase::Gone)
				|| (chain.phase == ToastPhase::Unresolved);
		},
		.then = [=] {
			if (!state->refusal.isEmpty()) {
				return;
			}
			const auto &first = state->first;
			const auto &chain = state->stateChain;
			Check(
				(first.phase == ToastPhase::Unsettled)
					&& first.live
					&& (chain.phase == ToastPhase::Shown)
					&& chain.live
					&& chain.seenShown,
				u"a fresh toast reads toast-unsettled in the turn it is "
				"created, where its shown level is still 0, and toast-shown "
				"once its fade-in settles - the settled-background term "
				"ToastSubtreeReady takes"_q,
				u"first: %1; settled: %2; phases %3"_q.arg(
					ToastStateDetails(first),
					ToastStateDetails(chain),
					PhaseLogText(state->stateLog)));
		},
		.timeoutDetails = [=] {
			return u"%1; %2"_q.arg(
				ToastStateDetails(state->stateChain),
				walkText());
		},
	});

	runner->addGated([=] { return state->refusal; }, [=] {
		runner->add({
			.name = u"toast-state self-test: right after hideAnimated() the "
				"toast reads hiding while the walk still counts it"_q,
			.skipReason = animationsGate,
			.run = [=] {
				state->hiddenAt = crl::now();
				if (const auto instance = state->stateToast.get()) {
					instance->hideAnimated();
				}
				state->sameTurn = RereadToastState(state->stateChain);
				state->stateChain = state->sameTurn;
				recordState();
			},
			.until = [=] {
				auto &chain = state->stateChain;
				chain = RereadToastState(chain);
				recordState();
				// Bounded, so a fade that never starts is a named FAIL in
				// |then| and not a stage timeout ending the scenario.
				const auto elapsed = crl::now() - state->hiddenAt;
				return (chain.phase != ToastPhase::Shown)
					|| (elapsed >= ToastFadeOut() + kToastGoneMargin);
			},
			.then = [=] {
				const auto &chain = state->stateChain;
				const auto widget = chain.toast.data();
				const auto elapsed = crl::now() - state->hiddenAt;
				const auto fadeOut = ToastFadeOut();
				const auto counted = widget
					&& ranges::contains(FindLiveToasts(), widget);
				Check(
					(chain.phase == ToastPhase::Hiding)
						&& chain.live
						&& counted
						&& (elapsed < fadeOut),
					u"after hideAnimated() the toast reads toast-hiding "
					"inside its fade-out while FindLiveToasts() still counts "
					"it: live in the walk is not shown"_q,
					u"%1 counted=%2 elapsed=%3ms fadeOut=%4ms; %5"_q
						.arg(ToastStateDetails(chain))
						.arg(counted ? 1 : 0)
						.arg(elapsed)
						.arg(fadeOut)
						.arg(walkText()));
				Check(
					state->sameTurn.phase == ToastPhase::Shown,
					u"documented limit: in the very turn hideAnimated() is "
					"called the fade has run no frame and the shown level is "
					"still 1, so no measurement can tell the toast from a "
					"shown one - the reading answers toast-shown there and "
					"toast-hiding from the first grab measurably below the "
					"settled blend"_q,
					ToastStateDetails(state->sameTurn));
			},
			.timeoutDetails = [=] {
				return u"%1; %2"_q.arg(
					ToastStateDetails(state->stateChain),
					walkText());
			},
		});

		runner->add({
			.name = u"toast-state self-test: after the fade the toast reads "
				"gone within the fade-out plus kToastGoneMargin"_q,
			.skipReason = animationsGate,
			.until = [=] {
				state->stateWait = ReadToastGoneWait(
					state->stateChain,
					crl::now() - state->hiddenAt,
					ToastFadeOut() + kToastGoneMargin);
				state->stateChain = state->stateWait.state;
				recordState();
				return state->stateWait.done();
			},
			.then = [=] {
				const auto &wait = state->stateWait;
				auto stateTextLive = false;
				for (const auto widget : FindLiveToasts()) {
					if (ReadToastText(widget) == kStateText) {
						stateTextLive = true;
					}
				}
				Check(
					wait.gone()
						&& wait.refusal.isEmpty()
						&& (wait.waited <= wait.bound)
						&& !stateTextLive,
					u"after the fade the toast reads toast-gone and the "
					"bounded wait returns within the fade-out plus "
					"kToastGoneMargin, measured from the hideAnimated() "
					"call, with no live toast left reading its phrase"_q,
					u"%1; phases %2; %3"_q.arg(
						ToastGoneWaitDetails(wait),
						PhaseLogText(state->stateLog),
						walkText()));
			},
		});

		runner->add({
			.name = u"toast-state self-test: a toast left alone reads shown "
				"until its duration ends, then hiding, then gone"_q,
			.skipReason = animationsGate,
			.run = [=] {
				state->aloneShownAt = crl::now();
				state->aloneToast = ShowStateToast(kAloneText, false);
				state->aloneChain = ReadToastState(
					InstanceWidget(state->aloneToast));
				recordAlone(crl::now() - state->aloneShownAt);
			},
			.until = [=] {
				const auto elapsed = crl::now() - state->aloneShownAt;
				state->aloneWait = ReadToastGoneWait(
					state->aloneChain,
					elapsed,
					aloneBound());
				state->aloneChain = state->aloneWait.state;
				recordAlone(elapsed);
				return state->aloneWait.done();
			},
			.then = [=] {
				const auto &wait = state->aloneWait;
				const auto &log = state->aloneLog;
				const auto ordered = LeftAloneOrder(log);
				const auto firstHiding = PhaseElapsed(log, ToastPhase::Hiding);
				const auto duration = Ui::Toast::kDefaultDuration;
				Check(
					ordered
						&& (firstHiding >= duration)
						&& (state->aloneLastShown >= duration / 2)
						&& wait.gone()
						&& wait.refusal.isEmpty()
						&& (wait.waited <= wait.bound),
					u"a default-duration toast left alone reads toast-shown "
					"until its duration ends, then toast-hiding, then "
					"toast-gone within the bound: the manager's hide timer "
					"fades only a toast whose hide time has passed, so "
					"hiding is never read before the duration, and a shown "
					"reading at half the duration or later shows the shown "
					"phase lasting through the toast's life"_q,
					u"phases %1; ordered=%2 firstHiding=%3ms (at least %4ms) "
					u"lastShown=%5ms (at least %6ms); %7"_q
						.arg(PhaseLogText(log))
						.arg(ordered ? 1 : 0)
						.arg(firstHiding)
						.arg(duration)
						.arg(state->aloneLastShown)
						.arg(duration / 2)
						.arg(ToastGoneWaitDetails(wait)));
			},
		});

		runner->add({
			.name = u"toast-state self-test: a toast never hidden ends the "
				"bounded wait in a named refusal naming it"_q,
			.run = [=] {
				state->neverShownAt = crl::now();
				state->neverToast = ShowStateToast(kNeverText, true);
				state->neverChain = ReadToastState(
					InstanceWidget(state->neverToast));
			},
			.until = [=] {
				state->neverWait = ReadToastGoneWait(
					state->neverChain,
					crl::now() - state->neverShownAt,
					kNotGoneBound);
				state->neverChain = state->neverWait.state;
				return state->neverWait.done();
			},
			.then = [=] {
				const auto &wait = state->neverWait;
				const auto widget = InstanceWidget(state->neverToast);
				const auto identity = widget
					? WidgetDescription(widget)
					: QString();
				const auto entry = widget ? LiveToastEntry(widget) : QString();
				Check(
					!wait.gone()
						&& wait.refusal.startsWith(u"toast-not-gone"_q)
						&& (widget != nullptr)
						&& wait.refusal.contains(identity)
						&& wait.refusal.contains(kNeverText)
						&& !wait.live.isEmpty()
						&& wait.live.contains(entry)
						&& (wait.waited >= kNotGoneBound),
					u"a toast not hidden within the bound ends the wait in "
					"a named toast-not-gone refusal that names it and lists "
					"the live toasts, never in a silent false"_q,
					u"%1; expected entry \"%2\""_q.arg(
						ToastGoneWaitDetails(wait),
						entry));
				if (const auto instance = state->neverToast.get()) {
					instance->hide();
				}
				state->neverChain = RereadToastState(state->neverChain);
				Check(
					state->neverChain.phase == ToastPhase::Gone,
					u"control: the same toast reads toast-gone in the turn "
					"Instance::hide() takes it out of the walk"_q,
					ToastStateDetails(state->neverChain));
			},
		});

		runner->add({
			.name = u"toast-state self-test: refusals are values, never a "
				"silent answer"_q,
			.run = [=] {
				const auto window = Core::App().activePrimaryWindow();
				const auto top = window ? window->widget().get() : nullptr;
				const auto before = FailureCount();
				const auto missing = ReadToastState(nullptr);
				const auto missingAgain = RereadToastState(missing);
				const auto owner = ReadToastState(top);
				const auto wait = ReadToastGoneWait(
					missing,
					crl::time(0),
					crl::time(1000));
				const auto after = FailureCount();
				Check(
					(missing.phase == ToastPhase::Unresolved)
						&& !missing.live
						&& missing.refusal.contains(u"no widget was handed"_q)
						&& (missingAgain.phase == ToastPhase::Unresolved),
					u"a null widget - such as a FindLiveToast() that refused "
					"an ambiguity - reads toast-unresolved by name and stays "
					"unresolved on a reread, never toast-gone"_q,
					u"%1; reread: %2"_q.arg(
						ToastStateDetails(missing),
						ToastStateDetails(missingAgain)));
				Check(
					top
						&& (owner.phase == ToastPhase::Unresolved)
						&& !owner.live
						&& owner.refusal.contains(
							u"not a Ui::Toast::internal::Widget"_q)
						&& owner.refusal.contains(WidgetDescription(top)),
					u"a widget that is not a toast - the primary window - "
					"reads toast-unresolved naming the handed widget"_q,
					ToastStateDetails(owner));
				Check(
					!wait.gone()
						&& wait.done()
						&& wait.refusal.startsWith(u"toast-unresolved"_q)
						&& wait.refusal.contains(missing.refusal),
					u"the gone wait on an unresolved chain refuses "
					"toast-unresolved at once, even at waited 0"_q,
					ToastGoneWaitDetails(wait));
				Check(
					after == before,
					u"none of these refusals logged a failure: a refusal is "
					"a returned value the caller decides about"_q,
					u"failures before=%1 after=%2"_q.arg(before).arg(after));
			},
		});
	});

	runner->addRecovery({
		.name = u"toast-state self-test: teardown"_q,
		.run = [=] {
			const auto toasts = {
				&state->stateToast,
				&state->aloneToast,
				&state->neverToast,
			};
			for (const auto toast : toasts) {
				if (const auto instance = toast->get()) {
					instance->hide();
				}
			}
			if (state->built) {
				const auto live = LiveToastEntries();
				Check(
					live.isEmpty(),
					u"the toast-state self-test leaves no live toast behind: "
					"Instance::hide() took every toast it showed out of the "
					"walk"_q,
					u"liveToasts=[%1]"_q.arg(live.join(u"; "_q)));
			}
			Note(u"toast-state self-test: fixture released, built=%1"_q
				.arg(state->built ? 1 : 0));
		},
	});
}

namespace {

constexpr auto kRichToastCount = 3;
constexpr auto kContentFadeInMargin = crl::time(100);
constexpr auto kNeverPaintedBound = crl::time(600);

const auto kPlainText = u"Harness plain toast"_q;
const auto kRichWrongText = u"Harness rich toast elsewhere"_q;

// Index 0 is the plain toast and 1..kRichToastCount the rich ones, so the
// toast a repeat hides animated is always the one at the index before.
struct ContentToast {
	base::weak_ptr<Ui::Toast::Instance> instance;
	QPointer<QWidget> widget;
	QString expected;
	crl::time shownAt = 0;
	ToastContentFrame accepted;
};

struct ContentSelfTest {
	std::shared_ptr<Ui::Show> show;
	base::unique_qptr<Ui::RpWidget> sentinel;
	std::array<ContentToast, kRichToastCount + 1> toasts;
	std::vector<QPointer<QWidget>> withheld;
	ToastContentFrame never;
	crl::time withheldAt = 0;
	int actions = 0;
	QString refusal;
	bool built = false;
};

[[nodiscard]] InkCandidate TextPen() {
	return { .name = u"text"_q, .color = st::toastFg->c };
}

[[nodiscard]] InkCandidate LinkPen() {
	return {
		.name = u"link"_q,
		.color = st::defaultToast.palette.linkFg->c,
	};
}

[[nodiscard]] std::vector<InkCandidate> RichPens() {
	return { TextPen(), LinkPen() };
}

[[nodiscard]] QString RichToastExpected(int index) {
	return u"Harness rich toast %1: bold words and a harness link"_q.arg(
		index);
}

// tr::link wraps the phrase in a CustomUrl entity, which is what makes the
// toast actionable (HasLinksOrSpoilers in toast_widget.cpp), and the
// filter is its action: it counts the click and consumes it.
[[nodiscard]] Ui::Toast::Config RichToastConfig(
		not_null<ContentSelfTest*> state,
		int index) {
	auto text = tr::marked(u"Harness rich toast %1: "_q.arg(index));
	text.append(tr::bold(u"bold words"_q))
		.append(u" and "_q)
		.append(tr::link(u"a harness link"_q));
	return {
		.text = std::move(text),
		.filter = [=](const ClickHandlerPtr &, Qt::MouseButton) {
			++state->actions;
			return false;
		},
		.st = &st::defaultToast,
		.adaptive = true,
		.infinite = true,
	};
}

// Every toast goes through the one Ui::Show, so the repeat is the
// product's own: showToast() calls hideAnimated() on the toast it showed
// last. The sentinel shares the toasts' parent and is never raised, so a
// toast created after it paints above it and the sentinel is exactly the
// surface beneath.
void ShowContentToast(
		not_null<ContentSelfTest*> state,
		int index,
		Ui::Toast::Config &&config,
		const QString &expected) {
	auto &toast = state->toasts[index];
	toast.expected = expected;
	toast.shownAt = crl::now();
	toast.instance = state->show->showToast(std::move(config));
	const auto instance = toast.instance.get();
	toast.widget = instance ? instance->widget().get() : nullptr;
	const auto widget = toast.widget.data();
	const auto sentinel = state->sentinel.get();
	if (widget && sentinel) {
		sentinel->setGeometry(widget->geometry());
		sentinel->show();
	}
}

[[nodiscard]] bool AwaitContent(
		ContentToast &toast,
		std::vector<InkCandidate> pens) {
	toast.accepted = ReadToastContent(
		toast.widget.data(),
		std::move(pens),
		crl::now() - toast.shownAt);
	return toast.accepted.decided();
}

[[nodiscard]] QString WidgetText(QWidget *widget) {
	return widget ? WidgetDescription(widget) : u"<none>"_q;
}

// The PNG is read back from disk and that file, not the frame in memory,
// is compared pixel for pixel, measured again and searched for the
// surface beneath.
void CheckSavedContent(
		const ToastContentFrame &accepted,
		const QString &name) {
	const auto saved = CaptureToastContent(accepted, name);
	const auto path = ScreenshotsDir() + name + u".png"_q;
	const auto reloaded = saved ? QImage(path) : QImage();
	const auto diff = CompareFrames(accepted.image, reloaded);
	auto remeasured = accepted;
	remeasured.image = reloaded;
	MeasureToastContent(remeasured);
	Check(
		saved
			&& diff.comparable
			&& !diff.differing
			&& remeasured.qualified(),
		u"the saved PNG is the accepted frame pixel for pixel and qualifies "
		"again when measured from disk, so readiness and saving qualified "
		"the same image"_q,
		u"%1 comparable=%2 differing=%3 maxDelta=%4; saved: %5"_q.arg(
			ImageText(path, reloaded),
			QString::number(diff.comparable ? 1 : 0),
			QString::number(diff.differing),
			QString::number(diff.maxDelta),
			ToastContentDetails(remeasured)));
	const auto acceptedSentinel = SentinelPixels(accepted.image);
	const auto savedSentinel = SentinelPixels(reloaded);
	Check(
		saved
			&& !reloaded.isNull()
			&& !acceptedSentinel
			&& !savedSentinel,
		u"the accepted and the saved frame hold no pixel within %1 channel "
		"units of either sentinel tone painted beneath the toast"_q.arg(
			kSentinelTolerance),
		u"accepted sentinelPixels=%1 saved sentinelPixels=%2 %3"_q.arg(
			QString::number(acceptedSentinel),
			QString::number(savedSentinel),
			ImageText(path, reloaded)));
}

void AppendRichContentStage(
		not_null<Runner*> runner,
		not_null<ContentSelfTest*> state,
		int index) {
	runner->add({
		.name = u"toast-content self-test: repeated rich/link toast %1 is "
			"accepted with text and link painted in the saved pixels"_q.arg(
				index),
		.run = [=] {
			ShowContentToast(
				state,
				index,
				RichToastConfig(state, index),
				RichToastExpected(index));
		},
		.until = [=] {
			return AwaitContent(state->toasts[index], RichPens());
		},
		.then = [=] {
			const auto &toast = state->toasts[index];
			const auto &previous = state->toasts[index - 1];
			const auto &frame = toast.accepted;
			const auto widget = toast.widget.data();
			Check(
				frame.qualified(),
				u"repeated rich toast %1 is content-qualified: its text and "
				"link pens are painted in the same grab that measured its "
				"settled background"_q.arg(index),
				ToastContentDetails(frame));
			CheckToastReads(
				widget,
				toast.expected,
				u"repeated rich toast %1 reads its own phrase back through "
				"its Ui::FlatLabel"_q.arg(index));
			const auto read = NormalizeSpaces(ReadToastText(widget));
			Check(
				read != NormalizeSpaces(kRichWrongText),
				u"the text term discriminates independently of paint: the "
				"read that matches rich toast %1's phrase does not match a "
				"wrong one"_q.arg(index),
				u"read=\"%1\" wrong=\"%2\""_q.arg(read, kRichWrongText));
			CheckSavedContent(frame, u"toast_content_rich_%1"_q.arg(index));

			const auto live = FindLiveToasts();
			const auto before = previous.widget.data();
			const auto beforeLive = before && ranges::contains(live, before);
			const auto currentLive = widget && ranges::contains(live, widget);
			const auto fades = !anim::Disabled();
			Check(
				(beforeLive || !fades)
					&& currentLive
					&& (widget != nullptr)
					&& (frame.reading.toast == widget)
					&& (read == NormalizeSpaces(toast.expected))
					&& (read != NormalizeSpaces(previous.expected)),
				u"the repeat overlaps the toast before it: Ui::Show::showToast "
				"faded that one out animated and the walk still holds it "
				"beside rich toast %1, while the accepted frame and its "
				"read-back belong to the new toast"_q.arg(index),
				u"previous=%1 \"%2\" live=%3; current=%4 live=%5 frame=%6 "
				u"read=\"%7\"; %8%9"_q.arg(
					WidgetText(before),
					previous.expected,
					QString::number(beforeLive ? 1 : 0),
					WidgetText(widget),
					QString::number(currentLive ? 1 : 0),
					frame.reading.identity,
					read,
					u"liveToasts=[%1]"_q.arg(LiveToastEntries().join(u"; "_q)),
					(fades
						? QString()
						: u" - animations are disabled in this process, so "
							u"the previous toast has no fade to overlap"_q)));
		},
		.timeoutDetails = [=] {
			return ToastContentDetails(state->toasts[index].accepted);
		},
	});
}

// No drain between the lever and the readings: lib_ui's restore is a
// Ui::PostponeCall tagged with the main loop's level, and the grab's own
// paint event unwinds one event level deeper, so the restore stays queued
// until this runner tick unwinds or SettlePostponedCalls() runs it.
void CheckMissingPaint(not_null<ContentSelfTest*> state) {
	const auto &rich = state->toasts[kRichToastCount];
	const auto widget = rich.widget.data();
	const auto toast = dynamic_cast<Ui::Toast::internal::Widget*>(widget);
	Check(
		toast != nullptr,
		u"fixture gate: the last rich toast is alive for the lifecycle "
		"lever"_q,
		WidgetText(widget));
	if (!toast) {
		return;
	}
	const auto pens = RichPens();
	const auto pre = ReadToastContent(widget, pens);
	toast->setShownLevel(0.5);
	const auto midfade = GrabToastSubtree(widget);
	toast->setShownLevel(1.);
	const auto sig = ReadToastContent(widget, pens);
	const auto oldReady = ToastSubtreeReady(widget);
	const auto read = NormalizeSpaces(ReadToastText(widget));
	const auto sigPath = SaveImage(
		sig.image,
		u"toast_content_missing_paint"_q);
	const auto controlName = u"toast_content_settled_only_control"_q;
	const auto failuresBefore = FailureCount();
	const auto controlSaved = CaptureToastSubtree(widget, controlName);
	const auto failuresAfter = FailureCount();
	const auto controlPath = ScreenshotsDir() + controlName + u".png"_q;
	auto control = sig;
	control.image = controlSaved ? QImage(controlPath) : QImage();
	MeasureToastContent(control);
	SettlePostponedCalls();
	const auto after = ReadToastContent(widget, pens);

	const auto bracket = u"pre: %1; signature: %2; after the drain: %3"_q.arg(
		ToastContentDetails(pre),
		ToastContentDetails(sig),
		ToastContentDetails(after));
	Check(
		pre.qualified()
			&& (pre.childrenPaintDisabled == 0)
			&& !midfade.isNull()
			&& (sig.childrenPaintDisabled > 0),
		u"fixture gate: the lifecycle lever reproduced the cause - a grab at "
		"shown level 0.5 ran Widget::paintEvent's proxy branch, whose "
		"disableChildrenPaintOnce() left the toast's direct children with "
		"Qt::WA_UpdatesDisabled and its restore queued"_q,
		u"midfade=%1x%2; %3"_q.arg(
			QString::number(midfade.width()),
			QString::number(midfade.height()),
			bracket));
	const auto unpainted = !sig.labels.empty()
		&& ranges::all_of(sig.labelInk, [](int ink) {
			return ink < kMinInkPixels;
		});
	Check(
		sig.settled
			&& oldReady
			&& (read == NormalizeSpaces(rich.expected))
			&& (sig.state == ToastContentState::NotPainted)
			&& unpainted,
		u"the missing-paint signature is reproduced: at full level the frame "
		"measures the settled background and ToastSubtreeReady answers "
		"true, the read-back matches the whole phrase, and no label rect "
		"holds content ink"_q,
		u"ToastSubtreeReady=%1 read=\"%2\" expected=\"%3\" image=%4; %5"_q.arg(
			QString::number(oldReady ? 1 : 0),
			read,
			rich.expected,
			ImageText(sigPath, sig.image),
			ToastContentDetails(sig)));
	Check(
		!sig.qualified(),
		u"rejecting control: the content contract refuses the background-only "
		"frame that background settledness and the read-back admit"_q,
		ToastContentDetails(sig));
	Check(
		controlSaved
			&& (failuresAfter == failuresBefore)
			&& (control.state == ToastContentState::NotPainted),
		u"removed-qualification control: CaptureToastSubtree, which qualifies "
		"background settledness only, saves that same missing-paint frame "
		"without a failure, and the saved PNG measures content-not-painted"_q,
		u"saved=%1 failures before=%2 after=%3 %4; %5"_q.arg(
			QString::number(controlSaved ? 1 : 0),
			QString::number(failuresBefore),
			QString::number(failuresAfter),
			ImageText(controlPath, control.image),
			ToastContentDetails(control)));
	Check(
		read != NormalizeSpaces(kRichWrongText),
		u"the text term stays separate from paint: on the unpainted frame "
		"the same read still rejects a wrong phrase"_q,
		u"read=\"%1\" wrong=\"%2\""_q.arg(read, kRichWrongText));
	Check(
		after.qualified() && (after.childrenPaintDisabled == 0),
		u"the lifecycle completes: once lib_ui's postponed restore runs, the "
		"same toast's children paint again and the frame qualifies"_q,
		bracket);
}

void ReleaseWithheld(not_null<ContentSelfTest*> state) {
	for (const auto &child : base::take(state->withheld)) {
		if (const auto raw = child.data()) {
			raw->setAttribute(Qt::WA_UpdatesDisabled, false);
		}
	}
}

void CheckNeverPainted(not_null<ContentSelfTest*> state) {
	const auto &rich = state->toasts[kRichToastCount];
	const auto widget = rich.widget.data();
	const auto &frame = state->never;
	Check(
		frame.expired
			&& !frame.qualified()
			&& (frame.state == ToastContentState::NotPainted)
			&& frame.reason.startsWith(u"content-wait-expired"_q)
			&& (frame.waited >= kNeverPaintedBound),
		u"a frame that never acquires its content ends the bounded wait in "
		"the named content-wait-expired diagnostic, keeping "
		"content-not-painted, and never in a stage timeout"_q,
		u"withheldChildren=%1; %2"_q.arg(
			QString::number(int(state->withheld.size())),
			ToastContentDetails(frame)));
	CheckToastReads(
		widget,
		rich.expected,
		u"text read-back is a separate assertion: the toast whose children "
		"never paint still reads its phrase back"_q);
	ReleaseWithheld(state);
	const auto restored = ReadToastContent(widget, RichPens());
	Check(
		restored.qualified(),
		u"control: the same toast qualifies once its children may paint "
		"again"_q,
		ToastContentDetails(restored));
}

} // namespace

void AppendToastContentCaptureSelfTest(not_null<Runner*> runner) {
	// Leaked on purpose, as the module's other self-tests leak theirs: the
	// stages outlive this call. The teardown stage hides every toast and
	// releases the sentinel and the Ui::Show, after which the state holds
	// frames, strings and PODs only.
	const auto state = new ContentSelfTest();

	runner->add({
		.name = u"toast-content self-test: a plain toast is accepted with its "
			"text painted in the saved pixels"_q,
		.run = [=] {
			const auto window = Core::App().activePrimaryWindow();
			const auto live = LiveToastEntries();
			const auto separation = MeasureSentinelSeparation();
			if (!window) {
				state->refusal = u"Core::App().activePrimaryWindow() is "
					u"null, so there is no Ui::Show to show the fixture "
					u"toasts through"_q;
			} else if (!live.isEmpty()) {
				state->refusal = u"%1 toast(s) already live, which the walk "
					u"would count beside this self-test's own: [%2]"_q
					.arg(int(live.size()))
					.arg(live.join(u"; "_q));
			} else if (separation.minimum < kSentinelMinSeparation) {
				state->refusal = u"the sentinel tones are not clear of this "
					u"palette: %1"_q.arg(SentinelSeparationText(separation));
			} else {
				state->built = true;
				state->show = window->uiShow();
				state->sentinel = base::make_unique_q<Ui::RpWidget>(
					state->show->toastParent());
				PaintSentinelBands(state->sentinel.get());
				auto plain = Ui::Toast::Config{
					.text = tr::marked(kPlainText),
					.st = &st::defaultToast,
					.infinite = true,
				};
				ShowContentToast(state, 0, std::move(plain), kPlainText);
				if (!state->toasts[0].widget) {
					state->refusal = u"Ui::Show::showToast answered no "
						u"instance"_q;
				}
			}
			Check(
				state->refusal.isEmpty(),
				u"fixture gate: a primary window's Ui::Show shows the "
				"fixture toasts, no other toast is live, and the sentinel "
				"tones beneath them stay clear of every colour a "
				"toast-rooted frame can hold"_q,
				state->refusal.isEmpty()
					? SentinelSeparationText(separation)
					: state->refusal);
		},
		.until = [=] {
			return !state->refusal.isEmpty()
				|| AwaitContent(state->toasts[0], { TextPen() });
		},
		.then = [=] {
			if (!state->refusal.isEmpty()) {
				return;
			}
			const auto &plain = state->toasts[0];
			const auto widget = plain.widget.data();
			const auto &frame = plain.accepted;
			Check(
				frame.qualified(),
				u"a plain toast is content-qualified: its label's content ink "
				"and the text pen are painted in the same grab that measured "
				"its settled background"_q,
				ToastContentDetails(frame));
			CheckToastReads(
				widget,
				kPlainText,
				u"the plain toast reads its phrase back through its "
				"Ui::FlatLabel"_q);
			CheckSavedContent(frame, u"toast_content_plain"_q);
			const auto beneath = SentinelPixels(GrabViaWindow(widget));
			Check(
				beneath > 0,
				u"non-vacuity: the window-mapped grab of the same toast holds "
				"sentinel pixels where its rounded corners leave the surface "
				"beneath uncovered, so the saved frame's zero is measured"_q,
				u"windowMapped sentinelPixels=%1 toast=%2"_q.arg(
					QString::number(beneath),
					WidgetText(widget)));
			const auto linkless = ReadToastContent(widget, { LinkPen() });
			Check(
				(linkless.state == ToastContentState::PenMissing)
					&& !linkless.penPixels.empty()
					&& (linkless.penPixels.front() < kMinInkPixels),
				u"the pen term discriminates: asked for the link pen, the "
				"painted plain toast reads content-pen-missing"_q,
				ToastContentDetails(linkless));
		},
		.timeoutDetails = [=] {
			return ToastContentDetails(state->toasts[0].accepted);
		},
	});

	runner->addGated([=] { return state->refusal; }, [=] {
		for (auto index = 1; index <= kRichToastCount; ++index) {
			AppendRichContentStage(runner, state, index);
		}

		runner->add({
			.name = u"toast-content self-test: the missing-paint signature: "
				"read-back matches, the frame has no paint, the content "
				"contract refuses it and the settled-only contract admits "
				"it"_q,
			// WHY: the stage before grabbed this toast near the end of its
			// fade-in, which can leave lib_ui's restore queued until that
			// tick unwinds; waiting out the fade-in starts the lever from
			// a full-level toast whose children paint.
			.until = [=] {
				const auto &rich = state->toasts[kRichToastCount];
				return !rich.widget
					|| (crl::now() - rich.shownAt
						>= ToastFadeIn() + kContentFadeInMargin);
			},
			.then = [=] {
				CheckMissingPaint(state);
			},
		});

		runner->add({
			.name = u"toast-content self-test: a frame that never acquires its "
				"content ends in a named bounded diagnostic"_q,
			.run = [=] {
				// Set directly rather than through lib_ui, so its own
				// _childrenPaintDisabled stays false and the postponed
				// restore never clears it.
				state->withheld.clear();
				const auto &rich = state->toasts[kRichToastCount];
				if (const auto widget = rich.widget.data()) {
					for (const auto child : widget->children()) {
						if (!child->isWidgetType()) {
							continue;
						}
						const auto raw = static_cast<QWidget*>(child);
						raw->setAttribute(Qt::WA_UpdatesDisabled, true);
						state->withheld.push_back(raw);
					}
				}
				state->withheldAt = crl::now();
			},
			.until = [=] {
				state->never = ReadToastContent(
					state->toasts[kRichToastCount].widget.data(),
					RichPens(),
					crl::now() - state->withheldAt,
					kNeverPaintedBound);
				return state->never.decided();
			},
			.then = [=] {
				CheckNeverPainted(state);
			},
			.timeoutDetails = [=] {
				return ToastContentDetails(state->never);
			},
		});

		runner->add({
			.name = u"toast-content self-test: refusals keep the toast-root "
				"contract"_q,
			.run = [=] {
				const auto window = Core::App().activePrimaryWindow();
				const auto top = window ? window->widget().get() : nullptr;
				const auto before = FailureCount();
				const auto missing = ReadToastContent(nullptr);
				const auto owner = ReadToastContent(top);
				const auto after = FailureCount();
				Check(
					(missing.state == ToastContentState::Refused)
						&& !missing.qualified()
						&& missing.image.isNull()
						&& missing.reason.contains(u"no widget was handed"_q),
					u"a null target is refused by name with no grab"_q,
					ToastContentDetails(missing));
				Check(
					top
						&& (owner.state == ToastContentState::Refused)
						&& !owner.qualified()
						&& owner.image.isNull()
						&& owner.reason.contains(WidgetDescription(top)),
					u"an ancestor that paints the product is refused naming "
					"the handed widget, and is never grabbed in place of the "
					"toast"_q,
					ToastContentDetails(owner));
				Check(
					after == before,
					u"none of these refusals logged a failure: a refusal is a "
					"returned value the caller decides about"_q,
					u"failures before=%1 after=%2"_q.arg(before).arg(after));
			},
		});
	});

	runner->addRecovery({
		.name = u"toast-content self-test: teardown"_q,
		.run = [=] {
			ReleaseWithheld(state);
			for (const auto &toast : state->toasts) {
				if (const auto instance = toast.instance.get()) {
					instance->hide();
				}
			}
			state->sentinel = nullptr;
			state->show = nullptr;
			if (state->built) {
				const auto live = LiveToastEntries();
				Check(
					live.isEmpty(),
					u"the toast-content self-test leaves no live toast "
					"behind: Instance::hide() took every toast it showed out "
					"of the walk"_q,
					u"liveToasts=[%1]"_q.arg(live.join(u"; "_q)));
			}
			Note(u"toast-content self-test: fixture released, built=%1 "
				"actions=%2"_q
				.arg(state->built ? 1 : 0)
				.arg(state->actions));
		},
	});
}

} // namespace Test

#endif // _DEBUG
