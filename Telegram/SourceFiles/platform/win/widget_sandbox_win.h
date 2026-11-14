/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/win/widget_channel_win.h"

namespace Platform::WidgetHelper {

struct Tokens {
	UniqueHandle lockdown;
	UniqueHandle initial;
	QString error;
};

struct Job {
	UniqueHandle handle;
	DWORD creationFlags = 0;
	QString error;
};

enum class MitigationStage : uchar {
	Creation,
	Startup,
};

struct Mitigation {
	PROCESS_MITIGATION_POLICY policy = {};
	DWORD size = 0;
	DWORD required = 0;
	DWORD forbidden = 0;
	MitigationStage stage = MitigationStage::Creation;
};

struct Policies {
	std::array<DWORD64, 2> creation = {};
	SIZE_T creationSize = 0;
	DWORD childProcess = 0;
	std::vector<Mitigation> mitigations;
};

[[nodiscard]] QString UnsupportedReason();

[[nodiscard]] Tokens CreateTokens();
[[nodiscard]] QString CheckLockdownToken(HANDLE token);
[[nodiscard]] QString CheckInitialToken(HANDLE token);

[[nodiscard]] Job CreateJob();

[[nodiscard]] const Policies &SandboxPolicies();

[[nodiscard]] QString LockDownHelper();
[[nodiscard]] Fn<QString()> PrepareHelperProbe();

} // namespace Platform::WidgetHelper
