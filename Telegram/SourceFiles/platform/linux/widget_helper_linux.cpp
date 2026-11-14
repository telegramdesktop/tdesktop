/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/platform_widget_helper.h"

#ifdef Q_OS_LINUX

#include "platform/linux/widget_lockdown_linux.h"
#include "platform/posix/widget_channel_posix.h"

#include <crl/crl_object_on_thread.h>
#include <flatpakportal/flatpakportal.hpp>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include <cerrno>
#include <climits>
#include <condition_variable>
#include <cstdint>
#include <fcntl.h>
#include <future>
#include <ksandbox.h>
#include <mutex>
#include <sched.h>
#include <signal.h>
#include <string>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace Platform::WidgetHelper {
namespace {

using namespace gi::repository;

constexpr auto kSpareDescriptorMin = 10;
constexpr auto kPollDelay = crl::time(10);
constexpr auto kSelfExecutable = "/proc/self/exe";
constexpr auto kDenyGroups = std::string_view("deny");
constexpr auto kChildStackSize = size_t(64 * 1024);
constexpr auto kChildFailed = 127;
constexpr auto kMaxSignal = 64;
constexpr auto kNamespaceFlags = CLONE_NEWUSER
	| CLONE_NEWPID
	| CLONE_NEWNET
	| CLONE_NEWNS;
constexpr auto kPortalService = "org.freedesktop.portal.Flatpak";
constexpr auto kPortalObjectPath = "/org/freedesktop/portal/Flatpak";
constexpr auto kPortalTimeout = 10 * 1000;
constexpr auto kPortalSandbox = uint32(4);
constexpr auto kPortalNoNetwork = uint32(8);
constexpr auto kPortalWatchBus = uint32(16);
constexpr auto kMaxPortalExits = size_t(64);

using KernelSignalSet = uint64_t;

constexpr auto kSignalSetSize = sizeof(KernelSignalSet);

enum class ChildStep {
	None,
	SignalAction,
	Duplicate,
	DescriptorFlags,
	WorkingDirectory,
	SignalMask,
	Execute,
};

struct ChildSlot {
	ChildStep step = ChildStep::None;
	int error = 0;
};

struct SpawnPlan {
	SpawnPlan() = default;
	SpawnPlan(const SpawnPlan &other) = delete;
	SpawnPlan &operator=(const SpawnPlan &other) = delete;
	~SpawnPlan();

	std::string exe;
	std::string flag;
	std::string token;
	std::vector<std::string> tail;
	std::vector<char*> argv;
	std::string uidMap;
	std::string gidMap;
	int devnullRead = -1;
	int devnullWrite = -1;
	int spare = -1;
};

struct ChildArguments {
	const SpawnPlan *plan = nullptr;
	ChildSlot slot;
	bool namespaces = false;
};

struct KernelSignalAction {
	void (*handler)(int) = nullptr;
	unsigned long flags = 0;
	void (*restorer)() = nullptr;
	KernelSignalSet mask = 0;
};

struct Cloned {
	QString error;
	pid_t pid = 0;
	bool cloneFailed = false;
};

struct SpawnerState {};

struct Portal {
	FlatpakPortal::FlatpakProxy proxy;
	std::mutex mutex;
	std::condition_variable exited;
	std::vector<std::pair<uint32, uint32>> statuses;
};

struct PortalLookup {
	Portal *portal = nullptr;
	QString error;
};

SpawnPlan::~SpawnPlan() {
	for (const auto descriptor : { devnullRead, devnullWrite, spare }) {
		if (descriptor >= 0) {
			close(descriptor);
		}
	}
}

[[nodiscard]] QString SystemError(const char *call, int error) {
	return u"%1 failed: %2"_q.arg(
		QString::fromLatin1(call),
		qt_error_string(error));
}

[[nodiscard]] QString DescribeStatus(int status) {
	if (WIFEXITED(status)) {
		return u"exit %1"_q.arg(WEXITSTATUS(status));
	} else if (WIFSIGNALED(status)) {
		return u"signal %1"_q.arg(WTERMSIG(status));
	}
	return u"status %1"_q.arg(status);
}

[[nodiscard]] std::optional<Completion> WaitFor(
		pid_t pid,
		crl::time timeout) {
	const auto deadline = crl::now() + timeout;
	while (true) {
		auto status = 0;
		const auto result = waitpid(pid, &status, WNOHANG);
		const auto error = (result < 0) ? errno : 0;
		if (result == pid) {
			return Completion{
				.detail = DescribeStatus(status),
				.reaped = true,
			};
		} else if (error == ECHILD) {
			return Completion{
				.detail = u"reaped elsewhere"_q,
				.reaped = true,
			};
		} else if (error && error != EINTR) {
			return Completion{ .detail = SystemError("waitpid", error) };
		} else if (crl::now() >= deadline) {
			return std::nullopt;
		} else if (!error) {
			std::this_thread::sleep_for(std::chrono::milliseconds(kPollDelay));
		}
	}
}

template <typename WaitExit, typename SendKill>
[[nodiscard]] Completion CompleteWith(
		crl::time exitWait,
		crl::time killWait,
		WaitExit &&waitExit,
		SendKill &&sendKill) {
	const auto started = crl::now();
	if (auto exited = waitExit(exitWait)) {
		return std::move(*exited);
	}
	const auto signaled = crl::now() - started;
	const auto killError = sendKill();
	auto result = waitExit(killWait);
	if (!result) {
		result = Completion{
			.detail = u"not reaped after %1 ms"_q.arg(crl::now() - started),
		};
	}
	result->detail += u", "_q + (killError.isEmpty()
		? u"sent SIGKILL after %1 ms"_q.arg(signaled)
		: killError);
	return std::move(*result);
}

[[nodiscard]] Completion Complete(
		pid_t pid,
		crl::time exitWait,
		crl::time killWait) {
	Expects(pid > 0);

	return CompleteWith(exitWait, killWait, [&](crl::time timeout) {
		return WaitFor(pid, timeout);
	}, [&] {
		return (kill(pid, SIGKILL) == 0)
			? QString()
			: SystemError("kill", errno);
	});
}

[[nodiscard]] std::string ExecutablePath() {
	auto result = std::string(PATH_MAX, char(0));
	const auto size = readlink(
		kSelfExecutable,
		result.data(),
		result.size());
	if (size <= 0 || size_t(size) >= result.size()) {
		return kSelfExecutable;
	}
	result.resize(size_t(size));
	return result;
}

[[nodiscard]] std::string IdentityMap(uint32 id) {
	return "0 " + std::to_string(id) + " 1\n";
}

[[nodiscard]] QString OpenNull(int &descriptor, int flags) {
	const auto opened = open("/dev/null", flags | O_CLOEXEC);
	if (opened < 0) {
		return SystemError("open", errno);
	}
	descriptor = fcntl(opened, F_DUPFD_CLOEXEC, kSpareDescriptorMin);
	const auto error = errno;
	close(opened);
	return (descriptor < 0) ? SystemError("fcntl", error) : QString();
}

[[nodiscard]] QString PrepareSpawn(
		SpawnPlan &plan,
		int child,
		const QStringList &arguments) {
	plan.exe = ExecutablePath();
	plan.flag = "-widgethelper";
	plan.token = std::to_string(kChannelDescriptor);
	plan.tail.reserve(arguments.size());
	for (const auto &argument : arguments) {
		plan.tail.push_back(argument.toUtf8().toStdString());
	}
	plan.argv = { plan.exe.data(), plan.flag.data(), plan.token.data() };
	for (auto &argument : plan.tail) {
		plan.argv.push_back(argument.data());
	}
	plan.argv.push_back(nullptr);
	plan.uidMap = IdentityMap(geteuid());
	plan.gidMap = IdentityMap(getegid());
	auto error = OpenNull(plan.devnullRead, O_RDONLY);
	if (error.isEmpty()) {
		error = OpenNull(plan.devnullWrite, O_WRONLY);
	}
	if (!error.isEmpty()) {
		return error;
	}
	plan.spare = fcntl(child, F_DUPFD_CLOEXEC, kSpareDescriptorMin);
	return (plan.spare < 0) ? SystemError("fcntl", errno) : QString();
}

[[nodiscard]] const char *CallName(ChildStep step) {
	switch (step) {
	case ChildStep::SignalAction: return "rt_sigaction";
	case ChildStep::Duplicate: return "dup2";
	case ChildStep::DescriptorFlags: return "fcntl";
	case ChildStep::WorkingDirectory: return "chdir";
	case ChildStep::SignalMask: return "rt_sigprocmask";
	case ChildStep::Execute: return "execve";
	case ChildStep::None: break;
	}
	Unexpected("Step in CallName.");
}

[[noreturn]] void FailChild(ChildSlot &slot, ChildStep step) {
	slot.error = errno;
	slot.step = step;
	_exit(kChildFailed);
}

void WriteMapping(const char *path, std::string_view text) {
	const auto descriptor = open(path, O_WRONLY | O_CLOEXEC);
	if (descriptor < 0) {
		return;
	}
	[[maybe_unused]] const auto written = write(
		descriptor,
		text.data(),
		text.size());
	close(descriptor);
}

int SpawnChild(void *raw) {
	auto &arguments = *static_cast<ChildArguments*>(raw);
	const auto &plan = *arguments.plan;
	auto &slot = arguments.slot;
	for (auto number = 1; number <= kMaxSignal; ++number) {
		if (number == SIGKILL || number == SIGSTOP) {
			continue;
		}
		const auto action = KernelSignalAction{ .handler = SIG_DFL };
		if (syscall(
				SYS_rt_sigaction,
				number,
				&action,
				nullptr,
				kSignalSetSize) != 0) {
			FailChild(slot, ChildStep::SignalAction);
		}
	}
	if (arguments.namespaces) {
		WriteMapping("/proc/self/setgroups", kDenyGroups);
		WriteMapping("/proc/self/uid_map", plan.uidMap);
		WriteMapping("/proc/self/gid_map", plan.gidMap);
	}
	if (dup2(plan.devnullRead, STDIN_FILENO) < 0
		|| dup2(plan.devnullWrite, STDOUT_FILENO) < 0) {
		FailChild(slot, ChildStep::Duplicate);
	}
	if (fcntl(STDERR_FILENO, F_SETFD, 0) != 0) {
		FailChild(slot, ChildStep::DescriptorFlags);
	}
	if (dup2(plan.spare, kChannelDescriptor) < 0) {
		FailChild(slot, ChildStep::Duplicate);
	}
	CloseDescriptorsFrom(kChannelDescriptor + 1);
	if (chdir("/") != 0) {
		FailChild(slot, ChildStep::WorkingDirectory);
	}
	const auto empty = KernelSignalSet(0);
	if (syscall(
			SYS_rt_sigprocmask,
			SIG_SETMASK,
			&empty,
			nullptr,
			kSignalSetSize) != 0) {
		FailChild(slot, ChildStep::SignalMask);
	}
	execve(kSelfExecutable, plan.argv.data(), environ);
	FailChild(slot, ChildStep::Execute);
}

void Reap(pid_t pid) {
	auto result = pid_t();
	do {
		result = waitpid(pid, nullptr, 0);
	} while (result < 0 && errno == EINTR);
}

[[nodiscard]] Cloned CloneHelper(const SpawnPlan &plan, bool namespaces) {
	const auto stack = mmap(
		nullptr,
		kChildStackSize,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK,
		-1,
		0);
	if (stack == MAP_FAILED) {
		return { .error = SystemError("mmap", errno) };
	}
	const auto unmap = gsl::finally([&] {
		munmap(stack, kChildStackSize);
	});
	auto arguments = ChildArguments{
		.plan = &plan,
		.namespaces = namespaces,
	};
	const auto all = ~KernelSignalSet(0);
	auto old = KernelSignalSet(0);
	if (syscall(
			SYS_rt_sigprocmask,
			SIG_SETMASK,
			&all,
			&old,
			kSignalSetSize) != 0) {
		return { .error = SystemError("rt_sigprocmask", errno) };
	}
	const auto pid = clone(
		SpawnChild,
		static_cast<char*>(stack) + kChildStackSize,
		CLONE_VM | CLONE_VFORK | SIGCHLD | (namespaces ? kNamespaceFlags : 0),
		&arguments);
	const auto error = errno;
	syscall(SYS_rt_sigprocmask, SIG_SETMASK, &old, nullptr, kSignalSetSize);
	if (pid < 0) {
		return {
			.error = SystemError("clone", error),
			.cloneFailed = true,
		};
	} else if (arguments.slot.step != ChildStep::None) {
		Reap(pid);
		return {
			.error = SystemError(
				CallName(arguments.slot.step),
				arguments.slot.error),
		};
	}
	return { .pid = pid };
}

[[nodiscard]] crl::object_on_thread<SpawnerState> &Spawner() {
	static const auto spawner = new crl::object_on_thread<SpawnerState>();
	return *spawner;
}

[[nodiscard]] QString RemoteErrorText(GLib::Error &error) {
	Gio::DBusErrorNS_::strip_remote_error(error);
	return QString::fromUtf8(error.message_().c_str());
}

void RecordExit(Portal &portal, uint32 pid, uint32 status) {
	auto lock = std::lock_guard(portal.mutex);
	if (portal.statuses.size() >= kMaxPortalExits) {
		portal.statuses.erase(portal.statuses.begin());
	}
	portal.statuses.emplace_back(pid, status);
	portal.exited.notify_all();
}

void ServePortal(std::promise<PortalLookup> found) {
	auto context = GLib::MainContext::new_();
	context.push_thread_default();
	const auto restore = gsl::finally([&] {
		context.pop_thread_default();
	});
	auto proxy = FlatpakPortal::FlatpakProxy::new_for_bus_sync(
		Gio::BusType::SESSION_,
		Gio::DBusProxyFlags::DO_NOT_LOAD_PROPERTIES_,
		kPortalService,
		kPortalObjectPath);
	if (!proxy) {
		found.set_value(PortalLookup{
			.error = u"the Flatpak portal is unavailable: "_q
				+ RemoteErrorText(proxy.error()),
		});
		return;
	}
	const auto portal = new Portal{ .proxy = std::move(*proxy) };
	portal->proxy.set_default_timeout(kPortalTimeout);
	FlatpakPortal::Flatpak(portal->proxy).signal_spawn_exited().connect([=](
			FlatpakPortal::Flatpak,
			uint32 pid,
			uint32 status) {
		RecordExit(*portal, pid, status);
	});
	found.set_value(PortalLookup{ .portal = portal });
	GLib::MainLoop::new_(context, false).run();
}

[[nodiscard]] PortalLookup LookupPortal() {
	static auto mutex = std::mutex();
	static auto instance = (Portal*)nullptr;
	auto lock = std::lock_guard(mutex);
	if (instance) {
		return { .portal = instance };
	}
	auto found = std::promise<PortalLookup>();
	auto result = found.get_future();
	std::thread(ServePortal, std::move(found)).detach();
	auto lookup = result.get();
	instance = lookup.portal;
	return lookup;
}

[[nodiscard]] std::optional<Completion> WaitForExit(
		Portal &portal,
		uint32 pid,
		crl::time timeout) {
	const auto find = [&] {
		return ranges::find(
			portal.statuses,
			pid,
			&std::pair<uint32, uint32>::first);
	};
	auto lock = std::unique_lock(portal.mutex);
	const auto exited = portal.exited.wait_for(
		lock,
		std::chrono::milliseconds(timeout),
		[&] { return find() != portal.statuses.end(); });
	if (!exited) {
		return std::nullopt;
	}
	const auto i = find();
	const auto status = int(i->second);
	portal.statuses.erase(i);
	return Completion{
		.detail = DescribeStatus(status),
		.reaped = true,
	};
}

[[nodiscard]] Completion CompleteThroughPortal(
		not_null<Portal*> portal,
		uint32 pid,
		crl::time exitWait,
		crl::time killWait) {
	return CompleteWith(exitWait, killWait, [&](crl::time timeout) {
		return WaitForExit(*portal, pid, timeout);
	}, [&] {
		auto interface = FlatpakPortal::Flatpak(portal->proxy);
		auto sent = interface.call_spawn_signal_sync(pid, SIGKILL, true);
		return sent
			? QString()
			: (u"SpawnSignal failed: "_q + RemoteErrorText(sent.error()));
	});
}

[[nodiscard]] GLib::Variant DescriptorMap(int count) {
	auto entries = std::vector<GLib::Variant>();
	for (auto descriptor = 0; descriptor != count; ++descriptor) {
		entries.push_back(GLib::Variant::new_dict_entry(
			GLib::Variant::new_uint32(descriptor),
			GLib::Variant::new_handle(descriptor)));
	}
	return GLib::Variant::new_array(
		GLib::VariantType::new_("{uh}"),
		std::move(entries));
}

[[nodiscard]] Launched LaunchThroughPortal(
		PosixChannel channel,
		const QStringList &arguments) {
	const auto lookup = LookupPortal();
	if (!lookup.portal) {
		return { .error = lookup.error };
	}
	auto plan = SpawnPlan();
	auto prepared = PrepareSpawn(plan, channel.child->descriptor(), arguments);
	if (!prepared.isEmpty()) {
		return { .error = std::move(prepared) };
	} else if (plan.exe == kSelfExecutable) {
		return { .error = u"the executable path is unknown"_q };
	}
	const auto errorOutput = fcntl(
		STDERR_FILENO,
		F_DUPFD_CLOEXEC,
		kSpareDescriptorMin);
	if (errorOutput < 0) {
		return { .error = SystemError("fcntl", errno) };
	}
	auto descriptors = std::array{
		std::exchange(plan.devnullRead, -1),
		std::exchange(plan.devnullWrite, -1),
		errorOutput,
		std::exchange(plan.spare, -1),
	};
	auto list = Gio::UnixFDList::new_from_array(
		descriptors.data(),
		int(descriptors.size()));
	auto fds = DescriptorMap(int(descriptors.size()));
	auto envs = GLib::Variant::new_array(
		GLib::VariantType::new_("{ss}"),
		{});
	auto options = GLib::Variant::new_array(
		GLib::VariantType::new_("{sv}"),
		{});
	auto interface = FlatpakPortal::Flatpak(lookup.portal->proxy);
	auto pid = uint32();
	auto failure = (GError*)nullptr;
	const auto spawned = flatpak_portal_flatpak_call_spawn_sync(
		interface.gobj_(),
		"/",
		plan.argv.data(),
		fds.gobj_(),
		envs.gobj_(),
		kPortalSandbox | kPortalNoNetwork | kPortalWatchBus,
		options.gobj_(),
		list.gobj_(),
		&pid,
		nullptr,
		nullptr,
		&failure);
	auto error = GLib::Error(failure);
	if (!spawned) {
		return { .error = u"Spawn failed: "_q + RemoteErrorText(error) };
	}
	const auto portal = not_null(lookup.portal);
	return {
		.stream = std::move(channel.parent),
		.complete = [=](crl::time exitWait, crl::time killWait) {
			return CompleteThroughPortal(portal, pid, exitWait, killWait);
		},
		.pid = pid,
	};
}

[[nodiscard]] Launched LaunchDirectly(
		PosixChannel channel,
		const QStringList &arguments) {
	auto plan = SpawnPlan();
	auto prepared = PrepareSpawn(plan, channel.child->descriptor(), arguments);
	if (!prepared.isEmpty()) {
		return { .error = std::move(prepared) };
	}
	auto owned = std::make_unique<pid_t>();
	const auto pid = owned.get();
	auto result = Launched{
		.stream = std::move(channel.parent),
		.complete = [owned = std::move(owned)](
				crl::time exitWait,
				crl::time killWait) {
			return Complete(base::take(*owned), exitWait, killWait);
		},
	};
	auto cloned = Cloned();
	Spawner().with_sync([&](SpawnerState &) {
		cloned = CloneHelper(plan, true);
		if (cloned.cloneFailed) {
			LOG(("Tgw: The widget helper starts without namespaces: %1."
				).arg(cloned.error));
			cloned = CloneHelper(plan, false);
		}
	});
	if (!cloned.error.isEmpty()) {
		return { .error = std::move(cloned.error) };
	}
	*pid = cloned.pid;
	result.pid = cloned.pid;
	return result;
}

} // namespace

bool Enabled() {
	return kLockdownAvailable;
}

Launched Launch(const QStringList &arguments) {
	if (!kLockdownAvailable) {
		return {
			.error = u"the widget helper is not available on this architecture"_q,
		};
	} else if (fcntl(STDERR_FILENO, F_GETFD) < 0) {
		return { .error = u"stderr is not open"_q };
	}
	auto channel = CreatePosixChannel();
	if (!channel.error.isEmpty()) {
		return { .error = std::move(channel.error) };
	}
	return KSandbox::isFlatpak()
		? LaunchThroughPortal(std::move(channel), arguments)
		: LaunchDirectly(std::move(channel), arguments);
}

std::shared_ptr<twidget::SandboxRemoteStream> Adopt(const QString &channel) {
	if (channel != QString::number(kChannelDescriptor)) {
		return nullptr;
	}
	PrepareHelperProcess(kChannelDescriptor + 1);
	return AdoptPosixChannel(kChannelDescriptor);
}

QString Lockdown() {
	return QString::fromStdString(LockDownHelper());
}

} // namespace Platform::WidgetHelper

#endif // Q_OS_LINUX
