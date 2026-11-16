/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/widget_sandbox_win.h"

#include "base/platform/win/base_windows_safe_library.h"
#include "base/platform/base_platform_info.h"
#include "platform/platform_widget_helper.h"

#include <QtCore/QOperatingSystemVersion>

#include <winsock2.h>
#include <bcrypt.h>
#include <delayimp.h>
#include <process.h>
#include <userenv.h>
#include <winternl.h>

#include <span>

namespace Platform::WidgetHelper {
namespace {

constexpr auto kAppContainerName = L"telegram.desktop.widget-helper";
constexpr auto kLessPrivilegedName = L"WIN://NOALLAPPPKG";
constexpr auto kChangeNotifyPrivilege = LUID{ .LowPart = 23 };
constexpr auto kWarmUpRandomBytes = 16;
constexpr auto kProbePathCapacity = DWORD(32768);
constexpr auto kProbeConnectSeconds = 1;
constexpr auto kProbeWait = DWORD(5000);
constexpr auto kProbeRandomBytes = 32;
constexpr auto kJobLimits = DWORD(JOB_OBJECT_LIMIT_ACTIVE_PROCESS
	| JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
	| JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION);
constexpr auto kJobActiveProcesses = DWORD(1);
constexpr auto kJobUiRestrictions = DWORD(JOB_OBJECT_UILIMIT_HANDLES
	| JOB_OBJECT_UILIMIT_READCLIPBOARD
	| JOB_OBJECT_UILIMIT_WRITECLIPBOARD
	| JOB_OBJECT_UILIMIT_SYSTEMPARAMETERS
	| JOB_OBJECT_UILIMIT_DISPLAYSETTINGS
	| JOB_OBJECT_UILIMIT_GLOBALATOMS
	| JOB_OBJECT_UILIMIT_DESKTOP
	| JOB_OBJECT_UILIMIT_EXITWINDOWS);

using Check = QString(*)(HANDLE token);
using Step = QString(*)();
using Ran = std::shared_ptr<std::atomic<bool>>;
using DeriveSid = decltype(&::DeriveAppContainerSidFromAppContainerName);
using CreateLowBoxToken = NTSTATUS(NTAPI*)(
	PHANDLE token,
	HANDLE existing,
	ACCESS_MASK access,
	POBJECT_ATTRIBUTES attributes,
	PSID package,
	ULONG capabilityCount,
	PSID_AND_ATTRIBUTES capabilities,
	ULONG handleCount,
	HANDLE *handles);

struct Buffer {
	std::vector<std::byte> bytes;
	DWORD error = 0;
};

struct Token {
	UniqueHandle handle;
	QString error;
};

struct Package {
	std::vector<std::byte> sid;
	QString error;
};

struct SecurityAttribute {
	UNICODE_STRING name = {};
	USHORT valueType = 0;
	USHORT reserved = 0;
	ULONG flags = 0;
	ULONG valueCount = 0;
	const void *values = nullptr;
};

struct SecurityAttributes {
	USHORT version = 0;
	USHORT reserved = 0;
	ULONG count = 0;
	const SecurityAttribute *attributes = nullptr;
};

struct PolicyFunctions {
	decltype(&::GetProcessMitigationPolicy) get = nullptr;
	decltype(&::SetProcessMitigationPolicy) set = nullptr;
	decltype(&::IsUserCetAvailableInEnvironment) cetAvailable = nullptr;
};

struct JobState {
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
	JOBOBJECT_BASIC_UI_RESTRICTIONS restrictions = {};
	const char *failed = nullptr;
	DWORD error = 0;
	BOOL inJob = FALSE;
};

struct ProbeTarget {
	QString name;
	std::wstring path;
};

struct ProbePolicy {
	PROCESS_MITIGATION_POLICY policy = {};
	DWORD size = 0;
	const char *name = nullptr;
};

struct Probe {
	QStringList lines;
	std::vector<ProbeTarget> directories;
	std::wstring executable;
	QString zone;
	sockaddr_in address = {};
	SOCKET listener = INVALID_SOCKET;
	bool winsock = false;
};

constexpr auto kProbePolicies = std::array{
	ProbePolicy{
		ProcessDEPPolicy,
		sizeof(PROCESS_MITIGATION_DEP_POLICY),
		"DEP",
	},
	ProbePolicy{ ProcessASLRPolicy, sizeof(DWORD), "ASLR" },
	ProbePolicy{ ProcessDynamicCodePolicy, sizeof(DWORD), "dynamic code" },
	ProbePolicy{ ProcessStrictHandleCheckPolicy, sizeof(DWORD), "handles" },
	ProbePolicy{ ProcessSystemCallDisablePolicy, sizeof(DWORD), "win32k" },
	ProbePolicy{
		ProcessExtensionPointDisablePolicy,
		sizeof(DWORD),
		"extension points",
	},
	ProbePolicy{ ProcessSignaturePolicy, sizeof(DWORD), "signature" },
	ProbePolicy{ ProcessChildProcessPolicy, sizeof(DWORD), "child process" },
	ProbePolicy{ ProcessUserShadowStackPolicy, sizeof(DWORD), "shadow stack" },
};

[[nodiscard]] QString SystemError(const char *call, DWORD error) {
	return u"%1 failed: %2"_q.arg(
		QString::fromLatin1(call),
		qt_error_string(int(error)));
}

[[nodiscard]] QString NumberedError(const char *call, DWORD error) {
	return u"%1 failed: error %2"_q.arg(QString::fromLatin1(call)).arg(error);
}

template <typename Type>
[[nodiscard]] const Type *As(const Buffer &buffer) {
	return reinterpret_cast<const Type*>(buffer.bytes.data());
}

[[nodiscard]] std::span<const SID_AND_ATTRIBUTES> Groups(
		const Buffer &buffer) {
	const auto groups = As<TOKEN_GROUPS>(buffer);
	return { groups->Groups, groups->GroupCount };
}

template <typename Type>
[[nodiscard]] bool Within(
		const Buffer &buffer,
		const Type *pointer,
		std::size_t count) {
	const auto begin = reinterpret_cast<std::uintptr_t>(buffer.bytes.data());
	const auto at = reinterpret_cast<std::uintptr_t>(pointer);
	const auto size = buffer.bytes.size();
	return !count
		|| ((at >= begin)
			&& !(at % alignof(Type))
			&& (at - begin <= size)
			&& (count <= (size - (at - begin)) / sizeof(Type)));
}

[[nodiscard]] const Buffer *Failed(
		std::initializer_list<const Buffer*> buffers) {
	for (const auto buffer : buffers) {
		if (buffer->bytes.empty()) {
			return buffer;
		}
	}
	return nullptr;
}

[[nodiscard]] Buffer ReadToken(HANDLE token, TOKEN_INFORMATION_CLASS what) {
	auto size = DWORD();
	GetTokenInformation(token, what, nullptr, 0, &size);
	if (!size) {
		return { .error = GetLastError() };
	}
	auto result = Buffer{ .bytes = std::vector<std::byte>(size) };
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

[[nodiscard]] Buffer WellKnownSid(WELL_KNOWN_SID_TYPE type) {
	auto result = Buffer{
		.bytes = std::vector<std::byte>(SECURITY_MAX_SID_SIZE),
	};
	auto size = DWORD(result.bytes.size());
	if (!CreateWellKnownSid(type, nullptr, result.bytes.data(), &size)) {
		return { .error = GetLastError() };
	}
	return result;
}

[[nodiscard]] bool DenyOnly(DWORD attributes) {
	return (attributes & SE_GROUP_USE_FOR_DENY_ONLY)
		&& !(attributes & SE_GROUP_ENABLED);
}

[[nodiscard]] bool RestrictsInitial(const SID_AND_ATTRIBUTES &group) {
	return (group.Attributes & SE_GROUP_ENABLED)
		&& !(group.Attributes
			& (SE_GROUP_USE_FOR_DENY_ONLY | SE_GROUP_INTEGRITY));
}

[[nodiscard]] bool ClientRestricts(
		PSID sid,
		PSID user,
		std::span<const SID_AND_ATTRIBUTES> groups) {
	return EqualSid(sid, user)
		|| ranges::any_of(groups, [&](const SID_AND_ATTRIBUTES &group) {
			return RestrictsInitial(group) && EqualSid(sid, group.Sid);
		});
}

[[nodiscard]] QString CheckPrimary(HANDLE token) {
	const auto type = ReadToken(token, TokenType);
	if (type.bytes.empty()) {
		return NumberedError("GetTokenInformation", type.error);
	} else if (*As<TOKEN_TYPE>(type) != TokenPrimary) {
		return u"the token is not a primary token"_q;
	}
	return QString();
}

[[nodiscard]] QString CheckImpersonation(HANDLE token) {
	const auto type = ReadToken(token, TokenType);
	if (type.bytes.empty()) {
		return NumberedError("GetTokenInformation", type.error);
	} else if (*As<TOKEN_TYPE>(type) != TokenImpersonation) {
		return u"the token is not an impersonation token"_q;
	}
	const auto level = ReadToken(token, TokenImpersonationLevel);
	if (level.bytes.empty()) {
		return NumberedError("GetTokenInformation", level.error);
	}
	const auto value = *As<SECURITY_IMPERSONATION_LEVEL>(level);
	if (value != SecurityImpersonation) {
		return u"the token's impersonation level is %1"_q.arg(int(value));
	}
	return QString();
}

[[nodiscard]] DWORD Rid(PSID sid) {
	const auto count = *GetSidSubAuthorityCount(sid);
	return count ? *GetSidSubAuthority(sid, DWORD(count - 1)) : DWORD();
}

[[nodiscard]] QString CheckLow(HANDLE token) {
	const auto label = ReadToken(token, TokenIntegrityLevel);
	auto low = WellKnownSid(WinLowLabelSid);
	if (label.bytes.empty()) {
		return NumberedError("GetTokenInformation", label.error);
	} else if (low.bytes.empty()) {
		return NumberedError("CreateWellKnownSid", low.error);
	}
	const auto sid = As<TOKEN_MANDATORY_LABEL>(label)->Label.Sid;
	if (EqualSid(sid, low.bytes.data())) {
		return QString();
	}
	return u"the token's integrity 0x%1 is not Low"_q.arg(Rid(sid), 0, 16);
}

[[nodiscard]] QString CheckDenyOnly(HANDLE token) {
	const auto user = ReadToken(token, TokenUser);
	const auto groups = ReadToken(token, TokenGroups);
	if (const auto failed = Failed({ &user, &groups })) {
		return NumberedError("GetTokenInformation", failed->error);
	} else if (!DenyOnly(As<TOKEN_USER>(user)->User.Attributes)) {
		return u"the token's user is not deny-only"_q;
	}
	for (const auto &group : Groups(groups)) {
		if (!(group.Attributes & SE_GROUP_INTEGRITY)
			&& !DenyOnly(group.Attributes)) {
			return u"the token's group (0x%1) is not deny-only"_q.arg(
				group.Attributes,
				0,
				16);
		}
	}
	return QString();
}

[[nodiscard]] QString CheckNullRestricting(HANDLE token) {
	const auto restricting = ReadToken(token, TokenRestrictedSids);
	auto null = WellKnownSid(WinNullSid);
	if (restricting.bytes.empty()) {
		return NumberedError("GetTokenInformation", restricting.error);
	} else if (null.bytes.empty()) {
		return NumberedError("CreateWellKnownSid", null.error);
	}
	const auto sids = Groups(restricting);
	if (sids.size() != 1 || !EqualSid(sids.front().Sid, null.bytes.data())) {
		return u"the token's restricting SIDs are not exactly S-1-0-0"_q;
	}
	return QString();
}

[[nodiscard]] QString CheckClientRestricting(HANDLE token) {
	const auto client = GetCurrentProcessToken();
	const auto user = ReadToken(token, TokenUser);
	const auto restricting = ReadToken(token, TokenRestrictedSids);
	const auto clientUser = ReadToken(client, TokenUser);
	const auto clientGroups = ReadToken(client, TokenGroups);
	if (const auto failed = Failed({
			&user,
			&restricting,
			&clientUser,
			&clientGroups })) {
		return NumberedError("GetTokenInformation", failed->error);
	}
	const auto sid = As<TOKEN_USER>(clientUser)->User.Sid;
	const auto sids = Groups(restricting);
	const auto allowed = [&](const SID_AND_ATTRIBUTES &entry) {
		return ClientRestricts(entry.Sid, sid, Groups(clientGroups));
	};
	if (!EqualSid(As<TOKEN_USER>(user)->User.Sid, sid)) {
		return u"the token's user is not the client's user"_q;
	} else if (sids.empty()) {
		return u"the token has no restricting SIDs"_q;
	} else if (!ranges::all_of(sids, allowed)) {
		return u"the token restricts to a SID the client has not enabled"_q;
	}
	return QString();
}

[[nodiscard]] QString CheckPrivileges(HANDLE token) {
	const auto privileges = ReadToken(token, TokenPrivileges);
	if (privileges.bytes.empty()) {
		return NumberedError("GetTokenInformation", privileges.error);
	}
	const auto list = As<TOKEN_PRIVILEGES>(privileges);
	const auto all = std::span(list->Privileges, list->PrivilegeCount);
	for (const auto &privilege : all) {
		if (privilege.Luid.LowPart != kChangeNotifyPrivilege.LowPart
			|| privilege.Luid.HighPart != kChangeNotifyPrivilege.HighPart) {
			return u"the token keeps privilege %1"_q.arg(
				privilege.Luid.LowPart);
		}
	}
	return QString();
}

[[nodiscard]] QString CheckLessPrivileged(
		const Buffer &attributes,
		AppContainerLevel level) {
	const auto information = As<SecurityAttributes>(attributes);
	const auto records = information->attributes;
	const auto count = std::size_t(information->count);
	const auto malformed = u"the token's security attributes are malformed"_q;
	if (information->version
		!= CLAIM_SECURITY_ATTRIBUTES_INFORMATION_VERSION_V1) {
		return u"the token's security attributes version is %1"_q.arg(
			information->version);
	} else if (!Within(attributes, records, count)) {
		return malformed;
	}
	auto found = static_cast<const SecurityAttribute*>(nullptr);
	for (const auto &record : std::span(records, count)) {
		const auto &name = record.name;
		const auto length = name.Length / sizeof(WCHAR);
		if ((name.Length % sizeof(WCHAR))
			|| !Within(attributes, name.Buffer, length)) {
			return malformed;
		} else if (CompareStringOrdinal(
				name.Buffer,
				int(length),
				kLessPrivilegedName,
				-1,
				TRUE) != CSTR_EQUAL) {
			continue;
		} else if (found) {
			return malformed;
		}
		found = &record;
	}
	if (found) {
		const auto value = static_cast<const ULONG64*>(found->values);
		if (found->valueType != CLAIM_SECURITY_ATTRIBUTE_TYPE_UINT64) {
			return u"the token's WIN://NOALLAPPPKG type is %1"_q.arg(
				found->valueType);
		} else if (found->valueCount != 1) {
			return u"the token's WIN://NOALLAPPPKG has %1 values"_q.arg(
				found->valueCount);
		} else if (!Within(attributes, value, 1)) {
			return malformed;
		} else if (*value != 1) {
			return u"the token's WIN://NOALLAPPPKG value is %1"_q.arg(*value);
		}
	}
	const auto less = (found != nullptr);
	if (less == (level == AppContainerLevel::LessPrivileged)) {
		return QString();
	}
	return less
		? u"the token is a less privileged AppContainer token"_q
		: u"the token is not a less privileged AppContainer token"_q;
}

[[nodiscard]] QString FirstError(
		HANDLE token,
		std::initializer_list<Check> checks) {
	for (const auto check : checks) {
		auto error = check(token);
		if (!error.isEmpty()) {
			return error;
		}
	}
	return QString();
}

[[nodiscard]] Token CreateLowRestricted(
		HANDLE source,
		std::span<SID_AND_ATTRIBUTES> disable,
		std::span<SID_AND_ATTRIBUTES> restricting,
		PSID low) {
	auto token = HANDLE();
	if (!CreateRestrictedToken(
			source,
			DISABLE_MAX_PRIVILEGE,
			DWORD(disable.size()),
			disable.data(),
			0,
			nullptr,
			DWORD(restricting.size()),
			restricting.data(),
			&token)) {
		return {
			.error = SystemError("CreateRestrictedToken", GetLastError()),
		};
	}
	auto result = Token{ .handle = UniqueHandle(token) };
	auto label = TOKEN_MANDATORY_LABEL{
		.Label = { .Sid = low, .Attributes = SE_GROUP_INTEGRITY },
	};
	if (!SetTokenInformation(
			token,
			TokenIntegrityLevel,
			&label,
			DWORD(sizeof(label) + GetLengthSid(low)))) {
		return {
			.error = SystemError("SetTokenInformation", GetLastError()),
		};
	}
	return result;
}

template <typename Function>
void Resolve(HMODULE module, Function &function, const char *name) {
	function = reinterpret_cast<Function>(GetProcAddress(module, name));
}

[[nodiscard]] const PolicyFunctions &ResolvedPolicyFunctions() {
	static const auto result = [] {
		auto functions = PolicyFunctions();
		if (const auto kernel = GetModuleHandleW(L"kernel32.dll")) {
			Resolve(kernel, functions.get, "GetProcessMitigationPolicy");
			Resolve(kernel, functions.set, "SetProcessMitigationPolicy");
			Resolve(
				kernel,
				functions.cetAvailable,
				"IsUserCetAvailableInEnvironment");
		}
		return functions;
	}();
	return result;
}

[[nodiscard]] bool CetAvailable() {
	const auto available = ResolvedPolicyFunctions().cetAvailable;
	return ::Platform::IsWindows64Bit()
		&& available
		&& available(USER_CET_ENVIRONMENT_WIN32_PROCESS);
}

[[nodiscard]] CreateLowBoxToken ResolvedCreateLowBox() {
	static const auto result = [] {
		auto function = CreateLowBoxToken();
		if (const auto ntdll = GetModuleHandleW(L"ntdll.dll")) {
			Resolve(ntdll, function, "NtCreateLowBoxToken");
		}
		return function;
	}();
	return result;
}

[[nodiscard]] Package DerivePackage() {
	auto derive = DeriveSid();
	const auto userenv = base::Platform::SafeLoadLibrary(L"userenv.dll");
	if (!base::Platform::LoadMethod(
			userenv,
			"DeriveAppContainerSidFromAppContainerName",
			derive)) {
		return {
			.error = u"DeriveAppContainerSidFromAppContainerName "
				"is not available"_q,
		};
	}
	auto sid = PSID();
	const auto derived = derive(kAppContainerName, &sid);
	if (FAILED(derived)) {
		return {
			.error = u"DeriveAppContainerSidFromAppContainerName "
				"failed: 0x%1"_q.arg(uint(derived), 0, 16),
		};
	}
	const auto guard = gsl::finally([&] {
		FreeSid(sid);
	});
	const auto bytes = static_cast<const std::byte*>(sid);
	return {
		.sid = std::vector<std::byte>(bytes, bytes + GetLengthSid(sid)),
	};
}

[[nodiscard]] const Package &ExpectedPackage() {
	static const auto result = DerivePackage();
	return result;
}

[[nodiscard]] Token CreateLowBox(HANDLE base) {
	const auto create = ResolvedCreateLowBox();
	const auto &package = ExpectedPackage();
	if (!create) {
		return { .error = u"NtCreateLowBoxToken is not available"_q };
	} else if (!package.error.isEmpty()) {
		return { .error = package.error };
	}
	auto attributes = OBJECT_ATTRIBUTES{
		.Length = ULONG(sizeof(OBJECT_ATTRIBUTES)),
	};
	auto token = HANDLE();
	const auto status = create(
		&token,
		base,
		TOKEN_ALL_ACCESS,
		&attributes,
		const_cast<std::byte*>(package.sid.data()),
		0,
		nullptr,
		0,
		nullptr);
	if (!NT_SUCCESS(status)) {
		return {
			.error = u"NtCreateLowBoxToken failed: status 0x%1"_q.arg(
				uint(status),
				0,
				16),
		};
	}
	return { .handle = UniqueHandle(token) };
}

template <typename Policy>
[[nodiscard]] Mitigation Expectation(
		PROCESS_MITIGATION_POLICY policy,
		const Policy &required,
		const Policy &forbidden = Policy(),
		MitigationStage stage = MitigationStage::Creation) {
	return {
		.policy = policy,
		.size = DWORD(sizeof(Policy)),
		.required = required.Flags,
		.forbidden = forbidden.Flags,
		.stage = stage,
	};
}

[[nodiscard]] Policies ComputePolicies() {
	constexpr auto k64Bit = ::Platform::IsWindows64Bit()
		|| ::Platform::IsWindowsARM64();
	auto result = Policies{
		.childProcess = PROCESS_CREATION_CHILD_PROCESS_RESTRICTED,
		.allApplicationPackages
			= PROCESS_CREATION_ALL_APPLICATION_PACKAGES_OPT_OUT,
	};
	auto &words = result.creation;
	auto &expected = result.mitigations;

	if constexpr (::Platform::IsWindows32Bit()) {
		auto dep = PROCESS_MITIGATION_DEP_POLICY();
		dep.Enable = 1;
		dep.DisableAtlThunkEmulation = 1;
		words[0] |= PROCESS_CREATION_MITIGATION_POLICY_DEP_ENABLE;
		expected.push_back(Expectation(ProcessDEPPolicy, dep));
	}

	auto aslr = PROCESS_MITIGATION_ASLR_POLICY();
	aslr.EnableBottomUpRandomization = 1;
	aslr.EnableForceRelocateImages = 1;
	words[0] |= PROCESS_CREATION_MITIGATION_POLICY_BOTTOM_UP_ASLR_ALWAYS_ON
		| PROCESS_CREATION_MITIGATION_POLICY_FORCE_RELOCATE_IMAGES_ALWAYS_ON;
	if constexpr (k64Bit) {
		aslr.EnableHighEntropy = 1;
		words[0]
			|= PROCESS_CREATION_MITIGATION_POLICY_HIGH_ENTROPY_ASLR_ALWAYS_ON;
	}
	expected.push_back(Expectation(ProcessASLRPolicy, aslr));

	auto extensions = PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY();
	extensions.DisableExtensionPoints = 1;
	words[0]
		|= PROCESS_CREATION_MITIGATION_POLICY_EXTENSION_POINT_DISABLE_ALWAYS_ON;
	expected.push_back(
		Expectation(ProcessExtensionPointDisablePolicy, extensions));

	auto dynamic = PROCESS_MITIGATION_DYNAMIC_CODE_POLICY();
	dynamic.ProhibitDynamicCode = 1;
	auto dynamicOptOut = PROCESS_MITIGATION_DYNAMIC_CODE_POLICY();
	dynamicOptOut.AllowThreadOptOut = 1;
	dynamicOptOut.AllowRemoteDowngrade = 1;
	words[0]
		|= PROCESS_CREATION_MITIGATION_POLICY_PROHIBIT_DYNAMIC_CODE_ALWAYS_ON;
	expected.push_back(
		Expectation(ProcessDynamicCodePolicy, dynamic, dynamicOptOut));

	auto child = PROCESS_MITIGATION_CHILD_PROCESS_POLICY();
	child.NoChildProcessCreation = 1;
	auto childOptOut = PROCESS_MITIGATION_CHILD_PROCESS_POLICY();
	childOptOut.AllowSecureProcessCreation = 1;
	expected.push_back(
		Expectation(ProcessChildProcessPolicy, child, childOptOut));

	if (CetAvailable()) {
		auto stack = PROCESS_MITIGATION_USER_SHADOW_STACK_POLICY();
		stack.EnableUserShadowStack = 1;
		words[1]
			|= PROCESS_CREATION_MITIGATION_POLICY2_CET_USER_SHADOW_STACKS_ALWAYS_ON;
		expected.push_back(Expectation(ProcessUserShadowStackPolicy, stack));
	}
	result.creationSize = words[1] ? sizeof(words) : sizeof(words[0]);

	auto signature = PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY();
	signature.MicrosoftSignedOnly = 1;
	expected.push_back(Expectation(
		ProcessSignaturePolicy,
		signature,
		{},
		MitigationStage::Startup));

	auto handles = PROCESS_MITIGATION_STRICT_HANDLE_CHECK_POLICY();
	handles.RaiseExceptionOnInvalidHandleReference = 1;
	handles.HandleExceptionsPermanentlyEnabled = 1;
	expected.push_back(Expectation(
		ProcessStrictHandleCheckPolicy,
		handles,
		{},
		MitigationStage::Startup));

	return result;
}

[[nodiscard]] QString PolicyError(
		const char *call,
		PROCESS_MITIGATION_POLICY policy,
		DWORD error) {
	return u"%1(%2) failed: error %3"_q.arg(
		QString::fromLatin1(call),
		QString::number(int(policy)),
		QString::number(error));
}

[[nodiscard]] std::optional<DWORD> ReadMitigation(
		PROCESS_MITIGATION_POLICY policy,
		DWORD size) {
	const auto get = ResolvedPolicyFunctions().get;
	auto value = std::array<DWORD, 2>();
	Expects(size <= sizeof(value));
	if (!get || !get(GetCurrentProcess(), policy, value.data(), size)) {
		return std::nullopt;
	}
	return value.front();
}

[[nodiscard]] JobState ReadJob() {
	auto result = JobState();
	if (!IsProcessInJob(GetCurrentProcess(), nullptr, &result.inJob)) {
		result.failed = "IsProcessInJob";
		result.error = GetLastError();
		return result;
	} else if (!result.inJob) {
		return result;
	}
	const auto limits = QueryInformationJobObject(
		nullptr,
		JobObjectExtendedLimitInformation,
		&result.limits,
		DWORD(sizeof(result.limits)),
		nullptr);
	if (!limits
		|| !QueryInformationJobObject(
			nullptr,
			JobObjectBasicUIRestrictions,
			&result.restrictions,
			DWORD(sizeof(result.restrictions)),
			nullptr)) {
		result.failed = "QueryInformationJobObject";
		result.error = GetLastError();
	}
	return result;
}

[[nodiscard]] QString FirstFailure(std::initializer_list<Step> steps) {
	for (const auto step : steps) {
		auto error = step();
		if (!error.isEmpty()) {
			return error;
		}
	}
	return QString();
}

[[nodiscard]] QString WarmUp() {
	const auto &functions = ResolvedPolicyFunctions();
	if (!functions.get || !functions.set) {
		return u"the mitigation policy functions are not available"_q;
	}
	// No thread can load a DLL after the lowering, so bind and warm them now.
	const auto bound = __HrLoadAllImportsForDll("ADVAPI32.dll");
	if (FAILED(bound)) {
		return u"__HrLoadAllImportsForDll(ADVAPI32.dll) failed: 0x%1"_q.arg(
			uint(bound),
			0,
			16);
	}
	const auto &package = ExpectedPackage();
	if (!package.error.isEmpty()) {
		return package.error;
	}
	auto bytes = std::array<UCHAR, kWarmUpRandomBytes>();
	const auto status = BCryptGenRandom(
		nullptr,
		bytes.data(),
		ULONG(bytes.size()),
		BCRYPT_USE_SYSTEM_PREFERRED_RNG);
	return BCRYPT_SUCCESS(status)
		? QString()
		: u"BCryptGenRandom failed: status 0x%1"_q.arg(uint(status), 0, 16);
}

[[nodiscard]] QString RevertImpersonation() {
	return RevertToSelf()
		? QString()
		: NumberedError("RevertToSelf", GetLastError());
}

[[nodiscard]] QString ForgetRegistryRoots() {
	const auto disabled = RegDisablePredefinedCacheEx();
	if (disabled != ERROR_SUCCESS) {
		return NumberedError("RegDisablePredefinedCacheEx", DWORD(disabled));
	}
	const auto roots = std::array{
		HKEY_CLASSES_ROOT,
		HKEY_CURRENT_USER,
		HKEY_LOCAL_MACHINE,
		HKEY_USERS,
	};
	for (const auto root : roots) {
		const auto closed = RegCloseKey(root);
		if (closed != ERROR_SUCCESS && closed != ERROR_INVALID_HANDLE) {
			return NumberedError("RegCloseKey", DWORD(closed));
		}
	}
	return QString();
}

[[nodiscard]] QString ApplyStartupPolicies() {
	const auto set = ResolvedPolicyFunctions().set;
	for (const auto &mitigation : SandboxPolicies().mitigations) {
		if (mitigation.stage != MitigationStage::Startup) {
			continue;
		}
		auto value = std::array<DWORD, 2>{ mitigation.required };
		Expects(mitigation.size <= sizeof(value));
		if (!set(mitigation.policy, value.data(), mitigation.size)) {
			return PolicyError(
				"SetProcessMitigationPolicy",
				mitigation.policy,
				GetLastError());
		}
	}
	return QString();
}

[[nodiscard]] QString VerifyNoThreadToken() {
	auto token = HANDLE();
	if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token)) {
		const auto guard = UniqueHandle(token);
		return u"the thread still has a token"_q;
	}
	const auto error = GetLastError();
	return (error == ERROR_NO_TOKEN)
		? QString()
		: NumberedError("OpenThreadToken", error);
}

[[nodiscard]] QString VerifyJob() {
	const auto job = ReadJob();
	const auto &basic = job.limits.BasicLimitInformation;
	const auto ui = job.restrictions.UIRestrictionsClass;
	if (job.failed) {
		return NumberedError(job.failed, job.error);
	} else if (!job.inJob) {
		return u"the process is not in a job"_q;
	} else if ((basic.LimitFlags & kJobLimits) != kJobLimits
		|| basic.ActiveProcessLimit != kJobActiveProcesses) {
		return u"the job's limits are 0x%1 for %2 processes"_q.arg(
			QString::number(basic.LimitFlags, 16),
			QString::number(basic.ActiveProcessLimit));
	} else if ((ui & kJobUiRestrictions) != kJobUiRestrictions) {
		return u"the job's UI restrictions are 0x%1"_q.arg(ui, 0, 16);
	}
	return QString();
}

[[nodiscard]] QString VerifyMitigations() {
	for (const auto &mitigation : SandboxPolicies().mitigations) {
		const auto flags = ReadMitigation(mitigation.policy, mitigation.size);
		if (!flags) {
			return PolicyError(
				"GetProcessMitigationPolicy",
				mitigation.policy,
				GetLastError());
		} else if ((*flags & mitigation.required) != mitigation.required
			|| (*flags & mitigation.forbidden)) {
			return u"the mitigation policy %1 reads 0x%2"_q.arg(
				QString::number(int(mitigation.policy)),
				QString::number(*flags, 16));
		}
	}
	return QString();
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

[[nodiscard]] QString TimeZone() {
	auto zone = TIME_ZONE_INFORMATION();
	const auto id = GetTimeZoneInformation(&zone);
	if (id == TIME_ZONE_ID_INVALID) {
		return u"GetTimeZoneInformation failed: error %1"_q.arg(
			GetLastError());
	}
	const auto daylight = (id == TIME_ZONE_ID_DAYLIGHT);
	return u"bias %1"_q.arg(zone.Bias + (daylight ? zone.DaylightBias : 0));
}

[[nodiscard]] QString Outcome(DWORD error) {
	return error ? u"error %1"_q.arg(error) : u"ok"_q;
}

[[nodiscard]] QString Denial(DWORD error, DWORD denied) {
	if (!error) {
		return u"ALLOWED"_q;
	}
	return u"%1 (error %2)"_q.arg(
		(error == denied) ? u"denied"_q : u"failed"_q,
		QString::number(error));
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

void PrepareProbeNetwork(Probe &probe) {
	const auto bound = __HrLoadAllImportsForDll("WS2_32.dll");
	if (FAILED(bound)) {
		probe.lines.push_back(u"before lowering: "
			"__HrLoadAllImportsForDll(WS2_32.dll) failed: 0x%1"_q.arg(
				uint(bound),
				0,
				16));
		return;
	}
	auto data = WSADATA();
	const auto started = WSAStartup(MAKEWORD(2, 2), &data);
	if (started) {
		probe.lines.push_back(
			u"before lowering: WSAStartup failed: error %1"_q.arg(started));
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
		probe.lines.push_back(u"before lowering: the loopback listener "
			"failed: error %1"_q.arg(WSAGetLastError()));
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
	probe.lines.push_back(u"before lowering: socket, connect to "
		"127.0.0.1:%1 and accept: %2"_q.arg(
			QString::number(ntohs(probe.address.sin_port)),
			Outcome(DWORD(error))));
}

void PrepareProbeTargets(Probe &probe, const QString &clientDirectory) {
	const auto appData = ReadPath([](wchar_t *buffer, DWORD size) {
		return GetEnvironmentVariableW(L"APPDATA", buffer, size);
	});
	const auto current = ReadPath([](wchar_t *buffer, DWORD size) {
		return GetCurrentDirectoryW(size, buffer);
	});
	const auto add = [&](const QString &name, const std::wstring &path) {
		probe.directories.push_back({ name, path });
		probe.lines.push_back(u"before lowering: %1 %2: "
			"FILE_LIST_DIRECTORY %3"_q.arg(
				name,
				QString::fromStdWString(path),
				Outcome(OpenDirectory(path))));
	};
	add(u"helper working directory"_q, current);
	if (clientDirectory.isEmpty()) {
		probe.lines.push_back(
			u"before lowering: client working directory: not given"_q);
	} else {
		add(u"client working directory"_q, clientDirectory.toStdWString());
	}
	add(u"APPDATA"_q, appData);
	probe.lines.push_back(u"before lowering: HKEY_CURRENT_USER\\Software: "
		"RegOpenKeyExW "_q + Outcome(OpenKey(HKEY_CURRENT_USER, L"Software")));
	const auto system = ReadPath([](wchar_t *buffer, DWORD size) {
		return GetSystemDirectoryW(buffer, size);
	});
	probe.executable = system + L"\\whoami.exe";
	const auto present = GetFileAttributesW(probe.executable.c_str())
		!= INVALID_FILE_ATTRIBUTES;
	probe.lines.push_back(u"before lowering: %1: %2"_q.arg(
		QString::fromStdWString(probe.executable),
		present ? u"present"_q : Outcome(GetLastError())));
	probe.zone = TimeZone();
	probe.lines.push_back(u"before lowering: time zone "_q + probe.zone);
}

[[nodiscard]] QString ProbeProcess(const std::wstring &executable) {
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
	const auto name = u"CreateProcessW %1: "_q.arg(
		QString::fromStdWString(executable));
	if (!created) {
		return name + Denial(error, ERROR_ACCESS_DENIED);
	}
	const auto process = UniqueHandle(information.hProcess);
	const auto thread = UniqueHandle(information.hThread);
	const auto terminated = TerminateProcess(process.get(), 1);
	const auto waited = WaitForSingleObject(process.get(), kProbeWait);
	return name + u"ALLOWED (pid %1, terminated %2, wait %3)"_q.arg(
		QString::number(information.dwProcessId),
		QString::number(terminated),
		QString::number(waited));
}

[[nodiscard]] QString ProbeSocket(const Probe &probe) {
	if (probe.listener == INVALID_SOCKET) {
		return u"socket: not tried, the loopback listener did not start"_q;
	}
	const auto created = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	const auto error = (created == INVALID_SOCKET) ? WSAGetLastError() : 0;
	const auto name = u"socket(AF_INET, SOCK_STREAM): "_q;
	if (created == INVALID_SOCKET) {
		return name + Denial(DWORD(error), WSAEACCES);
	}
	const auto connected = ConnectWithin(created, probe.address);
	::closesocket(created);
	return name + u"ALLOWED, connect to 127.0.0.1:%1: %2"_q.arg(
		QString::number(ntohs(probe.address.sin_port)),
		(connected ? Denial(DWORD(connected), WSAEACCES) : u"CONNECTED"_q));
}

[[nodiscard]] QString ProbeToken() {
	const auto token = GetCurrentProcessToken();
	const auto label = ReadToken(token, TokenIntegrityLevel);
	const auto privileges = ReadToken(token, TokenPrivileges);
	if (const auto failed = Failed({ &label, &privileges })) {
		return u"process token: "_q
			+ NumberedError("GetTokenInformation", failed->error);
	}
	const auto sid = As<TOKEN_MANDATORY_LABEL>(label)->Label.Sid;
	const auto count = As<TOKEN_PRIVILEGES>(privileges)->PrivilegeCount;
	const auto check = CheckLockdownToken(token);
	return u"process token: integrity 0x%1, restricted %2, privileges %3, "
		"lockdown check %4"_q.arg(
			QString::number(Rid(sid), 16),
			QString::number(IsTokenRestricted(token)),
			QString::number(count),
			check.isEmpty() ? u"passed"_q : check);
}

[[nodiscard]] QString ProbeJob() {
	const auto job = ReadJob();
	const auto check = VerifyJob();
	return u"job: in a job %1, limits 0x%2, active processes %3, UI 0x%4, "
		"check %5"_q.arg(
			QString::number(job.inJob),
			QString::number(job.limits.BasicLimitInformation.LimitFlags, 16),
			QString::number(
				job.limits.BasicLimitInformation.ActiveProcessLimit),
			QString::number(job.restrictions.UIRestrictionsClass, 16),
			check.isEmpty() ? u"passed"_q : check);
}

[[nodiscard]] QString ProbeMitigation(const ProbePolicy &entry) {
	const auto flags = ReadMitigation(entry.policy, entry.size);
	const auto error = flags ? DWORD() : GetLastError();
	return u"mitigation %1: %2"_q.arg(
		QString::fromLatin1(entry.name),
		flags
			? (u"0x"_q + QString::number(*flags, 16))
			: u"error %1"_q.arg(error));
}

[[nodiscard]] QString ProbeWin32k() {
	auto policy = PROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY();
	policy.DisallowWin32kSystemCalls = 1;
	const auto set = ResolvedPolicyFunctions().set;
	if (set(ProcessSystemCallDisablePolicy, &policy, sizeof(policy))) {
		return u"DisallowWin32kSystemCalls: APPLIED"_q;
	}
	const auto error = GetLastError();
	return u"DisallowWin32kSystemCalls: not applied (%1)"_q.arg(PolicyError(
		"SetProcessMitigationPolicy",
		ProcessSystemCallDisablePolicy,
		error));
}

[[nodiscard]] QString ProbeThreadCreation() {
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
		return u"_beginthreadex failed: errno %1"_q.arg(errno);
	}
	argument.release();
	const auto waited = WaitForSingleObject(thread.get(), kProbeWait);
	if (waited != WAIT_OBJECT_0) {
		return u"_beginthreadex: the thread did not end in %1 ms (wait %2)"_q
			.arg(kProbeWait)
			.arg(waited);
	}
	return *ran
		? u"_beginthreadex: the thread ran and joined"_q
		: u"_beginthreadex: the thread ended without running"_q;
}

[[nodiscard]] QString ProbeEvent() {
	const auto event = UniqueHandle(
		CreateEventW(nullptr, TRUE, FALSE, nullptr));
	if (!event) {
		return u"event: CreateEventW failed: error %1"_q.arg(GetLastError());
	} else if (!SetEvent(event.get())) {
		return u"event: SetEvent failed: error %1"_q.arg(GetLastError());
	}
	const auto waited = WaitForSingleObject(event.get(), 0);
	const auto error = GetLastError();
	return (waited == WAIT_OBJECT_0)
		? u"event: created, signalled and waited"_q
		: u"event: wait %1, error %2"_q.arg(waited).arg(error);
}

[[nodiscard]] QString ProbeOwnProcess() {
	const auto pid = GetCurrentProcessId();
	const auto own = UniqueHandle(OpenProcess(
		PROCESS_QUERY_LIMITED_INFORMATION,
		FALSE,
		pid));
	const auto error = own ? DWORD() : GetLastError();
	return u"OpenProcess(own pid %1): %2"_q.arg(
		QString::number(pid),
		own ? u"opened"_q : u"error %1"_q.arg(error));
}

[[nodiscard]] QString ProbeRandom() {
	auto bytes = std::array<UCHAR, kProbeRandomBytes>();
	const auto status = BCryptGenRandom(
		nullptr,
		bytes.data(),
		ULONG(bytes.size()),
		BCRYPT_USE_SYSTEM_PREFERRED_RNG);
	const auto nonzero = ranges::any_of(bytes, [](UCHAR value) {
		return value != 0;
	});
	SecureZeroMemory(bytes.data(), bytes.size());
	return u"BCryptGenRandom(%1 bytes): status 0x%2, %3"_q.arg(
		QString::number(kProbeRandomBytes),
		QString::number(uint(status), 16),
		nonzero ? u"nonzero output"_q : u"ALL ZERO"_q);
}

void CloseProbeNetwork(Probe &probe) {
	if (probe.listener != INVALID_SOCKET) {
		::closesocket(probe.listener);
		probe.listener = INVALID_SOCKET;
	}
	if (base::take(probe.winsock)) {
		WSACleanup();
	}
}

[[nodiscard]] QString FinishProbe(Probe &probe) {
	auto lines = probe.lines;
	const auto add = [&](const QString &line) {
		lines.push_back(u"after lowering: "_q + line);
	};
	add(ProbeProcess(probe.executable));
	add(ProbeSocket(probe));
	for (const auto &directory : probe.directories) {
		add(u"%1 %2: FILE_LIST_DIRECTORY %3"_q.arg(
			directory.name,
			QString::fromStdWString(directory.path),
			Denial(OpenDirectory(directory.path), ERROR_ACCESS_DENIED)));
	}
	add(u"HKEY_CURRENT_USER\\Software (opened before lowering): "
		"RegOpenKeyExW "_q + Denial(
			OpenKey(HKEY_CURRENT_USER, L"Software"),
			ERROR_ACCESS_DENIED));
	add(u"HKEY_LOCAL_MACHINE\\SOFTWARE (not opened before lowering): "
		"RegOpenKeyExW "_q + Denial(
			OpenKey(HKEY_LOCAL_MACHINE, L"SOFTWARE"),
			ERROR_ACCESS_DENIED));
	const auto thread = VerifyNoThreadToken();
	add(u"thread token: "_q
		+ (thread.isEmpty() ? u"none (ERROR_NO_TOKEN)"_q : thread));
	add(ProbeToken());
	add(ProbeJob());
	add(ProbeWin32k());
	for (const auto &entry : kProbePolicies) {
		add(ProbeMitigation(entry));
	}
	add(ProbeThreadCreation());
	add(ProbeEvent());
	add(ProbeOwnProcess());
	add(ProbeRandom());
	add(u"time zone %1 (before lowering %2)"_q.arg(TimeZone(), probe.zone));
	CloseProbeNetwork(probe);
	return lines.join('\n');
}

} // namespace

QString UnsupportedReason() {
	if (::Platform::IsWindowsStoreBuild()) {
		return u"the widget helper is not available in the Store build"_q;
	}
	const auto minimum = QOperatingSystemVersion(
		QOperatingSystemVersion::Windows,
		10,
		0,
		17763);
	return (QOperatingSystemVersion::current() >= minimum)
		? QString()
		: u"the widget helper needs Windows 10 1809 or later"_q;
}

Tokens CreateTokens() {
	auto source = HANDLE();
	if (!OpenProcessToken(
			GetCurrentProcess(),
			(TOKEN_DUPLICATE
				| TOKEN_QUERY
				| TOKEN_ASSIGN_PRIMARY
				| TOKEN_ADJUST_DEFAULT),
			&source)) {
		return { .error = SystemError("OpenProcessToken", GetLastError()) };
	}
	const auto guard = UniqueHandle(source);
	const auto user = ReadToken(source, TokenUser);
	const auto groups = ReadToken(source, TokenGroups);
	auto low = WellKnownSid(WinLowLabelSid);
	auto null = WellKnownSid(WinNullSid);
	if (const auto failed = Failed({ &user, &groups })) {
		return { .error = SystemError("GetTokenInformation", failed->error) };
	}
	if (const auto failed = Failed({ &low, &null })) {
		return { .error = SystemError("CreateWellKnownSid", failed->error) };
	}
	const auto userSid = As<TOKEN_USER>(user)->User.Sid;
	auto disable = std::vector<SID_AND_ATTRIBUTES>{ { .Sid = userSid } };
	auto restricting = std::vector<SID_AND_ATTRIBUTES>{ { .Sid = userSid } };
	for (const auto &group : Groups(groups)) {
		if (!(group.Attributes & SE_GROUP_INTEGRITY)) {
			disable.push_back({ .Sid = group.Sid });
		}
		if (RestrictsInitial(group)) {
			restricting.push_back({ .Sid = group.Sid });
		}
	}
	auto nullOnly = std::array{
		SID_AND_ATTRIBUTES{ .Sid = null.bytes.data() },
	};
	auto lockdown = CreateLowRestricted(
		source,
		disable,
		nullOnly,
		low.bytes.data());
	if (!lockdown.error.isEmpty()) {
		return { .error = std::move(lockdown.error) };
	}
	auto restricted = CreateLowRestricted(
		source,
		{},
		restricting,
		low.bytes.data());
	if (!restricted.error.isEmpty()) {
		return { .error = std::move(restricted.error) };
	}
	auto boxedLockdown = CreateLowBox(lockdown.handle.get());
	if (!boxedLockdown.error.isEmpty()) {
		return { .error = std::move(boxedLockdown.error) };
	}
	auto boxedRestricted = CreateLowBox(restricted.handle.get());
	if (!boxedRestricted.error.isEmpty()) {
		return { .error = std::move(boxedRestricted.error) };
	}
	auto initial = HANDLE();
	if (!DuplicateTokenEx(
			boxedRestricted.handle.get(),
			TOKEN_IMPERSONATE | TOKEN_QUERY,
			nullptr,
			SecurityImpersonation,
			TokenImpersonation,
			&initial)) {
		return { .error = SystemError("DuplicateTokenEx", GetLastError()) };
	}
	auto result = Tokens{
		.lockdown = std::move(boxedLockdown.handle),
		.initial = UniqueHandle(initial),
	};
	auto error = CheckLockdownToken(result.lockdown.get());
	if (!error.isEmpty()) {
		return { .error = u"the lockdown token check failed: "_q + error };
	}
	error = CheckInitialToken(result.initial.get());
	if (!error.isEmpty()) {
		return { .error = u"the initial token check failed: "_q + error };
	}
	// Only the process creation attribute makes the primary less privileged.
	error = CheckAppContainerToken(
		result.lockdown.get(),
		AppContainerLevel::Regular);
	if (!error.isEmpty()) {
		return { .error = u"the lockdown token check failed: "_q + error };
	}
	error = CheckAppContainerToken(
		result.initial.get(),
		AppContainerLevel::Regular);
	if (!error.isEmpty()) {
		return { .error = u"the initial token check failed: "_q + error };
	}
	return result;
}

QString CheckLockdownToken(HANDLE token) {
	return FirstError(token, {
		CheckPrimary,
		CheckLow,
		CheckDenyOnly,
		CheckNullRestricting,
		CheckPrivileges,
	});
}

QString CheckInitialToken(HANDLE token) {
	return FirstError(token, {
		CheckImpersonation,
		CheckLow,
		CheckClientRestricting,
		CheckPrivileges,
	});
}

QString CheckAppContainerToken(HANDLE token, AppContainerLevel level) {
	const auto &package = ExpectedPackage();
	if (!package.error.isEmpty()) {
		return package.error;
	}
	const auto container = ReadToken(token, TokenIsAppContainer);
	const auto sid = ReadToken(token, TokenAppContainerSid);
	const auto capabilities = ReadToken(token, TokenCapabilities);
	// TokenIsLessPrivilegedAppContainer may be unsupported (error 87).
	const auto attributes = ReadToken(token, TokenSecurityAttributes);
	if (const auto failed = Failed({
			&container,
			&sid,
			&capabilities,
			&attributes })) {
		return NumberedError("GetTokenInformation", failed->error);
	} else if (container.bytes.size() < sizeof(DWORD)
		|| sid.bytes.size() < sizeof(TOKEN_APPCONTAINER_INFORMATION)
		|| capabilities.bytes.size() < sizeof(DWORD)
		|| attributes.bytes.size() < sizeof(SecurityAttributes)) {
		return u"the token's AppContainer information is truncated"_q;
	} else if (!*As<DWORD>(container)) {
		return u"the token is not an AppContainer token"_q;
	}
	const auto information = As<TOKEN_APPCONTAINER_INFORMATION>(sid);
	const auto actual = information->TokenAppContainer;
	const auto expected = const_cast<std::byte*>(package.sid.data());
	const auto count = Groups(capabilities).size();
	if (!actual || !EqualSid(actual, expected)) {
		return u"the token's AppContainer SID is not the widget helper's"_q;
	} else if (count) {
		return u"the token has %1 capabilities"_q.arg(count);
	}
	return CheckLessPrivileged(attributes, level);
}

Job CreateJob() {
	auto inJob = BOOL();
	if (!IsProcessInJob(GetCurrentProcess(), nullptr, &inJob)) {
		return { .error = SystemError("IsProcessInJob", GetLastError()) };
	}
	auto creationFlags = DWORD();
	if (inJob) {
		auto own = JOBOBJECT_EXTENDED_LIMIT_INFORMATION();
		if (!QueryInformationJobObject(
				nullptr,
				JobObjectExtendedLimitInformation,
				&own,
				DWORD(sizeof(own)),
				nullptr)) {
			return {
				.error = SystemError(
					"QueryInformationJobObject",
					GetLastError()),
			};
		}
		const auto limits = own.BasicLimitInformation.LimitFlags;
		if ((limits & JOB_OBJECT_LIMIT_BREAKAWAY_OK)
			&& !(limits & JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK)) {
			creationFlags = CREATE_BREAKAWAY_FROM_JOB;
		}
	}
	auto result = Job{
		.handle = UniqueHandle(CreateJobObjectW(nullptr, nullptr)),
		.creationFlags = creationFlags,
	};
	if (!result.handle) {
		return { .error = SystemError("CreateJobObjectW", GetLastError()) };
	}
	auto limits = JOBOBJECT_EXTENDED_LIMIT_INFORMATION();
	limits.BasicLimitInformation.LimitFlags = kJobLimits;
	limits.BasicLimitInformation.ActiveProcessLimit = kJobActiveProcesses;
	auto restrictions = JOBOBJECT_BASIC_UI_RESTRICTIONS{
		.UIRestrictionsClass = kJobUiRestrictions,
	};
	if (!SetInformationJobObject(
			result.handle.get(),
			JobObjectExtendedLimitInformation,
			&limits,
			DWORD(sizeof(limits)))
		|| !SetInformationJobObject(
			result.handle.get(),
			JobObjectBasicUIRestrictions,
			&restrictions,
			DWORD(sizeof(restrictions)))) {
		return {
			.error = SystemError("SetInformationJobObject", GetLastError()),
		};
	}
	return result;
}

const Policies &SandboxPolicies() {
	static const auto result = ComputePolicies();
	return result;
}

QString LockDownHelper() {
	const auto error = FirstFailure({
		WarmUp,
		RevertImpersonation,
		ForgetRegistryRoots,
		ApplyStartupPolicies,
		VerifyNoThreadToken,
		[] { return CheckLockdownToken(GetCurrentProcessToken()); },
		[] {
			return CheckAppContainerToken(
				GetCurrentProcessToken(),
				AppContainerLevel::LessPrivileged);
		},
		VerifyJob,
		VerifyMitigations,
	});
	return error.isEmpty()
		? QString()
		: (u"the widget helper is not sandboxed: "_q + error);
}

Fn<QString()> PrepareHelperProbe(const QString &clientDirectory) {
	const auto probe = std::make_shared<Probe>();
	PrepareProbeNetwork(*probe);
	PrepareProbeTargets(*probe, clientDirectory);
	return [=] {
		return FinishProbe(*probe);
	};
}

} // namespace Platform::WidgetHelper
