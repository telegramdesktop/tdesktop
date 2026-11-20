/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QString>

#include <optional>

namespace Test {

class Runner;

// Sets the application's inactive or active state - the one
// QGuiApplication::applicationState() returns and
// Core::App().appDeactivatedValue() follows - through
// QGuiApplicationPrivate::setApplicationState(), and reads it back. Qt
// sends QEvent::ApplicationActivate or QEvent::ApplicationDeactivate and a
// QApplicationStateChangeEvent and emits applicationStateChanged() inside
// that call, so every direct subscriber has run when
// InjectApplicationActive returns; a reaction one of them queues runs on a
// later turn. The focus window and window activation are not touched.
//
// The reading. InjectApplicationActive fills it after its call;
// ReadApplicationState injects nothing, is legal in a stage's until, and
// carries no request and no refusal - never read it as "injected".
//
// Refusals are returned, never logged, and the first token of |refusal|
// names them:
// - unsupported: built with DESKTOP_APP_DISABLE_PRIVATE_QT_API, so there is
//   no setter; nothing was set;
// - not-read-back: after the call |state| or |deactivated| is not the
//   requested one.
// |refusal| is empty exactly when the requested state is in force at
// return. A request for the state already in force emits nothing and
// succeeds.
//
// The state stays until something changes it: with no focus window the
// next focus change to a window - Test::ForceWindowActive() included -
// returns it to active. Re-read it with ReadApplicationState() at the end
// of a hold; the reference lists everything that ends it.
// Reference: docs/helpers/test_application_state.md.
struct ApplicationStateReading {
	std::optional<bool> requestedActive; // empty for a pure reading
	Qt::ApplicationState state = Qt::ApplicationInactive;
	std::optional<bool> deactivated; // first appDeactivatedValue(), or empty
	bool focusWindowSet = false; // QGuiApplication::focusWindow() != nullptr
	QString refusal;
};

[[nodiscard]] ApplicationStateReading ReadApplicationState();
[[nodiscard]] ApplicationStateReading InjectApplicationActive(bool active);

// "application state: requested=<active|inactive|none>
// state=<active|inactive|hidden|suspended> appDeactivated=<1|0|none>
// focusWindow=<1|0>", followed by " - <refusal>" when refused.
[[nodiscard]] QString ApplicationStateText(
	const ApplicationStateReading &reading);

// The helper measuring itself, with no session, chats list, window or
// network. One stage, in one turn: it prints the CONSOLE_LOCK row, takes
// the found reading and an active baseline, attaches its own
// appDeactivatedValue() subscriber, injects inactive and active again,
// detaches, and restores the state it found. Its then checks each read-back,
// that the subscriber started from false and received exactly one true and
// one false, that the focus window was left as found, and the restore. The
// turn processes no queued platform event, so the verdict is the same on a
// locked and an unlocked console. Built with
// DESKTOP_APP_DISABLE_PRIVATE_QT_API every injection refuses unsupported
// and its checks FAIL with that refusal.
// Reference: docs/helpers/test_application_state.md.
void AppendApplicationStateSelfTest(not_null<Runner*> runner);

} // namespace Test
