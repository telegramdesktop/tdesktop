/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QString>

#include <memory>

// The widget conformance runner's action stages: each manifest action of an
// auto run performed with real input through the chat's own HistoryInner,
// the compose field, the app's palette and the list's scroll, about 350 ms
// apart, between the run's first result and its wait for done. Internal to
// test_widget_conformance.cpp; reference:
// docs/helpers/test_widget_conformance.md.

namespace Test {
class Runner;
} // namespace Test

namespace Test::Conformance {

struct ConformanceRun;
struct ConformanceState;

// Before the run's focus and injection: for a run with a visible action,
// registers its filler records with the fixture at once and appends the
// stage injecting them, local outgoing text messages above the widget
// that give the list room to scroll the card off screen. Appends nothing
// for any other run.
void AppendRunFillers(
	not_null<Runner*> runner,
	const std::shared_ptr<ConformanceState> &state,
	int index);

// Right after the run's first result: one group of stages per manifest
// action, in order, then the stage closing the last action. A run that
// has no actions gets none. An action is performed only on a widget whose
// first wait ended awaiting and that is still live; every other one keeps
// a named refusal, never a skip, and only an action this build cannot
// perform at all is skipped, with its reason.
void AppendRunActions(
	not_null<Runner*> runner,
	const std::shared_ptr<ConformanceState> &state,
	int index);

// With the run's widget message: destroys the fillers that were injected.
void RemoveRunFillers(ConformanceState &state, ConformanceRun &run);

// From Runner::onFinish, before the fixture's release: aborts every pointer
// sequence still running, which releases its held buttons and leaves the
// list.
void AbortRunActions(ConformanceState &state, const QString &reason);

} // namespace Test::Conformance
