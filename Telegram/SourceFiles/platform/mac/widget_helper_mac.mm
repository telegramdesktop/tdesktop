/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/platform_widget_helper.h"

#include "platform/posix/widget_channel_posix.h"

#include <Foundation/Foundation.h>
#include <mach/mach.h>
#include <servers/bootstrap.h>
#include <sys/resource.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <sandbox.h>
#include <signal.h>
#include <spawn.h>
#include <thread>
#include <time.h>
#include <unistd.h>

namespace Platform::WidgetHelper {
namespace {

constexpr auto kSpareDescriptorMin = 10;
constexpr auto kPollDelay = crl::time(10);
constexpr auto kServiceCheckTries = 3;
constexpr auto kTextEncodingVariable = "__CF_USER_TEXT_ENCODING";
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
	QString error;
};

[[nodiscard]] QString SystemError(const char *call, int error) {
	return u"%1 failed: %2"_q.arg(
		QString::fromLatin1(call),
		qt_error_string(error));
}

[[nodiscard]] QString KernelError(const QString &call, kern_return_t error) {
	return u"%1 failed: %2 (%3)"_q.arg(
		call,
		QString::fromUtf8(bootstrap_strerror(error)),
		QString::number(error));
}

[[nodiscard]] QByteArray ExecutablePath() {
	@autoreleasepool {
		const auto path = [[NSBundle mainBundle] executablePath];
		return path
			? QByteArray([path fileSystemRepresentation])
			: QByteArray();
	}
}

[[nodiscard]] QByteArray TextEncodingEntry() {
	auto value = qgetenv(kTextEncodingVariable);
	if (value.isEmpty()) {
		value = "0x" + QByteArray::number(getuid(), 16).toUpper() + ":0:0";
	}
	return QByteArray(kTextEncodingVariable) + '=' + value;
}

[[nodiscard]] QString PrepareActions(
		not_null<posix_spawn_file_actions_t*> actions,
		int spare) {
	if (const auto error = posix_spawn_file_actions_addopen(
			actions,
			STDIN_FILENO,
			"/dev/null",
			O_RDONLY,
			0)) {
		return SystemError("posix_spawn_file_actions_addopen", error);
	}
	if (const auto error = posix_spawn_file_actions_addopen(
			actions,
			STDOUT_FILENO,
			"/dev/null",
			O_WRONLY,
			0)) {
		return SystemError("posix_spawn_file_actions_addopen", error);
	}
	if (const auto error = posix_spawn_file_actions_addinherit_np(
			actions,
			STDERR_FILENO)) {
		return SystemError("posix_spawn_file_actions_addinherit_np", error);
	}
	if (const auto error = posix_spawn_file_actions_adddup2(
			actions,
			spare,
			kChannelDescriptor)) {
		return SystemError("posix_spawn_file_actions_adddup2", error);
	}
	if (const auto error = posix_spawn_file_actions_addchdir_np(
			actions,
			"/")) {
		return SystemError("posix_spawn_file_actions_addchdir_np", error);
	}
	return QString();
}

[[nodiscard]] QString PrepareAttributes(
		not_null<posix_spawnattr_t*> attributes) {
	if (const auto error = posix_spawnattr_setflags(
			attributes,
			POSIX_SPAWN_CLOEXEC_DEFAULT
				| POSIX_SPAWN_SETSIGDEF
				| POSIX_SPAWN_SETSIGMASK)) {
		return SystemError("posix_spawnattr_setflags", error);
	}
	auto defaults = sigset_t();
	sigfillset(&defaults);
	if (const auto error = posix_spawnattr_setsigdefault(
			attributes,
			&defaults)) {
		return SystemError("posix_spawnattr_setsigdefault", error);
	}
	auto mask = sigset_t();
	sigemptyset(&mask);
	if (const auto error = posix_spawnattr_setsigmask(attributes, &mask)) {
		return SystemError("posix_spawnattr_setsigmask", error);
	}
	// The client's Crashpad ports (EXC_CRASH too) must not get helper crashes.
	if (const auto error = posix_spawnattr_setexceptionports_np(
			attributes,
			EXC_MASK_ALL | EXC_MASK_CRASH,
			MACH_PORT_NULL,
			EXCEPTION_DEFAULT,
			THREAD_STATE_NONE)) {
		return SystemError("posix_spawnattr_setexceptionports_np", error);
	}
	return QString();
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

[[nodiscard]] Completion Complete(
		pid_t pid,
		crl::time exitWait,
		crl::time killWait) {
	Expects(pid > 0);

	const auto started = crl::now();
	if (auto exited = WaitFor(pid, exitWait)) {
		return std::move(*exited);
	}
	const auto signaled = crl::now() - started;
	const auto killError = (kill(pid, SIGKILL) == 0) ? 0 : errno;
	auto result = WaitFor(pid, killWait);
	if (!result) {
		result = Completion{
			.detail = u"not reaped after %1 ms"_q.arg(crl::now() - started),
		};
	}
	result->detail += u", "_q + (killError
		? SystemError("kill", killError)
		: u"sent SIGKILL after %1 ms"_q.arg(signaled));
	return std::move(*result);
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
		return { .error = KernelError(u"mach_port_names"_q, result) };
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
		return { .error = u"mach_port_names returned unequal arrays"_q };
	}
	auto rights = PortRights();
	rights.list.reserve(namesCount);
	for (auto i = mach_msg_type_number_t(); i != namesCount; ++i) {
		rights.list.emplace_back(names[i], types[i]);
	}
	ranges::sort(rights.list);
	return rights;
}

[[nodiscard]] QString CheckService(
		const char *service,
		const PortRights &before) {
	auto port = mach_port_t(MACH_PORT_NULL);
	const auto release = gsl::finally([&] {
		if (MACH_PORT_VALID(port)) {
			mach_port_deallocate(mach_task_self(), port);
		}
	});
	const auto result = bootstrap_look_up(bootstrap_port, service, &port);
	const auto text = QString::fromLatin1(service);
	if (result == BOOTSTRAP_UNKNOWN_SERVICE) {
		return QString();
	} else if (result != KERN_SUCCESS) {
		return KernelError(u"bootstrap_look_up(%1)"_q.arg(text), result);
	} else if (!MACH_PORT_VALID(port)) {
		return u"bootstrap_look_up(%1) returned no right"_q.arg(text);
	}
	const auto received = port;
	const auto released = mach_port_deallocate(
		mach_task_self(),
		std::exchange(port, MACH_PORT_NULL));
	const auto held = ranges::any_of(before.list, [&](const auto &right) {
		return (right.first == received)
			&& (right.second & MACH_PORT_TYPE_SEND);
	});
	if (held) {
		return u"the widget helper holds a connection to %1 before lockdown"_q
			.arg(text);
	} else if (released != KERN_SUCCESS) {
		return KernelError(u"mach_port_deallocate(%1)"_q.arg(text), released);
	}
	return QString();
}

[[nodiscard]] QString CheckServiceRights() {
	for (auto i = 0; i != kServiceCheckTries; ++i) {
		const auto before = ReadPortRights();
		if (!before.error.isEmpty()) {
			return before.error;
		}
		for (const auto service : kForbiddenServices) {
			auto error = CheckService(service, before);
			if (!error.isEmpty()) {
				return error;
			}
		}
		const auto after = ReadPortRights();
		if (!after.error.isEmpty()) {
			return after.error;
		} else if (after.list == before.list) {
			return QString();
		}
	}
	return u"the widget helper's port rights kept changing"_q;
}

} // namespace

bool Enabled() {
	return false;
}

Launched Launch(const QStringList &arguments) {
	auto path = ExecutablePath();
	if (path.isEmpty()) {
		return { .error = u"the executable path is unknown"_q };
	} else if (fcntl(STDERR_FILENO, F_GETFD) < 0) {
		return { .error = u"stderr is not open"_q };
	}
	auto channel = CreatePosixChannel();
	if (!channel.error.isEmpty()) {
		return { .error = std::move(channel.error) };
	}
	const auto spare = fcntl(
		channel.child->descriptor(),
		F_DUPFD_CLOEXEC,
		kSpareDescriptorMin);
	if (spare < 0) {
		return { .error = SystemError("fcntl", errno) };
	}
	const auto closeSpare = gsl::finally([=] {
		close(spare);
	});

	auto actions = posix_spawn_file_actions_t();
	if (const auto error = posix_spawn_file_actions_init(&actions)) {
		return { .error = SystemError("posix_spawn_file_actions_init", error) };
	}
	const auto destroyActions = gsl::finally([&] {
		posix_spawn_file_actions_destroy(&actions);
	});
	auto attributes = posix_spawnattr_t();
	if (const auto error = posix_spawnattr_init(&attributes)) {
		return { .error = SystemError("posix_spawnattr_init", error) };
	}
	const auto destroyAttributes = gsl::finally([&] {
		posix_spawnattr_destroy(&attributes);
	});
	auto prepared = PrepareActions(&actions, spare);
	if (prepared.isEmpty()) {
		prepared = PrepareAttributes(&attributes);
	}
	if (!prepared.isEmpty()) {
		return { .error = std::move(prepared) };
	}

	auto flag = QByteArray("-widgethelper");
	auto token = QByteArray::number(kChannelDescriptor);
	auto encoded = std::vector<QByteArray>();
	encoded.reserve(arguments.size());
	for (const auto &argument : arguments) {
		encoded.push_back(argument.toUtf8());
	}
	auto argv = std::vector<char*>{ path.data(), flag.data(), token.data() };
	for (auto &argument : encoded) {
		argv.push_back(argument.data());
	}
	argv.push_back(nullptr);
	auto encoding = TextEncodingEntry();
	auto environment = std::array<char*, 2>{ encoding.data(), nullptr };
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
	if (const auto error = posix_spawn(
			pid,
			path.constData(),
			&actions,
			&attributes,
			argv.data(),
			environment.data())) {
		return { .error = SystemError("posix_spawn", error) };
	}
	result.pid = *pid;
	return result;
}

std::shared_ptr<twidget::SandboxRemoteStream> Adopt(const QString &channel) {
	if (channel != QString::number(kChannelDescriptor)) {
		return nullptr;
	}
	return AdoptPosixChannel(kChannelDescriptor);
}

QString Lockdown() {
	tzset();
#ifndef OS_MAC_STORE
	const auto refused = CheckServiceRights();
	if (!refused.isEmpty()) {
		return refused;
	}
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
	if (result != 0) {
		return u"sandbox_init failed: "_q + (error
			? QString::fromUtf8(error)
			: qt_error_string(code));
	}
	// Seatbelt cannot refuse socket(2), so no new descriptor is allowed.
	const auto none = rlimit{ .rlim_cur = 0, .rlim_max = 0 };
	if (setrlimit(RLIMIT_NOFILE, &none) != 0) {
		return SystemError("setrlimit", errno);
	}
	return QString();
}

} // namespace Platform::WidgetHelper
