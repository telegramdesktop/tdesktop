/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/win/widget_channel_win.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Platform::WidgetHelper {

struct Tokens {
	UniqueHandle lockdown;
	UniqueHandle initial;
	std::string error;
};

struct Job {
	UniqueHandle handle;
	DWORD creationFlags = 0;
	std::string error;
};

struct JobState {
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
	JOBOBJECT_BASIC_UI_RESTRICTIONS restrictions = {};
	const char *failed = nullptr;
	DWORD error = 0;
	BOOL inJob = FALSE;
};

enum class AppContainerLevel : std::uint8_t {
	Regular,
	LessPrivileged,
};

struct Mitigation {
	PROCESS_MITIGATION_POLICY policy = {};
	DWORD size = 0;
	DWORD required = 0;
	DWORD forbidden = 0;
};

struct Policies {
	std::array<DWORD64, 2> creation = {};
	SIZE_T creationSize = 0;
	DWORD childProcess = 0;
	DWORD allApplicationPackages = 0;
	std::vector<Mitigation> mitigations;
};

[[nodiscard]] Tokens CreateTokens();
[[nodiscard]] std::string CheckLockdownToken(HANDLE token);
[[nodiscard]] std::string CheckInitialToken(HANDLE token);
[[nodiscard]] std::string CheckAppContainerToken(
	HANDLE token,
	AppContainerLevel level);

[[nodiscard]] Job CreateJob();
[[nodiscard]] JobState ReadJob();
[[nodiscard]] std::string VerifyJob();

[[nodiscard]] const Policies &SandboxPolicies();
[[nodiscard]] std::optional<DWORD> ReadMitigation(
	PROCESS_MITIGATION_POLICY policy,
	DWORD size);

[[nodiscard]] std::string Hex(DWORD value);
[[nodiscard]] std::string NumberedError(const char *call, DWORD error);
[[nodiscard]] std::string PolicyError(
	const char *call,
	PROCESS_MITIGATION_POLICY policy,
	DWORD error);

[[nodiscard]] std::string VerifyNoThreadToken();
[[nodiscard]] std::string LockDownHelper();

} // namespace Platform::WidgetHelper
