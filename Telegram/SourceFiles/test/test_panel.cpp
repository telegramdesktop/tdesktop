/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_panel.h"

#include "base/flat_map.h"
#include "base/unique_qptr.h"
#include "base/weak_qptr.h"
#include "test/test_capture.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "test/test_text_reads.h"
#include "test/test_widgets.h"
#include "ui/platform/ui_platform_utility.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/separate_panel.h"

#include <QtCore/QStringList>
#include <QtWidgets/QApplication>

#include "styles/style_layers.h"

namespace Test {
namespace {

const auto kPanelScanSubject = u"live top level Ui::SeparatePanel"_q;
const auto kPanelScanControl = u"Ui::SeparatePanel of any kind"_q;

const auto kWalkRefusal = u"this walk reached no Ui::SeparatePanel of any "
	"kind, so its empty answer cannot be told apart from an enumeration "
	"that could never have seen one - it does not say that no panel "
	"opened; give the walk a panel it can reach, or report its scan, "
	"before reading this as an absence"_q;

const auto kPickEmptyRefusal = u"the panel list handed to this identity "
	"ask holds no live candidate, so there is nothing in it to name - "
	"every entry was empty or names a panel that has since been "
	"destroyed"_q;

const auto kPickAmbiguousRefusal = u"the panel list handed to this "
	"identity ask holds more than one live candidate, so naming any of "
	"them would be a guess and not an answer; exclude the panels already "
	"held, take a mark before the action, or tell them apart by the text "
	"they show. candidates: "_q;

const auto kNoMarkText = u"the walk ran and no mark was supplied, so this "
	"reading is an enumeration and not a difference"_q;
const auto kEmptyDifferenceText = u"the walk ran and the difference across "
	"the mark is empty"_q;
const auto kDifferenceText = u"the walk ran and the difference across the "
	"mark is "_q;

struct Watch {
	crl::time started = 0;
	crl::time deadline = 0;
	PanelShowState last = PanelShowState::Hidden;
	int samples = 0;
	base::weak_qptr<QWidget> panel;
	bool done = false;
};

[[nodiscard]] base::flat_map<QString, Watch> &Watches() {
	static auto result = base::flat_map<QString, Watch>();
	return result;
}

void LogSettle(
		const QString &name,
		const Watch &watch,
		bool settled,
		const QString &state) {
	LogRaw(u"PANEL_SHOW_SETTLE: name=%1 settled=%2 elapsedMs=%3 "
		"deadlineMs=%4 state=%5 samples=%6"_q
		.arg(name)
		.arg(settled ? 1 : 0)
		.arg(qint64(watch.started
			? (crl::now() - watch.started)
			: 0))
		.arg(qint64(watch.deadline))
		.arg(state)
		.arg(watch.samples));
}

[[nodiscard]] bool KeptBy(PanelLiveness liveness, not_null<QWidget*> panel) {
	switch (liveness) {
	case PanelLiveness::Shown:
		return !panel->isHidden();
	case PanelLiveness::Settled:
		return ReadPanelShowState(panel) == PanelShowState::Live;
	}
	return false;
}

// The self-test's own fixture. The titles are ASCII literals and never
// tr:: keys, so no language pack is involved, and the sizes are multiples
// of a live scaled token instead of pixel literals, the way
// test_via_window.cpp derives its own side. The second fixture is a
// different width on purpose: Test::WidgetDescription is the typeid name
// plus the geometry, and SeparatePanel::initGeometry centres two
// same-sized panels on the same parentGeometry, so equal widths would
// leave the two fixtures indistinguishable in every quoted list and in
// the ambiguity refusal that has to name both of them.
const auto kPanelTitle = u"Harness separate panel"_q;
const auto kOtherTitle = u"Harness separate panel elsewhere"_q;

constexpr auto kPanelWide = 6;
constexpr auto kPanelTall = 4;
constexpr auto kOtherWide = 5;

// Well over st::separatePanelDuration's 150 ms and well under the stage's
// own kDefaultStageTimeout, so a settle that never arrives is reported by
// this module's own named failure rather than as an opaque stage timeout.
constexpr auto kPanelSettleDeadline = crl::time(3000);

// Every line here is required by a source fact rather than by taste.
// SeparatePanelArgs defaults every field, so the panel needs no session,
// account, network, chats list or wallet, and with no parent it is its own
// top level - initLayout sets Qt::Dialog, so the walk above reaches it.
// Qt::WA_DontShowOnScreen makes QWidgetPrivate::show_sys early-return: the
// window is marked mapped and is never shown, so it can take neither
// activation nor focus from anything else, while paint events still
// arrive and only the flush is skipped - which is what lets
// PanelShowSettled's own update() drive the panel out of its show cache.
// Qt::WA_QuitOnClose is cleared because ~QWidget still runs
// close_helper(CloseNoEvent) for a created, visible top level, and that
// path can reach QGuiApplicationPrivate::maybeQuit(); a fixture must not
// depend on a neighbour to avoid quitting the run. setInnerSize runs
// initGeometry while rect().isEmpty(), which is the state right after
// construction, so it is the call that gives the panel its fixed size -
// skipping it leaves a zero-size panel.
[[nodiscard]] base::unique_qptr<Ui::SeparatePanel> BuildPanel(
		const QString &title,
		int wide) {
	auto result = base::make_unique_q<Ui::SeparatePanel>();
	const auto raw = result.get();
	raw->setAttribute(Qt::WA_DontShowOnScreen);
	raw->setAttribute(Qt::WA_QuitOnClose, false);
	raw->setWindowFlag(Qt::WindowStaysOnTopHint, false);
	raw->setTitle(rpl::single(title));
	raw->setInnerSize(QSize(
		st::separatePanelTitleHeight * wide,
		st::separatePanelTitleHeight * kPanelTall));
	raw->showAndActivate();
	return result;
}

} // namespace

QString PanelShowStateName(PanelShowState state) {
	switch (state) {
	case PanelShowState::Hidden:
		return u"hidden"_q;
	case PanelShowState::ShowCache:
		return u"show_cache"_q;
	case PanelShowState::Live:
		return u"live"_q;
	}
	return u"missing"_q;
}

PanelShowState ReadPanelShowState(not_null<QWidget*> panel) {
	if (panel->isHidden()) {
		return PanelShowState::Hidden;
	}
	auto children = 0;
	auto shown = 0;
	for (const auto child : panel->children()) {
		if (const auto widget = qobject_cast<QWidget*>(child)) {
			++children;
			if (!widget->isHidden()) {
				++shown;
			}
		}
	}
	if (children > 0 && shown == 0) {
		return PanelShowState::ShowCache;
	}
	return PanelShowState::Live;
}

bool PanelShowSettled(
		const QString &name,
		not_null<QWidget*> panel,
		crl::time deadline) {
	auto &watch = Watches()[name];
	if (watch.done) {
		return true;
	}
	const auto now = crl::now();
	if (!watch.started) {
		watch.started = now;
		watch.deadline = deadline;
	}
	watch.panel = panel.get();
	++watch.samples;
	const auto strong = watch.panel.get();
	if (!strong) {
		watch.done = true;
		LogSettle(name, watch, false, u"missing"_q);
		Fail(
			u"panel show settle: %1"_q.arg(name),
			u"state=missing deadlineMs=%1"_q.arg(
				QString::number(qint64(watch.deadline))));
		return true;
	}
	strong->update();
	watch.last = ReadPanelShowState(strong);
	const auto state = PanelShowStateName(watch.last);
	if (watch.last == PanelShowState::Live) {
		watch.done = true;
		LogSettle(name, watch, true, state);
		Pass(u"panel show settle: %1"_q.arg(name));
		return true;
	}
	if (now - watch.started >= watch.deadline) {
		watch.done = true;
		LogSettle(name, watch, false, state);
		Fail(
			u"panel show settle: %1"_q.arg(name),
			u"state=%1 deadlineMs=%2"_q.arg(
				state,
				QString::number(qint64(watch.deadline))));
		return true;
	}
	return false;
}

QString PanelLivenessName(PanelLiveness liveness) {
	switch (liveness) {
	case PanelLiveness::Shown:
		return u"shown"_q;
	case PanelLiveness::Settled:
		return u"settled"_q;
	}
	return u"missing"_q;
}

// Address comparison only, never a dereference. The null guard is what
// keeps a destroyed entry from answering an ask about nothing: a null
// QPointer entry equals a wrapped null, so without it a caller with no
// panel would read a walk full of dead entries as a match.
bool PanelListHolds(const PanelList &list, QWidget *panel) {
	return panel && ranges::contains(list, QPointer<QWidget>(panel));
}

DiscriminatingScan MakePanelScan(const QString &name) {
	return DiscriminatingScan(name, kPanelScanSubject, kPanelScanControl);
}

PanelWalk WalkPanels(
		not_null<DiscriminatingScan*> scan,
		const PanelWalkQuery &query) {
	// No descendant search, and no second walk beside the scan's own.
	// SeparatePanel::initLayout sets Qt::Dialog, so a panel is isWindow()
	// whatever its parent was, and QApplication::topLevelWidgets() is
	// exactly the widgets for which isWindow() holds, hidden ones
	// included - so this one pass already reaches every panel in the
	// process, and a FindAll under each top level would only count the
	// same panels twice.
	const auto examinedBefore = scan->examinedCount();
	const auto seenBefore = scan->controlCount();
	auto result = PanelWalk{ .liveness = query.liveness };
	for (const auto top : QApplication::topLevelWidgets()) {
		scan->examined();
		const auto panel = dynamic_cast<Ui::SeparatePanel*>(top);
		if (!panel) {
			continue;
		}
		// The control is tallied before both filters on purpose: an
		// excluded or unsettled panel still proves the walk reached a
		// panel, so neither an exclusion nor the liveness notion can
		// quietly take the control away from a later zero.
		const auto excluded = PanelListHolds(query.exclude, panel);
		scan->matchedControl(u"%1 state=%2 excluded=%3"_q
			.arg(WidgetDescription(panel))
			.arg(PanelShowStateName(ReadPanelShowState(panel)))
			.arg(excluded ? 1 : 0));
		if (excluded) {
			++result.excluded;
			continue;
		} else if (!KeptBy(query.liveness, panel)) {
			continue;
		}
		scan->matchedSubject(WidgetDescription(panel));
		result.panels.push_back(panel);
	}
	result.examined = scan->examinedCount() - examinedBefore;
	result.seen = scan->controlCount() - seenBefore;
	if (query.before) {
		result.differenced = true;
		result.added = PanelsAdded(*query.before, result.panels);
	}
	if (!result.seen) {
		result.refusal = kWalkRefusal;
	}
	return result;
}

PanelList PanelsAdded(const PanelList &before, const PanelList &after) {
	auto result = PanelList();
	for (const auto &panel : after) {
		if (panel && !PanelListHolds(before, panel.data())) {
			result.push_back(panel);
		}
	}
	return result;
}

PanelPick PickPanel(const PanelList &panels) {
	auto live = PanelList();
	for (const auto &panel : panels) {
		if (panel) {
			live.push_back(panel);
		}
	}
	if (live.empty()) {
		return { .refusal = kPickEmptyRefusal };
	} else if (live.size() > 1) {
		return { .refusal = kPickAmbiguousRefusal + PanelListText(live) };
	}
	return { .panel = live.front() };
}

QString PanelListText(const PanelList &panels) {
	if (panels.empty()) {
		return u"<none>"_q;
	}
	auto entries = QStringList();
	for (const auto &panel : panels) {
		entries.push_back(u"%1) %2"_q
			.arg(int(entries.size()) + 1)
			.arg(panel
				? WidgetDescription(panel.data())
				: u"<destroyed>"_q));
	}
	return entries.join(u"; "_q);
}

QString PanelWalkText(const PanelWalk &reading) {
	const auto tallies = u"liveness=%1 examined=%2 seen=%3 excluded=%4 "
		"panels=%5"_q
		.arg(PanelLivenessName(reading.liveness))
		.arg(reading.examined)
		.arg(reading.seen)
		.arg(reading.excluded)
		.arg(PanelListText(reading.panels));
	const auto verdict = !reading.certified()
		? reading.refusal
		: !reading.differenced
		? kNoMarkText
		: reading.added.empty()
		? kEmptyDifferenceText
		: (kDifferenceText + PanelListText(reading.added));
	return u"%1 - %2"_q.arg(verdict, tallies);
}

void LogPanelWalk(const QString &name, const PanelWalk &reading) {
	LogRaw(u"PANEL_WALK: name=%1 liveness=%2 examined=%3 seen=%4 "
		"panels=%5 excluded=%6 added=%7 certified=%8 refusal=%9"_q
		.arg(name)
		.arg(PanelLivenessName(reading.liveness))
		.arg(reading.examined)
		.arg(reading.seen)
		.arg(PanelListText(reading.panels))
		.arg(reading.excluded)
		.arg(reading.differenced
			? PanelListText(reading.added)
			: u"none"_q)
		.arg(reading.certified() ? 1 : 0)
		.arg(reading.certified() ? u"none"_q : reading.refusal));
}

void AppendSeparatePanelWalkSelfTest(not_null<Runner*> runner) {
	struct State {
		base::unique_qptr<Ui::SeparatePanel> panel;
		base::unique_qptr<Ui::SeparatePanel> other;
		PanelWalk before;
		PanelWalk cacheSettled;
		PanelWalk cacheShown;
		QRect cacheGeometry;
		QRect settledGeometry;
		QString noPanelText;
		QString emptyDifferenceText;
		PanelShowState cacheState = PanelShowState::Hidden;
		bool cacheTranslucent = false;
		int addedCount = 0;
	};
	// Leaked on purpose, the way this directory's other self-tests leak
	// theirs: the stages outlive this call. After the teardown stage the
	// State holds nothing but QStrings, readings and PODs - it subscribes
	// to nothing a session owns.
	const auto state = new State();

	// The release point, registered here at append time rather than left
	// to the teardown stage: a stage that times out and the scenario
	// watchdog both skip every stage after them (README.md, "Scenario
	// teardown before quit"), and a top level Ui::SeparatePanel still
	// alive on such a run would outlive QApplication. finish() also runs
	// this after a teardown stage already ran, so it has to be safe
	// twice - assigning nullptr to an already-null base::unique_qptr is a
	// no-op, and its destroy() is a plain delete, so the release is
	// synchronous and the panel leaves the walk in that same statement.
	runner->onFinish([=] {
		state->panel = nullptr;
		state->other = nullptr;
	});

	runner->add({
		.name = u"separate-panel walk self-test: a walk that reaches no "
			"Ui::SeparatePanel refuses by name, and the same walk over a "
			"panel that is only painting its show cache certifies its "
			"zero instead"_q,
		.run = [=] {
			Note(u"separate-panel walk self-test: no session, account, "
				"network, chats list, wallet or primary window - one "
				"default-constructed top level Ui::SeparatePanel this "
				"self-test builds, shows and takes down itself"_q);
			auto scan = MakePanelScan(u"panel_walk_before_fixture"_q);
			state->before = WalkPanels(&scan, {});
			state->noPanelText = PanelWalkText(state->before);

			state->panel = BuildPanel(kPanelTitle, kPanelWide);
			const auto raw = state->panel.get();

			// Read in this one turn, which is the whole show-cache window
			// and is guaranteed by construction rather than by timing:
			// toggleOpacityAnimation(true) runs hideChildren() before it
			// starts the animation, and the show() two statements later
			// cannot undo that, because QWidgetPrivate::showChildren
			// skips every child carrying WA_WState_Hidden. So the
			// statement after showAndActivate() already reads ShowCache
			// whether or not anim::Disabled() is in force, and |then|
			// asserts from these snapshots instead of re-measuring a
			// panel that has settled since. The translucency reading
			// beside them is that platform predicate re-read, not the
			// panel's private _useTransparency, which Ui::SeparatePanel
			// does not expose the way Ui::PopupMenu::useTransparency()
			// does. It is the same value, because initGeometry latched it
			// from the predicate at setInnerSize() time
			// (separate_panel.cpp:1424, reached from :1367), in the
			// statement before the show and in this same turn, and it is
			// recorded and printed here, never branched on.
			state->cacheGeometry = raw->geometry();
			state->cacheState = ReadPanelShowState(raw);
			state->cacheTranslucent
				= Ui::Platform::TranslucentWindowsSupported();
			auto settledScan = MakePanelScan(u"panel_walk_show_cache"_q);
			state->cacheSettled = WalkPanels(
				&settledScan,
				{ .liveness = PanelLiveness::Settled });
			auto shownScan = MakePanelScan(
				u"panel_walk_show_cache_shown"_q);
			state->cacheShown = WalkPanels(
				&shownScan,
				{ .liveness = PanelLiveness::Shown });
			settledScan.report();
			Note(u"separate-panel walk self-test: the certification just "
				"above is the loud form, and it passes because this walk "
				"reached a panel of some kind while none of them was "
				"settled. Every refusal below is observed instead through "
				"WalkPanels, PickPanel and PanelWalkText, which log "
				"nothing: report() on the fixture-less walk would FAIL by "
				"design, and that failure is exactly the refusal this "
				"self-test reads back as a value"_q);
		},
		.then = [=] {
			const auto raw = state->panel.get();
			const auto certifiedText = PanelWalkText(state->cacheSettled);
			Check(
				state->before.panels.empty()
					&& !state->before.certified()
					&& (state->before.examined > 0),
				u"a walk that reached no Ui::SeparatePanel of any kind "
				"refuses by name rather than answering a bare zero or an "
				"empty vector a caller could read as absence"_q,
				state->noPanelText);
			Check(
				state->cacheState == PanelShowState::ShowCache,
				u"fixture gate: the fixture panel is still painting its "
				"show-animation cache in the turn it was shown, so the two "
				"liveness notions really are read over that frame"_q,
				u"state=%1 translucentWindows=%2 geometry=%3"_q.arg(
					PanelShowStateName(state->cacheState),
					state->cacheTranslucent ? u"1"_q : u"0"_q,
					RectText(state->cacheGeometry)));
			Check(
				!PanelListHolds(state->cacheSettled.panels, raw)
					&& state->cacheSettled.panels.empty()
					&& state->cacheSettled.certified()
					&& (state->cacheSettled.seen == 1),
				u"the settled reading does not count a panel that is only "
				"painting its show cache, and its zero is certified "
				"rather than refused, because the same walk reached a "
				"panel of some kind in the same pass"_q,
				certifiedText);
			Check(
				PanelListHolds(state->cacheShown.panels, raw),
				u"!isHidden(), the filter six of the eight disposable "
				"copies used, does count that same cache frame as a live "
				"panel - which is what makes it the control for the "
				"settled reading rather than a second default"_q,
				PanelWalkText(state->cacheShown));
			Check(
				state->noPanelText != certifiedText,
				u"the refusal's text differs from the text an ordinary "
				"certified zero produces, so a log reader can tell a walk "
				"that could never have seen a panel from one that saw a "
				"panel and settled none"_q,
				u"refused=%1; certified=%2"_q.arg(
					state->noPanelText,
					certifiedText));
			LogPanelWalk(u"panel_walk_show_cache"_q, state->cacheSettled);
			LogGeometry(u"panel_walk_show_cache"_q, state->cacheGeometry);
		},
	});

	runner->add({
		.name = u"separate-panel walk self-test: the panel settles and the "
			"difference across the mark is exactly that one panel, by "
			"pointer"_q,
		.until = [=] {
			const auto raw = state->panel.get();
			return !raw
				|| PanelShowSettled(
					u"harness_panel_show"_q,
					raw,
					kPanelSettleDeadline);
		},
		.then = [=] {
			const auto raw = state->panel.get();
			state->settledGeometry = raw ? raw->geometry() : QRect();
			LogGeometry(u"panel_walk_settled"_q, state->settledGeometry);
			Check(
				state->settledGeometry == state->cacheGeometry,
				u"the two readings differ in the frame and not in the "
				"layout: initGeometry ran once, from setInnerSize, and "
				"nothing on the settle path moves or resizes the panel"_q,
				u"showCache=%1 settled=%2"_q.arg(
					RectText(state->cacheGeometry),
					RectText(state->settledGeometry)));
			Check(
				raw && (ReadPanelShowState(raw) == PanelShowState::Live),
				u"the same panel reads live once PanelShowSettled has "
				"returned, so the settled notion is about the frame and "
				"not about the walk missing the panel"_q,
				raw
					? PanelShowStateName(ReadPanelShowState(raw))
					: u"<destroyed>"_q);

			auto scan = MakePanelScan(u"panel_walk_appeared"_q);
			const auto appeared = WalkPanels(
				&scan,
				{ .before = state->before.panels });
			const auto addedAgain = PanelsAdded(
				state->before.panels,
				appeared.panels);
			state->addedCount = int(appeared.added.size());
			Check(
				appeared.certified()
					&& (appeared.panels.size() == 1)
					&& PanelListHolds(appeared.panels, raw)
					&& (appeared.added.size() == 1)
					&& PanelListHolds(appeared.added, raw),
				u"the difference across a mark taken while the panel did "
				"not exist is exactly that one panel, read back as a "
				"pointer and never as a count"_q,
				PanelWalkText(appeared));
			scan.report();
			LogPanelWalk(u"panel_walk_appeared"_q, appeared);
			Check(
				addedAgain == appeared.added,
				u"the free difference helper over a previous vector "
				"answers exactly what the mark carried in the query "
				"answered, which is what makes the two promoted shapes "
				"one implementation instead of two that can drift"_q,
				u"helper=%1; inQuery=%2"_q.arg(
					PanelListText(addedAgain),
					PanelListText(appeared.added)));
			const auto picked = PickPanel(appeared.panels);
			Check(
				picked.resolved()
					&& (picked.panel.data() == raw)
					&& picked.refusal.isEmpty(),
				u"the ordinary identity ask over a one-candidate list "
				"resolves to that panel with an empty refusal, which is "
				"the control the ambiguity refusal is measured against"_q,
				u"resolved=%1 candidates=%2 refusal=%3"_q
					.arg(picked.resolved() ? 1 : 0)
					.arg(
						PanelListText(appeared.panels),
						picked.refusal.isEmpty()
							? u"<none>"_q
							: picked.refusal));

			auto sameScan = MakePanelScan(u"panel_walk_same"_q);
			const auto same = WalkPanels(
				&sameScan,
				{ .before = appeared.panels });
			state->emptyDifferenceText = PanelWalkText(same);
			Check(
				same.certified()
					&& same.differenced
					&& same.added.empty()
					&& (same.panels.size() == 1)
					&& (state->emptyDifferenceText != state->noPanelText),
				u"an ordinary empty difference - the walk ran, reached "
				"the panel, and nothing is new since the mark - reads "
				"differently from the refusal, so neither answer can be "
				"mistaken for the other"_q,
				u"emptyDifference=%1; refused=%2"_q.arg(
					state->emptyDifferenceText,
					state->noPanelText));

			auto names = QStringList();
			if (raw) {
				for (const auto label : FindAll<Ui::FlatLabel>(raw)) {
					names.push_back(label->accessibilityName());
				}
			}
			Note(u"separate-panel walk self-test: identifying the panel "
				"by the text it shows needed nothing new - "
				"FindAll<Ui::FlatLabel> -> accessibilityName() -> join -> "
				"CheckTextReads, composed here at the call site over %1 "
				"label(s), which is why this module promotes no panel "
				"label-join idiom"_q.arg(names.size()));
			CheckTextReads(
				names.join(u" | "_q),
				kPanelTitle,
				u"the fixture panel is identified by the joined "
				"accessibilityName of its Ui::FlatLabels"_q);
		},
		.timeoutDetails = [=] {
			const auto raw = state->panel.get();
			if (!raw) {
				return u"the fixture panel is gone"_q;
			}
			auto scan = MakePanelScan(u"panel_walk_settle_timeout"_q);
			const auto walk = WalkPanels(&scan, {});
			return u"state=%1 geometry=%2 hidden=%3 walk=%4"_q
				.arg(
					PanelShowStateName(ReadPanelShowState(raw)),
					RectText(raw->geometry()))
				.arg(raw->isHidden() ? 1 : 0)
				.arg(PanelWalkText(walk));
		},
	});

	runner->add({
		.name = u"separate-panel walk self-test: a caller-named exclusion, "
			"and an ambiguous identity ask refused rather than guessed"_q,
		.run = [=] {
			state->other = BuildPanel(kOtherTitle, kOtherWide);
		},
		// A distinct name, because Watches() is keyed by the name and its
		// done flag is sticky: reusing the first panel's name would
		// answer settled for this panel without ever looking at it.
		.until = [=] {
			const auto other = state->other.get();
			return !other
				|| PanelShowSettled(
					u"harness_panel_excluded"_q,
					other,
					kPanelSettleDeadline);
		},
		.then = [=] {
			const auto raw = state->panel.get();
			const auto other = state->other.get();
			const auto failuresBefore = FailureCount();

			// Every reading, every identity string and every derived flag
			// about the second panel is taken here, while it is still
			// alive: a PanelList nulls its entry on destruction, so a
			// PanelListHolds() or a PanelListText() taken after the
			// release below would answer about <destroyed> instead of
			// about the panel the check names.
			auto bothScan = MakePanelScan(u"panel_walk_both"_q);
			const auto both = WalkPanels(&bothScan, {});
			auto keptScan = MakePanelScan(u"panel_walk_excluded"_q);
			const auto kept = WalkPanels(
				&keptScan,
				{ .exclude = PanelList{ other } });
			const auto bothText = PanelListText(both.panels);
			const auto keptText = PanelListText(kept.panels);
			const auto bothHoldsFixture = PanelListHolds(both.panels, raw);
			const auto bothHoldsOther = PanelListHolds(both.panels, other);
			const auto keptHoldsFixture = PanelListHolds(kept.panels, raw);
			const auto keptHoldsOther = PanelListHolds(kept.panels, other);
			const auto ambiguous = PickPanel(both.panels);
			const auto empty = PickPanel(PanelList());
			const auto keptPick = PickPanel(kept.panels);

			state->other = nullptr;

			auto singleScan = MakePanelScan(u"panel_walk_single_again"_q);
			const auto single = WalkPanels(&singleScan, {});
			const auto singlePick = PickPanel(single.panels);
			const auto failuresAfter = FailureCount();

			Check(
				(both.panels.size() == 2)
					&& bothHoldsFixture
					&& bothHoldsOther
					&& (kept.panels.size() == 1)
					&& keptHoldsFixture
					&& !keptHoldsOther
					&& (kept.excluded == 1)
					&& (kept.seen == 2),
				u"a panel the caller names as excluded is present in the "
				"same pass and absent from the answer, while the control "
				"still counts it - so an exclusion narrows the answer and "
				"can never quietly weaken a later zero"_q,
				u"both=%1; kept=%2 excluded=%3 seen=%4"_q
					.arg(bothText, keptText)
					.arg(kept.excluded)
					.arg(kept.seen));
			Check(
				!ambiguous.resolved()
					&& (ambiguous.panel == nullptr)
					&& ambiguous.refusal.contains(bothText),
				u"an identity ask matching more than one candidate is "
				"refused rather than guessed, and the refusal names every "
				"candidate it saw as the whole numbered list, which two "
				"panels of one size could not be told apart in"_q,
				ambiguous.refusal);
			Check(
				!empty.resolved()
					&& (empty.refusal != ambiguous.refusal)
					&& (empty.refusal != state->before.refusal)
					&& (ambiguous.refusal != state->before.refusal)
					&& keptPick.resolved()
					&& (keptPick.panel.data() == raw),
				u"the three refusals are three different sentences - a "
				"list with no live candidate, a list with more than one, "
				"and a walk that could not have reached a panel - and the "
				"one-candidate ask beside them still resolves, so each is "
				"proved reachable and proved not to be what an ordinary "
				"reading produces"_q,
				u"empty=%1; ambiguousDiffers=%2 walkDiffers=%3 "
				"keptResolved=%4"_q
					.arg(empty.refusal)
					.arg((empty.refusal != ambiguous.refusal) ? 1 : 0)
					.arg((empty.refusal != state->before.refusal) ? 1 : 0)
					.arg(keptPick.resolved() ? 1 : 0));
			Check(
				(single.panels.size() == 1)
					&& PanelListHolds(single.panels, raw)
					&& singlePick.resolved()
					&& (singlePick.panel.data() == raw),
				u"the second panel does not outlive the stage that showed "
				"it: base::unique_qptr's destroy() is a plain delete, so "
				"the release took it out of the walk in that same "
				"statement, and every stage after this one reads the "
				"one-panel fixture again"_q,
				PanelListText(single.panels));
			Check(
				failuresAfter == failuresBefore,
				u"none of those refusals logged a failure: a refusal here "
				"is a returned value the caller decides about, and the "
				"readings that produced them are pure"_q,
				u"failures before=%1 after=%2"_q
					.arg(failuresBefore)
					.arg(failuresAfter));
		},
	});

	runner->add({
		.name = u"separate-panel walk self-test: teardown"_q,
		.run = [=] {
			// Last on purpose, and the walk one statement before the
			// release is this stage's control: a bare empty answer at an
			// arbitrary point would be an accident of ordering, while the
			// same walk answering the fixture alone a statement earlier
			// makes this stage's own release what took the count to zero.
			// Everything the check says about that walk is formatted here
			// too, while the panel is still alive to be described.
			const auto raw = state->panel.get();
			auto beforeScan = MakePanelScan(u"panel_walk_teardown_before"_q);
			const auto before = WalkPanels(&beforeScan, {});
			const auto beforeOne = (before.panels.size() == 1);
			const auto beforeHoldsFixture = PanelListHolds(before.panels, raw);
			const auto beforeCertified = before.certified();
			const auto beforeText = PanelWalkText(before);
			beforeScan.report();

			state->panel = nullptr;

			auto afterScan = MakePanelScan(u"panel_walk_teardown"_q);
			const auto after = WalkPanels(
				&afterScan,
				{ .before = state->before.panels });
			Check(
				beforeOne
					&& beforeHoldsFixture
					&& beforeCertified
					&& after.panels.empty()
					&& after.differenced
					&& after.added.empty()
					&& !after.certified()
					&& (after.refusal == state->before.refusal),
				u"the very reading that answered exactly one panel across "
				"this mark answers none across it now, so the non-empty "
				"difference was not a constant - and the empty one is "
				"uncertified on purpose, which is the stronger answer: "
				"with no panel of any kind left there is nothing for the "
				"zero to be certified against, so the reading says that "
				"by name instead of handing back an absence"_q,
				u"before=%1; after=%2; appearedAdded=%3"_q
					.arg(beforeText, PanelWalkText(after))
					.arg(state->addedCount));
			LogPanelWalk(u"panel_walk_teardown"_q, after);
			Note(u"separate-panel walk self-test: fixture released, "
				"panel=%1 other=%2"_q
				.arg(state->panel ? 1 : 0)
				.arg(state->other ? 1 : 0));
		},
	});
}

} // namespace Test

#endif // _DEBUG
