/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "_other/widget_helper.h"

#include "platform/win/widget_channel_win.h"
#include "platform/win/widget_sandbox_win.h"

#include <bcrypt.h>
#include <delayimp.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <system_error>

#ifdef _M_X64
namespace {

BOOLEAN WINAPI GenerateRandom(PVOID buffer, ULONG size) {
	return BCRYPT_SUCCESS(BCryptGenRandom(
		nullptr,
		static_cast<PUCHAR>(buffer),
		size,
		BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}

FARPROC WINAPI AnswerDelayLoad(unsigned notification, PDelayLoadInfo info) {
	// Before GetProcAddress, which can follow a forwarder into cryptbase.dll.
	if (notification == dliNotePreGetProcAddress
		&& info->dlp.fImportByName
		&& !_stricmp(info->szDll, "ADVAPI32.dll")
		&& std::string_view(info->dlp.szProcName) == "SystemFunction036") {
		return reinterpret_cast<FARPROC>(GenerateRandom);
	}
	return nullptr;
}

} // namespace

extern "C" const PfnDliHook __pfnDliNotifyHook2 = AnswerDelayLoad;
#endif // _M_X64

namespace WidgetHelper {
namespace {

[[nodiscard]] bool Sentinel(HANDLE handle) {
	const auto sentinels = std::array{
		INVALID_HANDLE_VALUE,
		GetCurrentThread(),
		GetCurrentProcessToken(),
		GetCurrentThreadToken(),
		GetCurrentThreadEffectiveToken(),
	};
	return std::ranges::find(sentinels, handle) != sentinels.end();
}

[[nodiscard]] HANDLE ParseHandle(std::string_view token) {
	if (token.empty() || token.front() == '0') {
		return nullptr;
	}
	const auto end = token.data() + token.size();
	auto value = std::uintptr_t();
	const auto parsed = std::from_chars(token.data(), end, value);
	if (parsed.ec != std::errc() || parsed.ptr != end) {
		return nullptr;
	}
	const auto result = reinterpret_cast<HANDLE>(value);
	return Sentinel(result) ? nullptr : result;
}

} // namespace

std::unique_ptr<twidget::SandboxRemoteStream> Adopt(std::string_view token) {
	using SetDirectories = decltype(&SetDefaultDllDirectories);
	const auto kernel = GetModuleHandleW(L"kernel32.dll");
	if (!kernel) {
		return nullptr;
	}
	const auto setDirectories = reinterpret_cast<SetDirectories>(
		GetProcAddress(kernel, "SetDefaultDllDirectories"));
	if (!setDirectories || !setDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32)) {
		return nullptr;
	}
	const auto handle = ParseHandle(token);
	if (!handle) {
		return nullptr;
	}
	return Platform::WidgetHelper::AdoptWinChannel(
		Platform::WidgetHelper::UniqueHandle(handle));
}

std::string Lockdown() {
	return Platform::WidgetHelper::LockDownHelper();
}

} // namespace WidgetHelper
