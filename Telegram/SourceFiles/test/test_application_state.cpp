/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_application_state.h"

#include "core/application.h"
#include "test/test_console_lock.h"
#include "test/test_log.h"
#include "test/test_runner.h"

#include <QtGui/QGuiApplication>

#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
#include <private/qguiapplication_p.h>
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API

namespace Test {
namespace {

[[nodiscard]] QString StateName(Qt::ApplicationState state) {
	switch (state) {
	case Qt::ApplicationSuspended: return u"suspended"_q;
	case Qt::ApplicationHidden: return u"hidden"_q;
	case Qt::ApplicationInactive: return u"inactive"_q;
	case Qt::ApplicationActive: return u"active"_q;
	}
	Unexpected("State in Test::StateName.");
}

[[nodiscard]] QString FlagText(std::optional<bool> value) {
	if (!value) {
		return u"none"_q;
	}
	return *value ? u"1"_q : u"0"_q;
}

// Core::Application::appDeactivatedValue() starts with an rpl::single() of
// the current state, so its first value arrives inside the subscription.
[[nodiscard]] std::optional<bool> ReadDeactivated() {
	auto result = std::optional<bool>();
	auto lifetime = rpl::lifetime();
	Core::App().appDeactivatedValue(
	) | rpl::take(1) | rpl::on_next([&](bool deactivated) {
		result = deactivated;
	}, lifetime);
	return result;
}

} // namespace

ApplicationStateReading ReadApplicationState() {
	auto result = ApplicationStateReading();
	result.state = QGuiApplication::applicationState();
	result.deactivated = ReadDeactivated();
	result.focusWindowSet = (QGuiApplication::focusWindow() != nullptr);
	return result;
}

ApplicationStateReading InjectApplicationActive(bool active) {
#ifdef DESKTOP_APP_DISABLE_PRIVATE_QT_API
	auto result = ReadApplicationState();
	result.requestedActive = active;
	result.refusal = u"unsupported: built with "
		u"DESKTOP_APP_DISABLE_PRIVATE_QT_API, so there is no "
		u"QGuiApplicationPrivate::setApplicationState; nothing was set"_q;
	return result;
#else // DESKTOP_APP_DISABLE_PRIVATE_QT_API
	const auto requested = active
		? Qt::ApplicationActive
		: Qt::ApplicationInactive;
	// Where Qt's own focus-derived changes land, with no capability assert.
	QGuiApplicationPrivate::setApplicationState(requested);
	auto result = ReadApplicationState();
	result.requestedActive = active;
	if (result.state != requested
		|| result.deactivated != std::optional<bool>(!active)) {
		result.refusal = u"not-read-back: requested %1, "
			u"QGuiApplication::applicationState() reads %2 and "
			u"Core::App().appDeactivatedValue() reads %3"_q.arg(
				StateName(requested),
				StateName(result.state),
				FlagText(result.deactivated));
	}
	return result;
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API
}

QString ApplicationStateText(const ApplicationStateReading &reading) {
	const auto requested = !reading.requestedActive
		? u"none"_q
		: *reading.requestedActive
		? u"active"_q
		: u"inactive"_q;
	const auto line = u"application state: requested=%1 state=%2 "
		u"appDeactivated=%3 focusWindow=%4"_q.arg(
			requested,
			StateName(reading.state),
			FlagText(reading.deactivated),
			(reading.focusWindowSet ? u"1"_q : u"0"_q));
	return reading.refusal.isEmpty()
		? line
		: (line + u" - %1"_q.arg(reading.refusal));
}

namespace {

struct State {
	QString console;
	ApplicationStateReading found;
	ApplicationStateReading baseline;
	ApplicationStateReading inactive;
	ApplicationStateReading active;
	ApplicationStateReading restored;
	QString initial;
	QString inactiveValues;
	QString activeValues;
};

// An oracle on the reading's own fields, independent of the helper's
// refusal text.
[[nodiscard]] bool ReadsBack(
		const ApplicationStateReading &reading,
		bool active) {
	const auto requested = active
		? Qt::ApplicationActive
		: Qt::ApplicationInactive;
	return reading.refusal.isEmpty()
		&& (reading.state == requested)
		&& (reading.deactivated == std::optional<bool>(!active));
}

} // namespace

void AppendApplicationStateSelfTest(not_null<Runner*> runner) {
	const auto state = std::make_shared<State>();
	runner->add({
		.name = u"application state self-test: active, inactive and active "
			u"again in one turn"_q,
		.run = [=] {
			// Everything in this one turn: no queued platform event is
			// processed inside it, so OS focus cannot interleave.
			state->console = ConsoleLockText(ReadConsoleLock());
			state->found = ReadApplicationState();
			state->baseline = InjectApplicationActive(true);
			{
				auto values = QString();
				auto lifetime = rpl::lifetime();
				Core::App().appDeactivatedValue(
				) | rpl::on_next([&](bool deactivated) {
					values += deactivated ? u'1' : u'0';
				}, lifetime);
				state->initial = values;
				const auto beforeInactive = values.size();
				state->inactive = InjectApplicationActive(false);
				state->inactiveValues = values.mid(beforeInactive);
				const auto beforeActive = values.size();
				state->active = InjectApplicationActive(true);
				state->activeValues = values.mid(beforeActive);
			}
			state->restored = InjectApplicationActive(
				state->found.state == Qt::ApplicationActive);
		},
		.then = [=] {
			Note(u"application state self-test: %1"_q.arg(state->console));
			Note(u"application state self-test: found - %1"_q.arg(
				ApplicationStateText(state->found)));
			Check(
				ReadsBack(state->baseline, true),
				u"baseline: the application reads active before the "
				u"inactive change"_q,
				ApplicationStateText(state->baseline));
			Check(
				state->initial == u"0"_q,
				u"a subscriber of Core::App().appDeactivatedValue() starts "
				u"from false"_q,
				u"values=[%1]"_q.arg(state->initial));
			Check(
				ReadsBack(state->inactive, false),
				u"inactive read back: Qt::ApplicationInactive with "
				u"appDeactivatedValue() true"_q,
				ApplicationStateText(state->inactive));
			Check(
				state->inactiveValues == u"1"_q,
				u"the subscriber received the inactive change once, as "
				u"true"_q,
				u"values=[%1]"_q.arg(state->inactiveValues));
			Check(
				ReadsBack(state->active, true),
				u"active read back: Qt::ApplicationActive with "
				u"appDeactivatedValue() false"_q,
				ApplicationStateText(state->active));
			Check(
				state->activeValues == u"0"_q,
				u"the subscriber received the active change once, as "
				u"false"_q,
				u"values=[%1]"_q.arg(state->activeValues));
			const auto focus = state->found.focusWindowSet;
			const auto bit = [](bool value) {
				return value ? u"1"_q : u"0"_q;
			};
			Check(
				(state->baseline.focusWindowSet == focus)
					&& (state->inactive.focusWindowSet == focus)
					&& (state->active.focusWindowSet == focus),
				u"the injection leaves the focus window as it found it"_q,
				u"focusWindow found=%1 baseline=%2 inactive=%3 "
				u"active=%4"_q.arg(
					bit(focus),
					bit(state->baseline.focusWindowSet),
					bit(state->inactive.focusWindowSet),
					bit(state->active.focusWindowSet)));
			Check(
				state->restored.refusal.isEmpty(),
				u"the state the stage found is restored"_q,
				ApplicationStateText(state->restored));
		},
	});
}

} // namespace Test

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
