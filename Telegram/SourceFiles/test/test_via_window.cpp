/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_via_window.h"

#include "base/unique_qptr.h"
#include "base/weak_ptr.h"
#include "core/application.h"
#include "test/test_capture.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "test/test_widgets.h"
#include "ui/toast/toast.h"
#include "ui/widgets/labels.h"
#include "ui/qt_object_factory.h"
#include "ui/rp_widget.h"
#include "window/window_controller.h"

#include <QtCore/QPointer>
#include <QtCore/QStringList>
#include <QtGui/QPainter>

#include "styles/palette.h"
#include "styles/style_widgets.h"

namespace Test {
namespace {

// LooksBlank samples a grid of up to 32x32 points and calls anything under
// 2x2 blank outright, so the flat half has to be comfortably larger than
// that grid for its blank verdict to be about the uniform fill rather than
// about an image too small to sample. Four button heights clear it at every
// interface scale, and the number itself comes from a live scaled style
// token rather than from a pixel count of its own.
constexpr auto kFlatBands = 4;

const auto kToastText = u"Harness toast"_q;

struct Fixture {
	base::unique_qptr<Ui::RpWidget> backdrop;
	base::unique_qptr<Ui::RpWidget> flat;
	base::unique_qptr<Ui::RpWidget> transparent;
	base::weak_ptr<Ui::Toast::Instance> toast;
	QPointer<QWidget> toastWidget;
};

[[nodiscard]] QString FixtureDetails(const Fixture &fixture) {
	const auto toast = fixture.toastWidget.data();
	if (!toast) {
		return u"the fixture toast no longer exists"_q;
	}
	return u"toast visible=%1 opaquePaint=%2 noSystemBackground=%3 - %4"_q
		.arg(toast->isVisible() ? 1 : 0)
		.arg(toast->testAttribute(Qt::WA_OpaquePaintEvent) ? 1 : 0)
		.arg(toast->testAttribute(Qt::WA_NoSystemBackground) ? 1 : 0)
		.arg(ViaWindowDetails(toast));
}

// Two tones, so a window-mapped crop of the region keeps a luma spread above
// kBlankSpreadThreshold whatever is fading in over it. With one tone the
// positive leg would rest on whatever the widget under measurement managed
// to paint, which is exactly the thing under measurement.
void PaintTwoTone(not_null<Ui::RpWidget*> widget) {
	const auto raw = widget.get();
	raw->paintOn([=](QPainter &p) {
		const auto half = raw->height() / 2;
		p.fillRect(QRect(0, 0, raw->width(), half), st::attentionButtonFg);
		p.fillRect(
			QRect(0, half, raw->width(), raw->height() - half),
			st::windowBgOver);
	});
}

[[nodiscard]] bool BuildFixture(Fixture &fixture) {
	const auto window = Core::App().activePrimaryWindow();
	if (!window) {
		return false;
	}
	const auto top = window->widget().get();

	// Created before the toast on purpose, and never raised: siblings paint
	// in child order, so the backdrop stays under the fade-in wrapper. A
	// raise() would put the fixture over the product widget and the crop
	// would then measure the fixture instead of what the window holds where
	// the toast is.
	fixture.backdrop = base::make_unique_q<Ui::RpWidget>(top);
	const auto backdrop = fixture.backdrop.get();
	PaintTwoTone(backdrop);
	backdrop->show();

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
	// 36-39), so it and the backdrop are siblings sharing one origin and
	// geometry() maps between them with no conversion at all.
	const auto widget = instance->widget().get();
	fixture.toastWidget = widget;
	backdrop->setGeometry(widget->geometry());

	// The flat half sits at the window's own top-left corner, where a toast
	// attached to nothing never reaches: it is centred in its parent
	// (ui/toast/toast_widget.cpp:474-485).
	const auto side = st::defaultActiveButton.height * kFlatBands;
	fixture.flat = base::make_unique_q<Ui::RpWidget>(top);
	const auto flat = fixture.flat.get();
	flat->setGeometry(0, 0, side, side);
	flat->paintOn([=](QPainter &p) {
		p.fillRect(flat->rect(), st::windowBgOver);
	});
	flat->show();

	// A child that paints nothing at all over that uniform fill: its own
	// rect maps inside the window, so the helper resolves it and declines
	// the frame on blankness alone, which is the branch under measurement.
	fixture.transparent = base::make_unique_q<Ui::RpWidget>(flat);
	const auto transparent = fixture.transparent.get();
	transparent->setGeometry(flat->rect());
	transparent->show();
	return true;
}

// Qt::WA_DontShowOnScreen is what makes a second top level safe here.
// QWidgetPrivate::show_sys() takes an early return for it
// (qwidget.cpp:7886-7903): it marks the widget mapped and never calls
// window->setVisible(true), so the QWidgetWindow QWidgetPrivate::create()
// made is never shown and can take neither activation nor focus from the
// primary window. Test::ResolveActivationWindow (test_widgets.cpp:89-102)
// only ever selects a top level whose QWindow::isVisible() is true, so it
// cannot select this one either. QWidget::show() still sets
// WA_WState_Visible on the widget and on its children, which is all
// ReadViaWindow's visibility gate asks for, and the reading takes no grab.
// Qt::WA_QuitOnClose is cleared because ~QWidget still runs
// close_helper(CloseNoEvent) for a created, visible top level
// (qwidget.cpp:1468-1470), and that path can reach
// QGuiApplicationPrivate::emitLastWindowClosed() and maybeQuit()
// (qwidget.cpp:8316-8326); the primary window keeps that branch false
// today, but a fixture must not depend on a neighbour to avoid quitting
// the run.
[[nodiscard]] base::unique_qptr<Ui::RpWidget> BuildOffscreenWindow(
		QPointer<QWidget> &child) {
	auto result = base::make_unique_q<Ui::RpWidget>(nullptr);
	const auto window = result.get();
	window->setAttribute(Qt::WA_DontShowOnScreen);
	window->setAttribute(Qt::WA_QuitOnClose, false);
	const auto side = st::defaultActiveButton.height * kFlatBands;
	window->resize(side, side);
	PaintTwoTone(window);
	const auto inner = Ui::CreateChild<Ui::RpWidget>(window);
	inner->setGeometry(0, side / 4, side, side / 2);
	inner->show();
	window->show();
	child = inner;
	return result;
}

} // namespace

void AppendCaptureViaWindowSelfTest(not_null<Runner*> runner) {
	struct State {
		Fixture fixture;
		QString bareReason;
		QString viaDetails;
		int failuresBefore = 0;
		int failuresAfter = 0;
		bool built = false;
		bool bareAccepted = false;
		bool viaBlank = false;
		bool saved = false;
		bool blankSaved = false;
		WindowMappedCapture retained;
		WindowActivation activationBefore;
		WindowActivation activationDuring;
		WindowActivation activationAfter;
		QString retainedWindowText;
		QString retainedIdentityBefore;
		QRect retainedMappedBefore;
		int retainedFailuresBefore = 0;
		int retainedFailuresAfter = 0;
		bool retainedBuilt = false;
		bool retainedResolvedBefore = false;
		bool retainedResolvedSameTurn = true;
		bool retainedReadyBefore = false;
	};
	// Leaked on purpose, the way the harness's other self-tests leak theirs:
	// the stages outlive this call. The teardown stage releases the fixture,
	// after which the State holds nothing but QStrings and PODs - no rpl
	// subscription to anything the session owns.
	const auto state = new State();
	const auto details = [=] {
		return FixtureDetails(state->fixture);
	};

	runner->add({
		.name = u"via-window self-test: the bare grab is refused where the "
			"window-mapped crop is not"_q,
		.run = [=] {
			state->built = BuildFixture(state->fixture);
			Check(
				state->built,
				u"fixture gate: the self-test fixture was built"_q,
				state->built
					? QString()
					: u"Core::App().activePrimaryWindow() is null, or "
						u"Ui::Toast::Show answered no instance"_q);
			const auto toast = state->fixture.toastWidget.data();
			if (!state->built || !toast) {
				return;
			}
			// Every reading below is taken here, in the one turn the failing
			// shape exists: the toast's shown level is still 0, so its
			// paintEvent is on its opacity branch and the bare grab is
			// blank. A tick later the fade-in has moved and the contrast is
			// gone, which is why the assertions read snapshots instead of
			// re-measuring in |then|.
			auto probe = PreparedWidgetCapture();
			state->bareAccepted = probe.prepare(toast);
			state->bareReason = probe.pendingReason();
			state->viaBlank = LooksBlank(GrabViaWindow(toast));
			state->viaDetails = ViaWindowDetails(toast);
			state->saved = CaptureViaWindow(
				toast,
				u"toast_via_window_open"_q);
		},
		.then = [=] {
			const auto toast = state->fixture.toastWidget.data();
			if (!state->built || !toast) {
				return;
			}
			Check(
				!toast->testAttribute(Qt::WA_OpaquePaintEvent)
					&& !toast->testAttribute(Qt::WA_NoSystemBackground),
				u"the premise holds: the toast carries neither "
				"Qt::WA_OpaquePaintEvent nor Qt::WA_NoSystemBackground, so "
				"it paints no opaque background of its own"_q,
				details());
			Check(
				!state->bareAccepted && !state->bareReason.isEmpty(),
				u"a bare prepared grab of the fade-in wrapper is refused in "
				"the turn it is created, so the failing shape is reached "
				"rather than described"_q,
				state->bareReason);
			Check(
				!state->viaBlank && state->saved,
				u"the window-mapped crop of the very same rect, in the very "
				"same turn, is not blank and is saved"_q,
				state->viaDetails);
		},
	});

	runner->add({
		.name = u"via-window self-test: the text oracle, not the pixels, "
			"decides"_q,
		.until = [=] {
			const auto toast = state->fixture.toastWidget.data();
			return !state->built || !toast || ViaWindowReady(toast);
		},
		.then = [=] {
			const auto toast = state->fixture.toastWidget.data();
			if (!state->built || !toast) {
				return;
			}
			auto names = QStringList();
			for (const auto label : FindAll<Ui::FlatLabel>(toast)) {
				names.push_back(label->accessibilityName());
			}
			const auto joined = names.join(u" | "_q);
			Check(
				joined.contains(kToastText),
				u"the decisive oracle for a widget that paints no opaque "
				"background of its own is textual: the joined "
				"accessibilityName of its Ui::FlatLabels carries the text "
				"whatever the pixels are doing"_q,
				u"labels=%1 joined=\"%2\""_q.arg(names.size()).arg(joined));
			Check(
				CaptureViaWindow(toast, u"toast_via_window_settled"_q),
				u"the window-mapped capture saves the settled toast too, as "
				"the corroborating evidence beside that oracle"_q,
				details());
			auto probe = PreparedWidgetCapture();
			const auto accepted = probe.prepare(toast);
			Note(u"via-window self-test: the bare prepared grab of the "
				u"toast now reports accepted=%1%2 - recorded and not "
				u"asserted, because once the fade-in has finished the toast "
				u"paints st::toastBg over almost its whole rect and "
				u"PreparedWidgetCapture accepts it, correctly: the failure "
				u"this helper answers exists only mid-fade"_q
				.arg(accepted ? 1 : 0)
				.arg(accepted
					? QString()
					: u" - %1"_q.arg(probe.pendingReason())));
		},
		.timeoutDetails = details,
	});

	runner->add({
		.name = u"via-window self-test: a blank frame is a Note and never a "
			"FAIL"_q,
		.run = [=] {
			const auto transparent = state->fixture.transparent.get();
			if (!state->built || !transparent) {
				return;
			}
			// Both counts are taken around the one call, not compared
			// against a live count in |then|, so a Check that fails there
			// cannot move the number this Check is about.
			state->failuresBefore = FailureCount();
			state->blankSaved = CaptureViaWindow(
				transparent,
				u"via_window_blank"_q);
			state->failuresAfter = FailureCount();
		},
		.then = [=] {
			const auto transparent = state->fixture.transparent.get();
			if (!state->built || !transparent) {
				return;
			}
			Check(
				!state->blankSaved,
				u"a transparent child over a uniformly filled region "
				"resolves and is still declined, so the helper judges the "
				"frame and not merely the geometry"_q,
				ViaWindowDetails(transparent));
			Check(
				state->failuresAfter == state->failuresBefore,
				u"declining that blank frame logged a Note and never a "
				"FAIL, which is the contract a fade-in wrapper's capture "
				"rests on"_q,
				u"failures before=%1 after=%2"_q
					.arg(state->failuresBefore)
					.arg(state->failuresAfter));
		},
	});

	runner->add({
		.name = u"via-window self-test: refusal"_q,
		.run = [=] {
			if (!state->built) {
				return;
			}
			const auto window = Core::App().activePrimaryWindow();
			const auto top = window ? window->widget().get() : nullptr;
			const auto before = FailureCount();
			const auto missing = ReadViaWindow(nullptr);
			const auto missingDetails = ViaWindowDetails(nullptr);
			const auto missingReady = ViaWindowReady(nullptr);
			const auto owner = top
				? ReadViaWindow(top)
				: WindowMappedCapture();
			const auto after = FailureCount();
			Note(u"via-window self-test: every refusal below is observed "
				u"through ReadViaWindow, ViaWindowReady and "
				u"ViaWindowDetails, which log nothing; CaptureViaWindow "
				u"turns each of them into a loud Fail, which is why the "
				u"refusal stage never goes through it"_q);
			Check(
				!missing.resolved()
					&& (missing.window == nullptr)
					&& missing.refusal.contains(u"no widget was handed"_q)
					&& missingDetails.contains(missing.refusal),
				u"a null target is refused by name, never grabbed and never "
				"answered with a null the caller would dereference"_q,
				missing.refusal);
			Check(
				!missingReady,
				u"an unresolved target is never ready, so a poll around it "
				"ends in a named stage timeout rather than in a grab of "
				"something else"_q,
				missingDetails);
			Check(
				top
					&& !owner.resolved()
					&& owner.refusal.contains(u"its own window"_q),
				u"a widget that is its own window is refused with the "
				"reason, which is the mechanism that keeps this helper off "
				"a Ui::PopupMenu"_q,
				owner.refusal);
			Check(
				after == before,
				u"none of these refusals logged a failure: a refusal is a "
				"returned value the caller decides about"_q,
				u"failures before=%1 after=%2"_q.arg(before).arg(after));
		},
	});

	runner->add({
		.name = u"via-window self-test: a retained reading reports its own "
			"window's destruction"_q,
		.run = [=] {
			// The whole fixture lives and dies inside this one turn: it is
			// built, read and destroyed here, so nothing it creates can
			// reach a neighbouring stage. The activation reading is taken
			// three times - before the fixture exists, while it is shown,
			// and after it is gone - because a top level that stole
			// activation would do it while it was up, and a before/after
			// pair alone could not see that. It is taken through
			// ReadWindowActivation, which is pure and does not move
			// ActivationAttempts() - only ForceWindowActive does
			// (test_widgets.cpp:385-401) - and the window activation
			// self-test asserts deltas of that counter, so this stage must
			// never call ForceWindowActive or ClearWindowActive.
			state->activationBefore = ReadWindowActivation();
			state->retainedFailuresBefore = FailureCount();
			auto child = QPointer<QWidget>();
			auto window = BuildOffscreenWindow(child);
			state->retainedBuilt = (child != nullptr);
			if (state->retainedBuilt) {
				state->retained = ReadViaWindow(child.data());
				state->retainedResolvedBefore = state->retained.resolved();
				state->retainedIdentityBefore = state->retained.identity;
				state->retainedMappedBefore = state->retained.mapped;
				if (state->retainedResolvedBefore) {
					state->retainedWindowText = WidgetDescription(
						state->retained.window.data());
					state->retainedReadyBefore = ViaWindowReady(child.data());
				}
			}
			state->activationDuring = ReadWindowActivation();
			window = nullptr;
			state->retainedResolvedSameTurn = state->retained.resolved();
			state->activationAfter = ReadWindowActivation();
			state->retainedFailuresAfter = FailureCount();
			Check(
				state->retainedBuilt && state->retainedResolvedBefore,
				u"fixture gate: an offscreen top level and its visible "
				"child were built and ReadViaWindow resolved the child "
				"against that window"_q,
				u"built=%1 resolved=%2 identity=%3 window=%4 mapped=%5,%6 "
				"%7x%8 readyBeforeDestroy=%9"_q
					.arg(state->retainedBuilt ? 1 : 0)
					.arg(state->retainedResolvedBefore ? 1 : 0)
					.arg(
						state->retainedIdentityBefore,
						state->retainedWindowText)
					.arg(state->retainedMappedBefore.x())
					.arg(state->retainedMappedBefore.y())
					.arg(state->retainedMappedBefore.width())
					.arg(state->retainedMappedBefore.height())
					.arg(state->retainedReadyBefore ? 1 : 0));
		},
		.then = [=] {
			if (!state->retainedBuilt || !state->retainedResolvedBefore) {
				return;
			}
			Check(
				!state->retainedResolvedSameTurn
					&& !state->retained.resolved()
					&& (state->retained.window == nullptr),
				u"the retained reading answers resolved()==false once its "
				"own window is destroyed, in that same turn and again a "
				"runner tick later in this |then|, instead of handing back "
				"a stale pointer"_q,
				u"resolvedBefore=%1 resolvedSameTurn=%2 resolvedLater=%3 "
				"window=[%4]"_q
					.arg(state->retainedResolvedBefore ? 1 : 0)
					.arg(state->retainedResolvedSameTurn ? 1 : 0)
					.arg(state->retained.resolved() ? 1 : 0)
					.arg(state->retainedWindowText));
			Check(
				(state->retained.identity == state->retainedIdentityBefore)
					&& (state->retained.mapped
						== state->retainedMappedBefore)
					&& !state->retained.identity.isEmpty()
					&& !state->retained.mapped.isEmpty()
					&& state->retained.refusal.isEmpty(),
				u"its identity, mapped rect and refusal are values and are "
				"unchanged and printable after the window is gone, which is "
				"what such a reading has left to report"_q,
				u"identity=%1 mapped=%2,%3 %4x%5 refusal=[%6]"_q
					.arg(state->retained.identity)
					.arg(state->retained.mapped.x())
					.arg(state->retained.mapped.y())
					.arg(state->retained.mapped.width())
					.arg(state->retained.mapped.height())
					.arg(state->retained.refusal));
			Check(
				(state->activationDuring.focusWindowSet
					== state->activationBefore.focusWindowSet)
					&& (state->activationDuring.activeWindow
						== state->activationBefore.activeWindow)
					&& (state->activationAfter.focusWindowSet
						== state->activationBefore.focusWindowSet)
					&& (state->activationAfter.activeWindow
						== state->activationBefore.activeWindow)
					&& (state->activationAfter.attempts
						== state->activationBefore.attempts),
				u"the offscreen fixture took no activation from the primary "
				"window while it was up and gave none back when it went, "
				"and moved no activation attempt counter, so a neighbouring "
				"activation-sensitive stage is measurably undisturbed"_q,
				u"before=[%1] during=[%2] after=[%3]"_q
					.arg(
						WindowActivationDetails(state->activationBefore),
						WindowActivationDetails(state->activationDuring),
						WindowActivationDetails(state->activationAfter)));
			Check(
				state->retainedFailuresAfter
					== state->retainedFailuresBefore,
				u"building, reading and destroying that fixture logged no "
				"failure of its own: the reading reports the destruction as "
				"a returned value"_q,
				u"failures before=%1 after=%2"_q
					.arg(state->retainedFailuresBefore)
					.arg(state->retainedFailuresAfter));
		},
	});

	runner->add({
		.name = u"via-window self-test: teardown"_q,
		.run = [=] {
			// Last on purpose. A timed-out stage or the watchdog skips every
			// stage after it, so anything still held here would outlive the
			// run: the toast was shown infinite, so no expiry timer will
			// ever take it down, and the three fixture widgets are parented
			// into the primary window. Instance::hide() is the product's own
			// immediate path - _widget->hide(); _widget->deleteLater();
			// (ui/toast/toast.cpp:116-119) - rather than hideAnimated(),
			// whose fade-out animation the harness's drained loop starves.
			if (const auto instance = state->fixture.toast.get()) {
				instance->hide();
			}
			state->fixture.transparent = nullptr;
			state->fixture.flat = nullptr;
			state->fixture.backdrop = nullptr;
			state->fixture.toastWidget = nullptr;
			Note(u"via-window self-test: fixture released, backdrop=%1 "
				"flat=%2 transparent=%3 toast=%4"_q
				.arg(state->fixture.backdrop ? 1 : 0)
				.arg(state->fixture.flat ? 1 : 0)
				.arg(state->fixture.transparent ? 1 : 0)
				.arg(state->fixture.toast ? 1 : 0));
		},
	});
}

} // namespace Test

#endif // _DEBUG
