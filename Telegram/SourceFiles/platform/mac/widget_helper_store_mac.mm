/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/platform_widget_helper.h"

#include "platform/mac/widget_helper_mac.h"
#include "platform/posix/widget_channel_posix.h"
#include "platform/posix/widget_stderr_posix.h"

#include <Foundation/Foundation.h>
#include <sys/event.h>
#include <xpc/xpc.h>

#include <condition_variable>
#include <fcntl.h>
#include <mutex>
#include <unistd.h>

namespace Platform::WidgetHelper {
namespace {

using details::SystemError;
using details::kRelayEndWait;

constexpr auto kStartWait = crl::time(15000);
constexpr auto kAbandonWait = crl::time(1000);
constexpr auto kServiceSuffix = ".WidgetHelper";

struct XpcReleaser {
	void operator()(xpc_object_t value) const;
};

using XpcHolder = std::unique_ptr<
	std::remove_pointer_t<xpc_object_t>,
	XpcReleaser>;

struct State {
	std::mutex mutex;
	std::condition_variable changed;
	XpcHolder reply;
	bool interrupted = false;
};

class Service final {
public:
	explicit Service(xpc_connection_t connection);
	~Service();

	[[nodiscard]] xpc_connection_t connection() const;
	[[nodiscard]] pid_t pid() const;
	[[nodiscard]] QString watch(pid_t pid);
	[[nodiscard]] std::optional<Completion> waitFor(crl::time timeout);
	void cancel();

private:
	const xpc_connection_t _connection = nullptr;
	int _queue = -1;
	pid_t _pid = 0;
	bool _canceled = false;
	bool _ended = false;

};

[[nodiscard]] QString EndedEarly() {
	return u"the widget helper ended before it took the channel"_q;
}

void XpcReleaser::operator()(xpc_object_t value) const {
	xpc_release(value);
}

Service::Service(xpc_connection_t connection) : _connection(connection) {
}

Service::~Service() {
	cancel();
	xpc_release(_connection);
	if (_queue >= 0) {
		close(_queue);
	}
}

xpc_connection_t Service::connection() const {
	return _connection;
}

pid_t Service::pid() const {
	return _pid;
}

QString Service::watch(pid_t pid) {
	Expects(pid > 0 && _queue < 0);

	_queue = kqueue();
	if (_queue < 0) {
		return SystemError("kqueue", errno);
	}
	struct kevent change = {};
	EV_SET(&change, pid, EVFILT_PROC, EV_ADD, NOTE_EXIT, 0, nullptr);
	if (kevent(_queue, &change, 1, nullptr, 0, nullptr) != 0) {
		const auto error = errno;
		return (error == ESRCH) ? EndedEarly() : SystemError("kevent", error);
	}
	_pid = pid;
	return QString();
}

std::optional<Completion> Service::waitFor(crl::time timeout) {
	Expects(_pid > 0);

	const auto deadline = crl::now() + timeout;
	while (!_ended) {
		const auto left = std::max(deadline - crl::now(), crl::time(0));
		const auto wait = timespec{
			.tv_sec = time_t(left / 1000),
			.tv_nsec = long(left % 1000) * 1000000,
		};
		struct kevent event = {};
		const auto result = kevent(_queue, nullptr, 0, &event, 1, &wait);
		const auto error = (result < 0) ? errno : 0;
		if (result > 0) {
			_ended = true;
		} else if (!result) {
			return std::nullopt;
		} else if (error != EINTR) {
			return Completion{ .detail = SystemError("kevent", error) };
		}
	}
	return Completion{ .detail = u"ended"_q, .reaped = true };
}

void Service::cancel() {
	if (!std::exchange(_canceled, true)) {
		xpc_connection_cancel(_connection);
	}
}

[[nodiscard]] QString MainBundleIdentifier() {
	@autoreleasepool {
		const auto identifier = [[NSBundle mainBundle] bundleIdentifier];
		return identifier ? QString::fromNSString(identifier) : QString();
	}
}

[[nodiscard]] XpcHolder Send(
		xpc_connection_t connection,
		XpcHolder message,
		std::shared_ptr<State> state,
		crl::time deadline) {
	xpc_connection_send_message_with_reply(
		connection,
		message.get(),
		nullptr,
		^(xpc_object_t reply) {
			const auto lock = std::lock_guard(state->mutex);
			state->reply = XpcHolder(xpc_retain(reply));
			state->changed.notify_all();
		});
	message = nullptr;
	const auto left = std::max(deadline - crl::now(), crl::time(0));
	auto lock = std::unique_lock(state->mutex);
	state->changed.wait_for(lock, std::chrono::milliseconds(left), [&] {
		return state->reply != nullptr;
	});
	return base::take(state->reply);
}

[[nodiscard]] API_AVAILABLE(macos(12.0))
QString DescribeError(xpc_connection_t connection, xpc_object_t error) {
	const auto description = xpc_dictionary_get_string(
		error,
		XPC_ERROR_KEY_DESCRIPTION);
	auto result = description
		? QString::fromUtf8(description)
		: u"unknown error"_q;
	if (const auto reason = xpc_connection_copy_invalidation_reason(
			connection)) {
		result += u" (%1)"_q.arg(QString::fromUtf8(reason));
		free(reason);
	}
	return result;
}

[[nodiscard]] API_AVAILABLE(macos(12.0))
QString ReplyError(
		xpc_connection_t connection,
		xpc_object_t reply,
		const QString &check,
		const QString &name) {
	if (!reply) {
		return u"the widget helper did not answer within %1 ms"_q.arg(
			kStartWait);
	} else if (reply == XPC_ERROR_PEER_CODE_SIGNING_REQUIREMENT) {
		return u"the widget helper failed the %1 check: %2"_q.arg(
			check,
			DescribeError(connection, reply));
	} else if (xpc_get_type(reply) != XPC_TYPE_DICTIONARY) {
		return u"the widget helper service %1 is unavailable (%2)"_q.arg(
			name,
			DescribeError(connection, reply));
	} else if (const auto refusal = xpc_dictionary_get_string(reply, "error")) {
		return u"the widget helper refused the channel: %1"_q.arg(
			QString::fromUtf8(refusal));
	}
	return QString();
}

[[nodiscard]] XpcHolder ChannelMessage(
		int channel,
		int errorOutput,
		const QStringList &arguments) {
	auto result = XpcHolder(xpc_dictionary_create(nullptr, nullptr, 0));
	xpc_dictionary_set_fd(result.get(), "channel", channel);
	xpc_dictionary_set_fd(result.get(), "stderr", errorOutput);
	if (!arguments.isEmpty()) {
		const auto list = XpcHolder(xpc_array_create(nullptr, 0));
		for (const auto &argument : arguments) {
			xpc_array_set_string(
				list.get(),
				XPC_ARRAY_APPEND,
				argument.toUtf8().constData());
		}
		xpc_dictionary_set_value(result.get(), "arguments", list.get());
	}
	return result;
}

[[nodiscard]] bool Replaced(Service &service, State &state) {
	const auto exited = service.waitFor(0);
	const auto lock = std::lock_guard(state.mutex);
	return exited
		|| state.interrupted
		|| (xpc_connection_get_pid(service.connection()) != service.pid());
}

[[nodiscard]] Completion Complete(
		std::unique_ptr<Service> service,
		crl::time exitWait,
		crl::time killWait) {
	Expects(service != nullptr);

	const auto started = crl::now();
	auto exited = service->waitFor(exitWait);
	if (exited && exited->reaped) {
		return std::move(*exited);
	}
	const auto canceled = crl::now();
	service->cancel();
	auto result = service->waitFor(killWait);
	if (!result) {
		result = Completion{
			.detail = u"did not end within %1 ms of its connection "
				"being canceled"_q.arg(crl::now() - canceled),
		};
	} else if (result->reaped) {
		result->detail = u"ended after its connection was canceled "
			"after %1 ms"_q.arg(canceled - started);
	}
	if (exited) {
		result->detail = exited->detail + u", "_q + result->detail;
	}
	return std::move(*result);
}

[[nodiscard]] QString Abandon(
		std::unique_ptr<Service> service,
		const QString &error) {
	if (!service->pid()) {
		return error;
	}
	const auto cleanup = Complete(std::move(service), 0, kAbandonWait);
	return u"%1 (%2)"_q.arg(error, cleanup.detail);
}

[[nodiscard]] API_AVAILABLE(macos(12.0))
Launched LaunchService(const QStringList &arguments) {
	if (fcntl(STDERR_FILENO, F_GETFD) < 0) {
		return { .error = u"stderr is not open"_q };
	}
	const auto bundle = MainBundleIdentifier();
	if (bundle.isEmpty()) {
		return { .error = u"the bundle identifier of this build is unknown"_q };
	}
	const auto name = bundle + QString::fromLatin1(kServiceSuffix);
	const auto path = details::BundledPath(
		"Contents/XPCServices/TelegramWidgetHelper.xpc");
	if (path.isEmpty()) {
		return { .error = u"the widget helper path is unknown"_q };
	}
	auto requirement = details::HelperRequirementText(name, path);
	if (!requirement.error.isEmpty()) {
		return { .error = std::move(requirement.error) };
	}
	auto channel = CreatePosixChannel();
	if (!channel.error.empty()) {
		return { .error = QString::fromStdString(channel.error) };
	}
	auto output = PrepareHelperStderr(STDERR_FILENO + 1);
	if (!output.error.empty()) {
		return { .error = QString::fromStdString(output.error) };
	}
	const auto closeOutput = gsl::finally([descriptor = output.descriptor] {
		close(descriptor);
	});

	const auto state = std::make_shared<State>();
	const auto connection = xpc_connection_create(
		name.toUtf8().constData(),
		nullptr);
	const auto required = xpc_connection_set_peer_code_signing_requirement(
		connection,
		requirement.text.toUtf8().constData());
	xpc_connection_set_event_handler(connection, ^(xpc_object_t event) {
		if (event == XPC_ERROR_CONNECTION_INTERRUPTED) {
			const auto lock = std::lock_guard(state->mutex);
			state->interrupted = true;
		}
	});
	xpc_connection_activate(connection);
	auto service = std::make_unique<Service>(connection);
	if (required) {
		return {
			.error = SystemError(
				"xpc_connection_set_peer_code_signing_requirement",
				required),
		};
	}

	const auto deadline = crl::now() + kStartWait;
	const auto hello = Send(
		connection,
		XpcHolder(xpc_dictionary_create(nullptr, nullptr, 0)),
		state,
		deadline);
	const auto greeted = ReplyError(
		connection,
		hello.get(),
		requirement.check,
		name);
	if (!greeted.isEmpty()) {
		return { .error = Abandon(std::move(service), greeted) };
	}
	const auto pid = xpc_connection_get_pid(connection);
	const auto watched = (pid > 0) ? service->watch(pid) : EndedEarly();
	if (!watched.isEmpty()) {
		return { .error = Abandon(std::move(service), watched) };
	}

	auto message = ChannelMessage(
		channel.child->descriptor(),
		output.descriptor,
		arguments);
	channel.child = nullptr;
	const auto reply = Send(connection, std::move(message), state, deadline);
	auto refused = ReplyError(
		connection,
		reply.get(),
		requirement.check,
		name);
	if (reply.get() == XPC_ERROR_CONNECTION_INTERRUPTED
		|| (refused.isEmpty() && Replaced(*service, *state))) {
		refused = EndedEarly();
	}
	if (!refused.isEmpty()) {
		return { .error = Abandon(std::move(service), refused) };
	}

	const auto keep = XpcHolder(xpc_dictionary_create(nullptr, nullptr, 0));
	xpc_connection_send_message_with_reply(
		connection,
		keep.get(),
		nullptr,
		^(xpc_object_t) {
		});
	return {
		.stream = std::move(channel.parent),
		.complete = [
			service = std::move(service),
			relay = std::move(output.relay)
		](crl::time exitWait, crl::time killWait) mutable {
			auto completion = Complete(std::move(service), exitWait, killWait);
			if (relay && completion.reaped) {
				// macOS discards the queued bytes once the end is shut.
				relay->drain(kRelayEndWait);
			}
			relay = nullptr;
			return completion;
		},
		.pid = pid,
	};
}

} // namespace

Launched Launch(const QStringList &arguments) {
	if (@available(macOS 12.0, *)) {
		return LaunchService(arguments);
	}
	return { .error = UnsupportedReason() };
}

} // namespace Platform::WidgetHelper
