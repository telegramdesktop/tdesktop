/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_marking_read.h"

#include "api/api_updates.h"
#include "core/application.h"
#include "dialogs/dialogs_widget.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "test/test_capture.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "ui/effects/animation_value.h"
#include "window/window_controller.h"

#include <QtGui/QWindow>

namespace Test {
namespace {

[[nodiscard]] bool LeverHeld(const MarkingReadReading &reading) {
	return reading.refusal.isEmpty()
		&& !reading.markingAsRead
		&& !reading.isHidden
		&& reading.isMinimized;
}

[[nodiscard]] QString BoundRefusal(const MarkingReadReading &reading) {
	const auto why = [&] {
		if (reading.markingAsRead) {
			return u"still marking messages read"_q;
		} else if (reading.isHidden) {
			return u"the window was hidden"_q;
		} else if (!reading.isMinimized) {
			return u"the window was not minimized"_q;
		}
		return u"the lever did not hold"_q;
	}();
	return u"minimize lever failed within the bound: %1"_q.arg(why);
}

void RefreshActive(not_null<Window::Controller*> controller) {
	const auto widget = controller->widget().get();
	ForceWindowActive(widget);
	widget->updateIsActive();
}

void RefreshIdleOnce(
		not_null<Window::Controller*> controller,
		bool &done) {
	if (done) {
		return;
	}
	const auto session = controller->maybeSession();
	if (!session || !session->updates().isIdle()) {
		return;
	}
	Core::App().updateNonIdle();
	done = true;
}

void Arrange(
		not_null<Window::Controller*> controller,
		bool &idleRefreshed) {
	const auto widget = controller->widget().get();
	if (widget->ui_isLayerShown()) {
		widget->ui_hideSettingsAndLayer(anim::type::instant);
	}
	if (widget->isHidden() || widget->isMinimized()) {
		controller->activate();
	}
	RefreshActive(controller);
	RefreshIdleOnce(controller, idleRefreshed);
}

[[nodiscard]] QString IdleSuffix(Window::Controller *controller) {
	const auto session = controller ? controller->maybeSession() : nullptr;
	const auto idle = !session || session->updates().isIdle();
	return u" idle=%1"_q.arg(idle ? 1 : 0);
}

struct State {
	Window::Controller *controller = nullptr;
	bool built = false;
	bool idleRefreshed = false;
	bool leverApplied = false;
	bool succeeded = false;
	bool reactivationMarking = false;
	bool relevered = false;
	crl::time controlStarted = 0;
	crl::time decideStarted = 0;
	crl::time reapplyStarted = 0;
	MarkingReadReading control;
	MarkingReadReading applied;
	MarkingReadReading reactivated;
	MarkingReadReading reapplied;
	MarkingReadReading refused;
	MarkingReadReading restored;
	PreparedWidgetCapture capture;
};

[[nodiscard]] QString FixtureSkip(const std::shared_ptr<State> &state) {
	return state->built ? QString() : u"fixture gate failed"_q;
}

[[nodiscard]] QString DecidingSkip(const std::shared_ptr<State> &state) {
	if (!state->built) {
		return u"fixture gate failed"_q;
	} else if (state->control.markingAsRead) {
		return QString();
	}
	const auto why = state->control.screenLocked
		? u"the console is locked"_q
		: !state->control.exposed
		? u"the window is not exposed"_q
		: u"control markingAsRead was false"_q;
	return u"deciding half does not apply: %1 - %2"_q.arg(
		why,
		MarkingReadDetails(state->control));
}

[[nodiscard]] QString ReactivationSkip(const std::shared_ptr<State> &state) {
	if (const auto fixture = FixtureSkip(state); !fixture.isEmpty()) {
		return fixture;
	} else if (!state->control.markingAsRead) {
		return u"control markingAsRead was false"_q;
	} else if (!state->succeeded) {
		return u"the helper did not hold"_q;
	}
	return QString();
}

[[nodiscard]] QString AgainSkip(const std::shared_ptr<State> &state) {
	if (const auto prior = ReactivationSkip(state); !prior.isEmpty()) {
		return prior;
	} else if (!state->reactivationMarking) {
		return u"activate did not make markingAsRead true"_q;
	}
	return QString();
}

[[nodiscard]] QString CaptureSkip(const std::shared_ptr<State> &state) {
	if (const auto prior = AgainSkip(state); !prior.isEmpty()) {
		return prior;
	} else if (!state->relevered) {
		return u"the helper did not hold after activate"_q;
	}
	return QString();
}

} // namespace

MarkingReadReading ReadMainWindowMarking(Window::Controller *controller) {
	auto result = MarkingReadReading();
	result.screenLocked = Core::App().screenIsLocked();
	if (!controller) {
		result.refusal = u"no main window to keep from marking "
			u"messages read"_q;
		result.activation.identity = u"none - no main window"_q;
		return result;
	}
	const auto widget = controller->widget().get();
	result.activation = ReadWindowActivation(widget);
	result.isActive = widget->isActive();
	result.isMinimized = widget->isMinimized();
	result.isHidden = widget->isHidden();
	const auto handle = widget->windowHandle();
	result.exposed = handle && handle->isExposed();
	// markingAsRead() dereferences windowHandle() once the hidden and
	// minimized terms have passed.
	if (!handle && !result.isHidden && !result.isMinimized) {
		result.markingAsRead = false;
	} else {
		result.markingAsRead = widget->markingAsRead();
	}
	return result;
}

MarkingReadReading KeepMainWindowNotMarkingRead(
		Window::Controller *controller,
		crl::time waited) {
	auto reading = ReadMainWindowMarking(controller);
	if (!reading.refusal.isEmpty()) {
		return reading;
	}
	const auto widget = controller->widget().get();
	// Not Controller::minimize(): WorkMode::TrayOnly hides to the tray,
	// and a hidden window fails the capture check this lever has to keep.
	widget->setWindowState(widget->windowState() | Qt::WindowMinimized);
	reading = ReadMainWindowMarking(controller);
	if (LeverHeld(reading)) {
		return reading;
	} else if (waited >= kNotMarkingReadBound) {
		reading.refusal = BoundRefusal(reading);
	}
	return reading;
}

QString MarkingReadDetails(const MarkingReadReading &reading) {
	const auto line = u"marking read: markingAsRead=%1 isActive=%2 "
		u"isMinimized=%3 isHidden=%4 exposed=%5 screenLocked=%6 | %7"_q
		.arg(reading.markingAsRead ? 1 : 0)
		.arg(reading.isActive ? 1 : 0)
		.arg(reading.isMinimized ? 1 : 0)
		.arg(reading.isHidden ? 1 : 0)
		.arg(reading.exposed ? 1 : 0)
		.arg(reading.screenLocked ? 1 : 0)
		.arg(WindowActivationDetails(reading.activation));
	return reading.refusal.isEmpty()
		? line
		: (line + u" - %1"_q.arg(reading.refusal));
}

void AppendMainWindowNotMarkingReadSelfTest(not_null<Runner*> runner) {
	const auto state = std::make_shared<State>();
	runner->onFinish([=] {
		if (state->leverApplied && state->controller) {
			state->controller->activate();
		}
	});

	runner->add({
		.name = u"not-marking-read self-test: fixture"_q,
		.run = [=] {
			state->controller = Core::App().activePrimaryWindow();
			const auto widget = state->controller
				? state->controller->widget().get()
				: nullptr;
			state->built = widget
				&& state->controller->sessionController()
				&& widget->sessionContent();
			auto missing = QString();
			if (!state->controller) {
				missing = u"activePrimaryWindow is null"_q;
			} else if (!state->controller->sessionController()) {
				missing = u"sessionController is null"_q;
			} else if (!widget->sessionContent()) {
				missing = u"sessionContent is null"_q;
			}
			Check(
				state->built,
				u"fixture gate: the self-test has a session main window"_q,
				state->built ? QString() : missing);
		},
	});

	runner->add({
		.name = u"not-marking-read self-test: null controller refuses"_q,
		.run = [=] {
			state->refused = KeepMainWindowNotMarkingRead(
				nullptr,
				kNotMarkingReadBound);
		},
		.then = [=] {
			Check(
				state->refused.refusal.contains(u"no main window"_q)
					&& !state->refused.markingAsRead,
				u"null controller is a named refusal"_q,
				MarkingReadDetails(state->refused));
		},
	});

	runner->add({
		.name = u"not-marking-read self-test: control"_q,
		.skipReason = [=] { return FixtureSkip(state); },
		.run = [=] {
			state->controlStarted = crl::now();
			Arrange(state->controller, state->idleRefreshed);
		},
		.until = [=] {
			// _isActive stays stale until updateIsActive(). This is the
			// same bounded-wait re-assert as ForceWindowActive, not the
			// minimize lever.
			RefreshActive(state->controller);
			state->control = ReadMainWindowMarking(state->controller);
			if (state->control.markingAsRead
				|| state->control.screenLocked) {
				return true;
			}
			return !state->control.exposed
				&& (crl::now() - state->controlStarted
					>= kNotMarkingReadBound);
		},
		.then = [=] {
			Note(u"not-marking-read self-test: control %1%2"_q.arg(
				MarkingReadDetails(state->control),
				IdleSuffix(state->controller)));
			if (!state->control.markingAsRead) {
				return;
			}
			Check(
				state->control.markingAsRead,
				u"control reads markingAsRead before the helper"_q,
				MarkingReadDetails(state->control));
			state->applied = KeepMainWindowNotMarkingRead(
				state->controller,
				crl::time(0));
			state->leverApplied = true;
		},
		.timeout = kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return MarkingReadDetails(state->control)
				+ IdleSuffix(state->controller);
		},
	});

	runner->add({
		.name = u"not-marking-read self-test: helper"_q,
		.skipReason = [=] { return DecidingSkip(state); },
		.run = [=] { state->decideStarted = crl::now(); },
		.until = [=] {
			state->applied = KeepMainWindowNotMarkingRead(
				state->controller,
				crl::now() - state->decideStarted);
			return !state->applied.refusal.isEmpty()
				|| LeverHeld(state->applied);
		},
		.then = [=] {
			const auto held = LeverHeld(state->applied);
			Check(
				held,
				u"helper leaves the main window minimized and not "
				u"marking messages read"_q,
				MarkingReadDetails(state->applied));
			state->succeeded = held;
		},
		.timeout = kNotMarkingReadBound + crl::time(2000),
		.timeoutDetails = [=] {
			return MarkingReadDetails(state->applied);
		},
	});

	runner->add({
		.name = u"not-marking-read self-test: activate undoes the lever"_q,
		.skipReason = [=] { return ReactivationSkip(state); },
		.run = [=] {
			state->controller->activate();
			RefreshActive(state->controller);
			auto again = false;
			RefreshIdleOnce(state->controller, again);
			state->reactivated = ReadMainWindowMarking(state->controller);
		},
		.until = [=] {
			RefreshActive(state->controller);
			state->reactivated = ReadMainWindowMarking(state->controller);
			return state->reactivated.markingAsRead;
		},
		.then = [=] {
			Check(
				state->reactivated.markingAsRead,
				u"activate undoes the minimize lever"_q,
				MarkingReadDetails(state->reactivated));
			if (!state->reactivated.markingAsRead) {
				return;
			}
			state->reactivationMarking = true;
			state->reapplied = KeepMainWindowNotMarkingRead(
				state->controller,
				crl::time(0));
			state->leverApplied = true;
		},
		.timeout = kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return MarkingReadDetails(state->reactivated);
		},
	});

	runner->add({
		.name = u"not-marking-read self-test: helper again"_q,
		.skipReason = [=] { return AgainSkip(state); },
		.run = [=] { state->reapplyStarted = crl::now(); },
		.until = [=] {
			state->reapplied = KeepMainWindowNotMarkingRead(
				state->controller,
				crl::now() - state->reapplyStarted);
			return !state->reapplied.refusal.isEmpty()
				|| LeverHeld(state->reapplied);
		},
		.then = [=] {
			const auto held = LeverHeld(state->reapplied);
			Check(
				held,
				u"calling the helper again leaves the window not "
				u"marking messages read"_q,
				MarkingReadDetails(state->reapplied));
			state->relevered = held;
		},
		.timeout = kNotMarkingReadBound + crl::time(2000),
		.timeoutDetails = [=] {
			return MarkingReadDetails(state->reapplied);
		},
	});

	runner->add({
		.name = u"not-marking-read self-test: capture"_q,
		.skipReason = [=] { return CaptureSkip(state); },
		.until = [=] {
			state->restored = ReadMainWindowMarking(state->controller);
			if (!LeverHeld(state->restored)) {
				state->capture.invalidate(
					MarkingReadDetails(state->restored));
				return false;
			}
			const auto root = state->controller->widget().get();
			for (const auto dialogs : FindVisible<Dialogs::Widget>(root)) {
				if (!dialogs->size().isEmpty()
					&& state->capture.prepare(dialogs)) {
					return true;
				}
			}
			if (const auto main = root->sessionContent()) {
				if (main->isVisible()
					&& !main->size().isEmpty()
					&& state->capture.prepare(main)) {
					return true;
				}
			}
			if (state->capture.pendingReason().isEmpty()) {
				state->capture.invalidate(
					u"no visible main-window widget"_q);
			}
			return false;
		},
		.then = [=] {
			const auto saved = state->capture.save(
				u"main_window_not_marking_read"_q);
			Check(
				saved,
				u"capture of a main-window widget is accepted while "
				u"the lever holds"_q,
				MarkingReadDetails(state->restored));
		},
		.timeout = kDefaultStageTimeout,
		.timeoutDetails = [=] {
			const auto pending = state->capture.pendingReason();
			return pending.isEmpty()
				? MarkingReadDetails(state->restored)
				: pending;
		},
	});

	runner->add({
		.name = u"not-marking-read self-test: restore"_q,
		.skipReason = [=] {
			return state->controller
				? QString()
				: u"no main window to show again"_q;
		},
		.run = [=] {
			state->controller->activate();
			RefreshActive(state->controller);
		},
		.until = [=] {
			const auto widget = state->controller->widget().get();
			state->restored = ReadMainWindowMarking(state->controller);
			return !widget->isMinimized()
				&& !widget->isHidden()
				&& widget->isVisible();
		},
		.then = [=] {
			Check(
				!state->restored.isMinimized && !state->restored.isHidden,
				u"the main window is shown again"_q,
				MarkingReadDetails(state->restored));
		},
		.timeout = kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return MarkingReadDetails(state->restored);
		},
	});
}

} // namespace Test

#endif // _DEBUG
