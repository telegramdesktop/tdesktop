/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "test/test_popup_hold.h"

#include "core/sandbox.h"

#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "base/platform/base_platform_info.h"
#include "test/test_agent.h"
#include "test/test_log.h"
#include "test/test_runner.h"

#include <QtWidgets/QApplication>

namespace Test {
namespace {

struct HoldState {
	PopupHoldReading reading;
	bool finishRegistered = false;
};

[[nodiscard]] HoldState &Hold() {
	static auto result = HoldState();
	return result;
}

} // namespace

void ArmPopupHold(not_null<Runner*> runner, const QString &label) {
	if (!Active()) {
		return;
	}
	auto &hold = Hold();
	if (hold.reading.armed) {
		Note(u"POPUP_HOLD: already armed label=%1 requested=%2 "
			u"stopped=%3"_q.arg(
				hold.reading.label,
				label,
				QString::number(hold.reading.stopped)));
		return;
	}
	hold.reading = PopupHoldReading{
		.label = label,
		.armed = true,
	};
	Note(u"POPUP_HOLD: armed label=%1 windows=%2"_q.arg(
		label,
		Platform::IsWindows() ? u"1"_q : u"0"_q));
	if (!hold.finishRegistered) {
		hold.finishRegistered = true;
		runner->onFinish([] {
			Hold().finishRegistered = false;
			ReleasePopupHold(u"finish"_q);
		});
	}
}

bool ReleasePopupHold(const QString &by) {
	auto &reading = Hold().reading;
	if (!reading.armed) {
		Note(u"POPUP_HOLD: release skipped by=%1 - not armed"_q.arg(by));
		return false;
	}
	reading.armed = false;
	Note(u"POPUP_HOLD: released by=%1 label=%2 stopped=%3"_q.arg(
		by,
		reading.label,
		QString::number(reading.stopped)));
	return true;
}

PopupHoldReading ReadPopupHold() {
	return Hold().reading;
}

// Called by Core::Sandbox::notify on the main thread for every
// QEvent::ApplicationDeactivate; true swallows it, so QApplication::notify
// never reaches closeAllPopups().
bool KeepPopupsOnDeactivate() {
	auto &reading = Hold().reading;
	if (!reading.armed
		|| !Platform::IsWindows()
		|| !QApplication::activePopupWidget()) {
		return false;
	}
	++reading.stopped;
	Note(u"POPUP_HOLD: stopped n=%1 label=%2"_q.arg(
		QString::number(reading.stopped),
		reading.label));
	return true;
}

} // namespace Test

#else // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS

namespace Test {

bool KeepPopupsOnDeactivate() {
	return false;
}

} // namespace Test

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
