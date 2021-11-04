/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include <windows.h>

#include <array>
#include <string>

using namespace std;

constexpr auto kMaxPathLong = 32767;

[[nodiscard]] std::wstring ExecutableDirectory() {
	auto exePath = std::array<WCHAR, kMaxPathLong + 1>{ 0 };
	const auto exeLength = GetModuleFileName(
		nullptr,
		exePath.data(),
		kMaxPathLong + 1);
	if (!exeLength || exeLength >= kMaxPathLong + 1) {
		return {};
	}
	const auto exe = std::wstring(exePath.data());
	const auto last1 = exe.find_last_of('\\');
	const auto last2 = exe.find_last_of('/');
	const auto last = std::max(
		(last1 == std::wstring::npos) ? -1 : int(last1),
		(last2 == std::wstring::npos) ? -1 : int(last2));
	if (last < 0) {
		return {};
	}
	return exe.substr(0, last);
}

int APIENTRY wWinMain(
		HINSTANCE instance,
		HINSTANCE prevInstance,
		LPWSTR cmdParamarg,
		int cmdShow) {
	const auto directory = ExecutableDirectory();
	if (!directory.empty()) {
		ShellExecute(
			nullptr,
			nullptr,
			(directory + L"\\Telegram.exe").c_str(),
			L"-autostart",
			directory.data(),
			SW_SHOWNORMAL);
	}
	return 0;
}
