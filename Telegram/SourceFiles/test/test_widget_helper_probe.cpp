/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "test/test_widget_helper_probe.h"

#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "platform/platform_widget_helper.h"
#include "settings.h"

#include <QtCore/QFile>
#include <QtCore/QStandardPaths>

#ifdef Q_OS_MAC
#include <CoreAudio/AudioHardware.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMediaIO/CMIOHardware.h>
#include <IOKit/IOKitLib.h>
#include <Security/Security.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach_debug/mach_debug.h>
#include <netinet/in.h>
#include <servers/bootstrap.h>
#include <sys/proc_info.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <dirent.h>
#include <fcntl.h>
#include <libproc.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

// Exported by libsystem_sandbox, CoreGraphics (SkyLight) and libdispatch,
// declared in no SDK header: the policy reading of a process, its window
// server connection (0 when there is none and none can be made) and the
// libtrace metadata at the end of the process's firehose buffer header
// (NULL with a zero length when logging is disabled).
extern "C" int sandbox_check(pid_t pid, const char *operation, int type, ...);
extern "C" int CGSMainConnectionID(void);
extern "C" void *voucher_activity_get_metadata_buffer(size_t *length);
#elif defined Q_OS_WIN // Q_OS_MAC
#include <cstdio>
#endif // Q_OS_MAC || Q_OS_WIN

namespace Test {
namespace {

[[nodiscard]] QString ProbeMode() {
	return u"probe"_q;
}

[[nodiscard]] QString UnsandboxedMode() {
	return u"probe-unsandboxed"_q;
}

[[nodiscard]] QString ConnectControlArgument() {
	return u"connect-control"_q;
}

#ifdef Q_OS_MAC

// One stderr row is cut past this (a cut row makes the listing incomplete);
// a whole PATH_MAX path fits in it.
constexpr auto kRowBytes = size_t(2048);

// The Refused reason: SandboxEngine::kRemoteTextLimit is 4000.
constexpr auto kSummaryBytes = size_t(3900);

constexpr auto kChildWait = std::chrono::milliseconds(2000);
constexpr auto kKillWait = std::chrono::milliseconds(1000);
constexpr auto kConnectWaitMs = 2000;
constexpr auto kListTries = 8;
constexpr auto kProcArgsBytes = size_t(1) << 20;

// sandbox_check()'s filter type for a path argument (SANDBOX_FILTER_PATH).
constexpr auto kSandboxFilterNone = 0;
constexpr auto kSandboxFilterPath = 1;

constexpr auto kTrue = "/usr/bin/true";

struct Reference {
	const char *service = nullptr;
	bool libSystem = false;
};

// libSystem's own logging and notify connections (accepted), then services
// a helper must never hold (each a named defect when matched).
constexpr auto kReferences = std::array{
	Reference{ "com.apple.logd", true },
	Reference{ "com.apple.system.notification_center", true },
	Reference{ "com.apple.windowserver.active" },
	Reference{ "com.apple.tccd" },
	Reference{ "com.apple.tccd.system" },
	Reference{ "com.apple.SecurityServer" },
	Reference{ "com.apple.securityd.xpc" },
	Reference{ "com.apple.trustd" },
	Reference{ "com.apple.cfprefsd.daemon" },
	Reference{ "com.apple.cfprefsd.agent" },
	Reference{ "com.apple.system.opendirectoryd.libinfo" },
	Reference{ "com.apple.distributed_notifications@Uv3" },
	Reference{ "com.apple.coreservices.launchservicesd" },
	Reference{ "com.apple.CoreServices.coreservicesd" },
	Reference{ "com.apple.lsd.mapdb" },
	Reference{ "com.apple.CARenderServer" },
	Reference{ "com.apple.audio.audiohald" },
	Reference{ "com.apple.cmio.registerassistantservice" },
	Reference{ "com.apple.locationd.registration" },
	Reference{ "com.apple.locationd.desktop.registration" },
	Reference{ "com.apple.locationd.desktop.synchronous" },
	Reference{ "com.apple.pasteboard.1" },
	Reference{ "com.apple.runningboard" },
	Reference{ "com.apple.analyticsd" },
	Reference{ "com.apple.diagnosticd" },
	Reference{ "com.apple.dock.server" },
};

struct Arguments {
	std::string mode;
	std::string cwd;
	std::string applicationSupport;
	std::string keychainAccount;
	std::vector<std::string> malformed;
	int loopbackPort = 0;
	bool connectControl = false;
};

struct ReferenceReading {
	std::string service;
	std::string error;
	kern_return_t lookup = KERN_SUCCESS;
	mach_port_name_t name = MACH_PORT_NULL;
	natural_t object = 0;
	bool libSystem = false;
	bool held = false;
	bool found = false;
};

struct MachEntry {
	mach_port_name_t name = MACH_PORT_NULL;
	mach_port_type_t type = 0;
	mach_port_urefs_t urefs = 0;
	natural_t object = 0;
	bool tree = false;
};

struct MachSnapshot {
	std::vector<MachEntry> entries;
	std::string error;
	mach_msg_type_number_t tableSize = 0;
	mach_msg_type_number_t treeSize = 0;
};

// The names after logd's in libdispatch's firehose buffer header: the
// first match of `matches`, with the header range and offset in `text`.
struct FirehoseReading {
	std::array<mach_port_name_t, 2> push = {};
	std::string text;
	std::string error;
	int matches = 0;
};

struct Descriptor {
	std::string identity;
	std::string kindName;
	std::string path;
	std::string statIdentity;
	std::vector<std::string> failures;
	pid_t peerPid = 0;
	int fd = -1;
	int kind = 0;
	int fdFlags = 0;
	int statusFlags = 0;
	int domain = 0;
	int type = 0;
};

struct Descriptors {
	std::vector<Descriptor> list;
	std::string error;
	int tries = 0;
};

struct Attempt {
	std::string name;
	std::string code;
};

struct Classified {
	std::string label;
	std::string defect;
	std::string unaccounted;
	std::string failure;
};

struct Counts {
	int task = 0;
	int thread = 0;
	int host = 0;
	int semaphore = 0;
	int clock = 0;
	int voucher = 0;
	int ownName = 0;
	int bootstrap = 0;
	int receive = 0;
	int sendReceive = 0;
	int portSet = 0;
	int deadName = 0;
	int libSystem = 0;
	int firehose = 0;
};

[[nodiscard]] std::string Format(const char *format, ...)
__attribute__((format(printf, 1, 2)));

std::string Format(const char *format, ...) {
	va_list args;
	va_start(args, format);
	va_list copy;
	va_copy(copy, args);
	const auto size = std::vsnprintf(nullptr, 0, format, copy);
	va_end(copy);
	auto result = std::string();
	if (size > 0) {
		result.resize(size_t(size) + 1);
		std::vsnprintf(result.data(), result.size(), format, args);
		result.resize(size_t(size));
	}
	va_end(args);
	return result;
}

[[nodiscard]] const char *ErrnoName(int error) {
	switch (error) {
	case 0: return "0";
	case EPERM: return "EPERM";
	case ENOENT: return "ENOENT";
	case ESRCH: return "ESRCH";
	case EINTR: return "EINTR";
	case EBADF: return "EBADF";
	case ECHILD: return "ECHILD";
	case EAGAIN: return "EAGAIN";
	case ENOMEM: return "ENOMEM";
	case EACCES: return "EACCES";
	case EFAULT: return "EFAULT";
	case ENOTDIR: return "ENOTDIR";
	case EINVAL: return "EINVAL";
	case ENFILE: return "ENFILE";
	case EMFILE: return "EMFILE";
	case EPIPE: return "EPIPE";
	case ENOTSOCK: return "ENOTSOCK";
	case EAFNOSUPPORT: return "EAFNOSUPPORT";
	case EADDRNOTAVAIL: return "EADDRNOTAVAIL";
	case ENETUNREACH: return "ENETUNREACH";
	case ECONNRESET: return "ECONNRESET";
	case EISCONN: return "EISCONN";
	case ETIMEDOUT: return "ETIMEDOUT";
	case ECONNREFUSED: return "ECONNREFUSED";
	case EHOSTUNREACH: return "EHOSTUNREACH";
	case EINPROGRESS: return "EINPROGRESS";
	case ENOTSUP: return "ENOTSUP";
	}
	return "errno";
}

[[nodiscard]] std::string ErrnoText(int error) {
	return Format("%s(%d) %s", ErrnoName(error), error, std::strerror(error));
}

[[nodiscard]] std::string ErrnoCode(int error) {
	const auto name = std::string(ErrnoName(error));
	return (name == "errno") ? Format("errno%d", error) : name;
}

[[nodiscard]] std::string KernText(kern_return_t result) {
	return Format("kr=%d (%s)", result, bootstrap_strerror(result));
}

[[nodiscard]] std::string KernCode(kern_return_t result) {
	return (result == KERN_SUCCESS) ? "ok" : Format("kr%d", result);
}

[[nodiscard]] std::string Status(OSStatus status) {
	return (status == 0) ? "ok" : Format("status%d", int(status));
}

[[nodiscard]] const char *KernelTypeName(ipc_info_object_type_t type) {
	switch (type) {
	case IPC_OTYPE_NONE: return "NONE";
	case IPC_OTYPE_THREAD_CONTROL: return "THREAD_CONTROL";
	case IPC_OTYPE_TASK_CONTROL: return "TASK_CONTROL";
	case IPC_OTYPE_HOST: return "HOST";
	case IPC_OTYPE_HOST_PRIV: return "HOST_PRIV";
	case IPC_OTYPE_PROCESSOR: return "PROCESSOR";
	case IPC_OTYPE_PROCESSOR_SET: return "PROCESSOR_SET";
	case IPC_OTYPE_PROCESSOR_SET_NAME: return "PROCESSOR_SET_NAME";
	case IPC_OTYPE_TIMER: return "TIMER";
	case IPC_OTYPE_UND_REPLY: return "UND_REPLY";
	case IPC_OTYPE_MAIN_DEVICE: return "MAIN_DEVICE";
	case IPC_OTYPE_TASK_NAME: return "TASK_NAME";
	case IPC_OTYPE_SEMAPHORE: return "SEMAPHORE";
	case IPC_OTYPE_CLOCK: return "CLOCK";
	case IPC_OTYPE_IOKIT_IDENT: return "IOKIT_IDENT";
	case IPC_OTYPE_NAMED_ENTRY: return "NAMED_ENTRY";
	case IPC_OTYPE_IOKIT_CONNECT: return "IOKIT_CONNECT";
	case IPC_OTYPE_IOKIT_OBJECT: return "IOKIT_OBJECT";
	case IPC_OTYPE_AU_SESSIONPORT: return "AU_SESSIONPORT";
	case IPC_OTYPE_FILEPORT: return "FILEPORT";
	case IPC_OTYPE_TASK_RESUME: return "TASK_RESUME";
	case IPC_OTYPE_VOUCHER: return "VOUCHER";
	case IPC_OTYPE_WORK_INTERVAL: return "WORK_INTERVAL";
	case IPC_OTYPE_UX_HANDLER: return "UX_HANDLER";
	case IPC_OTYPE_UEXT_OBJECT: return "UEXT_OBJECT";
	case IPC_OTYPE_ARCADE_REG: return "ARCADE_REG";
	case IPC_OTYPE_EVENTLINK: return "EVENTLINK";
	case IPC_OTYPE_TASK_INSPECT: return "TASK_INSPECT";
	case IPC_OTYPE_TASK_READ: return "TASK_READ";
	case IPC_OTYPE_THREAD_INSPECT: return "THREAD_INSPECT";
	case IPC_OTYPE_THREAD_READ: return "THREAD_READ";
	case IPC_OTYPE_HYPERVISOR: return "HYPERVISOR";
	case IPC_OTYPE_TASK_ID_TOKEN: return "TASK_ID_TOKEN";
	case IPC_OTYPE_TASK_FATAL: return "TASK_FATAL";
	case IPC_OTYPE_KCDATA: return "KCDATA";
	case IPC_OTYPE_EXCLAVES_RESOURCE: return "EXCLAVES_RESOURCE";
	case IPC_OTYPE_THREAD_RESUME: return "THREAD_RESUME";
	case IPC_OTYPE_UNKNOWN: return "UNKNOWN";
	default: return "OTHER";
	}
}

[[nodiscard]] std::string RightsText(mach_port_type_t type) {
	struct Bit {
		mach_port_type_t bit = 0;
		const char *name = nullptr;
	};
	const auto bits = std::array{
		Bit{ MACH_PORT_TYPE_SEND, "send" },
		Bit{ MACH_PORT_TYPE_RECEIVE, "receive" },
		Bit{ MACH_PORT_TYPE_SEND_ONCE, "send-once" },
		Bit{ MACH_PORT_TYPE_PORT_SET, "port-set" },
		Bit{ MACH_PORT_TYPE_DEAD_NAME, "dead-name" },
		Bit{ MACH_PORT_TYPE_LABELH, "labelh" },
		Bit{ MACH_PORT_TYPE_DNREQUEST, "dead-name-request" },
		Bit{ MACH_PORT_TYPE_SPREQUEST, "send-possible-request" },
		Bit{ MACH_PORT_TYPE_SPREQUEST_DELAYED, "send-possible-delayed" },
	};
	auto result = std::string();
	auto rest = type;
	for (const auto &[bit, name] : bits) {
		if (type & bit) {
			result += (result.empty() ? "" : "+") + std::string(name);
			rest &= ~bit;
		}
	}
	if (rest) {
		result += (result.empty() ? "" : "+") + Format("unknown-0x%x", rest);
	}
	return result.empty() ? "none" : result;
}

[[nodiscard]] std::string ShortRights(mach_port_type_t type) {
	auto result = std::string();
	const auto letters = std::array{
		std::pair{ MACH_PORT_TYPE_SEND, 'S' },
		std::pair{ MACH_PORT_TYPE_RECEIVE, 'R' },
		std::pair{ MACH_PORT_TYPE_SEND_ONCE, 'O' },
		std::pair{ MACH_PORT_TYPE_PORT_SET, 'P' },
		std::pair{ MACH_PORT_TYPE_DEAD_NAME, 'D' },
	};
	for (const auto &[bit, letter] : letters) {
		if (type & bit) {
			result += letter;
		}
	}
	return result.empty() ? "-" : result;
}

[[nodiscard]] std::string Path(const QString &path) {
	return path.isEmpty()
		? std::string()
		: QFile::encodeName(path).toStdString();
}

[[nodiscard]] Arguments ParseArguments(const QStringList &arguments) {
	auto result = Arguments();
	result.mode = arguments.value(1).toStdString();
	result.cwd = Path(arguments.value(2));
	result.applicationSupport = Path(arguments.value(3));
	result.keychainAccount = arguments.value(4).toStdString();
	const auto port = arguments.value(5);
	if (!port.isEmpty()) {
		auto ok = false;
		const auto value = port.toInt(&ok);
		if (ok && value >= 0 && value < 65536) {
			result.loopbackPort = value;
		} else {
			result.malformed.push_back("loopback port \""
				+ port.left(16).toStdString()
				+ "\"");
		}
	}
	if (arguments.size() > 6) {
		if (arguments[6] == ConnectControlArgument()) {
			result.connectControl = true;
		} else {
			result.malformed.push_back("seventh argument \""
				+ arguments[6].left(32).toStdString()
				+ "\"");
		}
	}
	if (arguments.size() > 7) {
		result.malformed.push_back(Format(
			"%d arguments past connect-control",
			int(arguments.size() - 7)));
	}
	return result;
}

class OwnedFd final {
public:
	OwnedFd() = default;
	explicit OwnedFd(int fd);
	OwnedFd(const OwnedFd &) = delete;
	OwnedFd &operator=(const OwnedFd &) = delete;
	~OwnedFd();

	[[nodiscard]] int get() const;
	[[nodiscard]] int release();
	void reset(int fd = -1);

private:
	int _fd = -1;

};

OwnedFd::OwnedFd(int fd)
: _fd(fd) {
}

OwnedFd::~OwnedFd() {
	reset();
}

int OwnedFd::get() const {
	return _fd;
}

int OwnedFd::release() {
	return std::exchange(_fd, -1);
}

void OwnedFd::reset(int fd) {
	if (_fd >= 0) {
		close(_fd);
	}
	_fd = fd;
}

class Report final {
public:
	Report(pid_t pid, const std::string &mode);

	void row(const std::string &text);

	[[nodiscard]] int rows() const;
	[[nodiscard]] int cut() const;
	[[nodiscard]] int writeError() const;
	[[nodiscard]] const std::string &prefix() const;

private:
	std::string _prefix;
	int _rows = 0;
	int _cut = 0;
	int _writeError = 0;

};

Report::Report(pid_t pid, const std::string &mode)
: _prefix(Format("widget-helper-probe %d %s: ", int(pid), mode.c_str())) {
}

void Report::row(const std::string &text) {
	auto line = _prefix + text;
	if (line.size() > kRowBytes) {
		const auto dropped = line.size() - kRowBytes;
		line.resize(kRowBytes);
		line += Format(" [row cut, %d bytes dropped]", int(dropped));
		++_cut;
	}
	line += '\n';
	++_rows;
	if (_writeError) {
		return;
	}
	auto data = line.data();
	auto left = line.size();
	while (left > 0) {
		const auto written = write(STDERR_FILENO, data, left);
		if (written < 0) {
			if (errno == EINTR) {
				continue;
			}
			_writeError = errno;
			return;
		} else if (written == 0) {
			_writeError = EIO;
			return;
		}
		data += written;
		left -= size_t(written);
	}
}

int Report::rows() const {
	return _rows;
}

int Report::cut() const {
	return _cut;
}

int Report::writeError() const {
	return _writeError;
}

const std::string &Report::prefix() const {
	return _prefix;
}

[[nodiscard]] MachSnapshot SnapshotMach() {
	auto result = MachSnapshot();
	auto info = ipc_info_space_t();
	auto table = ipc_info_name_array_t();
	auto tableCount = mach_msg_type_number_t();
	auto tree = ipc_info_tree_name_array_t();
	auto treeCount = mach_msg_type_number_t();
	const auto kr = mach_port_space_info(
		mach_task_self(),
		&info,
		&table,
		&tableCount,
		&tree,
		&treeCount);
	if (kr != KERN_SUCCESS) {
		result.error = "mach_port_space_info failed: " + KernText(kr);
		return result;
	}
	const auto release = gsl::finally([&] {
		if (table) {
			vm_deallocate(
				mach_task_self(),
				vm_address_t(table),
				tableCount * sizeof(*table));
		}
		if (tree) {
			vm_deallocate(
				mach_task_self(),
				vm_address_t(tree),
				treeCount * sizeof(*tree));
		}
	});
	result.tableSize = tableCount;
	result.treeSize = treeCount;
	for (auto i = mach_msg_type_number_t(); i != tableCount; ++i) {
		const auto &entry = table[i];
		if (entry.iin_type) {
			result.entries.push_back({
				.name = entry.iin_name,
				.type = entry.iin_type,
				.urefs = entry.iin_urefs,
				.object = entry.iin_object,
			});
		}
	}
	for (auto i = mach_msg_type_number_t(); i != treeCount; ++i) {
		const auto &entry = tree[i].iitn_name;
		if (entry.iin_type) {
			result.entries.push_back({
				.name = entry.iin_name,
				.type = entry.iin_type,
				.urefs = entry.iin_urefs,
				.object = entry.iin_object,
				.tree = true,
			});
		}
	}
	ranges::sort(result.entries, std::less<>(), &MachEntry::name);
	return result;
}

[[nodiscard]] const MachEntry *FindEntry(
		const MachSnapshot &snapshot,
		mach_port_name_t name) {
	for (const auto &entry : snapshot.entries) {
		if (entry.name == name) {
			return &entry;
		}
	}
	return nullptr;
}

[[nodiscard]] std::string EntryText(const MachEntry &entry) {
	return Format(
		"0x%x %s urefs=%u object=0x%x%s",
		entry.name,
		ShortRights(entry.type).c_str(),
		entry.urefs,
		entry.object,
		entry.tree ? " tree" : "");
}

[[nodiscard]] std::vector<std::string> CompareSnapshots(
		const MachSnapshot &was,
		const MachSnapshot &now) {
	auto result = std::vector<std::string>();
	for (const auto &entry : was.entries) {
		const auto found = FindEntry(now, entry.name);
		if (!found) {
			result.push_back("-" + EntryText(entry));
		} else if (found->type != entry.type
			|| found->urefs != entry.urefs
			|| found->object != entry.object) {
			result.push_back(
				"~" + EntryText(entry) + " -> " + EntryText(*found));
		}
	}
	for (const auto &entry : now.entries) {
		if (!FindEntry(was, entry.name)) {
			result.push_back("+" + EntryText(entry));
		}
	}
	return result;
}

[[nodiscard]] std::vector<ReferenceReading> LookUpReferences(
		const MachSnapshot &before) {
	auto result = std::vector<ReferenceReading>();
	for (const auto &reference : kReferences) {
		auto reading = ReferenceReading{
			.service = reference.service,
			.libSystem = reference.libSystem,
		};
		auto port = mach_port_t(MACH_PORT_NULL);
		const auto release = gsl::finally([&] {
			if (MACH_PORT_VALID(port)) {
				mach_port_deallocate(mach_task_self(), port);
			}
		});
		reading.lookup = bootstrap_look_up(
			bootstrap_port,
			reference.service,
			&port);
		if (reading.lookup == KERN_SUCCESS && MACH_PORT_VALID(port)) {
			reading.name = port;
			reading.held = (FindEntry(before, port) != nullptr);
			const auto snapshot = SnapshotMach();
			if (!snapshot.error.empty()) {
				reading.error = snapshot.error;
			} else if (const auto entry = FindEntry(snapshot, port)) {
				reading.object = entry->object;
				reading.found = true;
			} else {
				reading.error = "the returned name is not in the space";
			}
			const auto released = mach_port_deallocate(
				mach_task_self(),
				std::exchange(port, MACH_PORT_NULL));
			if (released != KERN_SUCCESS) {
				reading.error += (reading.error.empty() ? "" : "; ")
					+ ("mach_port_deallocate failed: " + KernText(released));
			}
		} else if (reading.lookup == KERN_SUCCESS) {
			reading.error = "the lookup returned no valid name";
		}
		result.push_back(std::move(reading));
	}
	return result;
}

[[nodiscard]] FirehoseReading ReadFirehosePorts(mach_port_name_t logd) {
	// The header is one chunk; the metadata returned ends it.
	constexpr auto kChunk = size_t(4096);
	constexpr auto kWords = kChunk / sizeof(mach_port_name_t);
	auto result = FirehoseReading();
	auto length = size_t();
	const auto metadata = voucher_activity_get_metadata_buffer(&length);
	const auto begin = mach_vm_address_t(reinterpret_cast<uintptr_t>(metadata));
	const auto end = begin + length;
	if (!metadata
		|| !length
		|| length >= kChunk
		|| end < begin
		|| (end % kChunk)) {
		result.error = Format(
			"no firehose header (metadata %p, %zu bytes)",
			metadata,
			length);
		return result;
	}
	const auto start = end - kChunk;
	const auto range = Format(
		"firehose header 0x%llx..0x%llx",
		(unsigned long long)start,
		(unsigned long long)end);
	auto address = mach_vm_address_t(start);
	auto size = mach_vm_size_t();
	auto info = vm_region_basic_info_data_64_t();
	auto count = mach_msg_type_number_t(VM_REGION_BASIC_INFO_COUNT_64);
	auto object = mach_port_t(MACH_PORT_NULL);
	const auto release = gsl::finally([&] {
		if (MACH_PORT_VALID(object)) {
			mach_port_deallocate(mach_task_self(), object);
		}
	});
	const auto kr = mach_vm_region(
		mach_task_self(),
		&address,
		&size,
		VM_REGION_BASIC_INFO_64,
		vm_region_info_t(&info),
		&count,
		&object);
	if (kr != KERN_SUCCESS) {
		result.error = range + " not readable: " + KernText(kr);
		return result;
	} else if (address > start
		|| size < end - address
		|| !(info.protection & VM_PROT_READ)) {
		result.error = range + Format(
			" not readable: region 0x%llx size 0x%llx protection 0x%x",
			(unsigned long long)address,
			(unsigned long long)size,
			unsigned(info.protection));
		return result;
	}

	// logd's port, the two push ports, then the buffer's own port.
	auto words = std::array<mach_port_name_t, kWords>();
	const auto limit = (kChunk - length) / sizeof(mach_port_name_t);
	std::memcpy(
		words.data(),
		reinterpret_cast<const void*>(uintptr_t(start)),
		limit * sizeof(mach_port_name_t));
	auto offset = size_t();
	auto next = mach_port_name_t(MACH_PORT_NULL);
	for (auto i = size_t(); i + 3 < limit; ++i) {
		if (words[i] != logd
			|| !MACH_PORT_VALID(words[i + 1])
			|| !MACH_PORT_VALID(words[i + 2])) {
			continue;
		} else if (!result.matches) {
			result.push = { words[i + 1], words[i + 2] };
			next = words[i + 3];
			offset = i * sizeof(mach_port_name_t);
		}
		++result.matches;
	}
	result.text = range + Format(
		" (metadata %zu bytes at its end), logd 0x%x found %d time(s)",
		length,
		logd,
		result.matches);
	if (result.matches) {
		result.text += Format(
			", first at offset %zu: push 0x%x 0x%x, next word 0x%x",
			offset,
			result.push[0],
			result.push[1],
			next);
	}
	return result;
}

[[nodiscard]] std::string DescriptorPath(int fd) {
	char path[MAXPATHLEN] = { 0 };
	return (fcntl(fd, F_GETPATH, path) == 0)
		? std::string(path)
		: std::string();
}

[[nodiscard]] std::string FileIdentity(pid_t pid, int fd) {
	auto vnode = vnode_fdinfowithpath();
	if (proc_pidfdinfo(
			pid,
			fd,
			PROC_PIDFDVNODEPATHINFO,
			&vnode,
			sizeof(vnode)) == int(sizeof(vnode))) {
		return Format(
			"vnode dev=0x%x ino=%llu",
			unsigned(vnode.pvip.vip_vi.vi_stat.vst_dev),
			(unsigned long long)vnode.pvip.vip_vi.vi_stat.vst_ino);
	}
	const auto vnodeError = errno;
	auto pipe = pipe_fdinfo();
	if (proc_pidfdinfo(
			pid,
			fd,
			PROC_PIDFDPIPEINFO,
			&pipe,
			sizeof(pipe)) == int(sizeof(pipe))) {
		return Format(
			"pipe handle=0x%llx",
			(unsigned long long)pipe.pipeinfo.pipe_handle);
	}
	auto socket = socket_fdinfo();
	if (proc_pidfdinfo(
			pid,
			fd,
			PROC_PIDFDSOCKETINFO,
			&socket,
			sizeof(socket)) == int(sizeof(socket))) {
		return Format(
			"socket pcb=0x%llx",
			(unsigned long long)socket.psi.soi_pcb);
	}
	return "unreadable: " + ErrnoText(vnodeError);
}

[[nodiscard]] const char *DescriptorKindName(int kind) {
	switch (kind) {
	case PROX_FDTYPE_ATALK: return "atalk";
	case PROX_FDTYPE_VNODE: return "vnode";
	case PROX_FDTYPE_SOCKET: return "socket";
	case PROX_FDTYPE_PSHM: return "posix-shm";
	case PROX_FDTYPE_PSEM: return "posix-semaphore";
	case PROX_FDTYPE_KQUEUE: return "kqueue";
	case PROX_FDTYPE_PIPE: return "pipe";
	case PROX_FDTYPE_FSEVENTS: return "fsevents";
	case PROX_FDTYPE_NETPOLICY: return "netpolicy";
	case PROX_FDTYPE_CHANNEL: return "channel";
	case PROX_FDTYPE_NEXUS: return "nexus";
	}
	return "other";
}

[[nodiscard]] std::string AccessText(int statusFlags) {
	switch (statusFlags & O_ACCMODE) {
	case O_RDONLY: return "read";
	case O_WRONLY: return "write";
	case O_RDWR: return "read-write";
	}
	return "access?";
}

void IdentifySocket(Descriptor &descriptor) {
	auto info = socket_fdinfo();
	if (proc_pidfdinfo(
			getpid(),
			descriptor.fd,
			PROC_PIDFDSOCKETINFO,
			&info,
			sizeof(info)) != int(sizeof(info))) {
		descriptor.failures.push_back(
			"PROC_PIDFDSOCKETINFO " + ErrnoText(errno));
		return;
	}
	descriptor.domain = info.psi.soi_family;
	descriptor.type = info.psi.soi_type;
	auto text = Format(
		"domain=%d type=%d protocol=%d kind=%d",
		info.psi.soi_family,
		info.psi.soi_type,
		info.psi.soi_protocol,
		info.psi.soi_kind);
	if (info.psi.soi_family == AF_UNIX) {
		auto pid = pid_t();
		auto size = socklen_t(sizeof(pid));
		if (getsockopt(
				descriptor.fd,
				SOL_LOCAL,
				LOCAL_PEERPID,
				&pid,
				&size) == 0) {
			descriptor.peerPid = pid;
			text += Format(" peer-pid=%d", int(pid));
		} else {
			descriptor.failures.push_back(
				"LOCAL_PEERPID " + ErrnoText(errno));
		}
		text += Format(
			" peer-pcb=0x%llx",
			(unsigned long long)info.psi.soi_proto.pri_un.unsi_conn_pcb);
	} else if (info.psi.soi_family == AF_INET) {
		const auto &in = info.psi.soi_proto.pri_tcp.tcpsi_ini;
		text += Format(
			" local-port=%d remote-port=%d tcp-state=%d",
			int(ntohs(uint16_t(in.insi_lport))),
			int(ntohs(uint16_t(in.insi_fport))),
			info.psi.soi_proto.pri_tcp.tcpsi_state);
	}
	descriptor.identity += " " + text;
}

[[nodiscard]] Descriptor Identify(const proc_fdinfo &info) {
	auto result = Descriptor{
		.fd = info.proc_fd,
		.kind = int(info.proc_fdtype),
	};
	result.kindName = DescriptorKindName(result.kind);
	result.fdFlags = fcntl(result.fd, F_GETFD);
	if (result.fdFlags < 0) {
		result.failures.push_back("F_GETFD " + ErrnoText(errno));
	}
	result.statusFlags = fcntl(result.fd, F_GETFL);
	if (result.statusFlags < 0) {
		result.failures.push_back("F_GETFL " + ErrnoText(errno));
	}
	struct stat st = {};
	const auto statError = (fstat(result.fd, &st) == 0) ? 0 : errno;
	if (!statError) {
		result.statIdentity = Format(
			"vnode dev=0x%x ino=%llu",
			unsigned(st.st_dev),
			(unsigned long long)st.st_ino);
	} else if (result.kind != PROX_FDTYPE_VNODE) {
		result.failures.push_back("fstat " + ErrnoText(statError));
	}
	result.identity = Format(
		"%s %s access=%s%s%s",
		result.kindName.c_str(),
		statError
			? ("fstat=" + ErrnoCode(statError)).c_str()
			: Format(
				"mode=0%o dev=0x%x ino=%llu rdev=0x%x",
				unsigned(st.st_mode),
				unsigned(st.st_dev),
				(unsigned long long)st.st_ino,
				unsigned(st.st_rdev)).c_str(),
		(result.statusFlags < 0) ? "?" : AccessText(result.statusFlags).c_str(),
		(result.statusFlags > 0 && (result.statusFlags & O_NONBLOCK))
			? " nonblocking"
			: "",
		(result.fdFlags > 0 && (result.fdFlags & FD_CLOEXEC))
			? " cloexec"
			: "");
	if (result.kind == PROX_FDTYPE_VNODE) {
		char path[MAXPATHLEN] = { 0 };
		if (fcntl(result.fd, F_GETPATH, path) == 0) {
			result.path = path;
			result.identity += " path=" + result.path;
		} else {
			result.failures.push_back("F_GETPATH " + ErrnoText(errno));
		}
	} else if (result.kind == PROX_FDTYPE_SOCKET) {
		IdentifySocket(result);
	}
	return result;
}

[[nodiscard]] Descriptors ListDescriptors() {
	auto result = Descriptors();
	auto bytes = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, nullptr, 0);
	if (bytes <= 0) {
		result.error = "PROC_PIDLISTFDS size failed: " + ErrnoText(errno);
		return result;
	}
	auto buffer = std::vector<proc_fdinfo>();
	for (auto tries = 1; tries <= kListTries; ++tries) {
		result.tries = tries;
		buffer.resize(size_t(bytes) / sizeof(proc_fdinfo) + 16);
		const auto capacity = int(buffer.size() * sizeof(proc_fdinfo));
		const auto filled = proc_pidinfo(
			getpid(),
			PROC_PIDLISTFDS,
			0,
			buffer.data(),
			capacity);
		if (filled <= 0) {
			result.error = "PROC_PIDLISTFDS failed: " + ErrnoText(errno);
			return result;
		} else if (filled % int(sizeof(proc_fdinfo))) {
			result.error = Format(
				"PROC_PIDLISTFDS returned %d bytes, not whole entries",
				filled);
			return result;
		} else if (filled < capacity) {
			const auto count = size_t(filled) / sizeof(proc_fdinfo);
			for (auto i = size_t(); i != count; ++i) {
				result.list.push_back(Identify(buffer[i]));
			}
			return result;
		}
		bytes = filled * 2;
	}
	result.error = Format(
		"PROC_PIDLISTFDS filled its buffer %d times",
		kListTries);
	return result;
}

[[nodiscard]] std::string ReapWithin(pid_t pid) {
	const auto wait = [&](std::chrono::milliseconds timeout) {
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while (true) {
			auto status = 0;
			const auto result = waitpid(pid, &status, WNOHANG);
			if (result == pid) {
				return WIFEXITED(status)
					? Format("reaped, exit %d", WEXITSTATUS(status))
					: WIFSIGNALED(status)
					? Format("reaped, signal %d", WTERMSIG(status))
					: Format("reaped, status %d", status);
			} else if (result < 0 && errno != EINTR) {
				return "waitpid failed: " + ErrnoText(errno);
			} else if (std::chrono::steady_clock::now() >= deadline) {
				return std::string();
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	};
	auto result = wait(kChildWait);
	if (result.empty()) {
		kill(pid, SIGKILL);
		result = wait(kKillWait);
		result = result.empty()
			? std::string("NOT reaped after SIGKILL")
			: (result + " after SIGKILL");
	}
	return result;
}

[[nodiscard]] int SandboxCheck(const char *operation) {
	return sandbox_check(getpid(), operation, kSandboxFilterNone);
}

[[nodiscard]] int SandboxCheckPath(const std::string &path) {
	return sandbox_check(
		getpid(),
		"file-read-data",
		kSandboxFilterPath,
		path.c_str());
}

[[nodiscard]] std::string PolicyCode(int result) {
	return (result == 1)
		? "denied"
		: (result == 0)
		? "allowed"
		: Format("check%d", result);
}

class Probe final {
public:
	Probe(Arguments arguments, Fn<QString()> lockdown);

	[[nodiscard]] std::string run();

private:
	void begin();
	void preopen();
	void reportPreLockdown();
	void lockDown();
	void readFirehose();
	[[nodiscard]] std::string firehoseRefusal(mach_port_name_t name) const;
	void listDescriptors();
	void listMach();
	void classifyEntry(
		const MachEntry &entry,
		const std::vector<mach_port_name_t> &threads,
		mach_port_name_t host,
		mach_port_name_t taskName);
	[[nodiscard]] std::string receiveStatus(mach_port_name_t name);
	[[nodiscard]] std::string portSetMembers(mach_port_name_t name);

	void attempt(
		const std::string &name,
		std::string code,
		const std::string &detail);
	void attemptErrno(
		const std::string &name,
		int error,
		const std::string &detail);
	void attemptKern(
		const std::string &name,
		kern_return_t result,
		const std::string &detail);
	void incomplete(const std::string &name, const std::string &why);
	void attemptLookup(const std::string &name, const char *service);
	void attemptConnect(const std::string &name, int fd);

	void attemptProcesses();
	void attemptNetwork();
	void attemptConnectControl();
	void attemptFiles();
	void attemptFile(const std::string &label, const std::string &path);
	void attemptParent();
	void attemptServices();
	void attemptKeychain();
	void attemptCamera();
	void attemptMicrophone();
	void attemptIOKit();
	void attemptWindowServer();
	void attemptThread();
	void attemptAfterClose();

	[[nodiscard]] std::string summary() const;

	const Arguments _arguments;
	const Fn<QString()> _lockdown;
	const pid_t _pid = 0;
	const pid_t _ppid = 0;
	const bool _locking = false;
	Report _report;

	MachSnapshot _pre;
	MachSnapshot _referenced;
	MachSnapshot _post;
	std::vector<ReferenceReading> _references;
	std::vector<std::string> _referenceChanges;
	std::vector<std::string> _lockdownChanges;
	std::vector<mach_port_name_t> _firehoseNames;
	std::string _firehoseRow;
	std::string _firehoseFailure;
	Descriptors _descriptors;
	std::string _parentStderr;
	std::string _ownStderr;
	std::string _ownStderrPath;
	std::string _lockdownResult;
	std::string _state;
	std::vector<std::string> _defects;
	std::vector<std::string> _unaccounted;
	std::vector<std::string> _failures;
	std::vector<std::string> _fdLabels;
	std::vector<Attempt> _attempts;
	Counts _counts;
	OwnedFd _preopened;
	int _preopenError = 0;
	int _incomplete = 0;

};

Probe::Probe(Arguments arguments, Fn<QString()> lockdown)
: _arguments(std::move(arguments))
, _lockdown(std::move(lockdown))
, _pid(getpid())
, _ppid(getppid())
, _locking(_arguments.mode == ProbeMode().toStdString())
, _report(_pid, _arguments.mode) {
}

std::string Probe::run() {
	begin();
	preopen();
	_pre = SnapshotMach();
	_references = LookUpReferences(_pre);
	_referenced = SnapshotMach();
	if (_pre.error.empty() && _referenced.error.empty()) {
		_referenceChanges = CompareSnapshots(_pre, _referenced);
	}
	_parentStderr = FileIdentity(_ppid, STDERR_FILENO);
	_ownStderr = FileIdentity(_pid, STDERR_FILENO);
	_ownStderrPath = DescriptorPath(STDERR_FILENO);
	reportPreLockdown();
	lockDown();
	_post = SnapshotMach();
	readFirehose();
	if (_referenced.error.empty() && _post.error.empty()) {
		_lockdownChanges = CompareSnapshots(_referenced, _post);
	}
	_descriptors = ListDescriptors();

	listDescriptors();
	listMach();

	attemptProcesses();
	attemptNetwork();
	attemptConnectControl();
	attemptFiles();
	attemptParent();
	attemptServices();
	attemptKeychain();
	attemptCamera();
	attemptMicrophone();
	attemptIOKit();
	attemptWindowServer();
	attemptThread();
	attemptAfterClose();

	auto result = summary();
	_report.row(Format(
		"end: rows=%d fds=%d mach=%d attempts=%d cut-rows=%d stderr=%s",
		_report.rows(),
		int(_descriptors.list.size()),
		int(_post.entries.size()),
		int(_attempts.size()),
		_report.cut(),
		_report.writeError() ? "FAILED" : "ok"));
	return result;
}

void Probe::begin() {
	_report.row(Format(
		"begin: pid=%d ppid=%d mode=%s keychain-account=%s loopback-port=%d"
		" connect-control=%s malformed=%d",
		int(_pid),
		int(_ppid),
		_arguments.mode.c_str(),
		_arguments.keychainAccount.empty() ? "absent" : "given",
		_arguments.loopbackPort,
		_arguments.connectControl ? "yes" : "no",
		int(_arguments.malformed.size())));
	for (const auto &malformed : _arguments.malformed) {
		_report.row("malformed argument: " + malformed);
		_defects.push_back("malformed argument: " + malformed);
	}
	const auto shown = [](const std::string &path) {
		return path.empty() ? std::string("(absent)") : path;
	};
	_report.row("paths: cwd="
		+ shown(_arguments.cwd)
		+ " application-support="
		+ shown(_arguments.applicationSupport));
}

void Probe::preopen() {
	if (!_arguments.connectControl) {
		return;
	}
	const auto fd = socket(AF_INET, SOCK_STREAM, 0);
	_preopenError = (fd < 0) ? errno : 0;
	_preopened.reset(fd);
	_report.row((fd >= 0)
		? Format(
			"connect-control: preopened unconnected TCP socket fd %d before"
			" the lockdown; this run is never a clean inventory",
			fd)
		: ("connect-control: preopening the TCP socket failed: "
			+ ErrnoText(_preopenError)));
}

void Probe::reportPreLockdown() {
	_report.row(Format(
		"fd 2 before the lockdown: parent's %s, own %s path=%s",
		_parentStderr.c_str(),
		_ownStderr.c_str(),
		_ownStderrPath.empty() ? "(none)" : _ownStderrPath.c_str()));
	if (!_pre.error.empty()) {
		_report.row("pre-lockdown snapshot FAILED: " + _pre.error);
		_failures.push_back("pre-lockdown snapshot: " + _pre.error);
	} else {
		_report.row(Format(
			"pre-lockdown snapshot: %d entries (table %u, tree %u)",
			int(_pre.entries.size()),
			_pre.tableSize,
			_pre.treeSize));
		for (const auto &entry : _pre.entries) {
			_report.row("pre " + EntryText(entry));
		}
	}
	for (const auto &reading : _references) {
		_report.row(Format(
			"reference %s: %s%s",
			reading.service.c_str(),
			(reading.lookup == KERN_SUCCESS)
				? Format(
					"name=0x%x object=0x%x held-before=%s",
					reading.name,
					reading.object,
					reading.held ? "yes" : "no").c_str()
				: ("lookup " + KernText(reading.lookup)).c_str(),
			reading.error.empty() ? "" : (" ERROR " + reading.error).c_str()));
		if (!reading.error.empty()) {
			_failures.push_back(
				"reference " + reading.service + ": " + reading.error);
		}
	}
	if (!_referenced.error.empty()) {
		_failures.push_back("post-reference snapshot: " + _referenced.error);
	} else if (!_referenceChanges.empty()) {
		for (const auto &change : _referenceChanges) {
			_report.row("reference lookups changed the space: " + change);
		}
		_failures.push_back(Format(
			"reference lookups changed the space (%d entries)",
			int(_referenceChanges.size())));
	} else if (_pre.error.empty()) {
		_report.row(Format(
			"reference lookups left the space as it was: %d entries equal"
			" in name, rights, urefs and object",
			int(_referenced.entries.size())));
	}
}

void Probe::lockDown() {
	if (_locking) {
		_lockdownResult = _lockdown
			? _lockdown().toStdString()
			: std::string("no lockdown callable");
		_report.row(_lockdownResult.empty()
			? std::string("lockdown: ok")
			: ("lockdown: FAILED " + _lockdownResult));
		if (!_lockdownResult.empty()) {
			_defects.push_back("lockdown failed: " + _lockdownResult);
		}
	} else {
		_report.row("lockdown: skipped (unsandboxed control)");
	}
	auto limit = rlimit();
	const auto limitRead = getrlimit(RLIMIT_NOFILE, &limit);
	_state = Format(
		"sandboxed=%d nofile=%s",
		sandbox_check(_pid, nullptr, kSandboxFilterNone),
		(limitRead == 0)
			? Format(
				"%llu/%llu",
				(unsigned long long)limit.rlim_cur,
				(unsigned long long)limit.rlim_max).c_str()
			: ("?" + ErrnoCode(errno)).c_str());
	_report.row("state: " + _state);
}

void Probe::readFirehose() {
	if (!_post.error.empty()) {
		return;
	}
	const auto refuse = [&](
			const std::string &reason,
			const std::string &text) {
		_firehoseFailure = reason;
		_firehoseRow = "firehose: FAILED "
			+ reason
			+ "; no right is classified by it"
			+ (text.empty() ? std::string() : (" (reading: " + text + ")"));
	};
	const auto reference = [&]() -> const ReferenceReading* {
		for (const auto &reading : _references) {
			if (reading.found && reading.service == "com.apple.logd") {
				return &reading;
			}
		}
		return nullptr;
	}();
	if (!reference) {
		refuse(
			"no logd name: the com.apple.logd reference found no port",
			std::string());
		return;
	}
	auto logd = mach_port_name_t(MACH_PORT_NULL);
	for (const auto &entry : _post.entries) {
		if (entry.object == reference->object
			&& (entry.type & MACH_PORT_TYPE_SEND)) {
			logd = entry.name;
			break;
		}
	}
	if (!MACH_PORT_VALID(logd)) {
		refuse(
			Format(
				"no logd name: the listing holds no send right to"
				" com.apple.logd's port (object 0x%x)",
				reference->object),
			std::string());
		return;
	}

	// Read right after the listing's snapshot, then confirm it at once.
	const auto reading = ReadFirehosePorts(logd);
	const auto after = SnapshotMach();
	const auto changes = after.error.empty()
		? CompareSnapshots(_post, after)
		: std::vector<std::string>();
	const auto [first, second] = reading.push;
	auto refusal = std::string();
	if (!reading.error.empty()) {
		refusal = reading.error;
	} else if (reading.matches != 1) {
		refusal = Format(
			"logd 0x%x found %d times in the firehose header",
			logd,
			reading.matches);
	} else if (!after.error.empty()) {
		refusal = "the space after the reading: " + after.error;
	} else if (!changes.empty()) {
		refusal = Format(
			"the space changed across the reading (%d entries, first %s)",
			int(changes.size()),
			changes.front().c_str());
	} else if (first == second || first == logd || second == logd) {
		refusal = Format(
			"push names 0x%x 0x%x are not two names other than logd 0x%x",
			first,
			second,
			logd);
	} else {
		refusal = firehoseRefusal(first);
		if (refusal.empty()) {
			refusal = firehoseRefusal(second);
		}
	}
	if (!refusal.empty()) {
		refuse(refusal, reading.text);
		return;
	}
	_firehoseNames = { first, second };
	_firehoseRow = Format(
		"firehose: libtrace push ports 0x%x 0x%x accepted, read in this"
		" process after the listing's snapshot: ",
		first,
		second)
		+ reading.text
		+ Format(
			"; the space after the reading equals that snapshot: %d entries"
			" equal in name, rights, urefs and object",
			int(after.entries.size()));
}

std::string Probe::firehoseRefusal(mach_port_name_t name) const {
	const auto entry = FindEntry(_post, name);
	if (!entry) {
		return Format("0x%x is not in the listing", name);
	} else if ((entry->type & MACH_PORT_TYPE_ALL_RIGHTS)
		!= MACH_PORT_TYPE_SEND) {
		return Format(
			"0x%x holds %s, not a plain send right",
			name,
			RightsText(entry->type).c_str());
	} else if (name == mach_task_self()) {
		return Format("0x%x is mach_task_self()", name);
	} else if (name == bootstrap_port) {
		return Format("0x%x is the bootstrap port", name);
	}
	auto type = ipc_info_object_type_t(IPC_OTYPE_NONE);
	auto address = mach_vm_address_t();
	const auto kernel = mach_port_kobject(
		mach_task_self(),
		name,
		&type,
		&address);
	if (kernel != KERN_SUCCESS) {
		return Format("0x%x kernel object query failed: ", name)
			+ KernText(kernel);
	} else if (type != IPC_OTYPE_NONE && type != IPC_OTYPE_UNKNOWN) {
		return Format(
			"0x%x is a kernel object %d/%s",
			name,
			int(type),
			KernelTypeName(type));
	}
	for (const auto &reading : _references) {
		if (reading.found && reading.object == entry->object) {
			return Format(
				"0x%x is the right to %s",
				name,
				reading.service.c_str());
		}
	}
	return std::string();
}

void Probe::listDescriptors() {
	if (!_descriptors.error.empty()) {
		_report.row("descriptors: listing FAILED: " + _descriptors.error);
		_failures.push_back("descriptor listing: " + _descriptors.error);
		return;
	}
	_report.row(Format(
		"descriptors: %d (PROC_PIDLISTFDS, %d tries)",
		int(_descriptors.list.size()),
		_descriptors.tries));
	for (const auto &descriptor : _descriptors.list) {
		auto label = std::string();
		auto brief = std::string();
		auto defect = std::string();
		const auto vnode = (descriptor.kind == PROX_FDTYPE_VNODE);
		const auto access = descriptor.statusFlags & O_ACCMODE;
		if (!descriptor.failures.empty()) {
			auto joined = std::string();
			for (const auto &failure : descriptor.failures) {
				joined += (joined.empty() ? "" : "; ") + failure;
			}
			_failures.push_back(
				Format("fd %d identity: ", descriptor.fd) + joined);
			label = "IDENTITY QUERY FAILED: " + joined;
			brief = "UNIDENTIFIED";
		} else if (descriptor.fd == _preopened.get()) {
			label = "connect-control fixture socket"
				" (injected before the lockdown)";
			brief = "connect-control-fixture";
		} else if (descriptor.fd == STDIN_FILENO) {
			if (vnode && descriptor.path == "/dev/null" && access == O_RDONLY) {
				label = "stdin /dev/null";
				brief = "stdin";
			} else {
				defect = "fd 0 is not /dev/null opened for reading";
			}
		} else if (descriptor.fd == STDOUT_FILENO) {
			if (vnode && descriptor.path == "/dev/null" && access == O_WRONLY) {
				label = "stdout /dev/null";
				brief = "stdout";
			} else {
				defect = "fd 1 is not /dev/null opened for writing";
			}
		} else if (descriptor.fd == STDERR_FILENO) {
			const auto unchanged = vnode
				? (!_ownStderrPath.empty()
					&& descriptor.path == _ownStderrPath
					&& (descriptor.statIdentity.empty()
						|| descriptor.statIdentity == _ownStderr))
				: (FileIdentity(_pid, STDERR_FILENO) == _ownStderr);
			if (_parentStderr.rfind("unreadable", 0) == 0
				|| _ownStderr.rfind("unreadable", 0) == 0) {
				label = "stderr, UNVERIFIED: the parent's fd 2 was "
					+ _parentStderr + ", own " + _ownStderr;
				brief = "stderr-unverified";
				_failures.push_back("fd 2 not compared: parent's "
					+ _parentStderr + ", own " + _ownStderr);
			} else if (_ownStderr != _parentStderr) {
				defect = "fd 2 (" + _ownStderr + ") is not the parent's fd 2 ("
					+ _parentStderr + ")";
			} else if (!unchanged) {
				defect = "fd 2 differs from the one compared"
					" before the lockdown";
			} else {
				label = "stderr, the parent's fd 2 (" + _ownStderr
					+ ", compared before the lockdown, same "
					+ (vnode ? "path" : "object") + " after)";
				brief = "stderr";
			}
		} else if (descriptor.fd == 3) {
			if (descriptor.kind == PROX_FDTYPE_SOCKET
				&& descriptor.domain == AF_UNIX
				&& descriptor.type == SOCK_STREAM
				&& descriptor.peerPid == _ppid) {
				label = "channel, AF_UNIX stream to the parent";
				brief = "channel";
			} else {
				defect = "fd 3 is not an AF_UNIX stream to the parent";
			}
		} else {
			defect = Format("unexpected fd %d", descriptor.fd);
		}
		if (!defect.empty()) {
			_defects.push_back(defect + " [" + descriptor.identity + "]");
			label = "DEFECT: " + defect;
			brief = "DEFECT";
		}
		_fdLabels.push_back(Format("%d ", descriptor.fd) + brief);
		_report.row(Format(
			"fd %d %s class=%s",
			descriptor.fd,
			descriptor.identity.c_str(),
			label.c_str()));
	}
}

std::string Probe::receiveStatus(mach_port_name_t name) {
	auto status = mach_port_status_t();
	auto count = mach_msg_type_number_t(MACH_PORT_RECEIVE_STATUS_COUNT);
	const auto kr = mach_port_get_attributes(
		mach_task_self(),
		name,
		MACH_PORT_RECEIVE_STATUS,
		mach_port_info_t(&status),
		&count);
	if (kr != KERN_SUCCESS) {
		_failures.push_back(
			Format("0x%x receive status: ", name) + KernText(kr));
		return " receive-status FAILED " + KernText(kr);
	}
	return Format(
		" port-sets=%u msgcount=%u qlimit=%u srights=%u sorights=%u"
		" mscount=%u seqno=%u flags=0x%x",
		status.mps_pset,
		status.mps_msgcount,
		status.mps_qlimit,
		unsigned(status.mps_srights),
		status.mps_sorights,
		status.mps_mscount,
		status.mps_seqno,
		status.mps_flags);
}

std::string Probe::portSetMembers(mach_port_name_t name) {
	auto members = mach_port_name_array_t();
	auto count = mach_msg_type_number_t();
	const auto kr = mach_port_get_set_status(
		mach_task_self(),
		name,
		&members,
		&count);
	if (kr != KERN_SUCCESS) {
		_failures.push_back(
			Format("0x%x port set members: ", name) + KernText(kr));
		return " members FAILED " + KernText(kr);
	}
	const auto release = gsl::finally([&] {
		if (members) {
			vm_deallocate(
				mach_task_self(),
				vm_address_t(members),
				count * sizeof(*members));
		}
	});
	auto result = Format(" members=%u [", count);
	for (auto i = mach_msg_type_number_t(); i != count; ++i) {
		result += Format("%s0x%x", i ? " " : "", members[i]);
	}
	return result + "]";
}

void Probe::classifyEntry(
		const MachEntry &entry,
		const std::vector<mach_port_name_t> &threads,
		mach_port_name_t host,
		mach_port_name_t taskName) {
	auto type = ipc_info_object_type_t(IPC_OTYPE_NONE);
	auto address = mach_vm_address_t();
	const auto ports = MACH_PORT_TYPE_SEND
		| MACH_PORT_TYPE_RECEIVE
		| MACH_PORT_TYPE_SEND_ONCE;
	const auto kernel = mach_port_kobject(
		mach_task_self(),
		entry.name,
		&type,
		&address);
	const auto kernelText = (kernel == KERN_SUCCESS)
		? Format("%d/%s", int(type), KernelTypeName(type))
		: ("none (" + KernText(kernel) + ")");
	const auto isKernel = (kernel == KERN_SUCCESS)
		&& (type != IPC_OTYPE_NONE)
		&& (type != IPC_OTYPE_UNKNOWN);
	auto label = std::string();
	auto defect = std::string();
	auto unaccounted = std::string();
	auto detail = std::string();
	const auto send = (entry.type & MACH_PORT_TYPE_SEND) != 0;
	const auto receive = (entry.type & MACH_PORT_TYPE_RECEIVE) != 0;
	const auto sendOnce = (entry.type & MACH_PORT_TYPE_SEND_ONCE) != 0;
	const auto set = (entry.type & MACH_PORT_TYPE_PORT_SET) != 0;
	const auto dead = (entry.type & MACH_PORT_TYPE_DEAD_NAME) != 0;
	const auto service = [&]() -> const ReferenceReading* {
		for (const auto &reading : _references) {
			if (reading.found && reading.object == entry.object) {
				return &reading;
			}
		}
		return nullptr;
	}();
	if (kernel != KERN_SUCCESS && (entry.type & ports)) {
		_failures.push_back(Format("0x%x kernel object query: ", entry.name)
			+ KernText(kernel));
		label = "IDENTITY QUERY FAILED";
	} else if (entry.name == mach_task_self()) {
		if (type == IPC_OTYPE_TASK_CONTROL) {
			label = "task (mach_task_self)";
			++_counts.task;
		} else {
			defect = "mach_task_self() names a " + kernelText + " object";
		}
	} else if (type == IPC_OTYPE_THREAD_CONTROL) {
		if (ranges::contains(threads, entry.name)) {
			label = "thread (task_threads)";
			++_counts.thread;
		} else {
			defect = "a thread port of no thread of this task";
		}
	} else if (type == IPC_OTYPE_HOST) {
		if (entry.name == host) {
			label = "host, unprivileged (mach_host_self)";
			++_counts.host;
		} else {
			defect = "a HOST port other than mach_host_self()";
		}
	} else if (type == IPC_OTYPE_SEMAPHORE) {
		label = "libSystem kernel object: semaphore";
		++_counts.semaphore;
	} else if (type == IPC_OTYPE_CLOCK) {
		label = "libSystem kernel object: clock";
		++_counts.clock;
	} else if (type == IPC_OTYPE_VOUCHER) {
		label = "libSystem kernel object: voucher";
		++_counts.voucher;
	} else if (type == IPC_OTYPE_TASK_NAME) {
		if (entry.name == taskName) {
			label = "own task name port (TASK_NAME_PORT)";
			++_counts.ownName;
		} else {
			defect = "a TASK_NAME port of another task";
		}
	} else if (isKernel) {
		defect = "kernel object " + kernelText;
	} else if (entry.name == bootstrap_port) {
		label = "bootstrap (bootstrap_port)";
		++_counts.bootstrap;
	} else if (receive) {
		label = send ? "own send-receive" : "own receive";
		++(send ? _counts.sendReceive : _counts.receive);
		if (sendOnce || set || dead) {
			defect = "a receive right with other rights";
		}
		detail = receiveStatus(entry.name);
	} else if (set) {
		label = "own port set";
		++_counts.portSet;
		detail = portSetMembers(entry.name);
	} else if (dead) {
		label = "dead name (no object)";
		++_counts.deadName;
	} else if (service) {
		if (service->libSystem) {
			label = "libSystem service " + service->service;
			++_counts.libSystem;
		} else {
			defect = "service " + service->service;
		}
	} else if (ranges::contains(_firehoseNames, entry.name)) {
		label = "libSystem firehose push port";
		++_counts.firehose;
	} else if (send || sendOnce) {
		unaccounted = "a send right to no reference service";
	} else {
		unaccounted = "no rights";
	}
	const auto row = Format(
		"mach 0x%x type=0x%08x rights=%s urefs=%u object=0x%x kernel=%s%s",
		entry.name,
		entry.type,
		RightsText(entry.type).c_str(),
		entry.urefs,
		entry.object,
		kernelText.c_str(),
		entry.tree ? " tree-entry" : "");
	const auto brief = Format(
		"0x%x %s urefs=%u object=0x%x kernel=%s",
		entry.name,
		ShortRights(entry.type).c_str(),
		entry.urefs,
		entry.object,
		kernelText.c_str());
	if (!defect.empty()) {
		_defects.push_back(brief + ": " + defect);
		_report.row(row + detail + " class=DEFECT: " + defect);
	} else if (!unaccounted.empty()) {
		_unaccounted.push_back(brief + " (" + unaccounted + ")");
		_report.row(row + detail + " class=UNACCOUNTED: " + unaccounted);
	} else {
		_report.row(row + detail + " class=" + label);
	}
}

void Probe::listMach() {
	if (!_post.error.empty()) {
		_report.row("mach: listing FAILED: " + _post.error);
		_failures.push_back("mach listing: " + _post.error);
		return;
	}
	_report.row(Format(
		"mach: %d entries (mach_port_space_info table %u, tree %u)",
		int(_post.entries.size()),
		_post.tableSize,
		_post.treeSize));
	if (!_lockdownChanges.empty()) {
		for (const auto &change : _lockdownChanges) {
			_report.row("changed between the reference snapshot and the"
				" listing: " + change);
		}
	}

	auto list = thread_act_array_t();
	auto count = mach_msg_type_number_t();
	auto host = mach_port_t(MACH_PORT_NULL);
	auto taskName = mach_port_t(MACH_PORT_NULL);
	const auto threadsRead = task_threads(mach_task_self(), &list, &count);
	const auto release = gsl::finally([&] {
		if (list) {
			for (auto i = mach_msg_type_number_t(); i != count; ++i) {
				mach_port_deallocate(mach_task_self(), list[i]);
			}
			vm_deallocate(
				mach_task_self(),
				vm_address_t(list),
				count * sizeof(*list));
		}
		if (MACH_PORT_VALID(host)) {
			mach_port_deallocate(mach_task_self(), host);
		}
		if (MACH_PORT_VALID(taskName)) {
			mach_port_deallocate(mach_task_self(), taskName);
		}
	});
	auto threads = std::vector<mach_port_name_t>();
	if (threadsRead == KERN_SUCCESS) {
		threads.assign(list, list + count);
	} else {
		_failures.push_back("task_threads: " + KernText(threadsRead));
	}
	const auto has = [&](ipc_info_object_type_t wanted) {
		for (const auto &entry : _post.entries) {
			auto type = ipc_info_object_type_t(IPC_OTYPE_NONE);
			auto address = mach_vm_address_t();
			if (mach_port_kobject(
					mach_task_self(),
					entry.name,
					&type,
					&address) == KERN_SUCCESS
				&& type == wanted) {
				return true;
			}
		}
		return false;
	};
	if (has(IPC_OTYPE_HOST)) {
		host = mach_host_self();
	}
	if (has(IPC_OTYPE_TASK_NAME)) {
		const auto kr = task_get_special_port(
			mach_task_self(),
			TASK_NAME_PORT,
			&taskName);
		if (kr != KERN_SUCCESS) {
			_failures.push_back("task_get_special_port(TASK_NAME_PORT): "
				+ KernText(kr));
		}
	}
	_report.row(Format(
		"mach identities: task=0x%x threads=%d host=0x%x task-name=0x%x"
		" bootstrap=0x%x",
		mach_task_self(),
		int(threads.size()),
		host,
		taskName,
		bootstrap_port));
	if (!_firehoseRow.empty()) {
		_report.row(_firehoseRow);
	}
	for (const auto &entry : _post.entries) {
		classifyEntry(entry, threads, host, taskName);
	}
}

void Probe::attempt(
		const std::string &name,
		std::string code,
		const std::string &detail) {
	_report.row("attempt " + name + ": " + code + " " + detail);
	_attempts.push_back({ name, std::move(code) });
}

void Probe::attemptErrno(
		const std::string &name,
		int error,
		const std::string &detail) {
	attempt(
		name,
		error ? ErrnoCode(error) : std::string("ok"),
		error ? (detail + ": " + ErrnoText(error)) : detail);
}

void Probe::attemptKern(
		const std::string &name,
		kern_return_t result,
		const std::string &detail) {
	attempt(name, KernCode(result), detail + " " + KernText(result));
}

void Probe::incomplete(const std::string &name, const std::string &why) {
	++_incomplete;
	attempt(name, "INCOMPLETE", why);
}

void Probe::attemptLookup(const std::string &name, const char *service) {
	auto port = mach_port_t(MACH_PORT_NULL);
	const auto result = bootstrap_look_up(bootstrap_port, service, &port);
	if (MACH_PORT_VALID(port)) {
		mach_port_deallocate(mach_task_self(), port);
	}
	attemptKern(name, result, Format("bootstrap_look_up(%s)", service));
}

void Probe::attemptConnect(const std::string &name, int fd) {
	const auto target = Format("127.0.0.1:%d", _arguments.loopbackPort);
	const auto flags = fcntl(fd, F_GETFL);
	if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
		attemptErrno(name, errno, "making the socket nonblocking");
		return;
	}
	auto on = 1;
	setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
	auto address = sockaddr_in();
	address.sin_len = sizeof(address);
	address.sin_family = AF_INET;
	address.sin_port = htons(uint16_t(_arguments.loopbackPort));
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	const auto connected = connect(
		fd,
		reinterpret_cast<const sockaddr*>(&address),
		sizeof(address));
	const auto error = connected ? errno : 0;
	if (connected && error != EINPROGRESS) {
		attemptErrno(
			name,
			error,
			Format("connect(fd %d, %s)", fd, target.c_str()));
		return;
	} else if (connected) {
		auto poller = pollfd{ .fd = fd, .events = POLLOUT };
		const auto polled = poll(&poller, 1, kConnectWaitMs);
		if (polled <= 0) {
			attempt(
				name,
				polled ? ErrnoCode(errno) : std::string("timeout"),
				Format(
					"connect(fd %d, %s): EINPROGRESS, then poll %s",
					fd,
					target.c_str(),
					polled ? ErrnoText(errno).c_str() : "timed out"));
			return;
		}
		auto pending = 0;
		auto size = socklen_t(sizeof(pending));
		const auto read = getsockopt(fd, SOL_SOCKET, SO_ERROR, &pending, &size);
		if (read != 0 || pending) {
			attemptErrno(
				name,
				read ? errno : pending,
				Format("connect(fd %d, %s) completion", fd, target.c_str()));
			return;
		}
	}
	const auto token = Format(
		"widget-helper-probe %d %s\n",
		int(_pid),
		_arguments.mode.c_str());
	const auto sent = send(fd, token.data(), token.size(), 0);
	attemptErrno(
		name,
		(sent == ssize_t(token.size())) ? 0 : (sent < 0) ? errno : EIO,
		Format(
			"connect(fd %d, %s) completed, token of %d bytes sent",
			fd,
			target.c_str(),
			int(token.size())));
}

void Probe::attemptProcesses() {
	const auto child = fork();
	const auto forkError = (child < 0) ? errno : 0;
	if (child == 0) {
		_exit(0);
	}
	const auto forkReaped = (child > 0) ? ReapWithin(child) : std::string();
	attemptErrno(
		"fork",
		forkError,
		(child > 0)
			? (Format("child %d ", int(child)) + forkReaped)
			: std::string("fork()"));

	auto attributes = posix_spawnattr_t();
	if (const auto error = posix_spawnattr_init(&attributes)) {
		attemptErrno("posix_spawn", error, "posix_spawnattr_init");
		return;
	}
	const auto destroyAttributes = gsl::finally([&] {
		posix_spawnattr_destroy(&attributes);
	});
	posix_spawnattr_setflags(&attributes, POSIX_SPAWN_CLOEXEC_DEFAULT);
	char *argv[] = { const_cast<char*>(kTrue), nullptr };
	char *envp[] = { nullptr };
	auto pid = pid_t();
	const auto spawned = posix_spawn(
		&pid,
		kTrue,
		nullptr,
		&attributes,
		argv,
		envp);
	const auto spawnReaped = spawned ? std::string() : ReapWithin(pid);
	attemptErrno(
		"posix_spawn",
		spawned,
		spawned
			? std::string(kTrue)
			: (Format("%s child %d ", kTrue, int(pid)) + spawnReaped));
}

void Probe::attemptNetwork() {
	auto fd = OwnedFd(socket(AF_INET, SOCK_STREAM, 0));
	const auto error = (fd.get() < 0) ? errno : 0;
	attemptErrno(
		"socket",
		error,
		Format("socket(AF_INET, SOCK_STREAM) = %d", fd.get()));
	if (fd.get() < 0) {
		incomplete("socket-connect", "no socket was created to connect");
	} else if (_arguments.loopbackPort <= 0) {
		incomplete("socket-connect", "no loopback port argument");
	} else {
		attemptConnect("socket-connect", fd.get());
	}
	fd.reset();
	const auto outbound = SandboxCheck("network-outbound");
	attempt(
		"policy network-outbound",
		PolicyCode(outbound),
		Format("sandbox_check = %d", outbound));
}

void Probe::attemptConnectControl() {
	if (!_arguments.connectControl) {
		return;
	} else if (_preopened.get() < 0) {
		incomplete(
			"connect-control",
			"the TCP socket was not preopened: " + ErrnoText(_preopenError));
		return;
	} else if (_arguments.loopbackPort <= 0) {
		incomplete("connect-control", "no loopback port argument");
	} else {
		attemptConnect("connect-control", _preopened.get());
	}
	const auto fd = _preopened.release();
	const auto closeError = close(fd) ? errno : 0;
	_report.row(Format(
		"connect-control: fd %d closed: %s",
		fd,
		closeError ? ErrnoText(closeError).c_str() : "ok"));
}

void Probe::attemptFile(const std::string &label, const std::string &path) {
	if (path.empty()) {
		for (const auto call : { " opendir", " stat", " access", " policy" }) {
			incomplete(label + call, "no path argument");
		}
		return;
	}
	auto entries = 0;
	const auto directory = opendir(path.c_str());
	const auto directoryError = directory ? 0 : errno;
	if (directory) {
		const auto closeDirectory = gsl::finally([&] {
			closedir(directory);
		});
		while (readdir(directory)) {
			++entries;
		}
	}
	attemptErrno(
		label + " opendir",
		directoryError,
		directory
			? Format("%s: %d entries", path.c_str(), entries)
			: path);
	struct stat st = {};
	attemptErrno(
		label + " stat",
		stat(path.c_str(), &st) ? errno : 0,
		path);
	attemptErrno(
		label + " access",
		access(path.c_str(), R_OK) ? errno : 0,
		path);
	const auto policy = SandboxCheckPath(path);
	attempt(
		label + " policy",
		PolicyCode(policy),
		Format("file-read-data %s = %d", path.c_str(), policy));
}

void Probe::attemptFiles() {
	attemptFile("cwd .", ".");
	attemptFile("client-cwd", _arguments.cwd);
	attemptFile("app-support", _arguments.applicationSupport);
	if (_arguments.cwd.empty()) {
		incomplete("client-file open", "no path argument");
		return;
	}
	const auto file = _arguments.cwd
		+ ((_arguments.cwd.back() == '/') ? "" : "/")
		+ "log.txt";
	const auto fd = OwnedFd(open(file.c_str(), O_RDONLY));
	const auto error = (fd.get() < 0) ? errno : 0;
	attemptErrno("client-file open", error, file);
}

void Probe::attemptParent() {
	const auto ppid = _ppid;
	attemptErrno(
		"parent kill-0",
		kill(ppid, 0) ? errno : 0,
		Format("kill(%d, 0)", int(ppid)));
	const auto task = [&](
			const std::string &name,
			pid_t pid,
			bool nameOnly) {
		auto port = mach_port_t(MACH_PORT_NULL);
		const auto result = nameOnly
			? task_name_for_pid(mach_task_self(), pid, &port)
			: task_for_pid(mach_task_self(), pid, &port);
		if (MACH_PORT_VALID(port)) {
			mach_port_deallocate(mach_task_self(), port);
		}
		attemptKern(
			name,
			result,
			Format(
				"%s(%d)",
				nameOnly ? "task_name_for_pid" : "task_for_pid",
				int(pid)));
	};
	task("parent task_for_pid", ppid, false);
	task("self task_for_pid", _pid, false);
	task("parent task_name_for_pid", ppid, true);
	const auto bsdInfo = [&](const std::string &name, pid_t pid) {
		auto info = proc_bsdinfo();
		const auto bytes = proc_pidinfo(
			pid,
			PROC_PIDTBSDINFO,
			0,
			&info,
			sizeof(info));
		const auto read = (bytes == int(sizeof(info)))
			&& (pid_t(info.pbi_pid) == pid);
		attemptErrno(
			name,
			read ? 0 : errno ? errno : ESRCH,
			Format("PROC_PIDTBSDINFO(%d) = %d bytes", int(pid), bytes));
	};
	bsdInfo("parent proc_pidinfo", ppid);
	bsdInfo("self proc_pidinfo", _pid);

	char path[PROC_PIDPATHINFO_MAXSIZE] = { 0 };
	const auto pathBytes = proc_pidpath(ppid, path, sizeof(path));
	attemptErrno(
		"parent proc_pidpath",
		(pathBytes > 0) ? 0 : errno,
		Format(
			"proc_pidpath(%d) = %d bytes (path not kept)",
			int(ppid),
			pathBytes));

	int process[] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, int(ppid) };
	auto info = kinfo_proc();
	auto infoSize = sizeof(info);
	const auto infoRead = sysctl(process, 4, &info, &infoSize, nullptr, 0);
	const auto infoError = infoRead ? errno : 0;
	if (!infoRead && (infoSize != sizeof(info) || info.kp_proc.p_pid != ppid)) {
		attempt(
			"parent sysctl-proc",
			"empty",
			Format("KERN_PROC_PID(%d) = %d bytes", int(ppid), int(infoSize)));
	} else {
		attemptErrno(
			"parent sysctl-proc",
			infoError,
			Format("KERN_PROC_PID(%d)", int(ppid)));
	}

	int arguments[] = { CTL_KERN, KERN_PROCARGS2, int(ppid) };
	auto buffer = std::vector<char>(kProcArgsBytes);
	auto argumentsSize = buffer.size();
	const auto argumentsRead = sysctl(
		arguments,
		3,
		buffer.data(),
		&argumentsSize,
		nullptr,
		0);
	const auto argumentsError = argumentsRead ? errno : 0;
	if (!argumentsRead && !argumentsSize) {
		attempt(
			"parent sysctl-args",
			"empty",
			Format("KERN_PROCARGS2(%d) = 0 bytes", int(ppid)));
	} else {
		attemptErrno(
			"parent sysctl-args",
			argumentsError,
			Format(
				"KERN_PROCARGS2(%d) = %d bytes (contents not kept)",
				int(ppid),
				argumentsError ? 0 : int(argumentsSize)));
	}
}

void Probe::attemptServices() {
	attemptLookup("lookup tccd", "com.apple.tccd");
	attemptLookup("lookup tccd.system", "com.apple.tccd.system");
	attemptLookup("lookup locationd", "com.apple.locationd.registration");
	attemptLookup(
		"lookup locationd.desktop",
		"com.apple.locationd.desktop.registration");
}

void Probe::attemptKeychain() {
	attemptLookup("lookup SecurityServer", "com.apple.SecurityServer");
	attemptLookup("lookup securityd.xpc", "com.apple.securityd.xpc");
	if (_arguments.keychainAccount.empty()) {
		incomplete("keychain item", "no keychain account argument");
		return;
	}
	const auto interaction = SecKeychainSetUserInteractionAllowed(false);
	auto service = CFStringRef();
	auto account = CFStringRef();
	auto query = CFDictionaryRef();
	auto result = CFTypeRef();
	const auto release = gsl::finally([&] {
		const auto objects = {
			CFTypeRef(result),
			CFTypeRef(query),
			CFTypeRef(account),
			CFTypeRef(service),
		};
		for (const auto object : objects) {
			if (object) {
				CFRelease(object);
			}
		}
	});
	service = CFStringCreateWithCString(
		kCFAllocatorDefault,
		kWidgetHelperProbeKeychainService,
		kCFStringEncodingUTF8);
	account = CFStringCreateWithCString(
		kCFAllocatorDefault,
		_arguments.keychainAccount.c_str(),
		kCFStringEncodingUTF8);
	const void *keys[] = {
		kSecClass,
		kSecAttrService,
		kSecAttrAccount,
		kSecReturnData,
		kSecMatchLimit,
		kSecUseAuthenticationUI,
	};
	const void *values[] = {
		kSecClassGenericPassword,
		service,
		account,
		kCFBooleanTrue,
		kSecMatchLimitOne,
		kSecUseAuthenticationUIFail,
	};
	query = CFDictionaryCreate(
		kCFAllocatorDefault,
		keys,
		values,
		CFIndex(std::size(keys)),
		&kCFTypeDictionaryKeyCallBacks,
		&kCFTypeDictionaryValueCallBacks);
	const auto status = (service && account && query)
		? SecItemCopyMatching(query, &result)
		: OSStatus(errSecAllocate);
	auto bytes = CFIndex(-1);
	if (result && CFGetTypeID(result) == CFDataGetTypeID()) {
		bytes = CFDataGetLength(static_cast<CFDataRef>(result));
	}
	attempt("keychain item", Status(status), Format(
		"SecItemCopyMatching(generic password of %s, the run's account,"
		" kSecUseAuthenticationUIFail) = %d, data of %ld bytes (not kept);"
		" SecKeychainSetUserInteractionAllowed(false) = %d",
		kWidgetHelperProbeKeychainService,
		int(status),
		long(bytes),
		int(interaction)));
}

void Probe::attemptCamera() {
	attemptLookup("lookup cmio", "com.apple.cmio.registerassistantservice");
	const auto address = CMIOObjectPropertyAddress{
		kCMIOHardwarePropertyDevices,
		kCMIOObjectPropertyScopeGlobal,
		kCMIOObjectPropertyElementMain,
	};
	auto size = UInt32();
	const auto status = CMIOObjectGetPropertyDataSize(
		kCMIOObjectSystemObject,
		&address,
		0,
		nullptr,
		&size);
	const auto devices = int(size / sizeof(CMIOObjectID));
	attempt(
		"camera devices",
		Status(status) + Format("/%d", devices),
		Format(
			"CMIOObjectGetPropertyDataSize(devices) = %d, %d devices",
			int(status),
			devices));
	const auto policy = SandboxCheck("device-camera");
	attempt(
		"policy device-camera",
		PolicyCode(policy),
		Format("sandbox_check = %d", policy));
}

[[nodiscard]] int CountInputDevices(UInt32 size) {
	const auto address = AudioObjectPropertyAddress{
		kAudioHardwarePropertyDevices,
		kAudioObjectPropertyScopeGlobal,
		kAudioObjectPropertyElementMain,
	};
	auto devices = std::vector<AudioObjectID>(size / sizeof(AudioObjectID));
	auto filled = size;
	if (devices.empty() || AudioObjectGetPropertyData(
			kAudioObjectSystemObject,
			&address,
			0,
			nullptr,
			&filled,
			devices.data()) != 0) {
		return 0;
	}
	devices.resize(filled / sizeof(AudioObjectID));
	auto result = 0;
	for (const auto device : devices) {
		const auto streams = AudioObjectPropertyAddress{
			kAudioDevicePropertyStreams,
			kAudioObjectPropertyScopeInput,
			kAudioObjectPropertyElementMain,
		};
		auto streamsSize = UInt32();
		if (AudioObjectGetPropertyDataSize(
				device,
				&streams,
				0,
				nullptr,
				&streamsSize) == 0
			&& streamsSize > 0) {
			++result;
		}
	}
	return result;
}

void Probe::attemptMicrophone() {
	attemptLookup("lookup audiohald", "com.apple.audio.audiohald");
	const auto address = AudioObjectPropertyAddress{
		kAudioHardwarePropertyDevices,
		kAudioObjectPropertyScopeGlobal,
		kAudioObjectPropertyElementMain,
	};
	auto size = UInt32();
	const auto status = AudioObjectGetPropertyDataSize(
		kAudioObjectSystemObject,
		&address,
		0,
		nullptr,
		&size);
	const auto devices = int(size / sizeof(AudioObjectID));
	const auto inputs = (status == 0) ? CountInputDevices(size) : 0;
	attempt(
		"microphone devices",
		Status(status) + Format("/%d/%d", devices, inputs),
		Format(
			"AudioObjectGetPropertyDataSize(devices) = %d, %d devices,"
			" %d with input streams",
			int(status),
			devices,
			inputs));
	const auto policy = SandboxCheck("device-microphone");
	attempt(
		"policy device-microphone",
		PolicyCode(policy),
		Format("sandbox_check = %d", policy));
}

void Probe::attemptIOKit() {
	const auto surface = IOServiceGetMatchingService(
		kIOMainPortDefault,
		IOServiceMatching("IOSurfaceRoot"));
	if (!surface) {
		incomplete("iokit open", "no IOSurfaceRoot service matched");
	} else {
		const auto releaseSurface = gsl::finally([&] {
			IOObjectRelease(surface);
		});
		auto connect = io_connect_t();
		const auto result = IOServiceOpen(
			surface,
			mach_task_self(),
			0,
			&connect);
		if (result == KERN_SUCCESS) {
			IOServiceClose(connect);
		}
		attempt(
			"iokit open",
			KernCode(result),
			Format("IOServiceOpen(IOSurfaceRoot) = 0x%x", unsigned(result)));
	}
	const auto expert = IOServiceGetMatchingService(
		kIOMainPortDefault,
		IOServiceMatching("IOPlatformExpertDevice"));
	if (!expert) {
		incomplete(
			"iokit property",
			"no IOPlatformExpertDevice service matched");
		return;
	}
	const auto releaseExpert = gsl::finally([&] {
		IOObjectRelease(expert);
	});
	const auto value = IORegistryEntryCreateCFProperty(
		expert,
		CFSTR("IOPlatformSerialNumber"),
		kCFAllocatorDefault,
		0);
	const auto present = (value != nullptr);
	if (value) {
		CFRelease(value);
	}
	attempt(
		"iokit property",
		present ? "ok" : "absent",
		present
			? "IOPlatformSerialNumber returned (value not kept)"
			: "IOPlatformSerialNumber not returned");
}

void Probe::attemptWindowServer() {
	attemptLookup("lookup windowserver", "com.apple.windowserver.active");
	const auto connection = CGSMainConnectionID();
	attempt(
		"windowserver connection",
		connection ? "CONNECTED" : "none",
		Format("CGSMainConnectionID() = %d", connection));
}

void Probe::attemptThread() {
	auto ran = false;
	try {
		auto thread = std::thread([&] {
			ran = true;
		});
		thread.join();
		attempt(
			"thread",
			ran ? "ok" : "not-run",
			"std::thread started and joined");
	} catch (const std::exception &e) {
		attempt("thread", "failed", e.what());
	}
}

void Probe::attemptAfterClose() {
	attemptErrno(
		"close stdin",
		close(STDIN_FILENO) ? errno : 0,
		"close(0) after the listing");
	auto fd = OwnedFd(socket(AF_INET, SOCK_STREAM, 0));
	const auto error = (fd.get() < 0) ? errno : 0;
	attemptErrno(
		"socket after close",
		error,
		Format("socket(AF_INET, SOCK_STREAM) = %d", fd.get()));
	fd.reset();
	auto limit = rlimit();
	if (getrlimit(RLIMIT_NOFILE, &limit) != 0) {
		attemptErrno("raise nofile", errno, "getrlimit(RLIMIT_NOFILE)");
		return;
	}
	const auto current = Format(
		"%llu/%llu",
		(unsigned long long)limit.rlim_cur,
		(unsigned long long)limit.rlim_max);
	if (limit.rlim_max == RLIM_INFINITY) {
		attempt(
			"raise nofile",
			"n/a",
			"the hard limit is already unlimited: " + current);
		return;
	}
	const auto raised = rlimit{
		.rlim_cur = limit.rlim_max + 1,
		.rlim_max = limit.rlim_max + 1,
	};
	attemptErrno(
		"raise nofile",
		setrlimit(RLIMIT_NOFILE, &raised) ? errno : 0,
		"setrlimit(RLIMIT_NOFILE) above " + current);
}

std::string Probe::summary() const {
	const auto clean = _locking
		&& _lockdownResult.empty()
		&& _defects.empty()
		&& _unaccounted.empty()
		&& _failures.empty()
		&& !_arguments.connectControl
		&& _report.cut() == 0
		&& !_report.writeError();
	auto reasons = std::vector<std::string>();
	if (!_locking) {
		reasons.push_back("unsandboxed control");
	}
	if (_arguments.connectControl) {
		reasons.push_back("connect-control fixture injected");
	}
	if (!_lockdownResult.empty()) {
		reasons.push_back("lockdown failed");
	}
	if (!_failures.empty()) {
		reasons.push_back(Format(
			"%d enumeration/identity failures",
			int(_failures.size())));
	}
	if (!_defects.empty()) {
		reasons.push_back(Format("%d defects", int(_defects.size())));
	}
	if (!_unaccounted.empty()) {
		reasons.push_back(Format("%d unaccounted", int(_unaccounted.size())));
	}
	if (_report.cut()) {
		reasons.push_back(Format("%d stderr rows cut", _report.cut()));
	}
	if (_report.writeError()) {
		reasons.push_back(
			"stderr write failed: " + ErrnoText(_report.writeError()));
	}
	auto verdict = std::string();
	for (const auto &reason : reasons) {
		verdict += (verdict.empty() ? "" : ", ") + reason;
	}
	const auto complete = _failures.empty()
		&& _report.cut() == 0
		&& !_report.writeError();

	const auto stable = _pre.error.empty()
		&& _referenced.error.empty()
		&& _referenceChanges.empty();
	auto head = Format(
		"widget helper probe pid=%d mode=%s lockdown=%s %s references=%d"
		" stable=%s",
		int(_pid),
		_arguments.mode.c_str(),
		!_locking ? "skipped" : _lockdownResult.empty() ? "ok" : "FAILED",
		_state.c_str(),
		int(_references.size()),
		stable ? "yes" : "NO");
	head += " | inventory: "
		+ (clean ? std::string("clean") : ("NOT clean (" + verdict + ")"));
	auto fds = Format("fds %d:", int(_descriptors.list.size()));
	if (!_descriptors.error.empty()) {
		fds = "fds: listing FAILED";
	}
	for (const auto &label : _fdLabels) {
		fds += " " + label + ";";
	}
	const auto mach = _post.error.empty()
		? Format(
			"mach %d: task %d, thread %d, host %d, semaphore %d, clock %d,"
			" voucher %d, task-name %d, bootstrap %d, receive %d,"
			" send-receive %d, port-set %d, dead %d, libSystem services %d,"
			" firehose push ports %d, defects %d, unaccounted %d",
			int(_post.entries.size()),
			_counts.task,
			_counts.thread,
			_counts.host,
			_counts.semaphore,
			_counts.clock,
			_counts.voucher,
			_counts.ownName,
			_counts.bootstrap,
			_counts.receive,
			_counts.sendReceive,
			_counts.portSet,
			_counts.deadName,
			_counts.libSystem,
			_counts.firehose,
			int(_defects.size()),
			int(_unaccounted.size()))
		: std::string("mach: listing FAILED");
	auto attempts = std::string("attempts:");
	for (const auto &[name, code] : _attempts) {
		attempts += " " + name + "=" + code + ";";
	}
	if (_incomplete) {
		attempts += Format(
			" (%d incomplete: their controlled claims are unproven)",
			_incomplete);
	}
	const auto tail = Format(
		" | full listing on stderr: %d rows, last \"%send\"%s",
		_report.rows() + 1,
		_report.prefix().c_str(),
		complete ? "" : "; this listing is INCOMPLETE");

	auto items = std::vector<std::string>();
	for (const auto &failure : _failures) {
		items.push_back("failure: " + failure);
	}
	for (const auto &defect : _defects) {
		items.push_back("defect: " + defect);
	}
	if (!_firehoseFailure.empty()) {
		items.push_back("firehose: FAILED " + _firehoseFailure);
	}
	if (_unaccounted.empty()) {
		items.push_back(complete
			? "unaccounted: none"
			: "unaccounted: unknown (listing incomplete)");
	} else {
		for (const auto &entry : _unaccounted) {
			items.push_back("unaccounted: " + entry);
		}
	}

	auto result = head + " | " + fds + " | " + mach;
	const auto room = [&](size_t more) {
		return result.size() + more + tail.size() <= kSummaryBytes;
	};
	auto dropped = 0;
	for (const auto &item : items) {
		if (!dropped && room(item.size() + 3 + attempts.size() + 3 + 64)) {
			result += " | " + item;
		} else {
			++dropped;
		}
	}
	if (room(attempts.size() + 3)) {
		result += " | " + attempts;
	} else {
		++dropped;
	}
	if (dropped) {
		const auto marker = Format(
			" | SUMMARY CUT: %d items left out, see stderr; this summary is"
			" INCOMPLETE",
			dropped);
		result = result.substr(0, kSummaryBytes - tail.size() - marker.size());
		result += marker;
	}
	return result + tail;
}

#elif defined Q_OS_WIN // Q_OS_MAC

// The native probe takes its controls before the real lockdown, while the
// helper still impersonates, and its attempts after it; the report goes to
// stderr and the helper serves widgets.
[[nodiscard]] std::string LockDownWithProbe(const Fn<QString()> &lockdown) {
	if (!lockdown) {
		return "no lockdown callable";
	}
	const auto probe = Platform::WidgetHelper::PrepareProbe();
	const auto error = lockdown();
	if (!error.isEmpty()) {
		return error.toStdString();
	}
	for (const auto &line : probe().split(QChar('\n'))) {
		const auto utf8 = line.toUtf8();
		std::fprintf(stderr, "Widget helper probe: %s\n", utf8.constData());
	}
	return std::string();
}

#endif // Q_OS_MAC || Q_OS_WIN

} // namespace

bool IsWidgetHelperProbeMode(const QString &mode) {
	return (mode == ProbeMode()) || (mode == UnsandboxedMode());
}

QStringList WidgetHelperProbeArguments(const QString &mode) {
	return WidgetHelperProbeArguments(mode, {});
}

QStringList WidgetHelperProbeArguments(
		const QString &mode,
		const WidgetHelperProbeFixture &fixture) {
	if (!IsWidgetHelperProbeMode(mode)) {
		return {};
	}
	auto result = QStringList{
		mode,
		cWorkingDir(),
		QStandardPaths::writableLocation(
			QStandardPaths::GenericDataLocation),
	};
	if (!fixture.keychainAccount.isEmpty()
		|| fixture.loopbackPort > 0
		|| fixture.connectControl) {
		result.push_back(fixture.keychainAccount);
		result.push_back(QString::number(std::max(fixture.loopbackPort, 0)));
		if (fixture.connectControl) {
			result.push_back(ConnectControlArgument());
		}
	}
	return result;
}

std::string WidgetHelperLockdown(
		const QStringList &arguments,
		Fn<QString()> lockdown) {
	if (arguments.size() < 2) {
		return lockdown
			? lockdown().toStdString()
			: std::string("no lockdown callable");
	}
	const auto mode = arguments[1];
	if (!IsWidgetHelperProbeMode(mode)) {
		return "unknown widget helper mode \""
			+ mode.left(64).toStdString()
			+ "\"";
	}
#ifdef Q_OS_MAC
	return Probe(ParseArguments(arguments), std::move(lockdown)).run();
#elif defined Q_OS_WIN // Q_OS_MAC
	if (mode == ProbeMode()) {
		return LockDownWithProbe(lockdown);
	}
	return "the widget helper probe is not available on this platform";
#else // Q_OS_MAC || Q_OS_WIN
	return "the widget helper probe is not available on this platform";
#endif // Q_OS_MAC || Q_OS_WIN
}

} // namespace Test

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
