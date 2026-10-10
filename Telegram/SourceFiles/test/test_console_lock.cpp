/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_console_lock.h"

#include "base/platform/base_platform_info.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <WtsApi32.h>
#elif defined Q_OS_MAC // Q_OS_WIN
#include <CoreGraphics/CoreGraphics.h>
#endif // Q_OS_WIN || Q_OS_MAC

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
#elif defined Q_OS_MAC // Q_OS_WIN
enum class SessionFlag {
	Absent,
	False,
	True,
	NotBoolean,
};

[[nodiscard]] SessionFlag ReadSessionFlag(
		CFDictionaryRef session,
		CFStringRef key) {
	const auto value = CFDictionaryGetValue(session, key);
	if (!value) {
		return SessionFlag::Absent;
	} else if (CFGetTypeID(value) != CFBooleanGetTypeID()) {
		return SessionFlag::NotBoolean;
	}
	return CFBooleanGetValue(static_cast<CFBooleanRef>(value))
		? SessionFlag::True
		: SessionFlag::False;
}

[[nodiscard]] QString SessionFlagText(SessionFlag flag) {
	switch (flag) {
	case SessionFlag::Absent: return u"absent"_q;
	case SessionFlag::False: return u"0"_q;
	case SessionFlag::True: return u"1"_q;
	case SessionFlag::NotBoolean: return u"not-boolean"_q;
	}
	Unexpected("Flag in Test::SessionFlagText.");
}

[[nodiscard]] ConsoleLockReading ReadMacConsoleLock() {
	auto result = ConsoleLockReading{
		.source = u"CGSessionCopyCurrentDictionary()"
			u".CGSSessionScreenIsLocked"_q,
		.state = ConsoleLockState::Unknown,
	};
	const auto session = CGSessionCopyCurrentDictionary();
	if (!session) {
		result.reason = u"CGSessionCopyCurrentDictionary returned null: "
			u"the process runs in no window-server session"_q;
		return result;
	}
	const auto guard = gsl::finally([&] {
		CFRelease(session);
	});
	// Only these two keys are read: the same dictionary carries the user's
	// names and ids, and every row is published with task evidence.
	const auto onConsole = ReadSessionFlag(session, kCGSessionOnConsoleKey);
	const auto locked = ReadSessionFlag(
		session,
		CFSTR("CGSSessionScreenIsLocked"));
	result.raw = u"kCGSSessionOnConsoleKey=%1 "
		u"CGSSessionScreenIsLocked=%2"_q.arg(
			SessionFlagText(onConsole),
			SessionFlagText(locked));
	if (locked == SessionFlag::True) {
		result.state = ConsoleLockState::Locked;
		result.reason = u"CGSSessionScreenIsLocked is true"_q;
	} else if (locked == SessionFlag::NotBoolean) {
		result.reason = u"CGSSessionScreenIsLocked is not a boolean"_q;
	} else if (onConsole == SessionFlag::True) {
		result.state = ConsoleLockState::Unlocked;
		result.reason = u"the session is on the console and "
			u"CGSSessionScreenIsLocked is not true"_q;
	} else {
		result.reason = u"the session is not on the console, so "
			u"CGSSessionScreenIsLocked does not say whether its screen is "
			u"shown"_q;
	}
	return result;
}
#endif // Q_OS_WIN || Q_OS_MAC

} // namespace

ConsoleLockReading ReadConsoleLock() {
#ifdef Q_OS_WIN
	return ReadWindowsConsoleLock();
#elif defined Q_OS_MAC // Q_OS_WIN
	return ReadMacConsoleLock();
#else // Q_OS_WIN || Q_OS_MAC
	return {
		.source = u"none"_q,
		.reason = u"neither a Windows nor a macOS build: nothing is read "
			u"and no gate fires"_q,
		.state = ConsoleLockState::NotApplicable,
	};
#endif // Q_OS_WIN || Q_OS_MAC
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
	return (Platform::IsWindows() && reading.locked())
		? (u"locked console: "_q + ConsoleLockText(reading))
		: QString();
}

} // namespace Test

#endif // _DEBUG
