/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_palette.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_wall_paper.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "window/themes/window_theme.h"

#include "styles/palette.h"

namespace Test {
namespace {

// The shipped st::windowBg values the self-test expects: day from
// Telegram/lib_ui/ui/colors.palette, night from the colors.tdesktop-theme
// inside Telegram/Resources/night.tdesktop-theme (NightThemePath()).
constexpr auto kShippedDayWindowBg = qRgb(0xff, 0xff, 0xff);
constexpr auto kShippedNightWindowBg = qRgb(0x17, 0x21, 0x2b);

// The self-test's bare-toggle observation window, and the colour its
// nondiscriminating probe always answers.
constexpr auto kBareToggleObserveFor = crl::time(1000);
constexpr auto kConstantProbe = qRgb(0x12, 0x34, 0x56);

// A switch's obligation, or the self-test's entry baseline. Plain data:
// the module keeps every obligation until the process ends, so it holds
// no caller probe (a probe may capture widgets, which must not outlive
// QApplication in a static).
enum class ObligationKind {
	Switch,
	Baseline,
};

struct PaletteObligation {
	QString name;
	PaletteModes captured;
	QColor capturedSample;
	ObligationKind kind = ObligationKind::Switch;
	int restorations = 0;
	bool outstanding = false;
};

// The bounded settle every operation shares. |until| calls SettleStep,
// which records only colours and timestamps.
struct SampleSettle {
	Fn<QColor()> sample;
	QColor last;
	crl::time stableFor = 0;
	crl::time bound = 0;
	crl::time startedAt = 0;
	crl::time lastChange = 0;
	crl::time lastSampledAt = 0;
	int samples = 0;
	bool waiting = false;
};

// Everything one appended switch owns. |obligation| is created by the
// first setting the switch changes.
struct PaletteSwitchDriver {
	Fn<PaletteMode()> resolve;
	Fn<QColor()> sample;
	Fn<QString()> skipReason;
	std::shared_ptr<PaletteSwitch> reading;
	std::shared_ptr<PaletteObligation> obligation;
	SampleSettle settle;
	bool argumentsValid = false;
};

struct PaletteRestoreDriver {
	std::shared_ptr<PaletteSwitchDriver> switched;
	std::shared_ptr<PaletteRestore> reading;
	SampleSettle settle;
};

// What one RestoreObligation call did, in order. |outstanding| is the
// obligation's state when the call began; a call on a consumed obligation
// touches nothing.
struct RestoreSteps {
	PaletteModes before;
	PaletteModes after;
	PaletteRefusal refusal = PaletteRefusal::None;
	bool outstanding = false;
	bool followPaused = false;
	bool toggled = false;
	bool followRestored = false;
	bool saved = false;
};

struct CleanupReading {
	QString by;
	std::vector<QString> restored;
	std::size_t from = 0;
};

// The self-test's view of one cleanup call: the live state around it and
// what reacted while it ran.
struct CleanupObservation {
	CleanupReading reading;
	PaletteModes before;
	PaletteModes after;
	QColor windowBgBefore;
	QColor windowBgAfter;
	QString restorationsBefore;
	QString restorationsAfter;
	int themeUpdates = 0;
	int followChanges = 0;
};

// One self-test's state, shared by its stages.
struct SelfTestState {
	SampleSettle settle;
	PaletteModes entry;
	PaletteModes premise;
	PaletteModes bareBefore;
	PaletteModes bareSync;
	PaletteModes constantLive;
	PaletteModes unfinishedLive;
	QColor entrySample;
	QColor premiseSample;
	QColor bareSampleBefore;
	QColor bareSampleSync;
	QColor constantLiveSample;
	QColor unfinishedLiveSample;
	std::shared_ptr<PaletteObligation> baseline;
	std::shared_ptr<PaletteSwitchDriver> unfinished;
	std::shared_ptr<const PaletteSwitch> switched;
	std::shared_ptr<const PaletteRestore> restored;
	std::shared_ptr<const PaletteRestore> restoredAgain;
	std::shared_ptr<const PaletteSwitch> noop;
	std::shared_ptr<const PaletteSwitch> constant;
	std::shared_ptr<const PaletteRestore> constantRestored;
	std::shared_ptr<const PaletteSwitch> overlapping;
	CleanupObservation cleanup;
	CleanupObservation cleanupAgain;
	RestoreSteps entrySteps;
	std::size_t mark = 0;
	bool fixture = false;
	bool followEnabled = false;
	bool bareReverted = false;
};

using ObligationList = std::vector<std::shared_ptr<PaletteObligation>>;
using DriverList = std::vector<std::weak_ptr<PaletteSwitchDriver>>;

[[nodiscard]] ObligationList &Obligations() {
	static auto result = ObligationList();
	return result;
}

// Weak: the stages own the drivers. The list only maps a returned reading
// back to its driver for AppendPaletteRestore.
[[nodiscard]] DriverList &Drivers() {
	static auto result = DriverList();
	return result;
}

[[nodiscard]] bool &FinishRegistered() {
	static auto result = false;
	return result;
}

[[nodiscard]] QString FlagText(bool value) {
	return value ? u"1"_q : u"0"_q;
}

[[nodiscard]] QString ColorText(const QColor &color) {
	if (!color.isValid()) {
		return u"invalid"_q;
	}
	const auto hex = [](int channel) {
		return u"%1"_q.arg(channel, 2, 16, QChar('0'));
	};
	auto result = u"#"_q
		+ hex(color.red())
		+ hex(color.green())
		+ hex(color.blue());
	if (color.alpha() != 255) {
		result += hex(color.alpha());
	}
	return result;
}

[[nodiscard]] bool SameColor(const QColor &a, const QColor &b) {
	return (a.isValid() == b.isValid())
		&& (!a.isValid() || (a.rgba() == b.rgba()));
}

[[nodiscard]] QColor WindowBg() {
	return st::windowBg->c;
}

[[nodiscard]] QColor ShippedWindowBg(bool night) {
	return QColor::fromRgb(night
		? kShippedNightWindowBg
		: kShippedDayWindowBg);
}

[[nodiscard]] QString ShippedText() {
	return u"shipped windowBg: day %1 (Telegram/lib_ui/ui/colors.palette), "
		u"night %2 (colors.tdesktop-theme inside "
		u"Telegram/Resources/night.tdesktop-theme)"_q.arg(
			ColorText(ShippedWindowBg(false)),
			ColorText(ShippedWindowBg(true)));
}

[[nodiscard]] PaletteMode ModeOf(bool night) {
	return night ? PaletteMode::Night : PaletteMode::Day;
}

[[nodiscard]] PaletteMode OtherMode(const PaletteModes &modes) {
	return ModeOf(!modes.night);
}

[[nodiscard]] PaletteModes ReadPaletteModes() {
	const auto background = Window::Theme::Background();
	const auto &settings = Core::App().settings();
	return {
		.read = true,
		.started = !Data::details::IsUninitializedWallPaper(
			background->paper()),
		.editing = background->editingTheme().has_value(),
		.night = Window::Theme::IsNightMode(),
		.follow = settings.systemDarkModeEnabled(),
		.systemDark = settings.systemDarkMode(),
		.changeAllowed = background->nightModeChangeAllowed(),
	};
}

void StartSettle(
		SampleSettle &settle,
		Fn<QColor()> sample,
		crl::time stableFor,
		crl::time bound) {
	const auto now = crl::now();
	settle.sample = std::move(sample);
	settle.stableFor = stableFor;
	settle.bound = bound;
	settle.startedAt = now;
	settle.lastChange = now;
	settle.lastSampledAt = now;
	settle.last = settle.sample();
	settle.samples = 1;
	settle.waiting = true;
}

[[nodiscard]] bool SettleStep(SampleSettle &settle) {
	if (!settle.waiting) {
		return true;
	}
	const auto now = crl::now();
	const auto color = settle.sample();
	++settle.samples;
	settle.lastSampledAt = now;
	if (!SameColor(color, settle.last)) {
		settle.last = color;
		settle.lastChange = now;
	}
	return (now - settle.lastChange >= settle.stableFor)
		|| (now - settle.startedAt >= settle.bound);
}

[[nodiscard]] bool SettleStable(const SampleSettle &settle) {
	return settle.waiting
		&& (settle.lastSampledAt - settle.lastChange >= settle.stableFor);
}

[[nodiscard]] QString SettleText(const SampleSettle &settle) {
	return u"last=%1 settled=%2 stableFor=%3 bound=%4 waitedMs=%5 "
		u"stableMs=%6 samples=%7"_q.arg(
			ColorText(settle.last),
			FlagText(SettleStable(settle)),
			QString::number(settle.stableFor),
			QString::number(settle.bound),
			QString::number(settle.lastSampledAt - settle.startedAt),
			QString::number(settle.lastSampledAt - settle.lastChange),
			QString::number(settle.samples));
}

[[nodiscard]] const PaletteObligation *OutstandingSwitch() {
	for (const auto &obligation : Obligations()) {
		if (obligation->outstanding
			&& (obligation->kind == ObligationKind::Switch)) {
			return obligation.get();
		}
	}
	return nullptr;
}

[[nodiscard]] int OutstandingCount() {
	return int(ranges::count_if(Obligations(), [](const auto &obligation) {
		return obligation->outstanding;
	}));
}

[[nodiscard]] std::shared_ptr<PaletteSwitchDriver> FindDriver(
		const PaletteSwitch *reading) {
	if (!reading) {
		return nullptr;
	}
	for (const auto &weak : Drivers()) {
		if (const auto driver = weak.lock()) {
			if (driver->reading.get() == reading) {
				return driver;
			}
		}
	}
	return nullptr;
}

// The night mode and the follow flag against a capture: equal, or why not.
[[nodiscard]] PaletteRefusal ModesRefusal(
		const PaletteModes &captured,
		const PaletteModes &now) {
	if ((now.night == captured.night) && (now.follow == captured.follow)) {
		return PaletteRefusal::None;
	}
	return (now.systemDark != captured.systemDark)
		? PaletteRefusal::SystemModeChanged
		: PaletteRefusal::ModesDiffer;
}

[[nodiscard]] QString RestoreStepsText(const RestoreSteps &steps) {
	return u"outstanding=%1 followPaused=%2 toggled=%3 followRestored=%4 "
		u"saved=%5 before{%6} after{%7} refusal=%8"_q.arg(
			FlagText(steps.outstanding),
			FlagText(steps.followPaused),
			FlagText(steps.toggled),
			FlagText(steps.followRestored),
			FlagText(steps.saved),
			PaletteModesText(steps.before),
			PaletteModesText(steps.after),
			PaletteRefusalName(steps.refusal));
}

// The one synchronous restoration, shared by the restore stage, the
// Runner::onFinish cleanup and the self-test. An outstanding obligation is
// consumed by the attempt: night mode first, while following is paused if
// it prevents the change, then the follow flag (re-enabling it runs
// Application::checkSystemDarkMode() synchronously, so |after| is read
// after it), then one synchronous settings save. A consumed obligation is
// only read, never applied again.
[[nodiscard]] RestoreSteps RestoreObligation(PaletteObligation &obligation) {
	auto result = RestoreSteps{
		.before = ReadPaletteModes(),
		.outstanding = obligation.outstanding,
	};
	if (!obligation.outstanding) {
		result.after = result.before;
		return result;
	}
	if (!result.before.started || result.before.editing) {
		// Nothing is touched, and the obligation stays outstanding.
		result.after = result.before;
		result.refusal = !result.before.started
			? PaletteRefusal::NotStarted
			: PaletteRefusal::ThemeEditing;
		return result;
	}
	obligation.outstanding = false;
	++obligation.restorations;
	const auto &captured = obligation.captured;
	auto &settings = Core::App().settings();
	if (Window::Theme::IsNightMode() != captured.night) {
		if (!Window::Theme::Background()->nightModeChangeAllowed()) {
			settings.setSystemDarkModeEnabled(false);
			result.followPaused = true;
		}
		Window::Theme::ToggleNightMode();
		Window::Theme::KeepApplied();
		result.toggled = true;
	}
	if (settings.systemDarkModeEnabled() != captured.follow) {
		settings.setSystemDarkModeEnabled(captured.follow);
		result.followRestored = true;
	}
	if (result.followPaused || result.toggled || result.followRestored) {
		Core::App().saveSettings();
		result.saved = true;
	}
	result.after = ReadPaletteModes();
	result.refusal = ModesRefusal(captured, result.after);
	return result;
}

// Restores every outstanding obligation registered at |from| or later,
// newest first, and Notes one PALETTE_CLEANUP row. The readings in the
// row are immediate: the cleanup does not wait for the palette to settle.
CleanupReading CleanupObligations(const QString &by, std::size_t from) {
	auto result = CleanupReading{ .by = by, .from = from };
	auto entries = QStringList();
	const auto &list = Obligations();
	for (auto i = list.size(); i > from; --i) {
		const auto &obligation = list[i - 1];
		if (!obligation->outstanding) {
			continue;
		}
		const auto steps = RestoreObligation(*obligation);
		result.restored.push_back(obligation->name);
		entries.push_back(u"%1{captured{%2} sampleCaptured=%3 %4}"_q.arg(
			obligation->name,
			PaletteModesText(obligation->captured),
			ColorText(obligation->capturedSample),
			RestoreStepsText(steps)));
	}
	const auto head = u"PALETTE_CLEANUP: by=%1 from=%2 outstanding=%3 "
		u"windowBgNow=%4"_q.arg(
			by,
			QString::number(from),
			QString::number(entries.size()),
			ColorText(WindowBg()));
	Note(entries.isEmpty()
		? (head + u" - nothing to restore"_q)
		: (head
			+ u" restored=["_q
			+ entries.join(u"; "_q)
			+ u"] - immediate readings: the cleanup does not wait for the "
				u"palette to settle"_q));
	return result;
}

void RegisterFinishCleanup(not_null<Runner*> runner) {
	if (FinishRegistered()) {
		return;
	}
	FinishRegistered() = true;

	// One registration for the process, walking every obligation newest
	// first. A consumed obligation is skipped, so this is safe after an
	// explicit restore and after a teardown stage.
	runner->onFinish([] {
		CleanupObligations(u"onFinish"_q, 0);
	});
}

[[nodiscard]] std::shared_ptr<PaletteSwitchDriver> MakeSwitchDriver(
		PaletteSwitchArgs args,
		Fn<PaletteMode()> resolve) {
	const auto driver = std::make_shared<PaletteSwitchDriver>();
	const auto defaultProbe = !args.sample;
	driver->resolve = std::move(resolve);
	driver->skipReason = std::move(args.skipReason);
	driver->sample = defaultProbe
		? Fn<QColor()>(WindowBg)
		: std::move(args.sample);
	driver->argumentsValid = (args.stableFor > 0)
		&& (args.bound >= args.stableFor);
	driver->reading = std::make_shared<PaletteSwitch>();
	auto &reading = *driver->reading;
	reading.name = args.name;
	reading.sampleLabel = !args.sampleLabel.isEmpty()
		? args.sampleLabel
		: defaultProbe
		? u"windowBg"_q
		: u"probe"_q;
	reading.requested = args.mode;
	reading.stableFor = args.stableFor;
	reading.bound = args.bound;
	Drivers().push_back(driver);
	return driver;
}

// The route of ToggleNightModeWithConfirmation's confirmation, without the
// box. The obligation is registered, outstanding, before the first change.
void PerformSwitch(PaletteSwitchDriver &driver, bool night) {
	auto &reading = *driver.reading;
	const auto obligation = std::make_shared<PaletteObligation>(
		PaletteObligation{
			.name = reading.name,
			.captured = reading.before,
			.capturedSample = reading.sampleBefore,
			.kind = ObligationKind::Switch,
			.outstanding = true,
		});
	Obligations().push_back(obligation);
	driver.obligation = obligation;

	if (!Window::Theme::Background()->nightModeChangeAllowed()) {
		Core::App().settings().setSystemDarkModeEnabled(false);
		Core::App().saveSettingsDelayed();
		reading.followPaused = true;
	}
	Window::Theme::ToggleNightMode();
	Window::Theme::KeepApplied();
	reading.toggled = true;
	const auto now = Window::Theme::IsNightMode();
	reading.committed = (now == night);

	// A refused toggle with following untouched changed nothing.
	obligation->outstanding = reading.followPaused
		|| (now != reading.before.night);
}

void StartPaletteSwitch(PaletteSwitchDriver &driver) {
	auto &reading = *driver.reading;
	reading.requested = driver.resolve();
	reading.before = ReadPaletteModes();
	if (!driver.argumentsValid) {
		reading.refusal = PaletteRefusal::InvalidArguments;
		reading.reason = u"stableFor=%1 bound=%2: stableFor must be "
			u"positive and bound at least stableFor; nothing was "
			u"touched"_q.arg(
				QString::number(reading.stableFor),
				QString::number(reading.bound));
		return;
	}
	reading.sampleBefore = driver.sample();
	if (!reading.before.started) {
		reading.refusal = PaletteRefusal::NotStarted;
		reading.reason = u"ChatBackground has not started (its paper is "
			u"still uninitialized), so ToggleNightMode() would return "
			u"without a change; nothing was touched"_q;
		return;
	} else if (reading.before.editing) {
		reading.refusal = PaletteRefusal::ThemeEditing;
		reading.reason = u"the theme editor is open, and the product "
			u"refuses a night-mode switch then; nothing was touched"_q;
		return;
	} else if (const auto other = OutstandingSwitch()) {
		reading.refusal = PaletteRefusal::AlreadySwitched;
		reading.reason = u"switch \"%1\" changed the theme and was not "
			u"restored yet; restore it first. Nothing was touched"_q.arg(
				other->name);
		return;
	} else if (!reading.sampleBefore.isValid()) {
		reading.refusal = PaletteRefusal::InvalidSample;
		reading.reason = u"the probe answered an invalid colour; nothing "
			u"was touched"_q;
		return;
	}
	const auto night = (reading.requested == PaletteMode::Night);
	reading.needed = (reading.before.night != night);
	if (reading.needed) {
		PerformSwitch(driver, night);
	}
	StartSettle(
		driver.settle,
		driver.sample,
		reading.stableFor,
		reading.bound);
}

void FinishPaletteSwitch(PaletteSwitchDriver &driver) {
	auto &reading = *driver.reading;
	const auto &settle = driver.settle;
	if (settle.waiting) {
		reading.settled = SettleStable(settle);
		reading.waitedMs = settle.lastSampledAt - settle.startedAt;
		reading.samples = settle.samples;
		reading.sampleAfter = settle.last;
	} else if (driver.argumentsValid) {
		reading.sampleAfter = driver.sample();
	}
	reading.after = ReadPaletteModes();
	if (reading.refusal == PaletteRefusal::None) {
		const auto night = (reading.requested == PaletteMode::Night);
		if (!reading.settled) {
			reading.refusal = PaletteRefusal::NotSettled;
			reading.reason = u"the probe was still changing %1 ms after "
				u"the switch"_q.arg(QString::number(reading.waitedMs));
		} else if (!reading.sampleAfter.isValid()) {
			reading.refusal = PaletteRefusal::InvalidSample;
			reading.reason = u"the settled probe answered an invalid "
				u"colour"_q;
		} else if (reading.after.night != night) {
			reading.refusal = PaletteRefusal::RouteRefused;
			reading.reason = reading.needed
				? u"ToggleNightMode() and KeepApplied() left "
					u"IsNightMode()=%1 (committed right after the keep: "
					u"%2)"_q.arg(
						FlagText(reading.after.night),
						FlagText(reading.committed))
				: u"the night mode moved to %1 while settling"_q.arg(
					FlagText(reading.after.night));
		} else if (reading.needed
			&& SameColor(reading.sampleAfter, reading.sampleBefore)) {
			reading.refusal = PaletteRefusal::SampleUnchanged;
			reading.reason = u"the requested mode was committed, but the "
				u"settled %1 still reads %2, its value before the switch: "
				u"the probe cannot tell the two palettes apart"_q.arg(
					reading.sampleLabel,
					ColorText(reading.sampleAfter));
		} else {
			reading.applied = reading.needed;
		}
	}
	reading.finished = true;
	Note(PaletteSwitchText(reading));
}

[[nodiscard]] std::shared_ptr<const PaletteSwitch> AppendSwitchStage(
		not_null<Runner*> runner,
		std::shared_ptr<PaletteSwitchDriver> driver) {
	RegisterFinishCleanup(runner);
	runner->add({
		.name = u"palette switch: %1"_q.arg(driver->reading->name),
		.skipReason = driver->skipReason,
		.run = [=] {
			StartPaletteSwitch(*driver);
		},
		.until = [=] {
			return SettleStep(driver->settle);
		},
		.then = [=] {
			FinishPaletteSwitch(*driver);
		},
		.timeout = std::max(driver->reading->bound, crl::time())
			+ kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return PaletteSwitchText(*driver->reading);
		},
	});
	return driver->reading;
}

void StartPaletteRestore(PaletteRestoreDriver &driver) {
	auto &reading = *driver.reading;
	reading.before = ReadPaletteModes();
	const auto source = driver.switched;
	if (!source) {
		reading.refusal = PaletteRefusal::InvalidArguments;
		reading.reason = u"the switch reading was not returned by "
			u"Test::AppendPaletteSwitch; nothing was touched"_q;
		return;
	}
	const auto &switched = *source->reading;
	reading.captured = switched.before;
	reading.sampleCaptured = switched.sampleBefore;
	if (!switched.before.read || !switched.sampleBefore.isValid()) {
		reading.refusal = PaletteRefusal::NothingCaptured;
		reading.reason = u"the switch captured no starting state "
			u"(finished=%1 refusal=%2); nothing was touched"_q.arg(
				FlagText(switched.finished),
				PaletteRefusalName(switched.refusal));
		return;
	}
	reading.sampleBefore = source->sample();
	if (const auto obligation = source->obligation.get()) {
		const auto steps = RestoreObligation(*obligation);
		reading.before = steps.before;
		reading.outstanding = steps.outstanding;
		reading.followPaused = steps.followPaused;
		reading.toggled = steps.toggled;
		reading.followRestored = steps.followRestored;
		reading.saved = steps.saved;
		if ((steps.refusal == PaletteRefusal::NotStarted)
			|| (steps.refusal == PaletteRefusal::ThemeEditing)) {
			reading.refusal = steps.refusal;
			reading.reason = u"the obligation stays outstanding; nothing "
				u"was touched"_q;
			return;
		}
	}
	StartSettle(
		driver.settle,
		source->sample,
		switched.stableFor,
		switched.bound);
}

void FinishPaletteRestore(PaletteRestoreDriver &driver) {
	auto &reading = *driver.reading;
	const auto &settle = driver.settle;
	if (settle.waiting) {
		reading.settled = SettleStable(settle);
		reading.waitedMs = settle.lastSampledAt - settle.startedAt;
		reading.samples = settle.samples;
		reading.sampleAfter = settle.last;
	} else if (driver.switched) {
		reading.sampleAfter = driver.switched->sample();
	}
	reading.after = ReadPaletteModes();
	const auto modes = ModesRefusal(reading.captured, reading.after);
	const auto sampleEqual = reading.sampleAfter.isValid()
		&& SameColor(reading.sampleAfter, reading.sampleCaptured);
	reading.equal = reading.captured.read
		&& (modes == PaletteRefusal::None)
		&& sampleEqual;
	if (reading.refusal == PaletteRefusal::None) {
		if (!reading.settled) {
			reading.refusal = PaletteRefusal::NotSettled;
			reading.reason = u"the probe was still changing %1 ms after "
				u"the restore"_q.arg(QString::number(reading.waitedMs));
		} else if (!reading.sampleAfter.isValid()) {
			reading.refusal = PaletteRefusal::InvalidSample;
			reading.reason = u"the settled probe answered an invalid "
				u"colour"_q;
		} else if (modes != PaletteRefusal::None) {
			reading.refusal = modes;
			reading.reason = u"captured night=%1 follow=%2, now night=%3 "
				u"follow=%4; the reported system mode is reported, never "
				u"fought"_q.arg(
					FlagText(reading.captured.night),
					FlagText(reading.captured.follow),
					FlagText(reading.after.night),
					FlagText(reading.after.follow));
		} else if (!sampleEqual) {
			reading.refusal = PaletteRefusal::SampleDiffers;
			reading.reason = u"the settled %1 reads %2, captured %3"_q.arg(
				reading.sampleLabel,
				ColorText(reading.sampleAfter),
				ColorText(reading.sampleCaptured));
		}
	}
	reading.finished = true;
	Note(PaletteRestoreText(reading));
}

// "<name>=<restorations><*outstanding>" for every obligation from |from|.
[[nodiscard]] QString RestorationsText(std::size_t from) {
	auto result = QStringList();
	const auto &list = Obligations();
	for (auto i = from; i < list.size(); ++i) {
		const auto &obligation = list[i];
		result.push_back(u"%1=%2%3"_q.arg(
			obligation->name,
			QString::number(obligation->restorations),
			obligation->outstanding ? u"*"_q : QString()));
	}
	return result.join(u", "_q);
}

// The cleanup the finish callback runs, observed from outside: every
// ChatBackground update and every follow-flag change while it runs.
[[nodiscard]] CleanupObservation ObserveCleanup(std::size_t from) {
	auto result = CleanupObservation();
	result.before = ReadPaletteModes();
	result.windowBgBefore = WindowBg();
	result.restorationsBefore = RestorationsText(from);
	auto lifetime = rpl::lifetime();
	Window::Theme::Background()->updates(
	) | rpl::on_next([&](const Window::Theme::BackgroundUpdate &) {
		++result.themeUpdates;
	}, lifetime);
	Core::App().settings().systemDarkModeEnabledChanges(
	) | rpl::on_next([&](bool) {
		++result.followChanges;
	}, lifetime);
	result.reading = CleanupObligations(u"self-test"_q, from);
	lifetime.destroy();
	result.after = ReadPaletteModes();
	result.windowBgAfter = WindowBg();
	result.restorationsAfter = RestorationsText(from);
	return result;
}

[[nodiscard]] QString CleanupObservationText(
		const CleanupObservation &observation) {
	auto restored = QStringList();
	for (const auto &name : observation.reading.restored) {
		restored.push_back(name);
	}
	return u"restored=[%1] themeUpdates=%2 followChanges=%3 before{%4} "
		u"after{%5} windowBg %6 -> %7 restorations [%8] -> [%9]"_q.arg(
			restored.join(u", "_q),
			QString::number(observation.themeUpdates),
			QString::number(observation.followChanges),
			PaletteModesText(observation.before),
			PaletteModesText(observation.after),
			ColorText(observation.windowBgBefore),
			ColorText(observation.windowBgAfter),
			observation.restorationsBefore,
			observation.restorationsAfter);
}

void StartEntry(SelfTestState &state) {
	state.entry = ReadPaletteModes();
	state.entrySample = WindowBg();
	const auto outstanding = OutstandingSwitch();
	state.fixture = state.entry.started
		&& !state.entry.editing
		&& state.entrySample.isValid()
		&& !outstanding;
	Check(
		state.fixture,
		u"fixture gate: the palette self-test starts with a started "
		u"theme, no theme editing, a valid windowBg and no outstanding "
		u"palette switch"_q,
		u"entry{%1} windowBg=%2 outstandingSwitch=%3"_q.arg(
			PaletteModesText(state.entry),
			ColorText(state.entrySample),
			outstanding ? outstanding->name : u"none"_q));
	if (!state.fixture) {
		return;
	}
	state.baseline = std::make_shared<PaletteObligation>(PaletteObligation{
		.name = u"self-test entry"_q,
		.captured = state.entry,
		.capturedSample = state.entrySample,
		.kind = ObligationKind::Baseline,
	});
	Obligations().push_back(state.baseline);
	state.mark = Obligations().size();
	if (!state.entry.follow) {
		// Outstanding before the change, so a run that dies from here on
		// still gets following turned off again by the finish cleanup.
		state.baseline->outstanding = true;
		Core::App().settings().setSystemDarkModeEnabled(true);
		state.followEnabled = true;
	}
	Note(u"PALETTE_SELFTEST: entry{%1} windowBg=%2 followEnabledByTest=%3 "
		u"now{%4} windowBgNow=%5; %6"_q.arg(
			PaletteModesText(state.entry),
			ColorText(state.entrySample),
			FlagText(state.followEnabled),
			PaletteModesText(ReadPaletteModes()),
			ColorText(WindowBg()),
			ShippedText()));
	StartSettle(
		state.settle,
		WindowBg,
		kPaletteStableFor,
		kPaletteSettleBound);
}

void CheckPremise(SelfTestState &state) {
	if (!state.fixture) {
		return;
	}
	state.premise = ReadPaletteModes();
	state.premiseSample = state.settle.last;
	const auto settled = SettleStable(state.settle);
	const auto expected = ShippedWindowBg(state.premise.night);
	const auto details = u"premise{%1} windowBg=%2 expected=%3 (%4) "
		u"entry{%5} followEnabledByTest=%6 settle{%7}"_q.arg(
			PaletteModesText(state.premise),
			ColorText(state.premiseSample),
			ColorText(expected),
			PaletteModeName(ModeOf(state.premise.night)),
			PaletteModesText(state.entry),
			FlagText(state.followEnabled),
			SettleText(state.settle));
	Check(
		state.premise.follow,
		u"palette self-test: premise: the theme follows the system dark "
		u"mode"_q,
		details);
	Check(
		state.premise.systemDark.has_value(),
		u"palette self-test: premise: the platform reports a system mode"_q,
		state.premise.systemDark.has_value()
			? details
			: (details
				+ u" - the platform reports no system mode, so the "
				u"refusal premise cannot be reached on this host"_q));
	Check(
		!state.premise.changeAllowed,
		u"palette self-test: premise: "
		u"ChatBackground::nightModeChangeAllowed() reads false"_q,
		details);
	Check(
		settled && SameColor(state.premiseSample, expected),
		u"palette self-test: premise: windowBg settled at the shipped "
		u"value of the current mode"_q,
		details + u"; "_q + ShippedText());
}

[[nodiscard]] QString BareToggleSkip(const SelfTestState &state) {
	if (!state.fixture) {
		return u"the palette self-test fixture gate failed"_q;
	} else if (Window::Theme::Background()->nightModeChangeAllowed()) {
		return u"nightModeChangeAllowed()=1: a bare ToggleNightMode() "
			u"would apply, so the refusal premise was not reached (see the "
			u"premise checks)"_q;
	}
	return QString();
}

void StartBareToggle(SelfTestState &state) {
	state.bareBefore = ReadPaletteModes();
	state.bareSampleBefore = WindowBg();
	Window::Theme::ToggleNightMode();
	state.bareSync = ReadPaletteModes();
	state.bareSampleSync = WindowBg();
	if (!SameColor(state.bareSampleSync, state.bareSampleBefore)) {
		// The toggle applied after all: drop the un-kept testing theme
		// (and its auto-reverting warning) rather than leave it.
		Window::Theme::Revert();
		state.bareReverted = true;
	}
	StartSettle(
		state.settle,
		WindowBg,
		kBareToggleObserveFor,
		kPaletteSettleBound);
}

void CheckBareToggle(const SelfTestState &state) {
	const auto now = ReadPaletteModes();
	const auto details = u"before{%1} windowBg=%2; same turn{%3} "
		u"windowBg=%4; settled{%5} settle{%6} reverted=%7"_q.arg(
			PaletteModesText(state.bareBefore),
			ColorText(state.bareSampleBefore),
			PaletteModesText(state.bareSync),
			ColorText(state.bareSampleSync),
			PaletteModesText(now),
			SettleText(state.settle),
			FlagText(state.bareReverted));
	Check(
		SameColor(state.bareSampleSync, state.bareSampleBefore)
			&& (state.bareSync.night == state.bareBefore.night),
		u"palette self-test: premise: a bare ToggleNightMode() leaves "
		u"windowBg and the night mode unchanged in the same turn"_q,
		details);
	Check(
		SettleStable(state.settle)
			&& SameColor(state.settle.last, state.bareSampleBefore)
			&& (now.night == state.bareBefore.night)
			&& !state.bareReverted,
		u"palette self-test: premise: windowBg and the night mode stay "
		u"unchanged over a settled interval after the bare toggle"_q,
		details);
}

void CheckSwitch(const SelfTestState &state) {
	const auto &reading = *state.switched;
	const auto night = (reading.requested == PaletteMode::Night);
	const auto expected = ShippedWindowBg(night);
	const auto live = WindowBg();
	Check(
		reading.succeeded()
			&& reading.needed
			&& reading.toggled
			&& reading.committed
			&& reading.applied
			&& (reading.requested == OtherMode(state.premise))
			&& (reading.before == state.premise)
			&& reading.before.follow
			&& !reading.before.changeAllowed
			&& reading.followPaused
			&& !reading.after.follow
			&& (reading.after.night == night),
		u"palette self-test: switch: under the premise the helper "
		u"switched to the other palette, pausing following"_q,
		PaletteSwitchText(reading));
	Check(
		SameColor(reading.sampleAfter, expected)
			&& SameColor(live, expected)
			&& !SameColor(reading.sampleAfter, state.premiseSample),
		u"palette self-test: switch: the settled windowBg reads the other "
		u"palette's shipped value"_q,
		u"requested=%1 expected=%2 reading=%3 live=%4 premise=%5; %6"_q.arg(
			PaletteModeName(reading.requested),
			ColorText(expected),
			ColorText(reading.sampleAfter),
			ColorText(live),
			ColorText(state.premiseSample),
			ShippedText()));
}

void CheckRestore(const SelfTestState &state) {
	const auto &first = *state.restored;
	const auto &again = *state.restoredAgain;
	const auto &switched = *state.switched;
	const auto live = WindowBg();
	const auto now = ReadPaletteModes();
	Check(
		first.restored()
			&& first.outstanding
			&& first.toggled
			&& first.saved
			&& (first.followRestored == switched.followPaused)
			&& (first.after.night == state.premise.night)
			&& (first.after.follow == state.premise.follow)
			&& SameColor(first.sampleAfter, state.premiseSample)
			&& SameColor(live, ShippedWindowBg(state.premise.night))
			&& (now.night == state.premise.night)
			&& (now.follow == state.premise.follow),
		u"palette self-test: restore: windowBg, the night mode and "
		u"following are back to the switch's starting values"_q,
		u"%1; live{%2} windowBg=%3 premise{%4} windowBg=%5"_q.arg(
			PaletteRestoreText(first),
			PaletteModesText(now),
			ColorText(live),
			PaletteModesText(state.premise),
			ColorText(state.premiseSample)));
	Check(
		again.restored()
			&& !again.outstanding
			&& !again.followPaused
			&& !again.toggled
			&& !again.followRestored
			&& !again.saved,
		u"palette self-test: restore: a second restore of the same switch "
		u"only verifies"_q,
		PaletteRestoreText(again));
}

void CheckNoOp(const SelfTestState &state) {
	const auto &reading = *state.noop;
	Check(
		reading.succeeded()
			&& !reading.needed
			&& !reading.applied
			&& !reading.followPaused
			&& !reading.toggled
			&& reading.settled
			&& reading.sampleAfter.isValid()
			&& SameColor(reading.sampleAfter, reading.sampleBefore)
			&& (reading.before == reading.after)
			&& !OutstandingSwitch(),
		u"palette self-test: no-op: a request for the palette already "
		u"committed touches nothing and succeeds with a settled sample"_q,
		PaletteSwitchText(reading));
}

void CheckConstantSwitch(SelfTestState &state) {
	state.constantLive = ReadPaletteModes();
	state.constantLiveSample = WindowBg();
	const auto &reading = *state.constant;
	const auto night = (reading.requested == PaletteMode::Night);
	Check(
		reading.finished
			&& (reading.refusal == PaletteRefusal::SampleUnchanged)
			&& reading.needed
			&& reading.toggled
			&& reading.committed
			&& reading.settled
			&& !reading.applied
			&& !reading.succeeded(),
		u"palette self-test: constant probe: a needed switch measured by a "
		u"constant colour is refused sample-unchanged and never applied"_q,
		PaletteSwitchText(reading));
	Check(
		SameColor(state.constantLiveSample, ShippedWindowBg(night))
			&& (state.constantLive.night == night),
		u"palette self-test: constant probe: an independent live windowBg "
		u"shows the palette did switch"_q,
		u"requested=%1 live{%2} windowBg=%3 expected=%4"_q.arg(
			PaletteModeName(reading.requested),
			PaletteModesText(state.constantLive),
			ColorText(state.constantLiveSample),
			ColorText(ShippedWindowBg(night))));
}

void CheckConstantRestore(const SelfTestState &state) {
	const auto &reading = *state.constantRestored;
	const auto live = WindowBg();
	const auto now = ReadPaletteModes();
	Check(
		reading.restored()
			&& reading.outstanding
			&& reading.toggled
			&& (now.night == state.premise.night)
			&& (now.follow == state.premise.follow)
			&& SameColor(live, state.premiseSample)
			&& !SameColor(state.constantLiveSample, state.premiseSample),
		u"palette self-test: constant probe: its restore brings the live "
		u"windowBg, the night mode and following back"_q,
		u"%1; live{%2} windowBg=%3 premise{%4} windowBg=%5"_q.arg(
			PaletteRestoreText(reading),
			PaletteModesText(now),
			ColorText(live),
			PaletteModesText(state.premise),
			ColorText(state.premiseSample)));
}

void StartUnfinished(SelfTestState &state) {
	const auto mode = OtherMode(state.premise);
	state.unfinished = MakeSwitchDriver(
		{ .name = u"self-test unfinished"_q },
		[=] { return mode; });

	// Only the run step: the reading is never finished, as when a stage
	// times out or the watchdog fires after the switch changed settings.
	StartPaletteSwitch(*state.unfinished);
	StartSettle(
		state.settle,
		WindowBg,
		kPaletteStableFor,
		kPaletteSettleBound);
}

void CheckUnfinished(SelfTestState &state) {
	state.unfinishedLive = ReadPaletteModes();
	state.unfinishedLiveSample = state.settle.last;
	const auto &driver = *state.unfinished;
	const auto &reading = *driver.reading;
	const auto night = (reading.requested == PaletteMode::Night);
	Check(
		driver.obligation
			&& driver.obligation->outstanding
			&& reading.toggled
			&& reading.committed
			&& !reading.finished
			&& SettleStable(state.settle)
			&& SameColor(state.unfinishedLiveSample, ShippedWindowBg(night))
			&& (state.unfinishedLive.night == night),
		u"palette self-test: unfinished switch: the switch changed the "
		u"palette and holds an outstanding obligation before its reading "
		u"finished"_q,
		u"%1; live{%2} windowBg=%3 expected=%4 settle{%5}"_q.arg(
			PaletteSwitchText(reading),
			PaletteModesText(state.unfinishedLive),
			ColorText(state.unfinishedLiveSample),
			ColorText(ShippedWindowBg(night)),
			SettleText(state.settle)));
}

void CheckOverlapping(const SelfTestState &state) {
	const auto &reading = *state.overlapping;
	const auto now = ReadPaletteModes();
	const auto live = WindowBg();
	Check(
		reading.finished
			&& (reading.refusal == PaletteRefusal::AlreadySwitched)
			&& !reading.needed
			&& !reading.toggled
			&& !reading.followPaused
			&& (reading.before == state.unfinishedLive)
			&& (reading.after == state.unfinishedLive)
			&& (now == state.unfinishedLive)
			&& SameColor(reading.sampleBefore, state.unfinishedLiveSample)
			&& SameColor(reading.sampleAfter, state.unfinishedLiveSample)
			&& SameColor(live, state.unfinishedLiveSample),
		u"palette self-test: a second switch while one is outstanding is "
		u"refused already-switched and changes nothing"_q,
		u"%1; live{%2} windowBg=%3"_q.arg(
			PaletteSwitchText(reading),
			PaletteModesText(now),
			ColorText(live)));
}

void StartCleanup(SelfTestState &state) {
	state.cleanup = ObserveCleanup(state.mark);
	StartSettle(
		state.settle,
		WindowBg,
		kPaletteStableFor,
		kPaletteSettleBound);
}

void CheckCleanup(const SelfTestState &state) {
	const auto &cleanup = state.cleanup;
	const auto &unfinished = *state.unfinished;
	const auto obligation = unfinished.obligation.get();
	const auto switched = FindDriver(state.switched.get());
	const auto constant = FindDriver(state.constant.get());
	const auto now = ReadPaletteModes();
	const auto restoredOnlyUnfinished = (cleanup.reading.restored
		== std::vector<QString>{ unfinished.reading->name });
	const auto details = u"%1; now{%2} settle{%3}"_q.arg(
		CleanupObservationText(cleanup),
		PaletteModesText(now),
		SettleText(state.settle));
	Check(
		obligation
			&& !obligation->outstanding
			&& (obligation->restorations == 1)
			&& restoredOnlyUnfinished
			&& !unfinished.reading->finished
			&& (cleanup.themeUpdates > 0)
			&& (now.night == obligation->captured.night)
			&& (now.follow == obligation->captured.follow)
			&& SettleStable(state.settle)
			&& SameColor(state.settle.last, obligation->capturedSample),
		u"palette self-test: cleanup: the finish cleanup restores a switch "
		u"whose reading never finished"_q,
		details);
	const auto once = [](const std::shared_ptr<PaletteSwitchDriver> &d) {
		return d
			&& d->obligation
			&& !d->obligation->outstanding
			&& (d->obligation->restorations == 1);
	};
	Check(
		once(switched) && once(constant) && restoredOnlyUnfinished,
		u"palette self-test: cleanup: switches restored explicitly are not "
		u"restored again"_q,
		details);
}

void CheckSecondCleanup(SelfTestState &state) {
	state.cleanupAgain = ObserveCleanup(state.mark);
	const auto &again = state.cleanupAgain;
	Check(
		again.reading.restored.empty()
			&& (again.themeUpdates == 0)
			&& (again.followChanges == 0)
			&& (again.before == again.after)
			&& SameColor(again.windowBgBefore, again.windowBgAfter)
			&& (again.restorationsBefore == again.restorationsAfter),
		u"palette self-test: cleanup: a second cleanup changes nothing"_q,
		CleanupObservationText(again));
}

void StartEntryRestore(SelfTestState &state) {
	state.entrySteps = RestoreObligation(*state.baseline);
	Note(u"PALETTE_SELFTEST: entry restore %1"_q.arg(
		RestoreStepsText(state.entrySteps)));
	StartSettle(
		state.settle,
		WindowBg,
		kPaletteStableFor,
		kPaletteSettleBound);
}

void CheckEntryRestored(const SelfTestState &state) {
	const auto now = ReadPaletteModes();
	const auto outstanding = OutstandingCount();
	Check(
		SettleStable(state.settle)
			&& SameColor(state.settle.last, state.entrySample)
			&& (now.night == state.entry.night)
			&& (now.follow == state.entry.follow)
			&& (state.entrySteps.refusal == PaletteRefusal::None)
			&& !state.baseline->outstanding
			&& (outstanding == 0),
		u"palette self-test: the palette, the night mode and following are "
		u"left as the self-test found them"_q,
		u"entry{%1} windowBg=%2; now{%3} settle{%4} steps{%5} "
		u"outstandingObligations=%6"_q.arg(
			PaletteModesText(state.entry),
			ColorText(state.entrySample),
			PaletteModesText(now),
			SettleText(state.settle),
			RestoreStepsText(state.entrySteps),
			QString::number(outstanding)));
}

} // namespace

QString PaletteModeName(PaletteMode mode) {
	switch (mode) {
	case PaletteMode::Day: return u"day"_q;
	case PaletteMode::Night: return u"night"_q;
	}
	Unexpected("Mode in Test::PaletteModeName.");
}

QString PaletteModesText(const PaletteModes &modes) {
	if (!modes.read) {
		return u"unread"_q;
	}
	const auto system = !modes.systemDark
		? u"none"_q
		: *modes.systemDark
		? u"dark"_q
		: u"light"_q;
	return u"night=%1 follow=%2 system=%3 changeAllowed=%4 started=%5 "
		u"editing=%6"_q.arg(
			FlagText(modes.night),
			FlagText(modes.follow),
			system,
			FlagText(modes.changeAllowed),
			FlagText(modes.started),
			FlagText(modes.editing));
}

QString PaletteRefusalName(PaletteRefusal refusal) {
	switch (refusal) {
	case PaletteRefusal::None: return u"none"_q;
	case PaletteRefusal::InvalidArguments: return u"invalid-arguments"_q;
	case PaletteRefusal::NotStarted: return u"not-started"_q;
	case PaletteRefusal::ThemeEditing: return u"theme-editing"_q;
	case PaletteRefusal::AlreadySwitched: return u"already-switched"_q;
	case PaletteRefusal::InvalidSample: return u"invalid-sample"_q;
	case PaletteRefusal::RouteRefused: return u"route-refused"_q;
	case PaletteRefusal::NotSettled: return u"not-settled"_q;
	case PaletteRefusal::SampleUnchanged: return u"sample-unchanged"_q;
	case PaletteRefusal::NothingCaptured: return u"nothing-captured"_q;
	case PaletteRefusal::SystemModeChanged:
		return u"system-mode-changed"_q;
	case PaletteRefusal::ModesDiffer: return u"modes-differ"_q;
	case PaletteRefusal::SampleDiffers: return u"sample-differs"_q;
	}
	Unexpected("Refusal in Test::PaletteRefusalName.");
}

QString PaletteSwitchText(const PaletteSwitch &reading) {
	const auto line = u"PALETTE_SWITCH: name=%1 finished=%2 requested=%3 "
		u"needed=%4 followPaused=%5 toggled=%6 committed=%7 applied=%8 "
		u"probe=%9 before{%10} after{%11} sampleBefore=%12 sampleAfter=%13 "
		u"settled=%14 stableFor=%15 bound=%16 waitedMs=%17 samples=%18 "
		u"refusal=%19"_q.arg(
			reading.name,
			FlagText(reading.finished),
			PaletteModeName(reading.requested),
			FlagText(reading.needed),
			FlagText(reading.followPaused),
			FlagText(reading.toggled),
			FlagText(reading.committed),
			FlagText(reading.applied),
			reading.sampleLabel,
			PaletteModesText(reading.before),
			PaletteModesText(reading.after),
			ColorText(reading.sampleBefore),
			ColorText(reading.sampleAfter),
			FlagText(reading.settled),
			QString::number(reading.stableFor),
			QString::number(reading.bound),
			QString::number(reading.waitedMs),
			QString::number(reading.samples),
			PaletteRefusalName(reading.refusal));
	return reading.reason.isEmpty()
		? line
		: (line + u" - "_q + reading.reason);
}

QString PaletteRestoreText(const PaletteRestore &reading) {
	const auto line = u"PALETTE_RESTORE: name=%1 switch=%2 finished=%3 "
		u"outstanding=%4 followPaused=%5 toggled=%6 followRestored=%7 "
		u"saved=%8 equal=%9 probe=%10 captured{%11} before{%12} "
		u"after{%13} sampleCaptured=%14 sampleBefore=%15 sampleAfter=%16 "
		u"settled=%17 stableFor=%18 bound=%19 waitedMs=%20 samples=%21 "
		u"refusal=%22"_q.arg(
			reading.name,
			reading.switchName,
			FlagText(reading.finished),
			FlagText(reading.outstanding),
			FlagText(reading.followPaused),
			FlagText(reading.toggled),
			FlagText(reading.followRestored),
			FlagText(reading.saved),
			FlagText(reading.equal),
			reading.sampleLabel,
			PaletteModesText(reading.captured),
			PaletteModesText(reading.before),
			PaletteModesText(reading.after),
			ColorText(reading.sampleCaptured),
			ColorText(reading.sampleBefore),
			ColorText(reading.sampleAfter),
			FlagText(reading.settled),
			QString::number(reading.stableFor),
			QString::number(reading.bound),
			QString::number(reading.waitedMs),
			QString::number(reading.samples),
			PaletteRefusalName(reading.refusal));
	return reading.reason.isEmpty()
		? line
		: (line + u" - "_q + reading.reason);
}

std::shared_ptr<const PaletteSwitch> AppendPaletteSwitch(
		not_null<Runner*> runner,
		PaletteSwitchArgs args) {
	const auto mode = args.mode;
	return AppendSwitchStage(
		runner,
		MakeSwitchDriver(std::move(args), [=] { return mode; }));
}

std::shared_ptr<const PaletteRestore> AppendPaletteRestore(
		not_null<Runner*> runner,
		std::shared_ptr<const PaletteSwitch> switched,
		QString name,
		Fn<QString()> skipReason) {
	RegisterFinishCleanup(runner);
	const auto driver = std::make_shared<PaletteRestoreDriver>();
	driver->switched = FindDriver(switched.get());
	driver->reading = std::make_shared<PaletteRestore>();
	auto &reading = *driver->reading;
	reading.switchName = switched ? switched->name : u"none"_q;
	reading.name = name.isEmpty() ? reading.switchName : name;
	if (const auto source = driver->switched.get()) {
		reading.sampleLabel = source->reading->sampleLabel;
		reading.stableFor = source->reading->stableFor;
		reading.bound = source->reading->bound;
	}
	runner->add({
		.name = u"palette restore: %1"_q.arg(reading.name),
		.skipReason = std::move(skipReason),
		.run = [=] {
			StartPaletteRestore(*driver);
		},
		.until = [=] {
			return SettleStep(driver->settle);
		},
		.then = [=] {
			FinishPaletteRestore(*driver);
		},
		.timeout = std::max(reading.bound, crl::time())
			+ kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return PaletteRestoreText(*driver->reading);
		},
	});
	return driver->reading;
}

void AppendPaletteSwitchSelfTest(not_null<Runner*> runner) {
	const auto state = std::make_shared<SelfTestState>();
	RegisterFinishCleanup(runner);
	const auto skip = [=] {
		return state->fixture
			? QString()
			: u"the palette self-test fixture gate failed"_q;
	};
	const auto settleTimeout = kPaletteSettleBound + kDefaultStageTimeout;
	const auto settleDetails = [=] {
		return SettleText(state->settle);
	};

	runner->add({
		.name = u"palette self-test: entry and premise"_q,
		.run = [=] {
			StartEntry(*state);
		},
		.until = [=] {
			return SettleStep(state->settle);
		},
		.then = [=] {
			CheckPremise(*state);
		},
		.timeout = settleTimeout,
		.timeoutDetails = settleDetails,
	});

	runner->add({
		.name = u"palette self-test: a bare ToggleNightMode() under the "
			u"premise"_q,
		.skipReason = [=] {
			return BareToggleSkip(*state);
		},
		.run = [=] {
			StartBareToggle(*state);
		},
		.until = [=] {
			return SettleStep(state->settle);
		},
		.then = [=] {
			CheckBareToggle(*state);
		},
		.timeout = settleTimeout,
		.timeoutDetails = settleDetails,
	});

	state->switched = AppendSwitchStage(runner, MakeSwitchDriver(
		{ .name = u"self-test switch"_q, .skipReason = skip },
		[=] { return OtherMode(state->premise); }));

	runner->add({
		.name = u"palette self-test: switch reading"_q,
		.skipReason = skip,
		.run = [=] {
			CheckSwitch(*state);
		},
	});

	state->restored = AppendPaletteRestore(
		runner,
		state->switched,
		QString(),
		skip);
	state->restoredAgain = AppendPaletteRestore(
		runner,
		state->switched,
		u"self-test switch, again"_q,
		skip);

	runner->add({
		.name = u"palette self-test: restore readings"_q,
		.skipReason = skip,
		.run = [=] {
			CheckRestore(*state);
		},
	});

	state->noop = AppendSwitchStage(runner, MakeSwitchDriver(
		{ .name = u"self-test already-requested"_q, .skipReason = skip },
		[] { return ModeOf(Window::Theme::IsNightMode()); }));

	runner->add({
		.name = u"palette self-test: no-op reading"_q,
		.skipReason = skip,
		.run = [=] {
			CheckNoOp(*state);
		},
	});

	state->constant = AppendSwitchStage(runner, MakeSwitchDriver(
		{
			.name = u"self-test constant probe"_q,
			.skipReason = skip,
			.sampleLabel = u"constant"_q,
			.sample = [] { return QColor::fromRgb(kConstantProbe); },
		},
		[=] { return OtherMode(state->premise); }));

	runner->add({
		.name = u"palette self-test: constant probe reading"_q,
		.skipReason = skip,
		.run = [=] {
			CheckConstantSwitch(*state);
		},
	});

	state->constantRestored = AppendPaletteRestore(
		runner,
		state->constant,
		QString(),
		skip);

	runner->add({
		.name = u"palette self-test: constant probe restore reading"_q,
		.skipReason = skip,
		.run = [=] {
			CheckConstantRestore(*state);
		},
	});

	runner->add({
		.name = u"palette self-test: an unfinished switch"_q,
		.skipReason = skip,
		.run = [=] {
			StartUnfinished(*state);
		},
		.until = [=] {
			return SettleStep(state->settle);
		},
		.then = [=] {
			CheckUnfinished(*state);
		},
		.timeout = settleTimeout,
		.timeoutDetails = settleDetails,
	});

	// Requests the premise mode, a needed switch back without the guard.
	state->overlapping = AppendSwitchStage(runner, MakeSwitchDriver(
		{ .name = u"self-test overlapping"_q, .skipReason = skip },
		[=] { return ModeOf(state->premise.night); }));

	runner->add({
		.name = u"palette self-test: overlapping switch reading"_q,
		.skipReason = skip,
		.run = [=] {
			CheckOverlapping(*state);
		},
	});

	runner->add({
		.name = u"palette self-test: the finish cleanup of an unfinished "
			u"switch"_q,
		.skipReason = skip,
		.run = [=] {
			StartCleanup(*state);
		},
		.until = [=] {
			return SettleStep(state->settle);
		},
		.then = [=] {
			CheckCleanup(*state);
		},
		.timeout = settleTimeout,
		.timeoutDetails = settleDetails,
	});

	runner->add({
		.name = u"palette self-test: a second cleanup"_q,
		.skipReason = skip,
		.run = [=] {
			CheckSecondCleanup(*state);
		},
	});

	runner->add({
		.name = u"palette self-test: back to the entry state"_q,
		.skipReason = skip,
		.run = [=] {
			StartEntryRestore(*state);
		},
		.until = [=] {
			return SettleStep(state->settle);
		},
		.then = [=] {
			CheckEntryRestored(*state);
		},
		.timeout = settleTimeout,
		.timeoutDetails = settleDetails,
	});
}

} // namespace Test

#endif // _DEBUG
