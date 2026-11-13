/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/platform_widget_helper.h"

#include "platform/posix/widget_channel_posix.h"

#include <Foundation/Foundation.h>
#include <sys/wait.h>

#include <crt_externs.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <thread>
#include <unistd.h>

namespace Platform::WidgetHelper {
namespace {

constexpr auto kSpareDescriptorMin = 10;
constexpr auto kPollDelay = crl::time(10);

[[nodiscard]] QString SystemError(const char *call, int error) {
	return u"%1 failed: %2"_q.arg(
		QString::fromLatin1(call),
		qt_error_string(error));
}

[[nodiscard]] QByteArray ExecutablePath() {
	@autoreleasepool {
		const auto path = [[NSBundle mainBundle] executablePath];
		return path
			? QByteArray([path fileSystemRepresentation])
			: QByteArray();
	}
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

} // namespace

bool Enabled() {
	return false;
}

Launched Launch() {
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
	auto arguments = std::array<char*, 4>{
		path.data(),
		flag.data(),
		token.data(),
		nullptr,
	};
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
			arguments.data(),
			*_NSGetEnviron())) {
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

} // namespace Platform::WidgetHelper
