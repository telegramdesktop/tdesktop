/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/platform_widget_helper.h"

#include "base/random.h"
#include "platform/win/widget_channel_win.h"
#include "platform/win/widget_helper_image_win.h"
#include "platform/win/widget_sandbox_win.h"

#include <aclapi.h>
#include <bcrypt.h>

#include <thread>

namespace Platform::WidgetHelper {
namespace {

constexpr auto kPathCapacity = DWORD(32768);
constexpr auto kAbandonWait = crl::time(1000);
constexpr auto kFirstErrorStatus = DWORD(0xC0000000);
constexpr auto kDesktopAccess = DWORD(DESKTOP_CREATEWINDOW
	| DESKTOP_READOBJECTS
	| DESKTOP_WRITEOBJECTS
	| WRITE_OWNER);

struct Path {
	std::wstring value;
	QString error;
};

struct Environment {
	std::wstring block;
	QString error;
};

struct Spawn {
	std::wstring path;
	std::wstring directory;
	std::wstring desktop;
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

struct DesktopCloser {
	void operator()(HDESK desktop) const;
};

using UniqueDesktop = std::unique_ptr<
	std::remove_pointer_t<HDESK>,
	DesktopCloser>;

struct HelperDesktop {
	UniqueDesktop handle;
	std::wstring name;
	QString error;
};

struct Child {
	UniqueHandle process;
	UniqueDesktop desktop;
	UniqueHandle job;
};

void DesktopCloser::operator()(HDESK desktop) const {
	CloseDesktop(desktop);
}

[[nodiscard]] QString QtSystemError(const char *call, DWORD error) {
	return QString::fromStdString(SystemError(call, error));
}

[[nodiscard]] std::wstring PipeName() {
	return u"\\\\.\\pipe\\telegram-widget-helper-%1-%2%3"_q
		.arg(GetCurrentProcessId())
		.arg(base::RandomValue<uint64>(), 16, 16, QChar('0'))
		.arg(base::RandomValue<uint64>(), 16, 16, QChar('0'))
		.toStdWString();
}

[[nodiscard]] Path WindowStationName() {
	const auto station = GetProcessWindowStation();
	if (!station) {
		return {
			.error = QtSystemError("GetProcessWindowStation", GetLastError()),
		};
	}
	auto name = ReadUserObjectName(&GetUserObjectInformationW, station);
	if (!name.value) {
		return {
			.error = QtSystemError("GetUserObjectInformationW", name.error),
		};
	}
	return { .value = std::move(*name.value) };
}

[[nodiscard]] Path DesktopName() {
	auto random = std::array<UCHAR, kHelperDesktopRandomBytes>();
	const auto status = BCryptGenRandom(
		nullptr,
		random.data(),
		ULONG(random.size()),
		BCRYPT_USE_SYSTEM_PREFERRED_RNG);
	if (!BCRYPT_SUCCESS(status)) {
		return {
			.error = u"BCryptGenRandom failed: status 0x%1"_q.arg(
				QString::fromStdString(Hex(DWORD(status)))),
		};
	}
	const auto bytes = QByteArray(
		reinterpret_cast<const char*>(random.data()),
		int(random.size()));
	const auto suffix = u"%1-%2"_q
		.arg(GetCurrentProcessId())
		.arg(QString::fromLatin1(bytes.toHex()));
	return {
		.value = std::wstring(kHelperDesktopPrefix) + suffix.toStdWString(),
	};
}

[[nodiscard]] QString SetLowLabel(HDESK desktop) {
	auto sid = std::vector<std::byte>(SECURITY_MAX_SID_SIZE);
	auto size = DWORD(sid.size());
	if (!CreateWellKnownSid(WinLowLabelSid, nullptr, sid.data(), &size)) {
		return QtSystemError("CreateWellKnownSid", GetLastError());
	}
	auto list = std::vector<std::byte>(sizeof(ACL)
		+ sizeof(SYSTEM_MANDATORY_LABEL_ACE)
		+ SECURITY_MAX_SID_SIZE);
	const auto acl = reinterpret_cast<ACL*>(list.data());
	if (!InitializeAcl(acl, DWORD(list.size()), ACL_REVISION)) {
		return QtSystemError("InitializeAcl", GetLastError());
	} else if (!AddMandatoryAce(
			acl,
			ACL_REVISION,
			0,
			SYSTEM_MANDATORY_LABEL_NO_WRITE_UP,
			sid.data())) {
		return QtSystemError("AddMandatoryAce", GetLastError());
	}
	const auto error = SetSecurityInfo(
		desktop,
		SE_WINDOW_OBJECT,
		LABEL_SECURITY_INFORMATION,
		nullptr,
		nullptr,
		nullptr,
		acl);
	return error ? QtSystemError("SetSecurityInfo", error) : QString();
}

[[nodiscard]] HelperDesktop CreateDesktopOnShortThread(
		const std::wstring &name) {
	auto desktop = HDESK();
	auto error = DWORD();
	std::thread([&] {
		desktop = CreateDesktopW(
			name.c_str(),
			nullptr,
			nullptr,
			0,
			kDesktopAccess,
			nullptr);
		error = desktop ? DWORD() : GetLastError();
	}).join();
	if (!desktop) {
		return { .error = QtSystemError("CreateDesktopW", error) };
	}
	return { .handle = UniqueDesktop(desktop) };
}

[[nodiscard]] HelperDesktop CreateHelperDesktop() {
	auto station = WindowStationName();
	if (!station.error.isEmpty()) {
		return { .error = std::move(station.error) };
	}
	auto name = DesktopName();
	if (!name.error.isEmpty()) {
		return { .error = std::move(name.error) };
	}
	auto result = CreateDesktopOnShortThread(name.value);
	if (!result.handle) {
		return result;
	}
	auto error = SetLowLabel(result.handle.get());
	if (!error.isEmpty()) {
		result.handle = nullptr;
		return { .error = std::move(error) };
	}
	result.name = station.value + L'\\' + name.value;
	return result;
}

[[nodiscard]] Path SystemDirectory() {
	auto result = std::wstring(kPathCapacity, wchar_t());
	const auto length = GetSystemDirectoryW(result.data(), kPathCapacity);
	if (!length) {
		return {
			.error = QtSystemError("GetSystemDirectoryW", GetLastError()),
		};
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
		return { .error = QtSystemError("DuplicateHandle", GetLastError()) };
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

[[nodiscard]] Environment HelperEnvironment() {
	const auto strings = GetEnvironmentStringsW();
	if (!strings) {
		return {
			.error = QtSystemError("GetEnvironmentStringsW", GetLastError()),
		};
	}
	const auto guard = gsl::finally([&] {
		FreeEnvironmentStringsW(strings);
	});
	auto result = Environment();
	auto next = strings;
	while (*next) {
		const auto entry = std::wstring_view(next);
		const auto name = entry.substr(0, entry.find(L'=', 1));
		next += entry.size() + 1;
		// A compatibility mode set on Telegram must not reach the helper.
		if (CompareStringOrdinal(
				name.data(),
				int(name.size()),
				L"__COMPAT_LAYER",
				-1,
				TRUE) != CSTR_EQUAL) {
			result.block.append(entry);
			result.block.push_back(L'\0');
		}
	}
	result.block.push_back(L'\0');
	return result;
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
	auto environment = HelperEnvironment();
	if (!environment.error.isEmpty()) {
		return { .error = std::move(environment.error) };
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
			.size = childProcess ? sizeof(childProcess) : 0,
		},
		Attribute{
			.id = PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY,
			.value = &allApplicationPackages,
			.size = (allApplicationPackages
				? sizeof(allApplicationPackages)
				: 0),
		},
		Attribute{
			.id = PROC_THREAD_ATTRIBUTE_JOB_LIST,
			.value = jobs.data(),
			.size = spawn.job ? (jobs.size() * sizeof(HANDLE)) : 0,
		},
	};
	const auto count = DWORD(std::ranges::count_if(
		attributes,
		[](const Attribute &attribute) { return attribute.size != 0; }));
	auto size = SIZE_T();
	InitializeProcThreadAttributeList(nullptr, count, 0, &size);
	auto storage = std::vector<std::byte>(size);
	const auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
		storage.data());
	if (!size || !InitializeProcThreadAttributeList(list, count, 0, &size)) {
		return {
			.error = QtSystemError(
				"InitializeProcThreadAttributeList",
				GetLastError()),
		};
	}
	const auto deleteList = gsl::finally([&] {
		DeleteProcThreadAttributeList(list);
	});
	for (const auto &attribute : attributes) {
		if (!attribute.size) {
			continue;
		} else if (!UpdateProcThreadAttribute(
				list,
				0,
				attribute.id,
				attribute.value,
				attribute.size,
				nullptr,
				nullptr)) {
			return {
				.error = QtSystemError(
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
	if (!spawn.desktop.empty()) {
		startup.StartupInfo.lpDesktop = spawn.desktop.data();
	}
	startup.lpAttributeList = list;
	auto commandLine = L"\"" + spawn.path + L"\" "
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
				| CREATE_UNICODE_ENVIRONMENT
				| EXTENDED_STARTUPINFO_PRESENT),
			environment.block.data(),
			spawn.directory.c_str(),
			&startup.StartupInfo,
			&information)) {
		return {
			.error = QtSystemError("CreateProcessAsUserW", GetLastError()),
		};
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
		return QtSystemError("OpenProcessToken", GetLastError());
	}
	const auto guard = UniqueHandle(token);
	const auto error = CheckLockdownToken(token);
	return QString::fromStdString(error.empty()
		? CheckAppContainerToken(token, AppContainerLevel::LessPrivileged)
		: error);
}

[[nodiscard]] QString Start(HANDLE process, HANDLE thread, HANDLE initial) {
	auto error = CheckProcessToken(process);
	if (!error.isEmpty()) {
		return u"the process token check failed: "_q + error;
	}
	auto target = thread;
	if (!SetThreadToken(&target, initial)) {
		return QtSystemError("SetThreadToken", GetLastError());
	}
	auto attached = HANDLE();
	if (!OpenThreadToken(thread, TOKEN_QUERY, TRUE, &attached)) {
		return QtSystemError("OpenThreadToken", GetLastError());
	}
	const auto guard = UniqueHandle(attached);
	error = QString::fromStdString(CheckInitialToken(attached));
	if (error.isEmpty()) {
		error = QString::fromStdString(
			CheckAppContainerToken(attached, AppContainerLevel::Regular));
	}
	if (!error.isEmpty()) {
		return u"the attached token check failed: "_q + error;
	} else if (ResumeThread(thread) == DWORD(-1)) {
		return QtSystemError("ResumeThread", GetLastError());
	}
	return QString();
}

[[nodiscard]] DWORD WaitTimeout(crl::time timeout) {
	return DWORD(std::clamp(timeout, crl::time(0), crl::time(INFINITE - 1)));
}

[[nodiscard]] QString ExitText(HANDLE process) {
	auto code = DWORD();
	if (!GetExitCodeProcess(process, &code)) {
		return QtSystemError("GetExitCodeProcess", GetLastError());
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
		.detail = QtSystemError("WaitForSingleObject", GetLastError()),
	};
}

[[nodiscard]] Completion Terminate(
		const Child &child,
		crl::time started,
		crl::time killWait) {
	const auto process = child.process.get();
	const auto job = child.job.get();
	const auto terminated = crl::now() - started;
	const auto killed = job
		? TerminateJobObject(job, ERROR_PROCESS_ABORTED)
		: TerminateProcess(process, ERROR_PROCESS_ABORTED);
	const auto killError = killed ? DWORD() : GetLastError();
	auto result = WaitFor(process, killWait);
	if (!result) {
		result = Completion{
			.detail = u"not reaped after %1 ms"_q.arg(crl::now() - started),
		};
	}
	result->detail += u", "_q + (killError
		? QtSystemError(
			job ? "TerminateJobObject" : "TerminateProcess",
			killError)
		: u"terminated after %1 ms"_q.arg(terminated));
	return std::move(*result);
}

[[nodiscard]] Completion Released(Child &child, Completion result) {
	if (!result.reaped || !child.desktop) {
		return result;
	} else if (!CloseDesktop(child.desktop.get())) {
		result.detail += u", "_q
			+ QtSystemError("CloseDesktop", GetLastError());
		return result;
	}
	child.desktop.release();
	return result;
}

[[nodiscard]] Completion Complete(
		Child &child,
		crl::time exitWait,
		crl::time killWait) {
	const auto started = crl::now();
	auto exited = WaitFor(child.process.get(), exitWait);
	if (exited && exited->reaped) {
		return Released(child, std::move(*exited));
	}
	auto result = Terminate(child, started, killWait);
	if (exited) {
		result.detail = exited->detail + u", "_q + result.detail;
	}
	return Released(child, std::move(result));
}

[[nodiscard]] QString Abandon(Child &child, const QString &error) {
	const auto cleanup = Complete(child, 0, kAbandonWait);
	return u"%1 (%2)"_q.arg(error, cleanup.detail);
}

[[nodiscard]] Launched AbandonUnassigned(
		Child child,
		DWORD error,
		DWORD pid) {
	const auto cleanup = Complete(child, 0, kAbandonWait);
	auto text = u"%1 (%2)"_q.arg(
		QtSystemError("AssignProcessToJobObject", error),
		cleanup.detail);
	if (cleanup.reaped) {
		return { .error = std::move(text) };
	}
	return {
		.complete = [child = std::move(child)](
				crl::time exitWait,
				crl::time killWait) mutable {
			return Complete(child, exitWait, killWait);
		},
		.error = std::move(text),
		.pid = int64(pid),
	};
}

} // namespace

QString UnsupportedReason() {
	return QString::fromStdString(SandboxUnsupportedReason());
}

bool Enabled() {
	return UnsupportedReason().isEmpty();
}

Launched Launch(const QStringList &arguments) {
	auto unsupported = UnsupportedReason();
	if (!unsupported.isEmpty()) {
		return { .error = std::move(unsupported) };
	}
	const auto tier = *CurrentSandbox().tier;
	const auto windows7 = (tier == SandboxTier::Windows7);
	const auto assignLater = windows7 || (tier == SandboxTier::AppContainer);
	const auto held = HoldHelper();
	if (!held.error.empty()) {
		return { .error = QString::fromStdString(held.error) };
	}
	auto directory = SystemDirectory();
	if (!directory.error.isEmpty()) {
		return { .error = std::move(directory.error) };
	}
	auto channel = CreateWinChannel(PipeName());
	if (!channel.error.empty()) {
		return { .error = QString::fromStdString(channel.error) };
	}
	auto tokens = CreateTokens();
	if (!tokens.error.empty()) {
		return { .error = QString::fromStdString(tokens.error) };
	}
	auto job = CreateJob();
	if (!job.error.empty()) {
		return { .error = QString::fromStdString(job.error) };
	}
	auto desktop = windows7 ? CreateHelperDesktop() : HelperDesktop();
	if (!desktop.error.isEmpty()) {
		return { .error = std::move(desktop.error) };
	}
	auto created = CreateSuspended({
		.path = held.path,
		.directory = std::move(directory.value),
		.desktop = std::move(desktop.name),
		.arguments = arguments,
		.token = tokens.lockdown.get(),
		.pipe = channel.child.get(),
		.job = assignLater ? nullptr : job.handle.get(),
		.flags = job.creationFlags,
	});
	if (!created.error.isEmpty()) {
		return { .error = std::move(created.error) };
	}
	auto child = Child{
		.process = std::move(created.process),
		.desktop = std::move(desktop.handle),
	};
	if (assignLater
		&& !AssignProcessToJobObject(job.handle.get(), child.process.get())) {
		const auto error = GetLastError();
		return AbandonUnassigned(std::move(child), error, created.pid);
	}
	child.job = std::move(job.handle);
	auto error = QString::fromStdString(
		CheckStartedImage(held, child.process.get()));
	if (error.isEmpty()) {
		error = Start(
			child.process.get(),
			created.thread.get(),
			tokens.initial.get());
	}
	if (!error.isEmpty()) {
		return { .error = Abandon(child, error) };
	}
	return {
		.stream = std::move(channel.parent),
		.complete = [child = std::move(child)](
				crl::time exitWait,
				crl::time killWait) mutable {
			return Complete(child, exitWait, killWait);
		},
		.pid = int64(created.pid),
	};
}

} // namespace Platform::WidgetHelper
