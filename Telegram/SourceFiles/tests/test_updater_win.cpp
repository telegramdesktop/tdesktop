/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "_other/updater.h"

#include <sddl.h>

#include <iostream>
#include <limits>
#include <memory>

namespace {

using Handle = std::unique_ptr<void, decltype(&CloseHandle)>;

auto Failures = 0;

void Check(bool condition, const char *name) {
	if (!condition) {
		++Failures;
		std::cerr << "FAILED: " << name << std::endl;
	}
}

ULONGLONG CreationTime(HANDLE process) {
	auto created = FILETIME();
	auto exited = FILETIME();
	auto kernel = FILETIME();
	auto user = FILETIME();
	const auto result = GetProcessTimes(
		process,
		&created,
		&exited,
		&kernel,
		&user);
	Check(result != FALSE, "read test process identity");
	return (ULONGLONG(created.dwHighDateTime) << 32) | created.dwLowDateTime;
}

void TestArguments() {
	auto id = DWORD(0);
	auto created = ULONGLONG(0);
	Check(
		ParseUpdateProcess(L"1234", L"134000000000000000", id, created),
		"accept launcher identity");
	Check(
		id == 1234 && created == 134000000000000000ULL,
		"preserve full creation time");
	Check(
		ParseUpdateProcess(L"4294967295", L"18446744073709551615", id, created),
		"accept representable upper bounds");
	Check(
		id == std::numeric_limits<DWORD>::max()
			&& created == std::numeric_limits<ULONGLONG>::max(),
		"preserve upper bounds");
	const auto invalid = {
		L"", L"0", L"-1", L"+1", L" 1", L"1 ", L"1x", L"1.0",
		L"18446744073709551616", L"999999999999999999999999999999",
	};
	for (const auto value : invalid) {
		id = 17;
		created = 19;
		Check(
			!ParseUpdateProcess(value, L"1", id, created),
			"reject malformed process id");
		Check(
			!ParseUpdateProcess(L"1", value, id, created),
			"reject malformed creation time");
		Check(id == 17 && created == 19, "failed parse preserves outputs");
	}
	Check(
		!ParseUpdateProcess(L"4294967296", L"1", id, created),
		"reject process id overflow");
}

void TestErrors() {
	const auto message = wstring(8192, L'x') + L"\u4e2d\u6587";
	const auto text = FormatUpdateError(message, ERROR_SHARING_VIOLATION);
	Check(text.starts_with(message), "preserve long Unicode error context");
	Check(
		text.find(L"Error code: 32") != wstring::npos,
		"report original numeric error");
	const auto unknown = FormatUpdateError(L"test", 0xE0000001);
	Check(
		unknown.find(L"3758096385") != wstring::npos,
		"format unsigned error code");
	Check(
		unknown.find(L"(Unknown error)") != wstring::npos,
		"handle unavailable system error description");
}

void TestProcess(const wchar_t *executable, bool force, bool denyTermination) {
	const auto eventName = L"Local\\TelegramUpdaterTest-"
		+ std::to_wstring(GetCurrentProcessId())
		+ L"-" + std::to_wstring(GetTickCount64());
	const auto event = Handle(
		CreateEvent(nullptr, TRUE, FALSE, eventName.c_str()),
		&CloseHandle);
	Check(event != nullptr, "create test exit event");
	if (!event) {
		return;
	}
	auto command = L"\"" + wstring(executable)
		+ L"\" --child " + eventName;
	auto startup = STARTUPINFO();
	startup.cb = sizeof(startup);
	auto information = PROCESS_INFORMATION();
	const auto started = CreateProcess(
		executable,
		command.data(),
		nullptr,
		nullptr,
		FALSE,
		CREATE_NO_WINDOW,
		nullptr,
		nullptr,
		&startup,
		&information);
	Check(started != FALSE, "start owned test child");
	if (!started) {
		return;
	}
	auto process = Handle(information.hProcess, &CloseHandle);
	auto thread = Handle(information.hThread, &CloseHandle);
	const auto created = CreationTime(process.get());
	Check(
		FinishUpdateProcess(information.dwProcessId, created + 1)
			== ERROR_SUCCESS,
		"ignore reused process id");
	Check(
		WaitForSingleObject(process.get(), 0) == WAIT_TIMEOUT,
		"identity mismatch leaves process running");
	if (denyTermination) {
		auto descriptor = PSECURITY_DESCRIPTOR(nullptr);
		const auto parsed = ConvertStringSecurityDescriptorToSecurityDescriptor(
			L"D:(D;;0x00000001;;;WD)(A;;0x001FFFFF;;;WD)",
			SDDL_REVISION_1,
			&descriptor,
			nullptr);
		const auto guard = std::unique_ptr<void, decltype(&LocalFree)>(
			descriptor,
			&LocalFree);
		Check(parsed != FALSE, "create test process permissions");
		if (parsed) {
			Check(
				SetKernelObjectSecurity(
					process.get(),
					DACL_SECURITY_INFORMATION,
					descriptor) != FALSE,
				"deny termination of test child");
		}
	}
	if (!force) {
		Check(SetEvent(event.get()) != FALSE, "request normal test exit");
	}
	const auto startedAt = GetTickCount64();
	const auto result = FinishUpdateProcess(information.dwProcessId, created);
	if (force) {
		Check(
			GetTickCount64() - startedAt >= 4900,
			"allow normal exit grace period");
	}
	if (force && denyTermination) {
		Check(result == ERROR_ACCESS_DENIED, "report denied termination");
		Check(
			WaitForSingleObject(process.get(), 0) == WAIT_TIMEOUT,
			"denied termination leaves process running");
	} else {
		Check(result == ERROR_SUCCESS, "finish matching process");
		Check(
			WaitForSingleObject(process.get(), 0) == WAIT_OBJECT_0,
			"success guarantees process exited");
		auto code = DWORD(0);
		Check(
			GetExitCodeProcess(process.get(), &code) != FALSE,
			"read test exit code");
		Check(
			code == (force ? ERROR_PROCESS_ABORTED : 37),
			"use forced exit only after timeout");
		Check(
			FinishUpdateProcess(information.dwProcessId, created)
				== ERROR_SUCCESS,
			"accept already exited process");
	}
	Check(SetEvent(event.get()) != FALSE, "release test child");
	Check(
		WaitForSingleObject(process.get(), 35000) == WAIT_OBJECT_0,
		"test child cleanup");
	if (!denyTermination) {
		thread.reset();
		process.reset();
		Check(
			FinishUpdateProcess(information.dwProcessId, created)
				== ERROR_SUCCESS,
			"accept absent process");
	}
}

} // namespace

int wmain(int argc, wchar_t *argv[]) {
	if (argc == 3 && wstring(argv[1]) == L"--child") {
		const auto event = Handle(
			OpenEvent(SYNCHRONIZE, FALSE, argv[2]),
			&CloseHandle);
		if (!event || WaitForSingleObject(event.get(), 30000) != WAIT_OBJECT_0) {
			return 38;
		}
		Sleep(250);
		return 37;
	}
	TestArguments();
	TestErrors();
	Check(
		FinishUpdateProcess(0, 1) == ERROR_INVALID_PARAMETER,
		"reject zero process id");
	Check(
		FinishUpdateProcess(
			GetCurrentProcessId(),
			CreationTime(GetCurrentProcess())) == ERROR_INVALID_PARAMETER,
		"refuse to terminate updater itself");
	TestProcess(argv[0], false, false);
	TestProcess(argv[0], true, false);
	TestProcess(argv[0], false, true);
	TestProcess(argv[0], true, true);
	std::cout << "Updater tests: " << Failures << " failures." << std::endl;
	return Failures ? 1 : 0;
}
