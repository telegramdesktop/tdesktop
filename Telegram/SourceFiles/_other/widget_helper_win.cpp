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

#if defined _M_IX86 || defined _M_X64
namespace {

using ProcessPrngFunction = BOOL(WINAPI*)(PBYTE, SIZE_T);

// The impersonated initial token lets only one preferred-RNG BCryptGenRandom succeed: WarmUp()'s.
[[nodiscard]] ProcessPrngFunction ResolvedProcessPrng() {
	static const auto result = [] {
		const auto module = LoadLibraryExW(
			L"bcryptprimitives.dll",
			nullptr,
			LOAD_LIBRARY_SEARCH_SYSTEM32);
		return module
			? reinterpret_cast<ProcessPrngFunction>(
				GetProcAddress(module, "ProcessPrng"))
			: nullptr;
	}();
	return result;
}

BOOLEAN WINAPI GenerateRandom(PVOID buffer, ULONG size) {
	if (const auto generate = ResolvedProcessPrng()) {
		return generate(static_cast<PBYTE>(buffer), size);
	}
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
#endif // _M_IX86 || _M_X64

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
#if defined _M_IX86 || defined _M_X64
	[[maybe_unused]] const auto resolved = ResolvedProcessPrng();
#endif // _M_IX86 || _M_X64
	return Platform::WidgetHelper::LockDownHelper();
}

} // namespace WidgetHelper
