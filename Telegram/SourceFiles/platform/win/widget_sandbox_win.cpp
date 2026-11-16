/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/widget_sandbox_win.h"

#include <gsl/gsl>

#include <bcrypt.h>
#include <delayimp.h>
#include <userenv.h>
#include <winternl.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Platform::WidgetHelper {
namespace {

#ifdef _M_IX86
constexpr auto kX86 = true;
#else // _M_IX86
constexpr auto kX86 = false;
#endif // _M_IX86

#ifdef _M_X64
constexpr auto kX64 = true;
#else // _M_X64
constexpr auto kX64 = false;
#endif // _M_X64

#ifdef _M_ARM64
constexpr auto kARM64 = true;
#else // _M_ARM64
constexpr auto kARM64 = false;
#endif // _M_ARM64

constexpr auto k64Bit = kX64 || kARM64;
constexpr auto kAppContainerName = L"telegram.desktop.widget-helper";
constexpr auto kLessPrivilegedName = L"WIN://NOALLAPPPKG";
constexpr auto kChangeNotifyPrivilege = LUID{ .LowPart = 23 };
constexpr auto kWarmUpRandomBytes = 16;
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

using Check = std::string(*)(HANDLE token);
using Step = std::string(*)();
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
	std::string error;
};

struct Package {
	std::vector<std::byte> sid;
	std::string error;
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
	decltype(&::IsUserCetAvailableInEnvironment) cetAvailable = nullptr;
};

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
		|| std::ranges::any_of(groups, [&](const SID_AND_ATTRIBUTES &group) {
			return RestrictsInitial(group) && EqualSid(sid, group.Sid);
		});
}

[[nodiscard]] std::string CheckPrimary(HANDLE token) {
	const auto type = ReadToken(token, TokenType);
	if (type.bytes.empty()) {
		return NumberedError("GetTokenInformation", type.error);
	} else if (*As<TOKEN_TYPE>(type) != TokenPrimary) {
		return "the token is not a primary token";
	}
	return std::string();
}

[[nodiscard]] std::string CheckImpersonation(HANDLE token) {
	const auto type = ReadToken(token, TokenType);
	if (type.bytes.empty()) {
		return NumberedError("GetTokenInformation", type.error);
	} else if (*As<TOKEN_TYPE>(type) != TokenImpersonation) {
		return "the token is not an impersonation token";
	}
	const auto level = ReadToken(token, TokenImpersonationLevel);
	if (level.bytes.empty()) {
		return NumberedError("GetTokenInformation", level.error);
	}
	const auto value = *As<SECURITY_IMPERSONATION_LEVEL>(level);
	if (value != SecurityImpersonation) {
		return "the token's impersonation level is "
			+ std::to_string(int(value));
	}
	return std::string();
}

[[nodiscard]] DWORD Rid(PSID sid) {
	const auto count = *GetSidSubAuthorityCount(sid);
	return count ? *GetSidSubAuthority(sid, DWORD(count - 1)) : DWORD();
}

[[nodiscard]] std::string CheckLow(HANDLE token) {
	const auto label = ReadToken(token, TokenIntegrityLevel);
	auto low = WellKnownSid(WinLowLabelSid);
	if (label.bytes.empty()) {
		return NumberedError("GetTokenInformation", label.error);
	} else if (low.bytes.empty()) {
		return NumberedError("CreateWellKnownSid", low.error);
	}
	const auto sid = As<TOKEN_MANDATORY_LABEL>(label)->Label.Sid;
	if (EqualSid(sid, low.bytes.data())) {
		return std::string();
	}
	return "the token's integrity 0x" + Hex(Rid(sid)) + " is not Low";
}

[[nodiscard]] std::string CheckDenyOnly(HANDLE token) {
	const auto user = ReadToken(token, TokenUser);
	const auto groups = ReadToken(token, TokenGroups);
	if (const auto failed = Failed({ &user, &groups })) {
		return NumberedError("GetTokenInformation", failed->error);
	} else if (!DenyOnly(As<TOKEN_USER>(user)->User.Attributes)) {
		return "the token's user is not deny-only";
	}
	for (const auto &group : Groups(groups)) {
		if (!(group.Attributes & SE_GROUP_INTEGRITY)
			&& !DenyOnly(group.Attributes)) {
			return "the token's group (0x"
				+ Hex(group.Attributes)
				+ ") is not deny-only";
		}
	}
	return std::string();
}

[[nodiscard]] std::string CheckNullRestricting(HANDLE token) {
	const auto restricting = ReadToken(token, TokenRestrictedSids);
	auto null = WellKnownSid(WinNullSid);
	if (restricting.bytes.empty()) {
		return NumberedError("GetTokenInformation", restricting.error);
	} else if (null.bytes.empty()) {
		return NumberedError("CreateWellKnownSid", null.error);
	}
	const auto sids = Groups(restricting);
	if (sids.size() != 1 || !EqualSid(sids.front().Sid, null.bytes.data())) {
		return "the token's restricting SIDs are not exactly S-1-0-0";
	}
	return std::string();
}

[[nodiscard]] std::string CheckClientRestricting(HANDLE token) {
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
		return "the token's user is not the client's user";
	} else if (sids.empty()) {
		return "the token has no restricting SIDs";
	} else if (!std::ranges::all_of(sids, allowed)) {
		return "the token restricts to a SID the client has not enabled";
	}
	return std::string();
}

[[nodiscard]] std::string CheckPrivileges(HANDLE token) {
	const auto privileges = ReadToken(token, TokenPrivileges);
	if (privileges.bytes.empty()) {
		return NumberedError("GetTokenInformation", privileges.error);
	}
	const auto list = As<TOKEN_PRIVILEGES>(privileges);
	const auto all = std::span(list->Privileges, list->PrivilegeCount);
	for (const auto &privilege : all) {
		if (privilege.Luid.LowPart != kChangeNotifyPrivilege.LowPart
			|| privilege.Luid.HighPart != kChangeNotifyPrivilege.HighPart) {
			return "the token keeps privilege "
				+ std::to_string(privilege.Luid.LowPart);
		}
	}
	return std::string();
}

[[nodiscard]] std::string CheckLessPrivileged(
		const Buffer &attributes,
		AppContainerLevel level) {
	const auto information = As<SecurityAttributes>(attributes);
	const auto records = information->attributes;
	const auto count = std::size_t(information->count);
	const auto malformed = std::string(
		"the token's security attributes are malformed");
	if (information->version
		!= CLAIM_SECURITY_ATTRIBUTES_INFORMATION_VERSION_V1) {
		return "the token's security attributes version is "
			+ std::to_string(information->version);
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
			return "the token's WIN://NOALLAPPPKG type is "
				+ std::to_string(found->valueType);
		} else if (found->valueCount != 1) {
			return "the token's WIN://NOALLAPPPKG has "
				+ std::to_string(found->valueCount)
				+ " values";
		} else if (!Within(attributes, value, 1)) {
			return malformed;
		} else if (*value != 1) {
			return "the token's WIN://NOALLAPPPKG value is "
				+ std::to_string(*value);
		}
	}
	const auto less = (found != nullptr);
	if (less == (level == AppContainerLevel::LessPrivileged)) {
		return std::string();
	}
	return less
		? "the token is a less privileged AppContainer token"
		: "the token is not a less privileged AppContainer token";
}

[[nodiscard]] std::string FirstError(
		HANDLE token,
		std::initializer_list<Check> checks) {
	for (const auto check : checks) {
		auto error = check(token);
		if (!error.empty()) {
			return error;
		}
	}
	return std::string();
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
	return kX64
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
	if (const auto userenv = LoadLibraryExW(
			L"userenv.dll",
			nullptr,
			LOAD_LIBRARY_SEARCH_SYSTEM32)) {
		Resolve(userenv, derive, "DeriveAppContainerSidFromAppContainerName");
	}
	if (!derive) {
		return {
			.error = "DeriveAppContainerSidFromAppContainerName "
				"is not available",
		};
	}
	auto sid = PSID();
	const auto derived = derive(kAppContainerName, &sid);
	if (FAILED(derived)) {
		return {
			.error = "DeriveAppContainerSidFromAppContainerName failed: 0x"
				+ Hex(DWORD(derived)),
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
		return { .error = "NtCreateLowBoxToken is not available" };
	} else if (!package.error.empty()) {
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
			.error = "NtCreateLowBoxToken failed: status 0x"
				+ Hex(DWORD(status)),
		};
	}
	return { .handle = UniqueHandle(token) };
}

template <typename Policy>
[[nodiscard]] Mitigation Expectation(
		PROCESS_MITIGATION_POLICY policy,
		const Policy &required,
		const Policy &forbidden = Policy()) {
	return {
		.policy = policy,
		.size = DWORD(sizeof(Policy)),
		.required = required.Flags,
		.forbidden = forbidden.Flags,
	};
}

[[nodiscard]] Policies ComputePolicies() {
	auto result = Policies{
		.childProcess = PROCESS_CREATION_CHILD_PROCESS_RESTRICTED,
		.allApplicationPackages
			= PROCESS_CREATION_ALL_APPLICATION_PACKAGES_OPT_OUT,
	};
	auto &words = result.creation;
	auto &expected = result.mitigations;

	if constexpr (kX86) {
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

	auto win32k = PROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY();
	win32k.DisallowWin32kSystemCalls = 1;
	words[0]
		|= PROCESS_CREATION_MITIGATION_POLICY_WIN32K_SYSTEM_CALL_DISABLE_ALWAYS_ON;
	expected.push_back(Expectation(ProcessSystemCallDisablePolicy, win32k));

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

	auto signature = PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY();
	signature.MicrosoftSignedOnly = 1;
	words[0]
		|= PROCESS_CREATION_MITIGATION_POLICY_BLOCK_NON_MICROSOFT_BINARIES_ALWAYS_ON;
	expected.push_back(Expectation(ProcessSignaturePolicy, signature));

	auto handles = PROCESS_MITIGATION_STRICT_HANDLE_CHECK_POLICY();
	handles.RaiseExceptionOnInvalidHandleReference = 1;
	handles.HandleExceptionsPermanentlyEnabled = 1;
	words[0]
		|= PROCESS_CREATION_MITIGATION_POLICY_STRICT_HANDLE_CHECKS_ALWAYS_ON;
	expected.push_back(Expectation(ProcessStrictHandleCheckPolicy, handles));

	auto fonts = PROCESS_MITIGATION_FONT_DISABLE_POLICY();
	fonts.DisableNonSystemFonts = 1;
	words[0] |= PROCESS_CREATION_MITIGATION_POLICY_FONT_DISABLE_ALWAYS_ON;
	expected.push_back(Expectation(ProcessFontDisablePolicy, fonts));

	auto images = PROCESS_MITIGATION_IMAGE_LOAD_POLICY();
	images.PreferSystem32Images = 1;
	words[0]
		|= PROCESS_CREATION_MITIGATION_POLICY_IMAGE_LOAD_PREFER_SYSTEM32_ALWAYS_ON;
	expected.push_back(Expectation(ProcessImageLoadPolicy, images));

	result.creationSize = words[1] ? sizeof(words) : sizeof(words[0]);
	return result;
}

[[nodiscard]] std::string FirstFailure(std::initializer_list<Step> steps) {
	for (const auto step : steps) {
		auto error = step();
		if (!error.empty()) {
			return error;
		}
	}
	return std::string();
}

[[nodiscard]] std::string WarmUp() {
	const auto &functions = ResolvedPolicyFunctions();
	if (!functions.get) {
		return "the mitigation policy functions are not available";
	}
	// No thread can load a DLL after the lowering, so bind and warm them now.
	const auto bound = __HrLoadAllImportsForDll("bcrypt.dll");
	if (FAILED(bound)) {
		return "__HrLoadAllImportsForDll(bcrypt.dll) failed: 0x"
			+ Hex(DWORD(bound));
	}
	const auto &package = ExpectedPackage();
	if (!package.error.empty()) {
		return package.error;
	}
	auto bytes = std::array<UCHAR, kWarmUpRandomBytes>();
	const auto status = BCryptGenRandom(
		nullptr,
		bytes.data(),
		ULONG(bytes.size()),
		BCRYPT_USE_SYSTEM_PREFERRED_RNG);
	return BCRYPT_SUCCESS(status)
		? std::string()
		: ("BCryptGenRandom failed: status 0x" + Hex(DWORD(status)));
}

[[nodiscard]] std::string RevertImpersonation() {
	return RevertToSelf()
		? std::string()
		: NumberedError("RevertToSelf", GetLastError());
}

[[nodiscard]] std::string ForgetRegistryRoots() {
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
	return std::string();
}

[[nodiscard]] std::string VerifyMitigations() {
	for (const auto &mitigation : SandboxPolicies().mitigations) {
		const auto flags = ReadMitigation(mitigation.policy, mitigation.size);
		if (!flags) {
			return PolicyError(
				"GetProcessMitigationPolicy",
				mitigation.policy,
				GetLastError());
		} else if ((*flags & mitigation.required) != mitigation.required
			|| (*flags & mitigation.forbidden)) {
			return "the mitigation policy "
				+ std::to_string(int(mitigation.policy))
				+ " reads 0x"
				+ Hex(*flags);
		}
	}
	return std::string();
}

} // namespace

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
	if (!lockdown.error.empty()) {
		return { .error = std::move(lockdown.error) };
	}
	auto restricted = CreateLowRestricted(
		source,
		{},
		restricting,
		low.bytes.data());
	if (!restricted.error.empty()) {
		return { .error = std::move(restricted.error) };
	}
	auto boxedLockdown = CreateLowBox(lockdown.handle.get());
	if (!boxedLockdown.error.empty()) {
		return { .error = std::move(boxedLockdown.error) };
	}
	auto boxedRestricted = CreateLowBox(restricted.handle.get());
	if (!boxedRestricted.error.empty()) {
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
	if (!error.empty()) {
		return { .error = "the lockdown token check failed: " + error };
	}
	error = CheckInitialToken(result.initial.get());
	if (!error.empty()) {
		return { .error = "the initial token check failed: " + error };
	}
	// Only the process creation attribute makes the primary less privileged.
	error = CheckAppContainerToken(
		result.lockdown.get(),
		AppContainerLevel::Regular);
	if (!error.empty()) {
		return { .error = "the lockdown token check failed: " + error };
	}
	error = CheckAppContainerToken(
		result.initial.get(),
		AppContainerLevel::Regular);
	if (!error.empty()) {
		return { .error = "the initial token check failed: " + error };
	}
	return result;
}

std::string CheckLockdownToken(HANDLE token) {
	return FirstError(token, {
		CheckPrimary,
		CheckLow,
		CheckDenyOnly,
		CheckNullRestricting,
		CheckPrivileges,
	});
}

std::string CheckInitialToken(HANDLE token) {
	return FirstError(token, {
		CheckImpersonation,
		CheckLow,
		CheckClientRestricting,
		CheckPrivileges,
	});
}

std::string CheckAppContainerToken(HANDLE token, AppContainerLevel level) {
	const auto &package = ExpectedPackage();
	if (!package.error.empty()) {
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
		return "the token's AppContainer information is truncated";
	} else if (!*As<DWORD>(container)) {
		return "the token is not an AppContainer token";
	}
	const auto information = As<TOKEN_APPCONTAINER_INFORMATION>(sid);
	const auto actual = information->TokenAppContainer;
	const auto expected = const_cast<std::byte*>(package.sid.data());
	const auto count = Groups(capabilities).size();
	if (!actual || !EqualSid(actual, expected)) {
		return "the token's AppContainer SID is not the widget helper's";
	} else if (count) {
		return "the token has " + std::to_string(count) + " capabilities";
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

JobState ReadJob() {
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

std::string VerifyJob() {
	const auto job = ReadJob();
	const auto &basic = job.limits.BasicLimitInformation;
	const auto ui = job.restrictions.UIRestrictionsClass;
	if (job.failed) {
		return NumberedError(job.failed, job.error);
	} else if (!job.inJob) {
		return "the process is not in a job";
	} else if ((basic.LimitFlags & kJobLimits) != kJobLimits
		|| basic.ActiveProcessLimit != kJobActiveProcesses) {
		return "the job's limits are 0x"
			+ Hex(basic.LimitFlags)
			+ " for "
			+ std::to_string(basic.ActiveProcessLimit)
			+ " processes";
	} else if ((ui & kJobUiRestrictions) != kJobUiRestrictions) {
		return "the job's UI restrictions are 0x" + Hex(ui);
	}
	return std::string();
}

const Policies &SandboxPolicies() {
	static const auto result = ComputePolicies();
	return result;
}

std::optional<DWORD> ReadMitigation(
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

std::string Hex(DWORD value) {
	auto buffer = std::array<char, sizeof(value) * 2>();
	const auto end = std::to_chars(
		buffer.data(),
		buffer.data() + buffer.size(),
		value,
		16).ptr;
	return std::string(buffer.data(), end);
}

std::string NumberedError(const char *call, DWORD error) {
	return std::string(call) + " failed: error " + std::to_string(error);
}

std::string PolicyError(
		const char *call,
		PROCESS_MITIGATION_POLICY policy,
		DWORD error) {
	return std::string(call)
		+ "("
		+ std::to_string(int(policy))
		+ ") failed: error "
		+ std::to_string(error);
}

std::string VerifyNoThreadToken() {
	auto token = HANDLE();
	if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token)) {
		const auto guard = UniqueHandle(token);
		return "the thread still has a token";
	}
	const auto error = GetLastError();
	return (error == ERROR_NO_TOKEN)
		? std::string()
		: NumberedError("OpenThreadToken", error);
}

std::string LockDownHelper() {
	const auto error = FirstFailure({
		WarmUp,
		RevertImpersonation,
		ForgetRegistryRoots,
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
	return error.empty()
		? std::string()
		: ("the widget helper is not sandboxed: " + error);
}

} // namespace Platform::WidgetHelper
