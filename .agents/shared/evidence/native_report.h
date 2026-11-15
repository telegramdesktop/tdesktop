/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#if !defined(__APPLE__)
#error "native_report.h supports macOS hosts only; see native-execution.md"
#endif // !__APPLE__

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/sysctl.h>
#include <sys/utsname.h>

namespace NativeReport {

inline constexpr auto kFlag = "--native-report";

[[nodiscard]] inline const char *BuildArchitecture() {
#if defined(__aarch64__) || defined(__arm64__)
	return "arm64";
#elif defined(__x86_64__)
	return "x86_64";
#else
	return "unknown";
#endif
}

[[nodiscard]] inline std::string RunningArchitecture() {
	auto name = utsname();
	if (uname(&name) != 0) {
		return std::string();
	}
	return std::string(name.machine);
}

[[nodiscard]] inline bool Translated() {
	auto value = 0;
	auto size = sizeof(value);
	if (sysctlbyname("sysctl.proc_translated", &value, &size, nullptr, 0)) {
		return false;
	}
	return (value == 1);
}

[[nodiscard]] inline bool Requested(int argc, char **argv) {
	return (argc == 3) && !std::strcmp(argv[1], kFlag);
}

inline int Print(const char *nonce) {
	const auto running = RunningArchitecture();
	std::printf(
		"NATIVE_REPORT {\"schema\":1,\"nonce\":\"%s\","
		"\"buildArch\":\"%s\",\"runningArch\":\"%s\","
		"\"translated\":%s}\n",
		nonce,
		BuildArchitecture(),
		running.c_str(),
		Translated() ? "true" : "false");
	std::fflush(stdout);
	return 0;
}

} // namespace NativeReport
