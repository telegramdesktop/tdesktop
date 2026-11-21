/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "test/test_widget_helper_probe_lockdown.h"

#ifdef _DEBUG

#include <winsock2.h>
#include <ws2tcpip.h>
#include <appmodel.h>
#include <bcrypt.h>
#include <delayimp.h>
#include <process.h>
#include <winternl.h>

#include "platform/win/widget_sandbox_win.h"

#include <gsl/gsl>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace Test {
namespace {

using Platform::WidgetHelper::CurrentSandbox;
using Platform::WidgetHelper::DesktopQueries;
using Platform::WidgetHelper::Hex;
using Platform::WidgetHelper::NumberedError;
using Platform::WidgetHelper::PolicyError;
using Platform::WidgetHelper::ReadAssignedDesktop;
using Platform::WidgetHelper::SandboxTier;
using Platform::WidgetHelper::UniqueHandle;
using Platform::WidgetHelper::UserObjectQuery;
using Platform::WidgetHelper::Utf8;
using Platform::WidgetHelper::WindowsVersion;
using Ran = std::shared_ptr<std::atomic<bool>>;

#ifdef _M_IX86
constexpr auto kProbeX86 = true;
#else // _M_IX86
constexpr auto kProbeX86 = false;
#endif // _M_IX86

// The first Windows version whose documentation has each query.
constexpr auto kWindows8 = WindowsVersion{ 6, 2, 0 };
constexpr auto kWindows81 = WindowsVersion{ 6, 3, 0 };
constexpr auto kWindows10 = WindowsVersion{ 10, 0, 10240 };
constexpr auto kWindows1709 = WindowsVersion{ 10, 0, 16299 };
constexpr auto kWindows2004 = WindowsVersion{ 10, 0, 19041 };

constexpr auto kProbePathCapacity = DWORD(32768);
constexpr auto kProbeConnectSeconds = 1;
constexpr auto kProbeWait = DWORD(5000);
constexpr auto kProbeRandomBytes = 32;
constexpr auto kModeTextLimit = std::size_t(64);

// At most this many network targets, the loopback port's four included:
// a TCP one can wait kConnectWaitMs in each phase, all before Ready.
constexpr auto kTargetLimit = 16;
constexpr auto kTargetTextLimit = std::size_t(64);
constexpr auto kPortTextLimit = std::size_t(16);
constexpr auto kMaxPort = 65535;
constexpr auto kConnectWaitMs = 1000;
constexpr auto kBeforeLowering = "before lowering";
constexpr auto kAfterLowering = "after lowering";
constexpr auto kStatusAccessDenied = ULONG(0xC0000022);
constexpr auto kAfdEndpoint = L"\\Device\\Afd\\Endpoint";

// Winsock's own endpoint-open attribute, as mswsock.dll passes it to
// NtCreateFile for socket(): ReactOS's AFD_CREATE_PACKET (endpoint flags,
// group, transport name) with the family, type and protocol that
// mswsock.dll writes between the group and the empty transport name. UDP
// endpoints are AFD_ENDPOINT_CONNECTIONLESS | AFD_ENDPOINT_MESSAGE_ORIENTED.
constexpr auto kAfdEaName = std::string_view("AfdOpenPacketXX");
constexpr auto kAfdConnectionless = ULONG(0x1);
constexpr auto kAfdMessageOriented = ULONG(0x10);

// An argument of the narrow CRT argv back in UTF-16; |ok| is false only
// when the conversion failed.
struct WideText {
	std::wstring text;
	DWORD error = 0;
	bool ok = false;
};

struct TokenInformation {
	std::vector<std::byte> bytes;
	DWORD error = 0;
};

struct ProbeTarget {
	std::string name;
	std::wstring path;
};

struct ProbePolicy {
	PROCESS_MITIGATION_POLICY policy = {};
	DWORD size = 0;
	const char *name = nullptr;
	WindowsVersion documented;
};

// The native probe's controls taken before the lowering and what its
// attempts after it reuse, on the Windows 7 tier its own process token.
struct NativeProbe {
	std::vector<std::string> lines;
	std::vector<ProbeTarget> directories;
	std::wstring executable;
	std::string zone;
	sockaddr_in address = {};
	SOCKET listener = INVALID_SOCKET;
	UniqueHandle token;
	DWORD tokenError = 0;
	bool winsock = false;
};

constexpr auto kProbePolicies = std::array{
	ProbePolicy{
		ProcessDEPPolicy,
		sizeof(PROCESS_MITIGATION_DEP_POLICY),
		"DEP",
		kWindows8,
	},
	ProbePolicy{ ProcessASLRPolicy, sizeof(DWORD), "ASLR", kWindows8 },
	ProbePolicy{
		ProcessDynamicCodePolicy,
		sizeof(DWORD),
		"dynamic code",
		kWindows81,
	},
	ProbePolicy{
		ProcessStrictHandleCheckPolicy,
		sizeof(DWORD),
		"handles",
		kWindows8,
	},
	ProbePolicy{
		ProcessSystemCallDisablePolicy,
		sizeof(DWORD),
		"win32k",
		kWindows8,
	},
	ProbePolicy{
		ProcessExtensionPointDisablePolicy,
		sizeof(DWORD),
		"extension points",
		kWindows8,
	},
	ProbePolicy{
		ProcessSignaturePolicy,
		sizeof(DWORD),
		"signature",
		kWindows10,
	},
	ProbePolicy{
		ProcessChildProcessPolicy,
		sizeof(DWORD),
		"child process",
		kWindows1709,
	},
	ProbePolicy{
		ProcessUserShadowStackPolicy,
		sizeof(DWORD),
		"shadow stack",
		kWindows2004,
	},
	ProbePolicy{
		ProcessFontDisablePolicy,
		sizeof(DWORD),
		"font disable",
		kWindows10,
	},
	ProbePolicy{
		ProcessImageLoadPolicy,
		sizeof(DWORD),
		"image load",
		kWindows10,
	},
};

// FILE_FULL_EA_INFORMATION of the DDK's ntifs.h.
struct FullEaInformation {
	ULONG nextEntryOffset = 0;
	UCHAR flags = 0;
	UCHAR nameLength = 0;
	USHORT valueLength = 0;
	CHAR name[1] = {};
};

struct AfdOpenPacket {
	ULONG endpointFlags = 0;
	LONG group = 0;
	ULONG family = 0;
	ULONG type = 0;
	ULONG protocol = 0;
	ULONG transportNameLength = 0;
	WCHAR transportName[1] = {};
};

struct AfdEa {
	std::vector<ULONG> storage;
	ULONG size = 0;
	ULONG endpointFlags = 0;
};

struct SocketKind {
	int family = 0;
	int type = 0;
	int protocol = 0;
	const char *name = nullptr;
};

constexpr auto kSocketKinds = std::array{
	SocketKind{
		AF_INET,
		SOCK_STREAM,
		IPPROTO_TCP,
		"AF_INET, SOCK_STREAM, IPPROTO_TCP",
	},
	SocketKind{
		AF_INET,
		SOCK_DGRAM,
		IPPROTO_UDP,
		"AF_INET, SOCK_DGRAM, IPPROTO_UDP",
	},
	SocketKind{
		AF_INET6,
		SOCK_STREAM,
		IPPROTO_TCP,
		"AF_INET6, SOCK_STREAM, IPPROTO_TCP",
	},
	SocketKind{
		AF_INET6,
		SOCK_DGRAM,
		IPPROTO_UDP,
		"AF_INET6, SOCK_DGRAM, IPPROTO_UDP",
	},
};

struct NtFunctions {
	decltype(&::NtCreateFile) create = nullptr;
	decltype(&::RtlInitUnicodeString) init = nullptr;
};

class Socket final {
public:
	Socket() = default;
	explicit Socket(SOCKET handle);
	Socket(Socket &&other) noexcept;
	Socket &operator=(Socket &&other) noexcept;
	~Socket();

	[[nodiscard]] SOCKET get() const;
	[[nodiscard]] explicit operator bool() const;
	[[nodiscard]] int close();

private:
	SOCKET _handle = INVALID_SOCKET;

};

struct Target {
	std::string text;
	SOCKADDR_STORAGE address = {};
	int length = 0;
	bool tcp = false;
	Socket preopened;
};

struct Endpoint {
	const SocketKind *kind = nullptr;
	AfdEa ea;
};

// The harness's own network controls and attempts around the lowering,
// parsed from the argv tail; its destructor closes every socket it still
// holds and ends its Winsock start.
class NetworkProbe final {
public:
	explicit NetworkProbe(const std::vector<std::string> &arguments);
	NetworkProbe(const NetworkProbe &) = delete;
	NetworkProbe &operator=(const NetworkProbe &) = delete;
	~NetworkProbe();

	void prepare();
	void attempt();
	[[nodiscard]] const std::vector<std::string> &lines() const;

private:
	void parse(const std::vector<std::string> &arguments);
	[[nodiscard]] std::optional<std::wstring> argument(
		const std::vector<std::string> &arguments,
		std::size_t index);
	void addTarget(const std::wstring &text);
	void malformed(const std::string &what);
	void startWinsock();
	void resolveTargets();
	void resolveNative();
	void attemptKinds(const std::string &phase);
	void attemptTargets(const std::string &phase, const char *markerPhase);
	void attemptEndpoints(const std::string &phase, bool control);
	void preopen();
	void attemptPreopened();
	void close();

	std::string _mode;
	std::vector<std::string> _lines;
	std::vector<std::wstring> _targetTexts;
	std::vector<Target> _targets;
	std::vector<Endpoint> _endpoints;
	NtFunctions _nt;
	UNICODE_STRING _afdName = {};
	int _loopbackPort = 0;
	bool _connectControl = false;
	bool _winsock = false;

};

[[nodiscard]] std::string UpperHex(DWORD value) {
	auto result = Hex(value);
	for (auto &digit : result) {
		if (digit >= 'a' && digit <= 'f') {
			digit = char(digit - 'a' + 'A');
		}
	}
	return result;
}

[[nodiscard]] std::string HexBytes(const void *data, std::size_t size) {
	constexpr auto kDigits = std::string_view("0123456789abcdef");
	const auto bytes = static_cast<const unsigned char*>(data);
	auto result = std::string();
	result.reserve(size * 2);
	for (auto i = std::size_t(); i != size; ++i) {
		result.push_back(kDigits[bytes[i] >> 4]);
		result.push_back(kDigits[bytes[i] & 0x0F]);
	}
	return result;
}

[[nodiscard]] std::wstring_view Left(
		std::wstring_view text,
		std::size_t limit) {
	return text.substr(0, limit);
}

// The CRT made its narrow argv from the command line in the ANSI code page;
// this is the inverse.
[[nodiscard]] WideText Wide(const std::string &text) {
	if (text.empty()) {
		return { .ok = true };
	}
	const auto size = MultiByteToWideChar(
		CP_ACP,
		0,
		text.data(),
		int(text.size()),
		nullptr,
		0);
	if (size <= 0) {
		return { .error = GetLastError() };
	}
	auto result = WideText{ .text = std::wstring(size, wchar_t()) };
	if (!MultiByteToWideChar(
			CP_ACP,
			0,
			text.data(),
			int(text.size()),
			result.text.data(),
			size)) {
		return { .error = GetLastError() };
	}
	result.ok = true;
	return result;
}

[[nodiscard]] std::string Argument(
		const std::vector<std::string> &arguments,
		std::size_t index) {
	return (index < arguments.size()) ? arguments[index] : std::string();
}

template <typename Type>
[[nodiscard]] const Type *As(const TokenInformation &information) {
	return reinterpret_cast<const Type*>(information.bytes.data());
}

[[nodiscard]] TokenInformation ReadToken(
		HANDLE token,
		TOKEN_INFORMATION_CLASS what) {
	auto size = DWORD();
	GetTokenInformation(token, what, nullptr, 0, &size);
	if (!size) {
		return { .error = GetLastError() };
	}
	auto result = TokenInformation{
		.bytes = std::vector<std::byte>(size),
	};
	if (!GetTokenInformation(
			token,
			what,
			result.bytes.data(),
			size,
			&size)) {
		return { .error = GetLastError() };
	}
	return result;
}

[[nodiscard]] DWORD Rid(PSID sid) {
	const auto count = *GetSidSubAuthorityCount(sid);
	return count ? *GetSidSubAuthority(sid, DWORD(count - 1)) : DWORD();
}

template <typename Read>
[[nodiscard]] std::wstring ReadPath(Read &&read) {
	auto result = std::wstring(kProbePathCapacity, wchar_t());
	const auto length = read(result.data(), kProbePathCapacity);
	if (!length || length >= kProbePathCapacity) {
		return std::wstring();
	}
	result.resize(length);
	return result;
}

[[nodiscard]] DWORD OpenDirectory(const std::wstring &path) {
	const auto handle = CreateFileW(
		path.c_str(),
		FILE_LIST_DIRECTORY,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS,
		nullptr);
	if (handle == INVALID_HANDLE_VALUE) {
		return GetLastError();
	}
	const auto guard = UniqueHandle(handle);
	return ERROR_SUCCESS;
}

[[nodiscard]] DWORD OpenKey(HKEY root, const wchar_t *path) {
	auto key = HKEY();
	const auto result = RegOpenKeyExW(root, path, 0, KEY_READ, &key);
	if (result == ERROR_SUCCESS) {
		RegCloseKey(key);
	}
	return DWORD(result);
}

[[nodiscard]] std::string TimeZone() {
	auto zone = TIME_ZONE_INFORMATION();
	const auto id = GetTimeZoneInformation(&zone);
	if (id == TIME_ZONE_ID_INVALID) {
		return "GetTimeZoneInformation failed: error "
			+ std::to_string(GetLastError());
	}
	const auto daylight = (id == TIME_ZONE_ID_DAYLIGHT);
	return "bias "
		+ std::to_string(zone.Bias + (daylight ? zone.DaylightBias : 0));
}

[[nodiscard]] std::string Outcome(DWORD error) {
	return error ? ("error " + std::to_string(error)) : std::string("ok");
}

[[nodiscard]] std::string Denial(DWORD error, DWORD denied) {
	if (!error) {
		return "ALLOWED";
	}
	return std::string((error == denied) ? "denied" : "failed")
		+ " (error "
		+ std::to_string(error)
		+ ')';
}

[[nodiscard]] int ConnectWithin(SOCKET handle, const sockaddr_in &address) {
	auto nonblocking = u_long(1);
	if (ioctlsocket(handle, FIONBIO, &nonblocking) == SOCKET_ERROR) {
		return WSAGetLastError();
	} else if (!::connect(
			handle,
			reinterpret_cast<const sockaddr*>(&address),
			int(sizeof(address)))) {
		return 0;
	}
	const auto error = WSAGetLastError();
	if (error != WSAEWOULDBLOCK) {
		return error;
	}
	auto writable = fd_set();
	auto failed = fd_set();
	FD_SET(handle, &writable);
	FD_SET(handle, &failed);
	auto timeout = timeval{ .tv_sec = kProbeConnectSeconds };
	const auto ready = select(0, nullptr, &writable, &failed, &timeout);
	if (ready == SOCKET_ERROR) {
		return WSAGetLastError();
	} else if (!ready) {
		return WSAETIMEDOUT;
	} else if (!FD_ISSET(handle, &failed)) {
		return 0;
	}
	auto result = 0;
	auto size = int(sizeof(result));
	getsockopt(
		handle,
		SOL_SOCKET,
		SO_ERROR,
		reinterpret_cast<char*>(&result),
		&size);
	return result ? result : WSAECONNREFUSED;
}

unsigned __stdcall ProbeThread(void *argument) {
	const auto ran = std::unique_ptr<Ran>(static_cast<Ran*>(argument));
	**ran = true;
	return 0;
}

// The client's working directory argument as a native path, without the
// trailing separator QDir::cleanPath() dropped: cWorkingDir() is a clean
// absolute path ending with '/'. An empty one stays empty.
[[nodiscard]] std::wstring NativeDirectory(std::wstring path) {
	std::ranges::replace(path, L'/', L'\\');
	while (path.size() > 1
		&& path.back() == L'\\'
		&& !(path.size() == 3 && path[1] == L':')) {
		path.pop_back();
	}
	return path;
}

[[nodiscard]] bool OnWindows7() {
	return (CurrentSandbox().tier == SandboxTier::Windows7);
}

[[nodiscard]] std::string VersionText(const WindowsVersion &version) {
	return std::to_string(version.major)
		+ '.'
		+ std::to_string(version.minor)
		+ '.'
		+ std::to_string(version.build);
}

// Whether the helper's own version reading is at least |since|.
[[nodiscard]] bool Documented(const WindowsVersion &since) {
	const auto &version = CurrentSandbox().version;
	return std::tie(version.major, version.minor, version.build)
		>= std::tie(since.major, since.minor, since.build);
}

[[nodiscard]] std::string NotDocumented(
		const std::string &call,
		const WindowsVersion &since) {
	return "not read ("
		+ call
		+ " is documented from "
		+ VersionText(since)
		+ ')';
}

[[nodiscard]] std::string PolicyCall(PROCESS_MITIGATION_POLICY policy) {
	return "GetProcessMitigationPolicy("
		+ std::to_string(int(policy))
		+ ')';
}

void PrepareProbeNetwork(NativeProbe &probe) {
	const auto bound = __HrLoadAllImportsForDll("WS2_32.dll");
	if (FAILED(bound)) {
		probe.lines.push_back("before lowering: "
			"__HrLoadAllImportsForDll(WS2_32.dll) failed: 0x"
			+ Hex(DWORD(bound)));
		return;
	}
	auto data = WSADATA();
	const auto started = WSAStartup(MAKEWORD(2, 2), &data);
	if (started) {
		probe.lines.push_back("before lowering: WSAStartup failed: error "
			+ std::to_string(started));
		return;
	}
	probe.winsock = true;
	probe.address.sin_family = AF_INET;
	probe.address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	const auto address = reinterpret_cast<sockaddr*>(&probe.address);
	auto size = int(sizeof(probe.address));
	probe.listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (probe.listener == INVALID_SOCKET
		|| ::bind(probe.listener, address, size)
		|| ::listen(probe.listener, 1)
		|| ::getsockname(probe.listener, address, &size)) {
		probe.lines.push_back("before lowering: the loopback listener "
			"failed: error "
			+ std::to_string(WSAGetLastError()));
		return;
	}
	const auto client = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	const auto connected = (client == INVALID_SOCKET)
		? WSAGetLastError()
		: ConnectWithin(client, probe.address);
	const auto accepted = connected
		? INVALID_SOCKET
		: ::accept(probe.listener, nullptr, nullptr);
	const auto error = connected
		? connected
		: (accepted == INVALID_SOCKET)
		? WSAGetLastError()
		: 0;
	for (const auto handle : { client, accepted }) {
		if (handle != INVALID_SOCKET) {
			::closesocket(handle);
		}
	}
	probe.lines.push_back("before lowering: socket, connect to 127.0.0.1:"
		+ std::to_string(ntohs(probe.address.sin_port))
		+ " and accept: "
		+ Outcome(DWORD(error)));
}

void PrepareProbeTargets(
		NativeProbe &probe,
		const std::string &clientDirectory) {
	const auto appData = ReadPath([](wchar_t *buffer, DWORD size) {
		return GetEnvironmentVariableW(L"APPDATA", buffer, size);
	});
	const auto current = ReadPath([](wchar_t *buffer, DWORD size) {
		return GetCurrentDirectoryW(size, buffer);
	});
	const auto add = [&](const std::string &name, const std::wstring &path) {
		probe.directories.push_back({ name, path });
		probe.lines.push_back("before lowering: "
			+ name
			+ ' '
			+ Utf8(path)
			+ ": FILE_LIST_DIRECTORY "
			+ Outcome(OpenDirectory(path)));
	};
	add("helper working directory", current);
	const auto client = Wide(clientDirectory);
	if (!client.ok) {
		probe.lines.push_back("before lowering: client working directory: "
			+ NumberedError("MultiByteToWideChar", client.error));
	} else if (client.text.empty()) {
		probe.lines.push_back(
			"before lowering: client working directory: not given");
	} else {
		add("client working directory", NativeDirectory(client.text));
	}
	add("APPDATA", appData);
	probe.lines.push_back("before lowering: HKEY_CURRENT_USER\\Software: "
		"RegOpenKeyExW "
		+ Outcome(OpenKey(HKEY_CURRENT_USER, L"Software")));
	const auto system = ReadPath([](wchar_t *buffer, DWORD size) {
		return GetSystemDirectoryW(buffer, size);
	});
	probe.executable = system + L"\\whoami.exe";
	const auto present = GetFileAttributesW(probe.executable.c_str())
		!= INVALID_FILE_ATTRIBUTES;
	const auto error = present ? DWORD() : GetLastError();
	probe.lines.push_back("before lowering: "
		+ Utf8(probe.executable)
		+ ": "
		+ (present ? std::string("present") : Outcome(error)));
	probe.zone = TimeZone();
	probe.lines.push_back("before lowering: time zone " + probe.zone);
}

// The Windows 7 tier has no pseudo token handles: open a real one while
// the startup token still grants it.
void PrepareProbeToken(NativeProbe &probe) {
	if (!OnWindows7()) {
		return;
	}
	auto token = HANDLE();
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
		probe.token = UniqueHandle(token);
	} else {
		probe.tokenError = GetLastError();
	}
}

[[nodiscard]] std::string ProbeProcess(const std::wstring &executable) {
	auto command = L"\"" + executable + L"\"";
	auto startup = STARTUPINFOW{ .cb = DWORD(sizeof(STARTUPINFOW)) };
	auto information = PROCESS_INFORMATION();
	const auto created = CreateProcessW(
		executable.c_str(),
		command.data(),
		nullptr,
		nullptr,
		FALSE,
		CREATE_SUSPENDED | CREATE_NO_WINDOW,
		nullptr,
		nullptr,
		&startup,
		&information);
	const auto error = created ? DWORD() : GetLastError();
	const auto name = "CreateProcessW " + Utf8(executable) + ": ";
	if (!created) {
		return name + Denial(error, ERROR_ACCESS_DENIED);
	}
	const auto process = UniqueHandle(information.hProcess);
	const auto thread = UniqueHandle(information.hThread);
	const auto terminated = TerminateProcess(process.get(), 1);
	const auto waited = WaitForSingleObject(process.get(), kProbeWait);
	return name
		+ "ALLOWED (pid "
		+ std::to_string(information.dwProcessId)
		+ ", terminated "
		+ std::to_string(terminated)
		+ ", wait "
		+ std::to_string(waited)
		+ ')';
}

[[nodiscard]] std::string ProbeSocket(const NativeProbe &probe) {
	if (probe.listener == INVALID_SOCKET) {
		return "socket: not tried, the loopback listener did not start";
	}
	const auto created = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	const auto error = (created == INVALID_SOCKET) ? WSAGetLastError() : 0;
	const auto name = std::string("socket(AF_INET, SOCK_STREAM): ");
	if (created == INVALID_SOCKET) {
		return name + Denial(DWORD(error), WSAEACCES);
	}
	const auto connected = ConnectWithin(created, probe.address);
	::closesocket(created);
	return name
		+ "ALLOWED, connect to 127.0.0.1:"
		+ std::to_string(ntohs(probe.address.sin_port))
		+ ": "
		+ (connected
			? Denial(DWORD(connected), WSAEACCES)
			: std::string("CONNECTED"));
}

[[nodiscard]] std::string ProbeToken(const NativeProbe &probe) {
	const auto windows7 = OnWindows7();
	if (windows7 && !probe.token) {
		return "process token: "
			+ NumberedError("OpenProcessToken", probe.tokenError)
			+ " before the lowering";
	}
	const auto token = windows7
		? probe.token.get()
		: GetCurrentProcessToken();
	const auto label = ReadToken(token, TokenIntegrityLevel);
	const auto privileges = ReadToken(token, TokenPrivileges);
	const auto failed = label.bytes.empty()
		? &label
		: privileges.bytes.empty()
		? &privileges
		: nullptr;
	if (failed) {
		return "process token: "
			+ NumberedError("GetTokenInformation", failed->error);
	}
	const auto sid = As<TOKEN_MANDATORY_LABEL>(label)->Label.Sid;
	const auto count = As<TOKEN_PRIVILEGES>(privileges)->PrivilegeCount;
	const auto check = Platform::WidgetHelper::CheckLockdownToken(token);
	return "process token: integrity 0x"
		+ Hex(Rid(sid))
		+ ", restricted "
		+ std::to_string(IsTokenRestricted(token))
		+ ", privileges "
		+ std::to_string(count)
		+ ", lockdown check "
		+ (check.empty() ? std::string("passed") : check);
}

[[nodiscard]] std::string ProbeJob() {
	const auto job = Platform::WidgetHelper::ReadJob();
	const auto check = Platform::WidgetHelper::VerifyJob();
	const auto &basic = job.limits.BasicLimitInformation;
	return "job: in a job "
		+ std::to_string(job.inJob)
		+ ", limits 0x"
		+ Hex(basic.LimitFlags)
		+ ", active processes "
		+ std::to_string(basic.ActiveProcessLimit)
		+ ", UI 0x"
		+ Hex(job.restrictions.UIRestrictionsClass)
		+ ", check "
		+ (check.empty() ? std::string("passed") : check);
}

[[nodiscard]] const char *TierName(std::optional<SandboxTier> tier) {
	if (!tier) {
		return "none";
	}
	switch (*tier) {
	case SandboxTier::Windows7: return "Windows 7";
	case SandboxTier::AppContainer: return "AppContainer";
	case SandboxTier::Lpac1703: return "LPAC 1703";
	case SandboxTier::Full: return "full";
	}
	return "unknown";
}

// The helper's own choice, cached by the shared unit: never an input.
[[nodiscard]] std::string ProbeTier() {
	const auto &sandbox = CurrentSandbox();
	return "tier: "
		+ std::string(TierName(sandbox.tier))
		+ ", version "
		+ VersionText(sandbox.version);
}

// Windows 7 only: the serving thread's window station and desktop after
// the lowering, looked up in the user32.dll that the lowering mapped. The
// outside reading of the thread's desktop is the evidence, not this row.
[[nodiscard]] std::string ProbeDesktop() {
	using GetStation = decltype(&::GetProcessWindowStation);
	using GetDesktop = decltype(&::GetThreadDesktop);
	const auto user32 = GetModuleHandleW(L"user32.dll");
	if (!user32) {
		return "desktop: not read (user32.dll is not mapped)";
	}
	const auto queries = DesktopQueries{
		.station = reinterpret_cast<GetStation>(
			GetProcAddress(user32, "GetProcessWindowStation")),
		.threadDesktop = reinterpret_cast<GetDesktop>(
			GetProcAddress(user32, "GetThreadDesktop")),
		.information = reinterpret_cast<UserObjectQuery>(
			GetProcAddress(user32, "GetUserObjectInformationW")),
	};
	if (!queries.station || !queries.threadDesktop || !queries.information) {
		return "desktop: not read (a user32.dll function is missing)";
	}
	const auto thread = GetCurrentThreadId();
	const auto desktop = ReadAssignedDesktop(queries, thread);
	if (desktop.failed) {
		return "desktop: " + NumberedError(desktop.failed, desktop.error);
	}
	return "desktop: "
		+ Utf8(desktop.name)
		+ " (thread "
		+ std::to_string(thread)
		+ ')';
}

[[nodiscard]] std::string ProbePackage() {
	if (!Documented(kWindows8)) {
		return "package identity: "
			+ NotDocumented("GetCurrentPackageFullName", kWindows8);
	}
	using GetFullName = decltype(&GetCurrentPackageFullName);
	auto get = GetFullName();
	if (const auto kernel = GetModuleHandleW(L"kernel32.dll")) {
		get = reinterpret_cast<GetFullName>(
			GetProcAddress(kernel, "GetCurrentPackageFullName"));
	}
	if (!get) {
		return "package identity: not available "
			"(GetCurrentPackageFullName is missing)";
	}
	auto length = UINT32();
	auto result = get(&length, nullptr);
	auto name = std::wstring();
	if (result == ERROR_INSUFFICIENT_BUFFER) {
		name.resize(length);
		result = get(&length, name.data());
	}
	if (result == APPMODEL_ERROR_NO_PACKAGE) {
		return "package identity: none (APPMODEL_ERROR_NO_PACKAGE)";
	} else if (result != ERROR_SUCCESS) {
		return "package identity: "
			+ NumberedError("GetCurrentPackageFullName", DWORD(result));
	}
	return "package identity: " + Utf8(name.c_str());
}

// The helper disables win32k system calls at its creation: read it back.
[[nodiscard]] std::string ProbeWin32k() {
	if (!Documented(kWindows8)) {
		return "win32k system calls: "
			+ NotDocumented(
				PolicyCall(ProcessSystemCallDisablePolicy),
				kWindows8);
	}
	const auto flags = Platform::WidgetHelper::ReadMitigation(
		ProcessSystemCallDisablePolicy,
		sizeof(PROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY));
	const auto error = flags ? DWORD() : GetLastError();
	if (!flags) {
		return "win32k system calls: not read ("
			+ PolicyError(
				"GetProcessMitigationPolicy",
				ProcessSystemCallDisablePolicy,
				error)
			+ ')';
	}
	auto policy = PROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY();
	policy.Flags = *flags;
	return std::string("win32k system calls: ")
		+ (policy.DisallowWin32kSystemCalls ? "DISABLED" : "ENABLED")
		+ " (ProcessSystemCallDisablePolicy 0x"
		+ Hex(*flags)
		+ ')';
}

// A policy before the version that documents its query: not read, but an
// x86 process reads its DEP through GetProcessDEPPolicy.
[[nodiscard]] std::string ProbeUndocumented(const ProbePolicy &entry) {
	if constexpr (kProbeX86) {
		if (entry.policy == ProcessDEPPolicy) {
			auto flags = DWORD();
			auto permanent = BOOL();
			if (!GetProcessDEPPolicy(
					GetCurrentProcess(),
					&flags,
					&permanent)) {
				return NumberedError("GetProcessDEPPolicy", GetLastError());
			}
			return "0x"
				+ Hex(flags)
				+ ", permanent "
				+ std::to_string(permanent)
				+ " (GetProcessDEPPolicy)";
		}
	}
	return NotDocumented(PolicyCall(entry.policy), entry.documented);
}

[[nodiscard]] std::string ProbeMitigation(const ProbePolicy &entry) {
	const auto name = std::string("mitigation ") + entry.name + ": ";
	if (!Documented(entry.documented)) {
		return name + ProbeUndocumented(entry);
	}
	const auto flags = Platform::WidgetHelper::ReadMitigation(
		entry.policy,
		entry.size);
	const auto error = flags ? DWORD() : GetLastError();
	return name
		+ (flags
			? ("0x" + Hex(*flags))
			: ("error " + std::to_string(error)));
}

[[nodiscard]] std::string ProbeThreadCreation() {
	const auto ran = std::make_shared<std::atomic<bool>>(false);
	auto argument = std::make_unique<Ran>(ran);
	const auto thread = UniqueHandle(reinterpret_cast<HANDLE>(_beginthreadex(
		nullptr,
		0,
		ProbeThread,
		argument.get(),
		0,
		nullptr)));
	if (!thread) {
		return "_beginthreadex failed: errno " + std::to_string(errno);
	}
	argument.release();
	const auto waited = WaitForSingleObject(thread.get(), kProbeWait);
	if (waited != WAIT_OBJECT_0) {
		return "_beginthreadex: the thread did not end in "
			+ std::to_string(kProbeWait)
			+ " ms (wait "
			+ std::to_string(waited)
			+ ')';
	}
	return *ran
		? "_beginthreadex: the thread ran and joined"
		: "_beginthreadex: the thread ended without running";
}

[[nodiscard]] std::string ProbeEvent() {
	const auto event = UniqueHandle(
		CreateEventW(nullptr, TRUE, FALSE, nullptr));
	if (!event) {
		return "event: CreateEventW failed: error "
			+ std::to_string(GetLastError());
	} else if (!SetEvent(event.get())) {
		return "event: SetEvent failed: error "
			+ std::to_string(GetLastError());
	}
	const auto waited = WaitForSingleObject(event.get(), 0);
	const auto error = GetLastError();
	return (waited == WAIT_OBJECT_0)
		? std::string("event: created, signalled and waited")
		: ("event: wait "
			+ std::to_string(waited)
			+ ", error "
			+ std::to_string(error));
}

[[nodiscard]] std::string ProbeOwnProcess() {
	const auto pid = GetCurrentProcessId();
	const auto own = UniqueHandle(OpenProcess(
		PROCESS_QUERY_LIMITED_INFORMATION,
		FALSE,
		pid));
	const auto error = own ? DWORD() : GetLastError();
	return "OpenProcess(own pid "
		+ std::to_string(pid)
		+ "): "
		+ (own ? std::string("opened") : ("error " + std::to_string(error)));
}

[[nodiscard]] std::string ProbeRandom() {
	auto bytes = std::array<UCHAR, kProbeRandomBytes>();
	const auto status = BCryptGenRandom(
		nullptr,
		bytes.data(),
		ULONG(bytes.size()),
		BCRYPT_USE_SYSTEM_PREFERRED_RNG);
	const auto nonzero = std::ranges::any_of(bytes, [](UCHAR value) {
		return value != 0;
	});
	SecureZeroMemory(bytes.data(), bytes.size());
	return "BCryptGenRandom("
		+ std::to_string(kProbeRandomBytes)
		+ " bytes): status 0x"
		+ Hex(DWORD(status))
		+ ", "
		+ (nonzero ? "nonzero output" : "ALL ZERO");
}

void CloseProbeNetwork(NativeProbe &probe) {
	if (probe.listener != INVALID_SOCKET) {
		::closesocket(probe.listener);
		probe.listener = INVALID_SOCKET;
	}
	if (std::exchange(probe.winsock, false)) {
		WSACleanup();
	}
}

[[nodiscard]] std::vector<std::string> FinishProbe(NativeProbe &probe) {
	auto lines = std::move(probe.lines);
	const auto add = [&](const std::string &line) {
		lines.push_back("after lowering: " + line);
	};
	add(ProbeProcess(probe.executable));
	add(ProbeSocket(probe));
	for (const auto &directory : probe.directories) {
		add(directory.name
			+ ' '
			+ Utf8(directory.path)
			+ ": FILE_LIST_DIRECTORY "
			+ Denial(OpenDirectory(directory.path), ERROR_ACCESS_DENIED));
	}
	add("HKEY_CURRENT_USER\\Software (opened before lowering): "
		"RegOpenKeyExW "
		+ Denial(
			OpenKey(HKEY_CURRENT_USER, L"Software"),
			ERROR_ACCESS_DENIED));
	add("HKEY_LOCAL_MACHINE\\SOFTWARE (not opened before lowering): "
		"RegOpenKeyExW "
		+ Denial(
			OpenKey(HKEY_LOCAL_MACHINE, L"SOFTWARE"),
			ERROR_ACCESS_DENIED));
	const auto thread = Platform::WidgetHelper::VerifyNoThreadToken();
	add("thread token: "
		+ (thread.empty() ? std::string("none (ERROR_NO_TOKEN)") : thread));
	add(ProbeToken(probe));
	probe.token.reset();
	add(ProbeJob());
	add(ProbeTier());
	if (OnWindows7()) {
		add(ProbeDesktop());
	}
	add(ProbePackage());
	add(ProbeWin32k());
	for (const auto &entry : kProbePolicies) {
		add(ProbeMitigation(entry));
	}
	add(ProbeThreadCreation());
	add(ProbeEvent());
	add(ProbeOwnProcess());
	add(ProbeRandom());
	add("time zone " + TimeZone() + " (before lowering " + probe.zone + ')');
	CloseProbeNetwork(probe);
	return lines;
}

Socket::Socket(SOCKET handle)
: _handle(handle) {
}

Socket::Socket(Socket &&other) noexcept
: _handle(std::exchange(other._handle, INVALID_SOCKET)) {
}

Socket &Socket::operator=(Socket &&other) noexcept {
	if (this != &other) {
		if (_handle != INVALID_SOCKET) {
			::closesocket(_handle);
		}
		_handle = std::exchange(other._handle, INVALID_SOCKET);
	}
	return *this;
}

Socket::~Socket() {
	if (_handle != INVALID_SOCKET) {
		::closesocket(_handle);
	}
}

SOCKET Socket::get() const {
	return _handle;
}

Socket::operator bool() const {
	return (_handle != INVALID_SOCKET);
}

int Socket::close() {
	const auto handle = std::exchange(_handle, INVALID_SOCKET);
	return (handle == INVALID_SOCKET || !::closesocket(handle))
		? 0
		: WSAGetLastError();
}

// QString::toInt()'s reading of the loopback port: surrounding white space,
// an optional sign and decimal digits; empty when that is no number from 0
// to 65535.
[[nodiscard]] std::optional<int> ParsePort(std::wstring_view text) {
	constexpr auto kSpace = std::wstring_view(L" \t\n\v\f\r");
	const auto first = text.find_first_not_of(kSpace);
	if (first == std::wstring_view::npos) {
		return std::nullopt;
	}
	text = text.substr(first, text.find_last_not_of(kSpace) + 1 - first);
	const auto negative = text.starts_with(L'-');
	if (negative || text.starts_with(L'+')) {
		text.remove_prefix(1);
	}
	if (text.empty()) {
		return std::nullopt;
	}
	auto value = 0;
	for (const auto digit : text) {
		if (digit < L'0' || digit > L'9') {
			return std::nullopt;
		}
		value = value * 10 + int(digit - L'0');
		if (value > kMaxPort) {
			return std::nullopt;
		}
	}
	if (negative && value) {
		return std::nullopt;
	}
	return value;
}

// Only WSAEACCES is a denial: a missing family, a bad argument or an
// unreachable target fails without proving anything.
[[nodiscard]] std::string Failure(int error) {
	return std::string((error == WSAEACCES) ? "denied" : "failed")
		+ " (error "
		+ std::to_string(error)
		+ ')';
}

[[nodiscard]] std::string StatusVerdict(NTSTATUS status) {
	const auto verdict = NT_SUCCESS(status)
		? "ALLOWED"
		: (ULONG(status) == kStatusAccessDenied)
		? "denied"
		: "failed";
	return std::string(verdict)
		+ " (status 0x"
		+ UpperHex(DWORD(status))
		+ ')';
}

[[nodiscard]] std::string Marker(const std::string &mode, const char *phase) {
	return "widget-helper-probe "
		+ std::to_string(GetCurrentProcessId())
		+ ' '
		+ phase
		+ ' '
		+ mode
		+ '\n';
}

[[nodiscard]] bool HasPort(const Target &target) {
	return (target.address.ss_family == AF_INET)
		? (reinterpret_cast<const sockaddr_in&>(target.address).sin_port != 0)
		: (reinterpret_cast<const sockaddr_in6&>(target.address).sin6_port
			!= 0);
}

[[nodiscard]] Socket CreateSocket(const Target &target) {
	return Socket(::socket(
		target.address.ss_family,
		target.tcp ? SOCK_STREAM : SOCK_DGRAM,
		target.tcp ? IPPROTO_TCP : IPPROTO_UDP));
}

[[nodiscard]] std::string MakeNonblocking(SOCKET handle) {
	auto nonblocking = u_long(1);
	return (ioctlsocket(handle, FIONBIO, &nonblocking) == SOCKET_ERROR)
		? ("FIONBIO failed (error " + std::to_string(WSAGetLastError()) + ')')
		: std::string();
}

// A nonblocking connect judged by SO_ERROR once select() saw it complete:
// empty when connected, else the verdict. Writability alone, a pending
// WSAEWOULDBLOCK or a timeout is no success and no denial.
[[nodiscard]] std::string Connect(SOCKET handle, const Target &target) {
	if (!::connect(
			handle,
			reinterpret_cast<const sockaddr*>(&target.address),
			target.length)) {
		return std::string();
	}
	const auto error = WSAGetLastError();
	if (error != WSAEWOULDBLOCK) {
		return Failure(error);
	}
	auto writable = fd_set();
	auto failed = fd_set();
	FD_SET(handle, &writable);
	FD_SET(handle, &failed);
	auto timeout = timeval{ .tv_sec = kConnectWaitMs / 1000 };
	const auto ready = select(0, nullptr, &writable, &failed, &timeout);
	if (ready == SOCKET_ERROR) {
		return "failed (select error "
			+ std::to_string(WSAGetLastError())
			+ ')';
	} else if (!ready) {
		return "failed (no completion within "
			+ std::to_string(kConnectWaitMs)
			+ " ms)";
	}
	auto pending = 0;
	auto size = int(sizeof(pending));
	if (getsockopt(
			handle,
			SOL_SOCKET,
			SO_ERROR,
			reinterpret_cast<char*>(&pending),
			&size) == SOCKET_ERROR) {
		return "failed (SO_ERROR unreadable: error "
			+ std::to_string(WSAGetLastError())
			+ ')';
	} else if (pending) {
		return Failure(pending);
	} else if (!FD_ISSET(handle, &writable)) {
		return "failed (SO_ERROR 0 without a completed connect)";
	}
	return std::string();
}

// Takes a nonblocking socket: connects TCP and sends |marker|, or sends
// |marker| in one UDP datagram, a send that would block failing at once
// (WSAEWOULDBLOCK, never a denial); the receiving fixture decides delivery.
[[nodiscard]] std::string Exchange(
		SOCKET handle,
		const Target &target,
		const std::string &marker) {
	if (!target.tcp) {
		const auto sent = ::sendto(
			handle,
			marker.data(),
			int(marker.size()),
			0,
			reinterpret_cast<const sockaddr*>(&target.address),
			target.length);
		return (sent == SOCKET_ERROR)
			? ("sendto " + Failure(WSAGetLastError()))
			: ("sendto SENT (" + std::to_string(sent) + " bytes)");
	}
	const auto connected = Connect(handle, target);
	if (!connected.empty()) {
		return "connect " + connected;
	}
	const auto sent = ::send(handle, marker.data(), int(marker.size()), 0);
	return (sent == int(marker.size()))
		? ("connect CONNECTED, marker sent ("
			+ std::to_string(sent)
			+ " bytes)")
		: ("connect CONNECTED, marker send failed ("
			+ ((sent == SOCKET_ERROR)
				? ("error " + std::to_string(WSAGetLastError()))
				: (std::to_string(sent)
					+ " of "
					+ std::to_string(marker.size())
					+ " bytes"))
			+ ')');
}

[[nodiscard]] AfdEa AfdEndpointEa(const SocketKind &kind) {
	const auto packet = AfdOpenPacket{
		.endpointFlags = (kind.type == SOCK_DGRAM)
			? (kAfdConnectionless | kAfdMessageOriented)
			: ULONG(0),
		.family = ULONG(kind.family),
		.type = ULONG(kind.type),
		.protocol = ULONG(kind.protocol),
	};
	const auto header = FullEaInformation{
		.nameLength = UCHAR(kAfdEaName.size()),
		.valueLength = USHORT(sizeof(packet) + sizeof(WCHAR)),
	};
	const auto value = offsetof(FullEaInformation, name)
		+ header.nameLength
		+ 1;

	// Sized as mswsock.dll sizes it; the bytes past the packet stay zero.
	const auto size = sizeof(header) + header.nameLength + header.valueLength;
	auto result = AfdEa{
		.storage = std::vector<ULONG>(
			(size + sizeof(ULONG) - 1) / sizeof(ULONG)),
		.size = ULONG(size),
		.endpointFlags = packet.endpointFlags,
	};
	const auto bytes = reinterpret_cast<char*>(result.storage.data());
	std::memcpy(bytes, &header, offsetof(FullEaInformation, name));
	std::memcpy(
		bytes + offsetof(FullEaInformation, name),
		kAfdEaName.data(),
		kAfdEaName.size());
	std::memcpy(
		bytes + value,
		&packet,
		offsetof(AfdOpenPacket, transportName));
	return result;
}

[[nodiscard]] NTSTATUS OpenEndpoint(
		const NtFunctions &nt,
		UNICODE_STRING &name,
		AfdEa &ea) {
	auto attributes = OBJECT_ATTRIBUTES();
	InitializeObjectAttributes(
		&attributes,
		&name,
		OBJ_CASE_INSENSITIVE,
		nullptr,
		nullptr);
	auto handle = HANDLE();
	auto io = IO_STATUS_BLOCK();
	const auto status = nt.create(
		&handle,
		GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
		&attributes,
		&io,
		nullptr,
		0,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		FILE_OPEN_IF,
		0,
		ea.storage.data(),
		ea.size);
	if (!NT_SUCCESS(status)) {
		return status;
	}
	const auto guard = gsl::finally([&] {
		CloseHandle(handle);
	});
	return status;
}

NetworkProbe::NetworkProbe(const std::vector<std::string> &arguments)
: _mode(Argument(arguments, 1)) {
	parse(arguments);
}

NetworkProbe::~NetworkProbe() {
	close();
}

const std::vector<std::string> &NetworkProbe::lines() const {
	return _lines;
}

void NetworkProbe::parse(const std::vector<std::string> &arguments) {
	const auto port = argument(arguments, 5);
	if (port && !port->empty()) {
		const auto value = ParsePort(*port);
		if (!value) {
			malformed("loopback port \""
				+ Utf8(Left(*port, kPortTextLimit))
				+ '"');
		} else if (*value > 0) {
			_loopbackPort = *value;
			for (const auto host : { L"127.0.0.1", L"[::1]" }) {
				for (const auto protocol : { L"tcp", L"udp" }) {
					addTarget(std::wstring(protocol)
						+ L':'
						+ host
						+ L':'
						+ std::to_wstring(*value));
				}
			}
		}
	}
	for (auto i = std::size_t(6); i < arguments.size(); ++i) {
		if (arguments[i] == kWidgetHelperConnectControl) {
			_connectControl = true;
		} else if (const auto text = argument(arguments, i)) {
			if (text->starts_with(L"tcp:") || text->starts_with(L"udp:")) {
				addTarget(*text);
			} else {
				malformed('"' + Utf8(Left(*text, kTargetTextLimit)) + '"');
			}
		}
	}
}

std::optional<std::wstring> NetworkProbe::argument(
		const std::vector<std::string> &arguments,
		std::size_t index) {
	auto result = Wide(Argument(arguments, index));
	if (!result.ok) {
		malformed("argument "
			+ std::to_string(index)
			+ ": "
			+ NumberedError("MultiByteToWideChar", result.error));
		return std::nullopt;
	}
	return std::move(result.text);
}

void NetworkProbe::addTarget(const std::wstring &text) {
	if (text.size() > kTargetTextLimit) {
		malformed("target \""
			+ Utf8(Left(text, kTargetTextLimit))
			+ "...\": longer than "
			+ std::to_string(kTargetTextLimit)
			+ " characters");
	} else {
		_targetTexts.push_back(text);
	}
}

void NetworkProbe::malformed(const std::string &what) {
	_lines.push_back("argument: malformed " + what);
}

void NetworkProbe::startWinsock() {
	const auto bound = __HrLoadAllImportsForDll("WS2_32.dll");
	if (FAILED(bound)) {
		_lines.push_back(std::string(kBeforeLowering)
			+ ": harness Winsock: "
			"__HrLoadAllImportsForDll(WS2_32.dll) failed: 0x"
			+ Hex(DWORD(bound)));
		return;
	}
	auto data = WSADATA();
	const auto started = WSAStartup(MAKEWORD(2, 2), &data);
	_winsock = !started;
	_lines.push_back(std::string(kBeforeLowering)
		+ ": harness Winsock: WS2_32 bound, WSAStartup(2.2) "
		+ (started
			? ("failed (error " + std::to_string(started) + ')')
			: std::string("ok")));
}

void NetworkProbe::resolveTargets() {
	for (const auto &text : _targetTexts) {
		auto target = Target{
			.text = Utf8(text),
			.tcp = text.starts_with(L"tcp:"),
		};
		auto address = text.substr(4);
		const auto read = [&](int family) {
			target.length = int(sizeof(target.address));
			return !WSAStringToAddressW(
				address.data(),
				family,
				nullptr,
				reinterpret_cast<sockaddr*>(&target.address),
				&target.length);
		};
		if (!read(AF_INET) && !read(AF_INET6)) {
			malformed("target \""
				+ target.text
				+ "\": WSAStringToAddressW failed: error "
				+ std::to_string(WSAGetLastError()));
		} else if (!HasPort(target)) {
			malformed("target \"" + target.text + "\": no port");
		} else if (int(_targets.size()) >= kTargetLimit) {
			malformed("target \""
				+ target.text
				+ "\": past the limit of "
				+ std::to_string(kTargetLimit)
				+ " targets");
		} else {
			_targets.push_back(std::move(target));
		}
	}
}

void NetworkProbe::resolveNative() {
	if (const auto ntdll = GetModuleHandleW(L"ntdll.dll")) {
		_nt.create = reinterpret_cast<decltype(_nt.create)>(
			GetProcAddress(ntdll, "NtCreateFile"));
		_nt.init = reinterpret_cast<decltype(_nt.init)>(
			GetProcAddress(ntdll, "RtlInitUnicodeString"));
	}
	if (!_nt.create || !_nt.init) {
		_nt = NtFunctions();
		return;
	}
	_nt.init(&_afdName, kAfdEndpoint);
	for (const auto &kind : kSocketKinds) {
		_endpoints.push_back({ &kind, AfdEndpointEa(kind) });
	}
}

void NetworkProbe::attemptKinds(const std::string &phase) {
	for (const auto &kind : kSocketKinds) {
		const auto socket = Socket(
			::socket(kind.family, kind.type, kind.protocol));
		const auto error = socket ? 0 : WSAGetLastError();
		_lines.push_back(phase
			+ ": socket("
			+ kind.name
			+ "): "
			+ (error ? Failure(error) : std::string("ALLOWED")));
	}
}

void NetworkProbe::attemptTargets(
		const std::string &phase,
		const char *markerPhase) {
	const auto marker = Marker(_mode, markerPhase);
	for (const auto &target : _targets) {
		const auto socket = CreateSocket(target);
		const auto error = socket ? 0 : WSAGetLastError();
		const auto nonblocking = socket
			? MakeNonblocking(socket.get())
			: std::string();
		const auto use = std::string(target.tcp ? "connect" : "sendto");
		const auto result = !socket
			? ("socket "
				+ Failure(error)
				+ ", "
				+ use
				+ " not tried, socket creation "
				+ ((error == WSAEACCES) ? "was denied" : "failed"))
			: !nonblocking.empty()
			? ("socket ALLOWED, " + nonblocking + ", " + use + " not tried")
			: ("socket ALLOWED, " + Exchange(socket.get(), target, marker));
		_lines.push_back(phase + ": target " + target.text + ": " + result);
	}
}

void NetworkProbe::attemptEndpoints(const std::string &phase, bool control) {
	if (!_nt.create) {
		_lines.push_back(phase + ": AFD endpoint: not tried, "
			"ntdll's NtCreateFile or RtlInitUnicodeString is missing");
		return;
	}
	for (auto &endpoint : _endpoints) {
		const auto status = OpenEndpoint(_nt, _afdName, endpoint.ea);
		auto line = phase
			+ ": AFD endpoint "
			+ endpoint.kind->name
			+ ": NtCreateFile("
			+ Utf8(kAfdEndpoint)
			+ ", EA "
			+ std::string(kAfdEaName)
			+ " of "
			+ std::to_string(endpoint.ea.size)
			+ " bytes, endpoint flags 0x"
			+ Hex(endpoint.ea.endpointFlags)
			+ "): "
			+ StatusVerdict(status);
		if (control) {
			line += "; EA bytes "
				+ HexBytes(endpoint.ea.storage.data(), endpoint.ea.size);
		}
		_lines.push_back(std::move(line));
	}
}

void NetworkProbe::preopen() {
	for (auto &target : _targets) {
		target.preopened = CreateSocket(target);
		const auto error = target.preopened ? 0 : WSAGetLastError();
		const auto nonblocking = target.preopened
			? MakeNonblocking(target.preopened.get())
			: std::string();
		if (!nonblocking.empty()) {
			target.preopened = Socket();
		}
		_lines.push_back("connect-control: "
			+ std::string(kBeforeLowering)
			+ ": target "
			+ target.text
			+ ": preopened socket "
			+ (error
				? Failure(error)
				: !nonblocking.empty()
				? ("ALLOWED, " + nonblocking + ", closed")
				: std::string("ALLOWED, unconnected and nonblocking")));
	}
}

void NetworkProbe::attemptPreopened() {
	const auto phase = std::string(kAfterLowering);
	const auto marker = Marker(_mode, "connect-control");
	auto used = 0;
	for (auto &target : _targets) {
		if (!target.preopened) {
			_lines.push_back("connect-control: "
				+ phase
				+ ": target "
				+ target.text
				+ ": not tried, no socket was preopened");
			continue;
		}
		++used;
		const auto result = Exchange(target.preopened.get(), target, marker);
		const auto closed = target.preopened.close();
		_lines.push_back("connect-control: "
			+ phase
			+ ": target "
			+ target.text
			+ ": "
			+ result
			+ "; closesocket "
			+ (closed
				? ("failed (error " + std::to_string(closed) + ')')
				: std::string("ok")));
	}
	_lines.push_back("connect-control: "
		+ std::to_string(used)
		+ " preopened sockets used and closed before Ready; this run holds "
		"injected handles and is no ordinary helper");
}

void NetworkProbe::prepare() {
	const auto phase = std::string(kBeforeLowering);
	startWinsock();
	if (_winsock) {
		resolveTargets();
	}
	auto summary = "arguments: loopback port "
		+ (_loopbackPort
			? std::to_string(_loopbackPort)
			: std::string("absent"))
		+ ", "
		+ (_winsock
			? std::to_string(_targets.size())
			: (std::to_string(_targetTexts.size()) + " unresolved"))
		+ " network targets, connect-control "
		+ (_connectControl ? "yes" : "no");
	_lines.insert(_lines.begin(), std::move(summary));
	if (_winsock) {
		attemptKinds(phase);
		attemptTargets(phase, "before-lowering");
	} else {
		_lines.push_back(phase
			+ ": sockets and targets: not tried, Winsock is not available");
	}
	resolveNative();
	attemptEndpoints(phase, true);
	if (_connectControl && _winsock) {
		preopen();
	}
}

void NetworkProbe::attempt() {
	const auto phase = std::string(kAfterLowering);
	if (_winsock) {
		attemptKinds(phase);
		attemptTargets(phase, "after-lowering");
	} else {
		_lines.push_back(phase
			+ ": sockets and targets: not tried, Winsock is not available");
	}
	attemptEndpoints(phase, false);
	if (_connectControl && _winsock) {
		attemptPreopened();
	} else if (_connectControl) {
		_lines.push_back(
			"connect-control: not tried, Winsock is not available");
	}
}

void NetworkProbe::close() {
	for (auto &target : _targets) {
		target.preopened = Socket();
	}
	if (std::exchange(_winsock, false)) {
		WSACleanup();
	}
}

// The native probe takes its controls before the real lockdown, while the
// helper still impersonates, then the harness takes its own network
// controls; both make their attempts after the lockdown, the network ones
// first. The report goes to stderr, every socket either part holds and the
// Windows 7 tier's token handle are closed before it returns, a refused
// lockdown included, and the helper serves widgets.
[[nodiscard]] std::string LockDownWithProbe(
		const std::vector<std::string> &arguments,
		const std::function<std::string()> &lockdown) {
	if (!lockdown) {
		return "no lockdown callable";
	}
	auto probe = NativeProbe();
	const auto closeProbe = gsl::finally([&] {
		CloseProbeNetwork(probe);
	});
	PrepareProbeNetwork(probe);
	PrepareProbeTargets(probe, Argument(arguments, 2));
	PrepareProbeToken(probe);
	auto network = NetworkProbe(arguments);
	network.prepare();
	const auto error = lockdown();
	if (!error.empty()) {
		return error;
	}
	network.attempt();
	auto lines = FinishProbe(probe);
	const auto &own = network.lines();
	lines.insert(lines.end(), own.begin(), own.end());
	for (const auto &line : lines) {
		std::fprintf(stderr, "Widget helper probe: %s\n", line.c_str());
	}
	return std::string();
}

} // namespace

std::string RefuseWidgetHelperMode(const std::string &mode) {
	const auto wide = Wide(mode);
	return wide.ok
		? ("unknown widget helper mode \""
			+ Utf8(Left(wide.text, kModeTextLimit))
			+ '"')
		: ("unknown widget helper mode, "
			+ NumberedError("MultiByteToWideChar", wide.error));
}

std::string RunWidgetHelperProbe(
		bool sandboxed,
		const std::vector<std::string> &arguments,
		const std::function<std::string()> &lockdown) {
	if (sandboxed) {
		return LockDownWithProbe(arguments, lockdown);
	}
	return "the widget helper probe is not available on this platform";
}

} // namespace Test

#endif // _DEBUG
