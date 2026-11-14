/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/platform_widget_helper.h"

#include "platform/win/widget_channel_win.h"
#include "platform/win/widget_sandbox_win.h"

#include <charconv>

namespace Platform::WidgetHelper {
namespace {

constexpr auto kPathCapacity = DWORD(32768);
constexpr auto kAbandonWait = crl::time(1000);
constexpr auto kFirstErrorStatus = DWORD(0xC0000000);

struct Path {
	std::wstring value;
	QString error;
};

struct Spawn {
	std::wstring path;
	std::wstring directory;
	QStringList arguments;
	HANDLE token = nullptr;
	HANDLE pipe = nullptr;
	HANDLE job = nullptr;
	DWORD flags = 0;
};

struct Created {
	UniqueHandle process;
	UniqueHandle thread;
	DWORD pid = 0;
	QString error;
};

struct Inheritable {
	UniqueHandle handle;
	QString error;
};

struct Attribute {
	DWORD_PTR id = 0;
	void *value = nullptr;
	SIZE_T size = 0;
};

struct Child {
	UniqueHandle process;
	UniqueHandle job;
};

[[nodiscard]] QString SystemError(const char *call, DWORD error) {
	return u"%1 failed: %2"_q.arg(
		QString::fromLatin1(call),
		qt_error_string(int(error)));
}

[[nodiscard]] Path ExecutablePath() {
	auto result = std::wstring(kPathCapacity, wchar_t());
	const auto length = GetModuleFileNameW(
		nullptr,
		result.data(),
		kPathCapacity);
	if (!length) {
		return { .error = SystemError("GetModuleFileNameW", GetLastError()) };
	} else if (length >= kPathCapacity) {
		return { .error = u"the executable path is too long"_q };
	}
	result.resize(length);
	return { .value = std::move(result) };
}

[[nodiscard]] Path SystemDirectory() {
	auto result = std::wstring(kPathCapacity, wchar_t());
	const auto length = GetSystemDirectoryW(result.data(), kPathCapacity);
	if (!length) {
		return { .error = SystemError("GetSystemDirectoryW", GetLastError()) };
	} else if (length >= kPathCapacity) {
		return { .error = u"the system directory path is too long"_q };
	}
	result.resize(length);
	return { .value = std::move(result) };
}

[[nodiscard]] std::wstring QuotedArgument(const QString &argument) {
	const auto value = argument.toStdWString();
	if (!value.empty()
		&& value.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
		return value;
	}
	auto result = std::wstring(1, L'"');
	auto slashes = std::size_t();
	for (const auto ch : value) {
		if (ch == L'\\') {
			++slashes;
			continue;
		}
		result.append((ch == L'"') ? (slashes * 2 + 1) : slashes, L'\\');
		slashes = 0;
		result.push_back(ch);
	}
	result.append(slashes * 2, L'\\');
	result.push_back(L'"');
	return result;
}

[[nodiscard]] Inheritable InheritableCopy(HANDLE source) {
	auto copy = HANDLE();
	if (!DuplicateHandle(
			GetCurrentProcess(),
			source,
			GetCurrentProcess(),
			&copy,
			0,
			TRUE,
			DUPLICATE_SAME_ACCESS)) {
		return { .error = SystemError("DuplicateHandle", GetLastError()) };
	}
	return { .handle = UniqueHandle(copy) };
}

[[nodiscard]] Inheritable InheritableDiagnostics() {
	const auto handle = GetStdHandle(STD_ERROR_HANDLE);
	if (!handle || handle == INVALID_HANDLE_VALUE) {
		return {};
	}
	const auto type = GetFileType(handle);
	if (type != FILE_TYPE_DISK && type != FILE_TYPE_PIPE) {
		return {};
	}
	return InheritableCopy(handle);
}

[[nodiscard]] Created CreateSuspended(Spawn spawn) {
	auto pipe = InheritableCopy(spawn.pipe);
	if (!pipe.error.isEmpty()) {
		return { .error = std::move(pipe.error) };
	}
	auto diagnostics = InheritableDiagnostics();
	if (!diagnostics.error.isEmpty()) {
		return { .error = std::move(diagnostics.error) };
	}
	auto inherited = std::vector<HANDLE>{ pipe.handle.get() };
	if (diagnostics.handle) {
		inherited.push_back(diagnostics.handle.get());
	}
	const auto &policies = SandboxPolicies();
	auto mitigations = policies.creation;
	auto childProcess = policies.childProcess;
	auto allApplicationPackages = policies.allApplicationPackages;
	auto jobs = std::array{ spawn.job };
	const auto attributes = std::array{
		Attribute{
			.id = PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
			.value = inherited.data(),
			.size = inherited.size() * sizeof(HANDLE),
		},
		Attribute{
			.id = PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY,
			.value = mitigations.data(),
			.size = policies.creationSize,
		},
		Attribute{
			.id = PROC_THREAD_ATTRIBUTE_CHILD_PROCESS_POLICY,
			.value = &childProcess,
			.size = sizeof(childProcess),
		},
		Attribute{
			.id = PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY,
			.value = &allApplicationPackages,
			.size = sizeof(allApplicationPackages),
		},
		Attribute{
			.id = PROC_THREAD_ATTRIBUTE_JOB_LIST,
			.value = jobs.data(),
			.size = jobs.size() * sizeof(HANDLE),
		},
	};
	const auto count = DWORD(attributes.size());
	auto size = SIZE_T();
	InitializeProcThreadAttributeList(nullptr, count, 0, &size);
	auto storage = std::vector<std::byte>(size);
	const auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
		storage.data());
	if (!size || !InitializeProcThreadAttributeList(list, count, 0, &size)) {
		return {
			.error = SystemError(
				"InitializeProcThreadAttributeList",
				GetLastError()),
		};
	}
	const auto deleteList = gsl::finally([&] {
		DeleteProcThreadAttributeList(list);
	});
	for (const auto &attribute : attributes) {
		if (!UpdateProcThreadAttribute(
				list,
				0,
				attribute.id,
				attribute.value,
				attribute.size,
				nullptr,
				nullptr)) {
			return {
				.error = SystemError(
					"UpdateProcThreadAttribute",
					GetLastError()),
			};
		}
	}
	auto startup = STARTUPINFOEXW();
	startup.StartupInfo.cb = DWORD(sizeof(startup));
	startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES
		| STARTF_FORCEOFFFEEDBACK;
	startup.StartupInfo.hStdError = diagnostics.handle.get();
	startup.lpAttributeList = list;
	auto commandLine = L"\"" + spawn.path + L"\" -widgethelper "
		+ std::to_wstring(reinterpret_cast<uintptr_t>(pipe.handle.get()));
	for (const auto &argument : spawn.arguments) {
		commandLine += L' ' + QuotedArgument(argument);
	}
	auto information = PROCESS_INFORMATION();
	if (!CreateProcessAsUserW(
			spawn.token,
			spawn.path.c_str(),
			commandLine.data(),
			nullptr,
			nullptr,
			TRUE,
			(spawn.flags
				| CREATE_SUSPENDED
				| EXTENDED_STARTUPINFO_PRESENT),
			nullptr,
			spawn.directory.c_str(),
			&startup.StartupInfo,
			&information)) {
		return { .error = SystemError("CreateProcessAsUserW", GetLastError()) };
	}
	return {
		.process = UniqueHandle(information.hProcess),
		.thread = UniqueHandle(information.hThread),
		.pid = information.dwProcessId,
	};
}

[[nodiscard]] QString CheckProcessToken(HANDLE process) {
	auto token = HANDLE();
	if (!OpenProcessToken(process, TOKEN_QUERY, &token)) {
		return SystemError("OpenProcessToken", GetLastError());
	}
	const auto guard = UniqueHandle(token);
	const auto error = CheckLockdownToken(token);
	return error.isEmpty()
		? CheckAppContainerToken(token, AppContainerLevel::LessPrivileged)
		: error;
}

[[nodiscard]] QString Start(HANDLE process, HANDLE thread, HANDLE initial) {
	auto error = CheckProcessToken(process);
	if (!error.isEmpty()) {
		return u"the process token check failed: "_q + error;
	}
	auto target = thread;
	if (!SetThreadToken(&target, initial)) {
		return SystemError("SetThreadToken", GetLastError());
	}
	auto attached = HANDLE();
	if (!OpenThreadToken(thread, TOKEN_QUERY, TRUE, &attached)) {
		return SystemError("OpenThreadToken", GetLastError());
	}
	const auto guard = UniqueHandle(attached);
	error = CheckInitialToken(attached);
	if (error.isEmpty()) {
		error = CheckAppContainerToken(attached, AppContainerLevel::Regular);
	}
	if (!error.isEmpty()) {
		return u"the attached token check failed: "_q + error;
	} else if (ResumeThread(thread) == DWORD(-1)) {
		return SystemError("ResumeThread", GetLastError());
	}
	return QString();
}

[[nodiscard]] DWORD WaitTimeout(crl::time timeout) {
	return DWORD(std::clamp(timeout, crl::time(0), crl::time(INFINITE - 1)));
}

[[nodiscard]] QString ExitText(HANDLE process) {
	auto code = DWORD();
	if (!GetExitCodeProcess(process, &code)) {
		return SystemError("GetExitCodeProcess", GetLastError());
	}
	return (code >= kFirstErrorStatus)
		? u"exit 0x%1"_q.arg(QString::number(code, 16).toUpper())
		: u"exit %1"_q.arg(code);
}

[[nodiscard]] std::optional<Completion> WaitFor(
		HANDLE process,
		crl::time timeout) {
	const auto waited = WaitForSingleObject(process, WaitTimeout(timeout));
	if (waited == WAIT_OBJECT_0) {
		return Completion{ .detail = ExitText(process), .reaped = true };
	} else if (waited == WAIT_TIMEOUT) {
		return std::nullopt;
	}
	return Completion{
		.detail = SystemError("WaitForSingleObject", GetLastError()),
	};
}

[[nodiscard]] Completion Complete(
		const Child &child,
		crl::time exitWait,
		crl::time killWait) {
	const auto process = child.process.get();
	const auto started = crl::now();
	auto exited = WaitFor(process, exitWait);
	if (exited && exited->reaped) {
		return std::move(*exited);
	}
	const auto terminated = crl::now() - started;
	const auto job = child.job.get();
	const auto killError = TerminateJobObject(job, ERROR_PROCESS_ABORTED)
		? DWORD()
		: GetLastError();
	auto result = WaitFor(process, killWait);
	if (!result) {
		result = Completion{
			.detail = u"not reaped after %1 ms"_q.arg(crl::now() - started),
		};
	}
	if (exited) {
		result->detail = exited->detail + u", "_q + result->detail;
	}
	result->detail += u", "_q + (killError
		? SystemError("TerminateJobObject", killError)
		: u"terminated after %1 ms"_q.arg(terminated));
	return std::move(*result);
}

[[nodiscard]] QString Abandon(const Child &child, const QString &error) {
	const auto cleanup = Complete(child, 0, kAbandonWait);
	return u"%1 (%2)"_q.arg(error, cleanup.detail);
}

[[nodiscard]] bool Sentinel(HANDLE handle) {
	const auto sentinels = std::array{
		INVALID_HANDLE_VALUE,
		GetCurrentThread(),
		GetCurrentProcessToken(),
		GetCurrentThreadToken(),
		GetCurrentThreadEffectiveToken(),
	};
	return ranges::contains(sentinels, handle);
}

[[nodiscard]] HANDLE ParseHandle(const QString &channel) {
	const auto text = channel.toLatin1();
	const auto begin = text.constData();
	const auto end = begin + text.size();
	auto value = uintptr_t();
	const auto parsed = std::from_chars(begin, end, value);
	if (text.isEmpty()
		|| text.front() == '0'
		|| parsed.ec != std::errc()
		|| parsed.ptr != end) {
		return nullptr;
	}
	const auto result = reinterpret_cast<HANDLE>(value);
	return Sentinel(result) ? nullptr : result;
}

} // namespace

bool Enabled() {
	return false;
}

Launched Launch(const QStringList &arguments) {
	auto unsupported = UnsupportedReason();
	if (!unsupported.isEmpty()) {
		return { .error = std::move(unsupported) };
	}
	auto path = ExecutablePath();
	if (!path.error.isEmpty()) {
		return { .error = std::move(path.error) };
	}
	auto directory = SystemDirectory();
	if (!directory.error.isEmpty()) {
		return { .error = std::move(directory.error) };
	}
	auto channel = CreateWinChannel();
	if (!channel.error.isEmpty()) {
		return { .error = std::move(channel.error) };
	}
	auto tokens = CreateTokens();
	if (!tokens.error.isEmpty()) {
		return { .error = std::move(tokens.error) };
	}
	auto job = CreateJob();
	if (!job.error.isEmpty()) {
		return { .error = std::move(job.error) };
	}
	auto created = CreateSuspended({
		.path = std::move(path.value),
		.directory = std::move(directory.value),
		.arguments = arguments,
		.token = tokens.lockdown.get(),
		.pipe = channel.child.get(),
		.job = job.handle.get(),
		.flags = job.creationFlags,
	});
	if (!created.error.isEmpty()) {
		return { .error = std::move(created.error) };
	}
	auto child = Child{
		.process = std::move(created.process),
		.job = std::move(job.handle),
	};
	const auto error = Start(
		child.process.get(),
		created.thread.get(),
		tokens.initial.get());
	if (!error.isEmpty()) {
		return { .error = Abandon(child, error) };
	}
	return {
		.stream = std::move(channel.parent),
		.complete = [child = std::move(child)](
				crl::time exitWait,
				crl::time killWait) {
			return Complete(child, exitWait, killWait);
		},
		.pid = int64(created.pid),
	};
}

std::shared_ptr<twidget::SandboxRemoteStream> Adopt(const QString &channel) {
	const auto handle = ParseHandle(channel);
	if (!handle) {
		return nullptr;
	}
	return AdoptWinChannel(UniqueHandle(handle));
}

QString Lockdown() {
	return LockDownHelper();
}

Fn<QString()> PrepareProbe(const QString &clientDirectory) {
	return PrepareHelperProbe(clientDirectory);
}

} // namespace Platform::WidgetHelper
