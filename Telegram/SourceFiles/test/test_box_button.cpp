/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_box_button.h"

#include "base/unique_qptr.h"
#include "base/weak_qptr.h"
#include "core/application.h"
#include "test/test_capture.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "test/test_widgets.h"
#include "ui/layers/box_content.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/layer_widget.h"
#include "ui/widgets/buttons.h"
#include "ui/abstract_button.h"
#include "ui/rp_widget.h"
#include "ui/vertical_list.h"
#include "window/window_controller.h"

#include <QtGui/QPainter>

#include "styles/palette.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

namespace Test {
namespace {

const auto kSubmitLabel = u"Harness Submit"_q;
const auto kCancelLabel = u"Harness Cancel"_q;
const auto kCloseLabel = u"Harness Close"_q;
const auto kDecoyLabel = u"Harness Content"_q;
const auto kUnknownLabel = u"Harness Nowhere"_q;

struct Fixture {
	base::unique_qptr<Ui::RpWidget> stray;
	base::weak_qptr<Ui::GenericBox> box;
};

// Ui::BoxLayerWidget::addButton is the only thing that parents a footer
// button directly to the shell (ui/layers/box_layer_widget.cpp:327-331), and
// the box content sits under that same root, so an unrestricted walk would
// answer content buttons too. Filtering on the parent is what makes the
// answer exactly "the shell's own button row" - the close Ui::IconButton an
// addTopButton call parents there is included on purpose, so a refusal lists
// every direct child and stays diagnosable through a layout change.
[[nodiscard]] std::vector<Ui::AbstractButton*> ShellButtons(QWidget *root) {
	auto result = std::vector<Ui::AbstractButton*>();
	if (!root) {
		return result;
	}
	for (const auto button : FindAll<Ui::AbstractButton>(root)) {
		if (button->parentWidget() == root) {
			result.push_back(button);
		}
	}
	return result;
}

void SelfTestBoxContent(
		not_null<Ui::GenericBox*> box,
		Fn<void()> submitted) {
	box->setTitle(u"Harness shell buttons"_q);
	box->setWidth(st::boxWidth);
	const auto content = box->verticalLayout();
	const auto band = st::defaultActiveButton.height;
	Ui::AddSkip(content, band);
	auto owned = object_ptr<Ui::RpWidget>(content.get());
	const auto row = owned.data();
	row->resize(st::boxWidth, band);
	row->paintOn([=](QPainter &p) {
		p.fillRect(row->rect(), st::attentionButtonFg);
	});
	content->add(std::move(owned));
	Ui::AddSkip(content, band);
	// The control of this whole self-test. It carries the same kind of
	// Ui::RoundButton the footer does, so the content-rooted search answers
	// one instead of zero and the zero the next stage quotes is provably
	// about where the footer row lives rather than about an empty box.
	content->add(object_ptr<Ui::RoundButton>(
		content.get(),
		rpl::single(kDecoyLabel),
		st::defaultActiveButton));
	Ui::AddSkip(content, band);
	box->addButton(rpl::single(kSubmitLabel), std::move(submitted));
	box->addButton(rpl::single(kCancelLabel), nullptr);
	box->addButton(rpl::single(kCloseLabel), [=] { box->closeBox(); });
}

[[nodiscard]] bool BuildFixture(Fixture &fixture, Fn<void()> submitted) {
	const auto window = Core::App().activePrimaryWindow();
	if (!window) {
		return false;
	}
	fixture.stray = base::make_unique_q<Ui::RpWidget>(window->widget().get());
	// anim::type::instant is why this fixture needs no animation wait:
	// LayerStackWidget takes its instant branch straight to animationDone,
	// where layer->show() runs, while the normal path hides the layer for
	// the whole animation. test_layer_root.cpp:96-122 carries the full walk.
	fixture.box = window->show(
		Box(SelfTestBoxContent, std::move(submitted)),
		Ui::LayerOption::CloseOther,
		anim::type::instant);
	return (fixture.box.get() != nullptr);
}

} // namespace

BoxShellButtons ReadBoxButtons(QWidget *box, const QString &label) {
	auto result = BoxShellButtons();
	if (!box) {
		result.identity = u"no widget"_q;
		result.refusal = u"no box was handed to the shell button clicker"_q;
		return result;
	}
	result.identity = WidgetDescription(box);
	result.contentRoundButtons = int(FindAll<Ui::RoundButton>(box).size());
	const auto root = PaintingLayerRoot(box);
	if (!root.resolved()) {
		result.refusal = root.refusal;
		return result;
	}
	result.root = root.widget;
	auto unusable = QString();
	for (const auto button : ShellButtons(root.widget.data())) {
		++result.shellButtons;
		if (dynamic_cast<Ui::RoundButton*>(button)) {
			++result.shellRoundButtons;
		}
		auto name = button->accessibilityName();
		if (name.isEmpty()) {
			name = u"<unnamed>"_q;
		}
		const auto visible = button->isVisible();
		const auto enabled = !button->isDisabled();
		result.labels.push_back((visible && enabled)
			? name
			: u"%1 (%2)"_q.arg(name, visible ? u"disabled"_q : u"hidden"_q));
		if (name.compare(label, Qt::CaseInsensitive) != 0) {
			continue;
		}
		// A click on a hidden or a disabled button is exactly the silent
		// no-op this helper exists to remove, and Test::Click would deliver
		// it just as happily as any other, so a name that matches an
		// unusable button is a refusal here and never a match.
		if (!visible || !enabled) {
			if (unusable.isEmpty()) {
				unusable = u"the shell button labelled \"%1\" under %2 is "
					"%3, so clicking it would do nothing at all"_q
					.arg(
						label,
						WidgetDescription(root.widget.data()),
						visible ? u"disabled"_q : u"hidden"_q);
			}
		} else if (!result.match) {
			result.match = button;
		}
	}
	if (!result.match) {
		result.refusal = unusable.isEmpty()
			? u"no direct child of %1 carries the label \"%2\"; its %3 "
				"shell button(s) are [%4]"_q
				.arg(WidgetDescription(root.widget.data()), label)
				.arg(result.shellButtons)
				.arg(result.labels.join(u", "_q))
			: unusable;
	}
	return result;
}

bool BoxButtonReady(QWidget *box, const QString &label) {
	return ReadBoxButtons(box, label).matched();
}

QString BoxButtonDetails(QWidget *box, const QString &label) {
	const auto reading = ReadBoxButtons(box, label);
	const auto text = u"box %1 root=%2 wanted=\"%3\" shellButtons=%4 "
		"shellRoundButtons=%5 contentRoundButtons=%6 labels=[%7]"_q
		.arg(
			reading.identity,
			reading.root
				? WidgetDescription(reading.root.data())
				: u"unresolved"_q,
			label)
		.arg(reading.shellButtons)
		.arg(reading.shellRoundButtons)
		.arg(reading.contentRoundButtons)
		.arg(reading.labels.join(u", "_q));
	return reading.refusal.isEmpty()
		? text
		: u"%1 - %2"_q.arg(text, reading.refusal);
}

bool ClickBoxButton(QWidget *box, const QString &label) {
	const auto reading = ReadBoxButtons(box, label);
	if (!reading.matched()) {
		Fail(u"click box button \"%1\""_q.arg(label), reading.refusal);
		return false;
	}
	// Both identities are formatted before the click, never after it. A
	// footer callback commonly closes its box - 180 of the 645 addButton
	// call sites under Telegram/SourceFiles are a plain closeBox() lambda -
	// and that path is synchronous inside Test::Click:
	// AbstractButton::setDown(false) calls clicked()
	// (abstract_button.cpp:190-191), clicked() runs _clickedCallback()
	// (:122-131), and the callback reaches LayerStackWidget::clearLayers(),
	// whose clearClosingLayers() erases the Ui::BoxLayerWidget out of its
	// unique_ptr vector (layer_widget.cpp:944-977) and deletes the shell
	// with every button on it. A WidgetDescription taken after the click
	// would read freed memory. The reading now reports that destruction
	// itself: BoxShellButtons::match is a QPointer<QWidget>, so survived= is
	// matched() on this same reading rather than a second weak guard beside
	// it. That is also why the two descriptions are still taken first - the
	// way the reading reports the death is by answering null, and afterwards
	// it has nothing left to hand WidgetDescription, which takes
	// not_null<QWidget*>.
	const auto matchText = WidgetDescription(reading.match.data());
	const auto rootText = WidgetDescription(reading.root.data());
	Click(reading.match.data());
	Note(u"clicked box shell button \"%1\" %2 under %3, survived=%4 "
		"shellButtons=%5 contentRoundButtons=%6 labels=[%7]"_q
		.arg(label, matchText, rootText)
		.arg(reading.matched() ? 1 : 0)
		.arg(reading.shellButtons)
		.arg(reading.contentRoundButtons)
		.arg(reading.labels.join(u", "_q)));
	return true;
}

void AppendBoxButtonClickSelfTest(not_null<Runner*> runner) {
	struct State {
		Fixture fixture;
		BoxShellButtons closedReading;
		QString closedMatchText;
		QString closedRootText;
		QString closedIdentityBefore;
		QStringList closedLabelsBefore;
		int fired = 0;
		bool built = false;
		bool closedClicked = false;
		bool closedAlive = false;
		int closedFailuresBefore = 0;
		int closedFailuresAfter = 0;
		int closedShellButtonsBefore = 0;
		int closedContentRoundButtonsBefore = 0;
		bool closedMatchedBefore = false;
		bool closedMatchedSameTurn = true;
	};
	// Leaked on purpose, the way the harness's other self-tests leak theirs:
	// the stages outlive this call. The teardown stage releases the fixture,
	// after which the State holds nothing but PODs and an empty weak pointer
	// - no rpl subscription to anything the session owns.
	const auto state = new State();
	const auto details = [=] {
		return BoxButtonDetails(state->fixture.box.get(), kSubmitLabel);
	};
	const auto ready = [=] {
		if (!state->built) {
			return true;
		}
		const auto box = state->fixture.box.get();
		// isVisible(), not !isHidden(): a box inside a layer that is still
		// hidden holds no shell row for the rooted search to reach.
		return box
			&& box->isVisible()
			&& PaintingLayerRoot(box).resolved();
	};

	runner->add({
		.name = u"box button self-test: the shell row is not in the "
			"content"_q,
		.run = [=] {
			state->built = BuildFixture(
				state->fixture,
				[=] { ++state->fired; });
			Check(
				state->built,
				u"fixture gate: the self-test fixture was built"_q,
				state->built
					? QString()
					: u"Core::App().activePrimaryWindow() is null"_q);
		},
		.until = ready,
		.then = [=] {
			if (!state->built) {
				return;
			}
			const auto box = state->fixture.box.get();
			if (!box) {
				Check(
					false,
					u"the fixture box is still alive to be read"_q,
					details());
				return;
			}
			const auto root = PaintingLayerRoot(box);
			const auto shell = ShellButtons(root.widget.data());
			auto reachable = 0;
			for (const auto button : shell) {
				if (box->isAncestorOf(button)) {
					++reachable;
				}
			}
			Check(
				!shell.empty() && (reachable == 0),
				u"every shell footer button is a direct child of the "
				"Ui::BoxLayerWidget and none of them is a descendant of "
				"the published box content"_q,
				u"shellButtons=%1 shellFooterButtonsFoundFromContent=%2 "
				"- %3"_q
					.arg(int(shell.size()))
					.arg(reachable)
					.arg(details()));
			const auto content = FindAll<Ui::RoundButton>(box);
			Check(
				(int(content.size()) == 1)
					&& (content.front()->accessibilityName().compare(
						kDecoyLabel,
						Qt::CaseInsensitive) == 0),
				u"the content-rooted Ui::RoundButton search still answers "
				"the decoy inside the box, so its zero footer buttons is "
				"about where the shell row lives and not about an empty "
				"box"_q,
				details());
			Check(
				state->fired == 0,
				u"the footer callback has not fired before anything "
				"clicked it"_q,
				u"fired=%1"_q.arg(state->fired));
		},
		.timeoutDetails = details,
	});

	runner->add({
		.name = u"box button self-test: the rooted clicker finds and "
			"clicks"_q,
		.until = [=] {
			return !state->built
				|| BoxButtonReady(state->fixture.box.get(), kSubmitLabel);
		},
		.then = [=] {
			if (!state->built) {
				return;
			}
			const auto box = state->fixture.box.get();
			if (!box) {
				Check(
					false,
					u"the fixture box is still alive to be clicked"_q,
					details());
				return;
			}
			const auto before = FailureCount();
			const auto clicked = ClickBoxButton(box, kSubmitLabel);
			const auto after = FailureCount();
			Check(
				clicked,
				u"the rooted clicker finds the shell footer button the "
				"content-rooted search could not reach, and clicks it"_q,
				details());
			Check(
				state->fired == 1,
				u"the footer callback fired exactly once, so the click "
				"went through the button's real press/release route"_q,
				u"fired=%1"_q.arg(state->fired));
			Check(
				after == before,
				u"a successful click logs no failure of its own"_q,
				u"failures before=%1 after=%2"_q.arg(before).arg(after));
			Check(
				CaptureInLayerRoot(box, u"box_button_shell"_q),
				u"the clicked shell row is saved as durable evidence "
				"through the existing layer-root capture"_q,
				details());
		},
		.timeoutDetails = details,
	});

	runner->add({
		.name = u"box button self-test: refusal"_q,
		.run = [=] {
			if (!state->built) {
				return;
			}
			const auto box = state->fixture.box.get();
			const auto before = FailureCount();
			const auto missing = ReadBoxButtons(nullptr, kSubmitLabel);
			const auto missingDetails = BoxButtonDetails(
				nullptr,
				kSubmitLabel);
			const auto unknown = ReadBoxButtons(box, kUnknownLabel);
			const auto decoy = ReadBoxButtons(box, kDecoyLabel);
			const auto stray = ReadBoxButtons(
				state->fixture.stray.get(),
				kSubmitLabel);
			const auto strayRoot = PaintingLayerRoot(
				state->fixture.stray.get());
			const auto after = FailureCount();
			Note(u"box button self-test: every refusal below is observed "
				u"through ReadBoxButtons and BoxButtonDetails, which log "
				u"nothing; ClickBoxButton turns each of them into a Fail, "
				u"which is why the refusal stage never goes through it"_q);
			Check(
				!missing.matched()
					&& (missing.match == nullptr)
					&& missing.refusal.contains(u"no box was handed"_q)
					&& missingDetails.contains(missing.refusal),
				u"a null box is refused by name, never walked and never "
				"answered with a null the caller would dereference"_q,
				missing.refusal);
			Check(
				!unknown.matched()
					&& (unknown.shellButtons > 0)
					&& unknown.refusal.contains(kUnknownLabel)
					&& unknown.refusal.contains(kSubmitLabel)
					&& unknown.refusal.contains(kCancelLabel),
				u"a label no shell button carries is refused, and the "
				"refusal lists every direct child of the shell it saw"_q,
				unknown.refusal);
			Check(
				!decoy.matched()
					&& (decoy.contentRoundButtons == 1)
					&& !decoy.refusal.isEmpty(),
				u"the decoy's label matches a button inside the box "
				"content and is still refused, so the direct-child "
				"restriction keeps a content button from being clicked in "
				"a footer button's place"_q,
				decoy.refusal);
			Check(
				!stray.matched()
					&& !strayRoot.resolved()
					&& !strayRoot.refusal.isEmpty()
					&& (stray.refusal == strayRoot.refusal),
				u"a widget with no Ui::BoxLayerWidget ancestor carries "
				"Test::PaintingLayerRoot's own refusal verbatim, instead "
				"of a second wording of the same fact"_q,
				stray.refusal);
			Check(
				after == before,
				u"none of these refusals logged a failure: a refusal is a "
				"returned value the caller decides about"_q,
				u"failures before=%1 after=%2"_q.arg(before).arg(after));
		},
	});

	runner->add({
		.name = u"box button self-test: a click that closes its own box"_q,
		.run = [=] {
			if (!state->built) {
				return;
			}
			const auto box = state->fixture.box.get();
			if (!box) {
				Check(
					false,
					u"the fixture box is still alive to be closed"_q,
					details());
				return;
			}
			// This reading is taken to be kept, not to be used here: the
			// stage asserts on this very object one runner tick later,
			// after the click has deleted the shell it names. Both
			// identities are formatted now, while it is still matched,
			// because the way it reports that death is by answering null,
			// and WidgetDescription takes not_null<QWidget*>, whose
			// Expects is a crash and not a refusal - afterwards there is
			// nothing left to hand it, so the |then| prints what was
			// recorded here. The click really does destroy the shell
			// inside this same turn: closeBox() reaches
			// LayerStackWidget::prepareAnimation, which calls
			// clearOldWidgets() synchronously on both of its branches
			// (layer_widget.cpp:663-671), and _closingLayers holds
			// std::unique_ptrs (layer_widget.h:296), so the erase is a
			// plain delete - the same fact boxAliveRightAfter=0 below
			// already measures through a base::weak_qptr. ReadBoxButtons
			// logs nothing, so taking it ahead of the FailureCount()
			// bracket cannot move the number that bracket is about.
			state->closedReading = ReadBoxButtons(box, kCloseLabel);
			state->closedMatchedBefore = state->closedReading.matched();
			state->closedIdentityBefore = state->closedReading.identity;
			state->closedLabelsBefore = state->closedReading.labels;
			state->closedShellButtonsBefore
				= state->closedReading.shellButtons;
			state->closedContentRoundButtonsBefore
				= state->closedReading.contentRoundButtons;
			if (state->closedMatchedBefore) {
				state->closedMatchText = WidgetDescription(
					state->closedReading.match.data());
				state->closedRootText = WidgetDescription(
					state->closedReading.root.data());
			}
			state->closedFailuresBefore = FailureCount();
			state->closedClicked = ClickBoxButton(box, kCloseLabel);
			state->closedAlive = (state->fixture.box.get() != nullptr);
			state->closedMatchedSameTurn = state->closedReading.matched();
			state->closedFailuresAfter = FailureCount();
		},
		.then = [=] {
			if (!state->built) {
				return;
			}
			Check(
				state->closedClicked,
				u"a footer button whose callback closes its own box is "
				"found, clicked and reported like any other"_q,
				u"clicked=%1 boxAliveRightAfter=%2"_q
					.arg(state->closedClicked ? 1 : 0)
					.arg(state->closedAlive ? 1 : 0));
			Check(
				state->fixture.box.get() == nullptr,
				u"that click really destroyed the box and the shell under "
				"it, inside Test::Click"_q,
				u"box=%1"_q.arg(state->fixture.box ? 1 : 0));
			Check(
				state->closedFailuresAfter == state->closedFailuresBefore,
				u"the clicker described the click it made without touching "
				"the freed shell, so no failure was logged across it"_q,
				u"failures before=%1 after=%2"_q
					.arg(state->closedFailuresBefore)
					.arg(state->closedFailuresAfter));
			Check(
				state->closedMatchedBefore
					&& !state->closedMatchedSameTurn
					&& !state->closedReading.matched()
					&& (state->closedReading.match == nullptr)
					&& (state->closedReading.root == nullptr),
				u"the retained reading reports its own subject's "
				"destruction: it matched before the click and answers "
				"matched()==false after it, in that same turn and again a "
				"runner tick later in this |then|, because "
				"BoxShellButtons::match is a QPointer<QWidget>"_q,
				u"matchedBefore=%1 matchedSameTurn=%2 matchedLater=%3 "
				"match=[%4] root=[%5]"_q
					.arg(state->closedMatchedBefore ? 1 : 0)
					.arg(state->closedMatchedSameTurn ? 1 : 0)
					.arg(state->closedReading.matched() ? 1 : 0)
					.arg(state->closedMatchText, state->closedRootText));
			Check(
				(state->closedReading.identity
					== state->closedIdentityBefore)
					&& (state->closedReading.labels
						== state->closedLabelsBefore)
					&& (state->closedReading.shellButtons
						== state->closedShellButtonsBefore)
					&& (state->closedReading.contentRoundButtons
						== state->closedContentRoundButtonsBefore)
					&& !state->closedLabelsBefore.isEmpty()
					&& (state->closedShellButtonsBefore > 0),
				u"the non-widget fields of that same reading are values "
				"and are unchanged and printable after the shell is gone, "
				"which is what a post-destruction reading has left to "
				"report"_q,
				u"identity=%1 shellButtons=%2 (before %3) "
				"contentRoundButtons=%4 (before %5) labels=[%6] "
				"(before [%7])"_q
					.arg(state->closedReading.identity)
					.arg(state->closedReading.shellButtons)
					.arg(state->closedShellButtonsBefore)
					.arg(state->closedReading.contentRoundButtons)
					.arg(state->closedContentRoundButtonsBefore)
					.arg(
						state->closedReading.labels.join(u", "_q),
						state->closedLabelsBefore.join(u", "_q)));
			Check(
				!state->closedMatchText.isEmpty()
					&& !state->closedRootText.isEmpty(),
				u"the identities the log prints were formatted while the "
				"reading was matched; nothing is formatted from the "
				"reading after the click, because it hands back no "
				"pointer to format"_q,
				u"match=[%1] root=[%2]"_q
					.arg(state->closedMatchText, state->closedRootText));
		},
	});

	runner->add({
		.name = u"box button self-test: teardown"_q,
		.run = [=] {
			// Last on purpose. A timed-out stage or the watchdog skips
			// every stage after it, so anything still held here would
			// outlive the run. Hiding the layer is also what the next
			// self-test in a packed scenario depends on: a shown
			// Ui::LayerStackWidget covers the whole primary window, so a
			// window-mapped grab taken while this fixture is up would read
			// the layer's pixels instead of the widget under it.
			if (const auto window = Core::App().activePrimaryWindow()) {
				window->hideLayer(anim::type::instant);
			}
			state->fixture.stray = nullptr;
			state->fixture.box.reset();
			Note(u"box button self-test: fixture released, stray=%1 box=%2 "
				"fired=%3"_q
				.arg(state->fixture.stray ? 1 : 0)
				.arg(state->fixture.box ? 1 : 0)
				.arg(state->fired));
		},
	});
}

} // namespace Test

#endif // _DEBUG
