/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QString>
#include <QtGui/QColor>

#include <memory>
#include <optional>

namespace Test {

class Runner;

// A switch to the day or the night palette, and its restore, that work
// while the theme follows the system dark mode.
//
// Why it exists. A bare Window::Theme::ToggleNightMode() reaches
// ChatBackground::reapplyWithNightMode(), which returns with no change and
// no log whenever ChatBackground::nightModeChangeAllowed() reads false:
// while Core::Settings::systemDarkModeEnabled() holds and the platform
// reports a system mode (Core::Settings::systemDarkMode()), any change away
// from that mode is refused. Core::Settings starts following the system,
// so a fresh or golden account follows it. Two campaigns lost their
// "other palette" evidence this way and each rebuilt the same overlay-local
// repair:
// - 2026/10/12/recognize-tgw-widget-documents (work/test.md, Test 22 in
//   Runs 1-3; the Run 2 recovery-plan row "T22 palette"): Run 2 read
//   "T22 palette flip: night before=1 now=1" and every flipped frame was a
//   night frame; Run 3 paused following, toggled and kept, and restored
//   both afterwards;
// - 2026/10/08/paint-the-ton-connect-card-balance-like-the-money-card
//   (work/test.md, Run 2): "S0 fixture: the run starts in the day theme -
//   night=1"; the app followed the Windows dark mode, the toggle was
//   refused and every capture was night.
// And from the earlier wallet palette campaigns: IsNightMode() is not an
// oracle for the live palette (ToggleNightMode() applies the palette at
// once, but the night-mode value is committed only inside KeepApplied()),
// so the decisive field here is a live palette sample, never IsNightMode()
// alone.
//
// The route. The product's own confirmation route,
// Window::Theme::ToggleNightModeWithConfirmation(), without its box: when
// the change is not allowed, Core::App().settings()
// .setSystemDarkModeEnabled(false) and Core::App().saveSettingsDelayed();
// then Window::Theme::ToggleNightMode() and Window::Theme::KeepApplied(),
// as the main menu's night-mode toggle runs it. Following is paused ONLY
// when nightModeChangeAllowed() reads false, so a switch that the product
// allows keeps following on. The platform's reported mode is never
// synthesized and the OS appearance is never changed.
//
// Persisted side effects (this is not a byte-for-byte rollback of the
// portable data): KeepApplied() runs the keep reapplyWithNightMode() set
// up - Local::writeTheme() for the other mode's theme slot when it had
// none on disk yet (a first switch), ChatBackground::keepApplied()
// (Local::writeBackground() on a testing wallpaper) and
// Local::writeSettings() when the mode changed, which persists the
// system-follow flag as it is at that moment; pausing following arms
// saveSettingsDelayed(), as the product route does; a restore that changed
// anything ends with Core::App().saveSettings(), so the settings file ends
// with the restored night mode and follow flag.
//
// Readings. The helpers never Pass or Fail. Each operation is one stage
// that Notes one row (PALETTE_SWITCH / PALETTE_RESTORE) and fills a const
// reading the caller judges from a later stage. Everything is read when the
// stage runs, never when it is appended. Each reading carries the modes
// before and after (PaletteModes: night, system-follow, the reported
// system mode or none, nightModeChangeAllowed(), started, editing), the
// sample before and the settled sample after, the waited time and the
// sample count, and a named refusal.
//
// Settling. After the action the stage samples its probe on every runner
// tick and finishes once the sample has not changed for |stableFor|, or
// when |bound| passed since it began (refused not-settled). The palette is
// applied synchronously by the toggle (style::main_palette::apply or
// reset), so the wait mostly proves that nothing moves it afterwards: an
// accent colorizer, Application::checkSystemDarkMode() or another theme
// reaction. Test::StyleSettled is not used: it writes its own verdict and
// latches by name.
//
// The obligation. The first setting a switch changes registers a
// restoration obligation holding the modes and sample it captured. The
// module registers one Runner::onFinish callback (at the first append)
// that walks the outstanding obligations newest first and restores each
// with the same synchronous routine the restore stage uses, writing one
// PALETTE_CLEANUP row; an obligation that a restore already consumed is
// never restored again, so a later stage's palette is never overwritten
// with a stale baseline. That callback covers a stage timeout, the
// watchdog and skipped teardown stages. It cannot wait for the palette to
// settle (Runner::finish() quits kFinishDrainDelay later), and a palette
// change at quit starts the chat background re-cache that the
// d->allThreads.isEmpty() row of README.md's "Failure diagnosis" is about,
// so it is a backstop for an already failed run: restore in a stage.
// A second switch while a switch's obligation is outstanding is refused
// already-switched; nested palette transactions are not modelled.

enum class PaletteMode {
	Day,
	Night,
};

// "day" or "night".
[[nodiscard]] QString PaletteModeName(PaletteMode mode);

// How long the sample must stay unchanged, and how long a stage waits for
// that at most.
inline constexpr auto kPaletteStableFor = crl::time(400);
inline constexpr auto kPaletteSettleBound = crl::time(4000);

// One reading of the theme's modes. |night| is Window::Theme::IsNightMode()
// (the committed value), |follow| Core::Settings::systemDarkModeEnabled(),
// |systemDark| Core::Settings::systemDarkMode() (nullopt when the platform
// reports none), |changeAllowed| ChatBackground::nightModeChangeAllowed(),
// |started| whether ChatBackground::start() initialized its paper and
// |editing| ChatBackground::editingTheme(). |read| is false only on a
// default-constructed value.
struct PaletteModes {
	bool read = false;
	bool started = false;
	bool editing = false;
	bool night = false;
	bool follow = false;
	std::optional<bool> systemDark;
	bool changeAllowed = false;

	friend bool operator==(
		const PaletteModes &a,
		const PaletteModes &b) = default;
};

// "night=<0|1> follow=<0|1> system=<dark|light|none>
// changeAllowed=<0|1> started=<0|1> editing=<0|1>", or "unread".
[[nodiscard]] QString PaletteModesText(const PaletteModes &modes);

// The named refusals. Switch:
// - invalid-arguments: |stableFor| is not positive or |bound| is below it;
// - not-started: ChatBackground was not started, so the toggle would
//   return without a change;
// - theme-editing: the theme editor is open; the product refuses a
//   night-mode switch then (lng_theme_editor_cant_change_theme);
// - already-switched: another switch's obligation is outstanding; nothing
//   is touched;
// - invalid-sample: the probe answered an invalid QColor;
// - route-refused: after the route the committed night mode is not the
//   requested one (or it moved away while settling);
// - not-settled: the sample was still changing when |bound| passed;
// - sample-unchanged: a needed switch was committed, but the settled
//   sample equals the sample before it, so the probe cannot tell the two
//   palettes apart and "applied" cannot be claimed.
// Restore, besides invalid-arguments, not-started, theme-editing,
// invalid-sample and not-settled:
// - nothing-captured: the switch never captured a starting state (it was
//   skipped, or refused before sampling);
// - system-mode-changed: the night mode or the follow flag differs from
//   the captured one and the platform's reported mode moved since the
//   capture (re-enabling following made Application::checkSystemDarkMode()
//   switch to it);
// - modes-differ: the night mode or the follow flag differs from the
//   captured one with the reported mode unchanged;
// - sample-differs: both modes are back but the settled sample is not the
//   captured one.
enum class PaletteRefusal {
	None,
	InvalidArguments,
	NotStarted,
	ThemeEditing,
	AlreadySwitched,
	InvalidSample,
	RouteRefused,
	NotSettled,
	SampleUnchanged,
	NothingCaptured,
	SystemModeChanged,
	ModesDiffer,
	SampleDiffers,
};

// none, invalid-arguments, not-started, theme-editing, already-switched,
// invalid-sample, route-refused, not-settled, sample-unchanged,
// nothing-captured, system-mode-changed, modes-differ, sample-differs.
[[nodiscard]] QString PaletteRefusalName(PaletteRefusal refusal);

// |name| labels the stage ("palette switch: <name>") and the rows. |mode|
// is the requested palette. |sample| is the probe the switch AND its
// restore read (the restore takes no probe of its own); a null |sample|
// reads st::windowBg->c, labelled "windowBg", whose shipped values differ
// (day #ffffff, night #17212b). A caller probe must discriminate the two
// palettes it switches between: a probe that reads the same value in both
// is refused sample-unchanged, never certified.
struct PaletteSwitchArgs {
	QString name;
	PaletteMode mode = PaletteMode::Day;
	Fn<QString()> skipReason;
	QString sampleLabel;
	Fn<QColor()> sample;
	crl::time stableFor = kPaletteStableFor;
	crl::time bound = kPaletteSettleBound;
};

// The switch's reading. |name|, |sampleLabel|, |stableFor| and |bound| are
// filled when the stage is appended, the rest when it runs. |needed| is
// before.night != requested. |followPaused| means the switch turned
// following off because nightModeChangeAllowed() read false; |toggled|
// that it ran ToggleNightMode() and KeepApplied(); |committed| that
// IsNightMode() read the requested mode right after KeepApplied().
// |applied| is true only for a needed switch whose committed mode is the
// requested one AND whose settled sample differs from |sampleBefore|.
// A request for the palette already committed is a no-op: needed=0,
// applied=0, nothing touched, and still a valid settled sample, so it
// succeeds. |finished| stays false for a skipped stage.
struct PaletteSwitch {
	QString name;
	QString sampleLabel;
	QString reason;
	PaletteMode requested = PaletteMode::Day;
	PaletteModes before;
	PaletteModes after;
	QColor sampleBefore;
	QColor sampleAfter;
	crl::time stableFor = 0;
	crl::time bound = 0;
	crl::time waitedMs = 0;
	int samples = 0;
	PaletteRefusal refusal = PaletteRefusal::None;
	bool needed = false;
	bool followPaused = false;
	bool toggled = false;
	bool committed = false;
	bool settled = false;
	bool applied = false;
	bool finished = false;

	[[nodiscard]] bool succeeded() const {
		return finished && (refusal == PaletteRefusal::None);
	}
};

// "PALETTE_SWITCH: name=<name> finished=<0|1> requested=<day|night>
// needed=<0|1> followPaused=<0|1> toggled=<0|1> committed=<0|1>
// applied=<0|1> probe=<label> before{<modes>} after{<modes>}
// sampleBefore=<#rrggbb|invalid> sampleAfter=<#rrggbb|invalid>
// settled=<0|1> stableFor=<ms> bound=<ms> waitedMs=<ms> samples=<n>
// refusal=<name>", and " - <reason>" when the reading carries one.
[[nodiscard]] QString PaletteSwitchText(const PaletteSwitch &reading);

// Appends the stage "palette switch: <name>" and returns the reading it
// fills. Read it from a later stage; treat a refusal as a fixture gate.
// Restore it with AppendPaletteRestore before the scenario ends.
[[nodiscard]] std::shared_ptr<const PaletteSwitch> AppendPaletteSwitch(
	not_null<Runner*> runner,
	PaletteSwitchArgs args);

// The restore's reading. |captured| and |sampleCaptured| are the switch's
// |before| and |sampleBefore|. |outstanding| means the switch's obligation
// was outstanding when this restore began, so this restore performed the
// restoration and consumed it: with following paused when it prevents the
// change (|followPaused|), ToggleNightMode() and KeepApplied() back to the
// captured night mode (|toggled|), then setSystemDarkModeEnabled() back to
// the captured flag (|followRestored|; re-enabling following runs
// Application::checkSystemDarkMode() synchronously, and |after| is read
// after it), then Core::App().saveSettings() when anything changed
// (|saved|). Otherwise - a no-op or refused switch, or an obligation an
// earlier restore or cleanup consumed - it touches nothing and only
// verifies. |equal| compares the settled sample (the switch's own probe),
// the night mode and the follow flag with the captured ones. |name| labels
// the stage and |switchName| is the switch's |name|; both, the probe's
// label, |stableFor| and |bound| are filled when the stage is appended.
struct PaletteRestore {
	QString name;
	QString switchName;
	QString sampleLabel;
	QString reason;
	PaletteModes captured;
	PaletteModes before;
	PaletteModes after;
	QColor sampleCaptured;
	QColor sampleBefore;
	QColor sampleAfter;
	crl::time stableFor = 0;
	crl::time bound = 0;
	crl::time waitedMs = 0;
	int samples = 0;
	PaletteRefusal refusal = PaletteRefusal::None;
	bool outstanding = false;
	bool followPaused = false;
	bool toggled = false;
	bool followRestored = false;
	bool saved = false;
	bool settled = false;
	bool equal = false;
	bool finished = false;

	[[nodiscard]] bool restored() const {
		return finished && (refusal == PaletteRefusal::None) && equal;
	}
};

// "PALETTE_RESTORE: name=<name> switch=<switch name> finished=<0|1>
// outstanding=<0|1> followPaused=<0|1> toggled=<0|1>
// followRestored=<0|1> saved=<0|1> equal=<0|1> probe=<label>
// captured{<modes>} before{<modes>} after{<modes>} sampleCaptured=<hex>
// sampleBefore=<hex> sampleAfter=<hex> settled=<0|1> stableFor=<ms>
// bound=<ms> waitedMs=<ms> samples=<n> refusal=<name>", and
// " - <reason>" when the reading carries one.
[[nodiscard]] QString PaletteRestoreText(const PaletteRestore &reading);

// Appends the stage "palette restore: <name>" (|name| defaults to the
// switch's name) restoring what |switched| - a reading AppendPaletteSwitch
// returned - found, and returns the reading it fills. |skipReason| is the
// stage's, as in PaletteSwitchArgs. Safe to append more than once for one
// switch: only the first run after the switch changed something restores,
// every later one verifies.
[[nodiscard]] std::shared_ptr<const PaletteRestore> AppendPaletteRestore(
	not_null<Runner*> runner,
	std::shared_ptr<const PaletteSwitch> switched,
	QString name = QString(),
	Fn<QString()> skipReason = {});

// The helper measuring itself. It needs no session, network or chat; it
// reads st::windowBg->c, whose shipped values are day #ffffff
// (Telegram/lib_ui/ui/colors.palette) and night #17212b (the
// colors.tdesktop-theme inside Telegram/Resources/night.tdesktop-theme).
// The stages, in order, never interleaved with other stages:
// - entry and premise: the fixture gate (theme started, no theme editing,
//   no outstanding switch); the entry modes and sample are captured as a
//   baseline obligation; when following is off it is turned on (the
//   obligation is outstanding from then on); after settling: following
//   on, a reported system mode (a host reporting none is named, never
//   passed), nightModeChangeAllowed() false, and windowBg at the shipped
//   value of the current mode;
// - bare toggle: a bare ToggleNightMode() leaves windowBg and the night
//   mode unchanged in the same turn and over a settled interval (skipped
//   when the change is allowed, since it would apply);
// - switch: the helper switches to the other palette: needed, following
//   paused, committed, applied, and windowBg (the reading's and an
//   independent live read) at the other palette's shipped value;
// - restore: the restore returns windowBg, the night mode and following to
//   the switch's starting values; a second restore only verifies;
// - no-op: a request for the current palette changes nothing and
//   succeeds with needed=0 applied=0;
// - constant probe: a needed switch measured by a constant colour is
//   refused sample-unchanged although the mode was committed, while an
//   independent live windowBg shows the real switch; its restore brings
//   the live palette back;
// - unfinished switch and cleanup: a switch started through the module's
//   private routine mutates and leaves its reading unfinished; a second
//   switch is then refused already-switched and changes nothing; the
//   private cleanup the finish callback runs restores the unfinished one
//   and leaves the explicitly restored switches alone; a second cleanup
//   changes nothing (no theme update, no follow change);
// - entry: the entry baseline is restored by the same routine and the
//   settled windowBg, the night mode and following equal the entry
//   values, with no obligation outstanding.
// The OS appearance is never changed and the platform's reported mode is
// never synthesized; no deliberate failure is emitted.
void AppendPaletteSwitchSelfTest(not_null<Runner*> runner);

} // namespace Test
