/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "test/test_probe.h"

#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtWidgets/QWidget>

#include <optional>
#include <vector>

namespace Test {

class Runner;

// A grab taken while Ui::SeparatePanel is still showing is not the
// panel's children: toggleOpacityAnimation() copies the widget into
// _animationCache, calls hideChildren(), and paintEvent draws that
// cache into rect().marginsRemoved(...) at reduced opacity, with
// marginRatio = (1 - opacity) / 5. The content region then holds a
// squeezed, faded copy of the whole frame — or nothing — so a pixel
// check reads "the widget painted nothing" even though every logged
// geometry rect is identical to a settled grab. The layout never
// moved; the frame did.
//
// PanelShowSettled answers when that cache has stopped painting. It
// returns only after the panel's own hideChildren() state has cleared
// (every direct QWidget child shown again, or the widget was never
// in that state). Elapsed time is not the contract. At its deadline
// it FAILs, naming the panel, the observed state, and deadlineMs,
// and never returns a cache frame as success. After it returns,
// a blank content grab is "painted nothing"; a
// PANEL_SHOW_SETTLE settled=0 / panel show settle FAIL is
// "captured during the show animation".

enum class PanelShowState {
	Hidden,
	ShowCache,
	Live,
};

[[nodiscard]] QString PanelShowStateName(PanelShowState state);

[[nodiscard]] PanelShowState ReadPanelShowState(not_null<QWidget*> panel);

[[nodiscard]] bool PanelShowSettled(
	const QString &name,
	not_null<QWidget*> panel,
	crl::time deadline);

// Which top level Ui::SeparatePanels are live right now, which of them a
// mark taken before an action did not hold, and a named refusal for a
// walk that could not have seen one at all.
//
// Eight disposable overlays wrote this walk by hand and all eight copies
// were thrown away with them. Six filtered it with isVisible(), which
// this very module already knows does not exclude a panel still painting
// the show-animation cache described above. Three needed "did a panel
// appear since the mark", and two answered that with a count, which is
// not a difference: one panel closing while another opens reads as no
// change, and a count cannot name which pointer is new. So the answer
// here is a set of pointers, the liveness notion is chosen by the caller
// instead of assumed, and a zero nothing could certify is refused by
// name rather than handed back as an absence.
//
// Settled is ReadPanelShowState(panel) == PanelShowState::Live, and it is
// the default. Shown is !isHidden() - the filter those six copies used -
// and it DOES keep a panel that is still painting its show cache, which
// is what makes it the control for a settled reading rather than a
// second default. For a top level it decides what isVisible() would
// decide, because a top level has no ancestor to be hidden by; the trap
// this module knows about is the cache frame, not the ancestry.

enum class PanelLiveness {
	Shown,
	Settled,
};

[[nodiscard]] QString PanelLivenessName(PanelLiveness liveness);

// One type for the answer and for the mark, so a mark is just a previous
// answer and there is no conversion between the two. QPointer keeps
// nothing alive, is never dereferenced by this module, and nulls on
// destruction - so a panel allocated later at a reused address cannot
// read back as one the mark already held, which is exactly the ABA hazard
// a raw QWidget* mark carries.
//
// PanelListHolds is the membership answer, and it is address comparison
// only - never a dereference. A null |panel| is never a match and a
// destroyed entry is never a match, so "did this mark hold the panel I
// am holding" answers false when the caller holds nothing. A bare
// ranges::contains(list, QPointer<QWidget>(widget)) gets that one wrong:
// a null QPointer entry equals a wrapped null, so a caller with no panel
// would read a mark full of destroyed entries as a match - which is the
// exact reading this API exists for, a mark kept across an action and
// re-read after the panel it named was taken down. The asked-about
// pointer has to be wrapped at all because the range concepts do not
// accept a QPointer compared against a bare pointer, which is the second
// reason this is a function here rather than an expression every caller
// retypes.
using PanelList = std::vector<QPointer<QWidget>>;

[[nodiscard]] bool PanelListHolds(const PanelList &list, QWidget *panel);

// |exclude| holds panels the caller already has - never a type, and never
// a product notion of "the panel the press opened". An excluded panel is
// still tallied as reached, so an exclusion can never silently weaken a
// later zero. |before| is a mark taken before the action; when it is set
// the reading is a difference as well as an enumeration.
struct PanelWalkQuery {
	PanelLiveness liveness = PanelLiveness::Settled;
	PanelList exclude;
	std::optional<PanelList> before;
};

// |refusal| is empty exactly when the walk reached at least one
// Ui::SeparatePanel of any kind - hidden, excluded, or still painting its
// show cache - and certified() asks that same question by name. Its
// sentence carries no count, so the same refusal is byte-identical in
// every run and in every walk of one run, and two moments may be compared
// for equality; the counts live in PanelWalkText and in the PANEL_WALK
// row instead, where a reader wants them and no oracle compares them.
//
// |liveness| is the notion |panels| was kept by, recorded on the reading
// because PanelWalkText and LogPanelWalk take only the reading and could
// not otherwise say which notion produced the answer.
//
// |examined|, |seen| and |excluded| describe THIS walk. The first two are
// read back from the scan's own counters as deltas rather than tallied a
// second time beside it, so a scan may span several walks while each
// reading still describes only its own; report() is the one that speaks
// for the whole scan.
//
// |added| is meaningful only when |differenced| is true, and certified()
// is orthogonal to it: an empty difference over a walk that reached no
// panel at all is uncertified on purpose, because nothing was there for
// the zero to be certified against.
struct PanelWalk {
	PanelList panels;
	PanelList added;
	QString refusal;
	PanelLiveness liveness = PanelLiveness::Settled;
	int examined = 0;
	int seen = 0;
	int excluded = 0;
	bool differenced = false;

	[[nodiscard]] bool certified() const {
		return refusal.isEmpty();
	}
};

// |panel| is non-null exactly when |refusal| is empty, the contract
// Test::ToastSubtreeReading and Test::PaintingLayerRootResult already
// carry. Test::FindLiveToast() answers an ambiguous ask with a bare
// nullptr, and README.md records what that costs: one null means both "no
// candidate" and "more than one", so every caller has to print the count
// and the identities itself before its log can be read. Here the two are
// different sentences and the ambiguous one names every candidate it saw.
struct PanelPick {
	QPointer<QWidget> panel;
	QString refusal;

	[[nodiscard]] bool resolved() const {
		return panel != nullptr;
	}
};

// This module's own subject and control wording for a panel walk, so two
// logs name the same counts the same way. The control is every
// Ui::SeparatePanel the walk reached at all, which is a superset of the
// subject: report() can therefore only refuse a walk that reached no
// panel whatsoever - precisely the case a bare zero cannot tell from
// absence - while "panels reached, none of them settled" stays a zero it
// certifies.
[[nodiscard]] DiscriminatingScan MakePanelScan(const QString &name);

// WalkPanels, PanelsAdded, PickPanel, PanelListText and PanelWalkText are
// pure: they log nothing and are safe to poll from a stage's |until|. The
// loud forms are scan.report(), which FAILs on a zero it cannot certify,
// and LogPanelWalk, which writes one PANEL_WALK row. A caller that wants
// the refusal without a verdict reads reading.refusal or
// PanelWalkText(reading), which say the same thing and log nothing.
//
// There is deliberately no scan-less overload. A zero read without the
// control is exactly the defect this promotes a cure for, and the scan is
// the only thing that can tell "no panel is settled" from "this walk
// could never have seen one" - the same discipline test_probe.h states
// for Probe's missing whole-history accessor.
[[nodiscard]] PanelWalk WalkPanels(
	not_null<DiscriminatingScan*> scan,
	const PanelWalkQuery &query = {});

// Every panel in |after| that |before| did not hold, dropping destroyed
// entries on both sides. WalkPanels calls this for query.before, so the
// mark carried in a query and the free helper over a previous vector are
// one implementation and can never disagree.
[[nodiscard]] PanelList PanelsAdded(
	const PanelList &before,
	const PanelList &after);

// Resolves only when exactly one live candidate remains. A list with no
// live candidate and a list with more than one get different sentences,
// and neither reads like the walk's uncertified-zero refusal: these two
// answer "what is in the list you handed me", while that one answers
// "could this enumeration have reached a panel at all".
[[nodiscard]] PanelPick PickPanel(const PanelList &panels);

// PanelListText numbers its entries, because Test::WidgetDescription is
// the typeid name plus the geometry and two same-sized Ui::SeparatePanels
// are centred identically by initGeometry, so an unnumbered join of two
// of them is unreadable. A destroyed entry prints <destroyed> and an
// empty list prints <none>.
//
// PanelWalkText is never empty and has exactly four shapes - the refusal,
// an empty difference, the difference itself, and "no mark was supplied"
// - each carrying the liveness, this walk's tallies and the identities it
// judged.
[[nodiscard]] QString PanelListText(const PanelList &panels);
[[nodiscard]] QString PanelWalkText(const PanelWalk &reading);

// One PANEL_WALK row in the PANEL_SHOW_SETTLE grammar above, so panel
// readings stay comparable across runs line by line.
void LogPanelWalk(const QString &name, const PanelWalk &reading);

// Identifying a panel from the text it shows needs nothing from here: it
// is Test::FindAll<Ui::FlatLabel>(panel) -> accessibilityName() -> join
// -> Test::CheckTextReads, composed at the call site the way
// test_via_window.cpp already writes it, and this module deliberately
// promotes no panel label-join idiom for it.

// This module measuring itself over a synthetic top level
// Ui::SeparatePanel it creates, shows and takes down inside the stages
// that read it - no session, account, network, chats list or wallet, and
// no primary window, because a panel with no parent is its own top level.
//
// What it does ask of the process is exclusivity, and that is a real
// precondition rather than one more thing it does without: for the stages
// it runs it must be the only owner of a live Ui::SeparatePanel. Its
// preamble refusal and its 1 / 2 / 1 / 0 panel counts are taken over every
// top level in the process and not over a subtree it owns, so a foreign
// panel - an export, payments, passport, attach-bot, location-picker or
// standalone-layer-stack one - turns them into FAILs. A violation shows
// first in the first stage's refusal check, whose details are that
// preamble walk's PanelWalkText and therefore name every panel the walk
// reached: read that FAIL as a packing error in the scenario rather than
// as a fault in the instrument. For the same cross-fixture reason it must
// not be packed beside a live Ui::PopupMenu fixture either, because
// showAndActivate() closes every active popup in the process.
//
// It emits no deliberate failure: every refusal it demonstrates is
// observed through the pure readings above, which log nothing, and
// asserted as a passing Check whose details carry the refusal verbatim -
// the rule test_toast_capture.h states for this directory.
void AppendSeparatePanelWalkSelfTest(not_null<Runner*> runner);

} // namespace Test
