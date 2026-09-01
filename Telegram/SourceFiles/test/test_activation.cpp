/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_activation.h"

#include "base/unique_qptr.h"
#include "core/application.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "test/test_widgets.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/qt_object_factory.h"
#include "ui/rp_widget.h"
#include "window/window_controller.h"

#include <QtGui/QPainter>
#include <QtWidgets/QApplication>
#include <QtWidgets/QTextEdit>

#include "styles/palette.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

namespace Test {
namespace {

// Twelve polls at Runner's kTickInterval of 50ms (test_runner.cpp:31) is
// about 600ms, well inside kDefaultStageTimeout, and it is more than one
// kActivationNoteEvery period - so the cap on the re-assertion note is
// provably exercised instead of merely never being reached.
constexpr auto kSelfTestPolls = 12;

const auto kTyped = u"harness"_q;

struct Fixture {
	base::unique_qptr<Ui::RpWidget> container;
	base::unique_qptr<Ui::RpWidget> stray;
	Ui::InputField *field = nullptr;
	int changes = 0;
	rpl::lifetime lifetime;
};

struct FocusReading {
	bool focusWindowSet = false;
	bool activeWindow = false;
	bool baseHasFocus = false;
	bool derivedHasFocus = false;
	bool focusInsideField = false;
	QString focusWidgetClass;
	QString text;
};

[[nodiscard]] FocusReading ReadFocus(Ui::InputField *field) {
	const auto activation = ReadWindowActivation(field);
	auto result = FocusReading();
	result.focusWindowSet = activation.focusWindowSet;
	result.activeWindow = activation.activeWindow;
	const auto focused = QApplication::focusWidget();
	result.focusWidgetClass = focused
		? QString::fromLatin1(focused->metaObject()->className())
		: u"none"_q;
	if (!field) {
		return result;
	}
	// The two readings a scenario can take of one wrapper, side by side.
	// QWidget::hasFocus() is non-virtual and Ui::InputField hides it with
	// its own, so the call through the base type is exactly what a generic
	// finder's QWidget* gives, while the call through the derived type is
	// what the inner QTextEdit answers. Taking both here is what puts the
	// contrast into a single line of the log instead of leaving a reader to
	// infer it from two stages that disagree.
	result.baseHasFocus = static_cast<QWidget*>(field)->hasFocus();
	result.derivedHasFocus = field->hasFocus();
	result.focusInsideField = focused && field->isAncestorOf(focused);
	result.text = field->getLastText();
	return result;
}

[[nodiscard]] QString Describe(
		const QString &label,
		const FocusReading &reading) {
	return u"%1: focusWindowSet=%2 activeWindow=%3 baseHasFocus=%4 "
		"derivedHasFocus=%5 focusInsideField=%6 focusWidget=%7 "
		"text=\"%8\""_q
		.arg(label)
		.arg(reading.focusWindowSet ? 1 : 0)
		.arg(reading.activeWindow ? 1 : 0)
		.arg(reading.baseHasFocus ? 1 : 0)
		.arg(reading.derivedHasFocus ? 1 : 0)
		.arg(reading.focusInsideField ? 1 : 0)
		.arg(reading.focusWidgetClass, reading.text);
}

[[nodiscard]] bool BuildFixture(Fixture &fixture) {
	const auto window = Core::App().activePrimaryWindow();
	if (!window) {
		return false;
	}
	const auto skip = st::defaultActiveButton.height / 2;
	const auto width = st::boxWidth;
	fixture.container = base::make_unique_q<Ui::RpWidget>(
		window->widget().get());
	const auto container = fixture.container.get();
	container->paintOn([=](QPainter &p) {
		p.fillRect(container->rect(), st::windowBgOver);
	});
	const auto field = Ui::CreateChild<Ui::InputField>(
		container,
		st::defaultInputField,
		rpl::single(u"Harness"_q));
	field->resize(width, field->height());
	field->move(skip, skip);
	container->setGeometry(
		0,
		0,
		width + skip * 2,
		field->height() + skip * 2);
	field->show();
	// Shown, unlike test_hover.cpp's deliberately hidden container, and the
	// two are not in conflict: that fixture is read through
	// Ui::GrabWidgetToImage, which renders a not-visible source explicitly,
	// while a widget inside a hidden subtree can never become
	// QApplication::focusWidget() at all - so a self-test about focus has
	// nothing left to measure the moment it hides its field. That is the
	// whole reason the teardown stage exists here.
	container->show();
	fixture.field = field;
	const auto counter = &fixture.changes;
	field->changes() | rpl::on_next([=] {
		++*counter;
	}, fixture.lifetime);
	// Unparented and never shown: its window() is itself and its
	// windowHandle() is null, which is the one shape ForceWindowActive
	// refuses by name instead of retargeting at some other top-level.
	fixture.stray = base::make_unique_q<Ui::RpWidget>(nullptr);
	return true;
}

} // namespace

void AppendWindowActivationSelfTest(not_null<Runner*> runner) {
	struct State {
		Fixture fixture;
		WindowActivation cleared;
		WindowActivation activation;
		WindowActivation polled;
		WindowActivation strayRead;
		WindowActivation strayForced;
		FocusReading inactive;
		FocusReading active;
		QString wrapperText;
		QString editorText;
		QString typedVia;
		int wrapperChanges = 0;
		int editorChanges = 0;
		int attemptsBefore = 0;
		int notesBefore = 0;
		int failuresBefore = 0;
		bool built = false;
	};
	// Leaked on purpose, the way the harness's other self-tests leak theirs:
	// the stages outlive this call. The teardown stage releases the fixture,
	// after which the State holds nothing but QStrings and PODs - no rpl
	// subscription to anything the session owns.
	const auto state = new State();

	runner->add({
		.name = u"window activation self-test: an inactive window silences "
			"focus, and activation restores it"_q,
		.run = [=] {
			state->built = BuildFixture(state->fixture);
			Check(
				state->built,
				u"fixture gate: the self-test fixture was built"_q,
				state->built
					? QString()
					: u"Core::App().activePrimaryWindow() is null"_q);
			if (!state->built) {
				return;
			}
			// Both snapshots are taken in this one turn, for the reason
			// test_menu.cpp:231-243 takes its two there: the failing shape
			// exists only inside the turn that produced it. Here the turn
			// is a safety boundary as well. ClearWindowActive mutates
			// process-global Qt state, and a stage past its timeout skips
			// every stage after it, so a de-activation left in force at a
			// stage boundary would silence the rest of the run - which is
			// the very defect this module measures.
			const auto field = state->fixture.field;
			state->cleared = ClearWindowActive();
			field->setFocusFast();
			state->inactive = ReadFocus(field);
			state->activation = ForceWindowActive(field);
			field->setFocusFast();
			state->active = ReadFocus(field);
		},
		.then = [=] {
			if (!state->built) {
				return;
			}
			const auto inactive = Describe(u"inactive"_q, state->inactive);
			const auto active = Describe(u"active"_q, state->active);
			Check(
				!state->inactive.focusWindowSet,
				u"clearing the focus window through the QPA seam really "
				"removes it, so the failing shape is reached and not "
				"merely described"_q,
				u"%1 - %2"_q.arg(
					WindowActivationDetails(state->cleared),
					inactive));
			Check(
				!state->inactive.focusWindowSet
					&& !state->inactive.activeWindow,
				u"the Qt-level activation this helper owns is gone, so "
				"every isActiveWindow()-routed and focusWindow()-routed "
				"branch now reads false"_q,
				inactive);
			Note(u"window activation self-test: the cleared half still "
				"reads derivedHasFocus=%1 focusInsideField=%2 "
				"focusWidget=%3 - QWidget::isActiveWindow() ends in a "
				"fallback to QPlatformWindow::isActive() "
				"(qwidget.cpp:6723-6725), so on a host whose OS window is "
				"genuinely active it answers true all the same and "
				"setFocus() (qwidget.cpp:6351) promotes the inner editor as "
				"usual. The full silence of this signature needs a "
				"genuinely inactive platform window - a locked or "
				"unattended console."_q
				.arg(state->inactive.derivedHasFocus ? 1 : 0)
				.arg(state->inactive.focusInsideField ? 1 : 0)
				.arg(state->inactive.focusWidgetClass));
			Check(
				state->activation.injected
					&& state->activation.focusWindowSet,
				u"Test::ForceWindowActive puts the application's focus "
				"window back in the same turn it was asked in"_q,
				WindowActivationDetails(state->activation));
			Check(
				state->active.derivedHasFocus
					&& !state->active.baseHasFocus,
				u"Ui::InputField::hasFocus() answers true for the very "
				"wrapper whose QWidget::hasFocus() answers false, which is "
				"the reading a generic finder's QWidget* would have given"_q,
				active);
			Check(
				state->active.focusInsideField
					&& (state->active.focusWidgetClass == u"QTextEdit"_q),
				u"the focus is inside the field, on the inner editor whose "
				"nearest Q_OBJECT ancestor is what a log prints as "
				"focusWidget=QTextEdit"_q,
				active);
		},
	});

	runner->add({
		.name = u"window activation self-test: typing lands only in the raw "
			"editor"_q,
		.run = [=] {
			if (!state->built) {
				return;
			}
			const auto field = state->fixture.field;
			ForceWindowActive(field);
			state->fixture.changes = 0;
			TypeText(field, kTyped);
			state->wrapperText = field->getLastText();
			state->wrapperChanges = state->fixture.changes;
			state->fixture.changes = 0;
			TypeText(field->rawTextEdit(), kTyped);
			state->editorText = field->getLastText();
			state->editorChanges = state->fixture.changes;
			state->typedVia = u"keys"_q;
			if (state->editorText.isEmpty()) {
				CommitText(field->rawTextEdit(), kTyped);
				state->editorText = field->getLastText();
				state->editorChanges = state->fixture.changes;
				state->typedVia = u"commit"_q;
			}
		},
		.then = [=] {
			if (!state->built) {
				return;
			}
			const auto details = u"wrapperText=\"%1\" wrapperChanges=%2 "
				"editorText=\"%3\" editorChanges=%4 typedVia=%5 - %6"_q
				.arg(state->wrapperText)
				.arg(state->wrapperChanges)
				.arg(state->editorText)
				.arg(state->editorChanges)
				.arg(
					state->typedVia,
					Describe(u"typed"_q, ReadFocus(state->fixture.field)));
			Check(
				state->wrapperText.isEmpty()
					&& (state->wrapperChanges == 0),
				u"Test::TypeText aimed at the Ui::InputField wrapper "
				"inserts nothing and fires no changes(), because the "
				"wrapper does not own the key route"_q,
				details);
			Check(
				(state->editorText == kTyped)
					&& (state->editorChanges > 0),
				u"the same helper aimed at rawTextEdit() inserts the text, "
				"proven by the field's own getLastText()"_q,
				details);
		},
	});

	runner->add({
		.name = u"window activation self-test: re-assertion inside a "
			"bounded wait"_q,
		.run = [=] {
			if (!state->built) {
				return;
			}
			state->polled = ReadWindowActivation(state->fixture.field);
			state->attemptsBefore = state->polled.attempts;
			state->notesBefore = state->polled.notes;
		},
		.until = [=] {
			if (!state->built) {
				return true;
			}
			// The documented, narrow exception to the README's rule that a
			// readiness must be pure. This mutates only which window Qt
			// considers focused - never product state - it is idempotent
			// and repeatable, and it encodes no expected product result.
			// It is also the only arrangement that survives a platform
			// that de-activates the window again between turns, which is
			// what run 7 of the campaign found.
			state->polled = ForceWindowActive(state->fixture.field);
			return (state->polled.attempts - state->attemptsBefore)
				>= kSelfTestPolls;
		},
		.then = [=] {
			if (!state->built) {
				return;
			}
			const auto polls = state->polled.attempts
				- state->attemptsBefore;
			const auto notes = state->polled.notes - state->notesBefore;
			const auto details = u"%1 polls=%2 notes=%3 attemptsBefore=%4 "
				"notesBefore=%5 kActivationNoteEvery=%6"_q
				.arg(WindowActivationDetails(state->polled))
				.arg(polls)
				.arg(notes)
				.arg(state->attemptsBefore)
				.arg(state->notesBefore)
				.arg(kActivationNoteEvery);
			Check(
				polls >= kSelfTestPolls,
				u"activation was re-asserted on every poll of a bounded "
				"wait, and the attempt count is reported at pass time"_q,
				details);
			Check(
				state->polled.focusWindowSet,
				u"the application still has a focus window at the end of "
				"the wait, which a one-shot activation could not promise"_q,
				details);
			Check(
				notes <= (kSelfTestPolls / kActivationNoteEvery) + 1,
				u"the re-assertion note stayed capped, so a 50ms-tick wait "
				"cannot flood the log"_q,
				details);
		},
		.timeoutDetails = [=] {
			return WindowActivationDetails(state->polled);
		},
	});

	runner->add({
		.name = u"window activation self-test: refusal"_q,
		.run = [=] {
			if (!state->built) {
				return;
			}
			state->failuresBefore = FailureCount();
			const auto stray = state->fixture.stray.get();
			state->strayRead = ReadWindowActivation(stray);
			state->strayForced = ForceWindowActive(stray);
		},
		.then = [=] {
			if (!state->built) {
				return;
			}
			Check(
				!state->strayRead.injected
					&& state->strayRead.refusal.contains(
						u"no QWindow handle"_q),
				u"a widget whose own window carries no QWindow handle is "
				"refused by name, never silently retargeted at another "
				"top-level the caller did not ask for"_q,
				WindowActivationDetails(state->strayRead));
			Check(
				!state->strayForced.injected
					&& (state->strayForced.refusal
						== state->strayRead.refusal),
				u"the imperative helper refuses that widget with exactly "
				"the text the pure reading gave, so a poll and a verdict "
				"quote the same refusal"_q,
				WindowActivationDetails(state->strayForced));
			Check(
				state->strayForced.focusWindowSet,
				u"a refused activation leaves the application's focus "
				"window exactly as it was"_q,
				WindowActivationDetails(state->strayForced));
			Check(
				FailureCount() == state->failuresBefore,
				u"the refusal is a returned value and not a logged "
				"failure: only the caller knows whether it is one"_q,
				u"failuresBefore=%1 failuresAfter=%2"_q
					.arg(state->failuresBefore)
					.arg(FailureCount()));
		},
	});

	runner->add({
		.name = u"window activation self-test: teardown"_q,
		.run = [=] {
			// Last on purpose, and the argument-less ForceWindowActive is
			// the first thing it does. This is the only module that mutates
			// process-global Qt state, a timed-out stage or the watchdog
			// skips every stage after it, and a process left without a
			// focus window would silence every focus-routed affordance in
			// whatever runs next - the exact defect measured above. The
			// fixture is parented to the primary window, so releasing the
			// unique_qptr destroys the container and with it the field,
			// which is why the raw back-pointer is dropped in the same
			// breath as its owner.
			const auto restored = ForceWindowActive();
			state->fixture.lifetime.destroy();
			state->fixture.field = nullptr;
			state->fixture.container = nullptr;
			state->fixture.stray = nullptr;
			Note(u"window activation self-test: fixture released, "
				"container=%1 stray=%2 field=%3 - %4"_q
				.arg(state->fixture.container ? 1 : 0)
				.arg(state->fixture.stray ? 1 : 0)
				.arg(state->fixture.field ? 1 : 0)
				.arg(WindowActivationDetails(restored)));
		},
	});
}

} // namespace Test

#endif // _DEBUG
