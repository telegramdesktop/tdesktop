/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/win/widget_channel_win.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Platform::WidgetHelper {

inline constexpr auto kHelperDesktopPrefix = std::wstring_view(
	L"TelegramWidgetHelper-");
inline constexpr auto kHelperDesktopRandomBytes = std::size_t(16);

using UserObjectQuery = decltype(&::GetUserObjectInformationW);

struct DesktopQueries {
	decltype(&::GetProcessWindowStation) station = nullptr;
	decltype(&::GetThreadDesktop) threadDesktop = nullptr;
	UserObjectQuery information = nullptr;
};

enum class SandboxTier : std::uint8_t {
	Windows7,
	AppContainer,
	Lpac1703,
	Full,
};

struct WindowsVersion {
	DWORD major = 0;
	DWORD minor = 0;
	DWORD build = 0;
};

struct Sandbox {
	WindowsVersion version;
	std::optional<SandboxTier> tier;
};

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

struct UserObjectName {
	std::optional<std::wstring> value;
	DWORD error = 0;
};

struct AssignedDesktop {
	std::wstring name;
	const char *failed = nullptr;
	DWORD error = 0;
};

[[nodiscard]] const Sandbox &CurrentSandbox();
[[nodiscard]] std::string SandboxUnsupportedReason();

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

[[nodiscard]] UserObjectName ReadUserObjectName(
	UserObjectQuery information,
	HANDLE object);
[[nodiscard]] AssignedDesktop ReadAssignedDesktop(
	const DesktopQueries &queries,
	DWORD thread);

[[nodiscard]] std::string VerifyNoThreadToken();
[[nodiscard]] std::string LockDownHelper();

} // namespace Platform::WidgetHelper
