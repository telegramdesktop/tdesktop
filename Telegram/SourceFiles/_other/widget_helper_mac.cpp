/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "_other/widget_helper.h"

#include "platform/posix/widget_channel_posix.h"

#include <gsl/gsl>

#include <mach/mach.h>
#include <servers/bootstrap.h>
#include <sys/resource.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <sandbox.h>
#include <string>
#include <system_error>
#include <time.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace WidgetHelper {
namespace {

constexpr auto kServiceCheckTries = 3;
constexpr auto kForbiddenServices = std::array{
	"com.apple.cfprefsd.agent",
	"com.apple.cfprefsd.daemon",
	"com.apple.system.opendirectoryd.libinfo",
	"com.apple.windowserver.active",
	"com.apple.CARenderServer",
	"com.apple.tccd",
	"com.apple.tccd.system",
	"com.apple.coreservices.launchservicesd",
	"com.apple.distributed_notifications@Uv3",
	"com.apple.SecurityServer",
	"com.apple.securityd.xpc",
	"com.apple.cmio.registerassistantservice",
	"com.apple.audio.audiohald",
	"com.apple.pasteboard.1",
	"com.apple.locationd.desktop.registration",
};
constexpr auto kProfile = R"(
(version 1)
(deny default)
(deny process-info* nvram* iokit-get-properties)
(allow process-info* (target self))
(allow signal (target self))
(allow file-read*
    (subpath "/usr/lib")
    (subpath "/System/Library/Frameworks")
    (subpath "/System/Library/PrivateFrameworks")
    (subpath "/System/Library/dyld")
    (subpath "/System/Cryptexes/OS/System/Library/dyld")
    (subpath "/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld"))
)";

struct PortRights {
	std::vector<std::pair<mach_port_name_t, mach_port_type_t>> list;
	std::string error;
};

[[nodiscard]] std::string SystemError(const char *call, int error) {
	return std::string(call)
		+ " failed: "
		+ std::generic_category().message(error);
}

[[nodiscard]] std::string KernelError(
		const std::string &call,
		kern_return_t error) {
	return call
		+ " failed: "
		+ bootstrap_strerror(error)
		+ " ("
		+ std::to_string(error)
		+ ")";
}

[[nodiscard]] PortRights ReadPortRights() {
	auto names = mach_port_name_array_t();
	auto namesCount = mach_msg_type_number_t();
	auto types = mach_port_type_array_t();
	auto typesCount = mach_msg_type_number_t();
	const auto result = mach_port_names(
		mach_task_self(),
		&names,
		&namesCount,
		&types,
		&typesCount);
	if (result != KERN_SUCCESS) {
		return { .error = KernelError("mach_port_names", result) };
	}
	const auto release = gsl::finally([&] {
		if (names) {
			vm_deallocate(
				mach_task_self(),
				vm_address_t(names),
				namesCount * sizeof(*names));
		}
		if (types) {
			vm_deallocate(
				mach_task_self(),
				vm_address_t(types),
				typesCount * sizeof(*types));
		}
	});
	if (namesCount != typesCount) {
		return { .error = "mach_port_names returned unequal arrays" };
	}
	auto rights = PortRights();
	rights.list.reserve(namesCount);
	for (auto i = mach_msg_type_number_t(); i != namesCount; ++i) {
		rights.list.emplace_back(names[i], types[i]);
	}
	std::ranges::sort(rights.list);
	return rights;
}

[[nodiscard]] std::string CheckService(
		const char *service,
		const PortRights &before) {
	auto port = mach_port_t(MACH_PORT_NULL);
	const auto release = gsl::finally([&] {
		if (MACH_PORT_VALID(port)) {
			mach_port_deallocate(mach_task_self(), port);
		}
	});
	const auto result = bootstrap_look_up(bootstrap_port, service, &port);
	const auto text = std::string(service);
#ifdef OS_MAC_STORE
	// The App Sandbox denies the lookup from before any initializer runs, so no right can be held.
	const auto absent = (result == BOOTSTRAP_UNKNOWN_SERVICE)
		|| (result == BOOTSTRAP_NOT_PRIVILEGED);
#else // OS_MAC_STORE
	const auto absent = (result == BOOTSTRAP_UNKNOWN_SERVICE);
#endif // OS_MAC_STORE
	if (absent) {
		return {};
	} else if (result != KERN_SUCCESS) {
		return KernelError("bootstrap_look_up(" + text + ")", result);
	} else if (!MACH_PORT_VALID(port)) {
		return "bootstrap_look_up(" + text + ") returned no right";
	}
	const auto received = port;
	const auto released = mach_port_deallocate(
		mach_task_self(),
		std::exchange(port, MACH_PORT_NULL));
	const auto held = std::ranges::any_of(before.list, [&](const auto &right) {
		return (right.first == received)
			&& (right.second & MACH_PORT_TYPE_SEND);
	});
	if (held) {
		return "the widget helper holds a connection to "
			+ text
			+ " before lockdown";
	} else if (released != KERN_SUCCESS) {
		return KernelError("mach_port_deallocate(" + text + ")", released);
	}
	return {};
}

[[nodiscard]] std::string CheckServiceRights() {
	for (auto i = 0; i != kServiceCheckTries; ++i) {
		const auto before = ReadPortRights();
		if (!before.error.empty()) {
			return before.error;
		}
		for (const auto service : kForbiddenServices) {
			auto error = CheckService(service, before);
			if (!error.empty()) {
				return error;
			}
		}
		const auto after = ReadPortRights();
		if (!after.error.empty()) {
			return after.error;
		} else if (after.list == before.list) {
			return {};
		}
	}
	return "the widget helper's port rights kept changing";
}

#ifdef OS_MAC_STORE
[[nodiscard]] std::vector<int> OpenDescriptors() {
	auto result = std::vector<int>();
	for (auto fd = 0, till = getdtablesize(); fd != till; ++fd) {
		if (fcntl(fd, F_GETFD) != -1) {
			result.push_back(fd);
		}
	}
	return result;
}

[[nodiscard]] std::vector<int> AddedDescriptors(
		const std::vector<int> &before) {
	auto result = OpenDescriptors();
	std::erase_if(result, [&](int fd) {
		return std::ranges::binary_search(before, fd);
	});
	return result;
}

[[nodiscard]] std::string DropBootstrap() {
	const auto task = mach_task_self();
	const auto name = std::exchange(bootstrap_port, MACH_PORT_NULL);
	const auto special = task_set_special_port(
		task,
		TASK_BOOTSTRAP_PORT,
		MACH_PORT_NULL);
	if (special != KERN_SUCCESS) {
		return KernelError("task_set_special_port", special);
	}
	// launchd registers a port that mach_ports_lookup returns.
	const auto registered = mach_ports_register(task, nullptr, 0);
	if (registered != KERN_SUCCESS) {
		return KernelError("mach_ports_register", registered);
	}
	// libxpc's bootstrap pipe holds its own references and uses the name until a send fails.
	auto refs = mach_port_urefs_t();
	const auto counted = mach_port_get_refs(
		task,
		name,
		MACH_PORT_RIGHT_SEND,
		&refs);
	if (counted != KERN_SUCCESS) {
		return KernelError("mach_port_get_refs", counted);
	}
	const auto removed = mach_port_mod_refs(
		task,
		name,
		MACH_PORT_RIGHT_SEND,
		-mach_port_delta_t(refs));
	if (removed != KERN_SUCCESS) {
		return KernelError("mach_port_mod_refs", removed);
	}
	auto type = mach_port_type_t();
	if (mach_port_type(task, name, &type) != KERN_INVALID_NAME) {
		return "the widget helper still holds its bootstrap port";
	}
	auto port = mach_port_t(MACH_PORT_NULL);
	const auto release = gsl::finally([&] {
		if (MACH_PORT_VALID(port)) {
			mach_port_deallocate(task, port);
		}
	});
	const auto found = bootstrap_look_up(
		MACH_PORT_NULL,
		kForbiddenServices.front(),
		&port);
	if (found == KERN_SUCCESS) {
		return "the widget helper can still look up Mach services";
	}
	return {};
}
#endif // OS_MAC_STORE

} // namespace

std::unique_ptr<twidget::SandboxRemoteStream> Adopt(std::string_view token) {
	constexpr auto kDescriptor = Platform::WidgetHelper::kChannelDescriptor;
	if (token != std::to_string(kDescriptor) || chdir("/") != 0) {
		return nullptr;
	}
	return Platform::WidgetHelper::AdoptPosixChannel(kDescriptor);
}

std::string Lockdown() {
	tzset();
	const auto refused = CheckServiceRights();
	if (!refused.empty()) {
		return refused;
	}
#ifdef OS_MAC_STORE
	const auto before = OpenDescriptors();
#endif // OS_MAC_STORE
	auto error = (char*)nullptr;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
	const auto result = sandbox_init(kProfile, 0, &error);
	const auto code = errno;
	const auto freeError = gsl::finally([&] {
		sandbox_free_error(error);
	});
#pragma clang diagnostic pop
#ifdef OS_MAC_STORE
	// sandbox_init fails with EPERM inside the App Sandbox, which already confines the helper.
	const auto failed = (result != 0) && (code != EPERM);
	const auto added = (result != 0)
		? AddedDescriptors(before)
		: std::vector<int>();
#else // OS_MAC_STORE
	const auto failed = (result != 0);
#endif // OS_MAC_STORE
	if (failed) {
		return "sandbox_init failed: " + (error
			? std::string(error)
			: std::generic_category().message(code));
	}
	// Seatbelt cannot refuse socket(2), so no new descriptor is allowed.
	const auto none = rlimit{ .rlim_cur = 0, .rlim_max = 0 };
	if (setrlimit(RLIMIT_NOFILE, &none) != 0) {
		return SystemError("setrlimit", errno);
	}
#ifdef OS_MAC_STORE
	// libsandbox's failure report leaves its log socket open.
	for (const auto fd : added) {
		close(fd);
	}
	if (setrlimit(RLIMIT_NPROC, &none) != 0) {
		return SystemError("setrlimit", errno);
	}
	const auto dropped = DropBootstrap();
	if (!dropped.empty()) {
		return dropped;
	}
#endif // OS_MAC_STORE
	return {};
}

} // namespace WidgetHelper
