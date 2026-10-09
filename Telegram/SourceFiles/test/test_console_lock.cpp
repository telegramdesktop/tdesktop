/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_console_lock.h"

#ifdef Q_OS_WIN
#include "base/platform/base_platform_info.h"

#include <windows.h>
#include <WtsApi32.h>
#endif // Q_OS_WIN

namespace Test {
namespace {

#ifdef Q_OS_WIN
// What WTSGetActiveConsoleSessionId answers with no session attached.
constexpr auto kNoConsoleSession = DWORD(0xFFFFFFFF);
#endif // Q_OS_WIN

[[nodiscard]] QString ConsoleLockStateName(ConsoleLockState state) {
	switch (state) {
	case ConsoleLockState::NotApplicable: return u"not-applicable"_q;
	case ConsoleLockState::Unlocked: return u"unlocked"_q;
	case ConsoleLockState::Locked: return u"locked"_q;
	case ConsoleLockState::Unknown: return u"unknown"_q;
	}
	Unexpected("State in Test::ConsoleLockStateName.");
}

#ifdef Q_OS_WIN
[[nodiscard]] ConsoleLockReading ReadWindowsConsoleLock() {
	auto result = ConsoleLockReading{
		.source = u"WTSQuerySessionInformationW(WTSSessionInfoEx)"
			u".SessionFlags"_q,
		.state = ConsoleLockState::Unknown,
	};
	auto session = DWORD(0);
	if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) {
		const auto error = GetLastError();
		result.reason = u"ProcessIdToSessionId failed, GetLastError="_q
			+ QString::number(error);
		return result;
	}
	const auto console = WTSGetActiveConsoleSessionId();
	auto buffer = LPWSTR(nullptr);
	auto bytes = DWORD(0);
	const auto ok = WTSQuerySessionInformationW(
		WTS_CURRENT_SERVER_HANDLE,
		session,
		WTSSessionInfoEx,
		&buffer,
		&bytes);
	const auto error = ok ? DWORD(0) : GetLastError();
	const auto guard = gsl::finally([&] {
		if (buffer) {
			WTSFreeMemory(buffer);
		}
	});
	result.raw = u"session=%1 consoleSession=%2 bytes=%3"_q.arg(
		QString::number(session),
		((console == kNoConsoleSession)
			? u"none"_q
			: QString::number(console)),
		QString::number(bytes));
	if (!ok) {
		result.reason = u"WTSQuerySessionInformationW failed, GetLastError="_q
			+ QString::number(error);
		return result;
	} else if (!buffer || bytes < sizeof(WTSINFOEXW)) {
		result.reason = u"the answer is shorter than WTSINFOEXW (%1)"_q.arg(
			QString::number(sizeof(WTSINFOEXW)));
		return result;
	}
	const auto info = reinterpret_cast<const WTSINFOEXW*>(buffer);
	result.raw += u" level="_q + QString::number(info->Level);
	if (info->Level != 1) {
		result.reason = u"level is not 1"_q;
		return result;
	}
	const auto &level1 = info->Data.WTSInfoExLevel1;
	const auto flags = DWORD(level1.SessionFlags);
	result.raw += u" reportedSession=%1 sessionState=%2"
		u" sessionFlags=0x%3"_q.arg(
			QString::number(level1.SessionId),
			QString::number(int(level1.SessionState)),
			QString::number(flags, 16).rightJustified(8, QChar('0')));
	if (!Platform::IsWindows8OrGreater()) {
		result.reason = u"Windows 7 and Server 2008 R2 report "
			u"WTS_SESSIONSTATE_LOCK and WTS_SESSIONSTATE_UNLOCK reversed "
			u"(a documented defect), so the flags are not interpreted"_q;
	} else if (flags == WTS_SESSIONSTATE_LOCK) {
		result.state = ConsoleLockState::Locked;
		result.reason = u"SessionFlags is WTS_SESSIONSTATE_LOCK"_q;
	} else if (flags == WTS_SESSIONSTATE_UNLOCK) {
		result.state = ConsoleLockState::Unlocked;
		result.reason = u"SessionFlags is WTS_SESSIONSTATE_UNLOCK"_q;
	} else {
		result.reason = u"SessionFlags is neither WTS_SESSIONSTATE_LOCK "
			u"nor WTS_SESSIONSTATE_UNLOCK"_q;
	}
	return result;
}
#endif // Q_OS_WIN

} // namespace

ConsoleLockReading ReadConsoleLock() {
#ifdef Q_OS_WIN
	return ReadWindowsConsoleLock();
#else // Q_OS_WIN
	return {
		.source = u"none"_q,
		.reason = u"not a Windows build: nothing is read and no gate fires"_q,
		.state = ConsoleLockState::NotApplicable,
	};
#endif // Q_OS_WIN
}

QString ConsoleLockText(const ConsoleLockReading &reading) {
	return u"CONSOLE_LOCK: state="_q
		+ ConsoleLockStateName(reading.state)
		+ u" source="_q
		+ reading.source
		+ (reading.raw.isEmpty() ? QString() : (u" "_q + reading.raw))
		+ u" - "_q
		+ reading.reason;
}

QString ConsoleLockGate(const ConsoleLockReading &reading) {
	return reading.locked()
		? (u"locked console: "_q + ConsoleLockText(reading))
		: QString();
}

} // namespace Test

#endif // _DEBUG
