/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/widget_helper_image_win.h"

#include "platform/win/widget_sandbox_win.h"
#include "widget_helper_identity.h"

#include <gsl/gsl>

#include <bcrypt.h>
#include <wincrypt.h>

#include <mscat.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <utility>

namespace Platform::WidgetHelper {
namespace {

constexpr auto kPathCapacity = DWORD(32768);
constexpr auto kDigestSize = std::size_t(32);
constexpr auto kChunkSize = DWORD(64 * 1024);
constexpr auto kChecksumSize = std::uint64_t(4);
constexpr auto kCertificateEntrySize = std::uint64_t(8);
constexpr auto kGlobalRoot = std::wstring_view(L"\\\\?\\GLOBALROOT");
constexpr auto kNetworkPrefix = std::wstring_view(L"\\\\?\\UNC\\");
constexpr auto kModulesName = std::wstring_view(L"modules");
constexpr auto kHelperName = std::wstring_view(L"TelegramWidgetHelper.exe");
constexpr auto kFolderAccess = DWORD(FILE_TRAVERSE
	| FILE_READ_ATTRIBUTES
	| SYNCHRONIZE);
constexpr auto kFolderSharing = DWORD(FILE_SHARE_READ | FILE_SHARE_WRITE);
constexpr auto kAnySharing = DWORD(FILE_SHARE_READ
	| FILE_SHARE_WRITE
	| FILE_SHARE_DELETE);
// FAT file ids can change and ReFS 64-bit ids are not unique.
constexpr auto kIndexedFileSystem = std::wstring_view(L"NTFS");

#if defined _M_ARM64
constexpr auto kHelperArchitecture = std::wstring_view(L"arm64");
#elif defined _M_X64
constexpr auto kHelperArchitecture = std::wstring_view(L"x64");
#elif defined _M_IX86
constexpr auto kHelperArchitecture = std::wstring_view(L"x86");
#else // _M_ARM64 || _M_X64 || _M_IX86
#error "The widget helper has no modules folder for this architecture."
#endif // _M_ARM64 || _M_X64 || _M_IX86

struct Name {
	std::wstring value;
	std::string error;
};

struct Opened {
	UniqueHandle handle;
	std::string error;
};

struct Entry {
	UniqueHandle handle;
	std::wstring name;
	std::string error;
};

struct Identity {
	FILE_ID_INFO value = {};
	std::string error;
};

struct Root {
	std::wstring native;
	std::vector<std::wstring> components;
	std::string error;
};

struct CatalogFunctions {
	decltype(&::CryptCATAdminAcquireContext2) acquire = nullptr;
	decltype(&::CryptCATAdminCalcHashFromFileHandle2) calculate = nullptr;
	decltype(&::CryptCATAdminReleaseContext) release = nullptr;
};

template <typename Function>
void Resolve(HMODULE module, Function &function, const char *name) {
	function = reinterpret_cast<Function>(GetProcAddress(module, name));
}

[[nodiscard]] const CatalogFunctions &ResolvedCatalogFunctions() {
	static const auto result = [] {
		auto functions = CatalogFunctions();
		if (const auto wintrust = LoadLibraryExW(
				L"wintrust.dll",
				nullptr,
				LOAD_LIBRARY_SEARCH_SYSTEM32)) {
			Resolve(
				wintrust,
				functions.acquire,
				"CryptCATAdminAcquireContext2");
			Resolve(
				wintrust,
				functions.calculate,
				"CryptCATAdminCalcHashFromFileHandle2");
			Resolve(
				wintrust,
				functions.release,
				"CryptCATAdminReleaseContext");
		}
		return functions;
	}();
	return result;
}

[[nodiscard]] const details::BuiltDigest *ExpectedDigest() {
	const auto architecture = Utf8(kHelperArchitecture);
	for (const auto &built : details::kBuiltDigests) {
		if (built.architecture == architecture) {
			return &built;
		}
	}
	return nullptr;
}

[[nodiscard]] std::string HexDigest(std::span<const BYTE> bytes) {
	constexpr auto kDigits = std::string_view("0123456789abcdef");
	auto result = std::string();
	result.reserve(bytes.size() * 2);
	for (const auto byte : bytes) {
		result.push_back(kDigits[byte >> 4]);
		result.push_back(kDigits[byte & 0x0F]);
	}
	return result;
}

[[nodiscard]] std::string HoldError(
		std::wstring_view item,
		const std::string &error) {
	return "could not hold " + Utf8(item) + ": " + error;
}

[[nodiscard]] std::string RedirectedError(std::wstring_view item) {
	return "the widget helper path is redirected: " + Utf8(item);
}

[[nodiscard]] std::string ImageError(const std::string &detail) {
	return "the image check failed: " + detail;
}

[[nodiscard]] Name ExecutableDirectory() {
	auto result = std::wstring(kPathCapacity, wchar_t());
	const auto length = GetModuleFileNameW(
		nullptr,
		result.data(),
		kPathCapacity);
	if (!length) {
		return { .error = SystemError("GetModuleFileNameW", GetLastError()) };
	} else if (length >= kPathCapacity) {
		return { .error = "the executable path is too long" };
	}
	result.resize(length);
	const auto separator = result.find_last_of(L'\\');
	if (separator == std::wstring::npos) {
		return { .error = "the executable path has no directory" };
	}
	result.resize(separator + 1);
	return { .value = std::move(result) };
}

[[nodiscard]] Name FinalName(HANDLE handle, DWORD volume) {
	auto result = std::wstring(kPathCapacity, wchar_t());
	const auto length = GetFinalPathNameByHandleW(
		handle,
		result.data(),
		kPathCapacity,
		FILE_NAME_NORMALIZED | volume);
	if (!length) {
		return {
			.error = SystemError("GetFinalPathNameByHandleW", GetLastError()),
		};
	} else if (length >= kPathCapacity) {
		return { .error = "the final path is too long" };
	}
	result.resize(length);
	if (!result.empty() && result.back() == L'\\') {
		result.pop_back();
	}
	return { .value = std::move(result) };
}

[[nodiscard]] Opened Open(
		const std::wstring &path,
		DWORD access,
		DWORD sharing,
		DWORD flags) {
	const auto handle = CreateFileW(
		path.c_str(),
		access,
		sharing,
		nullptr,
		OPEN_EXISTING,
		flags,
		nullptr);
	if (handle == INVALID_HANDLE_VALUE) {
		return { .error = SystemError("CreateFileW", GetLastError()) };
	}
	return { .handle = UniqueHandle(handle) };
}

[[nodiscard]] std::string CheckIndexedFileSystem(HANDLE handle) {
	auto name = std::array<wchar_t, MAX_PATH + 1>();
	if (!GetVolumeInformationByHandleW(
			handle,
			nullptr,
			0,
			nullptr,
			nullptr,
			nullptr,
			name.data(),
			DWORD(name.size()))) {
		return SystemError("GetVolumeInformationByHandleW", GetLastError());
	}
	const auto system = std::wstring_view(name.data());
	if (system != kIndexedFileSystem) {
		return "the "
			+ Utf8(system)
			+ " file system has no reliable file index";
	}
	return std::string();
}

[[nodiscard]] Identity ReadLegacyIdentity(HANDLE handle) {
	auto information = BY_HANDLE_FILE_INFORMATION();
	if (!GetFileInformationByHandle(handle, &information)) {
		return {
			.error = SystemError("GetFileInformationByHandle", GetLastError()),
		};
	}
	auto error = CheckIndexedFileSystem(handle);
	if (!error.empty()) {
		return { .error = std::move(error) };
	}
	const auto index = (std::uint64_t(information.nFileIndexHigh) << 32)
		| information.nFileIndexLow;
	if (!index) {
		return { .error = "the file index is unavailable" };
	}
	auto result = Identity();
	result.value.VolumeSerialNumber = information.dwVolumeSerialNumber;
	std::memcpy(result.value.FileId.Identifier, &index, sizeof(index));
	return result;
}

[[nodiscard]] Identity ReadIdentity(HANDLE handle) {
	if (CurrentSandbox().tier == SandboxTier::Windows7) {
		return ReadLegacyIdentity(handle);
	}
	auto result = Identity();
	if (!GetFileInformationByHandleEx(
			handle,
			FileIdInfo,
			&result.value,
			DWORD(sizeof(result.value)))) {
		return {
			.error = SystemError(
				"GetFileInformationByHandleEx",
				GetLastError()),
		};
	}
	return result;
}

[[nodiscard]] bool SameFile(const FILE_ID_INFO &a, const FILE_ID_INFO &b) {
	return (a.VolumeSerialNumber == b.VolumeSerialNumber)
		&& std::ranges::equal(a.FileId.Identifier, b.FileId.Identifier);
}

[[nodiscard]] bool InFolder(
		std::wstring_view native,
		std::wstring_view folder) {
	return (native.size() > folder.size() + 1)
		&& native.starts_with(folder)
		&& (native[folder.size()] == L'\\')
		&& (native.find(L'\\', folder.size() + 1) == std::wstring_view::npos);
}

[[nodiscard]] std::string CheckEntry(
		HANDLE handle,
		std::wstring_view item,
		bool folder) {
	auto tag = FILE_ATTRIBUTE_TAG_INFO();
	if (!GetFileInformationByHandleEx(
			handle,
			FileAttributeTagInfo,
			&tag,
			DWORD(sizeof(tag)))) {
		return HoldError(
			item,
			SystemError("GetFileInformationByHandleEx", GetLastError()));
	} else if ((tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
		&& IsReparseTagNameSurrogate(tag.ReparseTag)) {
		return RedirectedError(item);
	}
	const auto directory = (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY);
	if ((directory != 0) != folder) {
		return HoldError(
			item,
			folder ? "it is not a folder" : "it is a folder");
	}
	return std::string();
}

[[nodiscard]] Entry HoldEntry(const std::wstring &native, bool folder) {
	auto opened = Open(
		std::wstring(kGlobalRoot) + native,
		folder ? kFolderAccess : GENERIC_READ,
		folder ? kFolderSharing : FILE_SHARE_READ,
		(FILE_FLAG_OPEN_REPARSE_POINT
			| (folder ? FILE_FLAG_BACKUP_SEMANTICS : DWORD())));
	if (!opened.error.empty()) {
		return { .error = HoldError(native, opened.error) };
	}
	auto error = CheckEntry(opened.handle.get(), native, folder);
	if (!error.empty()) {
		return { .error = std::move(error) };
	}
	auto name = FinalName(opened.handle.get(), VOLUME_NAME_NT);
	if (!name.error.empty()) {
		return { .error = HoldError(native, name.error) };
	}
	return {
		.handle = std::move(opened.handle),
		.name = std::move(name.value),
	};
}

[[nodiscard]] std::string HoldFolder(
		std::vector<Entry> &folders,
		std::wstring_view name) {
	const auto &parent = folders.back().name;
	auto item = parent + L'\\';
	item += name;
	auto entry = HoldEntry(item, true);
	if (!entry.error.empty()) {
		return std::move(entry.error);
	} else if (!InFolder(entry.name, parent)) {
		return RedirectedError(item);
	}
	folders.push_back(std::move(entry));
	return std::string();
}

[[nodiscard]] Root FindRoot(HANDLE folder) {
	auto native = FinalName(folder, VOLUME_NAME_NT);
	if (!native.error.empty()) {
		return { .error = std::move(native.error) };
	}
	auto relative = FinalName(folder, VOLUME_NAME_NONE);
	if (!relative.error.empty()) {
		return { .error = std::move(relative.error) };
	}
	auto dos = FinalName(folder, VOLUME_NAME_DOS);
	if (!dos.error.empty()) {
		return { .error = std::move(dos.error) };
	}
	const auto &inside = relative.value;
	if ((!inside.empty() && inside.front() != L'\\')
		|| (native.value.size() <= inside.size())
		|| !native.value.ends_with(inside)) {
		return { .error = "the final paths of the folder disagree" };
	}
	auto result = Root{
		.native = native.value.substr(0, native.value.size() - inside.size()),
	};
	for (auto from = std::size_t(); from < inside.size();) {
		const auto till = std::min(inside.find(L'\\', from + 1), inside.size());
		auto component = inside.substr(from + 1, till - from - 1);
		if (component.empty()) {
			return { .error = "the final path has an empty component" };
		}
		result.components.push_back(std::move(component));
		from = till;
	}
	if (dos.value.starts_with(kNetworkPrefix)) {
		auto &components = result.components;
		if (components.size() < 2) {
			return { .error = "the network path has no share" };
		}
		result.native += L'\\' + components[0] + L'\\' + components[1];
		components.erase(begin(components), begin(components) + 2);
	}
	return result;
}

[[nodiscard]] std::string StatusError(const char *call, NTSTATUS status) {
	return std::string(call) + " failed: status 0x" + Hex(DWORD(status));
}

[[nodiscard]] std::string Seek(HANDLE file, std::uint64_t offset) {
	auto position = LARGE_INTEGER();
	position.QuadPart = LONGLONG(offset);
	if (!SetFilePointerEx(file, position, nullptr, FILE_BEGIN)) {
		return SystemError("SetFilePointerEx", GetLastError());
	}
	return std::string();
}

[[nodiscard]] std::string ReadExactly(
		HANDLE file,
		std::span<std::byte> buffer) {
	const auto size = DWORD(buffer.size());
	auto read = DWORD();
	if (!ReadFile(file, buffer.data(), size, &read, nullptr)) {
		return SystemError("ReadFile", GetLastError());
	} else if (read != size) {
		return "ReadFile read "
			+ std::to_string(read)
			+ " of "
			+ std::to_string(size)
			+ " bytes";
	}
	return std::string();
}

[[nodiscard]] std::string CheckImageExtent(
		HANDLE file,
		const details::BuiltDigest &built) {
	auto length = LARGE_INTEGER();
	if (!GetFileSizeEx(file, &length)) {
		return SystemError("GetFileSizeEx", GetLastError());
	}
	const auto size = std::uint64_t(length.QuadPart);
	if (length.QuadPart < 0 || size < built.unsignedEnd) {
		return "the widget helper image extent is invalid";
	}
	auto entry = std::array<DWORD, 2>();
	auto error = Seek(file, built.certificateEntryOffset);
	if (error.empty()) {
		error = ReadExactly(file, std::as_writable_bytes(std::span(entry)));
	}
	if (!error.empty()) {
		return error;
	}
	const auto [table, tableSize] = entry;
	const auto valid = (!table && !tableSize)
		? (size == built.unsignedEnd)
		: ((table == built.unsignedEnd)
			&& (tableSize != 0)
			&& !(tableSize % 8)
			&& (std::uint64_t(table) + tableSize == size));
	return valid
		? std::string()
		: "the widget helper image extent is invalid";
}

[[nodiscard]] std::string HashImage(
		HANDLE file,
		const details::BuiltDigest &built,
		std::array<BYTE, kDigestSize> &digest) {
	auto algorithm = BCRYPT_ALG_HANDLE();
	auto status = BCryptOpenAlgorithmProvider(
		&algorithm,
		BCRYPT_SHA256_ALGORITHM,
		nullptr,
		0);
	if (!BCRYPT_SUCCESS(status)) {
		return StatusError("BCryptOpenAlgorithmProvider", status);
	}
	const auto closeAlgorithm = gsl::finally([&] {
		BCryptCloseAlgorithmProvider(algorithm, 0);
	});
	auto hash = BCRYPT_HASH_HANDLE();
	status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
	if (!BCRYPT_SUCCESS(status)) {
		return StatusError("BCryptCreateHash", status);
	}
	const auto destroyHash = gsl::finally([&] {
		BCryptDestroyHash(hash);
	});
	const auto ranges = std::array{
		std::pair(std::uint64_t(), built.checksumOffset),
		std::pair(
			built.checksumOffset + kChecksumSize,
			built.certificateEntryOffset),
		std::pair(
			built.certificateEntryOffset + kCertificateEntrySize,
			built.unsignedEnd),
	};
	auto chunk = std::vector<std::byte>(kChunkSize);
	for (const auto &[from, till] : ranges) {
		auto error = Seek(file, from);
		if (!error.empty()) {
			return error;
		}
		for (auto at = from; at < till;) {
			const auto size = DWORD(
				std::min(till - at, std::uint64_t(kChunkSize)));
			error = ReadExactly(file, std::span(chunk).first(size));
			if (!error.empty()) {
				return error;
			}
			status = BCryptHashData(
				hash,
				reinterpret_cast<PUCHAR>(chunk.data()),
				size,
				0);
			if (!BCRYPT_SUCCESS(status)) {
				return StatusError("BCryptHashData", status);
			}
			at += size;
		}
	}
	status = BCryptFinishHash(hash, digest.data(), ULONG(digest.size()), 0);
	if (!BCRYPT_SUCCESS(status)) {
		return StatusError("BCryptFinishHash", status);
	}
	return std::string();
}

[[nodiscard]] std::string CheckImageDigest(
		HANDLE file,
		const details::BuiltDigest &built) {
	auto error = CheckImageExtent(file, built);
	if (!error.empty()) {
		return error;
	}
	auto digest = std::array<BYTE, kDigestSize>();
	error = HashImage(file, built, digest);
	if (!error.empty()) {
		return error;
	} else if (HexDigest(digest) != built.digest) {
		return "the widget helper digest does not match this build";
	}
	return std::string();
}

[[nodiscard]] std::string CheckDigest(
		HANDLE file,
		const details::BuiltDigest &built) {
	if (CurrentSandbox().tier == SandboxTier::Windows7) {
		return CheckImageDigest(file, built);
	}
	const auto expected = std::string_view(built.digest);
	const auto &functions = ResolvedCatalogFunctions();
	if (!functions.acquire) {
		return "CryptCATAdminAcquireContext2 is not available";
	} else if (!functions.calculate) {
		return "CryptCATAdminCalcHashFromFileHandle2 is not available";
	} else if (!functions.release) {
		return "CryptCATAdminReleaseContext is not available";
	}
	auto admin = HCATADMIN();
	if (!functions.acquire(
			&admin,
			nullptr,
			BCRYPT_SHA256_ALGORITHM,
			nullptr,
			0)) {
		return SystemError("CryptCATAdminAcquireContext2", GetLastError());
	}
	const auto release = gsl::finally([&] {
		functions.release(admin, 0);
	});
	auto digest = std::array<BYTE, kDigestSize>();
	auto size = DWORD(digest.size());
	if (!functions.calculate(admin, file, &size, digest.data(), 0)) {
		return SystemError(
			"CryptCATAdminCalcHashFromFileHandle2",
			GetLastError());
	} else if (size != digest.size()) {
		return "CryptCATAdminCalcHashFromFileHandle2 gave "
			+ std::to_string(size)
			+ " bytes";
	} else if (HexDigest(digest) != expected) {
		return "the widget helper digest does not match this build";
	}
	return std::string();
}

} // namespace

HeldHelper HoldHelper() {
	const auto expected = ExpectedDigest();
	if (!expected) {
		return {
			.error = "the widget helper identity of this build is unknown",
		};
	}
	auto directory = ExecutableDirectory();
	if (!directory.error.empty()) {
		return { .error = std::move(directory.error) };
	}
	auto initial = Open(
		directory.value,
		kFolderAccess,
		kFolderSharing,
		FILE_FLAG_BACKUP_SEMANTICS);
	if (!initial.error.empty()) {
		return { .error = HoldError(directory.value, initial.error) };
	}
	const auto own = ReadIdentity(initial.handle.get());
	if (!own.error.empty()) {
		return { .error = HoldError(directory.value, own.error) };
	}
	auto root = FindRoot(initial.handle.get());
	if (!root.error.empty()) {
		return { .error = HoldError(directory.value, root.error) };
	}
	auto folders = std::vector<Entry>();
	folders.push_back({
		.handle = std::move(initial.handle),
		.name = std::move(directory.value),
	});
	auto top = HoldEntry(root.native + L'\\', true);
	if (!top.error.empty()) {
		return { .error = std::move(top.error) };
	} else if (top.name != root.native) {
		return { .error = RedirectedError(root.native) };
	}
	folders.push_back(std::move(top));
	for (const auto &component : root.components) {
		auto error = HoldFolder(folders, component);
		if (!error.empty()) {
			return { .error = std::move(error) };
		}
	}
	const auto canonical = ReadIdentity(folders.back().handle.get());
	if (!canonical.error.empty()) {
		return { .error = HoldError(folders.back().name, canonical.error) };
	} else if (!SameFile(canonical.value, own.value)) {
		return { .error = RedirectedError(folders.back().name) };
	}
	for (const auto name : std::array{ kModulesName, kHelperArchitecture }) {
		auto error = HoldFolder(folders, name);
		if (!error.empty()) {
			return { .error = std::move(error) };
		}
	}
	auto item = folders.back().name + L'\\';
	item += kHelperName;
	auto file = HoldEntry(item, false);
	if (!file.error.empty()) {
		return { .error = std::move(file.error) };
	} else if (!InFolder(file.name, folders.back().name)) {
		return { .error = RedirectedError(item) };
	}
	auto result = HeldHelper();
	for (auto &folder : folders) {
		auto error = CheckEntry(folder.handle.get(), folder.name, true);
		if (!error.empty()) {
			return { .error = std::move(error) };
		}
		result.folders.push_back(std::move(folder.handle));
	}
	const auto identity = ReadIdentity(file.handle.get());
	if (!identity.error.empty()) {
		return { .error = HoldError(file.name, identity.error) };
	}
	auto error = CheckDigest(file.handle.get(), *expected);
	if (!error.empty()) {
		return { .error = std::move(error) };
	}
	auto path = FinalName(file.handle.get(), VOLUME_NAME_DOS);
	if (!path.error.empty()) {
		return { .error = HoldError(file.name, path.error) };
	}
	result.path = std::move(path.value);
	result.native = std::move(file.name);
	result.file = std::move(file.handle);
	result.identity = identity.value;
	return result;
}

std::string CheckStartedImage(const HeldHelper &held, HANDLE process) {
	auto name = std::wstring(kPathCapacity, wchar_t());
	auto size = kPathCapacity;
	if (!QueryFullProcessImageNameW(
			process,
			PROCESS_NAME_NATIVE,
			name.data(),
			&size)) {
		return ImageError(
			SystemError("QueryFullProcessImageNameW", GetLastError()));
	}
	name.resize(size);
	if (name != held.native) {
		return ImageError("the started image is not the checked file");
	}
	const auto opened = Open(
		std::wstring(kGlobalRoot) + name,
		FILE_READ_ATTRIBUTES,
		kAnySharing,
		0);
	if (!opened.error.empty()) {
		return ImageError(opened.error);
	}
	const auto identity = ReadIdentity(opened.handle.get());
	if (!identity.error.empty()) {
		return ImageError(identity.error);
	} else if (!SameFile(identity.value, held.identity)) {
		return ImageError("the started image is not the checked file");
	}
	return std::string();
}

} // namespace Platform::WidgetHelper
