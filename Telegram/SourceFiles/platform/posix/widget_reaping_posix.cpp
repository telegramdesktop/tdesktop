/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/posix/widget_reaping_posix.h"

#include <sys/wait.h>

#include <cerrno>
#include <signal.h>
#include <thread>

namespace Platform::WidgetHelper {
namespace {

constexpr auto kPollDelay = crl::time(10);

[[nodiscard]] QString SystemError(const char *call, int error) {
	return u"%1 failed: %2"_q.arg(
		QString::fromLatin1(call),
		qt_error_string(error));
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

} // namespace

QString DescribeStatus(int status) {
	if (WIFEXITED(status)) {
		return u"exit %1"_q.arg(WEXITSTATUS(status));
	} else if (WIFSIGNALED(status)) {
		return u"signal %1"_q.arg(WTERMSIG(status));
	}
	return u"status %1"_q.arg(status);
}

Completion CompleteWith(
		crl::time exitWait,
		crl::time killWait,
		Fn<std::optional<Completion>(crl::time timeout)> waitExit,
		Fn<QString()> sendKill) {
	const auto started = crl::now();
	auto exited = waitExit(exitWait);
	if (exited && exited->reaped) {
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
	if (exited) {
		result->detail = exited->detail + u", "_q + result->detail;
	}
	result->detail += u", "_q + (killError.isEmpty()
		? u"sent SIGKILL after %1 ms"_q.arg(signaled)
		: killError);
	return std::move(*result);
}

Completion CompleteChild(
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

} // namespace Platform::WidgetHelper
